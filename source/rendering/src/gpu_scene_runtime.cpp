#include <vanguard/rendering/gpu_scene_runtime.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(GpuSceneRuntimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuSceneRuntimeFailure* const failure, const GpuSceneRuntimeFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }
    } // namespace

    struct GpuSceneRuntime::PublicationState
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct Publication
        {
            RenderSceneGpuPublication token;
            u32 reservationOffset = 0;
        };

        struct WriteWork
        {
            enum class Kind : u8
            {
                Scene,
                Contribution
            };

            Kind kind = Kind::Scene;
            u32 publicationIndex = 0;
            u32 rangeIndex = 0;
        };

        enum class ContributionOutcome : u8
        {
            None,
            Staged,
            Submitted,
            SubmittedFailure,
            Retry
        };

        struct Contribution
        {
            GpuSceneContributionDesc desc;
            u32 requestOffset = 0;
            u32 requestCount = 0;
            u32 reservationOffset = 0;
        };

        PublicationState(const u32 maximumScenes, const u32 maximumUpdates, const u32 maximumContributions) noexcept
            : publications(memory::pools::Rendering::GetInstance()), requests(memory::pools::Rendering::GetInstance()), reservations(memory::pools::Rendering::GetInstance()),
              writeWork(memory::pools::Rendering::GetInstance()), scratchRequests(memory::pools::Rendering::GetInstance()), contributions(memory::pools::Rendering::GetInstance()),
              contributionRequests(memory::pools::Rendering::GetInstance())
        {
            publications.Resize(maximumScenes);
            requests.Resize(maximumUpdates);
            reservations.Resize(maximumUpdates);
            writeWork.Resize(maximumUpdates);
            scratchRequests.Reserve(maximumUpdates);
            contributions.Resize(maximumContributions);
            contributionRequests.Resize(maximumUpdates);
        }

        [[nodiscard]] bool StorageReady(const u32 maximumScenes, const u32 maximumUpdates, const u32 maximumContributions) const noexcept
        {
            return publications.Size() == maximumScenes && requests.Size() == maximumUpdates && reservations.Size() == maximumUpdates && writeWork.Size() == maximumUpdates &&
                   contributions.Size() == maximumContributions && contributionRequests.Size() == maximumUpdates;
        }

        void Report(const GpuSceneRuntimeFailure& source) noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(lock);
            if (!failurePending)
            {
                failure = source;
                failurePending = true;
            }
            ++stats.failedPublications;
            stats.publicationFailurePending = true;
        }

        containers::DynamicArray<Publication> publications;
        containers::DynamicArray<GpuSceneUploadRequest> requests;
        containers::DynamicArray<GpuSceneUploadReservation> reservations;
        containers::DynamicArray<WriteWork> writeWork;
        containers::DynamicArray<GpuSceneUploadRequest> scratchRequests;
        containers::DynamicArray<Contribution> contributions;
        containers::DynamicArray<GpuSceneUploadRequest> contributionRequests;
        concurrency::SpinLock lock;
        concurrency::Atomic<bool> publicationOpen{false};
        concurrency::Atomic<bool> writeFailed{false};
        GpuSceneRuntimeFailure failure;
        GpuSceneRuntimeStats stats;
        u32 publicationCount = 0;
        u32 requestCount = 0;
        u32 writeWorkCount = 0;
        u32 contributionCount = 0;
        u32 contributionRequestCount = 0;
        ContributionOutcome contributionOutcome = ContributionOutcome::None;
        u64 uploadBytesPerBatch = 0;
        rhi::GpuFence contributionCompletion;
        bool failurePending = false;
    };

    GpuSceneRuntime::~GpuSceneRuntime()
    {
        if (m_initialized)
            static_cast<void>(Shutdown({}));
    }

    bool GpuSceneRuntime::Initialize(RenderSceneManager& scenes, const GpuSceneRuntimeConfig& config, GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_initialized)
            return Fail(failure, GpuSceneRuntimeFailureCode::AlreadyInitialized, "GPU Scene runtime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneRuntimeFailureCode::WrongThread, "GPU Scene runtime must initialize on the main thread");
        if (!scenes.IsInitialized() || scenes.GetStats().activeScenes != 0)
            return Fail(failure, GpuSceneRuntimeFailureCode::LiveScenesRemain, "GPU Scene runtime requires an initialized RenderSceneManager with no live scenes");
        if (config.maximumExternalContributions == 0 || config.maximumExternalContributions > config.upload.maximumUpdatesPerBatch)
            return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene contribution capacity is invalid");

        GpuSceneTablesFailure tablesFailure;
        if (!m_tables.Initialize(config.tables, &tablesFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::TablesFailure;
                failure->message = "GPU Scene table initialization failed";
                failure->tablesFailure = tablesFailure;
            }
            return false;
        }

        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_lifetime.Initialize(m_tables, config.lifetime, &lifetimeFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::LifetimeFailure;
                failure->message = "GPU Scene lifetime initialization failed";
                failure->lifetimeFailure = lifetimeFailure;
            }
            RollbackInitialization();
            return false;
        }

        GpuSceneUploadFailure uploadFailure;
        if (!m_uploader.Initialize(m_tables, m_lifetime, config.upload, &uploadFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::UploadFailure;
                failure->message = "GPU Scene uploader initialization failed";
                failure->uploadFailure = uploadFailure;
            }
            RollbackInitialization();
            return false;
        }

        GpuSceneDefinitionFailure definitionsFailure;
        if (!m_definitions.Initialize(m_lifetime, m_uploader, config.definitions, &definitionsFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::DefinitionsFailure;
                failure->message = "GPU Scene definition initialization failed";
                failure->definitionsFailure = definitionsFailure;
            }
            RollbackInitialization();
            return false;
        }

        RenderSceneGpuFailure scenePublicationFailure;
        if (!m_scenePublisher.Initialize(scenes, m_lifetime, config.scenePublication, &scenePublicationFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                failure->message = "RenderScene GPU publication initialization failed";
                failure->scenePublicationFailure = scenePublicationFailure;
            }
            RollbackInitialization();
            return false;
        }

        memory::MemoryBlock publicationBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(PublicationState), alignof(PublicationState));
        if (publicationBlock)
            m_publication = ::new (publicationBlock.address) PublicationState(scenes.GetStats().capacity, config.upload.maximumUpdatesPerBatch, config.maximumExternalContributions);
        if (m_publication == nullptr || !m_publication->StorageReady(scenes.GetStats().capacity, config.upload.maximumUpdatesPerBatch, config.maximumExternalContributions))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                failure->message = "GPU Scene publication coordinator allocation failed";
            }
            RollbackInitialization();
            return false;
        }

        m_publication->uploadBytesPerBatch = config.upload.bytesPerSegment;
        m_scenes = &scenes;
        m_initialized = true;
        m_lastPublicationFence = {};
        return true;
    }

    bool GpuSceneRuntime::Shutdown(const rhi::DescriptorRetirement& safeAfter, GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_initialized)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneRuntimeFailureCode::WrongThread, "GPU Scene runtime must shutdown on the main thread");
        if (m_publication != nullptr && m_publication->publicationOpen.GetValue())
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene runtime shutdown requires its publication batch to complete");
        if (m_publication != nullptr && m_publication->contributionCount != 0)
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene runtime shutdown requires staged contributions to be resolved");
        if (m_scenes == nullptr || m_scenes->GetStats().activeScenes != 0)
            return Fail(failure, GpuSceneRuntimeFailureCode::LiveScenesRemain, "GPU Scene runtime shutdown requires every RenderScene to be destroyed");
        if (m_publication != nullptr && m_publication->failurePending)
            return Fail(failure, GpuSceneRuntimeFailureCode::ScenePublicationFailure, "GPU Scene runtime shutdown requires pending publication failures to be consumed");

        const GpuSceneDefinitionsStats definitionStats = m_definitions.GetStats();
        if (definitionStats.geometries != 0 || definitionStats.materials != 0 || definitionStats.renderables != 0)
            return Fail(failure, GpuSceneRuntimeFailureCode::DefinitionsFailure, "GPU Scene runtime shutdown requires every shared definition reference to be released");
        const GpuSceneLifetimeStats lifetimeStats = m_lifetime.GetStats();
        if (lifetimeStats.allocated != 0 || lifetimeStats.active != 0 || lifetimeStats.retiring != 0 || lifetimeStats.pendingRetirements != 0 || lifetimeStats.sealedRetirements != 0 ||
            lifetimeStats.sealedEpochs != 0)
            return Fail(failure, GpuSceneRuntimeFailureCode::LifetimeFailure, "GPU Scene runtime shutdown requires every allocation and retirement epoch to be drained");

        RenderSceneGpuFailure scenePublicationFailure;
        if (!m_scenePublisher.Shutdown(&scenePublicationFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                failure->message = "RenderScene GPU publisher shutdown failed";
                failure->scenePublicationFailure = scenePublicationFailure;
            }
            return false;
        }

        GpuSceneDefinitionFailure definitionsFailure;
        if (!m_definitions.Shutdown(&definitionsFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::DefinitionsFailure;
                failure->message = "GPU Scene definitions shutdown failed";
                failure->definitionsFailure = definitionsFailure;
            }
            return false;
        }

        GpuSceneUploadFailure uploadFailure;
        if (!m_uploader.Shutdown(&uploadFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::UploadFailure;
                failure->message = "GPU Scene uploader shutdown failed";
                failure->uploadFailure = uploadFailure;
            }
            return false;
        }

        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_lifetime.Shutdown(&lifetimeFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::LifetimeFailure;
                failure->message = "GPU Scene lifetime shutdown failed";
                failure->lifetimeFailure = lifetimeFailure;
            }
            return false;
        }

        GpuSceneTablesFailure tablesFailure;
        if (!m_tables.Shutdown(safeAfter, &tablesFailure))
        {
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::TablesFailure;
                failure->message = "GPU Scene tables shutdown failed";
                failure->tablesFailure = tablesFailure;
            }
            return false;
        }

        if (m_publication != nullptr)
        {
            PublicationState* const publication = m_publication;
            m_publication = nullptr;
            publication->~PublicationState();
            memory::MemoryBlock publicationBlock{publication, sizeof(PublicationState), memory::PoolId::Rendering};
            memory::Free(publicationBlock);
        }

        m_scenes = nullptr;
        m_initialized = false;
        return true;
    }

    bool GpuSceneRuntime::AbandonDevice(GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_initialized)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneRuntimeFailureCode::WrongThread, "GPU Scene abandonment must run on the main thread");
        if (m_publication != nullptr && m_publication->publicationOpen.GetValue())
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene abandonment requires the CPU publication chain to be joined");
        if (m_publication != nullptr)
        {
            PublicationState* const publication = m_publication;
            m_publication = nullptr;
            publication->~PublicationState();
            memory::MemoryBlock publicationBlock{publication, sizeof(PublicationState), memory::PoolId::Rendering};
            memory::Free(publicationBlock);
        }
        m_scenePublisher.AbandonDevice();
        m_definitions.AbandonDevice();
        m_uploader.AbandonDevice();
        m_lifetime.AbandonDevice();
        m_tables.AbandonDevice();
        m_scenes = nullptr;
        m_initialized = false;
        return true;
    }

    bool GpuSceneRuntime::IsInitialized() const noexcept
    {
        return m_initialized;
    }

    bool GpuSceneRuntime::Manages(const RenderSceneManager& scenes) const noexcept
    {
        return m_initialized && m_scenes == &scenes;
    }

    GpuSceneContributionBudget GpuSceneRuntime::GetContributionBudget() const noexcept
    {
        if (!concurrency::IsMainThread() || !m_initialized || m_publication == nullptr)
            return {};
        const auto& state = *m_publication;
        GpuSceneContributionBudget budget{state.contributionRequests.Size(), 0, state.uploadBytesPerBatch, 0};
        if (state.publicationOpen.GetValue() || state.contributionCount >= state.contributions.Size() ||
            (state.contributionOutcome != PublicationState::ContributionOutcome::None && state.contributionOutcome != PublicationState::ContributionOutcome::Staged))
            return budget;
        budget.availableUpdates = state.contributionRequests.Size() - state.contributionRequestCount;
        budget.availableBytes = budget.maximumBytes;
        for (u32 index = 0; index < state.contributionRequestCount; ++index)
        {
            const auto& request = state.contributionRequests[index];
            const auto table = request.destinationTable == GpuSceneTableKind::Count ? request.allocation.table : request.destinationTable;
            const u64 bytes = static_cast<u64>(request.elementCount) * m_tables.GetTableStats(table).elementStride;
            // Conservative per-range alignment; duplicate/superseded ranges do
            // not earn budget before the shared uploader resolves them.
            const u64 aligned = (bytes + 15u) & ~15ull;
            budget.availableBytes = aligned < budget.availableBytes ? budget.availableBytes - aligned : 0;
        }
        return budget;
    }

    bool GpuSceneRuntime::StageContribution(const GpuSceneContributionDesc& contribution, GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_initialized || m_publication == nullptr)
            return Fail(failure, GpuSceneRuntimeFailureCode::NotInitialized, "GPU Scene runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneRuntimeFailureCode::WrongThread, "GPU Scene contribution staging must run on the main thread");
        if (!contribution.IsValid())
            return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene contribution descriptor is invalid");

        PublicationState& state = *m_publication;
        if (state.publicationOpen.GetValue() || (state.contributionOutcome != PublicationState::ContributionOutcome::None && state.contributionOutcome != PublicationState::ContributionOutcome::Staged))
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene contribution staging requires the previous contribution outcome to be resolved");
        if (state.contributionCount >= state.contributions.Size() || contribution.requests.Size() > state.contributionRequests.Size() - state.contributionRequestCount)
            return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene contribution capacity is exhausted");
        for (u32 index = 0; index < state.contributionCount; ++index)
            if (state.contributions[index].desc.owner == contribution.owner)
                return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene producer already has a staged contribution");

        PublicationState::Contribution& record = state.contributions[state.contributionCount++];
        record = {};
        record.desc = contribution;
        record.desc.requests = {};
        record.requestOffset = state.contributionRequestCount;
        record.requestCount = contribution.requests.Size();
        for (const GpuSceneUploadRequest& request : contribution.requests)
            state.contributionRequests[state.contributionRequestCount++] = request;
        state.contributionOutcome = PublicationState::ContributionOutcome::Staged;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
            ++state.stats.stagedContributions;
        }
        return true;
    }

    bool GpuSceneRuntime::ResolveContributions(GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_initialized || m_publication == nullptr)
            return Fail(failure, GpuSceneRuntimeFailureCode::NotInitialized, "GPU Scene runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneRuntimeFailureCode::WrongThread, "GPU Scene contribution resolution must run on the main thread");

        PublicationState& state = *m_publication;
        if (state.publicationOpen.GetValue())
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene contributions cannot resolve while their renderer job is active");
        if (state.contributionCount == 0)
            return true;
        if (state.contributionOutcome == PublicationState::ContributionOutcome::SubmittedFailure)
            return Fail(failure, GpuSceneRuntimeFailureCode::UploadFailure, "submitted GPU Scene failure retains contributions until device abandonment");

        if (state.contributionOutcome == PublicationState::ContributionOutcome::Submitted)
        {
            for (u32 index = 0; index < state.contributionCount; ++index)
            {
                PublicationState::Contribution& contribution = state.contributions[index];
                contribution.desc.accept(contribution.desc.owner, contribution.desc.token, state.contributionCompletion);
            }
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
                state.stats.acceptedContributions += state.contributionCount;
            }
            state.contributionCount = 0;
        }
        else if (state.contributionOutcome == PublicationState::ContributionOutcome::Retry || state.contributionOutcome == PublicationState::ContributionOutcome::Staged)
        {
            while (state.contributionCount != 0)
            {
                PublicationState::Contribution& contribution = state.contributions[state.contributionCount - 1u];
                const char* contributionFailure = nullptr;
                if (!contribution.desc.retry(contribution.desc.owner, contribution.desc.token, contributionFailure))
                {
                    if (failure != nullptr)
                    {
                        failure->code = GpuSceneRuntimeFailureCode::ContributionFailure;
                        failure->message = "GPU Scene producer could not retry its contribution";
                        failure->contributionFailure = contributionFailure;
                    }
                    return false;
                }
                --state.contributionCount;
                concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
                ++state.stats.retriedContributions;
            }
        }
        else
        {
            return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene contribution outcome is invalid");
        }

        state.contributionRequestCount = 0;
        state.contributionOutcome = PublicationState::ContributionOutcome::None;
        state.contributionCompletion = {};
        return true;
    }

    bool GpuSceneRuntime::Publish(RenderFrameTickContext& context, GpuSceneRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_initialized || m_publication == nullptr)
            return Fail(failure, GpuSceneRuntimeFailureCode::NotInitialized, "GPU Scene runtime is not initialized");
        if (!context.IsValid())
            return Fail(failure, GpuSceneRuntimeFailureCode::ScenePublicationFailure, "GPU Scene publication requires a valid renderer FrameTick context");

        PublicationState& state = *m_publication;
        if (state.contributionCount != 0 && state.contributionOutcome != PublicationState::ContributionOutcome::Staged)
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene contribution outcome must resolve before another publication");
        if (state.publicationOpen.Exchange(true))
            return Fail(failure, GpuSceneRuntimeFailureCode::Busy, "GPU Scene runtime already has an open publication batch");
        state.publicationCount = 0;
        state.requestCount = 0;
        state.writeWorkCount = 0;
        state.writeFailed.SetValue(false);
        const auto requestBytes = [this](const GpuSceneUploadRequest& request) noexcept
        {
            const auto table = request.destinationTable == GpuSceneTableKind::Count ? request.allocation.table : request.destinationTable;
            return (static_cast<u64>(request.elementCount) * m_tables.GetTableStats(table).elementStride + 15u) & ~15ull;
        };
        u64 plannedBytes = 0;
        for (u32 contributionIndex = 0; contributionIndex < state.contributionCount; ++contributionIndex)
        {
            PublicationState::Contribution& contribution = state.contributions[contributionIndex];
            contribution.reservationOffset = state.requestCount;
            const u32 requestEnd = contribution.requestOffset + contribution.requestCount;
            if (requestEnd > state.contributionRequestCount)
            {
                state.publicationOpen.SetValue(false);
                return Fail(failure, GpuSceneRuntimeFailureCode::ContributionFailure, "GPU Scene staged contribution request range is invalid");
            }
            for (u32 requestIndex = contribution.requestOffset; requestIndex < requestEnd; ++requestIndex)
            {
                state.requests[state.requestCount++] = state.contributionRequests[requestIndex];
                const u64 bytes = requestBytes(state.contributionRequests[requestIndex]);
                plannedBytes = bytes <= state.uploadBytesPerBatch - plannedBytes ? plannedBytes + bytes : state.uploadBytesPerBatch;
            }
            state.writeWork[state.writeWorkCount++] = {PublicationState::WriteWork::Kind::Contribution, contributionIndex, 0};
        }
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
            ++state.stats.publicationTicks;
        }

        const auto cancelPrepared = [this, &state]() noexcept
        {
            for (u32 index = 0; index < state.publicationCount; ++index)
                static_cast<void>(m_scenePublisher.Cancel(state.publications[index].token));
            state.publicationCount = 0;
            state.requestCount = 0;
            state.writeWorkCount = 0;
        };

        const containers::ArraySpan<const RenderSceneProcessingEpoch> scenes = context.GetScenes();
        for (u32 sceneIndex = 0; sceneIndex < scenes.Size(); ++sceneIndex)
        {
            RenderSceneGpuPublication publication;
            RenderSceneGpuFailure sceneFailure;
            if (!m_scenePublisher.Prepare(scenes[sceneIndex].scene, scenes[sceneIndex].mutationEpoch, publication, &sceneFailure))
            {
                cancelPrepared();
                if (failure != nullptr)
                {
                    failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                    failure->message = "RenderScene GPU publication preparation failed";
                    failure->scenePublicationFailure = sceneFailure;
                }
                state.publicationOpen.SetValue(false);
                return false;
            }

            if (publication.changeCount > state.requests.Size() - state.requestCount)
            {
                static_cast<void>(m_scenePublisher.Cancel(publication));
                concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
                state.stats.deferredScenes += scenes.Size() - sceneIndex;
                break;
            }

            state.scratchRequests.Clear();
            if (!m_scenePublisher.BuildUploadRequests(publication, state.scratchRequests, &sceneFailure) || state.scratchRequests.Size() != publication.changeCount)
            {
                static_cast<void>(m_scenePublisher.Cancel(publication));
                cancelPrepared();
                if (failure != nullptr)
                {
                    failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                    failure->message = "RenderScene GPU upload request planning failed";
                    failure->scenePublicationFailure = sceneFailure;
                }
                state.publicationOpen.SetValue(false);
                return false;
            }

            u64 sceneBytes = 0;
            bool fits = true;
            for (const auto& request : state.scratchRequests)
            {
                const u64 bytes = requestBytes(request);
                if (bytes > state.uploadBytesPerBatch - plannedBytes - sceneBytes)
                {
                    fits = false;
                    break;
                }
                sceneBytes += bytes;
            }
            if (!fits)
            {
                static_cast<void>(m_scenePublisher.Cancel(publication));
                if (plannedBytes == 0)
                {
                    cancelPrepared();
                    state.publicationOpen.SetValue(false);
                    return Fail(failure, GpuSceneRuntimeFailureCode::UploadFailure, "one RenderScene publication exceeds the entire staging segment");
                }
                concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
                state.stats.deferredScenes += scenes.Size() - sceneIndex;
                break;
            }
            plannedBytes += sceneBytes;
            const u32 publicationIndex = state.publicationCount++;
            PublicationState::Publication& record = state.publications[publicationIndex];
            record.token = publication;
            record.reservationOffset = state.requestCount;
            for (u32 requestIndex = 0; requestIndex < state.scratchRequests.Size(); ++requestIndex)
                state.requests[state.requestCount++] = state.scratchRequests[requestIndex];
            const u32 rangeCount = m_scenePublisher.GetWriteRangeCount(publication);
            for (u32 rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
            {
                RenderSceneGpuWriteRange range;
                if (!m_scenePublisher.GetWriteRange(publication, rangeIndex, range))
                {
                    cancelPrepared();
                    state.publicationOpen.SetValue(false);
                    return Fail(failure, GpuSceneRuntimeFailureCode::ScenePublicationFailure, "RenderScene GPU publication produced an invalid write range");
                }
                if (range.HasWork())
                    state.writeWork[state.writeWorkCount++] = {PublicationState::WriteWork::Kind::Scene, publicationIndex, rangeIndex};
            }
        }

        if (state.publicationCount == 0 && state.contributionCount == 0)
        {
            state.publicationOpen.SetValue(false);
            return true;
        }
        if (state.requestCount == 0)
        {
            for (u32 index = 0; index < state.publicationCount; ++index)
            {
                RenderSceneGpuFailure sceneFailure;
                if (!m_scenePublisher.Complete(state.publications[index].token, &sceneFailure))
                {
                    cancelPrepared();
                    if (failure != nullptr)
                    {
                        failure->code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                        failure->message = "RenderScene retirement-only publication failed";
                        failure->scenePublicationFailure = sceneFailure;
                    }
                    state.publicationOpen.SetValue(false);
                    return false;
                }
            }
            concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
            state.stats.completedPublications += state.publicationCount;
            state.publicationCount = 0;
            state.publicationOpen.SetValue(false);
            return true;
        }

        GpuSceneUploadFailure uploadFailure;
        if (!m_uploader.Begin({state.requests.TypedData(), state.requestCount}, {state.reservations.TypedData(), state.requestCount}, &uploadFailure))
        {
            cancelPrepared();
            if (state.contributionCount != 0)
                state.contributionOutcome = PublicationState::ContributionOutcome::Retry;
            if (failure != nullptr)
            {
                failure->code = GpuSceneRuntimeFailureCode::UploadFailure;
                failure->message = "GPU Scene upload batch planning failed";
                failure->uploadFailure = uploadFailure;
            }
            state.publicationOpen.SetValue(false);
            return false;
        }

        const u32 groupCount = state.writeWorkCount < jobs::GetWorkerCount() + 1u ? state.writeWorkCount : jobs::GetWorkerCount() + 1u;
        static jobs::JobName writeName{"GpuScene.Publish.WriteRanges"};
        jobs::ParallelTask writers = jobs::ParallelTask::Create(
            [this, &state, groupCount](const u32 group, const jobs::JobContext&) noexcept
            {
                for (u32 workIndex = group; workIndex < state.writeWorkCount; workIndex += groupCount)
                {
                    const PublicationState::WriteWork work = state.writeWork[workIndex];
                    if (work.kind == PublicationState::WriteWork::Kind::Contribution)
                    {
                        PublicationState::Contribution& contribution = state.contributions[work.publicationIndex];
                        const char* contributionFailure = nullptr;
                        if (!contribution.desc.write(contribution.desc.owner, contribution.desc.token, {state.reservations.TypedData() + contribution.reservationOffset, contribution.requestCount},
                                                     contributionFailure) &&
                            !state.writeFailed.Exchange(true))
                        {
                            GpuSceneRuntimeFailure runtimeFailure;
                            runtimeFailure.code = GpuSceneRuntimeFailureCode::ContributionFailure;
                            runtimeFailure.message = "GPU Scene contribution staging write failed";
                            runtimeFailure.contributionFailure = contributionFailure;
                            state.Report(runtimeFailure);
                        }
                        continue;
                    }

                    const PublicationState::Publication& record = state.publications[work.publicationIndex];
                    RenderSceneGpuFailure sceneFailure;
                    if (!m_scenePublisher.WriteRange(record.token, {state.reservations.TypedData() + record.reservationOffset, record.token.changeCount}, work.rangeIndex, &sceneFailure))
                    {
                        if (!state.writeFailed.Exchange(true))
                        {
                            GpuSceneRuntimeFailure runtimeFailure;
                            runtimeFailure.code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                            runtimeFailure.message = "RenderScene GPU staging write failed";
                            runtimeFailure.scenePublicationFailure = sceneFailure;
                            state.Report(runtimeFailure);
                        }
                    }
                }
            });
        jobs::Task epilogue = jobs::Task::Create(
            [this, &state](const jobs::JobContext&) noexcept
            {
                GpuSceneRuntimeFailure runtimeFailure;
                bool succeeded = !state.writeFailed.GetValue();
                bool submitted = false;
                for (u32 index = 0; succeeded && index < state.publicationCount; ++index)
                {
                    RenderSceneGpuFailure sceneFailure;
                    if (!m_scenePublisher.FinishWrites(state.publications[index].token, &sceneFailure))
                    {
                        runtimeFailure.code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                        runtimeFailure.message = "RenderScene GPU staging completion failed";
                        runtimeFailure.scenePublicationFailure = sceneFailure;
                        succeeded = false;
                    }
                }
                for (u32 index = 0; succeeded && index < state.requestCount; ++index)
                {
                    GpuSceneUploadFailure uploadFailure;
                    if (!m_uploader.Complete(state.reservations[index], &uploadFailure))
                    {
                        runtimeFailure.code = GpuSceneRuntimeFailureCode::UploadFailure;
                        runtimeFailure.message = "GPU Scene upload reservation completion failed";
                        runtimeFailure.uploadFailure = uploadFailure;
                        succeeded = false;
                    }
                }
                GpuSceneUploadResult uploadResult;
                if (succeeded)
                {
                    GpuSceneUploadFailure uploadFailure;
                    if (!m_uploader.Submit(uploadResult, &uploadFailure))
                    {
                        submitted = uploadResult.workSubmitted;
                        if (submitted && state.contributionCount != 0)
                            state.contributionOutcome = PublicationState::ContributionOutcome::SubmittedFailure;
                        runtimeFailure.code = GpuSceneRuntimeFailureCode::UploadFailure;
                        runtimeFailure.message = "GPU Scene upload submission failed";
                        runtimeFailure.uploadFailure = uploadFailure;
                        succeeded = false;
                    }
                    else
                    {
                        submitted = true;
                        if (state.contributionCount != 0)
                        {
                            state.contributionOutcome = PublicationState::ContributionOutcome::Submitted;
                            state.contributionCompletion = uploadResult.completion;
                        }
                    }
                }
                if (uploadResult.completion.IsValid())
                    m_lastPublicationFence = uploadResult.completion;
                for (u32 index = 0; succeeded && index < state.publicationCount; ++index)
                {
                    RenderSceneGpuFailure sceneFailure;
                    if (!m_scenePublisher.Complete(state.publications[index].token, &sceneFailure))
                    {
                        runtimeFailure.code = GpuSceneRuntimeFailureCode::ScenePublicationFailure;
                        runtimeFailure.message = "RenderScene GPU publication completion failed";
                        runtimeFailure.scenePublicationFailure = sceneFailure;
                        succeeded = false;
                    }
                }
                if (!succeeded)
                {
                    if (!submitted)
                    {
                        static_cast<void>(m_uploader.Cancel());
                        for (u32 index = 0; index < state.publicationCount; ++index)
                            static_cast<void>(m_scenePublisher.Cancel(state.publications[index].token));
                        if (state.contributionCount != 0)
                            state.contributionOutcome = PublicationState::ContributionOutcome::Retry;
                    }
                    if (runtimeFailure.code != GpuSceneRuntimeFailureCode::None)
                        state.Report(runtimeFailure);
                }
                else
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(state.lock);
                    ++state.stats.submittedBatches;
                    state.stats.completedPublications += state.publicationCount;
                    state.stats.publishedObjects += uploadResult.uniqueUpdates;
                }
                state.publicationCount = 0;
                state.requestCount = 0;
                state.writeWorkCount = 0;
                state.publicationOpen.SetValue(false);
            });
        if (groupCount == 0 || !writers || !epilogue || !context.GetBuilder().DispatchParallel(writeName, groupCount, std::move(writers), std::move(epilogue), 1u))
        {
            static_cast<void>(m_uploader.Cancel());
            cancelPrepared();
            if (state.contributionCount != 0)
                state.contributionOutcome = PublicationState::ContributionOutcome::Retry;
            state.publicationOpen.SetValue(false);
            return Fail(failure, GpuSceneRuntimeFailureCode::ScenePublicationFailure, "GPU Scene publication Jobs dispatch failed");
        }
        return true;
    }

    bool GpuSceneRuntime::ConsumePublicationFailure(GpuSceneRuntimeFailure& failure) noexcept
    {
        failure = {};
        if (m_publication == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_publication->lock);
        if (!m_publication->failurePending)
            return false;
        failure = m_publication->failure;
        m_publication->failure = {};
        m_publication->failurePending = false;
        m_publication->stats.publicationFailurePending = false;
        return true;
    }

    GpuSceneRuntimeStats GpuSceneRuntime::GetStats() const noexcept
    {
        if (m_publication == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_publication->lock);
        return m_publication->stats;
    }

    GpuSceneTables& GpuSceneRuntime::GetTables() noexcept
    {
        return m_tables;
    }

    const GpuSceneTables& GpuSceneRuntime::GetTables() const noexcept
    {
        return m_tables;
    }

    GpuSceneLifetime& GpuSceneRuntime::GetLifetime() noexcept
    {
        return m_lifetime;
    }

    const GpuSceneLifetime& GpuSceneRuntime::GetLifetime() const noexcept
    {
        return m_lifetime;
    }

    GpuSceneUploader& GpuSceneRuntime::GetUploader() noexcept
    {
        return m_uploader;
    }

    const GpuSceneUploader& GpuSceneRuntime::GetUploader() const noexcept
    {
        return m_uploader;
    }

    GpuSceneDefinitions& GpuSceneRuntime::GetDefinitions() noexcept
    {
        return m_definitions;
    }

    const GpuSceneDefinitions& GpuSceneRuntime::GetDefinitions() const noexcept
    {
        return m_definitions;
    }

    RenderSceneGpuPublisher& GpuSceneRuntime::GetScenePublisher() noexcept
    {
        return m_scenePublisher;
    }

    const RenderSceneGpuPublisher& GpuSceneRuntime::GetScenePublisher() const noexcept
    {
        return m_scenePublisher;
    }

    void GpuSceneRuntime::RollbackInitialization() noexcept
    {
        if (m_publication != nullptr)
        {
            PublicationState* const publication = m_publication;
            m_publication = nullptr;
            publication->~PublicationState();
            memory::MemoryBlock publicationBlock{publication, sizeof(PublicationState), memory::PoolId::Rendering};
            memory::Free(publicationBlock);
        }
        static_cast<void>(m_scenePublisher.Shutdown());
        static_cast<void>(m_definitions.Shutdown());
        static_cast<void>(m_uploader.Shutdown());
        static_cast<void>(m_lifetime.Shutdown());
        static_cast<void>(m_tables.Shutdown({}));
        m_scenes = nullptr;
        m_initialized = false;
    }
} // namespace vanguard::rendering
