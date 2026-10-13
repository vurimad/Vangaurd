#pragma once

#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/projects/project_workspace.hpp>

namespace vanguard::mesh_tools
{
    class ModelImportPlan;
    struct ModelOutputBuildRequestContext;
    struct ModelImportPlanReport;
}

namespace vanguard::assets
{
    struct OwnedBuildRequest;
}

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
        MeshTools,
        TextureCompiler,
        MaterialContext,
        MaterialCompiler,
        ModelCompiler,
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

    // Private tool seam shared by the production model compiler and focused integration proofs.
    // It composes the exact transient VPBI request later consumed by the prefab compiler.
    [[nodiscard]] bool ComposeModelCompositionBuildRequest(const mesh_tools::ModelOutputBuildRequestContext& context,
                                                           assets::OwnedBuildRequest& request) noexcept;

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
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult CommitModelImportPlan(
            const mesh_tools::ModelImportPlan& plan, containers::ArraySpan<const u8> sourceSnapshot,
            assets::AssetId& importedAsset, asset_streaming::ProjectAssetRestoreReport* report = nullptr,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // P5 headless source refresh. This is intentionally distinct from
        // RequestAssetReimport(), which only recooks the already accepted schema.
        // Refresh re-inspects current source bytes, reconciles stable identities,
        // atomically commits the new metadata/output schema, then admits the exact
        // accepted model generation as one publication cohort.
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult RefreshModelFromSource(
            assets::AssetId asset, mesh_tools::ModelImportPlanReport* planReport = nullptr,
            asset_streaming::ProjectAssetRestoreReport* restore = nullptr,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) noexcept;
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
