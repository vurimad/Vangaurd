#include <vanguard/editor/editor_model_import_session.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/textures/textures.hpp>

#include <algorithm>
#include <cstring>
#include <new>

namespace vanguard::editor
{
    namespace
    {
        struct ModelInspectionWork final
        {
            explicit ModelInspectionWork(const filesystem::AbsolutePath& path, const u64 maximum) noexcept
                : source(path), bytes(memory::pools::Tools::GetInstance()), maximumBytes(maximum)
            {
            }

            concurrency::Atomic<u32> references{1u};
            concurrency::Atomic<u32> completed{0u};
            concurrency::Atomic<u32> cancelRequested{0u};
            concurrency::Atomic<u32> progressPermille{0u};
            filesystem::AbsolutePath source;
            containers::DynamicArray<u8> bytes;
            mesh_tools::ModelImportManifest manifest;
            mesh_tools::ModelImportReport report;
            mesh_tools::ModelImportResult result = mesh_tools::ModelImportResult::ImportFailure;
            u64 maximumBytes = 0u;
        };

        void RetainInspection(ModelInspectionWork* const work) noexcept
        {
            if (work != nullptr)
                static_cast<void>(work->references.Increment());
        }

        void ReleaseInspection(ModelInspectionWork* const work) noexcept
        {
            if (work == nullptr || work->references.Decrement() != 0u)
                return;
            work->~ModelInspectionWork();
            memory::MemoryBlock block{work, sizeof(ModelInspectionWork), memory::PoolId::Tools};
            memory::Free(block);
        }

        [[nodiscard]] bool InspectionProgress(const f32 progress, void* const userData) noexcept
        {
            auto* const work = static_cast<ModelInspectionWork*>(userData);
            if (work == nullptr)
                return false;
            const f32 clamped = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
            work->progressPermille.SetValue(static_cast<u32>(clamped * 1000.0f));
            return work->cancelRequested.GetValue() == 0u;
        }

        [[nodiscard]] bool ReadSourceSnapshot(ModelInspectionWork& work) noexcept
        {
            filesystem::Manager& files = filesystem::GetManager();
            if (!work.source.IsFilePath() || !files.FileExist(work.source))
            {
                work.result = mesh_tools::ModelImportResult::SourceNotFound;
                return false;
            }
            const u64 size = files.GetFileSize(work.source);
            if (size == 0u || size > work.maximumBytes || size > ~u32{0})
            {
                work.result = mesh_tools::ModelImportResult::SourceTooLarge;
                return false;
            }
            auto file = files.CreateFileReader(work.source, filesystem::FOF_Buffered);
            if (!file)
            {
                work.result = mesh_tools::ModelImportResult::SourceNotFound;
                return false;
            }
            work.bytes.Resize(static_cast<u32>(size));
            if (work.bytes.Size() != size)
            {
                work.result = mesh_tools::ModelImportResult::LimitExceeded;
                return false;
            }
            file->Serialize(work.bytes.Data(), work.bytes.Size());
            if (file->HasErrors())
            {
                work.bytes.Clear();
                work.result = mesh_tools::ModelImportResult::ImportFailure;
                return false;
            }
            return true;
        }

        void RunInspection(ModelInspectionWork* const work) noexcept
        {
            if (work == nullptr)
                return;
            if (work->cancelRequested.GetValue() != 0u)
                work->result = mesh_tools::ModelImportResult::Cancelled;
            else if (ReadSourceSnapshot(*work))
            {
                mesh_tools::ModelImportOptions options;
                options.progress = InspectionProgress;
                options.progressUserData = work;
                mesh_tools::ModelImportLimits limits;
                limits.maximumSourceBytes = work->maximumBytes;
                const containers::StringView extension = filesystem::paths::GetExtension(work->source.AsStringView());
                work->result = mesh_tools::InspectModelMemory(
                    work->bytes.Data(), work->bytes.Size(), extension, work->source.AsStringView(),
                    options, limits, work->manifest, &work->report);
            }
            work->progressPermille.SetValue(1000u);
            work->completed.SetValue(1u);
            ReleaseInspection(work);
        }

        [[nodiscard]] ModelInspectionWork* CreateInspection(const filesystem::AbsolutePath& source,
                                                            const u64 maximumBytes,
                                                            const containers::ArraySpan<const u8> sourceSnapshot = {}) noexcept
        {
            const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(ModelInspectionWork), alignof(ModelInspectionWork));
            if (!block)
                return nullptr;
            auto* const work = ::new (block.address) ModelInspectionWork(source, maximumBytes);
            if (!sourceSnapshot.Empty())
            {
                if (sourceSnapshot.Data() == nullptr || sourceSnapshot.Count() > maximumBytes || sourceSnapshot.Count() > ~u32{0})
                {
                    work->~ModelInspectionWork();
                    memory::MemoryBlock released{work, sizeof(ModelInspectionWork), memory::PoolId::Tools};
                    memory::Free(released);
                    return nullptr;
                }
                work->bytes.Resize(static_cast<u32>(sourceSnapshot.Count()));
                if (work->bytes.Size() != sourceSnapshot.Count())
                {
                    work->~ModelInspectionWork();
                    memory::MemoryBlock released{work, sizeof(ModelInspectionWork), memory::PoolId::Tools};
                    memory::Free(released);
                    return nullptr;
                }
                std::memcpy(work->bytes.Data(), sourceSnapshot.Data(), sourceSnapshot.Count());
            }
            return work;
        }

        [[nodiscard]] bool IsCommittedResult(const asset_streaming::ProjectAssetPipelineResult result) noexcept
        {
            return result == asset_streaming::ProjectAssetPipelineResult::Success ||
                   result == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                   result == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        }

        [[nodiscard]] bool IsPublicationReady(const asset_streaming::ProjectAssetDeliveryStageState state) noexcept
        {
            return state == asset_streaming::ProjectAssetDeliveryStageState::Installed ||
                   state == asset_streaming::ProjectAssetDeliveryStageState::Current;
        }

        [[nodiscard]] bool IsBuildCommitted(const asset_streaming::ProjectAssetDeliveryStageState state) noexcept
        {
            return state == asset_streaming::ProjectAssetDeliveryStageState::Committed ||
                   state == asset_streaming::ProjectAssetDeliveryStageState::Installed ||
                   state == asset_streaming::ProjectAssetDeliveryStageState::Current;
        }

        [[nodiscard]] mesh_tools::ModelImportTextureResolution* FindTextureDecision(
            mesh_tools::ModelImportSettings& settings, const containers::String& authoredPath) noexcept
        {
            for (mesh_tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
                if (resolution.authoredPath == authoredPath)
                    return &resolution;
            return nullptr;
        }
    } // namespace

    struct EditorModelImportSession::Impl final
    {
        Impl() noexcept
            : sourceBytes(memory::pools::Tools::GetInstance()), outputs(memory::pools::Tools::GetInstance())
        {
        }

        filesystem::AbsolutePath sourcePath;
        containers::String rootRelativeFolder;
        containers::String sourceFileName;
        containers::DynamicArray<u8> sourceBytes;
        mesh_tools::ModelImportManifest manifest;
        mesh_tools::ModelImportReport inspectionReport;
        mesh_tools::ModelImportSettings settings;
        mesh_tools::ModelImportPlan plan;
        mesh_tools::ModelImportPlanReport planReport;
        containers::DynamicArray<resources::ResourceReference> outputs;
        ModelInspectionWork* inspection = nullptr;
        assets::AssetId existingAsset;
        assets::AssetId plannedAsset;
        assets::AssetId committedAsset;
        u64 expectedAcceptedGeneration = 0u;
        u64 expectedSourceGeneration = 0u;
        u64 committedAcceptedGeneration = 0u;
        u32 destinationRoot = 0u;
        EditorModelImportSessionPhase phase = EditorModelImportSessionPhase::Empty;
        mesh_tools::ModelImportResult inspectionResult = mesh_tools::ModelImportResult::Success;
        asset_streaming::ProjectAssetPipelineResult pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
        u32 progressPermille = 0u;
        asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase transactionPreparePhase =
            asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase::None;
        u64 transactionPreparedBytes = 0u;
        u64 transactionTotalBytes = 0u;
        u32 commitRetryDelayFrames = 0u;
        u32 commitAttempts = 0u;
        u32 stalePlanSite = 0u;
        u32 readyOutputs = 0u;
        u32 builtOutputs = 0u;
        u32 failedOutputs = 0u;
        u32 retryOutputs = 0u;
        asset_streaming::ProjectAssetAutomaticRecookState automaticState =
            asset_streaming::ProjectAssetAutomaticRecookState::Disabled;
        u32 buildOperations = 0u;
        u32 completedBuildOperations = 0u;
        resources::ResourceReference publicationRetryOutput;
        assets::ProjectAssetBuildResult cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
        bool cohortAdmissionBuildFailed = false;
        bool durableCommitted = false;
        bool successorTruthValidated = false;
        bool commitQueued = false;

        [[nodiscard]] bool HasUnfinishedDurableImport() const noexcept
        {
            return durableCommitted && phase != EditorModelImportSessionPhase::Ready;
        }

        void AbandonInspection() noexcept
        {
            if (inspection == nullptr)
                return;
            if (inspection->completed.GetValue() == 0u)
                inspection->cancelRequested.SetValue(1u);
            ReleaseInspection(inspection);
            inspection = nullptr;
        }

        void ClearTransient() noexcept
        {
            AbandonInspection();
            sourcePath = {};
            rootRelativeFolder.Clear();
            sourceFileName.Clear();
            sourceBytes.Clear();
            manifest.Clear();
            inspectionReport = {};
            settings = {};
            plan = {};
            planReport = {};
            outputs.Clear();
            existingAsset = {};
            plannedAsset = {};
            committedAsset = {};
            expectedAcceptedGeneration = 0u;
            expectedSourceGeneration = 0u;
            committedAcceptedGeneration = 0u;
            destinationRoot = 0u;
            phase = EditorModelImportSessionPhase::Empty;
            inspectionResult = mesh_tools::ModelImportResult::Success;
            pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
            progressPermille = 0u;
            transactionPreparePhase = asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase::None;
            transactionPreparedBytes = 0u;
            transactionTotalBytes = 0u;
            commitRetryDelayFrames = 0u;
            commitAttempts = 0u;
            stalePlanSite = 0u;
            commitQueued = false;
            readyOutputs = 0u;
            builtOutputs = 0u;
            failedOutputs = 0u;
            retryOutputs = 0u;
            automaticState = asset_streaming::ProjectAssetAutomaticRecookState::Disabled;
            buildOperations = 0u;
            completedBuildOperations = 0u;
            publicationRetryOutput = {};
            cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
            cohortAdmissionBuildFailed = false;
            durableCommitted = false;
            successorTruthValidated = false;
        }

        [[nodiscard]] bool ReviewedPlanMatchesTransientState() const noexcept
        {
            if (sourceBytes.Empty() || plan.GetWitnesses().sourceFingerprint.IsEmpty() ||
                crypto::Sha256(sourceBytes.TypedData(), sourceBytes.Size()) != plan.GetWitnesses().sourceFingerprint)
                return false;
            containers::DynamicArray<u8> candidateBytes(memory::pools::Assets::GetInstance());
            if (mesh_tools::EncodeModelImportSettings(settings, candidateBytes) != mesh_tools::ModelImportSettingsResult::Success)
                return false;
            return !plan.GetWitnesses().candidateSettingsFingerprint.IsEmpty() &&
                   crypto::Sha256(candidateBytes.TypedData(), candidateBytes.Size()) ==
                       plan.GetWitnesses().candidateSettingsFingerprint;
        }

        [[nodiscard]] bool DispatchInspection(const EditorProjectAssetAccess& assets,
                                              const containers::ArraySpan<const u8> sourceSnapshot = {}) noexcept
        {
            const u64 projectMaximum = assets.GetMaximumExternalImportSourceBytes();
            const u64 maximumBytes = projectMaximum != 0u
                                         ? std::min<u64>(projectMaximum, mesh_tools::ModelImportLimits{}.maximumSourceBytes)
                                         : mesh_tools::ModelImportLimits{}.maximumSourceBytes;
            inspection = CreateInspection(sourcePath, maximumBytes, sourceSnapshot);
            if (inspection == nullptr)
                return false;
            RetainInspection(inspection);
            jobs::Task task = jobs::Task::Create([work = inspection](const jobs::JobContext&) noexcept
            {
                if (work->bytes.Empty())
                    RunInspection(work);
                else
                {
                    if (work->cancelRequested.GetValue() != 0u)
                        work->result = mesh_tools::ModelImportResult::Cancelled;
                    else
                    {
                        mesh_tools::ModelImportOptions options;
                        options.progress = InspectionProgress;
                        options.progressUserData = work;
                        mesh_tools::ModelImportLimits limits;
                        limits.maximumSourceBytes = work->maximumBytes;
                        const containers::StringView extension = filesystem::paths::GetExtension(work->source.AsStringView());
                        work->result = mesh_tools::InspectModelMemory(
                            work->bytes.Data(), work->bytes.Size(), extension, work->source.AsStringView(),
                            options, limits, work->manifest, &work->report);
                    }
                    work->progressPermille.SetValue(1000u);
                    work->completed.SetValue(1u);
                    ReleaseInspection(work);
                }
            });
            static jobs::JobName inspectionName{"Editor.ModelImport.Inspect"};
            jobs::Builder builder({jobs::Priority::CriticalPath, jobs::Affinity::AnyWorker}, inspection);
            if (!builder.IsValid() || !task || !builder.Dispatch(inspectionName, static_cast<jobs::Task&&>(task)))
            {
                ReleaseInspection(inspection);
                AbandonInspection();
                return false;
            }
            phase = EditorModelImportSessionPhase::Inspecting;
            return true;
        }

        [[nodiscard]] bool CaptureCommittedOutputs(const EditorProjectAssetAccess& assets) noexcept
        {
            outputs.Clear();
            committedAcceptedGeneration = 0u;
            if (!successorTruthValidated || !committedAsset.IsValid() ||
                !(plan.GetWitnesses().parentAsset == committedAsset) ||
                !assets.GetAcceptedAssetGeneration(committedAsset, committedAcceptedGeneration) ||
                (existingAsset.IsValid() && committedAcceptedGeneration <= expectedAcceptedGeneration))
                return false;
            const containers::ArraySpan<const assets::AssetOutput> plannedOutputs = plan.GetImportDescription().outputs;
            u32 available = 0u;
            bool truncated = false;
            static_cast<void>(assets.CopyAssetOutputs(
                committedAsset, 0u, containers::ArraySpan<resources::ResourceReference>{}, available, truncated));
            if (truncated || available == 0u || available != plannedOutputs.Size())
                return false;
            outputs.Resize(available);
            if (outputs.Size() != available)
                return false;
            u32 observedAvailable = 0u;
            bool observedTruncated = false;
            const u32 copied = assets.CopyAssetOutputs(
                committedAsset, 0u, {outputs.TypedData(), outputs.Size()}, observedAvailable, observedTruncated);
            if (copied != available || observedAvailable != available || observedTruncated)
                return false;
            for (const assets::AssetOutput& plannedOutput : plannedOutputs)
            {
                const resources::ResourceReference expected = assets::GetAssetOutputReference(committedAsset, plannedOutput);
                bool found = false;
                for (const resources::ResourceReference observed : outputs)
                    found = found || observed == expected;
                if (!expected.IsValid() || !expected.IsTyped() || !found)
                    return false;
            }
            return true;
        }

        void PollBuildTruth(const EditorProjectAssetAccess& assets) noexcept
        {
            if (!durableCommitted || outputs.Empty())
                return;
            readyOutputs = 0u;
            builtOutputs = 0u;
            failedOutputs = 0u;
            retryOutputs = 0u;
            publicationRetryOutput = {};
            for (const resources::ResourceReference output : outputs)
            {
                asset_streaming::ProjectAssetOutputDeliveryStatus delivery;
                if (!assets.GetOutputDeliveryStatus(output, delivery) || delivery.authoredGeneration != committedAcceptedGeneration)
                    continue;
                if (delivery.publicationRetryRequired || delivery.residentRetryRequired)
                    ++retryOutputs;
                if (delivery.publicationRetryRequired && !publicationRetryOutput.IsValid())
                    publicationRetryOutput = output;
                if (delivery.buildState == asset_streaming::ProjectAssetDeliveryStageState::Failed ||
                    delivery.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Failed)
                    ++failedOutputs;
                if (IsBuildCommitted(delivery.buildState))
                    ++builtOutputs;
                if (IsBuildCommitted(delivery.buildState) && IsPublicationReady(delivery.publicationState))
                    ++readyOutputs;
            }
            const asset_streaming::ProjectAssetAutomaticRecookStatus automatic = assets.GetAutomaticRecookStatus();
            automaticState = automatic.state;
            buildOperations = automatic.buildOperations;
            completedBuildOperations = automatic.completedBuildOperations;
            if (readyOutputs == outputs.Size())
            {
                phase = EditorModelImportSessionPhase::Ready;
                pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
                return;
            }
            if (failedOutputs != 0u || automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed ||
                automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
            {
                phase = HasAutomaticRecookDirtyReason(automatic.dirtyReasons,
                                                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch)
                            ? EditorModelImportSessionPhase::Cancelled
                            : EditorModelImportSessionPhase::BuildFailed;
                return;
            }
            if (automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Cancelling)
                phase = EditorModelImportSessionPhase::Cancelling;
            else if (automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Planning ||
                     automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Building ||
                     automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Validating ||
                     automatic.activeRoots != 0u || automatic.completedRoots != 0u || builtOutputs != 0u)
                phase = EditorModelImportSessionPhase::Compiling;
            else
                phase = EditorModelImportSessionPhase::Imported;
        }
    };

    EditorModelImportSession::EditorModelImportSession() noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = ::new (block.address) Impl();
    }

    EditorModelImportSession::~EditorModelImportSession()
    {
        if (m_impl == nullptr)
            return;
        m_impl->AbandonInspection();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Tools};
        memory::Free(block);
        m_impl = nullptr;
    }

    bool EditorModelImportSession::BeginExternalSource(const filesystem::AbsolutePath& source,
                                                       const u32 destinationRoot,
                                                       const containers::StringView rootRelativeFolder,
                                                       const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr || m_impl->HasUnfinishedDurableImport() || !source.IsFilePath() ||
            assets.IsManagedProjectSourcePath(source))
            return false;
        m_impl->ClearTransient();
        m_impl->sourcePath = source;
        m_impl->destinationRoot = destinationRoot;
        m_impl->rootRelativeFolder = containers::String(
            rootRelativeFolder.Data(), static_cast<u32>(rootRelativeFolder.Length()));
        const containers::StringView sourceFileName = filesystem::paths::GetFileName(source.AsStringView());
        m_impl->sourceFileName = containers::String(sourceFileName.Data(), static_cast<u32>(sourceFileName.Length()));
        if (m_impl->sourceFileName.Empty() || !assets.CanImportExternalSource(destinationRoot, rootRelativeFolder) ||
            !m_impl->DispatchInspection(assets))
        {
            m_impl->phase = EditorModelImportSessionPhase::Failed;
            return false;
        }
        return true;
    }

    bool EditorModelImportSession::BeginExternalSnapshot(const filesystem::AbsolutePath& source,
                                                         const containers::ArraySpan<const u8> sourceSnapshot,
                                                         const u32 destinationRoot,
                                                         const containers::StringView rootRelativeFolder,
                                                         const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr || m_impl->HasUnfinishedDurableImport() || !source.IsFilePath() ||
            sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr || assets.IsManagedProjectSourcePath(source))
            return false;
        m_impl->ClearTransient();
        m_impl->sourcePath = source;
        m_impl->destinationRoot = destinationRoot;
        m_impl->rootRelativeFolder = containers::String(
            rootRelativeFolder.Data(), static_cast<u32>(rootRelativeFolder.Length()));
        const containers::StringView sourceFileName = filesystem::paths::GetFileName(source.AsStringView());
        m_impl->sourceFileName = containers::String(sourceFileName.Data(), static_cast<u32>(sourceFileName.Length()));
        if (m_impl->sourceFileName.Empty() || !assets.CanImportExternalSource(destinationRoot, rootRelativeFolder) ||
            !m_impl->DispatchInspection(assets, sourceSnapshot))
        {
            m_impl->phase = EditorModelImportSessionPhase::Failed;
            return false;
        }
        return true;
    }

    bool EditorModelImportSession::BeginExistingSource(const filesystem::AbsolutePath& source,
                                                       const assets::AssetId asset,
                                                       const u64 expectedAcceptedGeneration,
                                                       const u64 expectedSourceGeneration,
                                                       const mesh_tools::ModelImportSettings& settings,
                                                       const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr || m_impl->HasUnfinishedDurableImport() || !source.IsFilePath() || !asset.IsValid() ||
            expectedAcceptedGeneration == 0u ||
            expectedSourceGeneration == 0u)
            return false;
        m_impl->ClearTransient();
        m_impl->sourcePath = source;
        const containers::StringView sourceFileName = filesystem::paths::GetFileName(source.AsStringView());
        m_impl->sourceFileName = containers::String(sourceFileName.Data(), static_cast<u32>(sourceFileName.Length()));
        m_impl->existingAsset = asset;
        m_impl->expectedAcceptedGeneration = expectedAcceptedGeneration;
        m_impl->expectedSourceGeneration = expectedSourceGeneration;
        m_impl->settings = settings;
        if (!m_impl->DispatchInspection(assets))
        {
            m_impl->phase = EditorModelImportSessionPhase::Failed;
            return false;
        }
        return true;
    }

    bool EditorModelImportSession::Reinspect(const u64 expectedAcceptedGeneration,
                                             const u64 expectedSourceGeneration,
                                             const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr || m_impl->durableCommitted || !m_impl->sourcePath.IsFilePath() ||
            m_impl->phase == EditorModelImportSessionPhase::Inspecting)
            return false;
        if (m_impl->existingAsset.IsValid())
        {
            if (expectedAcceptedGeneration == 0u || expectedSourceGeneration == 0u)
                return false;
            m_impl->expectedAcceptedGeneration = expectedAcceptedGeneration;
            m_impl->expectedSourceGeneration = expectedSourceGeneration;
        }
        else if (expectedAcceptedGeneration != 0u || expectedSourceGeneration != 0u)
            return false;

        m_impl->AbandonInspection();
        m_impl->sourceBytes.Clear();
        m_impl->manifest.Clear();
        m_impl->inspectionReport = {};
        m_impl->plan = {};
        m_impl->planReport = {};
        m_impl->commitQueued = false;
        m_impl->commitRetryDelayFrames = 0u;
        m_impl->stalePlanSite = 0u;
        if (m_impl->existingAsset.IsValid())
            m_impl->plannedAsset = {};
        m_impl->inspectionResult = mesh_tools::ModelImportResult::Success;
        m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
        m_impl->progressPermille = 0u;
        if (!m_impl->DispatchInspection(assets))
        {
            m_impl->phase = EditorModelImportSessionPhase::Failed;
            return false;
        }
        return true;
    }

    void EditorModelImportSession::Poll(const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr)
            return;
        if (m_impl->phase == EditorModelImportSessionPhase::PreparingTransaction &&
            !m_impl->durableCommitted && m_impl->plannedAsset.IsValid())
        {
            // Owner-frame transaction progress is process-local. The worker
            // owns only Prepare; the Project Workspace owner owns durability.
            // A submitted review never re-enters CommitModelImportPlan here.
            const auto deferred = assets.GetDeferredModelImportStatus();
            if (deferred.asset == m_impl->plannedAsset)
            {
                m_impl->transactionPreparePhase = deferred.preparePhase;
                m_impl->transactionPreparedBytes = deferred.prepareCompletedBytes;
                m_impl->transactionTotalBytes = deferred.prepareTotalBytes;
                if (deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::AwaitingReconciliation ||
                    deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::CohortQueued ||
                    (deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed &&
                     deferred.acceptedGeneration != 0u))
                {
                    m_impl->durableCommitted = true;
                    m_impl->committedAsset = m_impl->plannedAsset;
                    m_impl->successorTruthValidated = false;
                    m_impl->phase = EditorModelImportSessionPhase::ReconciliationPending;
                    m_impl->pipelineResult = deferred.result;
                    m_impl->transactionPreparePhase = asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase::None;
                }
                else if (deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed)
                {
                    m_impl->pipelineResult = deferred.result;
                    m_impl->phase = deferred.result == asset_streaming::ProjectAssetPipelineResult::StalePlan
                                        ? EditorModelImportSessionPhase::StalePlan
                                        : EditorModelImportSessionPhase::Failed;
                    m_impl->transactionPreparePhase = asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase::None;
                }
            }
        }
        if (m_impl->phase == EditorModelImportSessionPhase::Inspecting && m_impl->inspection != nullptr)
        {
            m_impl->progressPermille = m_impl->inspection->progressPermille.GetValue();
            if (m_impl->inspection->completed.GetValue() != 0u)
            {
                ModelInspectionWork* const work = m_impl->inspection;
                m_impl->inspectionResult = work->result;
                m_impl->progressPermille = 1000u;
                m_impl->sourceBytes = static_cast<containers::DynamicArray<u8>&&>(work->bytes);
                m_impl->manifest = static_cast<mesh_tools::ModelImportManifest&&>(work->manifest);
                m_impl->inspectionReport = static_cast<mesh_tools::ModelImportReport&&>(work->report);
                m_impl->inspection = nullptr;
                ReleaseInspection(work);
                if (m_impl->inspectionResult == mesh_tools::ModelImportResult::Success)
                    static_cast<void>(Replan(assets));
                else
                    m_impl->phase = m_impl->inspectionResult == mesh_tools::ModelImportResult::Cancelled
                                        ? EditorModelImportSessionPhase::Cancelled
                                        : EditorModelImportSessionPhase::Failed;
            }
        }
        if (m_impl->durableCommitted &&
            m_impl->phase == EditorModelImportSessionPhase::ReconciliationPending &&
            m_impl->committedAsset.IsValid())
        {
            // A large first import can reconcile its durable source and admit
            // its complete cohort in Project Workspace owner frames, even when
            // this panel is hidden. Poll observes that exact one-shot result;
            // it must never issue another cohort request every UI frame.
            const asset_streaming::ProjectAssetDeferredPlannedImportStatus deferred =
                assets.GetDeferredModelImportStatus();
            if (deferred.asset == m_impl->committedAsset &&
                deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed)
            {
                m_impl->pipelineResult = deferred.result;
                if (deferred.result == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed)
                {
                    m_impl->cohortAdmissionBuildFailed = true;
                    m_impl->cohortAdmissionBuild = deferred.build;
                    m_impl->phase = EditorModelImportSessionPhase::BuildFailed;
                }
            }
            else if (deferred.asset == m_impl->committedAsset &&
                     deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::CohortQueued &&
                     deferred.acceptedGeneration != 0u)
            {
                asset_streaming::ProjectAssetPipelineFailure truthFailure;
                const auto exactTruth = assets.ResumeCommittedModelImport(
                    m_impl->plan, m_impl->committedAsset, m_impl->expectedAcceptedGeneration,
                    &truthFailure, true);
                if (exactTruth == asset_streaming::ProjectAssetPipelineResult::Success)
                {
                    m_impl->successorTruthValidated = true;
                    if (m_impl->CaptureCommittedOutputs(assets) &&
                        m_impl->committedAcceptedGeneration == deferred.acceptedGeneration)
                    {
                        m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
                        m_impl->phase = EditorModelImportSessionPhase::Compiling;
                    }
                    else
                    {
                        m_impl->successorTruthValidated = false;
                        m_impl->outputs.Clear();
                        m_impl->committedAcceptedGeneration = 0u;
                    }
                }
                else if (exactTruth == asset_streaming::ProjectAssetPipelineResult::StalePlan)
                {
                    m_impl->pipelineResult = exactTruth;
                    m_impl->cohortAdmissionBuildFailed = false;
                    m_impl->phase = EditorModelImportSessionPhase::BuildFailed;
                }
            }
        }
        if (m_impl->phase == EditorModelImportSessionPhase::Imported ||
            m_impl->phase == EditorModelImportSessionPhase::Compiling ||
            m_impl->phase == EditorModelImportSessionPhase::Cancelling ||
            m_impl->phase == EditorModelImportSessionPhase::BuildFailed ||
            m_impl->phase == EditorModelImportSessionPhase::Cancelled)
            m_impl->PollBuildTruth(assets);
        if (m_impl->commitQueued && m_impl->phase == EditorModelImportSessionPhase::Review)
        {
            // The UI deliberately retries only an explicitly approved, unchanged
            // review. Do not replan or spend 60 MiB+ source/hash/catalog work on
            // every render frame while a filesystem observer is catching up.
            if (m_impl->commitRetryDelayFrames != 0u)
                --m_impl->commitRetryDelayFrames;
            else if (assets.CanRetryReviewedModelCommit(m_impl->plan))
            {
                asset_streaming::ProjectAssetPipelineFailure retryFailure;
                static_cast<void>(CommitReviewedPlan(assets, &retryFailure));
            }
        }
    }

    bool EditorModelImportSession::Replan(const EditorProjectAssetAccess& assets) noexcept
    {
        if (m_impl == nullptr || m_impl->durableCommitted || m_impl->sourceBytes.Empty() ||
            m_impl->inspectionResult != mesh_tools::ModelImportResult::Success)
            return false;
        // A settings edit never inherits an outstanding approval to commit.
        m_impl->commitQueued = false;
        m_impl->commitRetryDelayFrames = 0u;
        m_impl->stalePlanSite = 0u;
        m_impl->plan = {};
        m_impl->planReport = {};
        const assets::AssetId provisionalAsset = m_impl->plannedAsset;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result = assets.PrepareModelImportPlan(
            m_impl->sourcePath, {m_impl->sourceBytes.TypedData(), m_impl->sourceBytes.Size()},
            m_impl->destinationRoot, m_impl->rootRelativeFolder, m_impl->sourceFileName,
            m_impl->existingAsset, m_impl->expectedAcceptedGeneration, m_impl->expectedSourceGeneration,
            m_impl->manifest, m_impl->settings, m_impl->plan, &m_impl->planReport, m_impl->plannedAsset, &failure);
        m_impl->pipelineResult = result;
        if (result == asset_streaming::ProjectAssetPipelineResult::Success)
        {
            m_impl->phase = EditorModelImportSessionPhase::Review;
            return true;
        }
        // A format/settings planning failure does not invalidate a previously
        // reviewed first-import parent identity. Keep it so correcting candidate
        // settings does not re-key every provisional logical child. StalePlan is
        // deliberately excluded: source/project truth may have invalidated the
        // provisional identity itself and Reinspect() must obtain fresh proof.
        if (!m_impl->existingAsset.IsValid() && provisionalAsset.IsValid() &&
            result != asset_streaming::ProjectAssetPipelineResult::StalePlan && !m_impl->plannedAsset.IsValid())
            m_impl->plannedAsset = provisionalAsset;
        if (result == asset_streaming::ProjectAssetPipelineResult::StalePlan && !m_impl->existingAsset.IsValid())
            m_impl->plannedAsset = {};
        if (result == asset_streaming::ProjectAssetPipelineResult::Busy)
            m_impl->phase = EditorModelImportSessionPhase::ProjectBusy;
        else if (result == asset_streaming::ProjectAssetPipelineResult::OperationFailed)
            m_impl->phase = EditorModelImportSessionPhase::ReviewInvalid;
        else
            m_impl->phase = result == asset_streaming::ProjectAssetPipelineResult::StalePlan
                                ? EditorModelImportSessionPhase::StalePlan
                                : EditorModelImportSessionPhase::Failed;
        return false;
    }

    bool EditorModelImportSession::QueueReviewedCommit() noexcept
    {
        if (m_impl == nullptr || m_impl->phase != EditorModelImportSessionPhase::Review ||
            m_impl->durableCommitted || m_impl->commitQueued || m_impl->sourceBytes.Empty() ||
            m_impl->plan.HasBlockingConflicts())
            return false;
        // Poll is invoked before drawing the panel. Two countdown ticks provide
        // at least one fully submitted frame showing the impending synchronous
        // durable cutover, including for high-frequency panels.
        m_impl->commitQueued = true;
        m_impl->commitRetryDelayFrames = 2u;
        m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::Pending;
        return true;
    }

    asset_streaming::ProjectAssetPipelineResult EditorModelImportSession::CommitReviewedPlan(
        const EditorProjectAssetAccess& assets, asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
    {
        // A queued review may be a 60 MiB+ source. Do not repeatedly hash it or
        // materialize its complete planned request while another coordinator
        // still owns admission. No mutation or approval is inferred from this
        // early Busy; the exact reviewed settings and source are revalidated on
        // the actual commit attempt once the project becomes eligible.
        if (m_impl != nullptr && m_impl->phase == EditorModelImportSessionPhase::Review &&
            !m_impl->plan.HasBlockingConflicts() && !m_impl->sourceBytes.Empty() &&
            !assets.CanRetryReviewedModelCommit(m_impl->plan))
        {
            if (m_impl->commitAttempts != ~u32{0})
                ++m_impl->commitAttempts;
            m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::Busy;
            m_impl->stalePlanSite = 0u;
            m_impl->commitQueued = true;
            m_impl->commitRetryDelayFrames = 30u;
            if (failure != nullptr)
            {
                *failure = {};
                failure->result = asset_streaming::ProjectAssetPipelineResult::Busy;
            }
            return asset_streaming::ProjectAssetPipelineResult::Busy;
        }
        if (m_impl == nullptr || m_impl->phase != EditorModelImportSessionPhase::Review ||
            m_impl->plan.HasBlockingConflicts() || m_impl->sourceBytes.Empty() ||
            !m_impl->ReviewedPlanMatchesTransientState())
        {
            if (m_impl != nullptr)
            {
                m_impl->commitQueued = false;
                m_impl->commitRetryDelayFrames = 0u;
                m_impl->stalePlanSite = 0u;
            }
            if (failure != nullptr)
            {
                *failure = {};
                failure->result = m_impl != nullptr && m_impl->phase == EditorModelImportSessionPhase::Review
                                      ? asset_streaming::ProjectAssetPipelineResult::StalePlan
                                      : asset_streaming::ProjectAssetPipelineResult::InvalidState;
            }
            if (m_impl != nullptr && m_impl->phase == EditorModelImportSessionPhase::Review)
            {
                m_impl->phase = EditorModelImportSessionPhase::StalePlan;
                if (!m_impl->existingAsset.IsValid())
                    m_impl->plannedAsset = {};
                return asset_streaming::ProjectAssetPipelineResult::StalePlan;
            }
            return asset_streaming::ProjectAssetPipelineResult::InvalidState;
        }
        assets::AssetId importedAsset;
        asset_streaming::ProjectAssetPipelineFailure commitFailure;
        if (m_impl->commitAttempts != ~u32{0})
            ++m_impl->commitAttempts;
        const asset_streaming::ProjectAssetPipelineResult result = assets.CommitModelImportPlan(
            m_impl->plan, {m_impl->sourceBytes.TypedData(), m_impl->sourceBytes.Size()}, importedAsset, &commitFailure);
        if (failure != nullptr)
            *failure = commitFailure;
        m_impl->pipelineResult = result;
        m_impl->stalePlanSite = commitFailure.stalePlanSite;
        if (result == asset_streaming::ProjectAssetPipelineResult::Pending &&
            !importedAsset.IsValid() && m_impl->plannedAsset.IsValid())
        {
            const auto deferred = assets.GetDeferredModelImportStatus();
            if (deferred.asset == m_impl->plannedAsset &&
                deferred.state == asset_streaming::ProjectAssetDeferredPlannedImportState::PreparingTransaction)
            {
                m_impl->commitQueued = false;
                m_impl->commitRetryDelayFrames = 0u;
                m_impl->phase = EditorModelImportSessionPhase::PreparingTransaction;
                m_impl->transactionPreparePhase = deferred.preparePhase;
                m_impl->transactionPreparedBytes = deferred.prepareCompletedBytes;
                m_impl->transactionTotalBytes = deferred.prepareTotalBytes;
                return result;
            }
        }
        if (!IsCommittedResult(result))
        {
            if (result == asset_streaming::ProjectAssetPipelineResult::Busy)
            {
                m_impl->phase = EditorModelImportSessionPhase::Review;
                m_impl->commitQueued = true;
                // Back off even if a watcher event arrives between the cheap
                // readiness check and transaction publication.
                m_impl->commitRetryDelayFrames = 30u;
            }
            else
            {
                m_impl->commitQueued = false;
                m_impl->commitRetryDelayFrames = 0u;
                if (result == asset_streaming::ProjectAssetPipelineResult::StalePlan && !m_impl->existingAsset.IsValid())
                    m_impl->plannedAsset = {};
                m_impl->phase = result == asset_streaming::ProjectAssetPipelineResult::StalePlan
                                    ? EditorModelImportSessionPhase::StalePlan
                                    : EditorModelImportSessionPhase::Failed;
            }
            return result;
        }
        m_impl->commitQueued = false;
        m_impl->commitRetryDelayFrames = 0u;
        m_impl->durableCommitted = true;
        m_impl->committedAsset = importedAsset.IsValid() ? importedAsset : m_impl->plannedAsset;
        m_impl->successorTruthValidated = result != asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        if (!m_impl->successorTruthValidated)
        {
            m_impl->outputs.Clear();
            m_impl->committedAcceptedGeneration = 0u;
            if (commitFailure.result == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed)
            {
                m_impl->cohortAdmissionBuild = commitFailure.build;
                m_impl->cohortAdmissionBuildFailed = true;
                m_impl->phase = EditorModelImportSessionPhase::BuildFailed;
            }
            else
            {
                m_impl->cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
                m_impl->cohortAdmissionBuildFailed = false;
                m_impl->phase = EditorModelImportSessionPhase::ReconciliationPending;
            }
            return result;
        }
        m_impl->cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
        m_impl->cohortAdmissionBuildFailed = false;
        if (!m_impl->CaptureCommittedOutputs(assets))
        {
            m_impl->successorTruthValidated = false;
            m_impl->phase = EditorModelImportSessionPhase::ReconciliationPending;
            m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            return m_impl->pipelineResult;
        }
        m_impl->phase = EditorModelImportSessionPhase::Imported;
        m_impl->PollBuildTruth(assets);
        return result;
    }

    bool EditorModelImportSession::CancelQueuedCommit() noexcept
    {
        if (m_impl == nullptr || m_impl->phase != EditorModelImportSessionPhase::Review ||
            !m_impl->commitQueued || m_impl->durableCommitted)
            return false;
        m_impl->commitQueued = false;
        m_impl->commitRetryDelayFrames = 0u;
        m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
        return true;
    }

    asset_streaming::ProjectAssetPipelineResult EditorModelImportSession::Cancel(
        const EditorProjectAssetAccess& assets, asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
    {
        if (m_impl == nullptr)
            return asset_streaming::ProjectAssetPipelineResult::InvalidState;
        if (m_impl->phase == EditorModelImportSessionPhase::Inspecting && m_impl->inspection != nullptr)
        {
            m_impl->inspection->cancelRequested.SetValue(1u);
            return asset_streaming::ProjectAssetPipelineResult::Success;
        }
        if (!m_impl->durableCommitted)
        {
            m_impl->phase = EditorModelImportSessionPhase::Cancelled;
            return asset_streaming::ProjectAssetPipelineResult::Success;
        }
        if (m_impl->phase != EditorModelImportSessionPhase::Imported &&
            m_impl->phase != EditorModelImportSessionPhase::Compiling)
            return asset_streaming::ProjectAssetPipelineResult::InvalidState;
        const asset_streaming::ProjectAssetPipelineResult result = assets.CancelAutomaticRecook(failure);
        m_impl->pipelineResult = result;
        if (result == asset_streaming::ProjectAssetPipelineResult::Success)
            m_impl->phase = EditorModelImportSessionPhase::Cancelling;
        return result;
    }

    asset_streaming::ProjectAssetPipelineResult EditorModelImportSession::Retry(
        const EditorProjectAssetAccess& assets, asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !m_impl->durableCommitted)
            return asset_streaming::ProjectAssetPipelineResult::InvalidState;
        if (m_impl->phase == EditorModelImportSessionPhase::ReconciliationPending ||
            (m_impl->phase == EditorModelImportSessionPhase::BuildFailed && m_impl->cohortAdmissionBuildFailed))
        {
            if (!m_impl->committedAsset.IsValid() || !m_impl->ReviewedPlanMatchesTransientState())
                return asset_streaming::ProjectAssetPipelineResult::InvalidState;
            asset_streaming::ProjectAssetPipelineFailure resumeFailure;
            const asset_streaming::ProjectAssetPipelineResult resumed = assets.ResumeCommittedModelImport(
                m_impl->plan, m_impl->committedAsset, m_impl->expectedAcceptedGeneration, &resumeFailure);
            if (failure != nullptr)
                *failure = resumeFailure;
            m_impl->pipelineResult = resumed;
            if (resumed == asset_streaming::ProjectAssetPipelineResult::Success)
            {
                m_impl->cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
                m_impl->cohortAdmissionBuildFailed = false;
                m_impl->successorTruthValidated = true;
                if (!m_impl->CaptureCommittedOutputs(assets))
                {
                    m_impl->successorTruthValidated = false;
                    m_impl->phase = EditorModelImportSessionPhase::ReconciliationPending;
                    m_impl->pipelineResult = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
                    return m_impl->pipelineResult;
                }
                m_impl->phase = EditorModelImportSessionPhase::Compiling;
            }
            else if (resumed == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed)
            {
                m_impl->outputs.Clear();
                m_impl->committedAcceptedGeneration = 0u;
                m_impl->cohortAdmissionBuild = resumeFailure.build;
                m_impl->cohortAdmissionBuildFailed = true;
                m_impl->successorTruthValidated = false;
                m_impl->phase = EditorModelImportSessionPhase::BuildFailed;
            }
            else
            {
                m_impl->cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
                m_impl->cohortAdmissionBuildFailed = false;
                m_impl->successorTruthValidated = false;
                m_impl->phase = EditorModelImportSessionPhase::ReconciliationPending;
            }
            return resumed;
        }
        bool publicationRetried = false;
        for (const resources::ResourceReference output : m_impl->outputs)
        {
            asset_streaming::ProjectAssetOutputPublicationStatus publication;
            if (!assets.GetOutputPublicationStatus(output, publication) || !publication.publicationRetryRequired)
                continue;
            const asset_streaming::ProjectAssetPipelineResult retried = assets.RetryAutomaticPublication(output, failure);
            if (retried != asset_streaming::ProjectAssetPipelineResult::Success)
            {
                m_impl->pipelineResult = retried;
                return retried;
            }
            publicationRetried = true;
        }
        const asset_streaming::ProjectAssetAutomaticRecookStatus automatic = assets.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetPipelineResult result = publicationRetried
                                                                  ? asset_streaming::ProjectAssetPipelineResult::Success
                                                                  : asset_streaming::ProjectAssetPipelineResult::InvalidState;
        if (automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed ||
            automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired ||
            HasAutomaticRecookDirtyReason(automatic.dirtyReasons,
                                          asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch))
            result = assets.RetryAutomaticRecook(failure);
        m_impl->pipelineResult = result;
        if (result == asset_streaming::ProjectAssetPipelineResult::Success ||
            result == asset_streaming::ProjectAssetPipelineResult::Pending)
            m_impl->phase = EditorModelImportSessionPhase::Compiling;
        return result;
    }

    void EditorModelImportSession::Reset() noexcept
    {
        if (m_impl != nullptr)
            m_impl->ClearTransient();
    }

    u32 EditorModelImportSession::AdoptResolvableProjectImages(const assets::SourceDatabase& sources) noexcept
    {
        if (m_impl == nullptr || m_impl->durableCommitted ||
            m_impl->inspectionResult != mesh_tools::ModelImportResult::Success)
            return 0u;
        u32 changed = 0u;
        const containers::ArraySpan<const assets::SourceRoot> roots = sources.GetRoots();
        for (const mesh_tools::ModelImportTextureReference& texture : m_impl->manifest.textureReferences)
        {
            if (texture.storage != mesh_tools::ImportedTextureStorage::ExternalSource || texture.resolvedPath.Empty())
                continue;
            const filesystem::AbsolutePath path = filesystem::AbsolutePath::ParseFilePath(texture.resolvedPath);
            const assets::SourceRecord* const source = path.IsFilePath() ? sources.LookupResource(path) : nullptr;
            if (source == nullptr || source->root >= roots.Count() || roots[source->root].readOnly ||
                source->importState != assets::SourceImportState::Candidate || !source->sourceIdentity.IsValid() ||
                source->primaryOutputType != textures::TextureResourceType)
                continue;
            mesh_tools::ModelImportTextureResolution* resolution = FindTextureDecision(m_impl->settings, texture.authoredPath);
            if (resolution == nullptr)
            {
                resolution = &m_impl->settings.textureResolutions.EmplaceBack();
                resolution->authoredPath = texture.authoredPath;
            }
            if (resolution->resolution == mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource &&
                resolution->projectSourceIdentity == source->sourceIdentity)
                continue;
            resolution->resolution = mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource;
            resolution->managedResource = {};
            resolution->projectSourceIdentity = source->sourceIdentity;
            ++changed;
        }
        return changed;
    }

    u32 EditorModelImportSession::IgnoreMissingTextures() noexcept
    {
        if (m_impl == nullptr || m_impl->durableCommitted ||
            m_impl->inspectionResult != mesh_tools::ModelImportResult::Success)
            return 0u;
        u32 changed = 0u;
        for (const mesh_tools::ModelImportTextureReference& texture : m_impl->manifest.textureReferences)
        {
            if (texture.storage != mesh_tools::ImportedTextureStorage::ExternalSource || !texture.missing)
                continue;
            mesh_tools::ModelImportTextureResolution* resolution = FindTextureDecision(m_impl->settings, texture.authoredPath);
            if (resolution == nullptr)
            {
                resolution = &m_impl->settings.textureResolutions.EmplaceBack();
                resolution->authoredPath = texture.authoredPath;
            }
            if (resolution->resolution == mesh_tools::ModelImportTextureResolutionKind::IgnoreMissing)
                continue;
            resolution->resolution = mesh_tools::ModelImportTextureResolutionKind::IgnoreMissing;
            resolution->managedResource = {};
            resolution->projectSourceIdentity = {};
            ++changed;
        }
        return changed;
    }

    EditorModelImportSessionStatus EditorModelImportSession::GetStatus() const noexcept
    {
        EditorModelImportSessionStatus status;
        if (m_impl == nullptr)
            return status;
        status.phase = m_impl->phase;
        status.inspection = m_impl->inspectionResult;
        status.pipeline = m_impl->pipelineResult;
        status.asset = m_impl->committedAsset.IsValid()
                           ? m_impl->committedAsset
                           : m_impl->plannedAsset.IsValid() ? m_impl->plannedAsset : m_impl->existingAsset;
        status.acceptedGeneration = m_impl->committedAcceptedGeneration;
        status.inspectionProgressPermille = m_impl->progressPermille;
        status.transactionPreparePhase = m_impl->transactionPreparePhase;
        status.transactionPreparedBytes = m_impl->transactionPreparedBytes;
        status.transactionTotalBytes = m_impl->transactionTotalBytes;
        status.outputCount = m_impl->outputs.Size();
        status.builtOutputs = m_impl->builtOutputs;
        status.readyOutputs = m_impl->readyOutputs;
        status.failedOutputs = m_impl->failedOutputs;
        status.retryOutputs = m_impl->retryOutputs;
        status.automaticState = m_impl->automaticState;
        status.buildOperations = m_impl->buildOperations;
        status.completedBuildOperations = m_impl->completedBuildOperations;
        status.publicationRetryOutput = m_impl->publicationRetryOutput;
        status.cohortAdmissionBuild = m_impl->cohortAdmissionBuild;
        status.cohortAdmissionBuildFailed = m_impl->cohortAdmissionBuildFailed;
        status.durableCommitted = m_impl->durableCommitted;
        status.reimport = m_impl->existingAsset.IsValid();
        status.commitQueued = m_impl->commitQueued;
        status.commitAttempts = m_impl->commitAttempts;
        status.stalePlanSite = m_impl->stalePlanSite;
        return status;
    }

    const filesystem::AbsolutePath& EditorModelImportSession::GetSourcePath() const noexcept
    {
        static const filesystem::AbsolutePath empty;
        return m_impl != nullptr ? m_impl->sourcePath : empty;
    }

    const mesh_tools::ModelImportManifest& EditorModelImportSession::GetManifest() const noexcept
    {
        static const mesh_tools::ModelImportManifest empty;
        return m_impl != nullptr ? m_impl->manifest : empty;
    }

    const mesh_tools::ModelImportReport& EditorModelImportSession::GetInspectionReport() const noexcept
    {
        static const mesh_tools::ModelImportReport empty;
        return m_impl != nullptr ? m_impl->inspectionReport : empty;
    }

    mesh_tools::ModelImportSettings& EditorModelImportSession::GetCandidateSettings() noexcept
    {
        static mesh_tools::ModelImportSettings empty;
        return m_impl != nullptr ? m_impl->settings : empty;
    }

    const mesh_tools::ModelImportSettings& EditorModelImportSession::GetCandidateSettings() const noexcept
    {
        static const mesh_tools::ModelImportSettings empty;
        return m_impl != nullptr ? m_impl->settings : empty;
    }

    const mesh_tools::ModelImportPlan& EditorModelImportSession::GetPlan() const noexcept
    {
        static const mesh_tools::ModelImportPlan empty;
        return m_impl != nullptr ? m_impl->plan : empty;
    }

    const mesh_tools::ModelImportPlanReport& EditorModelImportSession::GetPlanReport() const noexcept
    {
        static const mesh_tools::ModelImportPlanReport empty;
        return m_impl != nullptr ? m_impl->planReport : empty;
    }
} // namespace vanguard::editor
