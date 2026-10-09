#pragma once

#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/projects/project_workspace.hpp>

namespace vanguard::nanovanguard
{
    enum class ProjectAssetContextResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InitializationFailed,
        SourceScanFailed,
        PipelineInitializationFailed,
        ShutdownFailed
    };

    enum class ProjectAssetContextStage : u8
    {
        None,
        Validation,
        Diagnostics,
        Io,
        Reflection,
        Filesystem,
        Jobs,
        TextureTools,
        TextureCompiler,
        SourceScan,
        ProjectDirectories,
        ResourceRegistry,
        ResourcePipeline,
        ResourceStreamer,
        ProjectPipeline,
        Shutdown
    };

    struct ProjectAssetContextFailure
    {
        ProjectAssetContextResult result = ProjectAssetContextResult::Success;
        ProjectAssetContextStage stage = ProjectAssetContextStage::None;
        assets::SourceDatabaseResult sources = assets::SourceDatabaseResult::Success;
        asset_streaming::ProjectAssetPipelineResult pipeline = asset_streaming::ProjectAssetPipelineResult::Success;
        asset_streaming::ProjectAssetPipelineFailure pipelineFailure;
    };

    // Private command-scoped composition of the existing GPU-free project-asset
    // owners. Project/document resolution and target selection happen before
    // Open(); this type does not create a second shared backend/session service.
    class ProjectAssetContext final
    {
    public:
        struct Impl;

        ProjectAssetContext() noexcept = default;
        ~ProjectAssetContext();

        ProjectAssetContext(const ProjectAssetContext&) = delete;
        ProjectAssetContext& operator=(const ProjectAssetContext&) = delete;

        [[nodiscard]] ProjectAssetContextResult Open(const projects::ProjectWorkspace& workspace,
                                                     containers::StringView selectedTarget,
                                                     ProjectAssetContextFailure* failure = nullptr) noexcept;
        // On an unsafe refusal from any fallible owner shutdown, Close stops
        // immediately and retains the heap implementation for process lifetime.
        [[nodiscard]] ProjectAssetContextResult Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;

        [[nodiscard]] const asset_streaming::ProjectAssetPipelineConfig* GetConfig() const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipeline* GetPipeline() noexcept;
        [[nodiscard]] assets::SourceDatabase* GetSources() noexcept;
        // Pipeline initialization can cross the accepted-state commit boundary
        // and still return CommittedReconciliationPending. The command frontend
        // needs that exact startup truth for lifecycle-aware status and for
        // fail-closed mutation admission; it is not a second lifecycle owner.
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult GetStartupPipelineResult() const noexcept;
        [[nodiscard]] const asset_streaming::ProjectAssetRestoreReport* GetStartupRestoreReport() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::nanovanguard
