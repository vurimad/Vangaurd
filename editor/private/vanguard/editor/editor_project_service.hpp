#pragma once

#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/asset_streaming/project_asset_catalog.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/textures/textures.hpp>

namespace vanguard::editor
{
    inline constexpr engine::FrameParticipantId ProjectAssetFrameParticipantId = 0x6570726a61737401ull;

    enum class ProjectAssetInitializeFailureOwnerAction : u8
    {
        ReleaseCompilerImmediately,
        PreserveForLifecycleShutdown
    };

    // Private deterministic lifecycle decision used by the production failure
    // branch and its focused regression. A live ProjectAssetPipeline may retain
    // compiler/importer callback userData after a committed startup failure, so
    // those owners must survive until the normal pipeline-first Shutdown stage.
    [[nodiscard]] ProjectAssetInitializeFailureOwnerAction ProjectAssetInitializeFailureOwnerPolicy(
        bool pipelineInitialized) noexcept;

    enum class ProjectResidentRetirementPumpResult : u8
    {
        Success,
        Busy,
        InvalidState,
        RenderingFailure,
        ProjectFailure
    };

    struct ProjectWorkspaceConfig
    {
        filesystem::AbsolutePath projectFile;
        // Authored descriptor already read by composition. The service retains
        // its value; no second disk read can change roots during startup.
        projects::ProjectDescriptor project;
    };

    inline constexpr u32 InvalidProjectTexturePreviewIndex = ~u32{0};
    inline constexpr u32 MaximumProjectTexturePreviewInterests = 512u;
    inline constexpr u32 MaximumProjectTexturePreviewBackings = MaximumProjectTexturePreviewInterests;
    inline constexpr u32 MaximumProjectTexturePreviewProgressingBackings = 32u;
    inline constexpr u32 MaximumProjectTexturePreviewReadyBackings = 64u;
    inline constexpr u32 MaximumProjectTexturePreviewStartsPerTick = 4u;
    inline constexpr u32 MaximumProjectTexturePreviewStateChecksPerTick = 32u;

    enum class ProjectTexturePreviewPriority : u8
    {
        Background,
        Interactive,
        Focused
    };

    struct ProjectTexturePreviewQuality final
    {
        // Final presentation footprint in framebuffer pixels. The workspace
        // converts this to a generation-specific first-resident mip only after
        // exact texture metadata is available.
        u32 displayWidthPixels = 1u;
        u32 displayHeightPixels = 1u;

        [[nodiscard]] friend constexpr bool operator==(const ProjectTexturePreviewQuality&,
                                                       const ProjectTexturePreviewQuality&) noexcept = default;
    };

    struct ProjectTexturePreviewRequest final
    {
        ProjectTexturePreviewPriority priority = ProjectTexturePreviewPriority::Background;
        ProjectTexturePreviewQuality quality;
    };

    class ProjectWorkspaceService;

    namespace detail
    {
        [[nodiscard]] bool IsProjectTexturePreviewAdmissionWithinCumulativeReserves(
            ProjectTexturePreviewPriority priority,
            u32 background,
            u32 backgroundAndInteractive,
            u32 total,
            u32 maximumBackground,
            u32 maximumBackgroundAndInteractive,
            u32 maximumTotal) noexcept;
        [[nodiscard]] bool IsProjectTexturePreviewResidencyBelowHardLimit(u32 held) noexcept;
        [[nodiscard]] bool CanUseProjectTexturePreviewSupersessionResidencySlot(
            u32 held, bool supersessionSlotInUse, bool hasExactPredecessor) noexcept;

        struct EditorProjectAssetTestAccess;
        [[nodiscard]] ProjectWorkspaceService* CreateUninitializedProjectWorkspaceForTest(
            const ProjectWorkspaceConfig& config) noexcept;
        void DestroyUninitializedProjectWorkspaceForTest(ProjectWorkspaceService*& workspace) noexcept;
        [[nodiscard]] u64 GetProjectTexturePreviewOwnerCookieForTest(
            const ProjectWorkspaceService& workspace) noexcept;
    } // namespace detail

    enum class ProjectTexturePreviewState : u8
    {
        Queued,
        LoadingResource,
        WaitingForCurrentGeneration,
        LoadingResidency,
        Ready,
        Failed,
        Retiring
    };

    struct ProjectTexturePreviewHandle final
    {
        u32 index = InvalidProjectTexturePreviewIndex;
        u32 generation = 0u;
        u64 ownerCookie = 0u;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumProjectTexturePreviewInterests && generation != 0u && ownerCookie != 0u;
        }

        [[nodiscard]] friend constexpr bool operator==(const ProjectTexturePreviewHandle&,
                                                       const ProjectTexturePreviewHandle&) noexcept = default;
    };

    struct ProjectTexturePreviewInfo final
    {
        ProjectTexturePreviewState state = ProjectTexturePreviewState::Queued;
        resources::ResourceReference resource;
        u32 resourceGeneration = 0u;
        u32 width = 0u;
        u32 height = 0u;
        textures::TextureDimension dimension = textures::TextureDimension::Texture2D;
        u16 arrayLayers = 0u;
        bool srgb = false;
        bool qualitySatisfied = false;
        u64 presentationRevision = 0u;
        assets::BuildFingerprint buildFingerprint;
        assets::BuildFingerprint contentFingerprint;
    };

    struct EditorProjectAssetSettingsSnapshot final
    {
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 authoredImporterVersion = 0;
        u32 availableImporterVersion = 0;
        u32 settingsVersion = 0;
        u32 settingsBytes = 0;
        u64 sourceGeneration = 0;
        u64 acceptedGeneration = 0;
    };

    struct EditorProjectAssetRootPresentation final
    {
        u32 root = 0u;
        bool readOnly = true;
        char displayName[64]{};
    };

    enum class EditorProjectExternalImportFailureCode : u8
    {
        None,
        InvalidArgument,
        ReadOnlyDestination,
        StaleDestination,
        ManagedProjectSource,
        InputUnavailable,
        InputReadFailure,
        LimitExceeded,
        PipelineRejected
    };

    struct EditorProjectExternalImportFailure final
    {
        EditorProjectExternalImportFailureCode code = EditorProjectExternalImportFailureCode::None;
        asset_streaming::ProjectAssetPipelineFailure pipeline;
    };

    class EditorProjectAssetAccess final
    {
    public:
        EditorProjectAssetAccess() noexcept = default;

        [[nodiscard]] const asset_streaming::ProjectAssetCatalog* GetCatalog() const noexcept;
        [[nodiscard]] u64 GetAcceptedGeneration() const noexcept;
        [[nodiscard]] bool IsSourceGenerationCurrent(u64 generation) const noexcept;
        [[nodiscard]] bool GetAcceptedAssetGeneration(assets::AssetId asset, u64& generation) const noexcept;
        [[nodiscard]] bool GetOutputDeliveryStatus(resources::ResourceReference output,
                                                   asset_streaming::ProjectAssetOutputDeliveryStatus& status) const noexcept;
        [[nodiscard]] bool GetOutputPublicationStatus(resources::ResourceReference output,
                                                      asset_streaming::ProjectAssetOutputPublicationStatus& status) const noexcept;
        u32 CopyAssetOutputs(assets::AssetId asset, u32 offset,
                             containers::ArraySpan<resources::ResourceReference> output,
                             u32& available, bool& truncated) const noexcept;
        [[nodiscard]] bool ResolveUniqueOutputOfType(assets::AssetId asset,
                                                     resources::ResourceTypeId type,
                                                     resources::ResourceReference& output,
                                                     bool& ambiguous) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult QueryBuildFreshness(
            resources::ResourceReference output, asset_streaming::ProjectAssetBuildFreshness& freshness,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        u32 CopyForwardReferences(assets::AssetId source, u32 offset,
                                  containers::ArraySpan<assets::AssetReference> output,
                                  u32& available, bool& truncated) const noexcept;
        u32 CopyBackwardReferences(resources::ResourceReference target, u32 offset,
                                   containers::ArraySpan<assets::AssetReference> output,
                                   u32& available, bool& truncated) const noexcept;
        [[nodiscard]] bool CanMutateAssets() const noexcept;
        [[nodiscard]] u32 GetSourceRootCount() const noexcept;
        [[nodiscard]] bool GetSourceRootPresentation(u32 root,
                                                     EditorProjectAssetRootPresentation& presentation) const noexcept;
        [[nodiscard]] bool CanImportExternalSource(u32 root,
                                                   containers::StringView rootRelativeFolder) const noexcept;
        [[nodiscard]] u64 GetMaximumExternalImportSourceBytes() const noexcept;
        [[nodiscard]] bool IsManagedProjectSourcePath(const filesystem::AbsolutePath& sourcePath) const noexcept;
        // Frontend coordination facade for one external physical source. The
        // original external path never reaches this durable mutation seam:
        // editor coordination owns the complete bounded byte snapshot and only
        // passes the basename needed to construct the managed destination.
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult ImportExternalSource(
            u32 root, containers::StringView rootRelativeFolder,
            containers::StringView sourceFileName, containers::ArraySpan<const u8> sourceBytes,
            assets::AssetId& importedAsset,
            EditorProjectExternalImportFailure* failure = nullptr) const noexcept;
        // Resolves current project-authored source truth for non-mutating shell
        // actions such as Open Source Location. The generation fence prevents a
        // stale browser selection from revealing a replaced/relocated source.
        [[nodiscard]] bool ResolveSourcePhysicalPath(resources::ResourcePath sourceIdentity,
                                                     u64 expectedSourceGeneration,
                                                     filesystem::AbsolutePath& sourcePath) const noexcept;
        [[nodiscard]] bool CanImportCandidate(resources::ResourcePath sourceIdentity,
                                              assets::SourceImporterId importer, u32 importerVersion,
                                              u64 sourceGeneration) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult ImportCandidate(
            resources::ResourcePath sourceIdentity, assets::SourceImporterId importer, u32 importerVersion,
            u64 sourceGeneration, assets::AssetId& importedAsset,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CopyAssetSettings(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            containers::ArraySpan<u8> settings, EditorProjectAssetSettingsSnapshot& snapshot) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult UpdateAssetSettings(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            u32 settingsVersion, containers::ArraySpan<const u8> settings,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult RequestAssetReimport(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult MoveAsset(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            containers::StringView rootRelativeTarget,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DuplicateAsset(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            containers::StringView rootRelativeTarget, assets::AssetId& duplicatedAsset,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DeleteAsset(
            assets::AssetId asset, u64 expectedAcceptedGeneration, u64 expectedSourceGeneration,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CanUndoAssetOperation() const noexcept;
        [[nodiscard]] bool CanRedoAssetOperation() const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult UndoAssetOperation(
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult RedoAssetOperation(
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) const noexcept;
        // Main-thread project-texture preview interest only. Caller handles are
        // independent logical interests; exact published work is shared by the
        // workspace and advanced from its frame participant. Query is observation
        // only and never drives resource/residency work from a panel/widget.
        [[nodiscard]] bool RequestTexturePreview(resources::ResourceReference output,
                                                 const ProjectTexturePreviewRequest& request,
                                                 ProjectTexturePreviewHandle& handle) const noexcept;
        [[nodiscard]] bool RequestTexturePreview(resources::ResourceReference output,
                                                 ProjectTexturePreviewPriority priority,
                                                 ProjectTexturePreviewHandle& handle) const noexcept;
        [[nodiscard]] bool UpdateTexturePreview(ProjectTexturePreviewHandle handle,
                                                const ProjectTexturePreviewRequest& request) const noexcept;
        [[nodiscard]] bool UpdateTexturePreviewPriority(ProjectTexturePreviewHandle handle,
                                                        ProjectTexturePreviewPriority priority) const noexcept;
        [[nodiscard]] bool QueryTexturePreview(ProjectTexturePreviewHandle handle,
                                               ProjectTexturePreviewInfo& info) const noexcept;
        // If a UI texture sampled this Ready generation, unregister that UI
        // texture first and pass the capturing UI frame. The logical handle is
        // released immediately; exact backend ownership retires after a later
        // UI frame. Zero means this interest never published a UI binding.
        [[nodiscard]] bool ReleaseTexturePreview(ProjectTexturePreviewHandle& handle,
                                                 u64 capturedUiFrame = 0u) const noexcept;

    private:
        explicit EditorProjectAssetAccess(ProjectWorkspaceService& workspace) noexcept : m_workspace(&workspace) {}
        ProjectWorkspaceService* m_workspace = nullptr;

        friend struct EditorExtensionActivationContext;
        friend struct detail::EditorProjectAssetTestAccess;
    };

    // Presentation-only bridge for a Ready project-texture preview. The asset
    // facade remains evidence-only: raw RHI texture ownership never crosses it.
    // Callers own sampler/color policy and the returned EditorTextureHandle.
    class EditorProjectTexturePreviewPresentationAccess final
    {
    public:
        EditorProjectTexturePreviewPresentationAccess() noexcept = default;

        [[nodiscard]] bool Register(ProjectTexturePreviewHandle handle,
                                    EditorUiService& ui,
                                    rhi::SamplerStateRef sampler,
                                    EditorTextureColorSpace colorSpace,
                                    EditorTextureHandle& texture) const noexcept;

    private:
        explicit EditorProjectTexturePreviewPresentationAccess(ProjectWorkspaceService& workspace) noexcept
            : m_workspace(&workspace)
        {
        }
        ProjectWorkspaceService* m_workspace = nullptr;

        friend struct EditorExtensionActivationContext;
        friend struct detail::EditorProjectAssetTestAccess;
    };

    /// Owns the validated project document and every root derived from it for the lifetime of an editor workspace.
    /// Engine services remain project-agnostic and consume only the roots or resource identities they actually need.
    class ProjectWorkspaceService : public application::Service
    {
    public:
        ~ProjectWorkspaceService() override = default;

        [[nodiscard]] virtual const projects::ProjectDescriptor& GetProject() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetProjectFile() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetProjectRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetAssetsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetDerivedDataRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetIntermediateRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetSavedRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetBuildsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetConfigRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetPluginsRoot() const noexcept = 0;
        [[nodiscard]] virtual const assets::SourceDatabase& GetSources() const noexcept = 0;
        [[nodiscard]] virtual const asset_streaming::ProjectAssetCatalog& GetAssetCatalog() const noexcept = 0;
        [[nodiscard]] virtual asset_streaming::ProjectAssetPipeline* GetAssetPipeline() noexcept { return nullptr; }
        [[nodiscard]] virtual const asset_streaming::ProjectAssetPipeline* GetAssetPipeline() const noexcept { return nullptr; }

    private:
        [[nodiscard]] virtual bool RequestTexturePreview(resources::ResourceReference output,
                                                         const ProjectTexturePreviewRequest& request,
                                                         ProjectTexturePreviewHandle& handle) noexcept
        {
            return RequestTexturePreview(output, request.priority, handle);
        }
        [[nodiscard]] virtual bool RequestTexturePreview(resources::ResourceReference,
                                                         ProjectTexturePreviewPriority,
                                                         ProjectTexturePreviewHandle& handle) noexcept
        {
            handle = {};
            return false;
        }
        [[nodiscard]] virtual bool UpdateTexturePreview(ProjectTexturePreviewHandle,
                                                        const ProjectTexturePreviewRequest&) noexcept
        {
            return false;
        }
        [[nodiscard]] virtual bool UpdateTexturePreviewPriority(ProjectTexturePreviewHandle,
                                                                ProjectTexturePreviewPriority) noexcept
        {
            return false;
        }
        [[nodiscard]] virtual bool QueryTexturePreview(ProjectTexturePreviewHandle,
                                                       ProjectTexturePreviewInfo& info) noexcept
        {
            info = {};
            return false;
        }
        [[nodiscard]] virtual bool RegisterTexturePreview(ProjectTexturePreviewHandle,
                                                          EditorUiService&,
                                                          rhi::SamplerStateRef,
                                                          EditorTextureColorSpace,
                                                          EditorTextureHandle& texture) noexcept
        {
            texture = {};
            return false;
        }
        [[nodiscard]] virtual bool ReleaseTexturePreview(ProjectTexturePreviewHandle&, u64) noexcept
        {
            return false;
        }

        friend class EditorProjectAssetAccess;
        friend class EditorProjectTexturePreviewPresentationAccess;

    public:
        // Bounded owner-thread bridge for exact renderer generation-retirement
        // evidence. Busy leaves the renderer evidence unacknowledged for retry.
        [[nodiscard]] virtual ProjectResidentRetirementPumpResult PumpResidentRetirements() noexcept
        {
            return ProjectResidentRetirementPumpResult::InvalidState;
        }
        // Owner-thread calls only, after all borrowed catalog readers finish.
        [[nodiscard]] virtual assets::SourceDatabaseResult RescanSources(filesystem::ScanResult* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ClassifySources(containers::ArraySpan<const assets::SourceImporterDescriptor> importers) noexcept = 0;
        [[nodiscard]] virtual assets::SourceDatabaseResult ScanSourceReferences(containers::ArraySpan<const assets::SourceImporterDescriptor> importers,
                                                                                assets::TargetPlatform target) noexcept = 0;

    protected:
        ProjectWorkspaceService() noexcept = default;
    };

    [[nodiscard]] bool RegisterProjectWorkspaceService(application::EngineHost& host, const ProjectWorkspaceConfig& config,
                                                       application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::EngineHost& host) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
