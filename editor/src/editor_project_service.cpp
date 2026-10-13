#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_ui_frame_retirement.hpp>
#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/assets/project_source_data.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/material_tools/material_asset_compilers.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
#include <vanguard/mesh_tools/mesh_tools.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/model_tools/model_project_build.hpp>
#include <vanguard/projects/project_workspace.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/textures/texture_resource.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/prefabs/prefabs.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace vanguard::editor
{
    namespace
    {
        using PipelineResult = asset_streaming::ProjectAssetPipelineResult;

        // The imported static-surface canonical builder consumes exactly the
        // same shader source witnesses in the Editor as in headless builds.
        [[nodiscard]] bool ReadModelToolSource(const filesystem::AbsolutePath& path,
                                               containers::DynamicArray<u8>& bytes) noexcept
        {
            filesystem::Manager& files = filesystem::GetManager();
            if (!files.FileExist(path))
                return false;
            const u64 byteCount = files.GetFileSize(path);
            if (byteCount > ~u32{0})
                return false;
            auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!file)
                return false;
            bytes.Resize(static_cast<u32>(byteCount));
            if (bytes.Size() != byteCount)
                return false;
            file->Serialize(bytes.Data(), bytes.Size());
            return !file->HasErrors();
        }

        [[nodiscard]] bool ModelMetadataFingerprint(const assets::AssetMetadata& metadata,
                                                    crypto::Digest256& digest) noexcept
        {
            containers::String encoded;
            if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
                return false;
            digest = crypto::Sha256(encoded.AsChar(), encoded.Length());
            return !digest.IsEmpty();
        }

        // Produce exact dependency evidence for both already-authored resources
        // and writable, as-yet-unmanaged project-image adoption candidates.
        // P5 validates these same witnesses again at durable plan commit.
        [[nodiscard]] bool CaptureModelDependencyWitness(
            const assets::SourceRecord& source, const assets::SourceDatabase& sources,
            const asset_streaming::ProjectAssetPipeline& pipeline,
            containers::DynamicArray<mesh_tools::ModelImportDependencyWitness>& witnesses,
            const u32 maximum) noexcept
        {
            if (!source.sourceIdentity.IsValid() || !source.path.IsFilePath())
                return false;
            for (const mesh_tools::ModelImportDependencyWitness& previous : witnesses)
                if (previous.sourceIdentity == source.sourceIdentity)
                    return previous.physicalPath == source.path;
            if (witnesses.Size() >= maximum)
                return false;

            mesh_tools::ModelImportDependencyWitness evidence;
            evidence.sourceIdentity = source.sourceIdentity;
            evidence.physicalPath = source.path;
            evidence.sourceExists = true;
            if (source.importState == assets::SourceImportState::MetadataSelected &&
                source.metadataResult == assets::MetadataResult::Success && source.metadata.id.IsValid())
            {
                const asset_streaming::ProjectAssetState* const accepted =
                    pipeline.LookupAcceptedAsset(source.metadata.id);
                if (accepted == nullptr || accepted->presence != asset_streaming::ProjectAssetPresence::Present ||
                    accepted->sourceIdentity != source.sourceIdentity || accepted->physicalPath != source.path ||
                    !accepted->hasSourceContentFingerprint || accepted->sourceContentFingerprint.IsEmpty() ||
                    !ModelMetadataFingerprint(source.metadata, evidence.metadataFingerprint))
                    return false;
                evidence.asset = source.metadata.id;
                evidence.sourceContentFingerprint = accepted->sourceContentFingerprint;
                evidence.metadataExists = true;
                evidence.hasMetadataFingerprint = true;
            }
            else if (source.importState == assets::SourceImportState::Candidate &&
                     (source.issues & assets::MetadataMissing) != 0u)
            {
                const containers::ArraySpan<const assets::SourceRoot> roots = sources.GetRoots();
                if (source.root >= roots.Count() || roots[source.root].readOnly)
                    return false;
                containers::DynamicArray<u8> sourceBytes(memory::pools::Tools::GetInstance());
                if (!ReadModelToolSource(source.path, sourceBytes) || sourceBytes.Empty())
                    return false;
                evidence.sourceContentFingerprint = crypto::Sha256(sourceBytes.TypedData(), sourceBytes.Size());
            }
            else
                return false;

            evidence.hasSourceContentFingerprint = !evidence.sourceContentFingerprint.IsEmpty();
            if (!evidence.hasSourceContentFingerprint)
                return false;
            const u32 previous = witnesses.Size();
            witnesses.PushBack(static_cast<mesh_tools::ModelImportDependencyWitness&&>(evidence));
            return witnesses.Size() == previous + 1u;
        }

        [[nodiscard]] bool GatherModelDependencyWitnesses(
            const mesh_tools::ModelImportManifest& manifest, const mesh_tools::ModelImportSettings& settings,
            const assets::SourceDatabase& sources, const asset_streaming::ProjectAssetPipeline& pipeline,
            const u32 maximum, containers::DynamicArray<mesh_tools::ModelImportDependencyWitness>& witnesses) noexcept
        {
            witnesses.Clear();
            const auto appendReference = [&](const resources::ResourceReference reference) noexcept
            {
                if (!reference.IsValid())
                    return true;
                const assets::SourceRecord* const source = sources.LookupOutput(reference);
                return source != nullptr && CaptureModelDependencyWitness(*source, sources, pipeline, witnesses, maximum);
            };
            if (!appendReference(settings.mesh.defaultMaterial) || !appendReference(settings.mesh.skeleton))
                return false;
            for (const mesh_tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
            {
                if (resolution.resolution == mesh_tools::ModelImportTextureResolutionKind::ReuseManagedResource &&
                    !appendReference(resolution.managedResource))
                    return false;
                if (resolution.resolution == mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource)
                {
                    const assets::SourceRecord* const candidate = sources.LookupSource(resolution.projectSourceIdentity);
                    if (candidate == nullptr ||
                        !CaptureModelDependencyWitness(*candidate, sources, pipeline, witnesses, maximum))
                        return false;
                }
            }
            for (const mesh_tools::ModelImportTextureReference& texture : manifest.textureReferences)
            {
                if (texture.storage != mesh_tools::ImportedTextureStorage::ExternalSource || texture.resolvedPath.Empty())
                    continue;
                const filesystem::AbsolutePath path = filesystem::AbsolutePath::ParseFilePath(texture.resolvedPath);
                const assets::SourceRecord* const source = path.IsFilePath() ? sources.LookupResource(path) : nullptr;
                if (source != nullptr &&
                    !CaptureModelDependencyWitness(*source, sources, pipeline, witnesses, maximum))
                    return false;
            }
            return witnesses.Size() <= maximum;
        }

        [[nodiscard]] PipelineResult FailAssetAccess(
            asset_streaming::ProjectAssetPipelineFailure* const failure, const PipelineResult result,
            const assets::AssetOperationResult operation = assets::AssetOperationResult::Success,
            const assets::AssetId asset = {}) noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->result = result;
                failure->operation.result = operation;
                failure->operation.source = asset;
            }
            return result;
        }

        [[nodiscard]] bool SameAcceptedOutputs(const asset_streaming::ProjectAssetState& accepted,
                                               const assets::SourceRecord& source) noexcept
        {
            if (accepted.outputs.Count() != source.metadata.outputs.Size())
                return false;
            for (const assets::AssetOutput& authored : source.metadata.outputs)
            {
                const resources::ResourceReference authoredReference =
                    assets::GetAssetOutputReference(source.metadata.id, authored);
                bool found = false;
                for (const asset_streaming::ProjectAssetOutputState& current : accepted.outputs)
                {
                    if (containers::StringView(current.key) == containers::StringView(authored.key) &&
                        current.resource == authoredReference)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        [[nodiscard]] PipelineResult ResolveSelectedAsset(
            ProjectWorkspaceService* const workspace, const assets::AssetId asset,
            const u64 expectedAcceptedGeneration, const u64 expectedSourceGeneration,
            asset_streaming::ProjectAssetPipeline*& pipeline,
            const asset_streaming::ProjectAssetState*& accepted,
            const assets::SourceRecord*& source,
            asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
        {
            pipeline = workspace != nullptr ? workspace->GetAssetPipeline() : nullptr;
            accepted = nullptr;
            source = nullptr;
            if (pipeline == nullptr)
                return FailAssetAccess(failure, PipelineResult::InvalidState);
            if (!asset.IsValid() || expectedAcceptedGeneration == 0u || expectedSourceGeneration == 0u)
                return FailAssetAccess(failure, PipelineResult::InvalidArgument);

            const assets::SourceDatabase& sources = workspace->GetSources();
            if (sources.GetGeneration() != expectedSourceGeneration)
                return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                       assets::AssetOperationResult::SourceUnavailable, asset);
            accepted = pipeline->LookupAcceptedAsset(asset);
            if (accepted == nullptr || accepted->acceptedGeneration != expectedAcceptedGeneration)
                return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                       assets::AssetOperationResult::SourceUnavailable, asset);
            source = sources.LookupAsset(asset);
            if (source == nullptr || source->sourceIdentity != accepted->sourceIdentity ||
                source->path != accepted->physicalPath || source->metadata.importer != accepted->importer ||
                source->metadata.importerVersion != accepted->authoredImporterVersion ||
                source->availableImporterVersion != accepted->availableImporterVersion ||
                source->metadata.settingsVersion != accepted->settingsVersion ||
                source->sourceType != accepted->sourceType || source->issues != accepted->sourceIssues ||
                source->importState != accepted->importState ||
                crypto::Sha256(source->metadata.settings.TypedData(), source->metadata.settings.Size()) !=
                    accepted->settingsFingerprint ||
                !SameAcceptedOutputs(*accepted, *source))
                return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                       assets::AssetOperationResult::SourceUnavailable, asset);
            return PipelineResult::Success;
        }

        [[nodiscard]] bool IsRelativeMutationTarget(const containers::StringView target) noexcept
        {
            if (target.Empty() || target.Length() > resources::MaximumResourcePathBytes)
                return false;
            const char first = target.Front();
            if (first == '/' || first == '\\')
                return false;
            for (u32 index = 0u; index < target.Length(); ++index)
                if (target[index] == ':')
                    return false;
            return true;
        }
    } // namespace

    ProjectAssetInitializeFailureOwnerAction ProjectAssetInitializeFailureOwnerPolicy(
        const bool pipelineInitialized) noexcept
    {
        return pipelineInitialized ? ProjectAssetInitializeFailureOwnerAction::PreserveForLifecycleShutdown
                                   : ProjectAssetInitializeFailureOwnerAction::ReleaseCompilerImmediately;
    }

    bool detail::IsProjectTexturePreviewAdmissionWithinCumulativeReserves(
        const ProjectTexturePreviewPriority priority,
        const u32 background,
        const u32 backgroundAndInteractive,
        const u32 total,
        const u32 maximumBackground,
        const u32 maximumBackgroundAndInteractive,
        const u32 maximumTotal) noexcept
    {
        if (total >= maximumTotal)
            return false;
        if (priority == ProjectTexturePreviewPriority::Background)
            return background < maximumBackground && backgroundAndInteractive < maximumBackgroundAndInteractive;
        if (priority == ProjectTexturePreviewPriority::Interactive)
            return backgroundAndInteractive < maximumBackgroundAndInteractive;
        return true;
    }

    bool detail::IsProjectTexturePreviewResidencyBelowHardLimit(const u32 held) noexcept
    {
        return held < MaximumProjectTexturePreviewReadyBackings;
    }

    bool detail::CanUseProjectTexturePreviewSupersessionResidencySlot(
        const u32 held, const bool supersessionSlotInUse, const bool hasExactPredecessor) noexcept
    {
        static_assert(MaximumProjectTexturePreviewReadyBackings != 0u);
        return held == MaximumProjectTexturePreviewReadyBackings - 1u &&
               !supersessionSlotInUse && hasExactPredecessor;
    }

    const asset_streaming::ProjectAssetCatalog* EditorProjectAssetAccess::GetCatalog() const noexcept
    {
        return m_workspace != nullptr ? &m_workspace->GetAssetCatalog() : nullptr;
    }

    u64 EditorProjectAssetAccess::GetAcceptedGeneration() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->GetAcceptedAssetGeneration() : 0u;
    }

    bool EditorProjectAssetAccess::IsSourceGenerationCurrent(const u64 generation) const noexcept
    {
        return m_workspace != nullptr && generation != 0u &&
               m_workspace->GetSources().GetGeneration() == generation;
    }

    bool EditorProjectAssetAccess::GetAcceptedAssetGeneration(const assets::AssetId asset,
                                                               u64& generation) const noexcept
    {
        generation = 0u;
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !asset.IsValid())
            return false;
        const asset_streaming::ProjectAssetState* const accepted = pipeline->LookupAcceptedAsset(asset);
        if (accepted == nullptr)
            return false;
        generation = accepted->acceptedGeneration;
        return generation != 0u;
    }

    bool EditorProjectAssetAccess::GetOutputDeliveryStatus(
        const resources::ResourceReference output,
        asset_streaming::ProjectAssetOutputDeliveryStatus& status) const noexcept
    {
        status = {};
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr && pipeline->GetOutputDeliveryStatus(output, status);
    }

    bool EditorProjectAssetAccess::GetOutputPublicationStatus(
        const resources::ResourceReference output,
        asset_streaming::ProjectAssetOutputPublicationStatus& status) const noexcept
    {
        status = {};
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr && pipeline->GetOutputPublicationStatus(output, status);
    }

    u32 EditorProjectAssetAccess::CopyAssetOutputs(const assets::AssetId asset, const u32 offset,
                                                   const containers::ArraySpan<resources::ResourceReference> output,
                                                   u32& available, bool& truncated) const noexcept
    {
        available = 0u;
        truncated = false;
        if (m_workspace == nullptr || !asset.IsValid())
            return 0u;
        const assets::SourceRecord* const source = m_workspace->GetSources().LookupAsset(asset);
        if (source == nullptr)
            return 0u;
        constexpr u32 PresentationLimit = assets::MaximumAssetOutputs;
        available = std::min<u32>(source->metadata.outputs.Size(), PresentationLimit);
        truncated = source->metadata.outputs.Size() > PresentationLimit;
        if (offset >= available || output.Empty())
            return 0u;
        const u32 count = std::min<u32>(output.Size(), available - offset);
        for (u32 index = 0u; index < count; ++index)
            output[index] = assets::GetAssetOutputReference(source->metadata.id, source->metadata.outputs[offset + index]);
        return count;
    }

    bool EditorProjectAssetAccess::ResolveUniqueOutputOfType(
        const assets::AssetId asset, const resources::ResourceTypeId type,
        resources::ResourceReference& output, bool& ambiguous) const noexcept
    {
        output = {};
        ambiguous = false;
        if (m_workspace == nullptr || !asset.IsValid() || type == resources::InvalidResourceTypeId)
            return false;
        const assets::SourceRecord* const source = m_workspace->GetSources().LookupAsset(asset);
        return source != nullptr &&
               assets::ResolveUniqueAssetOutputOfType(source->metadata, type, output, ambiguous);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::QueryBuildFreshness(
        const resources::ResourceReference output, asset_streaming::ProjectAssetBuildFreshness& freshness,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr)
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->result = asset_streaming::ProjectAssetPipelineResult::InvalidState;
            }
            return asset_streaming::ProjectAssetPipelineResult::InvalidState;
        }
        return pipeline->QueryBuildFreshness(output, freshness, failure);
    }

    u32 EditorProjectAssetAccess::CopyForwardReferences(const assets::AssetId source, const u32 offset,
                                                        const containers::ArraySpan<assets::AssetReference> output,
                                                        u32& available, bool& truncated) const noexcept
    {
        available = 0u;
        truncated = false;
        if (m_workspace == nullptr || !source.IsValid())
            return 0u;
        const containers::ArraySpan<const assets::AssetReference> references =
            m_workspace->GetSources().GetForwardReferences(source);
        constexpr u32 PresentationLimit = 4096u;
        available = std::min<u32>(references.Size(), PresentationLimit);
        truncated = references.Size() > PresentationLimit;
        if (offset >= available || output.Empty())
            return 0u;
        const u32 count = std::min<u32>(output.Size(), available - offset);
        for (u32 index = 0u; index < count; ++index)
            output[index] = references[offset + index];
        return count;
    }

    u32 EditorProjectAssetAccess::CopyBackwardReferences(
        const resources::ResourceReference target, const u32 offset,
        const containers::ArraySpan<assets::AssetReference> output,
        u32& available, bool& truncated) const noexcept
    {
        available = 0u;
        truncated = false;
        if (m_workspace == nullptr || !target.IsValid() || !target.IsTyped())
            return 0u;
        const containers::ArraySpan<const assets::AssetReference> references =
            m_workspace->GetSources().GetBackwardReferences(target);
        constexpr u32 PresentationLimit = 4096u;
        available = std::min<u32>(references.Size(), PresentationLimit);
        truncated = references.Size() > PresentationLimit;
        if (offset >= available || output.Empty())
            return 0u;
        const u32 count = std::min<u32>(output.Size(), available - offset);
        for (u32 index = 0u; index < count; ++index)
            output[index] = references[offset + index];
        return count;
    }

    bool EditorProjectAssetAccess::CanMutateAssets() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr && pipeline->CanStartAssetOperation();
    }

    bool EditorProjectAssetAccess::CanRetryReviewedModelCommit(const mesh_tools::ModelImportPlan& plan) const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return false;
        // A reviewed replacement of a persisted cohort intent is allowed by
        // CommitPlannedImport's CanReconcileSources gate even while ordinary
        // CanStartAssetOperation is false. Leave authoritative admission to
        // the commit; only skip the expensive proof during watcher catch-up.
        const asset_streaming::ProjectAssetObservationStatus observation = pipeline->GetObservationStatus();
        const mesh_tools::ModelImportPlanWitnesses& witnesses = plan.GetWitnesses();
        return !observation.rescanRequired && !observation.reconcilePending &&
               pipeline->CanCommitPlannedImportNow(witnesses.parentAsset, witnesses.sourceIdentity);
    }

    u32 EditorProjectAssetAccess::GetSourceRootCount() const noexcept
    {
        return m_workspace != nullptr ? m_workspace->GetSources().GetRoots().Count() : 0u;
    }

    bool EditorProjectAssetAccess::GetSourceRootPresentation(
        const u32 root, EditorProjectAssetRootPresentation& presentation) const noexcept
    {
        presentation = {};
        if (m_workspace == nullptr)
            return false;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_workspace->GetSources().GetRoots();
        if (root >= roots.Count())
            return false;
        presentation.root = root;
        presentation.readOnly = roots[root].readOnly;
        const char* const label = roots[root].directory == m_workspace->GetAssetsRoot() ? "Project Assets" : nullptr;
        if (label != nullptr)
            std::snprintf(presentation.displayName, sizeof(presentation.displayName), "%s", label);
        else
            std::snprintf(presentation.displayName, sizeof(presentation.displayName), "Project Source %u", root + 1u);
        return true;
    }

    bool EditorProjectAssetAccess::CanImportExternalSource(
        const u32 root, const containers::StringView rootRelativeFolder) const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !pipeline->CanStartAssetOperation())
            return false;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_workspace->GetSources().GetRoots();
        if (root >= roots.Count() || roots[root].readOnly || rootRelativeFolder.Length() > resources::MaximumResourcePathBytes)
            return false;
        const asset_streaming::ProjectAssetCatalog& catalog = m_workspace->GetAssetCatalog();
        const asset_streaming::ProjectAssetCatalogStatus catalogStatus = catalog.GetStatus();
        if (catalogStatus.sourceGeneration == 0u ||
            catalogStatus.sourceGeneration != m_workspace->GetSources().GetGeneration())
            return false;
        return (rootRelativeFolder.Empty() ? catalog.GetRootFolder(root)
                                           : catalog.ResolveFolder(root, rootRelativeFolder)).IsValid();
    }

    u64 EditorProjectAssetAccess::GetMaximumExternalImportSourceBytes() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->GetMaximumExternalImportSourceBytes() : 0u;
    }

    bool EditorProjectAssetAccess::IsManagedProjectSourcePath(
        const filesystem::AbsolutePath& sourcePath) const noexcept
    {
        if (m_workspace == nullptr || !sourcePath.IsFilePath())
            return false;
        for (const assets::SourceRoot& root : m_workspace->GetSources().GetRoots())
            if (filesystem::ValidateMutationPath(root.directory, sourcePath) == filesystem::MutationPathResult::Success)
                return true;
        return false;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::ImportExternalSource(
        const u32 root, const containers::StringView rootRelativeFolder,
        const containers::StringView sourceFileName, const containers::ArraySpan<const u8> sourceBytes,
        assets::AssetId& importedAsset,
        EditorProjectExternalImportFailure* const failure) const noexcept
    {
        importedAsset = {};
        if (failure != nullptr)
            *failure = {};
        auto fail = [failure](const EditorProjectExternalImportFailureCode code,
                              const PipelineResult result,
                              const assets::AssetOperationResult operation = assets::AssetOperationResult::Success) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->pipeline.result = result;
                failure->pipeline.operation.result = operation;
            }
            return result;
        };
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr)
            return fail(EditorProjectExternalImportFailureCode::PipelineRejected, PipelineResult::InvalidState);
        if (!pipeline->CanStartAssetOperation())
            return fail(EditorProjectExternalImportFailureCode::PipelineRejected, PipelineResult::Busy);
        if (sourceFileName.Empty() || sourceFileName.Length() > resources::MaximumResourcePathBytes ||
            rootRelativeFolder.Length() > resources::MaximumResourcePathBytes)
            return fail(EditorProjectExternalImportFailureCode::InvalidArgument, PipelineResult::InvalidArgument,
                        assets::AssetOperationResult::InvalidArgument);

        const containers::ArraySpan<const assets::SourceRoot> roots = m_workspace->GetSources().GetRoots();
        if (root >= roots.Count())
            return fail(EditorProjectExternalImportFailureCode::StaleDestination, PipelineResult::OperationFailed,
                        assets::AssetOperationResult::SourceUnavailable);
        if (roots[root].readOnly)
            return fail(EditorProjectExternalImportFailureCode::ReadOnlyDestination, PipelineResult::OperationFailed,
                        assets::AssetOperationResult::ReadOnlySource);
        const asset_streaming::ProjectAssetCatalog& catalog = m_workspace->GetAssetCatalog();
        const asset_streaming::ProjectAssetCatalogStatus catalogStatus = catalog.GetStatus();
        if (catalogStatus.sourceGeneration == 0u ||
            catalogStatus.sourceGeneration != m_workspace->GetSources().GetGeneration())
            return fail(EditorProjectExternalImportFailureCode::StaleDestination, PipelineResult::OperationFailed,
                        assets::AssetOperationResult::SourceUnavailable);
        const asset_streaming::ProjectAssetCatalogFolderId folder =
            rootRelativeFolder.Empty() ? catalog.GetRootFolder(root) : catalog.ResolveFolder(root, rootRelativeFolder);
        if (!folder.IsValid())
            return fail(EditorProjectExternalImportFailureCode::StaleDestination, PipelineResult::OperationFailed,
                        assets::AssetOperationResult::SourceUnavailable);

        if (filesystem::paths::GetFileName(sourceFileName) != sourceFileName)
            return fail(EditorProjectExternalImportFailureCode::InvalidArgument, PipelineResult::InvalidArgument,
                        assets::AssetOperationResult::InvalidArgument);
        filesystem::AbsolutePath destinationDirectory = roots[root].directory;
        if (!rootRelativeFolder.Empty())
            destinationDirectory = destinationDirectory.AddDirPath(rootRelativeFolder);
        if (filesystem::ValidateMutationRoot(destinationDirectory) != filesystem::MutationPathResult::Success)
            return fail(EditorProjectExternalImportFailureCode::StaleDestination, PipelineResult::OperationFailed,
                        assets::AssetOperationResult::SourceUnavailable);
        const filesystem::AbsolutePath destination = destinationDirectory.AddFilePath(sourceFileName);
        if (filesystem::ValidateMutationPath(roots[root].directory, destination) != filesystem::MutationPathResult::Success)
            return fail(EditorProjectExternalImportFailureCode::InvalidArgument, PipelineResult::InvalidArgument,
                        assets::AssetOperationResult::InvalidArgument);

        const u64 maximumBytes = pipeline->GetMaximumExternalImportSourceBytes();
        if (sourceBytes.Count() > maximumBytes)
            return fail(EditorProjectExternalImportFailureCode::LimitExceeded, PipelineResult::HistoryLimitExceeded,
                        assets::AssetOperationResult::LimitExceeded);

        const asset_streaming::ProjectAssetExternalImportRequest request{
            destination, sourceBytes};
        asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
        const PipelineResult result = pipeline->ImportExternalSource(request, importedAsset, nullptr, &pipelineFailure);
        if (failure != nullptr)
        {
            failure->pipeline = pipelineFailure;
            if (result != PipelineResult::Success && result != PipelineResult::CommittedCleanupPending &&
                result != PipelineResult::CommittedReconciliationPending)
                failure->code = pipelineFailure.operation.result == assets::AssetOperationResult::LimitExceeded
                                    ? EditorProjectExternalImportFailureCode::LimitExceeded
                                    : EditorProjectExternalImportFailureCode::PipelineRejected;
        }
        return result;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::PrepareModelImportPlan(
        const filesystem::AbsolutePath& sourceOrigin, const containers::ArraySpan<const u8> sourceSnapshot,
        const u32 destinationRoot, const containers::StringView rootRelativeFolder,
        const containers::StringView sourceFileName, const assets::AssetId existingAsset,
        const u64 expectedAcceptedGeneration, const u64 expectedSourceGeneration,
        const mesh_tools::ModelImportManifest& manifest, const mesh_tools::ModelImportSettings& settings,
        mesh_tools::ModelImportPlan& plan, mesh_tools::ModelImportPlanReport* const report,
        assets::AssetId& plannedAsset, asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        plan = {};
        const assets::AssetId provisionalAsset = plannedAsset;
        plannedAsset = {};
        if (report != nullptr)
            *report = {};
        if (failure != nullptr)
            *failure = {};
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return FailAssetAccess(failure, PipelineResult::InvalidState);
        if (!sourceOrigin.IsFilePath() || sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr ||
            sourceSnapshot.Size() > pipeline->GetMaximumExternalImportSourceBytes() ||
            manifest.sourceFingerprint != crypto::Sha256(sourceSnapshot.Data(), sourceSnapshot.Size()))
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);
        if (!pipeline->CanStartAssetOperation())
            return FailAssetAccess(failure, PipelineResult::Busy);

        const assets::SourceDatabase& sources = m_workspace->GetSources();
        const containers::ArraySpan<const assets::SourceRoot> roots = sources.GetRoots();
        const assets::SourceRecord* persisted = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        mesh_tools::ModelImportPlanningContext planning;
        planning.database = &sources;
        planning.acceptedStateGeneration = pipeline->GetAcceptedAssetGeneration();
        planning.sourceOrigin = sourceOrigin;
        planning.importer = assets::HashCompilerName("vanguard.model.importer");
        planning.importerVersion = mesh_tools::ModelSourceImporterVersion;
        planning.outputTypes.mesh = meshes::MeshResourceType;
        planning.outputTypes.material = materials::MaterialResourceType;
        planning.outputTypes.texture = textures::TextureResourceType;
        planning.outputTypes.composition = prefabs::PrefabResourceType;

        if (existingAsset.IsValid())
        {
            asset_streaming::ProjectAssetPipeline* verifiedPipeline = nullptr;
            const PipelineResult verified = ResolveSelectedAsset(
                m_workspace, existingAsset, expectedAcceptedGeneration, expectedSourceGeneration,
                verifiedPipeline, accepted, persisted, failure);
            if (verified != PipelineResult::Success)
            {
                // A model plan is a reviewed generation-fenced snapshot, not
                // a generic asset mutation. Changed catalog/accepted evidence
                // invalidates the plan and requires explicit Reinspect rather
                // than presenting a candidate-settings validation failure.
                if (verified == PipelineResult::OperationFailed)
                    return FailAssetAccess(failure, PipelineResult::StalePlan);
                return verified;
            }
            if (persisted->importer != planning.importer || persisted->metadata.importer != planning.importer ||
                persisted->metadata.importerVersion != planning.importerVersion ||
                persisted->metadata.settingsVersion != mesh_tools::ModelImportSettingsVersion ||
                accepted->presence != asset_streaming::ProjectAssetPresence::Present ||
                accepted->sourceRoot.Empty() || !(sourceOrigin == persisted->path))
                return FailAssetAccess(failure, PipelineResult::StalePlan);
            planning.persistedMetadata = &persisted->metadata;
            planning.parentAsset = existingAsset;
            planning.sourceRoot = accepted->sourceRoot;
            planning.sourceDestination = persisted->path;
            planning.sourceIdentity = persisted->sourceIdentity;
            if (!ModelMetadataFingerprint(persisted->metadata, planning.persistedMetadataDigest))
                return FailAssetAccess(failure, PipelineResult::StalePlan);
            planning.hasPersistedMetadataDigest = true;
        }
        else
        {
            if (sourceFileName.Empty() ||
                filesystem::paths::GetFileName(sourceFileName) != sourceFileName ||
                rootRelativeFolder.Length() > resources::MaximumResourcePathBytes ||
                destinationRoot >= roots.Count() || roots[destinationRoot].readOnly ||
                !CanImportExternalSource(destinationRoot, rootRelativeFolder))
                return FailAssetAccess(failure, PipelineResult::InvalidArgument);
            filesystem::AbsolutePath directory = roots[destinationRoot].directory;
            if (!rootRelativeFolder.Empty())
                directory = directory.AddDirPath(rootRelativeFolder);
            const filesystem::AbsolutePath destination = directory.AddFilePath(sourceFileName);
            if (filesystem::ValidateMutationPath(roots[destinationRoot].directory, destination) !=
                filesystem::MutationPathResult::Success)
                return FailAssetAccess(failure, PipelineResult::InvalidArgument);

            const containers::StringView rootPath = roots[destinationRoot].directory.AsStringView();
            const containers::StringView destinationPath = destination.AsStringView();
            if (destinationPath.Length() <= rootPath.Length())
                return FailAssetAccess(failure, PipelineResult::InvalidArgument);
            planning.sourceIdentity = resources::ResourcePath::FromString(
                destinationPath.Slice(rootPath.Length(), destinationPath.Length()));
            planning.sourceRoot = roots[destinationRoot].directory;
            planning.sourceDestination = destination;
            if (!planning.sourceIdentity.IsValid())
                return FailAssetAccess(failure, PipelineResult::InvalidArgument);
            if (provisionalAsset.IsValid())
            {
                if (sources.ContainsAssetIdentity(provisionalAsset))
                    return FailAssetAccess(failure, PipelineResult::StalePlan);
                planning.parentAsset = provisionalAsset;
            }
            else
            {
                const PipelineResult identity = pipeline->GenerateImportIdentityCandidate(planning.parentAsset, failure);
                if (identity != PipelineResult::Success)
                    return identity;
            }
        }
        plannedAsset = planning.parentAsset;

        containers::DynamicArray<mesh_tools::ModelImportDependencyWitness> dependencies(memory::pools::Tools::GetInstance());
        if (!GatherModelDependencyWitnesses(manifest, settings, sources, *pipeline,
                                            planning.limits.maximumDependencyWitnesses, dependencies))
            return FailAssetAccess(failure, PipelineResult::StalePlan);
        planning.dependencyWitnesses = {dependencies.TypedData(), dependencies.Size()};
        const mesh_tools::ModelImportPlanResult planned =
            mesh_tools::PlanModelImport(manifest, settings, planning, plan, report);
        if (planned != mesh_tools::ModelImportPlanResult::Success)
        {
            // Keep the review's provisional identity through a temporarily
            // invalid settings edit. Only an actual identity collision forces
            // the caller to begin a new first-import identity selection.
            if (!existingAsset.IsValid() && provisionalAsset.IsValid())
                plannedAsset = provisionalAsset;
            return FailAssetAccess(failure, planned == mesh_tools::ModelImportPlanResult::MetadataMismatch ||
                                            planned == mesh_tools::ModelImportPlanResult::IdentityConflict
                                                ? PipelineResult::StalePlan : PipelineResult::OperationFailed);
        }
        // A syntactically valid model plan is not necessarily buildable by the
        // current production prefab/material backends. Reject known unsupported
        // output semantics at review, before committing source/VMETA. This
        // checker is pure; invoking createBuildRequest here would incorrectly
        // publish compiler-generated dependencies during speculative planning.
        const model_tools::ModelImportBuildabilityFailure unsupported =
            model_tools::CheckReviewedModelBuildability(manifest, settings);
        if (unsupported.reason != model_tools::ModelImportBuildability::Supported)
        {
            const char* explanation = "The selected model output cannot be built by the current compiler.";
            switch (unsupported.reason)
            {
            case model_tools::ModelImportBuildability::CompositionNeedsRigidUnitScale:
                explanation = "Composition prefab requires rigid unit-scale node transforms. Disable 'Generate composition prefab' or author a rigid hierarchy.";
                break;
            case model_tools::ModelImportBuildability::DefaultMaterialReferenceRequired:
                explanation = "'Use default material' requires an existing managed VMAT default material reference.";
                break;
            case model_tools::ModelImportBuildability::UnsupportedBlendMaterial:
                explanation = "Model-owned BLEND transparency is not supported by the current imported-surface renderer.";
                break;
            case model_tools::ModelImportBuildability::UnsupportedSpecularMaterial:
                explanation = "Model-owned specular texture/color is not supported by the current imported-surface shader.";
                break;
            case model_tools::ModelImportBuildability::UnsupportedMaterialTextureSemantic:
                explanation = "A model-owned material uses an unsupported texture semantic.";
                break;
            case model_tools::ModelImportBuildability::UnsupportedTextureUvSet:
                explanation = "A model-owned material uses a texture UV set other than UV0.";
                break;
            case model_tools::ModelImportBuildability::Supported: break;
            }
            if (report != nullptr)
            {
                ++report->blockers;
                report->diagnostic = containers::String::Printf("Unsupported reviewed output %u: %s",
                                                                  unsupported.sourceIndex, explanation);
            }
            return FailAssetAccess(failure, PipelineResult::OperationFailed);
        }
        return PipelineResult::Success;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::CommitModelImportPlan(
        const mesh_tools::ModelImportPlan& plan, const containers::ArraySpan<const u8> sourceSnapshot,
        assets::AssetId& importedAsset, asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        importedAsset = {};
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return FailAssetAccess(failure, PipelineResult::InvalidState);
        if (plan.HasBlockingConflicts() || sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr)
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);

        // The transaction writes a large source into a same-directory .vtxn-stage
        // file before its atomic cutover. With the native Windows watcher running,
        // the many size/write notifications can overflow its finite kernel buffer
        // even though normal .vtxn-stage path hints are ignored. The conservative
        // overflow fence then rolls back this exact reviewed commit on *every*
        // retry, after rewriting the entire stage file on the UI thread.
        //
        // Stop the watcher only at a quiescent source-reconciliation boundary for
        // large reviewed commits. DisableObservation joins all callbacks and
        // reconciles any last hint, or returns Busy without disabling admission.
        // CommitPlannedImport still performs full source/metadata/catalog proof,
        // per-file transaction assertions and post-commit authoritative Rescan.
        // The Project Workspace frame driver restarts observation with its
        // mandatory generation-zero catch-up, including after a failed commit.
        containers::DynamicArray<assets::PlannedAssetDependencyWitness> dependencies(memory::pools::Tools::GetInstance());
        assets::PlannedAssetImportRequest request;
        if (!mesh_tools::BuildModelPlannedImportRequest(plan, sourceSnapshot, dependencies, request))
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);

        // Only an independent, create-only external first import may use the
        // observer-free commit already supported by the pipeline. Reimports and
        // adopted/borrowed project dependencies retain their stronger global
        // watcher-generation fence until the transaction owner has a staged
        // watcher-safe cutover for those dependent mutations.
        constexpr u64 LargeWatchedSourceBytes = 8ull * 1024ull * 1024ull;
        const bool independentLargeFirstImport = sourceSnapshot.Count() >= LargeWatchedSourceBytes &&
                                                 request.copySourceToDestination &&
                                                 !request.hasPersistedMetadataFingerprint &&
                                                 request.dependencyWitnesses.Empty();
        if (independentLargeFirstImport && pipeline->IsObservingSources())
        {
            const PipelineResult stopped = pipeline->DisableObservation(failure);
            if (stopped != PipelineResult::Success)
                return stopped;
        }

        u64 predecessorGeneration = 0u;
        if (plan.GetWitnesses().hasPersistedMetadataFingerprint)
        {
            const asset_streaming::ProjectAssetState* const predecessor =
                pipeline->LookupAcceptedAsset(plan.GetWitnesses().parentAsset);
            if (predecessor == nullptr || predecessor->presence != asset_streaming::ProjectAssetPresence::Present)
                return FailAssetAccess(failure, PipelineResult::StalePlan);
            predecessorGeneration = predecessor->acceptedGeneration;
        }

        const PipelineResult committed = pipeline->CommitPlannedImport(
            request, importedAsset, nullptr, failure,
            independentLargeFirstImport && !pipeline->IsObservingSources());
        if (committed != PipelineResult::Success && committed != PipelineResult::CommittedCleanupPending)
            return committed;
        const PipelineResult admission =
            ResumeCommittedModelImport(plan, importedAsset, predecessorGeneration, failure);
        if (admission != PipelineResult::Success)
        {
            // The source/VMETA commit is already durable. Return a committed
            // pending status, but preserve the separate cohort-admission
            // diagnostic from Resume (including BuildRequestFailed and the
            // failing ProjectAssetBuildResult). Overwriting `failure` here
            // would misrepresent an imported source as a mere reconciliation
            // delay and erase the actionable build failure.
            if (failure != nullptr && failure->result == PipelineResult::Success)
                failure->result = admission;
            return PipelineResult::CommittedReconciliationPending;
        }
        return committed;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::ResumeCommittedModelImport(
        const mesh_tools::ModelImportPlan& reviewedPlan, const assets::AssetId asset,
        const u64 previousAcceptedGeneration,
        asset_streaming::ProjectAssetPipelineFailure* const failure, const bool validateOnly) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return FailAssetAccess(failure, PipelineResult::InvalidState);
        const mesh_tools::ModelImportPlanWitnesses& witness = reviewedPlan.GetWitnesses();
        if (!asset.IsValid() || asset != witness.parentAsset || witness.sourceFingerprint.IsEmpty() ||
            witness.candidateSettingsFingerprint.IsEmpty())
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);
        const asset_streaming::ProjectAssetState* const accepted = pipeline->LookupAcceptedAsset(asset);
        // The old accepted row is allowed to remain readable while a durable
        // VMETA publication is waiting for source reconciliation. NEVER admit
        // an explicit cohort for that predecessor, even if reimport kept an
        // identical output schema and source bytes.
        if (accepted == nullptr || accepted->acceptedGeneration <= previousAcceptedGeneration)
            return FailAssetAccess(failure, PipelineResult::CommittedReconciliationPending);
        if (accepted->presence != asset_streaming::ProjectAssetPresence::Present ||
            accepted->outputs.Empty() || accepted->sourceIdentity != witness.sourceIdentity ||
            accepted->physicalPath != witness.sourceDestination || accepted->sourceRoot != witness.sourceRoot ||
            !accepted->hasSourceContentFingerprint || accepted->sourceContentFingerprint != witness.sourceFingerprint ||
            accepted->importer != witness.importer || accepted->authoredImporterVersion != witness.importerVersion ||
            accepted->settingsVersion != witness.settingsVersion)
            return FailAssetAccess(failure, PipelineResult::StalePlan);

        bool requiresAdoptionFinalization = false;
        for (const mesh_tools::ModelImportPlannedTextureResolution& resolution : reviewedPlan.GetTextureResolutions())
            requiresAdoptionFinalization = requiresAdoptionFinalization ||
                resolution.resolution == mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource;
        // Planning reconciles candidate settings into the actual canonical
        // authored settings (persistent output/slot correspondences). Compare
        // against the *planned import description*, never against the earlier
        // candidateSettingsFingerprint. P5 can further finalize adopted-image
        // identities transactionally, so that one case has a different digest.
        const containers::DynamicArray<u8>& proposedSettings = reviewedPlan.GetImportDescription().settings;
        if (!requiresAdoptionFinalization &&
            (proposedSettings.Empty() || accepted->settingsFingerprint !=
                crypto::Sha256(proposedSettings.TypedData(), proposedSettings.Size())))
            return FailAssetAccess(failure, PipelineResult::StalePlan);

        const containers::ArraySpan<const assets::AssetOutput> proposedOutputs =
            {reviewedPlan.GetImportDescription().outputs.TypedData(),
             reviewedPlan.GetImportDescription().outputs.Size()};
        if (proposedOutputs.Count() != accepted->outputs.Count())
            return FailAssetAccess(failure, PipelineResult::StalePlan);

        containers::DynamicArray<resources::ResourceReference> cohort(memory::pools::Tools::GetInstance());
        containers::DynamicArray<resources::ResourceReference> expected(memory::pools::Tools::GetInstance());
        cohort.Reserve(accepted->outputs.Count());
        expected.Reserve(proposedOutputs.Count());
        if (cohort.Capacity() < accepted->outputs.Count() || expected.Capacity() < proposedOutputs.Count())
            return FailAssetAccess(failure, PipelineResult::HistoryLimitExceeded);
        for (const asset_streaming::ProjectAssetOutputState& output : accepted->outputs)
            cohort.PushBack(output.resource);
        for (const assets::AssetOutput& output : proposedOutputs)
            expected.PushBack(assets::GetAssetOutputReference(asset, output));
        if (cohort.Size() != accepted->outputs.Count() || expected.Size() != proposedOutputs.Count())
            return FailAssetAccess(failure, PipelineResult::HistoryLimitExceeded);
        const auto compareReferences = [](const resources::ResourceReference left,
                                          const resources::ResourceReference right) noexcept
        {
            return left.GetPath() != right.GetPath() ? left.GetPath() < right.GetPath()
                                                     : left.ExpectedType() < right.ExpectedType();
        };
        std::sort(cohort.TypedData(), cohort.TypedData() + cohort.Size(), compareReferences);
        std::sort(expected.TypedData(), expected.TypedData() + expected.Size(), compareReferences);
        for (u32 index = 0u; index < cohort.Size(); ++index)
            if (!cohort[index].IsTyped() || cohort[index] != expected[index])
                return FailAssetAccess(failure, PipelineResult::StalePlan);
        // A committed-but-not-yet-accepted source must never be mistaken for
        // its predecessor, and a hidden-panel UI observation must not re-admit
        // or restart an already in-flight cohort. This mode validates the exact
        // accepted successor without mutating pipeline state.
        if (validateOnly)
            return PipelineResult::Success;
        const asset_streaming::ProjectAssetCohortDeliveryRequest cohortRequest{
            asset, accepted->acceptedGeneration, {cohort.TypedData(), cohort.Size()}};
        return pipeline->RequestAssetCohortDelivery(cohortRequest, failure);
    }

    asset_streaming::ProjectAssetDeferredPlannedImportStatus
    EditorProjectAssetAccess::GetDeferredModelImportStatus() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->GetDeferredPlannedImportStatus()
                                   : asset_streaming::ProjectAssetDeferredPlannedImportStatus{};
    }

    asset_streaming::ProjectAssetAutomaticRecookStatus EditorProjectAssetAccess::GetAutomaticRecookStatus() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->GetAutomaticRecookStatus()
                                   : asset_streaming::ProjectAssetAutomaticRecookStatus{};
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::CancelAutomaticRecook(
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->CancelAutomaticRecook(failure)
                                   : FailAssetAccess(failure, PipelineResult::InvalidState);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::RetryAutomaticRecook(
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->RetryAutomaticRecook(failure)
                                   : FailAssetAccess(failure, PipelineResult::InvalidState);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::RetryAutomaticPublication(
        const resources::ResourceReference output, asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->RetryAutomaticPublication(output, failure)
                                   : FailAssetAccess(failure, PipelineResult::InvalidState);
    }

    bool EditorProjectAssetAccess::ResolveSourcePhysicalPath(
        const resources::ResourcePath sourceIdentity, const u64 expectedSourceGeneration,
        filesystem::AbsolutePath& sourcePath) const noexcept
    {
        sourcePath = {};
        if (m_workspace == nullptr || !sourceIdentity.IsValid() || expectedSourceGeneration == 0u)
            return false;
        const assets::SourceDatabase& sources = m_workspace->GetSources();
        if (sources.GetGeneration() != expectedSourceGeneration)
            return false;
        const assets::SourceRecord* const source = sources.LookupSource(sourceIdentity);
        if (source == nullptr || !source->path.IsFilePath())
            return false;
        sourcePath = source->path;
        return true;
    }

    bool EditorProjectAssetAccess::CanImportCandidate(
        const resources::ResourcePath sourceIdentity, const assets::SourceImporterId importer,
        const u32 importerVersion, const u64 sourceGeneration) const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr || !sourceIdentity.IsValid() || importer == assets::InvalidSourceImporterId ||
            importerVersion == 0u || sourceGeneration == 0u)
            return false;
        const assets::SourceRecord* const source = m_workspace->GetSources().LookupSource(sourceIdentity);
        if (source == nullptr)
            return false;
        const asset_streaming::ProjectAssetCandidateImportRequest request{
            source->path, sourceIdentity, importer, importerVersion, sourceGeneration};
        return pipeline->CanImportCandidate(request);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::ImportCandidate(
        const resources::ResourcePath sourceIdentity, const assets::SourceImporterId importer,
        const u32 importerVersion, const u64 sourceGeneration, assets::AssetId& importedAsset,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        importedAsset = {};
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        if (pipeline == nullptr)
            return FailAssetAccess(failure, PipelineResult::InvalidState);
        if (!sourceIdentity.IsValid() || importer == assets::InvalidSourceImporterId || importerVersion == 0u ||
            sourceGeneration == 0u)
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);
        const assets::SourceRecord* const source = m_workspace->GetSources().LookupSource(sourceIdentity);
        if (source == nullptr)
            return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                   assets::AssetOperationResult::CandidateUnavailable);
        const asset_streaming::ProjectAssetCandidateImportRequest request{
            source->path, sourceIdentity, importer, importerVersion, sourceGeneration};
        return pipeline->ImportCandidate(request, importedAsset, nullptr, failure);
    }

    bool EditorProjectAssetAccess::CopyAssetSettings(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration, const containers::ArraySpan<u8> settings,
        EditorProjectAssetSettingsSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        if (ResolveSelectedAsset(m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
                                 pipeline, accepted, source, nullptr) != PipelineResult::Success)
            return false;
        const u32 byteCount = source->metadata.settings.Size();
        if (byteCount > settings.Size())
            return false;
        if (byteCount != 0u)
            std::memcpy(settings.Data(), source->metadata.settings.TypedData(), byteCount);
        EditorProjectAssetSettingsSnapshot captured;
        captured.importer = source->metadata.importer;
        captured.authoredImporterVersion = source->metadata.importerVersion;
        captured.availableImporterVersion = source->availableImporterVersion;
        captured.settingsVersion = source->metadata.settingsVersion;
        captured.settingsBytes = byteCount;
        captured.sourceGeneration = expectedSourceGeneration;
        captured.acceptedGeneration = expectedAcceptedGeneration;
        snapshot = captured;
        return true;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::UpdateAssetSettings(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration, const u32 settingsVersion,
        const containers::ArraySpan<const u8> settings,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        const PipelineResult validation = ResolveSelectedAsset(
            m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
            pipeline, accepted, source, failure);
        if (validation != PipelineResult::Success)
            return validation;
        const asset_streaming::ProjectAssetSettingsUpdateRequest request{
            asset, expectedAcceptedGeneration, settingsVersion, settings};
        return pipeline->UpdateAssetSettings(request, nullptr, failure);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::RequestAssetReimport(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        const PipelineResult validation = ResolveSelectedAsset(
            m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
            pipeline, accepted, source, failure);
        return validation == PipelineResult::Success ? pipeline->RequestAssetReimport(asset, failure) : validation;
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::MoveAsset(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration, const containers::StringView rootRelativeTarget,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        if (!IsRelativeMutationTarget(rootRelativeTarget))
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        const PipelineResult validation = ResolveSelectedAsset(
            m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
            pipeline, accepted, source, failure);
        if (validation != PipelineResult::Success)
            return validation;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_workspace->GetSources().GetRoots();
        if (source->root >= roots.Count())
            return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                   assets::AssetOperationResult::SourceUnavailable, asset);
        const filesystem::AbsolutePath target = roots[source->root].directory.AddFilePath(rootRelativeTarget);
        if (filesystem::ValidateMutationPath(roots[source->root].directory, target) != filesystem::MutationPathResult::Success)
            return FailAssetAccess(failure, PipelineResult::InvalidArgument,
                                   assets::AssetOperationResult::InvalidArgument, asset);
        return pipeline->MoveAsset(asset, target, nullptr, failure);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::DuplicateAsset(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration, const containers::StringView rootRelativeTarget,
        assets::AssetId& duplicatedAsset,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        duplicatedAsset = {};
        if (!IsRelativeMutationTarget(rootRelativeTarget))
            return FailAssetAccess(failure, PipelineResult::InvalidArgument);
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        const PipelineResult validation = ResolveSelectedAsset(
            m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
            pipeline, accepted, source, failure);
        if (validation != PipelineResult::Success)
            return validation;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_workspace->GetSources().GetRoots();
        if (source->root >= roots.Count())
            return FailAssetAccess(failure, PipelineResult::OperationFailed,
                                   assets::AssetOperationResult::SourceUnavailable, asset);
        const filesystem::AbsolutePath target = roots[source->root].directory.AddFilePath(rootRelativeTarget);
        if (filesystem::ValidateMutationPath(roots[source->root].directory, target) != filesystem::MutationPathResult::Success)
            return FailAssetAccess(failure, PipelineResult::InvalidArgument,
                                   assets::AssetOperationResult::InvalidArgument, asset);
        return pipeline->DuplicateAssetWithGeneratedIdentity(asset, target, duplicatedAsset, nullptr, failure);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::DeleteAsset(
        const assets::AssetId asset, const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration,
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
        const asset_streaming::ProjectAssetState* accepted = nullptr;
        const assets::SourceRecord* source = nullptr;
        const PipelineResult validation = ResolveSelectedAsset(
            m_workspace, asset, expectedAcceptedGeneration, expectedSourceGeneration,
            pipeline, accepted, source, failure);
        return validation == PipelineResult::Success ? pipeline->DeleteAsset(asset, {}, nullptr, failure) : validation;
    }

    bool EditorProjectAssetAccess::CanUndoAssetOperation() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr && pipeline->CanUndoAssetOperation();
    }

    bool EditorProjectAssetAccess::CanRedoAssetOperation() const noexcept
    {
        const asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr && pipeline->CanRedoAssetOperation();
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::UndoAssetOperation(
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->UndoAssetOperation(nullptr, failure)
                                   : FailAssetAccess(failure, PipelineResult::InvalidState);
    }

    asset_streaming::ProjectAssetPipelineResult EditorProjectAssetAccess::RedoAssetOperation(
        asset_streaming::ProjectAssetPipelineFailure* const failure) const noexcept
    {
        asset_streaming::ProjectAssetPipeline* const pipeline =
            m_workspace != nullptr ? m_workspace->GetAssetPipeline() : nullptr;
        return pipeline != nullptr ? pipeline->RedoAssetOperation(nullptr, failure)
                                   : FailAssetAccess(failure, PipelineResult::InvalidState);
    }

    bool EditorProjectAssetAccess::RequestTexturePreview(const resources::ResourceReference output,
                                                         const ProjectTexturePreviewRequest& request,
                                                         ProjectTexturePreviewHandle& handle) const noexcept
    {
        handle = {};
        return m_workspace != nullptr && m_workspace->RequestTexturePreview(output, request, handle);
    }

    bool EditorProjectAssetAccess::RequestTexturePreview(const resources::ResourceReference output,
                                                         const ProjectTexturePreviewPriority priority,
                                                         ProjectTexturePreviewHandle& handle) const noexcept
    {
        ProjectTexturePreviewRequest request;
        request.priority = priority;
        return RequestTexturePreview(output, request, handle);
    }

    bool EditorProjectAssetAccess::UpdateTexturePreview(const ProjectTexturePreviewHandle handle,
                                                        const ProjectTexturePreviewRequest& request) const noexcept
    {
        return m_workspace != nullptr && m_workspace->UpdateTexturePreview(handle, request);
    }

    bool EditorProjectAssetAccess::UpdateTexturePreviewPriority(const ProjectTexturePreviewHandle handle,
                                                                const ProjectTexturePreviewPriority priority) const noexcept
    {
        return m_workspace != nullptr && m_workspace->UpdateTexturePreviewPriority(handle, priority);
    }

    bool EditorProjectAssetAccess::QueryTexturePreview(const ProjectTexturePreviewHandle handle,
                                                       ProjectTexturePreviewInfo& info) const noexcept
    {
        info = {};
        return m_workspace != nullptr && m_workspace->QueryTexturePreview(handle, info);
    }

    bool EditorProjectTexturePreviewPresentationAccess::Register(const ProjectTexturePreviewHandle handle,
                                                                 EditorUiService& ui,
                                                                 const rhi::SamplerStateRef sampler,
                                                                 const EditorTextureColorSpace colorSpace,
                                                                 EditorTextureHandle& texture) const noexcept
    {
        texture = {};
        return m_workspace != nullptr &&
               m_workspace->RegisterTexturePreview(handle, ui, sampler, colorSpace, texture);
    }

    bool EditorProjectAssetAccess::ReleaseTexturePreview(ProjectTexturePreviewHandle& handle,
                                                         const u64 capturedUiFrame) const noexcept
    {
        return m_workspace != nullptr && m_workspace->ReleaseTexturePreview(handle, capturedUiFrame);
    }
} // namespace vanguard::editor

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;

    constexpr vanguard::u32 ProjectAssetCatalogCaptureRowsPerFrame = 16384u;
    constexpr vanguard::u32 ProjectAssetCatalogCapturePathBytesPerFrame = 4u * 1024u * 1024u;

    inline constexpr vanguard::u32 ResidentRetirementBatchSize = 256u;
    inline constexpr vanguard::u32 MaximumResidentRetirementsPerPump =
        vanguard::rendering::TextureResidencyConfig{}.maximumPendingRetirements;
    inline constexpr vanguard::u32 MaximumResidentRetirementBatches =
        (MaximumResidentRetirementsPerPump + ResidentRetirementBatchSize - 1u) / ResidentRetirementBatchSize;
    inline constexpr vanguard::u32 ResidentDeliveryChecksPerFrame = 64u;
    inline constexpr vanguard::u32 InvalidProjectTexturePreviewMip = ~vanguard::u32{0};
    inline constexpr vanguard::u64 ProjectTexturePreviewQualityRetryBaseFrames = 8u;
    inline constexpr vanguard::u32 ProjectTexturePreviewQualityRetryMaximumShift = 6u;

    std::atomic<vanguard::u64> g_projectTexturePreviewNextOwnerCookie{1u};

    [[nodiscard]] vanguard::u64 AcquireProjectTexturePreviewOwnerCookie() noexcept
    {
        vanguard::u64 identity = g_projectTexturePreviewNextOwnerCookie.fetch_add(1u, std::memory_order_relaxed);
        if (identity != 0u)
            return identity;
        identity = g_projectTexturePreviewNextOwnerCookie.fetch_add(1u, std::memory_order_relaxed);
        return identity != 0u ? identity : 1u;
    }

    struct ProjectTexturePreviewInterest final
    {
        editor::ProjectTexturePreviewRequest request;
        vanguard::u32 resolvedFirstResidentMip = InvalidProjectTexturePreviewMip;
        vanguard::u32 backingIndex = editor::InvalidProjectTexturePreviewIndex;
        vanguard::u32 generation = 0u;
        vanguard::u32 nextFree = editor::InvalidProjectTexturePreviewIndex;
        bool occupied = false;
    };

    struct ProjectTexturePreviewBacking final
    {
        vanguard::resources::ResourceReference resource;
        vanguard::resources::PipelineRequest request;
        vanguard::resources::ResourceHandle loadedResource;
        vanguard::rendering::TextureDemandHandle textureDemand;
        vanguard::rhi::Texture readyTexture;
        vanguard::assets::BuildFingerprint buildFingerprint;
        vanguard::assets::BuildFingerprint contentFingerprint;
        editor::ProjectTexturePreviewState state = editor::ProjectTexturePreviewState::Queued;
        editor::ProjectTexturePreviewPriority priority = editor::ProjectTexturePreviewPriority::Background;
        vanguard::u32 desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
        vanguard::u32 currentFirstResidentMip = InvalidProjectTexturePreviewMip;
        vanguard::u64 retireAfterUiFrame = 0u;
        vanguard::u64 qualityRetryAfterUiFrame = 0u;
        vanguard::u64 presentationRevision = 0u;
        vanguard::u64 identity = 0u;
        vanguard::u64 supersessionPredecessorIdentity = 0u;
        vanguard::u32 qualityRetryShift = 0u;
        vanguard::u32 interestCount = 0u;
        vanguard::u32 nextFree = editor::InvalidProjectTexturePreviewIndex;
        vanguard::u32 supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
        bool occupied = false;
    };

    enum class ProjectTexturePreviewResidencyAdmission : vanguard::u8
    {
        None,
        Ordinary,
        Supersession
    };

    void ResetProjectTexturePreviewQualityRetry(ProjectTexturePreviewBacking& backing) noexcept
    {
        backing.qualityRetryAfterUiFrame = 0u;
        backing.qualityRetryShift = 0u;
    }

    void DeferProjectTexturePreviewQualityRetry(ProjectTexturePreviewBacking& backing,
                                                const vanguard::u64 currentUiFrame) noexcept
    {
        const vanguard::u32 shift =
            backing.qualityRetryShift < ProjectTexturePreviewQualityRetryMaximumShift
                ? backing.qualityRetryShift
                : ProjectTexturePreviewQualityRetryMaximumShift;
        const vanguard::u64 delay = ProjectTexturePreviewQualityRetryBaseFrames << shift;
        backing.qualityRetryAfterUiFrame =
            currentUiFrame > ~vanguard::u64{0} - delay ? ~vanguard::u64{0} : currentUiFrame + delay;
        if (backing.qualityRetryShift < ProjectTexturePreviewQualityRetryMaximumShift)
            ++backing.qualityRetryShift;
    }

    enum class ResidentDeliveryPumpResult : vanguard::u8
    {
        Success,
        Pending,
        Busy,
        StreamingFailure,
        RenderingFailure,
        ProjectFailure
    };

    class ManagedProjectWorkspaceService final : public editor::ProjectWorkspaceService
    {
    public:
        explicit ManagedProjectWorkspaceService(const editor::ProjectWorkspaceConfig* const config) noexcept
            : m_previewOwnerCookie(AcquireProjectTexturePreviewOwnerCookie())
        {
            if (config != nullptr)
            {
                m_projectFile = config->projectFile;
                m_project = config->project;
            }
        }

        [[nodiscard]] const vanguard::projects::ProjectDescriptor& GetProject() const noexcept override
        {
            return m_project;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectFile() const noexcept override
        {
            return m_projectFile;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectRoot() const noexcept override
        {
            return m_workspace.root;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetAssetsRoot() const noexcept override
        {
            return m_workspace.assets;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetDerivedDataRoot() const noexcept override
        {
            return m_workspace.derivedData;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetIntermediateRoot() const noexcept override
        {
            return m_workspace.intermediate;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetSavedRoot() const noexcept override
        {
            return m_workspace.saved;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetBuildsRoot() const noexcept override
        {
            return m_workspace.builds;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetConfigRoot() const noexcept override
        {
            return m_workspace.config;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetPluginsRoot() const noexcept override
        {
            return m_workspace.plugins;
        }

        const vanguard::assets::SourceDatabase& GetSources() const noexcept override { return m_sources; }
        const vanguard::asset_streaming::ProjectAssetCatalog& GetAssetCatalog() const noexcept override { return m_assetCatalog; }
        vanguard::asset_streaming::ProjectAssetPipeline* GetAssetPipeline() noexcept override { return &m_assetPipeline; }
        const vanguard::asset_streaming::ProjectAssetPipeline* GetAssetPipeline() const noexcept override { return &m_assetPipeline; }
        bool RequestTexturePreview(const vanguard::resources::ResourceReference output,
                                   const editor::ProjectTexturePreviewRequest& request,
                                   editor::ProjectTexturePreviewHandle& handle) noexcept override
        {
            handle = {};
            if (!m_previewAccepting || m_previewOwnerCookie == 0u || !output.IsValid() || !output.IsTyped() ||
                output.ExpectedType() != vanguard::textures::TextureResourceType || !IsProjectTexturePreviewRequestValid(request) ||
                m_streamingService == nullptr)
                return false;
            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus publication;
            if (!QueryProjectTexturePreviewPublication(output, publication))
                return false;

            const vanguard::u32 interestIndex = AcquireProjectTexturePreviewInterestIndex();
            if (interestIndex == editor::InvalidProjectTexturePreviewIndex)
                return false;

            const vanguard::u32 backingIndex = FindOrCreateProjectTexturePreviewBacking(output, publication);
            if (backingIndex == editor::InvalidProjectTexturePreviewIndex)
            {
                ReturnProjectTexturePreviewInterestIndex(interestIndex);
                return false;
            }

            ProjectTexturePreviewInterest& interest = m_previewInterests[interestIndex];
            ++interest.generation;
            VG_ASSERT_MSG(interest.generation != 0u, "project texture preview interest generation may not wrap");
            interest.request = request;
            interest.resolvedFirstResidentMip = InvalidProjectTexturePreviewMip;
            interest.backingIndex = backingIndex;
            interest.occupied = true;
            ++m_previewLiveInterests;

            ProjectTexturePreviewBacking& backing = m_previewBackings[backingIndex];
            ++backing.interestCount;
            if (!RefreshProjectTexturePreviewBackingDemand(backingIndex))
            {
                RecycleProjectTexturePreviewInterest(interestIndex, interest);
                --backing.interestCount;
                if (backing.interestCount == 0u)
                    RecycleProjectTexturePreviewBacking(backingIndex, backing);
                else
                    static_cast<void>(RefreshProjectTexturePreviewBackingDemand(backingIndex));
                return false;
            }
            handle = {interestIndex, interest.generation, m_previewOwnerCookie};
            return true;
        }
        bool UpdateTexturePreview(const editor::ProjectTexturePreviewHandle handle,
                                  const editor::ProjectTexturePreviewRequest& request) noexcept override
        {
            if (!IsProjectTexturePreviewRequestValid(request))
                return false;
            ProjectTexturePreviewInterest* const interest = ResolveProjectTexturePreviewInterest(handle);
            if (interest == nullptr)
                return false;
            const editor::ProjectTexturePreviewRequest previousRequest = interest->request;
            const vanguard::u32 previousResolvedFirstResidentMip = interest->resolvedFirstResidentMip;
            interest->request = request;
            if (RefreshProjectTexturePreviewBackingDemand(interest->backingIndex))
                return true;
            interest->request = previousRequest;
            interest->resolvedFirstResidentMip = previousResolvedFirstResidentMip;
            static_cast<void>(RefreshProjectTexturePreviewBackingDemand(interest->backingIndex));
            return false;
        }
        bool UpdateTexturePreviewPriority(const editor::ProjectTexturePreviewHandle handle,
                                          const editor::ProjectTexturePreviewPriority priority) noexcept override
        {
            if (!IsProjectTexturePreviewPriority(priority))
                return false;
            ProjectTexturePreviewInterest* const interest = ResolveProjectTexturePreviewInterest(handle);
            if (interest == nullptr)
                return false;
            editor::ProjectTexturePreviewRequest request = interest->request;
            request.priority = priority;
            return UpdateTexturePreview(handle, request);
        }
        bool QueryTexturePreview(const editor::ProjectTexturePreviewHandle handle,
                                 editor::ProjectTexturePreviewInfo& info) noexcept override
        {
            info = {};
            const ProjectTexturePreviewInterest* const interest = ResolveProjectTexturePreviewInterest(handle);
            if (interest == nullptr)
                return false;
            const ProjectTexturePreviewBacking* const backing = ResolveProjectTexturePreviewBacking(interest->backingIndex);
            if (backing == nullptr)
                return false;
            info.state = backing->state;
            info.resource = backing->resource;
            info.buildFingerprint = backing->buildFingerprint;
            info.contentFingerprint = backing->contentFingerprint;
            info.presentationRevision = backing->presentationRevision;
            info.qualitySatisfied =
                backing->state == editor::ProjectTexturePreviewState::Ready &&
                backing->currentFirstResidentMip != InvalidProjectTexturePreviewMip &&
                interest->resolvedFirstResidentMip != InvalidProjectTexturePreviewMip &&
                backing->currentFirstResidentMip <= interest->resolvedFirstResidentMip;
            if (backing->loadedResource.IsValid())
            {
                info.resourceGeneration = backing->loadedResource.GetGeneration();
                if (backing->loadedResource.GetType() == vanguard::textures::TextureResourceType)
                {
                    const auto* const texture = static_cast<const vanguard::textures::TextureResourceObject*>(backing->loadedResource.Get());
                    if (texture != nullptr && texture->IsOpen())
                    {
                        info.width = texture->GetMetadata().GetWidth();
                        info.height = texture->GetMetadata().GetHeight();
                        info.dimension = texture->GetMetadata().GetDimension();
                        info.arrayLayers = texture->GetMetadata().GetArrayLayers();
                        info.srgb = texture->GetMetadata().GetSpace() == vanguard::textures::ColorSpace::SRgb;
                    }
                }
            }
            return true;
        }
        bool RegisterTexturePreview(const editor::ProjectTexturePreviewHandle handle,
                                    editor::EditorUiService& ui,
                                    const vanguard::rhi::SamplerStateRef sampler,
                                    const editor::EditorTextureColorSpace colorSpace,
                                    editor::EditorTextureHandle& texture) noexcept override
        {
            texture = {};
            if (!sampler.IsValid())
                return false;
            const ProjectTexturePreviewInterest* const interest = ResolveProjectTexturePreviewInterest(handle);
            if (interest == nullptr)
                return false;
            const ProjectTexturePreviewBacking* const backing = ResolveProjectTexturePreviewBacking(interest->backingIndex);
            if (backing == nullptr || backing->state != editor::ProjectTexturePreviewState::Ready ||
                !backing->readyTexture.IsValid())
                return false;
            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus currentPublication;
            if (!QueryProjectTexturePreviewPublication(backing->resource, currentPublication) ||
                !SameProjectTexturePreviewPublication(*backing, currentPublication))
                return false;
            vanguard::rhi::TextureDesc physicalDesc;
            if (!vanguard::rhi::GetTextureDesc(backing->readyTexture.GetRef(), physicalDesc) ||
                physicalDesc.dimension != vanguard::rhi::TextureDimension::Texture2D || physicalDesc.sampleCount != 1u ||
                physicalDesc.arraySize != 1u || physicalDesc.virtualResource)
                return false;
            editor::EditorTextureDesc desc;
            desc.texture = backing->readyTexture.GetRef();
            desc.sampler = sampler;
            desc.colorSpace = colorSpace;
            editor::EditorUiFailure failure;
            return ui.RegisterTexture(desc, texture, &failure);
        }
        bool ReleaseTexturePreview(editor::ProjectTexturePreviewHandle& handle,
                                   const vanguard::u64 capturedUiFrame) noexcept override
        {
            ProjectTexturePreviewInterest* const interest = ResolveProjectTexturePreviewInterest(handle);
            if (interest == nullptr || m_previewLiveInterests == 0u)
                return false;

            const vanguard::u32 interestIndex = handle.index;
            const vanguard::u32 backingIndex = interest->backingIndex;
            ProjectTexturePreviewBacking* const backing = ResolveProjectTexturePreviewBacking(backingIndex);
            if (backing == nullptr || backing->interestCount == 0u)
                return false;

            // A shared Ready backing can have several independent UI consumers.
            // Each consumer unregisters its presentation-owned EditorTextureHandle
            // before release and supplies the frame that may have captured it. Keep
            // the maximum fence across all sharers; the final releaser may itself
            // have no UI registration even though an earlier sibling did.
            if (backing->state == editor::ProjectTexturePreviewState::Ready &&
                capturedUiFrame > backing->retireAfterUiFrame)
                backing->retireAfterUiFrame = capturedUiFrame;
            RecycleProjectTexturePreviewInterest(interestIndex, *interest);
            --backing->interestCount;
            if (backing->interestCount == 0u)
            {
                if (backing->state == editor::ProjectTexturePreviewState::Ready && backing->retireAfterUiFrame != 0u)
                    backing->state = editor::ProjectTexturePreviewState::Retiring;
                else if (backing->state == editor::ProjectTexturePreviewState::LoadingResource &&
                         backing->request && !backing->request.HasFinished())
                {
                    // PipelineRequest::Reset cancels only the root interest; an
                    // active ResourceReadRequest may remain live until its async
                    // cancellation callback completes. Keep the request owned by
                    // this backing instead. Normal frame progression retires it
                    // after true pipeline completion, and workspace quiesce can
                    // synchronously drain the same owner-visible request before
                    // ResourceStreaming performs its zero-active-work checks.
                    backing->state = editor::ProjectTexturePreviewState::Retiring;
                    backing->retireAfterUiFrame = 0u;
                }
                else
                    RecycleProjectTexturePreviewBacking(backingIndex, *backing);
            }
            else
                static_cast<void>(RefreshProjectTexturePreviewBackingDemand(backingIndex));
            handle = {};
            return true;
        }
        editor::ProjectResidentRetirementPumpResult PumpResidentRetirements() noexcept override
        {
            using PumpResult = editor::ProjectResidentRetirementPumpResult;
            using RetirementResult = vanguard::asset_streaming::ProjectAssetResidentRetirementResult;
            if (!m_assetPipeline.IsInitialized() || m_renderingService == nullptr)
                return PumpResult::InvalidState;

            auto& manager = m_renderingService->GetTextureResidency().GetResidencyManager();
            vanguard::rendering::TextureGenerationRetirement retirements[ResidentRetirementBatchSize]{};
            vanguard::rendering::TextureResidencyFailure renderingFailure;

            // CURRENT editor rendering uses TextureResidencyConfig's bounded pending-retirement
            // capacity. Consume at most that full bound plus one empty confirmation read, so a full
            // local batch never masquerades as ProjectAssetPipeline::Busy during lifecycle drain.
            for (vanguard::u32 batch = 0; batch <= MaximumResidentRetirementBatches; ++batch)
            {
                const vanguard::u32 count = manager.PeekCompletedGenerationRetirements(
                    {retirements, ResidentRetirementBatchSize}, &renderingFailure);
                if (renderingFailure.code != vanguard::rendering::TextureResidencyFailureCode::None)
                    return PumpResult::RenderingFailure;
                if (count == 0u)
                    return PumpResult::Success;
                if (batch == MaximumResidentRetirementBatches)
                    return PumpResult::RenderingFailure;

                for (vanguard::u32 index = 0; index < count; ++index)
                {
                    const vanguard::rendering::TextureGenerationRetirement& retirement = retirements[index];
                    const vanguard::asset_streaming::ProjectAssetResidentRetirementEvidence evidence{
                        retirement.resourcePath, retirement.resourceGeneration};
                    const RetirementResult reported = m_assetPipeline.ReportResidentRetirement(evidence);
                    if (reported == RetirementResult::Busy)
                        return PumpResult::Busy;
                    if (reported != RetirementResult::Accepted && reported != RetirementResult::NotTracked)
                        return PumpResult::ProjectFailure;
                    if (!manager.AcknowledgeGenerationRetirement(retirement.retirementId, &renderingFailure))
                        return PumpResult::RenderingFailure;
                }
                if (count < ResidentRetirementBatchSize)
                    return PumpResult::Success;
            }
            return PumpResult::RenderingFailure;
        }
        vanguard::assets::SourceDatabaseResult RescanSources(vanguard::filesystem::ScanResult* const failure = nullptr) noexcept override
        {
            const vanguard::assets::SourceRoot root{m_workspace.assets, false};
            return m_sources.Rescan(vanguard::filesystem::GetManager(), {&root, 1}, failure);
        }
        bool ClassifySources(vanguard::containers::ArraySpan<const vanguard::assets::SourceImporterDescriptor> importers) noexcept override
        {
            return m_sources.ClassifySources(importers);
        }
        vanguard::assets::SourceDatabaseResult ScanSourceReferences(
            const vanguard::containers::ArraySpan<const vanguard::assets::SourceImporterDescriptor> importers,
            const vanguard::assets::TargetPlatform target) noexcept override
        {
            vanguard::assets::ProjectSourceDataProvider provider;
            if (!provider.Initialize(vanguard::filesystem::GetManager(), target))
                return vanguard::assets::SourceDatabaseResult::InvalidReferences;
            return m_sources.ScanReferences(importers, &vanguard::assets::ProjectSourceDataProvider::LoadReferenceScanData, &provider);
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            if (!m_projectFile.IsFilePath() || !vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Project Workspace received an invalid project file or unavailable filesystem");

            vanguard::projects::Diagnostic diagnostic;
            if (vanguard::projects::ResolveWorkspace(m_projectFile, m_project, m_workspace, &diagnostic) != vanguard::projects::Result::Success)
                return app::LifecycleStatus::Failure(diagnostic.message != nullptr ? diagnostic.message : "Project Workspace resolution failed");

            const vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
            if (files.GetGameRoot() != m_workspace.root || files.GetCacheDirectory() != m_workspace.derivedData)
                return app::LifecycleStatus::Failure("Project Workspace roots disagree with the managed filesystem configuration");
            if (RescanSources() != vanguard::assets::SourceDatabaseResult::Success)
                return app::LifecycleStatus::Failure("Project Workspace source inventory failed");

            m_framePipeline = vanguard::engine::FindFramePipelineService(context);
            vanguard::engine::ResourceStreamingService* const streaming = vanguard::engine::FindResourceStreamingService(context);
            m_renderingService = vanguard::engine::FindRenderingService(context);
            if (m_framePipeline == nullptr || streaming == nullptr || m_renderingService == nullptr)
                return app::LifecycleStatus::Failure("Project Workspace asset pipeline dependencies are unavailable");
            m_streamingService = streaming;
            if (!vanguard::texture_tools::Initialize() || !m_textureCompiler.Initialize())
                return app::LifecycleStatus::Failure("Project Workspace texture compiler initialization failed");

            vanguard::asset_streaming::ProjectAssetPipelineConfig pipelineConfig;
            if (!vanguard::asset_streaming::MakeProjectAssetPipelineConfig(
                    m_workspace.derivedData, m_workspace.intermediate, "windows-x64", pipelineConfig))
                return app::LifecycleStatus::Failure("Project Workspace asset pipeline identity config is invalid");
            if (!pipelineConfig.build.IsValid())
                return app::LifecycleStatus::Failure("Project Workspace asset build config is invalid");
            if (!pipelineConfig.publication.IsValid())
                return app::LifecycleStatus::Failure("Project Workspace loose publication config is invalid");
            if (pipelineConfig.build.artifactCacheRoot != pipelineConfig.publication.artifactCacheRoot)
                return app::LifecycleStatus::Failure("Project Workspace asset cache roots disagree");
            if (pipelineConfig.build.target != pipelineConfig.publication.target)
                return app::LifecycleStatus::Failure("Project Workspace asset targets disagree");
            if (!(pipelineConfig.build.buildSystem.persistentCacheCoordination == pipelineConfig.publication.cacheCoordination))
                return app::LifecycleStatus::Failure("Project Workspace asset cache coordination limits disagree");
            if (!pipelineConfig.IsValid())
                return app::LifecycleStatus::Failure("Project Workspace asset pipeline bounds are invalid");
            pipelineConfig.build.resolveGeneratedFallback = vanguard::model_tools::ResolveGeneratedFallback;
            pipelineConfig.build.resolveGeneratedFallbackUserData = &m_generated;
            if (!InitializeModelBuildTools(pipelineConfig.build.target))
            {
                static_cast<void>(ShutdownModelBuildTools());
                static_cast<void>(m_textureCompiler.Shutdown());
                return app::LifecycleStatus::Failure("Project Workspace model/material compiler initialization failed");
            }
            const vanguard::filesystem::AbsolutePath derivedOwner = m_workspace.derivedData.AddDirPath("ProjectAssets");
            const vanguard::filesystem::AbsolutePath intermediateOwner = m_workspace.intermediate.AddDirPath("ProjectAssets");
            vanguard::filesystem::Manager& mutableFiles = vanguard::filesystem::GetManager();
            if (!mutableFiles.CreatePath(derivedOwner) || !mutableFiles.CreatePath(intermediateOwner))
            {
                static_cast<void>(ShutdownModelBuildTools());
                static_cast<void>(m_textureCompiler.Shutdown());
                m_renderingService = nullptr;
                return app::LifecycleStatus::Failure("Project Workspace asset pipeline directories could not be created");
            }

            const vanguard::assets::CompilerDescriptor compilers[]{
                m_textureCompiler.GetDescriptor(),
                m_modelCompiler.GetMeshDescriptor(),
                vanguard::model_tools::PrefabCompilerDescriptor(),
                m_materialCompilers.ProgramDescriptor(),
                m_materialCompilers.PipelineDescriptor(),
                m_materialCompilers.MaterialDescriptor()};
            const vanguard::assets::SourceImporterDescriptor importers[]{
                m_textureCompiler.GetImporterDescriptor(), m_modelCompiler.GetImporterDescriptor()};
            vanguard::asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
            const vanguard::asset_streaming::ProjectAssetPipelineResult initialized = m_assetPipeline.Initialize(
                mutableFiles, m_sources, streaming->GetStreamer(),
                {compilers, static_cast<vanguard::u32>(sizeof(compilers) / sizeof(compilers[0]))},
                {importers, static_cast<vanguard::u32>(sizeof(importers) / sizeof(importers[0]))},
                pipelineConfig, nullptr, &pipelineFailure);
            // P5 may complete its durable recovery boundary while a bounded
            // cleanup/reconciliation step remains outstanding. The pipeline is
            // live in both committed-pending cases, and normal Editor frames
            // must be allowed to advance that recovery (as Nanovanguard does).
            const bool committedStartupPending =
                initialized == vanguard::asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                initialized == vanguard::asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            if (initialized != vanguard::asset_streaming::ProjectAssetPipelineResult::Success && !committedStartupPending)
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Resources,
                             "Project Workspace asset pipeline initialization failed: result=%u build=%u sources=%u operation=%u transaction=%u",
                             static_cast<vanguard::u32>(pipelineFailure.result), static_cast<vanguard::u32>(pipelineFailure.build),
                             static_cast<vanguard::u32>(pipelineFailure.sources), static_cast<vanguard::u32>(pipelineFailure.operation.result),
                             static_cast<vanguard::u32>(pipelineFailure.operation.transaction));
                // Initialize can cross the accepted-state commit boundary and
                // still fail with a live pipeline (for example committed
                // reconciliation pending). In that case EngineHost rollback
                // will call OnShutdown; preserve every callback/lower owner so
                // that existing pipeline-first shutdown ordering stays valid.
                if (editor::ProjectAssetInitializeFailureOwnerPolicy(m_assetPipeline.IsInitialized()) ==
                    editor::ProjectAssetInitializeFailureOwnerAction::ReleaseCompilerImmediately)
                {
                    static_cast<void>(ShutdownModelBuildTools());
                    static_cast<void>(m_textureCompiler.Shutdown());
                    m_renderingService = nullptr;
                }
                return app::LifecycleStatus::Failure(vanguard::asset_streaming::ToString(initialized));
            }

            // Catalog capture/build is deliberately incremental. Startup does not
            // synchronously sort the complete source inventory; the normal
            // owner-thread frame participant advances bounded source capture and
            // worker-only construction while the last complete snapshot remains
            // published.

            engine::FrameParticipantDescriptor participant;
            participant.id = editor::ProjectAssetFrameParticipantId;
            participant.name = "projectAssets";
            participant.phase = engine::FramePhase::WorldStreaming;
            participant.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            participant.affinity = app::ThreadAffinity::MainThread;
            participant.execute = &ExecuteAssetFrame;
            participant.userData = this;
            engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(participant, &frameFailure))
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message
                                                                                    : "Project Workspace asset frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            ResetProjectTexturePreviewState();
            m_initialObservationCatchUpComplete = false;
            ResetResidentDeliveryDrive();
            vanguard::asset_streaming::ProjectAssetPipelineFailure failure;
            const vanguard::asset_streaming::ProjectAssetPipelineResult enabled = m_assetPipeline.EnableObservation({}, &failure);
            if (enabled != vanguard::asset_streaming::ProjectAssetPipelineResult::Success &&
                enabled != vanguard::asset_streaming::ProjectAssetPipelineResult::Busy)
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Resources,
                             "Project Workspace source observation start failed: result=%u build=%u sources=%u operation=%u transaction=%u",
                             static_cast<vanguard::u32>(failure.result), static_cast<vanguard::u32>(failure.build),
                             static_cast<vanguard::u32>(failure.sources), static_cast<vanguard::u32>(failure.operation.result),
                             static_cast<vanguard::u32>(failure.operation.transaction));
                return app::LifecycleStatus::Failure("Project Workspace source observation could not start");
            }
            if (enabled == vanguard::asset_streaming::ProjectAssetPipelineResult::Busy)
                VG_LOG_INFO(vanguard::diagnostics::Category::Resources,
                            "Project Workspace source observation deferred while asset coordination owns admission");
            m_previewAccepting = true;
            m_driveFrames = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_previewAccepting = false;
            // The asset-pipeline Prepare job must cancel/join before Jobs
            // service OnDrain checks for outstanding dependent work. OnShutdown
            // is too late for that lifecycle ordering. Publication remains
            // owner-only and uncommitted staging is rolled back here.
            vanguard::asset_streaming::ProjectAssetPipelineFailure prepareFailure;
            if (m_assetPipeline.QuiesceDeferredPlannedImport(&prepareFailure) !=
                vanguard::asset_streaming::ProjectAssetPipelineResult::Success)
                return app::LifecycleStatus::Failure("Project Workspace could not quiesce reviewed import staging");
            DrainProjectTexturePreviewRequests();
            ResetProjectTexturePreviewState();
            m_driveFrames = false;
            DrainResidentDeliveryRequest();
            ResetResidentDeliveryDrive();
            const editor::ProjectResidentRetirementPumpResult result = PumpResidentRetirements();
            if (result == editor::ProjectResidentRetirementPumpResult::Success || result == editor::ProjectResidentRetirementPumpResult::Busy)
                return app::LifecycleStatus::Success();
            return app::LifecycleStatus::Failure("Project Workspace resident-retirement quiesce pump failed");
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            const editor::ProjectResidentRetirementPumpResult result = PumpResidentRetirements();
            if (result == editor::ProjectResidentRetirementPumpResult::Success)
                return app::LifecycleStatus::Success();
            if (result == editor::ProjectResidentRetirementPumpResult::Busy)
                return app::LifecycleStatus::Failure("Project Workspace resident-retirement handoff remains busy during drain");
            return app::LifecycleStatus::Failure("Project Workspace resident-retirement drain pump failed");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_previewAccepting = false;
            // Quiesce normally drains these first. Keep shutdown independently
            // safe for startup rollback / failed-quiesce cleanup: ResourceStreaming
            // is still a live dependency at this point, so every preview-owned
            // root request can reach its real terminal preparation state before
            // its handle is reset.
            DrainProjectTexturePreviewRequests();
            ResetProjectTexturePreviewState();
            DrainResidentDeliveryRequest();
            ResetResidentDeliveryDrive();
            if (m_assetPipeline.Shutdown() != vanguard::asset_streaming::ProjectAssetPipelineResult::Success)
                return app::LifecycleStatus::Failure("Project Workspace asset pipeline shutdown failed");
            m_assetCatalog.Reset();
            if (!ShutdownModelBuildTools())
                return app::LifecycleStatus::Failure("Project Workspace model/material compiler shutdown failed");
            if (!m_textureCompiler.Shutdown())
                return app::LifecycleStatus::Failure("Project Workspace texture compiler shutdown failed");
            m_framePipeline = nullptr;
            m_streamingService = nullptr;
            m_renderingService = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] bool InitializeModelBuildTools(const vanguard::assets::TargetPlatform target) noexcept
        {
            if (!vanguard::mesh_tools::Acquire())
                return false;
            m_modelToolsAcquired = true;

            vanguard::containers::DynamicArray<vanguard::u8> surfacePrefix(vanguard::memory::pools::Tools::GetInstance());
            vanguard::containers::DynamicArray<vanguard::u8> surfaceSuffix(vanguard::memory::pools::Tools::GetInstance());
            vanguard::containers::DynamicArray<vanguard::u8> gpuSceneTypes(vanguard::memory::pools::Tools::GetInstance());
            const vanguard::filesystem::AbsolutePath shaders =
                vanguard::filesystem::paths::GetExecutableDirectory().AddDirPath("shaders");
            if (!vanguard::editor::ReadModelToolSource(shaders.AddFilePath("static_surface_prefix.vsl"), surfacePrefix) ||
                !vanguard::editor::ReadModelToolSource(shaders.AddFilePath("static_surface_suffix.vsl"), surfaceSuffix) ||
                !vanguard::editor::ReadModelToolSource(shaders.AddFilePath("gpu_scene_types.hlsli"), gpuSceneTypes))
                return false;

            vanguard::material_tools::ImportedSurfaceMaterialContextDescription materialDescription;
            materialDescription.prefix = {surfacePrefix.TypedData(), surfacePrefix.Size()};
            materialDescription.suffix = {surfaceSuffix.TypedData(), surfaceSuffix.Size()};
            materialDescription.gpuSceneTypes = {gpuSceneTypes.TypedData(), gpuSceneTypes.Size()};
            materialDescription.target = target;
            materialDescription.compileSettings.target =
                target == vanguard::assets::TargetPlatform::WindowsD3D12
                    ? vanguard::shader_tools::Target::D3D12Dxil
                    : vanguard::shader_tools::Target::VulkanSpirV;
            if (!m_materialContext.Initialize(materialDescription))
                return false;
            vanguard::material_tools::MaterialArtifactCompilerConfig materialCompilerConfig;
            if (!m_materialContext.ConfigureArtifactCompiler(materialCompilerConfig) ||
                !m_materialCompilers.Initialize(materialCompilerConfig))
                return false;

            m_modelAdapter.sources = &m_sources;
            m_modelAdapter.materialContext = &m_materialContext;
            m_modelAdapter.generated = &m_generated;
            vanguard::mesh_tools::ModelAssetCompilerConfig modelConfig;
            modelConfig.outputTypes.mesh = vanguard::meshes::MeshResourceType;
            modelConfig.outputTypes.material = vanguard::materials::MaterialResourceType;
            modelConfig.outputTypes.texture = vanguard::textures::TextureResourceType;
            modelConfig.outputTypes.composition = vanguard::prefabs::PrefabResourceType;
            modelConfig.composeMaterial = vanguard::model_tools::ComposeMaterial;
            modelConfig.composeMaterialUserData = &m_modelAdapter;
            modelConfig.composeEmbeddedTexture = vanguard::model_tools::ComposeEmbeddedTexture;
            modelConfig.composeEmbeddedTextureUserData = &m_modelAdapter;
            modelConfig.composeComposition = vanguard::model_tools::ComposeComposition;
            return m_modelCompiler.Initialize(modelConfig);
        }

        [[nodiscard]] bool ShutdownModelBuildTools() noexcept
        {
            if (m_modelCompiler.IsInitialized() && !m_modelCompiler.Shutdown())
                return false;
            if (m_materialCompilers.IsInitialized() && !m_materialCompilers.Shutdown())
                return false;
            m_materialContext.Reset();
            m_generated.Clear();
            m_modelAdapter = {};
            if (m_modelToolsAcquired)
            {
                if (!vanguard::mesh_tools::Release())
                    return false;
                m_modelToolsAcquired = false;
            }
            return true;
        }

        [[nodiscard]] static bool IsProjectTexturePreviewPriority(const editor::ProjectTexturePreviewPriority priority) noexcept
        {
            return priority == editor::ProjectTexturePreviewPriority::Background ||
                   priority == editor::ProjectTexturePreviewPriority::Interactive ||
                   priority == editor::ProjectTexturePreviewPriority::Focused;
        }

        [[nodiscard]] static bool IsProjectTexturePreviewRequestValid(
            const editor::ProjectTexturePreviewRequest& request) noexcept
        {
            return IsProjectTexturePreviewPriority(request.priority) &&
                   request.quality.displayWidthPixels != 0u &&
                   request.quality.displayHeightPixels != 0u;
        }

        [[nodiscard]] static bool IsHigherProjectTexturePreviewPriority(const editor::ProjectTexturePreviewPriority candidate,
                                                                       const editor::ProjectTexturePreviewPriority current) noexcept
        {
            return static_cast<vanguard::u8>(candidate) > static_cast<vanguard::u8>(current);
        }

        [[nodiscard]] static vanguard::u64 CeilDivideProjectTexturePreviewExtent(
            const vanguard::u64 numerator, const vanguard::u32 denominator) noexcept
        {
            VG_ASSERT_MSG(denominator != 0u, "project texture preview aspect-fit denominator must be nonzero");
            return numerator / denominator + (numerator % denominator != 0u ? 1u : 0u);
        }

        [[nodiscard]] static vanguard::u32 ResolveProjectTexturePreviewFirstResidentMip(
            const vanguard::textures::TextureFile& texture,
            const editor::ProjectTexturePreviewQuality quality) noexcept
        {
            const vanguard::u32 sourceWidth = texture.GetWidth();
            const vanguard::u32 sourceHeight = texture.GetHeight();
            const vanguard::u32 mipCount = texture.GetMipCount();
            if (sourceWidth == 0u || sourceHeight == 0u || mipCount == 0u)
                return InvalidProjectTexturePreviewMip;

            vanguard::u64 fittedWidth = quality.displayWidthPixels;
            vanguard::u64 fittedHeight = quality.displayHeightPixels;
            const vanguard::u64 widthLimited =
                static_cast<vanguard::u64>(quality.displayWidthPixels) * sourceHeight;
            const vanguard::u64 heightLimited =
                static_cast<vanguard::u64>(quality.displayHeightPixels) * sourceWidth;
            if (widthLimited <= heightLimited)
            {
                fittedHeight = CeilDivideProjectTexturePreviewExtent(
                    static_cast<vanguard::u64>(sourceHeight) * quality.displayWidthPixels, sourceWidth);
            }
            else
            {
                fittedWidth = CeilDivideProjectTexturePreviewExtent(
                    static_cast<vanguard::u64>(sourceWidth) * quality.displayHeightPixels, sourceHeight);
            }
            fittedWidth = std::max<vanguard::u64>(1u, fittedWidth);
            fittedHeight = std::max<vanguard::u64>(1u, fittedHeight);

            const vanguard::u32 maximumMip = std::min<vanguard::u32>(
                texture.GetMipTailFirstLevel(), mipCount - 1u);
            for (vanguard::u32 mip = maximumMip + 1u; mip != 0u; --mip)
            {
                const vanguard::u32 candidate = mip - 1u;
                if (vanguard::textures::CalculateMipExtent(sourceWidth, static_cast<vanguard::u8>(candidate)) >= fittedWidth &&
                    vanguard::textures::CalculateMipExtent(sourceHeight, static_cast<vanguard::u8>(candidate)) >= fittedHeight)
                    return candidate;
            }
            return 0u;
        }

        [[nodiscard]] static vanguard::resources::LoadPriority ProjectTexturePreviewLoadPriority(
            const editor::ProjectTexturePreviewPriority priority) noexcept
        {
            switch (priority)
            {
            case editor::ProjectTexturePreviewPriority::Background: return vanguard::resources::LoadPriority::Background;
            case editor::ProjectTexturePreviewPriority::Interactive: return vanguard::resources::LoadPriority::Normal;
            case editor::ProjectTexturePreviewPriority::Focused: return vanguard::resources::LoadPriority::High;
            }
            return vanguard::resources::LoadPriority::Normal;
        }

        [[nodiscard]] static vanguard::rendering::TextureDemandPriority ProjectTexturePreviewDemandPriority(
            const editor::ProjectTexturePreviewPriority priority) noexcept
        {
            switch (priority)
            {
            case editor::ProjectTexturePreviewPriority::Background:
                return vanguard::rendering::TextureDemandPriority::Background;
            case editor::ProjectTexturePreviewPriority::Interactive:
                return vanguard::rendering::TextureDemandPriority::Interactive;
            case editor::ProjectTexturePreviewPriority::Focused:
                return vanguard::rendering::TextureDemandPriority::Focused;
            }
            return vanguard::rendering::TextureDemandPriority::Streaming;
        }

        [[nodiscard]] bool QueryProjectTexturePreviewPublication(
            const vanguard::resources::ResourceReference output,
            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus& publication) const noexcept
        {
            publication = {};
            if (!m_assetPipeline.GetOutputDeliveryStatus(output, publication) || publication.resource != output ||
                publication.publishedBuildFingerprint.IsEmpty() || publication.publishedContentFingerprint.IsEmpty() ||
                !publication.publishedPhysicalPath.IsFilePath())
                return false;
            using DeliveryState = vanguard::asset_streaming::ProjectAssetDeliveryStageState;
            return publication.buildState == DeliveryState::Committed &&
                   (publication.publicationState == DeliveryState::Installed || publication.publicationState == DeliveryState::Stale);
        }

        [[nodiscard]] static bool SameProjectTexturePreviewPublication(
            const ProjectTexturePreviewBacking& backing,
            const vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus& publication) noexcept
        {
            return publication.resource == backing.resource &&
                   publication.publishedBuildFingerprint == backing.buildFingerprint &&
                   publication.publishedContentFingerprint == backing.contentFingerprint;
        }

        void RetargetProjectTexturePreviewBacking(ProjectTexturePreviewBacking& backing,
                                                  const vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus& publication,
                                                  const editor::ProjectTexturePreviewState state) noexcept
        {
            backing.request.Reset();
            backing.loadedResource.Reset();
            backing.textureDemand.Reset();
            backing.readyTexture.Reset();
            backing.resource = publication.resource;
            backing.buildFingerprint = publication.publishedBuildFingerprint;
            backing.contentFingerprint = publication.publishedContentFingerprint;
            backing.state = state;
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.supersessionPredecessorIdentity = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
        }

        [[nodiscard]] vanguard::u32 AcquireProjectTexturePreviewInterestIndex() noexcept
        {
            while (m_previewInterestFreeHead != editor::InvalidProjectTexturePreviewIndex)
            {
                const vanguard::u32 index = m_previewInterestFreeHead;
                ProjectTexturePreviewInterest& interest = m_previewInterests[index];
                m_previewInterestFreeHead = interest.nextFree;
                interest.nextFree = editor::InvalidProjectTexturePreviewIndex;
                if (interest.generation != ~vanguard::u32{0})
                    return index;
            }
            return editor::InvalidProjectTexturePreviewIndex;
        }

        void ReturnProjectTexturePreviewInterestIndex(const vanguard::u32 index) noexcept
        {
            VG_ASSERT_MSG(index < editor::MaximumProjectTexturePreviewInterests && !m_previewInterests[index].occupied,
                          "only an unoccupied project texture preview interest may return to the free list");
            ProjectTexturePreviewInterest& interest = m_previewInterests[index];
            interest.nextFree = m_previewInterestFreeHead;
            m_previewInterestFreeHead = index;
        }

        [[nodiscard]] vanguard::u32 FindOrCreateProjectTexturePreviewBacking(
            const vanguard::resources::ResourceReference output,
            const vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus& publication) noexcept
        {
            vanguard::u32 retargetable = editor::InvalidProjectTexturePreviewIndex;
            for (vanguard::u32 index = 0u; index < editor::MaximumProjectTexturePreviewBackings; ++index)
            {
                ProjectTexturePreviewBacking& backing = m_previewBackings[index];
                if (!backing.occupied || backing.resource != output)
                    continue;
                if (SameProjectTexturePreviewPublication(backing, publication) &&
                    backing.state != editor::ProjectTexturePreviewState::Retiring &&
                    backing.state != editor::ProjectTexturePreviewState::Failed)
                    return index;
                if (retargetable == editor::InvalidProjectTexturePreviewIndex &&
                    backing.state != editor::ProjectTexturePreviewState::Ready &&
                    backing.state != editor::ProjectTexturePreviewState::Retiring &&
                    (!backing.request || backing.request.HasFinished()))
                    retargetable = index;
            }

            if (retargetable != editor::InvalidProjectTexturePreviewIndex)
            {
                RetargetProjectTexturePreviewBacking(m_previewBackings[retargetable], publication,
                                                     editor::ProjectTexturePreviewState::Queued);
                return retargetable;
            }
            if (m_previewBackingFreeHead == editor::InvalidProjectTexturePreviewIndex)
                return editor::InvalidProjectTexturePreviewIndex;
            if (m_previewNextBackingIdentity == 0u)
                return editor::InvalidProjectTexturePreviewIndex;

            const vanguard::u32 index = m_previewBackingFreeHead;
            ProjectTexturePreviewBacking& backing = m_previewBackings[index];
            m_previewBackingFreeHead = backing.nextFree;
            backing.nextFree = editor::InvalidProjectTexturePreviewIndex;
            backing.resource = output;
            backing.buildFingerprint = publication.publishedBuildFingerprint;
            backing.contentFingerprint = publication.publishedContentFingerprint;
            backing.state = editor::ProjectTexturePreviewState::Queued;
            backing.priority = editor::ProjectTexturePreviewPriority::Background;
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.presentationRevision = 0u;
            backing.identity = m_previewNextBackingIdentity++;
            backing.supersessionPredecessorIdentity = 0u;
            backing.interestCount = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            backing.occupied = true;
            ++m_previewLiveBackings;
            return index;
        }

        void PromoteProjectTexturePreviewRequest(ProjectTexturePreviewBacking& backing) noexcept
        {
            if (backing.request)
                static_cast<void>(backing.request.Promote(ProjectTexturePreviewLoadPriority(backing.priority)));
        }

        [[nodiscard]] bool RefreshProjectTexturePreviewBackingDemand(const vanguard::u32 backingIndex) noexcept
        {
            ProjectTexturePreviewBacking* const backing = ResolveProjectTexturePreviewBacking(backingIndex);
            if (backing == nullptr || backing->interestCount == 0u)
                return false;
            const editor::ProjectTexturePreviewPriority previous = backing->priority;
            editor::ProjectTexturePreviewPriority effective = editor::ProjectTexturePreviewPriority::Background;
            vanguard::u32 desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            const auto* const loadedTexture =
                backing->loadedResource.IsValid() && backing->loadedResource.GetType() == vanguard::textures::TextureResourceType
                    ? static_cast<const vanguard::textures::TextureResourceObject*>(backing->loadedResource.Get())
                    : nullptr;
            const bool hasMetadata = loadedTexture != nullptr && loadedTexture->IsOpen();
            vanguard::u32 interests = 0u;
            for (ProjectTexturePreviewInterest& interest : m_previewInterests)
            {
                if (!interest.occupied || interest.backingIndex != backingIndex)
                    continue;
                ++interests;
                if (IsHigherProjectTexturePreviewPriority(interest.request.priority, effective))
                    effective = interest.request.priority;
                interest.resolvedFirstResidentMip =
                    hasMetadata ? ResolveProjectTexturePreviewFirstResidentMip(
                                      loadedTexture->GetMetadata(), interest.request.quality)
                                : InvalidProjectTexturePreviewMip;
                if (interest.resolvedFirstResidentMip != InvalidProjectTexturePreviewMip &&
                    (desiredFirstResidentMip == InvalidProjectTexturePreviewMip ||
                     interest.resolvedFirstResidentMip < desiredFirstResidentMip))
                    desiredFirstResidentMip = interest.resolvedFirstResidentMip;
            }
            VG_ASSERT_MSG(interests == backing->interestCount, "project texture preview backing interest count must match live interests");
            const bool qualityTargetChanged = desiredFirstResidentMip != backing->desiredFirstResidentMip;
            if (backing->textureDemand.IsValid() && desiredFirstResidentMip != InvalidProjectTexturePreviewMip &&
                (qualityTargetChanged || effective != previous))
            {
                if (m_renderingService == nullptr)
                    return false;
                vanguard::rendering::TextureResidencyRuntimeFailure failure;
                vanguard::rendering::TextureDemandDesc demand;
                demand.firstResidentMip = desiredFirstResidentMip;
                demand.priority = ProjectTexturePreviewDemandPriority(effective);
                if (!m_renderingService->GetTextureResidency().UpdateDemand(
                        backing->textureDemand, demand, &failure))
                    return false;
                if (qualityTargetChanged)
                    ResetProjectTexturePreviewQualityRetry(*backing);
            }
            backing->priority = effective;
            backing->desiredFirstResidentMip = desiredFirstResidentMip;
            if (IsHigherProjectTexturePreviewPriority(effective, previous))
                PromoteProjectTexturePreviewRequest(*backing);
            return true;
        }

        [[nodiscard]] bool StartProjectTexturePreviewRequest(ProjectTexturePreviewBacking& backing) noexcept
        {
            if (m_streamingService == nullptr || !backing.occupied || !backing.resource.IsValid() ||
                backing.state != editor::ProjectTexturePreviewState::Queued || backing.interestCount == 0u)
                return false;
            backing.request.Reset();
            backing.loadedResource.Reset();
            backing.textureDemand.Reset();
            backing.readyTexture.Reset();
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.supersessionPredecessorIdentity = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            backing.request = m_streamingService->GetStreamer().Request(backing.resource, ProjectTexturePreviewLoadPriority(backing.priority));
            if (!backing.request)
            {
                backing.state = editor::ProjectTexturePreviewState::Failed;
                return false;
            }
            backing.state = editor::ProjectTexturePreviewState::LoadingResource;
            return true;
        }

        [[nodiscard]] vanguard::u32 CountProjectTexturePreviewResidencyBackings() const noexcept
        {
            vanguard::u32 count = 0u;
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (backing.occupied && backing.textureDemand.IsValid())
                    ++count;
            return count;
        }

        [[nodiscard]] static bool IsProjectTexturePreviewResidencyHolding(
            const ProjectTexturePreviewBacking& backing) noexcept
        {
            return backing.occupied && backing.textureDemand.IsValid();
        }

        [[nodiscard]] static bool UsesProjectTexturePreviewSupersessionResidencySlot(
            const ProjectTexturePreviewBacking& backing) noexcept
        {
            return backing.supersessionPredecessorIdentity != 0u;
        }

        [[nodiscard]] vanguard::u32 CountOrdinaryProjectTexturePreviewResidencyBackingsAtOrBelow(
            const editor::ProjectTexturePreviewPriority priority) const noexcept
        {
            vanguard::u32 count = 0u;
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (IsProjectTexturePreviewResidencyHolding(backing) && !UsesProjectTexturePreviewSupersessionResidencySlot(backing) &&
                    !IsHigherProjectTexturePreviewPriority(backing.priority, priority))
                    ++count;
            return count;
        }

        [[nodiscard]] vanguard::u32 CountOrdinaryProjectTexturePreviewResidencyBackings() const noexcept
        {
            vanguard::u32 count = 0u;
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (IsProjectTexturePreviewResidencyHolding(backing) && !UsesProjectTexturePreviewSupersessionResidencySlot(backing))
                    ++count;
            return count;
        }

        [[nodiscard]] bool HasProjectTexturePreviewSupersessionResidencySlot() const noexcept
        {
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (IsProjectTexturePreviewResidencyHolding(backing) && UsesProjectTexturePreviewSupersessionResidencySlot(backing))
                    return true;
            return false;
        }

        [[nodiscard]] vanguard::u32 FindProjectTexturePreviewSupersessionSource(
            const ProjectTexturePreviewBacking& candidate) const noexcept
        {
            for (vanguard::u32 index = 0u; index < editor::MaximumProjectTexturePreviewBackings; ++index)
            {
                const ProjectTexturePreviewBacking& backing = m_previewBackings[index];
                if (!IsProjectTexturePreviewResidencyHolding(backing) || &backing == &candidate || backing.resource != candidate.resource ||
                    (backing.state != editor::ProjectTexturePreviewState::Ready &&
                     backing.state != editor::ProjectTexturePreviewState::Retiring))
                    continue;
                if (backing.buildFingerprint != candidate.buildFingerprint ||
                    backing.contentFingerprint != candidate.contentFingerprint)
                    return index;
            }
            return editor::InvalidProjectTexturePreviewIndex;
        }

        [[nodiscard]] bool IsProjectTexturePreviewSupersessionPredecessorAlive(
            const ProjectTexturePreviewBacking& backing) const noexcept
        {
            if (!UsesProjectTexturePreviewSupersessionResidencySlot(backing) ||
                backing.supersessionPredecessorIndex >= editor::MaximumProjectTexturePreviewBackings)
                return false;
            const ProjectTexturePreviewBacking& predecessor = m_previewBackings[backing.supersessionPredecessorIndex];
            return IsProjectTexturePreviewResidencyHolding(predecessor) &&
                   predecessor.identity == backing.supersessionPredecessorIdentity &&
                   (predecessor.state == editor::ProjectTexturePreviewState::Ready ||
                    predecessor.state == editor::ProjectTexturePreviewState::Retiring);
        }

        [[nodiscard]] ProjectTexturePreviewResidencyAdmission ClassifyProjectTexturePreviewResidencyAdmission(
            const ProjectTexturePreviewBacking& backing,
            vanguard::u32& supersessionPredecessorIndex,
            vanguard::u64& supersessionPredecessorIdentity) const noexcept
        {
            supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            supersessionPredecessorIdentity = 0u;
            const vanguard::u32 held = CountProjectTexturePreviewResidencyBackings();
            if (!editor::detail::IsProjectTexturePreviewResidencyBelowHardLimit(held))
                return ProjectTexturePreviewResidencyAdmission::None;

            static_assert(editor::MaximumProjectTexturePreviewReadyBackings >= 4u);
            // Partition the hard residency bound without borrowing: ordinary
            // Background work leaves room for Interactive + Focused, all ordinary
            // work leaves one additional slot for an immutable N -> N+1 overlap.
            constexpr vanguard::u32 maximumBackgroundOrdinary = editor::MaximumProjectTexturePreviewReadyBackings - 3u;
            constexpr vanguard::u32 maximumInteractiveOrdinary = editor::MaximumProjectTexturePreviewReadyBackings - 2u;
            constexpr vanguard::u32 maximumOrdinary = editor::MaximumProjectTexturePreviewReadyBackings - 1u;

            const vanguard::u32 ordinary = CountOrdinaryProjectTexturePreviewResidencyBackings();
            const vanguard::u32 background = CountOrdinaryProjectTexturePreviewResidencyBackingsAtOrBelow(
                editor::ProjectTexturePreviewPriority::Background);
            const vanguard::u32 backgroundAndInteractive = CountOrdinaryProjectTexturePreviewResidencyBackingsAtOrBelow(
                editor::ProjectTexturePreviewPriority::Interactive);
            if (editor::detail::IsProjectTexturePreviewAdmissionWithinCumulativeReserves(
                    backing.priority, background, backgroundAndInteractive, ordinary,
                    maximumBackgroundOrdinary, maximumInteractiveOrdinary, maximumOrdinary))
                return ProjectTexturePreviewResidencyAdmission::Ordinary;

            // The final physical slot is non-borrowable transition headroom. It
            // may be used by one exact same-output immutable-generation overlap,
            // never by unrelated steady-state demand.
            const vanguard::u32 predecessorIndex = FindProjectTexturePreviewSupersessionSource(backing);
            if (editor::detail::CanUseProjectTexturePreviewSupersessionResidencySlot(
                    held, HasProjectTexturePreviewSupersessionResidencySlot(),
                    predecessorIndex != editor::InvalidProjectTexturePreviewIndex))
            {
                const ProjectTexturePreviewBacking& predecessor = m_previewBackings[predecessorIndex];
                VG_ASSERT_MSG(predecessor.identity != 0u, "supersession predecessor must have a live backing identity");
                supersessionPredecessorIndex = predecessorIndex;
                supersessionPredecessorIdentity = predecessor.identity;
                return ProjectTexturePreviewResidencyAdmission::Supersession;
            }
            return ProjectTexturePreviewResidencyAdmission::None;
        }

        void RebalanceProjectTexturePreviewSupersessionResidencySlot() noexcept
        {
            for (ProjectTexturePreviewBacking& backing : m_previewBackings)
            {
                if (!IsProjectTexturePreviewResidencyHolding(backing) ||
                    !UsesProjectTexturePreviewSupersessionResidencySlot(backing) ||
                    IsProjectTexturePreviewSupersessionPredecessorAlive(backing))
                    continue;
                // The special slot represents an active immutable-generation
                // overlap, not a permanent priority exemption. Once the predecessor
                // is gone, rejoin ordinary accounting unconditionally. A priority
                // demotion can temporarily put an ordinary class above its reserve;
                // admission then stays closed for that class until ownership drains.
                backing.supersessionPredecessorIdentity = 0u;
                backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
                return;
            }
        }

        void FailProjectTexturePreviewBacking(ProjectTexturePreviewBacking& backing) noexcept
        {
            backing.readyTexture.Reset();
            backing.textureDemand.Reset();
            backing.loadedResource.Reset();
            backing.request.Reset();
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.supersessionPredecessorIdentity = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            backing.state = editor::ProjectTexturePreviewState::Failed;
        }

        void ReacquireProjectTexturePreviewGeneration(ProjectTexturePreviewBacking& backing) noexcept
        {
            backing.readyTexture.Reset();
            backing.textureDemand.Reset();
            backing.loadedResource.Reset();
            backing.request.Reset();
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.supersessionPredecessorIdentity = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            backing.state = editor::ProjectTexturePreviewState::WaitingForCurrentGeneration;
        }

        [[nodiscard]] bool ProjectTexturePreviewReplacementTargetsLoadedGeneration(
            const ProjectTexturePreviewBacking& backing,
            const vanguard::rendering::TextureRuntimeInfo& info) const noexcept
        {
            if (!backing.loadedResource.IsValid() || info.state != vanguard::rendering::TextureRuntimeState::BindlessReady ||
                info.replacementResourceGeneration != backing.loadedResource.GetGeneration())
                return false;
            return info.replacementState == vanguard::rendering::TextureReplacementState::Uploading ||
                   info.replacementState == vanguard::rendering::TextureReplacementState::InstallationPending ||
                   (info.replacementState == vanguard::rendering::TextureReplacementState::Failed && info.replacementRetryRequired);
        }

        [[nodiscard]] bool AdoptProjectTexturePreviewPhysicalTexture(
            ProjectTexturePreviewBacking& backing,
            const vanguard::rendering::TextureResidencyInfo& residencyInfo) noexcept
        {
            if (!residencyInfo.texture.IsValid())
                return false;
            const bool changed = !backing.readyTexture.IsValid() ||
                                 backing.readyTexture.GetRef() != residencyInfo.texture;
            if (changed)
            {
                if (backing.presentationRevision == ~vanguard::u64{0})
                    return false;
                vanguard::rhi::Texture replacement(residencyInfo.texture);
                if (!replacement.IsValid())
                    return false;
                backing.readyTexture = static_cast<vanguard::rhi::Texture&&>(replacement);
                ++backing.presentationRevision;
            }
            backing.currentFirstResidentMip = residencyInfo.firstResidentMip;
            return true;
        }

        void RefreshReadyProjectTexturePreviewPresentation(ProjectTexturePreviewBacking& backing,
                                                            const vanguard::u64 currentUiFrame) noexcept
        {
            if (m_renderingService == nullptr || !backing.loadedResource.IsValid() ||
                !backing.textureDemand.IsValid() || !backing.readyTexture.IsValid())
                return;
            vanguard::rendering::TextureResidencyRuntime& runtime = m_renderingService->GetTextureResidency();
            if (!runtime.IsInitialized())
                return;
            const vanguard::rendering::GpuTextureResidencyHandle residency = backing.textureDemand.GetResidency();
            if (!residency.IsValid())
                return;

            vanguard::rendering::TextureRuntimeInfo runtimeInfo;
            vanguard::rendering::TextureResidencyRuntimeFailure runtimeFailure;
            if (!runtime.GetInfo(residency, runtimeInfo, &runtimeFailure) ||
                runtimeInfo.state != vanguard::rendering::TextureRuntimeState::BindlessReady ||
                runtimeInfo.resourcePath != backing.resource.GetPath() ||
                runtimeInfo.resourceGeneration != backing.loadedResource.GetGeneration())
                return;

            if (runtimeInfo.qualityRetryRequired &&
                backing.desiredFirstResidentMip != InvalidProjectTexturePreviewMip &&
                currentUiFrame >= backing.qualityRetryAfterUiFrame)
            {
                // Keep a usable Ready presentation authoritative while retrying a
                // failed refinement at an exponentially bounded rate. An unchanged
                // UpdateDemand is the runtime's explicit quality-retry operation.
                DeferProjectTexturePreviewQualityRetry(backing, currentUiFrame);
                vanguard::rendering::TextureDemandDesc retry;
                retry.firstResidentMip = backing.desiredFirstResidentMip;
                retry.priority = ProjectTexturePreviewDemandPriority(backing.priority);
                vanguard::rendering::TextureResidencyRuntimeFailure retryFailure;
                static_cast<void>(runtime.UpdateDemand(backing.textureDemand, retry, &retryFailure));
            }
            else if (runtimeInfo.qualitySatisfied)
                ResetProjectTexturePreviewQualityRetry(backing);

            vanguard::rendering::TextureResidencyInfo residencyInfo;
            vanguard::rendering::TextureResidencyRuntimeFailure residencyFailure;
            if (!runtime.GetResidencyInfo(residency, residencyInfo, &residencyFailure) ||
                residencyInfo.state != vanguard::rendering::TextureResidencyState::BindlessReady ||
                !residencyInfo.texture.IsValid() || !residencyInfo.source.IsValid() ||
                residencyInfo.source.IsStale() ||
                residencyInfo.source.GetPath() != backing.resource.GetPath() ||
                residencyInfo.source.GetGeneration() != backing.loadedResource.GetGeneration())
                return;

            const auto* const loadedTexture =
                static_cast<const vanguard::textures::TextureResourceObject*>(backing.loadedResource.Get());
            if (loadedTexture == nullptr || !loadedTexture->IsOpen() ||
                residencyInfo.contentFingerprint != loadedTexture->GetMetadata().GetContentFingerprint())
                return;

            // Quality-transition failure leaves the current installation valid.
            // Refresh only when the residency manager exposes a new authoritative
            // physical texture; otherwise keep the already sampleable presentation.
            static_cast<void>(AdoptProjectTexturePreviewPhysicalTexture(backing, residencyInfo));
        }

        void AdvanceProjectTexturePreviewResidency(ProjectTexturePreviewBacking& backing) noexcept
        {
            if (m_renderingService == nullptr || !backing.loadedResource.IsValid())
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }

            vanguard::rendering::TextureResidencyRuntime& runtime = m_renderingService->GetTextureResidency();
            if (!runtime.IsInitialized())
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }

            if (!backing.textureDemand.IsValid())
            {
                const vanguard::u32 backingIndex =
                    static_cast<vanguard::u32>(&backing - m_previewBackings);
                if (!RefreshProjectTexturePreviewBackingDemand(backingIndex) ||
                    backing.desiredFirstResidentMip == InvalidProjectTexturePreviewMip)
                {
                    FailProjectTexturePreviewBacking(backing);
                    return;
                }
                vanguard::u32 supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
                vanguard::u64 supersessionPredecessorIdentity = 0u;
                const ProjectTexturePreviewResidencyAdmission admission =
                    ClassifyProjectTexturePreviewResidencyAdmission(
                        backing, supersessionPredecessorIndex, supersessionPredecessorIdentity);
                if (admission == ProjectTexturePreviewResidencyAdmission::None)
                    return;

                vanguard::rendering::TextureRuntimeInfo existing;
                const vanguard::rendering::TextureResidentLookupResult found =
                    runtime.FindDemandedResource(backing.resource.GetPath(), existing);
                if (found == vanguard::rendering::TextureResidentLookupResult::Ambiguous)
                {
                    FailProjectTexturePreviewBacking(backing);
                    return;
                }
                if (found == vanguard::rendering::TextureResidentLookupResult::Found)
                {
                    const vanguard::u32 loadedGeneration = backing.loadedResource.GetGeneration();
                    if (existing.resourceGeneration != loadedGeneration)
                    {
                        if (ProjectTexturePreviewReplacementTargetsLoadedGeneration(backing, existing))
                            return;
                        ReacquireProjectTexturePreviewGeneration(backing);
                        return;
                    }
                    if (existing.state != vanguard::rendering::TextureRuntimeState::MipTailLoading &&
                        existing.state != vanguard::rendering::TextureRuntimeState::InstallationPending &&
                        existing.state != vanguard::rendering::TextureRuntimeState::BindlessReady)
                    {
                        FailProjectTexturePreviewBacking(backing);
                        return;
                    }
                }

                vanguard::rendering::TextureResidencyRuntimeFailure requestFailure;
                vanguard::rendering::TextureDemandDesc demand;
                demand.firstResidentMip = backing.desiredFirstResidentMip;
                demand.priority = ProjectTexturePreviewDemandPriority(backing.priority);
                if (!runtime.RequestTexture(backing.loadedResource, demand, backing.textureDemand, &requestFailure))
                {
                    FailProjectTexturePreviewBacking(backing);
                    return;
                }
                if (admission == ProjectTexturePreviewResidencyAdmission::Supersession)
                {
                    backing.supersessionPredecessorIndex = supersessionPredecessorIndex;
                    backing.supersessionPredecessorIdentity = supersessionPredecessorIdentity;
                }
            }

            const vanguard::rendering::GpuTextureResidencyHandle residency = backing.textureDemand.GetResidency();
            if (!residency.IsValid())
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }

            vanguard::rendering::TextureRuntimeInfo runtimeInfo;
            vanguard::rendering::TextureResidencyRuntimeFailure runtimeFailure;
            if (!runtime.GetInfo(residency, runtimeInfo, &runtimeFailure))
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }
            if (runtimeInfo.resourcePath != backing.resource.GetPath() ||
                runtimeInfo.resourceGeneration != backing.loadedResource.GetGeneration())
            {
                ReacquireProjectTexturePreviewGeneration(backing);
                return;
            }
            if (runtimeInfo.state == vanguard::rendering::TextureRuntimeState::Failed ||
                runtimeInfo.state == vanguard::rendering::TextureRuntimeState::Cancelling ||
                runtimeInfo.state == vanguard::rendering::TextureRuntimeState::Retiring)
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }
            if (runtimeInfo.state != vanguard::rendering::TextureRuntimeState::BindlessReady)
                return;

            vanguard::rendering::TextureResidencyInfo residencyInfo;
            vanguard::rendering::TextureResidencyRuntimeFailure residencyFailure;
            if (!runtime.GetResidencyInfo(residency, residencyInfo, &residencyFailure) ||
                residencyInfo.state != vanguard::rendering::TextureResidencyState::BindlessReady ||
                !residencyInfo.texture.IsValid() || !residencyInfo.source.IsValid())
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }

            const bool sourceMismatch = residencyInfo.source.IsStale() ||
                                        residencyInfo.source.GetPath() != backing.resource.GetPath() ||
                                        residencyInfo.source.GetGeneration() != backing.loadedResource.GetGeneration();
            const auto* const loadedTexture =
                static_cast<const vanguard::textures::TextureResourceObject*>(backing.loadedResource.Get());
            const bool contentMismatch = loadedTexture == nullptr || !loadedTexture->IsOpen() ||
                                         residencyInfo.contentFingerprint != loadedTexture->GetMetadata().GetContentFingerprint();
            if (sourceMismatch || contentMismatch)
            {
                vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
                if (QueryProjectTexturePreviewPublication(backing.resource, current) &&
                    !SameProjectTexturePreviewPublication(backing, current))
                {
                    RetargetProjectTexturePreviewBacking(backing, current,
                                                        editor::ProjectTexturePreviewState::WaitingForCurrentGeneration);
                    return;
                }
                if (sourceMismatch && !contentMismatch)
                {
                    ReacquireProjectTexturePreviewGeneration(backing);
                    return;
                }
                FailProjectTexturePreviewBacking(backing);
                return;
            }

            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
            if (!QueryProjectTexturePreviewPublication(backing.resource, current))
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }
            if (!SameProjectTexturePreviewPublication(backing, current) ||
                !IsPublishedTextureGeneration(backing.loadedResource, current.publishedPhysicalPath))
            {
                RetargetProjectTexturePreviewBacking(backing, current,
                                                    editor::ProjectTexturePreviewState::WaitingForCurrentGeneration);
                return;
            }

            if (!AdoptProjectTexturePreviewPhysicalTexture(backing, residencyInfo))
            {
                FailProjectTexturePreviewBacking(backing);
                return;
            }
            backing.state = editor::ProjectTexturePreviewState::Ready;
        }

        void AdvanceProjectTexturePreviewBacking(ProjectTexturePreviewBacking& backing,
                                                 const vanguard::u64 currentUiFrame) noexcept
        {
            if (!backing.occupied || m_streamingService == nullptr || backing.interestCount == 0u)
                return;

            if (backing.state == editor::ProjectTexturePreviewState::Ready)
            {
                RefreshReadyProjectTexturePreviewPresentation(backing, currentUiFrame);
                return;
            }

            if (backing.state == editor::ProjectTexturePreviewState::LoadingResidency)
            {
                if (!backing.loadedResource.IsValid())
                {
                    FailProjectTexturePreviewBacking(backing);
                    return;
                }
                vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
                if (!QueryProjectTexturePreviewPublication(backing.resource, current))
                {
                    FailProjectTexturePreviewBacking(backing);
                    return;
                }
                if (!SameProjectTexturePreviewPublication(backing, current))
                {
                    RetargetProjectTexturePreviewBacking(backing, current,
                                                        editor::ProjectTexturePreviewState::WaitingForCurrentGeneration);
                    return;
                }
                if (!IsPublishedTextureGeneration(backing.loadedResource, current.publishedPhysicalPath))
                {
                    ReacquireProjectTexturePreviewGeneration(backing);
                    return;
                }
                AdvanceProjectTexturePreviewResidency(backing);
                return;
            }

            if (backing.state == editor::ProjectTexturePreviewState::WaitingForCurrentGeneration)
            {
                vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
                if (!QueryProjectTexturePreviewPublication(backing.resource, current))
                {
                    backing.state = editor::ProjectTexturePreviewState::Failed;
                    return;
                }
                if (!SameProjectTexturePreviewPublication(backing, current))
                    RetargetProjectTexturePreviewBacking(backing, current, editor::ProjectTexturePreviewState::Queued);
                else
                    backing.state = editor::ProjectTexturePreviewState::Queued;
                return;
            }

            if (backing.state != editor::ProjectTexturePreviewState::LoadingResource || !backing.request ||
                !backing.request.HasFinished())
                return;
            if (!backing.request.HasLoaded())
            {
                backing.request.Reset();
                vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
                if (QueryProjectTexturePreviewPublication(backing.resource, current) &&
                    !SameProjectTexturePreviewPublication(backing, current))
                    RetargetProjectTexturePreviewBacking(backing, current,
                                                        editor::ProjectTexturePreviewState::WaitingForCurrentGeneration);
                else
                    backing.state = editor::ProjectTexturePreviewState::Failed;
                return;
            }

            vanguard::resources::ResourceHandle loaded = backing.request.Acquire();
            backing.request.Reset();
            if (!loaded.IsValid())
            {
                backing.state = editor::ProjectTexturePreviewState::Failed;
                return;
            }
            if (loaded.GetPath() != backing.resource.GetPath() || loaded.GetType() != vanguard::textures::TextureResourceType)
            {
                loaded.Reset();
                backing.state = editor::ProjectTexturePreviewState::Failed;
                return;
            }

            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
            if (!QueryProjectTexturePreviewPublication(backing.resource, current))
            {
                loaded.Reset();
                backing.state = editor::ProjectTexturePreviewState::Failed;
                return;
            }
            if (!SameProjectTexturePreviewPublication(backing, current))
            {
                loaded.Reset();
                RetargetProjectTexturePreviewBacking(backing, current,
                                                    editor::ProjectTexturePreviewState::WaitingForCurrentGeneration);
                return;
            }
            if (!IsPublishedTextureGeneration(loaded, current.publishedPhysicalPath))
            {
                loaded.Reset();
                backing.state = editor::ProjectTexturePreviewState::WaitingForCurrentGeneration;
                return;
            }

            backing.loadedResource = static_cast<vanguard::resources::ResourceHandle&&>(loaded);
            // This is the exact P4.2 CPU handoff. P4.3 must classify same-path
            // residency by exact generation identity, never numeric ordering
            // (resource generations wrap): exact current may coalesce; a current
            // mismatch may wait only when residency GetInfo proves its replacement
            // target exactly equals this loaded generation and replacement state is
            // Uploading/InstallationPending, or Failed+retryRequired for the existing
            // resident-delivery retry owner; any other mismatch must reacquire current
            // publication or fail closed; NotFound may create the first residency;
            // Ambiguous fails closed.
            backing.state = editor::ProjectTexturePreviewState::LoadingResidency;
        }

        [[nodiscard]] static bool IsProjectTexturePreviewBackingProgressing(const ProjectTexturePreviewBacking& backing) noexcept
        {
            return backing.occupied &&
                   (backing.state == editor::ProjectTexturePreviewState::LoadingResource ||
                    backing.state == editor::ProjectTexturePreviewState::WaitingForCurrentGeneration ||
                    backing.state == editor::ProjectTexturePreviewState::LoadingResidency ||
                    (backing.state == editor::ProjectTexturePreviewState::Retiring &&
                     backing.request && !backing.request.HasFinished()));
        }

        [[nodiscard]] static bool IsProjectTexturePreviewBackingCheckable(const ProjectTexturePreviewBacking& backing) noexcept
        {
            return IsProjectTexturePreviewBackingProgressing(backing) ||
                   (backing.occupied &&
                    (backing.state == editor::ProjectTexturePreviewState::Ready ||
                     backing.state == editor::ProjectTexturePreviewState::Retiring));
        }

        [[nodiscard]] vanguard::u32 CountProjectTexturePreviewProgressingBackingsAtOrBelow(
            const editor::ProjectTexturePreviewPriority priority) const noexcept
        {
            vanguard::u32 count = 0u;
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (IsProjectTexturePreviewBackingProgressing(backing) &&
                    !IsHigherProjectTexturePreviewPriority(backing.priority, priority))
                    ++count;
            return count;
        }

        [[nodiscard]] bool CanStartProjectTexturePreviewBacking(const ProjectTexturePreviewBacking& backing,
                                                               const vanguard::u32 progressing) const noexcept
        {
            static_assert(editor::MaximumProjectTexturePreviewProgressingBackings >= 2u);
            // Lower-priority starts may not borrow the slots reserved for a
            // future Interactive/Focused request. Existing work is never cancelled
            // merely because a higher-priority request arrives later.
            return editor::detail::IsProjectTexturePreviewAdmissionWithinCumulativeReserves(
                backing.priority,
                CountProjectTexturePreviewProgressingBackingsAtOrBelow(editor::ProjectTexturePreviewPriority::Background),
                CountProjectTexturePreviewProgressingBackingsAtOrBelow(editor::ProjectTexturePreviewPriority::Interactive),
                progressing,
                editor::MaximumProjectTexturePreviewProgressingBackings - 2u,
                editor::MaximumProjectTexturePreviewProgressingBackings - 1u,
                editor::MaximumProjectTexturePreviewProgressingBackings);
        }

        void DriveProjectTexturePreviews(const vanguard::u64 currentUiFrame) noexcept
        {
            if (!m_previewAccepting || m_streamingService == nullptr || m_previewLiveBackings == 0u)
                return;

            const vanguard::u32 pollBase = m_previewPollCursor;
            vanguard::u32 nextPollCursor = pollBase;
            vanguard::u32 polled = 0u;
            for (vanguard::u32 scanned = 0u;
                 scanned < editor::MaximumProjectTexturePreviewBackings && polled < editor::MaximumProjectTexturePreviewStateChecksPerTick;
                 ++scanned)
            {
                const vanguard::u32 index = (pollBase + scanned) % editor::MaximumProjectTexturePreviewBackings;
                ProjectTexturePreviewBacking& backing = m_previewBackings[index];
                if (!IsProjectTexturePreviewBackingCheckable(backing))
                    continue;
                if (backing.state == editor::ProjectTexturePreviewState::Retiring)
                {
                    VG_ASSERT_MSG(backing.interestCount == 0u,
                                  "project texture preview Retiring backing may not retain logical interests");
                    if (backing.request)
                    {
                        if (backing.request.HasFinished())
                            RecycleProjectTexturePreviewBacking(index, backing);
                    }
                    else if (editor::detail::CanRetireEditorUiFrameResource(backing.retireAfterUiFrame, currentUiFrame))
                        RecycleProjectTexturePreviewBacking(index, backing);
                }
                else
                    AdvanceProjectTexturePreviewBacking(backing, currentUiFrame);
                ++polled;
                nextPollCursor = (index + 1u) % editor::MaximumProjectTexturePreviewBackings;
            }
            m_previewPollCursor = nextPollCursor;

            // A supersession backing uses the dedicated overlap slot only while
            // it cannot fit the ordinary priority reserves after its predecessor
            // has retired. Reclassify promptly so another N->N+1 handoff can use
            // the transition slot without increasing the hard 64-demand cap.
            RebalanceProjectTexturePreviewSupersessionResidencySlot();

            vanguard::u32 progressing = 0u;
            for (const ProjectTexturePreviewBacking& backing : m_previewBackings)
                if (IsProjectTexturePreviewBackingProgressing(backing))
                    ++progressing;

            vanguard::u32 starts = 0u;
            while (starts < editor::MaximumProjectTexturePreviewStartsPerTick &&
                   progressing < editor::MaximumProjectTexturePreviewProgressingBackings)
            {
                vanguard::u32 chosen = editor::InvalidProjectTexturePreviewIndex;
                editor::ProjectTexturePreviewPriority chosenPriority = editor::ProjectTexturePreviewPriority::Background;
                for (vanguard::u32 scanned = 0u; scanned < editor::MaximumProjectTexturePreviewBackings; ++scanned)
                {
                    const vanguard::u32 index = (m_previewStartCursor + scanned) % editor::MaximumProjectTexturePreviewBackings;
                    const ProjectTexturePreviewBacking& backing = m_previewBackings[index];
                    if (!backing.occupied || backing.interestCount == 0u || backing.state != editor::ProjectTexturePreviewState::Queued)
                        continue;
                    if (chosen == editor::InvalidProjectTexturePreviewIndex ||
                        IsHigherProjectTexturePreviewPriority(backing.priority, chosenPriority))
                    {
                        chosen = index;
                        chosenPriority = backing.priority;
                    }
                }
                if (chosen == editor::InvalidProjectTexturePreviewIndex)
                    break;
                if (!CanStartProjectTexturePreviewBacking(m_previewBackings[chosen], progressing))
                    break;

                m_previewStartCursor = (chosen + 1u) % editor::MaximumProjectTexturePreviewBackings;
                ++starts;
                if (StartProjectTexturePreviewRequest(m_previewBackings[chosen]))
                    ++progressing;
            }
        }

        [[nodiscard]] ProjectTexturePreviewInterest* ResolveProjectTexturePreviewInterest(
            const editor::ProjectTexturePreviewHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.ownerCookie != m_previewOwnerCookie ||
                handle.index >= editor::MaximumProjectTexturePreviewInterests)
                return nullptr;
            ProjectTexturePreviewInterest& interest = m_previewInterests[handle.index];
            return interest.occupied && interest.generation == handle.generation ? &interest : nullptr;
        }

        [[nodiscard]] const ProjectTexturePreviewInterest* ResolveProjectTexturePreviewInterest(
            const editor::ProjectTexturePreviewHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.ownerCookie != m_previewOwnerCookie ||
                handle.index >= editor::MaximumProjectTexturePreviewInterests)
                return nullptr;
            const ProjectTexturePreviewInterest& interest = m_previewInterests[handle.index];
            return interest.occupied && interest.generation == handle.generation ? &interest : nullptr;
        }

        [[nodiscard]] ProjectTexturePreviewBacking* ResolveProjectTexturePreviewBacking(const vanguard::u32 index) noexcept
        {
            if (index >= editor::MaximumProjectTexturePreviewBackings)
                return nullptr;
            ProjectTexturePreviewBacking& backing = m_previewBackings[index];
            return backing.occupied ? &backing : nullptr;
        }

        [[nodiscard]] const ProjectTexturePreviewBacking* ResolveProjectTexturePreviewBacking(const vanguard::u32 index) const noexcept
        {
            if (index >= editor::MaximumProjectTexturePreviewBackings)
                return nullptr;
            const ProjectTexturePreviewBacking& backing = m_previewBackings[index];
            return backing.occupied ? &backing : nullptr;
        }

        void RecycleProjectTexturePreviewInterest(const vanguard::u32 index, ProjectTexturePreviewInterest& interest) noexcept
        {
            interest.request = {};
            interest.resolvedFirstResidentMip = InvalidProjectTexturePreviewMip;
            interest.backingIndex = editor::InvalidProjectTexturePreviewIndex;
            interest.occupied = false;
            interest.nextFree = m_previewInterestFreeHead;
            m_previewInterestFreeHead = index;
            --m_previewLiveInterests;
        }

        void RecycleProjectTexturePreviewBacking(const vanguard::u32 index, ProjectTexturePreviewBacking& backing) noexcept
        {
            VG_ASSERT_MSG(backing.interestCount == 0u, "project texture preview backing may retire only after its final interest");
            backing.request.Reset();
            backing.loadedResource.Reset();
            backing.textureDemand.Reset();
            backing.readyTexture.Reset();
            backing.resource = {};
            backing.buildFingerprint = {};
            backing.contentFingerprint = {};
            backing.state = editor::ProjectTexturePreviewState::Queued;
            backing.priority = editor::ProjectTexturePreviewPriority::Background;
            backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
            backing.retireAfterUiFrame = 0u;
            ResetProjectTexturePreviewQualityRetry(backing);
            backing.presentationRevision = 0u;
            backing.identity = 0u;
            backing.supersessionPredecessorIdentity = 0u;
            backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
            backing.occupied = false;
            backing.nextFree = m_previewBackingFreeHead;
            m_previewBackingFreeHead = index;
            --m_previewLiveBackings;
        }

        void DrainProjectTexturePreviewRequests() noexcept
        {
            // Do not cancel-and-detach preview root requests during service
            // teardown. ResourceStreaming's async read cancellation is not a
            // completion barrier, while the lower service's lifecycle requires
            // zero active loads/reads immediately after this dependent quiesces.
            // Waiting here is bounded by the preview backing cap and is legal
            // while ResourceStreaming/Jobs/IO are still live dependencies.
            for (ProjectTexturePreviewBacking& backing : m_previewBackings)
            {
                if (!backing.occupied || !backing.request || backing.request.HasFinished())
                    continue;
                backing.request.Wait();
            }
        }

        void ResetProjectTexturePreviewState() noexcept
        {
            m_previewInterestFreeHead = editor::InvalidProjectTexturePreviewIndex;
            for (vanguard::u32 remaining = editor::MaximumProjectTexturePreviewInterests; remaining != 0u; --remaining)
            {
                const vanguard::u32 index = remaining - 1u;
                ProjectTexturePreviewInterest& interest = m_previewInterests[index];
                interest.request = {};
                interest.resolvedFirstResidentMip = InvalidProjectTexturePreviewMip;
                interest.backingIndex = editor::InvalidProjectTexturePreviewIndex;
                interest.occupied = false;
                if (interest.generation == ~vanguard::u32{0})
                {
                    interest.nextFree = editor::InvalidProjectTexturePreviewIndex;
                    continue;
                }
                interest.nextFree = m_previewInterestFreeHead;
                m_previewInterestFreeHead = index;
            }
            m_previewBackingFreeHead = editor::InvalidProjectTexturePreviewIndex;
            for (vanguard::u32 remaining = editor::MaximumProjectTexturePreviewBackings; remaining != 0u; --remaining)
            {
                const vanguard::u32 index = remaining - 1u;
                ProjectTexturePreviewBacking& backing = m_previewBackings[index];
                backing.request.Reset();
                backing.loadedResource.Reset();
                backing.textureDemand.Reset();
                backing.readyTexture.Reset();
                backing.resource = {};
                backing.buildFingerprint = {};
                backing.contentFingerprint = {};
                backing.state = editor::ProjectTexturePreviewState::Queued;
                backing.priority = editor::ProjectTexturePreviewPriority::Background;
                backing.desiredFirstResidentMip = InvalidProjectTexturePreviewMip;
                backing.currentFirstResidentMip = InvalidProjectTexturePreviewMip;
                backing.retireAfterUiFrame = 0u;
                backing.presentationRevision = 0u;
                backing.identity = 0u;
                backing.supersessionPredecessorIdentity = 0u;
                backing.interestCount = 0u;
                backing.supersessionPredecessorIndex = editor::InvalidProjectTexturePreviewIndex;
                backing.occupied = false;
                backing.nextFree = m_previewBackingFreeHead;
                m_previewBackingFreeHead = index;
            }
            m_previewLiveInterests = 0u;
            m_previewLiveBackings = 0u;
            m_previewPollCursor = 0u;
            m_previewStartCursor = 0u;
        }

        void ResetResidentDeliveryCandidate() noexcept
        {
            m_residentRequest.Reset();
            m_residentOutput = {};
            m_residentResidency = {};
            m_residentPublication = {};
            m_residentTargetGeneration = 0u;
            m_residentRequestIsReplacement = false;
        }

        void DrainResidentDeliveryRequest() noexcept
        {
            if (m_residentRequest && !m_residentRequest.HasFinished())
                m_residentRequest.Wait();
        }

        void ResetResidentDeliveryDrive() noexcept
        {
            ResetResidentDeliveryCandidate();
            m_residentSweepGeneration = ~vanguard::u64{0};
            m_residentSourceIndex = 0u;
            m_residentOutputIndex = 0u;
            m_residentSweepComplete = false;
            m_residentAutomaticWorkObserved = false;
        }

        void AdvanceResidentDeliveryCursor() noexcept
        {
            const auto sources = m_sources.GetResources();
            if (m_residentSourceIndex >= sources.Count())
            {
                m_residentSweepComplete = true;
                return;
            }
            ++m_residentOutputIndex;
            while (m_residentSourceIndex < sources.Count() && m_residentOutputIndex >= sources[m_residentSourceIndex].metadata.outputs.Size())
            {
                ++m_residentSourceIndex;
                m_residentOutputIndex = 0u;
            }
            m_residentSweepComplete = m_residentSourceIndex >= sources.Count();
        }

        [[nodiscard]] bool ResidentPublicationStillMatches() const noexcept
        {
            vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus current;
            return m_assetPipeline.GetOutputDeliveryStatus(m_residentOutput, current) &&
                   current.publicationState == vanguard::asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                   !current.publicationRetryRequired && current.publishedPhysicalPath == m_residentPublication.publishedPhysicalPath &&
                   current.publishedBuildFingerprint == m_residentPublication.publishedBuildFingerprint &&
                   current.publishedContentFingerprint == m_residentPublication.publishedContentFingerprint;
        }

        [[nodiscard]] static bool IsPublishedTextureGeneration(const vanguard::resources::ResourceHandle& resource,
                                                               const vanguard::filesystem::AbsolutePath& publishedPhysicalPath) noexcept
        {
            if (!resource.IsValid() || resource.GetType() != vanguard::textures::TextureResourceType)
                return false;
            const auto* const texture = static_cast<const vanguard::textures::TextureResourceObject*>(resource.Get());
            if (texture == nullptr || !texture->IsOpen())
                return false;
            const vanguard::textures::TextureSubresourceSource& source = texture->GetSubresourceSource();
            return source.IsOpen() && source.GetKind() == vanguard::streaming::ResourceSourceKind::LooseFile &&
                   source.GetPhysicalPath() == publishedPhysicalPath;
        }

        [[nodiscard]] ResidentDeliveryPumpResult PumpResidentDeliveries() noexcept
        {
            using DeliveryState = vanguard::asset_streaming::ProjectAssetDeliveryStageState;
            using LookupResult = vanguard::rendering::TextureResidentLookupResult;
            using RecookState = vanguard::asset_streaming::ProjectAssetAutomaticRecookState;
            if (!m_assetPipeline.IsInitialized() || m_streamingService == nullptr || m_renderingService == nullptr)
                return ResidentDeliveryPumpResult::ProjectFailure;

            const bool diagnostics = m_renderingService->IsFrameDiagnosticsEnabled();
            vanguard::rendering::TextureResidencyRuntime& textureResidency = m_renderingService->GetTextureResidency();
            if (m_residentRequest)
            {
                if (!m_residentRequest.HasFinished())
                    return ResidentDeliveryPumpResult::Pending;
                if (!m_residentRequest.HasLoaded())
                {
                    ResetResidentDeliveryCandidate();
                    return ResidentDeliveryPumpResult::StreamingFailure;
                }

                vanguard::resources::ResourceHandle resource = m_residentRequest.Acquire();
                const bool replacementRequest = m_residentRequestIsReplacement;
                m_residentRequest.Reset();
                if (!resource.IsValid() || !ResidentPublicationStillMatches())
                {
                    ResetResidentDeliveryCandidate();
                    return resource.IsValid() ? ResidentDeliveryPumpResult::Pending : ResidentDeliveryPumpResult::StreamingFailure;
                }

                if (!IsPublishedTextureGeneration(resource, m_residentPublication.publishedPhysicalPath))
                {
                    if (replacementRequest)
                    {
                        ResetResidentDeliveryCandidate();
                        return ResidentDeliveryPumpResult::StreamingFailure;
                    }
                    m_residentRequest = m_streamingService->GetStreamer().Replace(m_residentOutput, vanguard::resources::LoadPriority::High);
                    if (!m_residentRequest)
                    {
                        ResetResidentDeliveryCandidate();
                        return ResidentDeliveryPumpResult::StreamingFailure;
                    }
                    m_residentRequestIsReplacement = true;
                    if (diagnostics)
                        VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: replacement requested path=%llu",
                                    static_cast<unsigned long long>(m_residentOutput.GetPath().Id()));
                    return ResidentDeliveryPumpResult::Pending;
                }

                m_residentTargetGeneration = resource.GetGeneration();
                vanguard::rendering::TextureResidencyRuntimeFailure renderingFailure;
                if (!textureResidency.ReplaceTexture(resource, m_residentResidency, &renderingFailure))
                {
                    if (renderingFailure.code == vanguard::rendering::TextureResidencyRuntimeFailureCode::ReplacementBusy)
                    {
                        ResetResidentDeliveryCandidate();
                        return ResidentDeliveryPumpResult::Pending;
                    }
                    if (renderingFailure.code != vanguard::rendering::TextureResidencyRuntimeFailureCode::ReplacementRetryRequired)
                    {
                        ResetResidentDeliveryCandidate();
                        return ResidentDeliveryPumpResult::RenderingFailure;
                    }
                    if (!textureResidency.RetryTextureReplacement(m_residentResidency, &renderingFailure))
                    {
                        const bool busy = renderingFailure.code == vanguard::rendering::TextureResidencyRuntimeFailureCode::ReplacementBusy;
                        ResetResidentDeliveryCandidate();
                        return busy ? ResidentDeliveryPumpResult::Pending : ResidentDeliveryPumpResult::RenderingFailure;
                    }
                }
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: texture cutover requested path=%llu generation=%u",
                                static_cast<unsigned long long>(m_residentOutput.GetPath().Id()), m_residentTargetGeneration);
                return ResidentDeliveryPumpResult::Pending;
            }

            if (m_residentTargetGeneration != 0u)
            {
                if (!ResidentPublicationStillMatches())
                {
                    ResetResidentDeliveryCandidate();
                    return ResidentDeliveryPumpResult::Pending;
                }
                vanguard::rendering::TextureRuntimeInfo info;
                vanguard::rendering::TextureResidencyRuntimeFailure renderingFailure;
                if (!textureResidency.GetInfo(m_residentResidency, info, &renderingFailure))
                {
                    ResetResidentDeliveryCandidate();
                    return ResidentDeliveryPumpResult::RenderingFailure;
                }
                if (info.replacementState == vanguard::rendering::TextureReplacementState::Failed && info.replacementRetryRequired)
                {
                    if (!textureResidency.RetryTextureReplacement(m_residentResidency, &renderingFailure))
                        return renderingFailure.code == vanguard::rendering::TextureResidencyRuntimeFailureCode::ReplacementBusy
                                   ? ResidentDeliveryPumpResult::Pending
                                   : ResidentDeliveryPumpResult::RenderingFailure;
                    return ResidentDeliveryPumpResult::Pending;
                }
                if (info.state != vanguard::rendering::TextureRuntimeState::BindlessReady ||
                    info.resourceGeneration != m_residentTargetGeneration || info.replacementState != vanguard::rendering::TextureReplacementState::None)
                    return ResidentDeliveryPumpResult::Pending;

                const vanguard::asset_streaming::ProjectAssetResidentDeliveryEvidence evidence{
                    m_residentOutput, m_residentTargetGeneration, m_residentPublication.publishedBuildFingerprint,
                    m_residentPublication.publishedContentFingerprint, false};
                if (!m_assetPipeline.ReportResidentDelivery(evidence))
                {
                    if (ResidentPublicationStillMatches())
                        return ResidentDeliveryPumpResult::Busy;
                    ResetResidentDeliveryCandidate();
                    return ResidentDeliveryPumpResult::Pending;
                }
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: current path=%llu generation=%u",
                                static_cast<unsigned long long>(m_residentOutput.GetPath().Id()), m_residentTargetGeneration);
                AdvanceResidentDeliveryCursor();
                ResetResidentDeliveryCandidate();
                return ResidentDeliveryPumpResult::Pending;
            }

            const vanguard::asset_streaming::ProjectAssetAutomaticRecookStatus automatic = m_assetPipeline.GetAutomaticRecookStatus();
            if (automatic.state != RecookState::Idle || automatic.pendingDeliveryOutputs != 0u)
            {
                m_residentAutomaticWorkObserved = true;
                return ResidentDeliveryPumpResult::Pending;
            }

            const vanguard::u64 acceptedGeneration = m_assetPipeline.GetAcceptedAssetGeneration();
            if (acceptedGeneration != m_residentSweepGeneration || m_residentAutomaticWorkObserved)
            {
                m_residentSweepGeneration = acceptedGeneration;
                m_residentSourceIndex = 0u;
                m_residentOutputIndex = 0u;
                m_residentSweepComplete = false;
                m_residentAutomaticWorkObserved = false;
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: sweep generation=%llu sources=%u",
                                static_cast<unsigned long long>(acceptedGeneration), m_sources.GetResources().Count());
            }
            if (m_residentSweepComplete)
                return ResidentDeliveryPumpResult::Success;

            const auto sources = m_sources.GetResources();
            vanguard::u32 checked = 0u;
            while (checked < ResidentDeliveryChecksPerFrame)
            {
                while (m_residentSourceIndex < sources.Count() && m_residentOutputIndex >= sources[m_residentSourceIndex].metadata.outputs.Size())
                {
                    ++m_residentSourceIndex;
                    m_residentOutputIndex = 0u;
                }
                if (m_residentSourceIndex >= sources.Count())
                {
                    m_residentSweepComplete = true;
                    return ResidentDeliveryPumpResult::Success;
                }

                const vanguard::assets::SourceRecord& source = sources[m_residentSourceIndex];
                const vanguard::assets::AssetOutput& output = source.metadata.outputs[m_residentOutputIndex];
                ++checked;
                if (output.type != vanguard::textures::TextureResourceType)
                {
                    AdvanceResidentDeliveryCursor();
                    continue;
                }

                const vanguard::resources::ResourceReference resource = vanguard::assets::GetAssetOutputReference(source.metadata.id, output);
                vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus publication;
                const bool tracked = m_assetPipeline.GetOutputDeliveryStatus(resource, publication);
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources,
                                "project resident delivery: output path=%llu tracked=%u publication=%u resident=%u publicationRetry=%u physical=%u",
                                static_cast<unsigned long long>(resource.GetPath().Id()), tracked ? 1u : 0u, static_cast<vanguard::u32>(publication.publicationState),
                                static_cast<vanguard::u32>(publication.residentState), publication.publicationRetryRequired ? 1u : 0u,
                                publication.publishedPhysicalPath.IsFilePath() ? 1u : 0u);
                if (!tracked || publication.residentState == DeliveryState::Current ||
                    publication.publicationState != DeliveryState::Installed || publication.publicationRetryRequired ||
                    !publication.publishedPhysicalPath.IsFilePath())
                {
                    AdvanceResidentDeliveryCursor();
                    continue;
                }

                vanguard::rendering::TextureResidentResource resident;
                const LookupResult found = textureResidency.FindResidentResource(resource.GetPath(), resident);
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: lookup path=%llu result=%u generation=%u",
                                static_cast<unsigned long long>(resource.GetPath().Id()), static_cast<vanguard::u32>(found), resident.resourceGeneration);
                if (found == LookupResult::Ambiguous)
                    return ResidentDeliveryPumpResult::RenderingFailure;
                if (found == LookupResult::NotFound)
                {
                    AdvanceResidentDeliveryCursor();
                    continue;
                }

                m_residentOutput = resource;
                m_residentResidency = resident.residency;
                m_residentPublication = publication;
                m_residentRequest = m_streamingService->GetStreamer().Request(resource, vanguard::resources::LoadPriority::High);
                if (!m_residentRequest)
                {
                    ResetResidentDeliveryCandidate();
                    return ResidentDeliveryPumpResult::StreamingFailure;
                }
                m_residentRequestIsReplacement = false;
                if (diagnostics)
                    VG_LOG_INFO(vanguard::diagnostics::Category::Resources, "project resident delivery: current resource requested path=%llu",
                                static_cast<unsigned long long>(resource.GetPath().Id()));
                return ResidentDeliveryPumpResult::Pending;
            }
            return ResidentDeliveryPumpResult::Pending;
        }

        [[nodiscard]] engine::FrameParticipantStatus DriveAssetFrame(const engine::FrameContext& context) noexcept
        {
            using PipelineResult = vanguard::asset_streaming::ProjectAssetPipelineResult;
            if (!m_driveFrames)
                return engine::FrameParticipantStatus::Success();

            vanguard::asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
            // The large reviewed source transaction may have delegated only its
            // reversible Prepare step to a worker. Settle it on the workspace
            // owner before observation is re-enabled: ApplyPrepared, durable
            // cohort intent, VPAS and publication never run on the worker.
            // Terminal per-import failures are recorded in the pipeline's
            // process-local status for the review session, not fatal to Editor.
            const PipelineResult prepared = m_assetPipeline.TickDeferredPlannedImport(&pipelineFailure);
            if (prepared != PipelineResult::Success && prepared != PipelineResult::Pending &&
                prepared != PipelineResult::CommittedReconciliationPending)
                VG_LOG_WARNING(vanguard::diagnostics::Category::Resources,
                               "Project Workspace reviewed import staging stopped: result=%u transaction=%u staleSite=%u",
                               static_cast<vanguard::u32>(prepared),
                               static_cast<vanguard::u32>(pipelineFailure.operation.transaction),
                               pipelineFailure.stalePlanSite);
            PipelineResult observation = PipelineResult::Pending;
            if (!m_assetPipeline.IsObservingSources())
            {
                const PipelineResult enabled = m_assetPipeline.EnableObservation({}, &pipelineFailure);
                if (enabled != PipelineResult::Success && enabled != PipelineResult::Busy)
                {
                    VG_LOG_ERROR(vanguard::diagnostics::Category::Resources,
                                 "Project Workspace deferred source observation start failed: result=%u build=%u sources=%u operation=%u transaction=%u",
                                 static_cast<vanguard::u32>(pipelineFailure.result), static_cast<vanguard::u32>(pipelineFailure.build),
                                 static_cast<vanguard::u32>(pipelineFailure.sources),
                                 static_cast<vanguard::u32>(pipelineFailure.operation.result),
                                 static_cast<vanguard::u32>(pipelineFailure.operation.transaction));
                    return engine::FrameParticipantStatus::Failure("Project Workspace source observation could not start");
                }
            }
            if (m_assetPipeline.IsObservingSources())
                observation = m_assetPipeline.TickObservation(nullptr, &pipelineFailure);
            const bool observationRetryable =
                observation == PipelineResult::ReconciliationFailed ||
                observation == PipelineResult::SnapshotSaveFailed ||
                observation == PipelineResult::CommittedReconciliationPending;
            if (observation != PipelineResult::Success && observation != PipelineResult::Pending &&
                observation != PipelineResult::Busy && !observationRetryable)
                return engine::FrameParticipantStatus::Failure("Project Workspace source observation failed");

            if (!m_initialObservationCatchUpComplete && observation == PipelineResult::Success)
                m_initialObservationCatchUpComplete = true;

            if (m_initialObservationCatchUpComplete && !m_assetPipeline.IsAutomaticRecookEnabled())
            {
                const PipelineResult enabled = m_assetPipeline.EnableAutomaticRecook(&pipelineFailure);
                if (enabled != PipelineResult::Success && enabled != PipelineResult::Busy)
                    return engine::FrameParticipantStatus::Failure("Project Workspace automatic recook could not start");
            }

            const vanguard::asset_streaming::ProjectAssetAutomaticRecookStatus automaticStatus =
                m_assetPipeline.GetAutomaticRecookStatus();
            const bool explicitCoordinationNeedsTick =
                automaticStatus.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Pending ||
                automaticStatus.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Planning ||
                automaticStatus.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Building ||
                automaticStatus.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Validating ||
                automaticStatus.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Cancelling;
            if (m_assetPipeline.IsAutomaticRecookEnabled() || explicitCoordinationNeedsTick)
            {
                const PipelineResult automatic = m_assetPipeline.TickAutomaticRecook(&pipelineFailure);
                const auto completedRecook = m_assetPipeline.GetAutomaticRecookStatus();
                if (completedRecook.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::Failed ||
                    completedRecook.state == vanguard::asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                {
                    if (!m_automaticRecookFailureLogged)
                    {
                        const auto& root = completedRecook.firstFailedRoot;
                        VG_LOG_ERROR(vanguard::diagnostics::Category::Resources,
                                     "Project automatic recook failed: reason=%s graph=%s compiler=%s rootIndex=%u failedRoots=%u message=%s",
                                     vanguard::assets::ToString(completedRecook.lastRecookFailure),
                                     vanguard::assets::ToString(root.graphFailure),
                                     vanguard::assets::ToString(root.buildResult),
                                     root.rootIndex, completedRecook.failedRootCount, root.message);
                        m_automaticRecookFailureLogged = true;
                    }
                }
                else
                    m_automaticRecookFailureLogged = false;
                if (automatic != PipelineResult::Success && automatic != PipelineResult::Pending &&
                    automatic != PipelineResult::BuildRequestFailed && automatic != PipelineResult::PublicationFailed &&
                    automatic != PipelineResult::CommittedReconciliationPending)
                    return engine::FrameParticipantStatus::Failure("Project Workspace automatic recook failed");
            }

            RefreshAssetCatalogIfNeeded();

            const ResidentDeliveryPumpResult delivery = PumpResidentDeliveries();
            if (delivery != ResidentDeliveryPumpResult::Success && delivery != ResidentDeliveryPumpResult::Pending &&
                delivery != ResidentDeliveryPumpResult::Busy)
                return engine::FrameParticipantStatus::Failure("Project Workspace resident-delivery handoff failed");

            // Editor UI BeginFrame runs before WorldStreaming and copies this exact
            // FrameContext.frame into its UI-frame identity. Passing the frame
            // directly avoids a reverse ProjectWorkspace -> EditorUi dependency
            // while preserving the shared F -> later-frame retirement contract.
            DriveProjectTexturePreviews(context.frame);

            const editor::ProjectResidentRetirementPumpResult retirement = PumpResidentRetirements();
            if (retirement != editor::ProjectResidentRetirementPumpResult::Success &&
                retirement != editor::ProjectResidentRetirementPumpResult::Busy)
                return engine::FrameParticipantStatus::Failure("Project Workspace resident-retirement handoff failed");
            return engine::FrameParticipantStatus::Success();
        }

        static engine::FrameParticipantStatus ExecuteAssetFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedProjectWorkspaceService*>(userData);
            return service != nullptr ? service->DriveAssetFrame(context)
                                      : engine::FrameParticipantStatus::Failure("Project Workspace service is unavailable");
        }

        void RefreshAssetCatalogIfNeeded() noexcept
        {
            const vanguard::asset_streaming::ProjectAssetCatalogStatus status = m_assetCatalog.GetStatus();
            const vanguard::u64 generation = m_sources.GetGeneration();
            if (generation == 0u ||
                (status.refreshState == vanguard::asset_streaming::ProjectAssetCatalogRefreshState::Idle &&
                 status.sourceGeneration == generation))
                return;
            static_cast<void>(m_assetCatalog.Refresh(m_sources, ProjectAssetCatalogCaptureRowsPerFrame,
                                                     ProjectAssetCatalogCapturePathBytesPerFrame));
        }

        vanguard::projects::ProjectDescriptor m_project;
        vanguard::assets::SourceDatabase m_sources;
        vanguard::asset_streaming::ProjectAssetCatalog m_assetCatalog;
        vanguard::projects::ProjectWorkspace m_workspace;
        vanguard::filesystem::AbsolutePath m_projectFile;
        vanguard::texture_tools::TextureAssetCompiler m_textureCompiler;
        vanguard::model_tools::GeneratedRequestStore m_generated;
        vanguard::material_tools::ImportedSurfaceMaterialContext m_materialContext;
        vanguard::material_tools::MaterialArtifactCompilers m_materialCompilers;
        vanguard::mesh_tools::ModelAssetCompiler m_modelCompiler;
        vanguard::model_tools::ModelBuildAdapter m_modelAdapter;
        bool m_modelToolsAcquired = false;
        // Declared after all compiler/adapter owners: destruction is pipeline-first
        // even after an unsuccessful lifecycle cleanup.
        vanguard::asset_streaming::ProjectAssetPipeline m_assetPipeline;
        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::ResourceStreamingService* m_streamingService = nullptr;
        vanguard::engine::RenderingService* m_renderingService = nullptr;
        ProjectTexturePreviewInterest m_previewInterests[editor::MaximumProjectTexturePreviewInterests]{};
        ProjectTexturePreviewBacking m_previewBackings[editor::MaximumProjectTexturePreviewBackings]{};
        vanguard::u64 m_previewOwnerCookie = 0u;
        vanguard::u32 m_previewInterestFreeHead = editor::InvalidProjectTexturePreviewIndex;
        vanguard::u32 m_previewBackingFreeHead = editor::InvalidProjectTexturePreviewIndex;
        vanguard::u32 m_previewLiveInterests = 0u;
        vanguard::u32 m_previewLiveBackings = 0u;
        vanguard::u32 m_previewPollCursor = 0u;
        vanguard::u32 m_previewStartCursor = 0u;
        vanguard::u64 m_previewNextBackingIdentity = 1u;
        vanguard::resources::PipelineRequest m_residentRequest;
        vanguard::resources::ResourceReference m_residentOutput;
        vanguard::rendering::GpuTextureResidencyHandle m_residentResidency;
        vanguard::asset_streaming::ProjectAssetOutputDeliveryStatus m_residentPublication;
        vanguard::u64 m_residentSweepGeneration = ~vanguard::u64{0};
        vanguard::u32 m_residentSourceIndex = 0u;
        vanguard::u32 m_residentOutputIndex = 0u;
        vanguard::u32 m_residentTargetGeneration = 0u;
        bool m_driveFrames = false;
        bool m_initialObservationCatchUpComplete = false;
        bool m_automaticRecookFailureLogged = false;
        bool m_residentSweepComplete = false;
        bool m_residentRequestIsReplacement = false;
        bool m_residentAutomaticWorkObserved = false;
        bool m_previewAccepting = false;

        friend vanguard::u64 editor::detail::GetProjectTexturePreviewOwnerCookieForTest(
            const editor::ProjectWorkspaceService& workspace) noexcept;
    };

    app::Service* CreateProjectWorkspaceService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(ManagedProjectWorkspaceService), alignof(ManagedProjectWorkspaceService));
        return block ? ::new (block.address) ManagedProjectWorkspaceService(static_cast<const editor::ProjectWorkspaceConfig*>(userData)) : nullptr;
    }

    void DestroyProjectWorkspaceService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedProjectWorkspaceService*>(service)->~ManagedProjectWorkspaceService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedProjectWorkspaceService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    ProjectWorkspaceService* detail::CreateUninitializedProjectWorkspaceForTest(
        const ProjectWorkspaceConfig& config) noexcept
    {
        return static_cast<ProjectWorkspaceService*>(CreateProjectWorkspaceService(
            const_cast<ProjectWorkspaceConfig*>(&config)));
    }

    void detail::DestroyUninitializedProjectWorkspaceForTest(ProjectWorkspaceService*& workspace) noexcept
    {
        DestroyProjectWorkspaceService(workspace, nullptr);
        workspace = nullptr;
    }

    u64 detail::GetProjectTexturePreviewOwnerCookieForTest(const ProjectWorkspaceService& workspace) noexcept
    {
        return static_cast<const ManagedProjectWorkspaceService&>(workspace).m_previewOwnerCookie;
    }

    bool RegisterProjectWorkspaceService(application::EngineHost& host, const ProjectWorkspaceConfig& config, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{engine::FilesystemServiceId, application::DependencyKind::Required},
                                                                {engine::FramePipelineServiceId, application::DependencyKind::Required},
                                                                {engine::JobsServiceId, application::DependencyKind::Required},
                                                                {engine::ResourceStreamingServiceId, application::DependencyKind::Required},
                                                                {engine::RenderingServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{ProjectWorkspaceCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ProjectWorkspaceServiceId;
        descriptor.name = "projectWorkspace";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 5};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateProjectWorkspaceService;
        descriptor.destroy = DestroyProjectWorkspaceService;
        descriptor.userData = const_cast<ProjectWorkspaceConfig*>(&config);
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    ProjectWorkspaceService* FindProjectWorkspaceService(application::EngineHost& host) noexcept
    {
        return static_cast<ProjectWorkspaceService*>(host.FindCapability(ProjectWorkspaceCapabilityId));
    }

    ProjectWorkspaceService* FindProjectWorkspaceService(application::ServiceContext& context) noexcept
    {
        return static_cast<ProjectWorkspaceService*>(context.FindCapability(ProjectWorkspaceCapabilityId));
    }
} // namespace vanguard::editor
