#include <vanguard/nanovanguard/project_asset_context.hpp>

#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>

namespace vanguard::nanovanguard
{
    namespace
    {
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
    } // namespace

    struct ProjectAssetContext::Impl final
    {
        projects::ProjectWorkspace workspace;
        asset_streaming::ProjectAssetPipelineConfig config;
        texture_tools::TextureAssetCompiler compiler;
        assets::SourceDatabase sources;
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

        if (!texture_tools::IsInitialized() && !texture_tools::Initialize())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::TextureTools);
        if (!implementation->compiler.Initialize())
            return rollback(ProjectAssetContextResult::InitializationFailed, ProjectAssetContextStage::TextureCompiler);

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

        const assets::CompilerDescriptor compiler = implementation->compiler.GetDescriptor();
        const assets::SourceImporterDescriptor importer = implementation->compiler.GetImporterDescriptor();
        asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
        const asset_streaming::ProjectAssetPipelineResult pipelineResult = implementation->pipeline.Initialize(
            files, implementation->sources, implementation->streamer, {&compiler, 1u}, {&importer, 1u},
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
        if (implementation->compiler.IsInitialized() && !implementation->compiler.Shutdown())
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

    asset_streaming::ProjectAssetPipelineResult ProjectAssetContext::GetStartupPipelineResult() const noexcept
    {
        return m_impl != nullptr ? m_impl->startupPipelineResult : asset_streaming::ProjectAssetPipelineResult::InvalidState;
    }

    const asset_streaming::ProjectAssetRestoreReport* ProjectAssetContext::GetStartupRestoreReport() const noexcept
    {
        return m_impl != nullptr ? &m_impl->startupRestore : nullptr;
    }
} // namespace vanguard::nanovanguard
