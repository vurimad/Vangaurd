#include <vanguard/nanovanguard/project_asset_context.hpp>

#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/assets/project_source_data.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/component_schemas.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/material_tools/material_asset_compilers.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
#include "../../../source/modelTools/include/vanguard/model_tools/model_project_build.hpp"
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/mesh_tools/mesh_tools.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>

#include <cstring>

namespace vanguard::nanovanguard
{
    namespace
    {
        using model_tools::ComposeComposition;
        using model_tools::ComposeEmbeddedTexture;
        using model_tools::ComposeMaterial;
        using model_tools::GeneratedRequestStore;
        using model_tools::ModelBuildAdapter;
        using model_tools::PrefabCompilerDescriptor;
        using model_tools::ResolveGeneratedFallback;

        void ClearFailure(ProjectAssetContextFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        ProjectAssetContextResult SetFailure(ProjectAssetContextFailure* const failure,
                                             const ProjectAssetContextResult result,
                                             const ProjectAssetContextStage stage) noexcept
        {
            if (failure != nullptr)
            {
                failure->result = result;
                failure->stage = stage;
            }
            return result;
        }

        [[nodiscard]] bool ReadFile(const filesystem::AbsolutePath& path, containers::DynamicArray<u8>& bytes) noexcept
        {
            filesystem::Manager& files = filesystem::GetManager();
            if (!files.FileExist(path))
                return false;
            const u64 size = files.GetFileSize(path);
            if (size > ~u32{0})
                return false;
            auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!file)
                return false;
            bytes.Resize(static_cast<u32>(size));
            file->Serialize(bytes.Data(), bytes.Size());
            return !file->HasErrors();
        }

        [[nodiscard]] bool MetadataFingerprint(const assets::AssetMetadata& metadata,
                                               crypto::Digest256& digest) noexcept
        {
            containers::String encoded;
            if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
                return false;
            digest = crypto::Sha256(encoded.AsChar(), encoded.Length());
            return !digest.IsEmpty();
        }

        [[nodiscard]] bool AppendModelDependencyWitness(
            const assets::SourceRecord& source, const asset_streaming::ProjectAssetPipeline& pipeline,
            containers::DynamicArray<mesh_tools::ModelImportDependencyWitness>& witnesses) noexcept
        {
            if (!source.sourceIdentity.IsValid() || !source.path.IsFilePath() ||
                source.metadataResult != assets::MetadataResult::Success ||
                source.importState != assets::SourceImportState::MetadataSelected ||
                !(source.metadata.id.IsValid()))
                return false;
            for (const mesh_tools::ModelImportDependencyWitness& existing : witnesses)
            {
                if (existing.sourceIdentity == source.sourceIdentity)
                    return existing.physicalPath == source.path && existing.asset == source.metadata.id;
            }
            const asset_streaming::ProjectAssetState* const accepted =
                pipeline.LookupAcceptedAsset(source.metadata.id);
            if (accepted == nullptr || accepted->presence != asset_streaming::ProjectAssetPresence::Present ||
                accepted->physicalPath != source.path || accepted->sourceIdentity != source.sourceIdentity ||
                !accepted->hasSourceContentFingerprint || accepted->sourceContentFingerprint.IsEmpty())
                return false;
            crypto::Digest256 metadataDigest;
            if (!MetadataFingerprint(source.metadata, metadataDigest))
                return false;
            mesh_tools::ModelImportDependencyWitness witness;
            witness.sourceIdentity = source.sourceIdentity;
            witness.physicalPath = source.path;
            witness.asset = source.metadata.id;
            witness.sourceContentFingerprint = accepted->sourceContentFingerprint;
            witness.metadataFingerprint = metadataDigest;
            witness.sourceExists = true;
            witness.metadataExists = true;
            witness.hasSourceContentFingerprint = true;
            witness.hasMetadataFingerprint = true;
            const u32 previous = witnesses.Size();
            witnesses.PushBack(static_cast<mesh_tools::ModelImportDependencyWitness&&>(witness));
            return witnesses.Size() == previous + 1u;
        }

        [[nodiscard]] bool BuildModelDependencyWitnesses(
            const mesh_tools::ModelImportManifest& manifest, const mesh_tools::ModelImportSettings& settings,
            const assets::SourceDatabase& sources, const asset_streaming::ProjectAssetPipeline& pipeline,
            const u32 maximum,
            containers::DynamicArray<mesh_tools::ModelImportDependencyWitness>& witnesses) noexcept
        {
            witnesses.Clear();
            const auto appendReference = [&](const resources::ResourceReference reference) noexcept
            {
                if (!reference.IsValid())
                    return true;
                const assets::SourceRecord* const source = sources.LookupOutput(reference);
                return source != nullptr && witnesses.Size() < maximum &&
                       AppendModelDependencyWitness(*source, pipeline, witnesses);
            };
            if (!appendReference(settings.mesh.defaultMaterial) || !appendReference(settings.mesh.skeleton))
                return false;
            for (const mesh_tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
            {
                if (resolution.resolution == mesh_tools::ModelImportTextureResolutionKind::ReuseManagedResource &&
                    !appendReference(resolution.managedResource))
                    return false;
                // AdoptProjectSource is finalized to ReuseManagedResource by the
                // committed import path. Persisting a provisional adoption across
                // a later refresh would violate the P3 finalization contract.
                if (resolution.resolution == mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource)
                    return false;
            }
            for (const mesh_tools::ModelImportTextureReference& texture : manifest.textureReferences)
            {
                if (texture.storage != mesh_tools::ImportedTextureStorage::ExternalSource ||
                    texture.resolvedPath.Empty())
                    continue;
                const filesystem::AbsolutePath path = filesystem::AbsolutePath::ParseFilePath(texture.resolvedPath);
                const assets::SourceRecord* const source = path.IsFilePath() ? sources.LookupResource(path) : nullptr;
                if (source == nullptr || source->metadataResult != assets::MetadataResult::Success ||
                    source->importState != assets::SourceImportState::MetadataSelected)
                    continue;
                if (witnesses.Size() >= maximum || !AppendModelDependencyWitness(*source, pipeline, witnesses))
                    return false;
            }
            return witnesses.Size() <= maximum;
        }

    } // namespace

    bool ComposeModelCompositionBuildRequest(const mesh_tools::ModelOutputBuildRequestContext& context,
                                             assets::OwnedBuildRequest& request) noexcept
    {
        return model_tools::ComposeModelCompositionBuildRequest(context, request);
    }

    struct ProjectAssetContext::Impl final
    {
        projects::ProjectWorkspace workspace;
        asset_streaming::ProjectAssetPipelineConfig config;
        GeneratedRequestStore generated;
        material_tools::ImportedSurfaceMaterialContext materialContext;
        material_tools::MaterialArtifactCompilers materialCompilers;
        texture_tools::TextureAssetCompiler textureCompiler;
        mesh_tools::ModelAssetCompiler modelCompiler;
        assets::SourceDatabase sources;
        ModelBuildAdapter modelAdapter;
        resources::ResourceRegistry registry;
        resources::ResourcePipeline resourcePipeline;
        streaming::ResourceStreamer streamer;
        // Keep the project pipeline last so ordinary member destruction would
        // also destroy it before every callback/resource owner above it.
        asset_streaming::ProjectAssetPipeline pipeline;
        asset_streaming::ProjectAssetPipelineResult startupPipelineResult = asset_streaming::ProjectAssetPipelineResult::Success;
        asset_streaming::ProjectAssetRestoreReport startupRestore;
        bool ownsDiagnostics = false;
        bool ownsIo = false;
        bool ownsFilesystem = false;
        bool ownsJobs = false;
        bool ownsTextureTools = false;
        bool ownsMeshTools = false;
        bool open = false;
        bool retainedAfterUnsafeShutdown = false;
    };

    ProjectAssetContext::~ProjectAssetContext()
    {
        // Close intentionally leaves m_impl allocated when a live owner refuses
        // shutdown. Losing that pointer here is deliberate one-shot-process
        // retention: member destructors must not run under an unsafe owner graph.
        static_cast<void>(Close());
    }

    ProjectAssetContextResult ProjectAssetContext::Open(const projects::ProjectWorkspace& workspace,
                                                        const containers::StringView selectedTarget,
                                                        ProjectAssetContextFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return SetFailure(failure, ProjectAssetContextResult::InvalidState, ProjectAssetContextStage::Validation);
        if (!memory::IsInitialized() || !containers::IsInitialized() || !workspace.root.IsDirectoryPath() ||
            !workspace.assets.IsDirectoryPath() || !workspace.derivedData.IsDirectoryPath() ||
            !workspace.intermediate.IsDirectoryPath())
            return SetFailure(failure, ProjectAssetContextResult::InvalidArgument, ProjectAssetContextStage::Validation);

        asset_streaming::ProjectAssetPipelineConfig config;
        if (!asset_streaming::MakeProjectAssetPipelineConfig(workspace.derivedData, workspace.intermediate,
                                                              selectedTarget, config))
            return SetFailure(failure, ProjectAssetContextResult::InvalidArgument, ProjectAssetContextStage::Validation);

        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return SetFailure(failure, ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Validation);
        implementation->workspace = workspace;
        implementation->config = static_cast<asset_streaming::ProjectAssetPipelineConfig&&>(config);
        m_impl = implementation;

        const auto rollback = [this, failure](const ProjectAssetContextResult original,
                                              const ProjectAssetContextStage stage) noexcept
        {
            if (Close() != ProjectAssetContextResult::Success)
                return SetFailure(failure, ProjectAssetContextResult::ShutdownFailed, ProjectAssetContextStage::Shutdown);
            return SetFailure(failure, original, stage);
        };

        if (!diagnostics::IsInitialized())
        {
            if (!diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguard"))
                return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Diagnostics);
            implementation->ownsDiagnostics = true;
        }

        if (!io::IsInitialized())
        {
            if (!io::Initialize())
                return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Io);
            implementation->ownsIo = true;
        }

        if (!reflection::IsInitialized() && !reflection::Initialize())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Reflection);

        const filesystem::Config filesystemConfig{workspace.root, workspace.root, workspace.derivedData};
        if (!filesystem::IsInitialized())
        {
            if (!filesystem::Initialize(filesystemConfig))
                return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Filesystem);
            implementation->ownsFilesystem = true;
        }
        else
        {
            filesystem::Manager& inheritedFiles = filesystem::GetManager();
            if (inheritedFiles.GetEngineRoot() != filesystemConfig.engineRoot ||
                inheritedFiles.GetGameRoot() != filesystemConfig.gameRoot ||
                inheritedFiles.GetCacheDirectory() != filesystemConfig.cacheRoot)
                return rollback(ProjectAssetContextResult::InvalidState, ProjectAssetContextStage::Filesystem);
        }

        if (!jobs::IsInitialized())
        {
            if (!jobs::Initialize(jobs::ToolConfig()))
                return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::Jobs);
            implementation->ownsJobs = true;
        }

        if (!texture_tools::Acquire())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::TextureTools);
        implementation->ownsTextureTools = true;
        if (!mesh_tools::Acquire())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::MeshTools);
        implementation->ownsMeshTools = true;

        containers::DynamicArray<u8> surfacePrefix(memory::pools::Tools::GetInstance());
        containers::DynamicArray<u8> surfaceSuffix(memory::pools::Tools::GetInstance());
        containers::DynamicArray<u8> gpuSceneTypes(memory::pools::Tools::GetInstance());
        const filesystem::AbsolutePath shaderDirectory = filesystem::paths::GetExecutableDirectory().AddDirPath("shaders");
        if (!ReadFile(shaderDirectory.AddFilePath("static_surface_prefix.vsl"), surfacePrefix) ||
            !ReadFile(shaderDirectory.AddFilePath("static_surface_suffix.vsl"), surfaceSuffix) ||
            !ReadFile(shaderDirectory.AddFilePath("gpu_scene_types.hlsli"), gpuSceneTypes))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::MaterialContext);

        material_tools::ImportedSurfaceMaterialContextDescription materialContextDescription;
        materialContextDescription.prefix = {surfacePrefix.TypedData(), surfacePrefix.Size()};
        materialContextDescription.suffix = {surfaceSuffix.TypedData(), surfaceSuffix.Size()};
        materialContextDescription.gpuSceneTypes = {gpuSceneTypes.TypedData(), gpuSceneTypes.Size()};
        materialContextDescription.target = implementation->config.build.target;
        materialContextDescription.compileSettings.target =
            implementation->config.build.target == assets::TargetPlatform::WindowsD3D12
                ? shader_tools::Target::D3D12Dxil
                : shader_tools::Target::VulkanSpirV;
        if (!implementation->materialContext.Initialize(materialContextDescription))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::MaterialContext);
        material_tools::MaterialArtifactCompilerConfig materialCompilerConfig;
        if (!implementation->materialContext.ConfigureArtifactCompiler(materialCompilerConfig) ||
            !implementation->materialCompilers.Initialize(materialCompilerConfig))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::MaterialCompiler);

        if (!implementation->textureCompiler.Initialize())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::TextureCompiler);

        implementation->modelAdapter.sources = &implementation->sources;
        implementation->modelAdapter.materialContext = &implementation->materialContext;
        implementation->modelAdapter.generated = &implementation->generated;
        mesh_tools::ModelAssetCompilerConfig modelCompilerConfig;
        modelCompilerConfig.outputTypes.mesh = meshes::MeshResourceType;
        modelCompilerConfig.outputTypes.material = materials::MaterialResourceType;
        modelCompilerConfig.outputTypes.texture = textures::TextureResourceType;
        modelCompilerConfig.outputTypes.composition = prefabs::PrefabResourceType;
        modelCompilerConfig.composeMaterial = ComposeMaterial;
        modelCompilerConfig.composeMaterialUserData = &implementation->modelAdapter;
        modelCompilerConfig.composeEmbeddedTexture = ComposeEmbeddedTexture;
        modelCompilerConfig.composeEmbeddedTextureUserData = &implementation->modelAdapter;
        modelCompilerConfig.composeComposition = ComposeComposition;
        if (!implementation->modelCompiler.Initialize(modelCompilerConfig))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::ModelCompiler);

        filesystem::Manager& files = filesystem::GetManager();
        const assets::SourceRoot sourceRoot{workspace.assets, false};
        const assets::SourceDatabaseResult sourceResult =
            implementation->sources.Rescan(files, containers::ArraySpan<const assets::SourceRoot>(&sourceRoot, 1u));
        if (sourceResult != assets::SourceDatabaseResult::Success)
        {
            if (failure != nullptr)
                failure->sources = sourceResult;
            return rollback(ProjectAssetContextResult::SourceScanFailed, ProjectAssetContextStage::SourceScan);
        }

        const filesystem::AbsolutePath derivedOwner = workspace.derivedData.AddDirPath("ProjectAssets");
        const filesystem::AbsolutePath intermediateOwner = workspace.intermediate.AddDirPath("ProjectAssets");
        if (!files.CreatePath(derivedOwner) || !files.CreatePath(intermediateOwner))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::ProjectDirectories);

        if (!implementation->registry.Initialize())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::ResourceRegistry);
        if (!implementation->resourcePipeline.Initialize(implementation->registry))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::ResourcePipeline);
        if (!implementation->streamer.Initialize(implementation->resourcePipeline))
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::ResourceStreamer);

        implementation->config.build.resolveGeneratedFallback = ResolveGeneratedFallback;
        implementation->config.build.resolveGeneratedFallbackUserData = &implementation->generated;

        const assets::CompilerDescriptor compilers[]{
            implementation->textureCompiler.GetDescriptor(),
            implementation->modelCompiler.GetMeshDescriptor(),
            PrefabCompilerDescriptor(),
            implementation->materialCompilers.ProgramDescriptor(),
            implementation->materialCompilers.PipelineDescriptor(),
            implementation->materialCompilers.MaterialDescriptor()};
        const assets::SourceImporterDescriptor importers[]{
            implementation->textureCompiler.GetImporterDescriptor(),
            implementation->modelCompiler.GetImporterDescriptor()};
        asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
        const asset_streaming::ProjectAssetPipelineResult pipelineResult = implementation->pipeline.Initialize(
            files, implementation->sources, implementation->streamer,
            {compilers, static_cast<u32>(sizeof(compilers) / sizeof(compilers[0]))},
            {importers, static_cast<u32>(sizeof(importers) / sizeof(importers[0]))},
            implementation->config, &implementation->startupRestore, &pipelineFailure);
        implementation->startupPipelineResult = pipelineResult;
        const bool committedStartupPending = pipelineResult == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                                             pipelineResult == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        if (pipelineResult != asset_streaming::ProjectAssetPipelineResult::Success && !committedStartupPending)
        {
            if (failure != nullptr)
            {
                failure->pipeline = pipelineResult;
                failure->pipelineFailure = pipelineFailure;
            }
            // A non-Success Initialize can still leave the pipeline live. Close
            // tests IsInitialized() and never releases callbacks/lower owners
            // before a successful project-pipeline shutdown.
            return rollback(ProjectAssetContextResult::PipelineInitializationFailed, ProjectAssetContextStage::ProjectPipeline);
        }

        implementation->open = true;
        return ProjectAssetContextResult::Success;
    }

    ProjectAssetContextResult ProjectAssetContext::Close() noexcept
    {
        if (m_impl == nullptr)
            return ProjectAssetContextResult::Success;

        Impl* const implementation = m_impl;
        if (implementation->retainedAfterUnsafeShutdown)
            return ProjectAssetContextResult::ShutdownFailed;
        if (implementation->pipeline.IsInitialized() &&
            implementation->pipeline.Shutdown() != asset_streaming::ProjectAssetPipelineResult::Success)
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->modelCompiler.IsInitialized() && !implementation->modelCompiler.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->materialCompilers.IsInitialized() && !implementation->materialCompilers.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        implementation->materialContext.Reset();
        implementation->generated.Clear();
        if (implementation->textureCompiler.IsInitialized() && !implementation->textureCompiler.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->streamer.IsInitialized() && !implementation->streamer.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->resourcePipeline.IsInitialized() && !implementation->resourcePipeline.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->registry.IsInitialized() && !implementation->registry.Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        if (implementation->ownsMeshTools && !mesh_tools::Release())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        implementation->ownsMeshTools = false;
        if (implementation->ownsTextureTools && !texture_tools::Release())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }
        implementation->ownsTextureTools = false;
        if (implementation->ownsJobs && jobs::IsInitialized() && !jobs::Shutdown())
        {
            implementation->retainedAfterUnsafeShutdown = true;
            return ProjectAssetContextResult::ShutdownFailed;
        }

        if (implementation->ownsFilesystem && filesystem::IsInitialized())
            filesystem::Shutdown();
        if (implementation->ownsIo && io::IsInitialized())
            io::Shutdown();
        if (implementation->ownsDiagnostics && diagnostics::IsInitialized())
            diagnostics::Shutdown();

        implementation->open = false;
        VANGUARD_DELETE(implementation);
        m_impl = nullptr;
        return ProjectAssetContextResult::Success;
    }

    bool ProjectAssetContext::IsOpen() const noexcept
    {
        return m_impl != nullptr && m_impl->open && m_impl->pipeline.IsInitialized();
    }

    const asset_streaming::ProjectAssetPipelineConfig* ProjectAssetContext::GetConfig() const noexcept
    {
        return m_impl != nullptr ? &m_impl->config : nullptr;
    }

    asset_streaming::ProjectAssetPipeline* ProjectAssetContext::GetPipeline() noexcept
    {
        return m_impl != nullptr ? &m_impl->pipeline : nullptr;
    }

    assets::SourceDatabase* ProjectAssetContext::GetSources() noexcept
    {
        return m_impl != nullptr ? &m_impl->sources : nullptr;
    }

    asset_streaming::ProjectAssetPipelineResult ProjectAssetContext::CommitModelImportPlan(
        const mesh_tools::ModelImportPlan& plan, const containers::ArraySpan<const u8> sourceSnapshot,
        assets::AssetId& importedAsset, asset_streaming::ProjectAssetRestoreReport* const report,
        asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
    {
        importedAsset = {};
        if (!IsOpen() || plan.HasBlockingConflicts() || sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr)
        {
            if (failure != nullptr)
                failure->result = !IsOpen() ? asset_streaming::ProjectAssetPipelineResult::InvalidState
                                            : asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            return !IsOpen() ? asset_streaming::ProjectAssetPipelineResult::InvalidState
                             : asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
        }

        containers::DynamicArray<assets::PlannedAssetDependencyWitness> dependencies(memory::pools::Tools::GetInstance());
        assets::PlannedAssetImportRequest request;
        if (!mesh_tools::BuildModelPlannedImportRequest(plan, sourceSnapshot, dependencies, request))
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            return asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
        }
        return m_impl->pipeline.CommitPlannedImport(request, importedAsset, report, failure);
    }

    asset_streaming::ProjectAssetPipelineResult ProjectAssetContext::RefreshModelFromSource(
        const assets::AssetId asset, mesh_tools::ModelImportPlanReport* const planReport,
        asset_streaming::ProjectAssetRestoreReport* const restore,
        asset_streaming::ProjectAssetPipelineFailure* const failure) noexcept
    {
        if (planReport != nullptr)
            *planReport = {};
        if (restore != nullptr)
            *restore = {};
        if (failure != nullptr)
            *failure = {};
        if (!IsOpen() || !asset.IsValid())
            return failure != nullptr ? (failure->result = !IsOpen() ? asset_streaming::ProjectAssetPipelineResult::InvalidState
                                                                     : asset_streaming::ProjectAssetPipelineResult::InvalidArgument)
                                      : (!IsOpen() ? asset_streaming::ProjectAssetPipelineResult::InvalidState
                                                   : asset_streaming::ProjectAssetPipelineResult::InvalidArgument);

        // Refresh planning must start from authoritative current source/reference
        // evidence. The source edit is first accepted with the existing persisted
        // metadata; the subsequent immutable plan witnesses that exact generation.
        asset_streaming::ProjectAssetRestoreReport sourceRefresh;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources.GetRoots();
        const asset_streaming::ProjectAssetPipelineResult rescanned =
            m_impl->pipeline.Rescan(roots, &sourceRefresh, nullptr, failure);
        if (rescanned != asset_streaming::ProjectAssetPipelineResult::Success)
        {
            if (restore != nullptr)
                *restore = sourceRefresh;
            return rescanned;
        }

        const assets::SourceRecord* const source = m_impl->sources.LookupAsset(asset);
        const asset_streaming::ProjectAssetState* const accepted = m_impl->pipeline.LookupAcceptedAsset(asset);
        const assets::SourceImporterDescriptor modelImporter = m_impl->modelCompiler.GetImporterDescriptor();
        if (source == nullptr || accepted == nullptr || accepted->presence != asset_streaming::ProjectAssetPresence::Present ||
            source->metadataResult != assets::MetadataResult::Success ||
            source->importState != assets::SourceImportState::MetadataSelected ||
            source->importer != modelImporter.id || source->metadata.importer != modelImporter.id ||
            source->metadata.importerVersion != modelImporter.version ||
            accepted->physicalPath != source->path || accepted->sourceIdentity != source->sourceIdentity ||
            accepted->sourceRoot.Empty())
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            return asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
        }

        assets::ProjectSourceData sourceData;
        const assets::ProjectSourceDataResult loaded =
            assets::LoadProjectSourceData(filesystem::GetManager(), *source, sourceData, m_impl->config.build.sourceData);
        if (loaded != assets::ProjectSourceDataResult::Success)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::StalePlan;
            return asset_streaming::ProjectAssetPipelineResult::StalePlan;
        }
        const assets::ProjectSourceDataView sourceView = sourceData.View(m_impl->config.build.target);

        mesh_tools::ModelImportSettings settings;
        if (source->metadata.settingsVersion != mesh_tools::ModelImportSettingsVersion ||
            mesh_tools::DecodeModelImportSettings(source->metadata.settingsVersion,
                                                  {source->metadata.settings.TypedData(), source->metadata.settings.Size()},
                                                  settings) != mesh_tools::ModelImportSettingsResult::Success)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            return asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
        }

        const containers::StringView extension = filesystem::paths::GetExtension(source->path.AsStringView());
        mesh_tools::ModelImportManifest manifest;
        mesh_tools::ModelImportReport inspectReport;
        if (mesh_tools::InspectModelMemory(sourceView.content.Data(), sourceView.content.Count(), extension,
                                           source->path.AsStringView(), {}, {}, manifest, &inspectReport) !=
            mesh_tools::ModelImportResult::Success)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::OperationFailed;
            return asset_streaming::ProjectAssetPipelineResult::OperationFailed;
        }

        mesh_tools::ModelImportPlanningContext planning;
        planning.database = &m_impl->sources;
        planning.persistedMetadata = &source->metadata;
        planning.parentAsset = asset;
        planning.importer = source->metadata.importer;
        planning.importerVersion = source->metadata.importerVersion;
        planning.acceptedStateGeneration = m_impl->pipeline.GetAcceptedAssetGeneration();
        planning.sourceOrigin = source->path;
        planning.sourceRoot = accepted->sourceRoot;
        planning.sourceDestination = source->path;
        planning.sourceIdentity = source->sourceIdentity;
        planning.outputTypes.mesh = meshes::MeshResourceType;
        planning.outputTypes.material = materials::MaterialResourceType;
        planning.outputTypes.texture = textures::TextureResourceType;
        planning.outputTypes.composition = prefabs::PrefabResourceType;
        if (!MetadataFingerprint(source->metadata, planning.persistedMetadataDigest))
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            return asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
        }
        planning.hasPersistedMetadataDigest = true;

        containers::DynamicArray<mesh_tools::ModelImportDependencyWitness> dependencyWitnesses(memory::pools::Tools::GetInstance());
        if (!BuildModelDependencyWitnesses(manifest, settings, m_impl->sources, m_impl->pipeline,
                                           planning.limits.maximumDependencyWitnesses, dependencyWitnesses))
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::StalePlan;
            return asset_streaming::ProjectAssetPipelineResult::StalePlan;
        }
        planning.dependencyWitnesses = {dependencyWitnesses.TypedData(), dependencyWitnesses.Size()};

        mesh_tools::ModelImportPlan plan;
        const mesh_tools::ModelImportPlanResult planned =
            mesh_tools::PlanModelImport(manifest, settings, planning, plan, planReport);
        if (planned != mesh_tools::ModelImportPlanResult::Success || plan.HasBlockingConflicts())
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::OperationFailed;
            return asset_streaming::ProjectAssetPipelineResult::OperationFailed;
        }

        assets::AssetId committedAsset;
        asset_streaming::ProjectAssetRestoreReport committedRestore;
        const asset_streaming::ProjectAssetPipelineResult committed =
            CommitModelImportPlan(plan, sourceView.content, committedAsset, &committedRestore, failure);
        if (restore != nullptr)
            *restore = committedRestore;
        if (committed != asset_streaming::ProjectAssetPipelineResult::Success &&
            committed != asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending)
            return committed;
        if (committedAsset != asset)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::ReconciliationFailed;
            return asset_streaming::ProjectAssetPipelineResult::ReconciliationFailed;
        }

        const asset_streaming::ProjectAssetState* const refreshed = m_impl->pipeline.LookupAcceptedAsset(asset);
        if (refreshed == nullptr || refreshed->presence != asset_streaming::ProjectAssetPresence::Present ||
            refreshed->outputs.Empty() || refreshed->acceptedGeneration == 0u)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            return asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        containers::DynamicArray<resources::ResourceReference> cohort(memory::pools::Tools::GetInstance());
        cohort.Reserve(refreshed->outputs.Count());
        if (cohort.Capacity() < refreshed->outputs.Count())
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            return asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        for (const asset_streaming::ProjectAssetOutputState& output : refreshed->outputs)
            cohort.PushBack(output.resource);
        if (cohort.Size() != refreshed->outputs.Count())
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            return asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        const asset_streaming::ProjectAssetCohortDeliveryRequest cohortRequest{
            asset, refreshed->acceptedGeneration, {cohort.TypedData(), cohort.Size()}};
        const asset_streaming::ProjectAssetPipelineResult admitted =
            m_impl->pipeline.RequestAssetCohortDelivery(cohortRequest, failure);
        if (admitted != asset_streaming::ProjectAssetPipelineResult::Success)
        {
            if (failure != nullptr)
                failure->result = asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            return asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        return committed;
    }

    asset_streaming::ProjectAssetPipelineResult ProjectAssetContext::GetStartupPipelineResult() const noexcept
    {
        return m_impl != nullptr ? m_impl->startupPipelineResult : asset_streaming::ProjectAssetPipelineResult::InvalidState;
    }

    const asset_streaming::ProjectAssetRestoreReport* ProjectAssetContext::GetStartupRestoreReport() const noexcept
    {
        return m_impl != nullptr ? &m_impl->startupRestore : nullptr;
    }
} // namespace vanguard::nanovanguard
