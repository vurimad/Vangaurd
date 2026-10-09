#include <vanguard/editor/editor_static_extensions.hpp>

#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_asset_browser_persistence.hpp>
#include <vanguard/editor/editor_asset_browser_policy.hpp>
#include <vanguard/editor/editor_asset_browser_tree.hpp>
#include <vanguard/editor/editor_file_dialog.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_ui_imgui.hpp>
#include <vanguard/editor/editor_visual_style.hpp>
#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace assets = vanguard::assets;
    namespace assetStreaming = vanguard::asset_streaming;
    namespace editor = vanguard::editor;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace rhi = vanguard::rhi;
    namespace resources = vanguard::resources;
    namespace textureTools = vanguard::texture_tools;
    namespace textures = vanguard::textures;

    using editor::detail::AssetBrowserCompactGridMaximumThumbnailExtent;
    using editor::detail::AssetBrowserCompactGridMinimumThumbnailExtent;
    using editor::detail::AssetBrowserGridMaximumThumbnailExtent;
    using editor::detail::AssetBrowserGridMinimumThumbnailExtent;
    using editor::detail::AssetBrowserPanelStateVersion;
    using editor::detail::AssetBrowserSelectionIntent;

    inline constexpr editor::EditorExtensionId AssetBrowserExtensionId{0x6534617373627201ull};
    inline constexpr editor::EditorPanelTypeId AssetBrowserPanelTypeId{0x6534617373627202ull};
    inline constexpr editor::EditorPanelInstanceId AssetBrowserPrimaryInstanceId{0x6534617373627203ull};
    inline constexpr editor::EditorPanelInstanceId AssetBrowserSecondaryInstanceId{0x6534617373627204ull};
    inline constexpr editor::EditorCommandContextTypeId AssetBrowserContextTypeId{0x6534617373627205ull};
    inline constexpr editor::EditorCommandId RefreshAssetBrowserCommandId{0x6534617373627206ull};
    inline constexpr editor::EditorCommandId ImportAssetCommandId{0x6534617373627207ull};
    inline constexpr editor::EditorCommandId ReimportAssetCommandId{0x6534617373627208ull};
    inline constexpr editor::EditorCommandId MoveAssetCommandId{0x6534617373627209ull};
    inline constexpr editor::EditorCommandId DuplicateAssetCommandId{0x653461737362720aull};
    inline constexpr editor::EditorCommandId DeleteAssetCommandId{0x653461737362720bull};
    inline constexpr editor::EditorCommandId UndoAssetCommandId{0x653461737362720dull};
    inline constexpr editor::EditorCommandId RedoAssetCommandId{0x653461737362720eull};
    inline constexpr editor::EditorCommandId ApplyTextureSettingsCommandId{0x653461737362720full};
    inline constexpr editor::EditorCommandId OpenSourceLocationCommandId{0x653461737362721cull};
    inline constexpr editor::EditorUiSurfaceId AssetBrowserContextMenuSurfaceId{0x6534617373627210ull};

    inline constexpr editor::EditorUiContributionId ImportAssetContextContributionId{0x6534617373627211ull};
    inline constexpr editor::EditorUiContributionId ReimportAssetContextContributionId{0x6534617373627212ull};
    inline constexpr editor::EditorUiContributionId MoveAssetContextContributionId{0x6534617373627213ull};
    inline constexpr editor::EditorUiContributionId DuplicateAssetContextContributionId{0x6534617373627214ull};
    inline constexpr editor::EditorUiContributionId DeleteAssetContextContributionId{0x6534617373627215ull};
    inline constexpr editor::EditorUiContributionId ApplyTextureSettingsContextContributionId{0x6534617373627217ull};
    inline constexpr editor::EditorUiContributionId UndoAssetMainMenuContributionId{0x6534617373627218ull};
    inline constexpr editor::EditorUiContributionId RedoAssetMainMenuContributionId{0x6534617373627219ull};
    inline constexpr editor::EditorUiContributionId UndoAssetContextContributionId{0x653461737362721aull};
    inline constexpr editor::EditorUiContributionId RedoAssetContextContributionId{0x653461737362721bull};
    inline constexpr editor::EditorUiContributionId OpenSourceLocationContextContributionId{0x653461737362721dull};

    inline constexpr vanguard::u32 AssetBrowserQueryRowsPerFrame = 8192u;
    inline constexpr vanguard::u32 AssetBrowserQueryPathBytesPerFrame = 1024u * 1024u;
    inline constexpr vanguard::u32 AssetBrowserReferencePageSize = 32u;
    inline constexpr vanguard::u32 AssetBrowserThumbnailCapacity = 256u;
    inline constexpr vanguard::u32 AssetBrowserThumbnailCapacityPerPanel = 128u;
    inline constexpr vanguard::u32 AssetBrowserDetachedThumbnailDrainPerFrame = 8u;
    inline constexpr vanguard::u64 AssetBrowserThumbnailRetentionFrames = 8u;
    inline constexpr vanguard::u64 AssetBrowserThumbnailRetryFrames = 8u;
    inline constexpr float AssetBrowserExpandedFolderHeaderAlpha = 0.28f;
    inline constexpr float AssetBrowserGridCardPadding = 8.0f;
    inline constexpr float AssetBrowserGridCardSpacing = 8.0f;
    struct AssetBrowserExtensionState;

    struct AssetBrowserExternalImportCapture final
    {
        vanguard::concurrency::Atomic<vanguard::u32> references{1u};
        vanguard::concurrency::Atomic<vanguard::u32> completed{0u};
        red::SharedPtr<io::IOContext> ioContext;
        io::ShareableIOMemory memory;
        editor::EditorProjectExternalImportFailureCode failure =
            editor::EditorProjectExternalImportFailureCode::None;
        vanguard::u32 byteCount = 0u;
        vanguard::u32 memoryOffset = 0u;
        char fileName[vanguard::resources::MaximumResourcePathBytes + 1u]{};
    };

    enum class AssetBrowserExternalImportItemState : vanguard::u8
    {
        Pending,
        Success,
        Cancelled,
        Unsupported,
        AmbiguousImporter,
        AuthoringUnavailable,
        DestinationConflict,
        ReadOnlyDestination,
        StaleDestination,
        ManagedProjectSource,
        InputUnavailable,
        InputReadFailure,
        LimitExceeded,
        TransactionFailure,
        IdentityGenerationFailure,
        CleanupPending,
        ReconciliationPending,
        Failed
    };

    struct AssetBrowserExternalImportItemResult final
    {
        editor::EditorProjectExternalImportFailure failure;
        assets::AssetId importedAsset;
        AssetBrowserExternalImportItemState state = AssetBrowserExternalImportItemState::Pending;
    };

    enum class AssetBrowserOperationKind : vanguard::u8
    {
        None,
        Import,
        Reimport,
        Move,
        Duplicate,
        Delete,
        Undo,
        Redo,
        ApplyTextureSettings
    };

    enum class AssetBrowserTargetedOperationKind : vanguard::u8
    {
        None,
        Move,
        Duplicate
    };

    struct AssetBrowserOperationStatus final
    {
        assetStreaming::ProjectAssetPipelineFailure failure;
        assets::AssetId resultingAsset;
        AssetBrowserOperationKind kind = AssetBrowserOperationKind::None;
        assetStreaming::ProjectAssetPipelineResult result = assetStreaming::ProjectAssetPipelineResult::InvalidState;
    };

    struct AssetBrowserPanelState final : editor::detail::AssetBrowserBrowsePolicyState
    {
        AssetBrowserPanelState() noexcept
            : tree(vanguard::memory::pools::Tools::GetInstance()),
              importPaths(vanguard::memory::pools::Tools::GetInstance())
        {
            tree.expanded.Reserve(256u);
            tree.visible.Reserve(1024u);
        }

        AssetBrowserExtensionState* extension = nullptr;
        editor::EditorPanelInstanceId instance = editor::InvalidEditorPanelInstanceId;
        vanguard::u64 persistenceRevision = 1u;
        AssetBrowserSelectionIntent selectionIntent = AssetBrowserSelectionIntent::None;
        float treeScrollFraction = 0.0f;
        float contentScrollFractions[editor::detail::AssetBrowserContentViewModeCount]{};
        bool treePersistenceTruncated = false;
        bool treeScrollRestorePending = false;
        bool treeScrollRestoreGeometryReady = false;
        bool contentScrollRestorePending[editor::detail::AssetBrowserContentViewModeCount]{};
        bool contentScrollRestoreGeometryReady[editor::detail::AssetBrowserContentViewModeCount]{};
        editor::detail::AssetBrowserTreeState tree;
        resources::ResourcePath selectedSourceIdentity;
        resources::ResourceReference selectedPrimaryOutput;
        resources::ResourceReference previewOutput;
        assetStreaming::ProjectAssetBuildFreshness selectedFreshness;
        editor::ProjectTexturePreviewHandle previewCandidate;
        editor::ProjectTexturePreviewHandle previewDisplayed;
        editor::ProjectTexturePreviewInfo previewCandidateInfo;
        editor::ProjectTexturePreviewInfo previewDisplayedInfo;
        editor::ProjectTexturePreviewInfo previewUnsupportedInfo;
        editor::ProjectTexturePreviewInfo previewFailedInfo;
        editor::ProjectTexturePreviewQuality previewCandidateQuality;
        editor::ProjectTexturePreviewQuality previewDisplayedQuality;
        editor::EditorTextureHandle previewTexture;
        editor::EditorProjectAssetSettingsSnapshot textureSettingsSnapshot;
        textureTools::TextureBuildDescription textureSettings;
        textureTools::TextureBuildDescription authoredTextureSettings;
        AssetBrowserOperationStatus operation;
        assetStreaming::ProjectAssetPipelineResult selectedFreshnessResult =
            assetStreaming::ProjectAssetPipelineResult::InvalidState;
        vanguard::u64 selectedSourceGeneration = 0u;
        vanguard::u64 selectedAcceptedGeneration = 0u;
        vanguard::u64 previewCapturedUiFrame = 0u;
        vanguard::u64 previewLastRequestUiFrame = 0u;
        vanguard::u64 previewFailedUiFrame = 0u;
        vanguard::u64 previewObservedPresentationRevision = 0u;
        assets::SourceImporterId selectedImporter = assets::InvalidSourceImporterId;
        resources::ResourceTypeId selectedPrimaryOutputType = resources::InvalidResourceTypeId;
        vanguard::u32 selectedImporterVersion = 0u;
        assetStreaming::ProjectAssetCatalogFolderId selectedContentFolder;
        vanguard::u32 selectedContentFolderRoot = 0u;
        vanguard::u32 selectedContentFolderPathBytes = 0u;
        char selectedContentFolderPath[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        char mutationTarget[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        AssetBrowserTargetedOperationKind targetedOperation = AssetBrowserTargetedOperationKind::None;
        char importFolder[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        editor::EditorFileDialogHandle importDialog;
        vanguard::containers::DynamicArray<vanguard::containers::String> importPaths;
        vanguard::containers::String importMessage;
        AssetBrowserExternalImportCapture* importCaptureWork = nullptr;
        AssetBrowserExternalImportItemResult importResults[editor::MaximumEditorFileDialogSelections]{};
        vanguard::u32 importRoot = 0u;
        vanguard::u32 importNext = 0u;
        vanguard::u32 dependencyPageOffset = 0u;
        vanguard::u32 referencerPageOffset = 0u;
        vanguard::u32 outputPageOffset = 0u;
        assets::SourceImportState selectedImportState = assets::SourceImportState::NotClassified;
        bool selectedReadOnly = false;
        bool selectedPrimaryOutputAmbiguous = false;
        bool selectedFreshnessEvaluated = false;
        bool targetedOperationOpenRequested = false;
        bool deleteArmed = false;
        bool deleteOpenRequested = false;
        bool textureSettingsLoaded = false;
        bool textureSettingsSupported = false;
        bool textureSettingsDirty = false;
        bool previewUnsupportedShape = false;
        bool previewFailed = false;
        bool previewRequestAttempted = false;
        bool importBatchActive = false;
        bool importCancelRequested = false;
        bool importCapturePrepared = false;
    };

    [[nodiscard]] editor::EditorCommandState QueryAdoptedMutation(void* receiverData, void* commandData) noexcept;
    void ClearSelection(AssetBrowserPanelState& state, bool markPersistent) noexcept;

    struct AssetBrowserTextureThumbnail final
    {
        AssetBrowserExtensionState* extension = nullptr;
        AssetBrowserPanelState* owner = nullptr;
        assets::AssetId asset;
        resources::ResourceReference output;
        editor::ProjectTexturePreviewHandle candidate;
        editor::ProjectTexturePreviewHandle displayed;
        editor::ProjectTexturePreviewInfo candidateInfo;
        editor::ProjectTexturePreviewInfo displayedInfo;
        editor::ProjectTexturePreviewInfo suppressedInfo;
        editor::ProjectTexturePreviewQuality candidateQuality;
        editor::ProjectTexturePreviewQuality displayedQuality;
        editor::EditorTextureHandle texture;
        vanguard::u64 lastVisibleUiFrame = 0u;
        vanguard::u64 lastRequestUiFrame = 0u;
        vanguard::u64 capturedUiFrame = 0u;
        vanguard::u64 suppressedUiFrame = 0u;
        vanguard::u64 observedPresentationRevision = 0u;
        bool occupied = false;
        bool requestAttempted = false;
        bool publicationSuppressed = false;
        bool suppressedFailure = false;
        bool terminal = false;
    };

    struct AssetBrowserExtensionState final
    {
        AssetBrowserExtensionState() noexcept : droppedPaths(vanguard::memory::pools::Tools::GetInstance()) {}

        editor::EditorProjectAssetAccess assets;
        editor::EditorProjectTexturePreviewPresentationAccess previewPresentation;
        editor::EditorOwnedPanelAccess panels;
        editor::EditorUiService* ui = nullptr;
        editor::EditorFileDialogService* fileDialogs = nullptr;
        editor::EditorFileDropPollResult pendingDrop;
        vanguard::containers::DynamicArray<vanguard::containers::String> droppedPaths;
        vanguard::u64 dropPollFrame = 0u;
        rhi::SamplerState previewSampler;
        AssetBrowserTextureThumbnail thumbnails[AssetBrowserThumbnailCapacity]{};
        vanguard::u32 detachedThumbnailDrainCursor = 0u;
        AssetBrowserPanelState* primaryState = nullptr;
        AssetBrowserPanelState* secondaryState = nullptr;
        editor::EditorPanelHandle primaryPanel;
        editor::EditorPanelHandle secondaryPanel;
    };

    void MarkAssetBrowserPersistentStateDirty(AssetBrowserPanelState& state) noexcept
    {
        if (state.persistenceRevision != ~vanguard::u64{0})
            ++state.persistenceRevision;
    }

    void ResetAssetBrowserContentScrollIntent(AssetBrowserPanelState& state) noexcept
    {
        for (vanguard::u32 index = 0u; index < editor::detail::AssetBrowserContentViewModeCount; ++index)
        {
            state.contentScrollFractions[index] = 0.0f;
            state.contentScrollRestorePending[index] = true;
            state.contentScrollRestoreGeometryReady[index] = false;
        }
    }

    void TrackAssetBrowserScrollIntent(AssetBrowserPanelState& state, float& fraction, bool& restorePending,
                                       bool& restoreGeometryReady, const bool dataReady) noexcept
    {
        if (!dataReady)
        {
            if (restorePending)
                restoreGeometryReady = false;
            return;
        }
        const float maximum = std::max(0.0f, ImGui::GetScrollMaxY());
        if (restorePending)
        {
            // ImGui computes ScrollMax from the previous frame's child ContentSize at BeginChild.
            // Let one complete data-ready frame establish current geometry before consuming the
            // persisted normalized scroll intent on the following frame.
            if (!restoreGeometryReady)
            {
                restoreGeometryReady = true;
                return;
            }
            ImGui::SetScrollY(std::clamp(fraction, 0.0f, 1.0f) * maximum);
            restorePending = false;
            restoreGeometryReady = false;
            return;
        }
        const float current = maximum > 0.0f
                                  ? std::clamp(ImGui::GetScrollY() / maximum, 0.0f, 1.0f)
                                  : 0.0f;
        if (std::fabs(current - fraction) > 0.0005f)
        {
            fraction = current;
            MarkAssetBrowserPersistentStateDirty(state);
        }
    }

    [[nodiscard]] editor::EditorPanelStateResult CaptureAssetBrowserPresentationState(
        const AssetBrowserPanelState& state, void* const destination, const vanguard::u32 capacity,
        vanguard::u32& byteCount) noexcept
    {
        return editor::detail::CaptureAssetBrowserPresentationPersistence(
            state.contentView, destination, capacity, byteCount);
    }

    [[nodiscard]] editor::EditorPanelStateResult CaptureAssetBrowserContentState(
        const AssetBrowserPanelState& state, void* const destination, const vanguard::u32 capacity,
        vanguard::u32& byteCount) noexcept
    {
        editor::detail::AssetBrowserContentPersistenceView view;
        view.root = state.root;
        view.folderPath = vanguard::containers::StringView(state.folderPath);
        view.search = vanguard::containers::StringView(state.search);
        view.importFilter = state.importFilter;
        view.recursive = state.recursive;
        view.selection = state.selectionIntent;
        if (view.selection == AssetBrowserSelectionIntent::Asset && !state.selectedHasAsset)
            view.selection = AssetBrowserSelectionIntent::None;
        if (view.selection == AssetBrowserSelectionIntent::Folder &&
            state.selectedContentFolderPathBytes > vanguard::resources::MaximumResourcePathBytes)
            view.selection = AssetBrowserSelectionIntent::None;
        view.selectedContentFolderRoot = state.selectedContentFolderRoot;
        view.selectedContentFolderPath = vanguard::containers::StringView(
            state.selectedContentFolderPath, state.selectedContentFolderPathBytes);
        view.selectedAsset = state.selectedAsset;
        view.treeScrollFraction = state.treeScrollFraction;
        for (vanguard::u32 index = 0u; index < editor::detail::AssetBrowserContentViewModeCount; ++index)
            view.contentScrollFractions[index] = state.contentScrollFractions[index];
        view.treePersistenceTruncated = state.treePersistenceTruncated;
        view.expansions = vanguard::containers::ArraySpan<const editor::detail::AssetBrowserTreeExpandedLocation>(
            state.tree.expanded.TypedData(), state.tree.expanded.Size());
        return editor::detail::CaptureAssetBrowserContentPersistence(view, destination, capacity, byteCount);
    }

    [[nodiscard]] editor::EditorPanelStateResult CaptureAssetBrowserState(
        const editor::EditorPanelInstanceId instance, void* const panel, const editor::EditorPanelStateScope scope,
        void* const destination, const vanguard::u32 capacity, vanguard::u32& byteCount, void* const factoryData) noexcept
    {
        byteCount = 0u;
        auto* const state = static_cast<AssetBrowserPanelState*>(panel);
        if (state == nullptr || instance != state->instance || state->extension != factoryData || destination == nullptr)
            return editor::EditorPanelStateResult::CallbackFailure;
        switch (scope)
        {
        case editor::EditorPanelStateScope::Presentation:
            return CaptureAssetBrowserPresentationState(*state, destination, capacity, byteCount);
        case editor::EditorPanelStateScope::Content:
            return CaptureAssetBrowserContentState(*state, destination, capacity, byteCount);
        }
        return editor::EditorPanelStateResult::InvalidData;
    }

    [[nodiscard]] editor::EditorPanelStateResult MigrateAssetBrowserState(
        const editor::EditorPanelInstanceId instance, void* const panel, const editor::EditorPanelStateScope scope,
        const vanguard::u32 fromVersion, const vanguard::containers::ArraySpan<const vanguard::u8> source,
        void* const destination, const vanguard::u32 capacity, vanguard::u32& byteCount,
        void* const factoryData) noexcept
    {
        byteCount = 0u;
        const auto* const state = static_cast<const AssetBrowserPanelState*>(panel);
        if (state == nullptr || instance != state->instance || state->extension != factoryData)
            return editor::EditorPanelStateResult::CallbackFailure;
        if (fromVersion != editor::detail::AssetBrowserPanelStateVersionV1)
            return editor::EditorPanelStateResult::UnsupportedVersion;
        return editor::detail::MigrateAssetBrowserPersistenceV1(
            scope, source, destination, capacity, byteCount);
    }

    [[nodiscard]] editor::EditorPanelStateResult RestoreAssetBrowserState(
        const editor::EditorPanelInstanceId instance, void* const panel, const editor::EditorPanelStateScope scope,
        const vanguard::u32 version, const vanguard::containers::ArraySpan<const vanguard::u8> bytes,
        void* const factoryData) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(panel);
        if (state == nullptr || instance != state->instance || state->extension != factoryData)
            return editor::EditorPanelStateResult::CallbackFailure;
        if (version != AssetBrowserPanelStateVersion)
            return editor::EditorPanelStateResult::UnsupportedVersion;

        if (scope == editor::EditorPanelStateScope::Presentation)
        {
            editor::detail::AssetBrowserContentViewState decoded;
            if (!editor::detail::DecodeAssetBrowserPresentationPersistence(bytes, decoded))
                return editor::EditorPanelStateResult::InvalidData;
            state->contentView = decoded;
            MarkAssetBrowserPersistentStateDirty(*state);
            return editor::EditorPanelStateResult::Success;
        }
        if (scope != editor::EditorPanelStateScope::Content)
            return editor::EditorPanelStateResult::InvalidData;

        editor::detail::AssetBrowserDecodedContentPersistenceState decoded;
        if (!editor::detail::DecodeAssetBrowserContentPersistence(bytes, decoded))
            return editor::EditorPanelStateResult::InvalidData;

        // Restore only durable intent. Generation-local catalog handles remain empty and are
        // rebound later by SyncCatalog once a published catalog generation exists.
        state->folder = {};
        state->selectedRow = {};
        state->catalogGeneration = 0u;
        state->root = decoded.root;
        std::memcpy(state->folderPath, decoded.folderPath, decoded.folderPathBytes + 1u);
        std::memcpy(state->search, decoded.search, decoded.searchBytes + 1u);
        state->importFilter = decoded.importFilter;
        state->recursive = decoded.recursive;
        state->queryDirty = true;
        state->selectionIntent = decoded.selection;
        state->selectedHasAsset = false;
        state->selectedAsset = {};
        state->selectedContentFolder = {};
        state->selectedContentFolderRoot = 0u;
        state->selectedContentFolderPathBytes = 0u;
        state->selectedContentFolderPath[0] = '\0';
        if (state->selectionIntent == AssetBrowserSelectionIntent::Folder)
        {
            state->selectedContentFolderRoot = decoded.selectedContentFolderRoot;
            state->selectedContentFolderPathBytes = decoded.selectedContentFolderPathBytes;
            std::memcpy(state->selectedContentFolderPath, decoded.selectedContentFolderPath,
                        decoded.selectedContentFolderPathBytes + 1u);
        }
        else if (state->selectionIntent == AssetBrowserSelectionIntent::Asset)
        {
            state->selectedAsset = decoded.selectedAsset;
            state->selectedHasAsset = true;
        }

        state->tree.expanded.Clear();
        state->treeScrollFraction = decoded.treeScrollFraction;
        state->treeScrollRestorePending = true;
        state->treeScrollRestoreGeometryReady = false;
        for (vanguard::u32 index = 0u; index < editor::detail::AssetBrowserContentViewModeCount; ++index)
        {
            state->contentScrollFractions[index] = decoded.contentScrollFractions[index];
            state->contentScrollRestorePending[index] = true;
            state->contentScrollRestoreGeometryReady[index] = false;
        }
        state->treePersistenceTruncated = decoded.treePersistenceTruncated;
        for (vanguard::u32 index = 0u; index < decoded.expansionCount; ++index)
        {
            const editor::detail::AssetBrowserTreeExpandedLocation& expanded = decoded.expansions[index];
            if (!editor::detail::SetAssetBrowserTreeFolderExpanded(
                    state->tree, expanded.root,
                    vanguard::containers::StringView(expanded.path, expanded.pathBytes), true))
                return editor::EditorPanelStateResult::CapacityExceeded;
        }
        state->tree.visible.Clear();
        state->tree.visibleGeneration = 0u;
        state->tree.dirty = true;
        state->tree.initialized = false;
        state->tree.truncated = false;
        state->tree.expansionLimitReached = false;
        state->tree.scrollToSelection = false;
        state->tree.focusSelection = false;
        MarkAssetBrowserPersistentStateDirty(*state);
        return editor::EditorPanelStateResult::Success;
    }

    [[nodiscard]] vanguard::u64 GetAssetBrowserStateRevision(
        const editor::EditorPanelInstanceId instance, void* const panel, void* const factoryData) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(panel);
        return state != nullptr && state->instance == instance && state->extension == factoryData
                   ? state->persistenceRevision
                   : 0u;
    }

    void ClearExternalImportWork(AssetBrowserPanelState& state, bool releaseDialog) noexcept;

    void ClearPendingExternalDrop(AssetBrowserExtensionState& extension) noexcept
    {
        extension.pendingDrop = {};
        extension.droppedPaths.Clear();
    }

    void PollExternalDropOncePerFrame(AssetBrowserExtensionState& extension, const vanguard::u64 frame) noexcept
    {
        if (extension.fileDialogs == nullptr || frame == 0u || extension.dropPollFrame == frame)
            return;
        extension.dropPollFrame = frame;
        // A native drop is only meaningful on the UI frame whose host/panel
        // geometry can claim it. If no visible Asset Browser claimed the prior
        // frame's batch, discard it rather than stealing a later unrelated drop.
        if (extension.pendingDrop.state != editor::EditorFileDropState::None)
            ClearPendingExternalDrop(extension);
        editor::EditorFileDialogFailure failure;
        if (!extension.fileDialogs->PollFileDrop(extension.pendingDrop, extension.droppedPaths, &failure))
            ClearPendingExternalDrop(extension);
    }

    [[nodiscard]] bool PointInsidePanelContent(const editor::EditorPanelContext& panelContext,
                                               const editor::EditorFileDropPollResult& drop) noexcept
    {
        if (!panelContext.GetHost().IsValid() || !drop.window.IsValid())
            return false;
        editor::EditorHostWindowInfo host;
        if (!panelContext.GetUi().GetHost(panelContext.GetHost(), host) || !(host.window == drop.window))
            return false;
        ImGuiViewport* const viewport = ImGui::GetWindowViewport();
        if (viewport == nullptr)
            return false;
        const ImVec2 minimum = ImGui::GetWindowPos();
        const ImVec2 extent = ImGui::GetWindowSize();
        const float left = minimum.x - viewport->Pos.x;
        const float top = minimum.y - viewport->Pos.y;
        const float right = left + extent.x;
        const float bottom = top + extent.y;
        const bool inside = static_cast<float>(drop.position.x) >= left &&
                            static_cast<float>(drop.position.x) < right &&
                            static_cast<float>(drop.position.y) >= top &&
                            static_cast<float>(drop.position.y) < bottom;
        if (!inside)
            return false;

        // Platform events are drained before UI authoring, so the current mouse
        // may already have moved since DROP_COMPLETE. Resolve the topmost ImGui
        // window at the recorded drop coordinate and only let this content
        // window (or one of its nested children) claim the batch.
        ImGuiWindow* const contentWindow = ImGui::GetCurrentWindow();
        const ImVec2 dropPosition{viewport->Pos.x + static_cast<float>(drop.position.x),
                                  viewport->Pos.y + static_cast<float>(drop.position.y)};
        ImGuiWindow* dropWindow = nullptr;
        ImGuiContext* const imgui = ImGui::GetCurrentContext();
        if (imgui == nullptr)
            return false;
        for (int index = imgui->Windows.Size - 1; index >= 0; --index)
        {
            ImGuiWindow* const candidate = imgui->Windows[index];
            if (!candidate->WasActive || candidate->Hidden ||
                (candidate->Flags & ImGuiWindowFlags_NoMouseInputs) != 0 ||
                candidate->Viewport != viewport ||
                !candidate->OuterRectClipped.Contains(dropPosition))
                continue;
            if (candidate->HitTestHoleSize.x != 0)
            {
                const ImVec2 holeMinimum{
                    candidate->Pos.x + static_cast<float>(candidate->HitTestHoleOffset.x),
                    candidate->Pos.y + static_cast<float>(candidate->HitTestHoleOffset.y)};
                const ImVec2 holeMaximum{
                    candidate->Pos.x + static_cast<float>(candidate->HitTestHoleOffset.x +
                                                          candidate->HitTestHoleSize.x),
                    candidate->Pos.y + static_cast<float>(candidate->HitTestHoleOffset.y +
                                                          candidate->HitTestHoleSize.y)};
                const ImRect hole(holeMinimum, holeMaximum);
                if (hole.Contains(dropPosition))
                    continue;
            }
            dropWindow = candidate;
            break;
        }
        return dropWindow != nullptr && contentWindow != nullptr &&
               ImGui::IsWindowWithinBeginStackOf(dropWindow, contentWindow);
    }

    void ClaimExternalDropForPanel(const editor::EditorPanelContext& panelContext,
                                   AssetBrowserPanelState& state) noexcept
    {
        AssetBrowserExtensionState* const extension = state.extension;
        if (extension == nullptr)
            return;
        PollExternalDropOncePerFrame(*extension, panelContext.GetUi().GetFrame());
        if (extension->pendingDrop.state == editor::EditorFileDropState::None ||
            !PointInsidePanelContent(panelContext, extension->pendingDrop))
            return;

        if (state.importDialog.IsValid() || state.importBatchActive)
        {
            state.importMessage = vanguard::containers::String("Dropped files ignored while another import is active");
            ClearPendingExternalDrop(*extension);
            return;
        }
        if (extension->pendingDrop.state == editor::EditorFileDropState::LimitExceeded)
        {
            state.importMessage = vanguard::containers::String("Dropped file batch exceeded editor path limits");
            ClearPendingExternalDrop(*extension);
            return;
        }
        const vanguard::containers::StringView folder(state.folderPath);
        if (!extension->assets.CanImportExternalSource(state.root, folder))
        {
            state.importMessage = vanguard::containers::String("Dropped files target is no longer writable or available");
            ClearPendingExternalDrop(*extension);
            return;
        }

        ClearExternalImportWork(state, false);
        state.importRoot = state.root;
        const vanguard::u32 folderBytes = static_cast<vanguard::u32>(std::strlen(state.folderPath));
        if (folderBytes != 0u)
            std::memcpy(state.importFolder, state.folderPath, folderBytes);
        state.importFolder[folderBytes] = '\0';
        state.importPaths.Reserve(extension->droppedPaths.Size());
        if (state.importPaths.Capacity() < extension->droppedPaths.Size())
        {
            state.importMessage = vanguard::containers::String("Dropped file batch could not be queued");
            ClearPendingExternalDrop(*extension);
            return;
        }
        for (const vanguard::containers::String& path : extension->droppedPaths)
            state.importPaths.PushBack(path);
        state.importNext = 0u;
        state.importBatchActive = !state.importPaths.Empty();
        state.importCancelRequested = false;
        state.importMessage = state.importBatchActive ? vanguard::containers::String("Dropped files queued for import")
                                                     : vanguard::containers::String("Dropped file batch contained no usable paths");
        ClearPendingExternalDrop(*extension);
    }

    [[nodiscard]] const char* ImportStateName(const assets::SourceImportState state) noexcept
    {
        switch (state)
        {
        case assets::SourceImportState::NotClassified: return "Not classified";
        case assets::SourceImportState::MetadataSelected: return "Imported";
        case assets::SourceImportState::Candidate: return "Candidate";
        case assets::SourceImportState::Unsupported: return "Unsupported";
        case assets::SourceImportState::DiscoveryUnavailable: return "Discovery unavailable";
        case assets::SourceImportState::Ambiguous: return "Ambiguous";
        case assets::SourceImportState::ImporterUnavailable: return "Importer unavailable";
        case assets::SourceImportState::VersionMismatch: return "Version mismatch";
        case assets::SourceImportState::SourceMismatch: return "Source mismatch";
        case assets::SourceImportState::InvalidMetadata: return "Invalid metadata";
        default: return "Unknown";
        }
    }

    void FormatPrimaryOutputTypeLabel(const vanguard::u32 type, char (&output)[32]) noexcept
    {
        if (type == textures::TextureResourceType)
            std::snprintf(output, sizeof(output), "Texture");
        else if (type == resources::InvalidResourceTypeId)
            std::snprintf(output, sizeof(output), "Source");
        else
            std::snprintf(output, sizeof(output), "0x%08x", type);
    }

    [[nodiscard]] const char* ExternalImportItemStateName(const AssetBrowserExternalImportItemState state) noexcept
    {
        switch (state)
        {
        case AssetBrowserExternalImportItemState::Pending: return "Pending";
        case AssetBrowserExternalImportItemState::Success: return "Success";
        case AssetBrowserExternalImportItemState::Cancelled: return "Cancelled";
        case AssetBrowserExternalImportItemState::Unsupported: return "Unsupported";
        case AssetBrowserExternalImportItemState::AmbiguousImporter: return "Ambiguous importer";
        case AssetBrowserExternalImportItemState::AuthoringUnavailable: return "Authoring unavailable";
        case AssetBrowserExternalImportItemState::DestinationConflict: return "Destination conflict";
        case AssetBrowserExternalImportItemState::ReadOnlyDestination: return "Read-only destination";
        case AssetBrowserExternalImportItemState::StaleDestination: return "Destination changed / unavailable";
        case AssetBrowserExternalImportItemState::ManagedProjectSource: return "Already managed project source";
        case AssetBrowserExternalImportItemState::InputUnavailable: return "Input unavailable";
        case AssetBrowserExternalImportItemState::InputReadFailure: return "Input read failure";
        case AssetBrowserExternalImportItemState::LimitExceeded: return "Limit exceeded";
        case AssetBrowserExternalImportItemState::TransactionFailure: return "Transaction failure";
        case AssetBrowserExternalImportItemState::IdentityGenerationFailure: return "Identity generation failure";
        case AssetBrowserExternalImportItemState::CleanupPending: return "Committed; cleanup pending";
        case AssetBrowserExternalImportItemState::ReconciliationPending: return "Committed; reconciliation pending";
        case AssetBrowserExternalImportItemState::Failed: return "Failed";
        default: return "Unknown";
        }
    }

    [[nodiscard]] bool IsCommittedExternalImportResult(const assetStreaming::ProjectAssetPipelineResult result) noexcept
    {
        return result == assetStreaming::ProjectAssetPipelineResult::Success ||
               result == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
               result == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
    }

    [[nodiscard]] AssetBrowserExternalImportItemState ClassifyExternalImportResult(
        const assetStreaming::ProjectAssetPipelineResult result,
        const editor::EditorProjectExternalImportFailure& failure) noexcept
    {
        if (result == assetStreaming::ProjectAssetPipelineResult::Success)
            return AssetBrowserExternalImportItemState::Success;
        if (result == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending)
            return AssetBrowserExternalImportItemState::CleanupPending;
        if (result == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
            return AssetBrowserExternalImportItemState::ReconciliationPending;
        if (result == assetStreaming::ProjectAssetPipelineResult::IdentityGenerationFailed)
            return AssetBrowserExternalImportItemState::IdentityGenerationFailure;
        if (result == assetStreaming::ProjectAssetPipelineResult::HistoryLimitExceeded ||
            failure.code == editor::EditorProjectExternalImportFailureCode::LimitExceeded)
            return AssetBrowserExternalImportItemState::LimitExceeded;
        switch (failure.code)
        {
        case editor::EditorProjectExternalImportFailureCode::ReadOnlyDestination:
            return AssetBrowserExternalImportItemState::ReadOnlyDestination;
        case editor::EditorProjectExternalImportFailureCode::StaleDestination:
            return AssetBrowserExternalImportItemState::StaleDestination;
        case editor::EditorProjectExternalImportFailureCode::ManagedProjectSource:
            return AssetBrowserExternalImportItemState::ManagedProjectSource;
        case editor::EditorProjectExternalImportFailureCode::InputUnavailable:
            return AssetBrowserExternalImportItemState::InputUnavailable;
        case editor::EditorProjectExternalImportFailureCode::InputReadFailure:
            return AssetBrowserExternalImportItemState::InputReadFailure;
        default: break;
        }
        switch (failure.pipeline.operation.result)
        {
        case assets::AssetOperationResult::UnsupportedSource:
            return AssetBrowserExternalImportItemState::Unsupported;
        case assets::AssetOperationResult::AmbiguousSource:
            return AssetBrowserExternalImportItemState::AmbiguousImporter;
        case assets::AssetOperationResult::ImportDescriptionUnavailable:
            return AssetBrowserExternalImportItemState::AuthoringUnavailable;
        case assets::AssetOperationResult::TargetConflict:
            return AssetBrowserExternalImportItemState::DestinationConflict;
        case assets::AssetOperationResult::ReadOnlySource:
            return AssetBrowserExternalImportItemState::ReadOnlyDestination;
        case assets::AssetOperationResult::TransactionFailed:
            return AssetBrowserExternalImportItemState::TransactionFailure;
        case assets::AssetOperationResult::LimitExceeded:
            return AssetBrowserExternalImportItemState::LimitExceeded;
        default: return AssetBrowserExternalImportItemState::Failed;
        }
    }

    [[nodiscard]] AssetBrowserExternalImportItemState ClassifyExternalImportCaptureFailure(
        const editor::EditorProjectExternalImportFailureCode failure) noexcept
    {
        switch (failure)
        {
        case editor::EditorProjectExternalImportFailureCode::ManagedProjectSource:
            return AssetBrowserExternalImportItemState::ManagedProjectSource;
        case editor::EditorProjectExternalImportFailureCode::InputUnavailable:
            return AssetBrowserExternalImportItemState::InputUnavailable;
        case editor::EditorProjectExternalImportFailureCode::InputReadFailure:
            return AssetBrowserExternalImportItemState::InputReadFailure;
        case editor::EditorProjectExternalImportFailureCode::LimitExceeded:
            return AssetBrowserExternalImportItemState::LimitExceeded;
        default:
            return AssetBrowserExternalImportItemState::Failed;
        }
    }

    void FormatSourceRootName(const AssetBrowserPanelState& state, const vanguard::u32 root,
                              char* const output, const vanguard::u32 capacity) noexcept
    {
        if (output == nullptr || capacity == 0u)
            return;
        output[0] = '\0';
        editor::EditorProjectAssetRootPresentation presentation;
        if (state.extension != nullptr && state.extension->assets.GetSourceRootPresentation(root, presentation))
            std::snprintf(output, capacity, "%s", presentation.displayName);
        else
            std::snprintf(output, capacity, "Project Source %u", root + 1u);
    }

    [[nodiscard]] AssetBrowserExternalImportCapture* CreateExternalImportCapture() noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       sizeof(AssetBrowserExternalImportCapture),
                                       alignof(AssetBrowserExternalImportCapture));
        return block ? ::new (block.address) AssetBrowserExternalImportCapture() : nullptr;
    }

    void RetainExternalImportCapture(AssetBrowserExternalImportCapture* const capture) noexcept
    {
        if (capture != nullptr)
            static_cast<void>(capture->references.Increment());
    }

    void ReleaseExternalImportCapture(AssetBrowserExternalImportCapture* const capture) noexcept
    {
        if (capture == nullptr || capture->references.Decrement() != 0u)
            return;
        capture->~AssetBrowserExternalImportCapture();
        vanguard::memory::MemoryBlock block{
            capture, sizeof(AssetBrowserExternalImportCapture), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    void CompleteExternalImportCapture(
        const io::AsyncReadToken& token, const io::AsyncResult result,
        const vanguard::u32 bytesTransferred, io::ShareableIOMemory memory,
        const vanguard::u32 memoryOffset, io::UniqueBuffer) noexcept
    {
        auto* const capture = static_cast<AssetBrowserExternalImportCapture*>(token.m_userData);
        if (capture == nullptr)
            return;
        if (result == io::eAsyncResult_Success && bytesTransferred == capture->byteCount &&
            memory.GetSize() >= memoryOffset &&
            capture->byteCount <= memory.GetSize() - memoryOffset)
        {
            capture->memory = static_cast<io::ShareableIOMemory&&>(memory);
            capture->memoryOffset = memoryOffset;
        }
        else if (result == io::eAsyncResult_Canceled)
        {
            capture->failure = editor::EditorProjectExternalImportFailureCode::InputUnavailable;
        }
        else
        {
            capture->failure = editor::EditorProjectExternalImportFailureCode::InputReadFailure;
        }
        capture->completed.SetValue(1u);
        ReleaseExternalImportCapture(capture);
    }

    void AbandonExternalImportCapture(AssetBrowserPanelState& state) noexcept
    {
        // The panel owns one reference and the submitted I/O callback owns one.
        // Cancellation is advisory but callback completion is guaranteed by the
        // I/O owner, including during managed I/O shutdown. That lets panel
        // close/deactivation detach immediately without blocking the UI or
        // leaving a JobsService outstanding job behind.
        if (state.importCaptureWork != nullptr && state.importCaptureWork->completed.GetValue() == 0u &&
            state.importCaptureWork->ioContext)
            state.importCaptureWork->ioContext->RequestCancel();
        ReleaseExternalImportCapture(state.importCaptureWork);
        state.importCaptureWork = nullptr;
        state.importCapturePrepared = false;
    }

    void ClearExternalImportWork(AssetBrowserPanelState& state, const bool releaseDialog) noexcept
    {
        if (releaseDialog && state.importDialog.IsValid() && state.extension != nullptr &&
            state.extension->fileDialogs != nullptr)
        {
            editor::EditorFileDialogFailure ignored;
            static_cast<void>(state.extension->fileDialogs->Release(state.importDialog, &ignored));
        }
        AbandonExternalImportCapture(state);
        state.importDialog = {};
        state.importPaths.Clear();
        state.importNext = 0u;
        state.importBatchActive = false;
        state.importCancelRequested = false;
        for (AssetBrowserExternalImportItemResult& result : state.importResults)
            result = {};
    }

    [[nodiscard]] bool BeginExternalImportCapture(AssetBrowserPanelState& state,
                                                  const filesystem::AbsolutePath& externalSource,
                                                  AssetBrowserExternalImportItemResult& item) noexcept
    {
        if (state.extension == nullptr || state.importCapturePrepared || state.importCaptureWork != nullptr)
            return false;
        if (state.extension->assets.IsManagedProjectSourcePath(externalSource))
        {
            item.failure.code = editor::EditorProjectExternalImportFailureCode::ManagedProjectSource;
            item.state = AssetBrowserExternalImportItemState::ManagedProjectSource;
            return false;
        }
        const vanguard::containers::StringView fileName = filesystem::paths::GetFileName(externalSource);
        if (fileName.Empty() || fileName.Length() > vanguard::resources::MaximumResourcePathBytes)
        {
            item.failure.code = editor::EditorProjectExternalImportFailureCode::InputUnavailable;
            item.state = AssetBrowserExternalImportItemState::InputUnavailable;
            return false;
        }

        const vanguard::u64 maximumBytes = state.extension->assets.GetMaximumExternalImportSourceBytes();
        if (maximumBytes == 0u || !io::IsInitialized())
        {
            item.state = AssetBrowserExternalImportItemState::Failed;
            return false;
        }

        const io::FileHandle file = io::GetSystem().OpenFile(externalSource.AsChar());
        if (file == io::InvalidFileHandle)
        {
            item.failure.code = editor::EditorProjectExternalImportFailureCode::InputUnavailable;
            item.state = AssetBrowserExternalImportItemState::InputUnavailable;
            return false;
        }
        const vanguard::u64 sourceBytes = io::GetSystem().GetFileSize(file);
        if (sourceBytes > maximumBytes || sourceBytes > 0xffffffffull)
        {
            io::GetSystem().ReleaseFile(file);
            item.failure.code = editor::EditorProjectExternalImportFailureCode::LimitExceeded;
            item.state = AssetBrowserExternalImportItemState::LimitExceeded;
            return false;
        }

        AssetBrowserExternalImportCapture* const capture = CreateExternalImportCapture();
        if (capture == nullptr)
        {
            io::GetSystem().ReleaseFile(file);
            item.failure.code = editor::EditorProjectExternalImportFailureCode::LimitExceeded;
            item.state = AssetBrowserExternalImportItemState::LimitExceeded;
            return false;
        }
        std::memcpy(capture->fileName, fileName.Data(), fileName.Length());
        capture->fileName[fileName.Length()] = '\0';
        capture->byteCount = static_cast<vanguard::u32>(sourceBytes);
        if (sourceBytes == 0u)
        {
            io::GetSystem().ReleaseFile(file);
            capture->completed.SetValue(1u);
            state.importCaptureWork = capture;
            state.importCapturePrepared = true;
            return true;
        }

        capture->ioContext = red::CreateSharedPtr<io::IOContext>();
        if (!capture->ioContext)
        {
            io::GetSystem().ReleaseFile(file);
            ReleaseExternalImportCapture(capture);
            item.failure.code = editor::EditorProjectExternalImportFailureCode::LimitExceeded;
            item.state = AssetBrowserExternalImportItemState::LimitExceeded;
            return false;
        }

        io::AsyncReadToken token;
        token.m_callback = &CompleteExternalImportCapture;
        token.m_userData = capture;
        token.m_buffer = nullptr;
        token.m_debugLogicalFileName = capture->fileName;
        token.m_offset = 0;
        token.m_numberOfBytesToRead = capture->byteCount;
        token.m_ioContext = capture->ioContext;
        token.m_requestSource = io::RequestSource::Tools;
        RetainExternalImportCapture(capture); // callback reference
        io::GetSystem().BeginRead(file, token, io::eAsyncPriority_UI);
        io::GetSystem().ReleaseFile(file);
        state.importCaptureWork = capture;
        state.importCapturePrepared = capture->completed.GetValue() != 0u;
        return true;
    }

    [[nodiscard]] bool BeginExternalImport(AssetBrowserPanelState& state, const vanguard::u32 root,
                                           const vanguard::containers::StringView relativeFolder) noexcept
    {
        if (state.extension == nullptr || state.extension->fileDialogs == nullptr || state.importDialog.IsValid() ||
            state.importBatchActive || relativeFolder.Length() >= sizeof(state.importFolder) ||
            !state.extension->assets.CanImportExternalSource(root, relativeFolder))
            return false;
        ClearExternalImportWork(state, false);
        state.importRoot = root;
        if (!relativeFolder.Empty())
            std::memcpy(state.importFolder, relativeFolder.Data(), relativeFolder.Length());
        state.importFolder[relativeFolder.Length()] = '\0';
        state.importMessage = {};
        editor::EditorFileDialogFailure failure;
        if (!state.extension->fileDialogs->OpenFiles(true, state.importDialog, &failure))
        {
            state.importMessage = vanguard::containers::String(
                failure.message != nullptr ? failure.message : "Unable to open the native file picker");
            state.importDialog = {};
            return false;
        }
        return true;
    }

    void CancelExternalImport(AssetBrowserPanelState& state) noexcept
    {
        if (state.importDialog.IsValid())
        {
            ClearExternalImportWork(state, true);
            state.importMessage = vanguard::containers::String("Import cancelled before selection was accepted");
            return;
        }
        if (state.importBatchActive)
            state.importCancelRequested = true;
    }

    void PumpExternalImport(AssetBrowserPanelState& state) noexcept
    {
        if (state.extension == nullptr || state.extension->fileDialogs == nullptr)
            return;
        if (state.importDialog.IsValid())
        {
            editor::EditorFileDialogPollResult poll;
            editor::EditorFileDialogFailure failure;
            if (!state.extension->fileDialogs->Poll(state.importDialog, poll, state.importPaths,
                                                    state.importMessage, &failure))
            {
                state.importMessage = vanguard::containers::String(
                    failure.message != nullptr ? failure.message : "Native file picker result became unavailable");
                state.importDialog = {};
                return;
            }
            if (poll.state == editor::EditorFileDialogState::Pending)
                return;
            state.importDialog = {};
            if (poll.state == editor::EditorFileDialogState::Cancelled)
            {
                state.importMessage = vanguard::containers::String("Import cancelled");
                return;
            }
            if (poll.state == editor::EditorFileDialogState::BackendError)
            {
                if (state.importMessage.Empty())
                    state.importMessage = vanguard::containers::String("Native file picker failed");
                return;
            }
            if (poll.state == editor::EditorFileDialogState::LimitExceeded)
            {
                state.importMessage = vanguard::containers::String("Selected file list exceeded the editor dialog limits");
                return;
            }
            if (poll.state != editor::EditorFileDialogState::Selected || state.importPaths.Empty())
            {
                state.importMessage = vanguard::containers::String("Native file picker returned no usable files");
                return;
            }
            state.importNext = 0u;
            state.importBatchActive = true;
            state.importCancelRequested = false;
            for (AssetBrowserExternalImportItemResult& result : state.importResults)
                result = {};
        }

        if (!state.importBatchActive)
            return;

        if (state.importCancelRequested)
        {
            // No durable mutation runs off-thread. A live/prepared capture can
            // therefore be detached immediately; the worker keeps its own
            // reference and self-reclaims when the read eventually returns.
            AbandonExternalImportCapture(state);
            while (state.importNext < state.importPaths.Size() &&
                   state.importNext < editor::MaximumEditorFileDialogSelections)
                state.importResults[state.importNext++].state = AssetBrowserExternalImportItemState::Cancelled;
            state.importBatchActive = false;
            state.importCancelRequested = false;
            state.importMessage = vanguard::containers::String(
                "Import batch cancelled; committed files were kept");
            return;
        }

        if (state.importCaptureWork != nullptr && !state.importCapturePrepared)
        {
            if (state.importCaptureWork->completed.GetValue() == 0u)
                return;
            state.importCapturePrepared = true;
        }

        if (state.importCapturePrepared)
        {
            AssetBrowserExternalImportItemResult& item = state.importResults[state.importNext];
            AssetBrowserExternalImportCapture* const capture = state.importCaptureWork;
            if (capture == nullptr)
            {
                item.state = AssetBrowserExternalImportItemState::Failed;
            }
            else if (capture->failure != editor::EditorProjectExternalImportFailureCode::None)
            {
                item.failure.code = capture->failure;
                item.state = ClassifyExternalImportCaptureFailure(capture->failure);
            }
            else
            {
                const assetStreaming::ProjectAssetPipelineResult result = state.extension->assets.ImportExternalSource(
                    state.importRoot, vanguard::containers::StringView(state.importFolder),
                    vanguard::containers::StringView(capture->fileName),
                    {capture->byteCount == 0u
                         ? nullptr
                         : static_cast<const vanguard::u8*>(capture->memory.Data()) + capture->memoryOffset,
                     capture->byteCount},
                    item.importedAsset, &item.failure);
                if (result == assetStreaming::ProjectAssetPipelineResult::Busy)
                    return;
                item.state = ClassifyExternalImportResult(result, item.failure);
                if (IsCommittedExternalImportResult(result))
                    state.queryDirty = true;
            }
            ReleaseExternalImportCapture(state.importCaptureWork);
            state.importCaptureWork = nullptr;
            state.importCapturePrepared = false;
            ++state.importNext;
        }
        if (state.importNext >= state.importPaths.Size() || state.importNext >= editor::MaximumEditorFileDialogSelections)
        {
            state.importBatchActive = false;
            return;
        }

        const vanguard::containers::String& path = state.importPaths[state.importNext];
        const vanguard::containers::StringView pathView(path.AsChar(), path.Length());
        if (!filesystem::AbsolutePath::IsValidPath(pathView))
        {
            state.importResults[state.importNext].state = AssetBrowserExternalImportItemState::InputUnavailable;
            ++state.importNext;
            state.importBatchActive = state.importNext < state.importPaths.Size();
            return;
        }
        const filesystem::AbsolutePath externalSource = filesystem::AbsolutePath::CreateFilePath(pathView);
        AssetBrowserExternalImportItemResult& item = state.importResults[state.importNext];
        if (!BeginExternalImportCapture(state, externalSource, item))
        {
            ++state.importNext;
            state.importBatchActive = state.importNext < state.importPaths.Size();
            if (!state.importBatchActive)
                state.importMessage = vanguard::containers::String("Import batch complete");
        }
    }

    void DrawExternalImportStatus(AssetBrowserPanelState& state) noexcept
    {
        if (!state.importDialog.IsValid() && !state.importBatchActive && state.importPaths.Empty() && state.importMessage.Empty())
            return;
        ImGui::SeparatorText("External import");
        if (state.importDialog.IsValid())
            ImGui::TextDisabled("Waiting for file selection...");
        else if (state.importBatchActive)
            ImGui::Text("Reading / importing %u / %u", state.importNext + 1u, state.importPaths.Size());
        if ((state.importDialog.IsValid() || state.importBatchActive) && ImGui::Button("Cancel import"))
            CancelExternalImport(state);
        if (!state.importMessage.Empty())
            ImGui::TextDisabled("%s", state.importMessage.AsChar());
        for (vanguard::u32 index = 0u; index < state.importPaths.Size() && index < editor::MaximumEditorFileDialogSelections; ++index)
        {
            const AssetBrowserExternalImportItemResult& item = state.importResults[index];
            ImGui::TextWrapped("%s - %s", state.importPaths[index].AsChar(), ExternalImportItemStateName(item.state));
        }
    }

    [[nodiscard]] const char* MetadataResultName(const assets::MetadataResult result) noexcept
    {
        switch (result)
        {
        case assets::MetadataResult::Success: return "Valid";
        case assets::MetadataResult::InvalidData: return "Invalid";
        case assets::MetadataResult::UnsupportedVersion: return "Unsupported version";
        case assets::MetadataResult::LimitExceeded: return "Limit exceeded";
        case assets::MetadataResult::DuplicateOutput: return "Duplicate output";
        case assets::MetadataResult::IdentityCollision: return "Identity collision";
        default: return "Unknown";
        }
    }

    [[nodiscard]] const char* DeliveryStateName(const assetStreaming::ProjectAssetDeliveryStageState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetDeliveryStageState::Untracked: return "Untracked";
        case assetStreaming::ProjectAssetDeliveryStageState::Pending: return "Pending";
        case assetStreaming::ProjectAssetDeliveryStageState::Committed: return "Committed";
        case assetStreaming::ProjectAssetDeliveryStageState::Installed: return "Installed";
        case assetStreaming::ProjectAssetDeliveryStageState::Current: return "Current";
        case assetStreaming::ProjectAssetDeliveryStageState::Stale: return "Stale";
        case assetStreaming::ProjectAssetDeliveryStageState::Failed: return "Failed";
        default: return "Unknown";
        }
    }

    [[nodiscard]] const char* DirtyReasonName(const assets::DirtyReason reason) noexcept
    {
        switch (reason)
        {
        case assets::DirtyReason::UpToDate: return "Up to date";
        case assets::DirtyReason::Missing: return "Missing";
        case assets::DirtyReason::SourceChanged: return "Source changed";
        case assets::DirtyReason::CompilerChanged: return "Compiler changed";
        case assets::DirtyReason::DependenciesChanged: return "Dependencies changed";
        default: return "Unknown";
        }
    }

    [[nodiscard]] const char* OperationName(const AssetBrowserOperationKind kind) noexcept
    {
        switch (kind)
        {
        case AssetBrowserOperationKind::Import: return "Import";
        case AssetBrowserOperationKind::Reimport: return "Reimport";
        case AssetBrowserOperationKind::Move: return "Move / Rename";
        case AssetBrowserOperationKind::Duplicate: return "Duplicate";
        case AssetBrowserOperationKind::Delete: return "Delete";
        case AssetBrowserOperationKind::Undo: return "Undo";
        case AssetBrowserOperationKind::Redo: return "Redo";
        case AssetBrowserOperationKind::ApplyTextureSettings: return "Apply texture settings";
        default: return "None";
        }
    }

    [[nodiscard]] const char* AssetOperationResultName(const assets::AssetOperationResult result) noexcept
    {
        switch (result)
        {
        case assets::AssetOperationResult::Success: return "Success";
        case assets::AssetOperationResult::InvalidArgument: return "Invalid argument";
        case assets::AssetOperationResult::ReplacementUnavailable: return "Replacement unavailable";
        case assets::AssetOperationResult::ReferenceStateUnavailable: return "Reference state unavailable";
        case assets::AssetOperationResult::ReadOnlySource: return "Read-only source";
        case assets::AssetOperationResult::ImporterUnavailable: return "Importer unavailable";
        case assets::AssetOperationResult::RewriteUnavailable: return "Rewrite unavailable";
        case assets::AssetOperationResult::RelocationUnavailable: return "Relocation unavailable";
        case assets::AssetOperationResult::SourceUnavailable: return "Source changed / unavailable";
        case assets::AssetOperationResult::CandidateUnavailable: return "Candidate changed / unavailable";
        case assets::AssetOperationResult::SettingsInvalid: return "Settings invalid";
        case assets::AssetOperationResult::TargetConflict: return "Target already exists";
        case assets::AssetOperationResult::IdentityConflict: return "Identity conflict";
        case assets::AssetOperationResult::ReferencesRemain: return "References remain";
        case assets::AssetOperationResult::RewriteFailed: return "Reference rewrite failed";
        case assets::AssetOperationResult::TransactionFailed: return "Filesystem transaction failed";
        case assets::AssetOperationResult::LimitExceeded: return "Operation limit exceeded";
        case assets::AssetOperationResult::UnsupportedSource: return "Unsupported source";
        case assets::AssetOperationResult::AmbiguousSource: return "Ambiguous importer";
        case assets::AssetOperationResult::ImportDescriptionUnavailable: return "Import authoring unavailable";
        default: return "Unknown";
        }
    }

    [[nodiscard]] bool IsCommittedOperationResult(const assetStreaming::ProjectAssetPipelineResult result) noexcept
    {
        return result == assetStreaming::ProjectAssetPipelineResult::Success ||
               result == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
               result == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
    }

    [[nodiscard]] bool SameTextureSettings(const textureTools::TextureBuildDescription& left,
                                           const textureTools::TextureBuildDescription& right) noexcept
    {
        return left.sourceMode == right.sourceMode && left.colorSpace == right.colorSpace &&
               left.channels.red == right.channels.red && left.channels.green == right.channels.green &&
               left.channels.blue == right.channels.blue && left.channels.alpha == right.channels.alpha &&
               left.routeFlags == right.routeFlags && left.ddsMipTailCount == right.ddsMipTailCount &&
               left.profile == right.profile;
    }

    void RecordOperation(AssetBrowserPanelState& state, const AssetBrowserOperationKind kind,
                         const assetStreaming::ProjectAssetPipelineResult result,
                         const assetStreaming::ProjectAssetPipelineFailure& failure,
                         const assets::AssetId resultingAsset = {}) noexcept
    {
        state.operation.kind = kind;
        state.operation.result = result;
        state.operation.failure = failure;
        state.operation.resultingAsset = resultingAsset;
        if (!IsCommittedOperationResult(result))
        {
            const char* const resultName = assetStreaming::ToString(result);
            VG_LOG_ERROR(vanguard::diagnostics::Category::Tools,
                         "asset browser operation failed: action=%s result=%s operation=%s",
                         OperationName(kind), resultName != nullptr ? resultName : "Unknown",
                         AssetOperationResultName(failure.operation.result));
        }
        else if (result == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                 result == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
        {
            const char* const resultName = assetStreaming::ToString(result);
            VG_LOG_WARNING(vanguard::diagnostics::Category::Tools,
                           "asset browser operation committed with pending follow-up: action=%s result=%s",
                           OperationName(kind), resultName != nullptr ? resultName : "Unknown");
        }
        if (!IsCommittedOperationResult(result))
            return;
        state.selectedFreshness = {};
        state.selectedFreshnessResult = assetStreaming::ProjectAssetPipelineResult::InvalidState;
        state.selectedFreshnessEvaluated = false;
        state.textureSettingsLoaded = false;
        state.textureSettingsSupported = false;
        state.textureSettingsDirty = false;
        state.deleteArmed = false;
        state.deleteOpenRequested = false;
        state.queryDirty = true;
    }

    void DrawOperationStatus(const AssetBrowserPanelState& state) noexcept
    {
        if (state.operation.kind == AssetBrowserOperationKind::None)
            return;
        const char* const result = assetStreaming::ToString(state.operation.result);
        if (IsCommittedOperationResult(state.operation.result))
            ImGui::Text("%s: %s", OperationName(state.operation.kind), result != nullptr ? result : "Success");
        else
        {
            ImGui::TextDisabled("%s failed: %s", OperationName(state.operation.kind), result != nullptr ? result : "Unknown");
            if (state.operation.failure.operation.result != assets::AssetOperationResult::Success)
                ImGui::TextDisabled("Operation detail: %s",
                                    AssetOperationResultName(state.operation.failure.operation.result));
        }
        if (state.operation.result == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending)
            ImGui::TextDisabled("Authored change committed; transaction cleanup is still pending.");
        else if (state.operation.result == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
            ImGui::TextDisabled("Authored change committed; catalog reconciliation is still pending.");
    }

    [[nodiscard]] bool PrepareTargetedOperation(AssetBrowserPanelState& state,
                                                const AssetBrowserTargetedOperationKind kind) noexcept
    {
        if (kind == AssetBrowserTargetedOperationKind::None ||
            QueryAdoptedMutation(&state, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        const assetStreaming::ProjectAssetCatalog* const catalog = state.extension->assets.GetCatalog();
        if (catalog == nullptr || catalog->GetStatus().sourceGeneration != state.catalogGeneration)
            return false;
        const vanguard::u32 bytes = catalog->CopyRelativePath(
            state.selectedRow,
            {state.mutationTarget, static_cast<vanguard::u32>(sizeof(state.mutationTarget))});
        if (bytes == 0u)
            return false;
        state.targetedOperation = kind;
        state.targetedOperationOpenRequested = true;
        return true;
    }

    void DrawTargetedOperationDialog(AssetBrowserPanelState& state) noexcept
    {
        if (state.targetedOperationOpenRequested)
        {
            const char* const popup = state.targetedOperation == AssetBrowserTargetedOperationKind::Duplicate
                                          ? "Duplicate Asset###AssetBrowserTargetedOperation"
                                          : "Move / Rename Asset###AssetBrowserTargetedOperation";
            ImGui::OpenPopup(popup);
            state.targetedOperationOpenRequested = false;
        }
        if (state.targetedOperation == AssetBrowserTargetedOperationKind::None)
            return;

        const char* const popup = state.targetedOperation == AssetBrowserTargetedOperationKind::Duplicate
                                      ? "Duplicate Asset###AssetBrowserTargetedOperation"
                                      : "Move / Rename Asset###AssetBrowserTargetedOperation";
        ImGui::SetNextWindowSizeConstraints(
            {editor::detail::EditorVisualPx(420.0f), 0.0f},
            {editor::detail::EditorVisualPx(680.0f), FLT_MAX});
        if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const bool duplicate = state.targetedOperation == AssetBrowserTargetedOperationKind::Duplicate;
        ImGui::TextUnformatted(duplicate ? "Choose a destination for the duplicate."
                                        : "Change the file name or move it within the same asset root.");
        ImGui::TextDisabled("Destination is root-relative (for example: Textures/Grid.png).");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##AssetTargetedOperationDestination", state.mutationTarget, sizeof(state.mutationTarget));

        bool selectionCurrent = QueryAdoptedMutation(&state, nullptr).kind == editor::EditorCommandStateKind::Enabled;
        bool destinationChanged = false;
        if (selectionCurrent && state.mutationTarget[0] != '\0')
        {
            const assetStreaming::ProjectAssetCatalog* const catalog = state.extension->assets.GetCatalog();
            if (catalog != nullptr && catalog->GetStatus().sourceGeneration == state.catalogGeneration)
            {
                char currentPath[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                const vanguard::u32 currentBytes = catalog->CopyRelativePath(
                    state.selectedRow, {currentPath, static_cast<vanguard::u32>(sizeof(currentPath))});
                destinationChanged = currentBytes != 0u &&
                                     vanguard::containers::StringView(currentPath) !=
                                         vanguard::containers::StringView(state.mutationTarget);
            }
        }
        const bool canCommit = selectionCurrent && state.mutationTarget[0] != '\0' && destinationChanged;
        if (!selectionCurrent)
            ImGui::TextDisabled("Selection changed while this action was open. Cancel and retry.");
        else if (state.mutationTarget[0] == '\0')
            ImGui::TextDisabled("Enter a destination path.");
        else if (!destinationChanged)
            ImGui::TextDisabled("Choose a destination different from the current source path.");

        ImGui::Spacing();
        if (!canCommit)
            ImGui::BeginDisabled();
        const bool commit = ImGui::Button(duplicate ? "Duplicate" : "Move / Rename");
        if (!canCommit)
            ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");

        if (commit && canCommit)
        {
            assetStreaming::ProjectAssetPipelineFailure failure;
            assetStreaming::ProjectAssetPipelineResult result = assetStreaming::ProjectAssetPipelineResult::InvalidState;
            assets::AssetId resultingAsset;
            if (duplicate)
            {
                result = state.extension->assets.DuplicateAsset(
                    state.selectedAsset, state.selectedAcceptedGeneration, state.selectedSourceGeneration,
                    vanguard::containers::StringView(state.mutationTarget), resultingAsset, &failure);
                RecordOperation(state, AssetBrowserOperationKind::Duplicate, result, failure, resultingAsset);
            }
            else
            {
                result = state.extension->assets.MoveAsset(
                    state.selectedAsset, state.selectedAcceptedGeneration, state.selectedSourceGeneration,
                    vanguard::containers::StringView(state.mutationTarget), &failure);
                RecordOperation(state, AssetBrowserOperationKind::Move, result, failure);
            }
            if (IsCommittedOperationResult(result))
            {
                state.selectedSourceGeneration = 0u;
                state.targetedOperation = AssetBrowserTargetedOperationKind::None;
                ImGui::CloseCurrentPopup();
            }
        }
        else if (cancel)
        {
            state.targetedOperation = AssetBrowserTargetedOperationKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawDeleteConfirmationDialog(AssetBrowserPanelState& state) noexcept
    {
        constexpr const char* Popup = "Delete Asset###AssetBrowserDeleteConfirmation";
        if (state.deleteOpenRequested)
        {
            ImGui::OpenPopup(Popup);
            state.deleteOpenRequested = false;
        }
        if (!state.deleteArmed)
            return;

        ImGui::SetNextWindowSizeConstraints(
            {editor::detail::EditorVisualPx(380.0f), 0.0f},
            {editor::detail::EditorVisualPx(620.0f), FLT_MAX});
        if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!ImGui::IsPopupOpen(Popup))
                state.deleteArmed = false;
            return;
        }

        ImGui::TextUnformatted("Delete this asset from the project?");
        const assetStreaming::ProjectAssetCatalog* const catalog = state.extension->assets.GetCatalog();
        char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 pathBytes = catalog != nullptr
                                            ? catalog->CopyRelativePath(
                                                  state.selectedRow,
                                                  {path, static_cast<vanguard::u32>(sizeof(path))})
                                            : 0u;
        if (pathBytes != 0u)
            ImGui::TextWrapped("%.*s", static_cast<int>(pathBytes), path);
        ImGui::Spacing();
        ImGui::TextDisabled("This removes the authored source asset. You can undo the operation afterward when available.");

        const bool canDelete = QueryAdoptedMutation(&state, nullptr).kind == editor::EditorCommandStateKind::Enabled;
        if (!canDelete)
            ImGui::TextDisabled("The selection changed or became stale. Cancel and retry.");
        if (state.operation.kind == AssetBrowserOperationKind::Delete &&
            !IsCommittedOperationResult(state.operation.result))
        {
            const char* const result = assetStreaming::ToString(state.operation.result);
            ImGui::TextDisabled("Delete failed: %s", result != nullptr ? result : "Unknown");
            if (state.operation.failure.operation.result != assets::AssetOperationResult::Success)
                ImGui::TextDisabled("Operation detail: %s",
                                    AssetOperationResultName(state.operation.failure.operation.result));
        }

        ImGui::Spacing();
        if (!canDelete)
            ImGui::BeginDisabled();
        const bool confirm = ImGui::Button("Delete");
        if (!canDelete)
            ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");

        if (confirm && canDelete)
        {
            assetStreaming::ProjectAssetPipelineFailure failure;
            const auto result = state.extension->assets.DeleteAsset(
                state.selectedAsset, state.selectedAcceptedGeneration, state.selectedSourceGeneration, &failure);
            RecordOperation(state, AssetBrowserOperationKind::Delete, result, failure);
            if (IsCommittedOperationResult(result))
            {
                ClearSelection(state, true);
                ImGui::CloseCurrentPopup();
            }
        }
        else if (cancel)
        {
            state.deleteArmed = false;
            state.deleteOpenRequested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void FormatAssetId(const assets::AssetId& id, char (&output)[33]) noexcept
    {
        constexpr char Hex[] = "0123456789abcdef";
        for (vanguard::u32 index = 0u; index < 16u; ++index)
        {
            output[index * 2u] = Hex[id.bytes[index] >> 4u];
            output[index * 2u + 1u] = Hex[id.bytes[index] & 0x0fu];
        }
        output[32] = '\0';
    }

    [[nodiscard]] const char* TexturePreviewStateName(const editor::ProjectTexturePreviewState state) noexcept
    {
        switch (state)
        {
        case editor::ProjectTexturePreviewState::Queued: return "Queued";
        case editor::ProjectTexturePreviewState::LoadingResource: return "Loading resource";
        case editor::ProjectTexturePreviewState::WaitingForCurrentGeneration: return "Waiting for current generation";
        case editor::ProjectTexturePreviewState::LoadingResidency: return "Loading residency";
        case editor::ProjectTexturePreviewState::Ready: return "Ready";
        case editor::ProjectTexturePreviewState::Failed: return "Failed";
        case editor::ProjectTexturePreviewState::Retiring: return "Retiring";
        default: return "Unknown";
        }
    }

    [[nodiscard]] bool IsTexturePreviewOutput(const resources::ResourceReference output) noexcept
    {
        return output.IsValid() && output.IsTyped() && output.ExpectedType() == textures::TextureResourceType;
    }

    [[nodiscard]] bool IsSupportedTexturePreviewShape(const editor::ProjectTexturePreviewInfo& info) noexcept
    {
        return info.dimension == textures::TextureDimension::Texture2D && info.arrayLayers == 1u;
    }

    [[nodiscard]] editor::ProjectTexturePreviewQuality TexturePreviewPhysicalQuality(
        const float width, const float height) noexcept
    {
        ImGuiViewport* const viewport = ImGui::GetWindowViewport();
        ImVec2 framebufferScale{1.0f, 1.0f};
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        if (viewport != nullptr && platform.Platform_GetWindowFramebufferScale != nullptr)
            framebufferScale = platform.Platform_GetWindowFramebufferScale(viewport);
        else if (viewport != nullptr && viewport->DpiScale > 0.0f)
            framebufferScale = {viewport->DpiScale, viewport->DpiScale};
        const auto pixels = [](const float extent, const float scale) noexcept
        {
            const double physical = std::ceil(
                static_cast<double>(std::max(1.0f, extent)) *
                static_cast<double>(scale > 0.0f ? scale : 1.0f));
            return physical >= static_cast<double>(~vanguard::u32{0})
                       ? ~vanguard::u32{0}
                       : static_cast<vanguard::u32>(physical);
        };
        return {pixels(width, framebufferScale.x), pixels(height, framebufferScale.y)};
    }

    [[nodiscard]] ImVec2 TexturePreviewAspectFitExtent(
        const editor::ProjectTexturePreviewInfo& info,
        const float maximumWidth, const float maximumHeight) noexcept
    {
        float width = std::max(1.0f, maximumWidth);
        float height = std::max(1.0f, maximumHeight);
        if (info.width == 0u || info.height == 0u)
            return {width, height};
        const float sourceAspect = static_cast<float>(info.width) / static_cast<float>(info.height);
        const float boxAspect = width / height;
        if (sourceAspect > boxAspect)
            height = width / sourceAspect;
        else
            width = height * sourceAspect;
        return {std::max(1.0f, width), std::max(1.0f, height)};
    }

    [[nodiscard]] bool CanRetireTexturePreviewRegistration(
        const editor::EditorUiService& ui, const vanguard::u64 capturedUiFrame) noexcept
    {
        if (capturedUiFrame == 0u)
            return true;
        const vanguard::u64 currentUiFrame = ui.GetFrame();
        return currentUiFrame > capturedUiFrame ||
               (currentUiFrame == capturedUiFrame && ui.GetState() == editor::EditorUiState::Ready);
    }

    [[nodiscard]] bool SameTexturePreviewPublication(
        const editor::ProjectTexturePreviewInfo& left,
        const editor::ProjectTexturePreviewInfo& right) noexcept
    {
        return left.resource == right.resource &&
               left.buildFingerprint == right.buildFingerprint &&
               left.contentFingerprint == right.contentFingerprint;
    }

    [[nodiscard]] editor::ProjectTexturePreviewRequest MakeTexturePreviewRequest(
        const editor::ProjectTexturePreviewPriority priority,
        const editor::ProjectTexturePreviewQuality quality) noexcept
    {
        editor::ProjectTexturePreviewRequest request;
        request.priority = priority;
        request.quality = quality;
        return request;
    }

    [[nodiscard]] bool UpdateTexturePreviewDemand(
        editor::EditorProjectAssetAccess& assets,
        const editor::ProjectTexturePreviewHandle handle,
        const editor::ProjectTexturePreviewPriority priority,
        const editor::ProjectTexturePreviewQuality quality,
        editor::ProjectTexturePreviewQuality& requestedQuality) noexcept
    {
        if (!handle.IsValid())
            return false;
        if (quality == requestedQuality)
            return true;
        const editor::ProjectTexturePreviewRequest request = MakeTexturePreviewRequest(priority, quality);
        if (!assets.UpdateTexturePreview(handle, request))
            return false;
        requestedQuality = quality;
        return true;
    }

    [[nodiscard]] bool RefreshTexturePreviewRegistration(
        AssetBrowserExtensionState& extension,
        const editor::ProjectTexturePreviewHandle handle,
        const editor::ProjectTexturePreviewInfo& info,
        editor::EditorTextureHandle& texture,
        vanguard::u64& capturedUiFrame,
        vanguard::u64& observedPresentationRevision) noexcept
    {
        if (!handle.IsValid() || extension.ui == nullptr || !extension.previewSampler.IsValid() ||
            info.state != editor::ProjectTexturePreviewState::Ready || info.presentationRevision == 0u)
            return false;
        if (texture.IsValid() && observedPresentationRevision == info.presentationRevision)
            return true;

        editor::EditorTextureHandle replacement;
        if (!extension.previewPresentation.Register(
                handle, *extension.ui, extension.previewSampler.GetRef(),
                info.srgb ? editor::EditorTextureColorSpace::DisplaySrgb
                          : editor::EditorTextureColorSpace::DisplayLinear,
                replacement))
            return false;

        if (texture.IsValid())
        {
            if (!CanRetireTexturePreviewRegistration(*extension.ui, capturedUiFrame))
            {
                editor::EditorUiFailure replacementFailure;
                static_cast<void>(extension.ui->UnregisterTexture(replacement, &replacementFailure));
                return false;
            }
            editor::EditorUiFailure oldFailure;
            if (!extension.ui->UnregisterTexture(texture, &oldFailure))
            {
                editor::EditorUiFailure replacementFailure;
                static_cast<void>(extension.ui->UnregisterTexture(replacement, &replacementFailure));
                return false;
            }
        }

        texture = replacement;
        capturedUiFrame = 0u;
        observedPresentationRevision = info.presentationRevision;
        return true;
    }

    [[nodiscard]] bool PreviewRetryDue(const bool attempted, const vanguard::u64 lastAttemptUiFrame,
                                       const vanguard::u64 uiFrame) noexcept
    {
        if (!attempted)
            return true;
        if (uiFrame <= lastAttemptUiFrame)
            return false;
        return uiFrame - lastAttemptUiFrame >= AssetBrowserThumbnailRetryFrames;
    }

    [[nodiscard]] bool HasPublishedTexturePublication(
        const assetStreaming::ProjectAssetOutputPublicationStatus& publication) noexcept
    {
        using DeliveryState = assetStreaming::ProjectAssetDeliveryStageState;
        return publication.resource.IsValid() && publication.resource.IsTyped() &&
               publication.resource.ExpectedType() == textures::TextureResourceType &&
               !publication.publishedBuildFingerprint.IsEmpty() &&
               !publication.publishedContentFingerprint.IsEmpty() &&
               (publication.publicationState == DeliveryState::Installed ||
                publication.publicationState == DeliveryState::Stale);
    }

    [[nodiscard]] bool PreviewPublicationDiffers(
        const editor::ProjectTexturePreviewInfo& preview,
        const assetStreaming::ProjectAssetOutputPublicationStatus& publication) noexcept
    {
        return HasPublishedTexturePublication(publication) &&
               (publication.publishedBuildFingerprint != preview.buildFingerprint ||
                publication.publishedContentFingerprint != preview.contentFingerprint);
    }

    void ReleaseTextureThumbnailsForAsset(AssetBrowserExtensionState& extension, assets::AssetId asset) noexcept;

    [[nodiscard]] bool ReleasePreviewCandidate(AssetBrowserPanelState& state) noexcept
    {
        if (!state.previewCandidate.IsValid())
        {
            state.previewCandidateInfo = {};
            state.previewCandidateQuality = {};
            return true;
        }
        if (state.extension == nullptr || !state.extension->assets.ReleaseTexturePreview(state.previewCandidate, 0u))
            return false;
        state.previewCandidateInfo = {};
        state.previewCandidateQuality = {};
        return true;
    }

    [[nodiscard]] bool ReleaseDisplayedPreview(AssetBrowserPanelState& state) noexcept
    {
        if ((state.previewTexture.IsValid() || state.previewDisplayed.IsValid()) && state.previewCapturedUiFrame != 0u)
        {
            if (state.extension == nullptr || state.extension->ui == nullptr)
                return false;
            const vanguard::u64 currentUiFrame = state.extension->ui->GetFrame();
            const bool frameAdvanced = currentUiFrame > state.previewCapturedUiFrame;
            const bool capturedFrameFinished =
                currentUiFrame == state.previewCapturedUiFrame &&
                state.extension->ui->GetState() == editor::EditorUiState::Ready;
            if (!frameAdvanced && !capturedFrameFinished)
                return false;
        }
        if (state.previewTexture.IsValid())
        {
            if (state.extension == nullptr || state.extension->ui == nullptr)
                return false;
            editor::EditorUiFailure failure;
            if (!state.extension->ui->UnregisterTexture(state.previewTexture, &failure))
                return false;
            state.previewTexture = {};
        }
        if (state.previewDisplayed.IsValid())
        {
            if (state.extension == nullptr ||
                !state.extension->assets.ReleaseTexturePreview(state.previewDisplayed, state.previewCapturedUiFrame))
                return false;
        }
        state.previewDisplayedInfo = {};
        state.previewDisplayedQuality = {};
        state.previewCapturedUiFrame = 0u;
        state.previewObservedPresentationRevision = 0u;
        return true;
    }

    void ResetSelectedTexturePreview(AssetBrowserPanelState& state) noexcept
    {
        static_cast<void>(ReleasePreviewCandidate(state));
        static_cast<void>(ReleaseDisplayedPreview(state));
        state.previewUnsupportedInfo = {};
        state.previewFailedInfo = {};
        state.previewUnsupportedShape = false;
        state.previewFailed = false;
        state.previewLastRequestUiFrame = 0u;
        state.previewFailedUiFrame = 0u;
        state.previewRequestAttempted = false;
        if (!state.previewCandidate.IsValid() && !state.previewDisplayed.IsValid() && !state.previewTexture.IsValid())
            state.previewOutput = {};
    }

    [[nodiscard]] bool RequestSelectedTexturePreview(AssetBrowserPanelState& state,
                                                     const vanguard::u64 uiFrame,
                                                     const editor::ProjectTexturePreviewQuality quality) noexcept
    {
        if (state.previewCandidate.IsValid() || state.extension == nullptr || !IsTexturePreviewOutput(state.selectedPrimaryOutput))
            return state.previewCandidate.IsValid();
        if (!PreviewRetryDue(state.previewRequestAttempted, state.previewLastRequestUiFrame, uiFrame))
            return false;
        state.previewCandidateInfo = {};
        state.previewRequestAttempted = true;
        state.previewLastRequestUiFrame = uiFrame;
        const editor::ProjectTexturePreviewRequest request =
            MakeTexturePreviewRequest(editor::ProjectTexturePreviewPriority::Focused, quality);
        const bool requested = state.extension->assets.RequestTexturePreview(
            state.selectedPrimaryOutput, request, state.previewCandidate);
        state.previewCandidateQuality = requested ? quality : editor::ProjectTexturePreviewQuality{};
        // A selected texture must not retain an independent Background thumbnail
        // interest on an obsolete generation. Acquire/promote the Focused interest
        // first so a same-publication Ready backing stays shared, then retire the
        // row presentation with its captured UI frame. If Focused admission failed,
        // releasing the low-priority interest creates headroom for the next retry.
        ReleaseTextureThumbnailsForAsset(*state.extension, state.selectedAsset);
        return requested;
    }

    void DrawSelectedTexturePreview(editor::EditorPanelContext& panelContext, AssetBrowserPanelState& state) noexcept
    {
        if (!IsTexturePreviewOutput(state.selectedPrimaryOutput) || state.extension == nullptr || state.extension->ui == nullptr ||
            !state.extension->previewSampler.IsValid())
        {
            ResetSelectedTexturePreview(state);
            if (state.selectedPrimaryOutput.IsValid())
                ImGui::TextDisabled("Preview: selected output is not a texture.");
            return;
        }

        if (state.previewOutput.IsValid() && state.previewOutput != state.selectedPrimaryOutput)
        {
            ResetSelectedTexturePreview(state);
            if (state.previewCandidate.IsValid() || state.previewDisplayed.IsValid() || state.previewTexture.IsValid())
            {
                ImGui::TextDisabled("Preview: retiring the previous selection...");
                return;
            }
        }
        if (!state.previewOutput.IsValid())
            state.previewOutput = state.selectedPrimaryOutput;
        const vanguard::u64 uiFrame = panelContext.GetUi().GetFrame();
        const float availablePreviewWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float maximumPreviewWidth =
            std::min(availablePreviewWidth, editor::detail::EditorVisualPx(480.0f));
        const float maximumPreviewHeight = editor::detail::EditorVisualPx(320.0f);
        const editor::ProjectTexturePreviewQuality initialQuality =
            TexturePreviewPhysicalQuality(maximumPreviewWidth, maximumPreviewHeight);

        assetStreaming::ProjectAssetOutputPublicationStatus publication;
        const bool hasPublicationStatus =
            state.extension->assets.GetOutputPublicationStatus(state.selectedPrimaryOutput, publication);

        if (state.previewUnsupportedShape && hasPublicationStatus &&
            PreviewPublicationDiffers(state.previewUnsupportedInfo, publication))
        {
            state.previewUnsupportedInfo = {};
            state.previewUnsupportedShape = false;
            state.previewRequestAttempted = false;
        }
        if (state.previewFailed && hasPublicationStatus &&
            PreviewPublicationDiffers(state.previewFailedInfo, publication))
        {
            state.previewFailedInfo = {};
            state.previewFailed = false;
            state.previewRequestAttempted = false;
            state.previewFailedUiFrame = 0u;
        }
        else if (state.previewFailed && PreviewRetryDue(true, state.previewFailedUiFrame, uiFrame))
        {
            state.previewFailedInfo = {};
            state.previewFailed = false;
            state.previewRequestAttempted = false;
            state.previewFailedUiFrame = 0u;
        }

        if (state.previewCandidate.IsValid())
        {
            editor::ProjectTexturePreviewInfo candidate;
            if (!state.extension->assets.QueryTexturePreview(state.previewCandidate, candidate))
            {
                state.previewCandidate = {};
                state.previewCandidateInfo = {};
                state.previewCandidateQuality = {};
                state.previewLastRequestUiFrame = uiFrame;
            }
            else
            {
                const ImVec2 candidateExtent =
                    TexturePreviewAspectFitExtent(candidate, maximumPreviewWidth, maximumPreviewHeight);
                const editor::ProjectTexturePreviewQuality candidateQuality =
                    TexturePreviewPhysicalQuality(candidateExtent.x, candidateExtent.y);
                const bool qualityChanged = candidateQuality != state.previewCandidateQuality;
                if (UpdateTexturePreviewDemand(
                    state.extension->assets, state.previewCandidate,
                    editor::ProjectTexturePreviewPriority::Focused,
                    candidateQuality, state.previewCandidateQuality) && qualityChanged)
                {
                    editor::ProjectTexturePreviewInfo updated;
                    if (state.extension->assets.QueryTexturePreview(state.previewCandidate, updated))
                        candidate = updated;
                }
                state.previewCandidateInfo = candidate;
                if (hasPublicationStatus && PreviewPublicationDiffers(candidate, publication))
                {
                    if (ReleasePreviewCandidate(state))
                        state.previewRequestAttempted = false;
                }
                else if (candidate.state == editor::ProjectTexturePreviewState::Failed)
                {
                    state.previewFailedInfo = candidate;
                    state.previewFailed = true;
                    state.previewFailedUiFrame = uiFrame;
                    state.previewLastRequestUiFrame = uiFrame;
                    static_cast<void>(ReleasePreviewCandidate(state));
                }
                else if (candidate.state == editor::ProjectTexturePreviewState::Ready && hasPublicationStatus &&
                         HasPublishedTexturePublication(publication))
                {
                    if (!IsSupportedTexturePreviewShape(candidate))
                    {
                        state.previewUnsupportedInfo = candidate;
                        state.previewUnsupportedShape = true;
                        static_cast<void>(ReleasePreviewCandidate(state));
                    }
                    else
                    {
                        editor::EditorTextureHandle replacement;
                        if (state.extension->previewPresentation.Register(
                                state.previewCandidate, *state.extension->ui, state.extension->previewSampler.GetRef(),
                                candidate.srgb ? editor::EditorTextureColorSpace::DisplaySrgb
                                               : editor::EditorTextureColorSpace::DisplayLinear,
                                replacement))
                        {
                            if (ReleaseDisplayedPreview(state))
                            {
                                state.previewDisplayed = state.previewCandidate;
                                state.previewDisplayedInfo = candidate;
                                state.previewDisplayedQuality = state.previewCandidateQuality;
                                state.previewObservedPresentationRevision = candidate.presentationRevision;
                                state.previewTexture = replacement;
                                state.previewCandidate = {};
                                state.previewCandidateInfo = {};
                                state.previewCandidateQuality = {};
                                state.previewUnsupportedInfo = {};
                                state.previewFailedInfo = {};
                                state.previewUnsupportedShape = false;
                                state.previewFailed = false;
                                state.previewRequestAttempted = false;
                                state.previewFailedUiFrame = 0u;
                                state.previewCapturedUiFrame = 0u;
                            }
                            else
                            {
                                editor::EditorUiFailure failure;
                                static_cast<void>(state.extension->ui->UnregisterTexture(replacement, &failure));
                            }
                        }
                    }
                }
            }
        }

        if (state.previewDisplayed.IsValid())
        {
            editor::ProjectTexturePreviewInfo displayed;
            if (state.extension->assets.QueryTexturePreview(state.previewDisplayed, displayed))
            {
                const ImVec2 displayedExtent =
                    TexturePreviewAspectFitExtent(displayed, maximumPreviewWidth, maximumPreviewHeight);
                const editor::ProjectTexturePreviewQuality displayedQuality =
                    TexturePreviewPhysicalQuality(displayedExtent.x, displayedExtent.y);
                const bool qualityChanged = displayedQuality != state.previewDisplayedQuality;
                if (UpdateTexturePreviewDemand(
                    state.extension->assets, state.previewDisplayed,
                    editor::ProjectTexturePreviewPriority::Focused,
                    displayedQuality, state.previewDisplayedQuality) && qualityChanged)
                {
                    editor::ProjectTexturePreviewInfo updated;
                    if (state.extension->assets.QueryTexturePreview(state.previewDisplayed, updated))
                        displayed = updated;
                }
                if (SameTexturePreviewPublication(state.previewDisplayedInfo, displayed))
                {
                    if (displayed.presentationRevision == state.previewObservedPresentationRevision ||
                        RefreshTexturePreviewRegistration(
                            *state.extension, state.previewDisplayed, displayed, state.previewTexture,
                            state.previewCapturedUiFrame, state.previewObservedPresentationRevision))
                        state.previewDisplayedInfo = displayed;
                }
            }
        }

        if (!state.previewCandidate.IsValid() && !state.previewUnsupportedShape && !state.previewFailed &&
            (!state.previewDisplayed.IsValid() ||
             (hasPublicationStatus && PreviewPublicationDiffers(state.previewDisplayedInfo, publication))))
            static_cast<void>(RequestSelectedTexturePreview(state, uiFrame, initialQuality));

        // The first click frame may already have authored this asset's thumbnail
        // into ImGui before selection changed. That displayed registration cannot
        // be revoked until EndFrame has resolved the texture identity. Retry every
        // selected frame; IsFocusedTextureSelection prevents new same-asset row
        // images, so a BuildingFrame deferral drains on the next frame.
        ReleaseTextureThumbnailsForAsset(*state.extension, state.selectedAsset);

        if (state.previewTexture.IsValid() && state.previewDisplayed.IsValid())
        {
            const ImVec2 displayExtent =
                TexturePreviewAspectFitExtent(state.previewDisplayedInfo, maximumPreviewWidth, maximumPreviewHeight);
            ImGui::Image(editor::detail::ToImGuiTexture(state.previewTexture), displayExtent);
            state.previewCapturedUiFrame = uiFrame;
            ImGui::TextDisabled(
                "Preview: delivered generation %u%s%s", state.previewDisplayedInfo.resourceGeneration,
                state.previewCandidate.IsValid() ? " (showing while update prepares)" : "",
                !state.previewDisplayedInfo.qualitySatisfied ? " (refining quality)" : "");
            using DeliveryState = assetStreaming::ProjectAssetDeliveryStageState;
            if (!hasPublicationStatus)
                ImGui::TextDisabled("Showing the last delivered preview; current publication status is unavailable.");
            else if (
                (publication.buildState != DeliveryState::Committed ||
                 publication.committedBuildFingerprint != publication.publishedBuildFingerprint ||
                 publication.committedContentFingerprint != publication.publishedContentFingerprint ||
                 (publication.publicationState != DeliveryState::Installed &&
                  publication.publicationState != DeliveryState::Stale)))
                ImGui::TextDisabled("Showing the last delivered preview; newer authored/build state is not yet published.");
        }

        if (state.previewUnsupportedShape)
        {
            ImGui::TextDisabled("Preview update: unsupported texture shape (only non-array 2D textures are displayable here).");
            if (state.previewDisplayed.IsValid())
                ImGui::TextDisabled("Showing the last delivered preview; the newer published texture shape is not displayable.");
        }
        else if (state.previewCandidate.IsValid())
            ImGui::TextDisabled("Preview update: %s", TexturePreviewStateName(state.previewCandidateInfo.state));
        else if (state.previewFailed)
        {
            ImGui::TextDisabled("Preview update: Failed");
            if (state.previewDisplayed.IsValid())
                ImGui::TextDisabled("Showing the last delivered preview; the newer published preview failed.");
        }
        else if (!state.previewDisplayed.IsValid())
            ImGui::TextDisabled("Preview: waiting for a committed texture publication.");
    }

    [[nodiscard]] bool ReleaseTextureThumbnailCandidate(AssetBrowserTextureThumbnail& thumbnail) noexcept
    {
        if (!thumbnail.candidate.IsValid())
        {
            thumbnail.candidateInfo = {};
            thumbnail.candidateQuality = {};
            return true;
        }
        if (thumbnail.extension == nullptr ||
            !thumbnail.extension->assets.ReleaseTexturePreview(thumbnail.candidate, 0u))
            return false;
        thumbnail.candidateInfo = {};
        thumbnail.candidateQuality = {};
        return true;
    }

    [[nodiscard]] bool ReleaseTextureThumbnailDisplayed(AssetBrowserTextureThumbnail& thumbnail) noexcept
    {
        AssetBrowserExtensionState* const extension = thumbnail.extension;
        if ((thumbnail.texture.IsValid() || thumbnail.displayed.IsValid()) && thumbnail.capturedUiFrame != 0u)
        {
            if (extension == nullptr || extension->ui == nullptr)
                return false;
            const vanguard::u64 currentUiFrame = extension->ui->GetFrame();
            const bool frameAdvanced = currentUiFrame > thumbnail.capturedUiFrame;
            const bool capturedFrameFinished = currentUiFrame == thumbnail.capturedUiFrame &&
                                               extension->ui->GetState() == editor::EditorUiState::Ready;
            if (!frameAdvanced && !capturedFrameFinished)
                return false;
        }
        if (thumbnail.texture.IsValid())
        {
            if (extension == nullptr || extension->ui == nullptr)
                return false;
            editor::EditorUiFailure failure;
            if (!extension->ui->UnregisterTexture(thumbnail.texture, &failure))
                return false;
            thumbnail.texture = {};
        }
        if (thumbnail.displayed.IsValid())
        {
            if (extension == nullptr ||
                !extension->assets.ReleaseTexturePreview(thumbnail.displayed, thumbnail.capturedUiFrame))
                return false;
        }
        thumbnail.displayedInfo = {};
        thumbnail.displayedQuality = {};
        thumbnail.capturedUiFrame = 0u;
        thumbnail.observedPresentationRevision = 0u;
        return true;
    }

    [[nodiscard]] bool ReleaseTextureThumbnail(AssetBrowserTextureThumbnail& thumbnail) noexcept
    {
        if (!thumbnail.occupied)
            return true;
        if (!ReleaseTextureThumbnailCandidate(thumbnail) || !ReleaseTextureThumbnailDisplayed(thumbnail))
            return false;
        thumbnail = {};
        return true;
    }

    void ResetPanelTextureThumbnails(AssetBrowserPanelState& state) noexcept
    {
        if (state.extension == nullptr)
            return;
        for (AssetBrowserTextureThumbnail& thumbnail : state.extension->thumbnails)
        {
            if (thumbnail.occupied && thumbnail.owner == &state)
            {
                if (!ReleaseTextureThumbnail(thumbnail))
                    thumbnail.owner = nullptr;
            }
        }
    }

    void ResetAllTextureThumbnails(AssetBrowserExtensionState& extension) noexcept
    {
        for (AssetBrowserTextureThumbnail& thumbnail : extension.thumbnails)
        {
            if (thumbnail.occupied)
                static_cast<void>(ReleaseTextureThumbnail(thumbnail));
        }
    }

    [[nodiscard]] AssetBrowserTextureThumbnail* FindTextureThumbnail(AssetBrowserPanelState& state,
                                                                     const assets::AssetId asset) noexcept
    {
        if (state.extension == nullptr || !asset.IsValid())
            return nullptr;
        for (AssetBrowserTextureThumbnail& thumbnail : state.extension->thumbnails)
        {
            if (thumbnail.occupied && thumbnail.owner == &state && thumbnail.asset == asset)
                return &thumbnail;
        }
        return nullptr;
    }

    void ReleaseTextureThumbnailsForAsset(AssetBrowserExtensionState& extension, const assets::AssetId asset) noexcept
    {
        if (!asset.IsValid())
            return;
        for (AssetBrowserTextureThumbnail& thumbnail : extension.thumbnails)
        {
            if (!thumbnail.occupied || thumbnail.asset != asset)
                continue;
            if (!ReleaseTextureThumbnail(thumbnail))
                thumbnail.owner = nullptr;
        }
    }

    [[nodiscard]] bool IsFocusedTextureSelection(const AssetBrowserExtensionState& extension,
                                                 const assets::AssetId asset) noexcept
    {
        if (!asset.IsValid())
            return false;
        const auto focused = [&](const AssetBrowserPanelState* const state,
                                 const editor::EditorPanelHandle panel) noexcept
        {
            return state != nullptr && extension.panels.IsOpen(panel) && state->selectedHasAsset &&
                   state->selectedAsset == asset && IsTexturePreviewOutput(state->selectedPrimaryOutput);
        };
        return focused(extension.primaryState, extension.primaryPanel) ||
               focused(extension.secondaryState, extension.secondaryPanel);
    }

    [[nodiscard]] AssetBrowserTextureThumbnail* AcquireTextureThumbnail(AssetBrowserPanelState& state,
                                                                        const assets::AssetId asset,
                                                                        const vanguard::u64 uiFrame) noexcept
    {
        if (state.extension == nullptr || !asset.IsValid())
            return nullptr;
        if (AssetBrowserTextureThumbnail* const existing = FindTextureThumbnail(state, asset))
        {
            existing->lastVisibleUiFrame = uiFrame;
            return existing;
        }

        AssetBrowserTextureThumbnail* freeRecord = nullptr;
        AssetBrowserTextureThumbnail* oldestOwned = nullptr;
        vanguard::u32 ownedCount = 0u;
        for (AssetBrowserTextureThumbnail& thumbnail : state.extension->thumbnails)
        {
            if (thumbnail.occupied && thumbnail.owner == nullptr && ReleaseTextureThumbnail(thumbnail))
                freeRecord = freeRecord == nullptr ? &thumbnail : freeRecord;
            if (!thumbnail.occupied)
            {
                freeRecord = freeRecord == nullptr ? &thumbnail : freeRecord;
                continue;
            }
            if (thumbnail.owner == &state)
            {
                ++ownedCount;
                if (thumbnail.lastVisibleUiFrame != uiFrame &&
                    (oldestOwned == nullptr || thumbnail.lastVisibleUiFrame < oldestOwned->lastVisibleUiFrame))
                    oldestOwned = &thumbnail;
            }
        }

        AssetBrowserTextureThumbnail* record = ownedCount < AssetBrowserThumbnailCapacityPerPanel ? freeRecord : nullptr;
        if (record == nullptr && oldestOwned != nullptr && ReleaseTextureThumbnail(*oldestOwned))
            record = oldestOwned;
        if (record == nullptr)
            return nullptr;

        *record = {};
        record->extension = state.extension;
        record->owner = &state;
        record->asset = asset;
        record->lastVisibleUiFrame = uiFrame;
        record->occupied = true;
        return record;
    }

    void SweepTextureThumbnails(AssetBrowserPanelState& state, const vanguard::u64 uiFrame) noexcept
    {
        if (state.extension == nullptr)
            return;
        AssetBrowserExtensionState& extension = *state.extension;
        for (vanguard::u32 offset = 0u; offset < AssetBrowserDetachedThumbnailDrainPerFrame; ++offset)
        {
            const vanguard::u32 index = (extension.detachedThumbnailDrainCursor + offset) % AssetBrowserThumbnailCapacity;
            AssetBrowserTextureThumbnail& thumbnail = extension.thumbnails[index];
            if (thumbnail.occupied && thumbnail.owner == nullptr)
                static_cast<void>(ReleaseTextureThumbnail(thumbnail));
        }
        extension.detachedThumbnailDrainCursor =
            (extension.detachedThumbnailDrainCursor + AssetBrowserDetachedThumbnailDrainPerFrame) %
            AssetBrowserThumbnailCapacity;

        for (AssetBrowserTextureThumbnail& thumbnail : state.extension->thumbnails)
        {
            if (!thumbnail.occupied || thumbnail.owner != &state ||
                uiFrame <= thumbnail.lastVisibleUiFrame ||
                uiFrame - thumbnail.lastVisibleUiFrame <= AssetBrowserThumbnailRetentionFrames)
                continue;
            static_cast<void>(ReleaseTextureThumbnail(thumbnail));
        }
    }

    [[nodiscard]] bool DrawTextureThumbnail(editor::EditorPanelContext& panelContext,
                                            AssetBrowserPanelState& state,
                                            const assetStreaming::ProjectAssetCatalogRow& row,
                                            const bool allowRuntimePreview,
                                            const float thumbnailExtent) noexcept
    {
        // Details/list traversal stays metadata-only. Grid callers opt in only
        // after final card layout has resolved the physical thumbnail extent.
        if (!allowRuntimePreview)
            return false;
        if (state.extension == nullptr || state.extension->ui == nullptr || !state.extension->previewSampler.IsValid() ||
            !row.hasAssetIdentity || !row.asset.IsValid() || row.primaryOutputType != textures::TextureResourceType)
            return false;
        if (IsFocusedTextureSelection(*state.extension, row.asset))
            return false;

        const vanguard::u64 uiFrame = panelContext.GetUi().GetFrame();
        const float extent = std::max(16.0f, thumbnailExtent);
        const editor::ProjectTexturePreviewQuality thumbnailQuality =
            TexturePreviewPhysicalQuality(extent, extent);
        AssetBrowserTextureThumbnail* const thumbnail = AcquireTextureThumbnail(state, row.asset, uiFrame);
        if (thumbnail == nullptr)
            return false;

        if (!thumbnail->output.IsValid() && !thumbnail->terminal)
        {
            bool ambiguous = false;
            if (!state.extension->assets.ResolveUniqueOutputOfType(
                    row.asset, textures::TextureResourceType, thumbnail->output, ambiguous) || ambiguous ||
                !IsTexturePreviewOutput(thumbnail->output))
                thumbnail->terminal = true;
        }

        assetStreaming::ProjectAssetOutputPublicationStatus publication;
        const bool hasPublicationStatus = thumbnail->output.IsValid() &&
            state.extension->assets.GetOutputPublicationStatus(thumbnail->output, publication);
        if (thumbnail->publicationSuppressed &&
            ((hasPublicationStatus && PreviewPublicationDiffers(thumbnail->suppressedInfo, publication)) ||
             (thumbnail->suppressedFailure && PreviewRetryDue(true, thumbnail->suppressedUiFrame, uiFrame))))
        {
            thumbnail->suppressedInfo = {};
            thumbnail->publicationSuppressed = false;
            thumbnail->suppressedFailure = false;
            thumbnail->suppressedUiFrame = 0u;
            thumbnail->requestAttempted = false;
        }

        if (thumbnail->candidate.IsValid())
        {
            editor::ProjectTexturePreviewInfo candidate;
            if (!state.extension->assets.QueryTexturePreview(thumbnail->candidate, candidate))
            {
                thumbnail->candidate = {};
                thumbnail->candidateInfo = {};
                thumbnail->candidateQuality = {};
            }
            else
            {
                const bool qualityChanged = thumbnailQuality != thumbnail->candidateQuality;
                if (UpdateTexturePreviewDemand(
                    state.extension->assets, thumbnail->candidate,
                    editor::ProjectTexturePreviewPriority::Background,
                    thumbnailQuality, thumbnail->candidateQuality) && qualityChanged)
                {
                    editor::ProjectTexturePreviewInfo updated;
                    if (state.extension->assets.QueryTexturePreview(thumbnail->candidate, updated))
                        candidate = updated;
                }
                thumbnail->candidateInfo = candidate;
                if (hasPublicationStatus && PreviewPublicationDiffers(candidate, publication))
                {
                    if (ReleaseTextureThumbnailCandidate(*thumbnail))
                        thumbnail->requestAttempted = false;
                }
                else if (candidate.state == editor::ProjectTexturePreviewState::Failed)
                {
                    thumbnail->suppressedInfo = candidate;
                    thumbnail->publicationSuppressed = true;
                    thumbnail->suppressedFailure = true;
                    thumbnail->suppressedUiFrame = uiFrame;
                    thumbnail->lastRequestUiFrame = uiFrame;
                    static_cast<void>(ReleaseTextureThumbnailCandidate(*thumbnail));
                }
                else if (candidate.state == editor::ProjectTexturePreviewState::Ready && hasPublicationStatus &&
                         HasPublishedTexturePublication(publication))
                {
                    if (!IsSupportedTexturePreviewShape(candidate))
                    {
                        thumbnail->suppressedInfo = candidate;
                        thumbnail->publicationSuppressed = true;
                        thumbnail->suppressedFailure = false;
                        thumbnail->suppressedUiFrame = 0u;
                        static_cast<void>(ReleaseTextureThumbnailCandidate(*thumbnail));
                    }
                    else
                    {
                        editor::EditorTextureHandle replacement;
                        if (state.extension->previewPresentation.Register(
                                thumbnail->candidate, *state.extension->ui, state.extension->previewSampler.GetRef(),
                                candidate.srgb ? editor::EditorTextureColorSpace::DisplaySrgb
                                               : editor::EditorTextureColorSpace::DisplayLinear,
                                replacement))
                        {
                            if (ReleaseTextureThumbnailDisplayed(*thumbnail))
                            {
                                thumbnail->displayed = thumbnail->candidate;
                                thumbnail->displayedInfo = candidate;
                                thumbnail->displayedQuality = thumbnail->candidateQuality;
                                thumbnail->observedPresentationRevision = candidate.presentationRevision;
                                thumbnail->texture = replacement;
                                thumbnail->candidate = {};
                                thumbnail->candidateInfo = {};
                                thumbnail->candidateQuality = {};
                                thumbnail->suppressedInfo = {};
                                thumbnail->publicationSuppressed = false;
                                thumbnail->suppressedFailure = false;
                                thumbnail->suppressedUiFrame = 0u;
                                thumbnail->requestAttempted = false;
                                thumbnail->capturedUiFrame = 0u;
                            }
                            else
                            {
                                editor::EditorUiFailure failure;
                                static_cast<void>(state.extension->ui->UnregisterTexture(replacement, &failure));
                            }
                        }
                    }
                }
            }
        }

        if (thumbnail->displayed.IsValid())
        {
            editor::ProjectTexturePreviewInfo displayed;
            if (state.extension->assets.QueryTexturePreview(thumbnail->displayed, displayed))
            {
                const bool qualityChanged = thumbnailQuality != thumbnail->displayedQuality;
                if (UpdateTexturePreviewDemand(
                    state.extension->assets, thumbnail->displayed,
                    editor::ProjectTexturePreviewPriority::Background,
                    thumbnailQuality, thumbnail->displayedQuality) && qualityChanged)
                {
                    editor::ProjectTexturePreviewInfo updated;
                    if (state.extension->assets.QueryTexturePreview(thumbnail->displayed, updated))
                        displayed = updated;
                }
                if (SameTexturePreviewPublication(thumbnail->displayedInfo, displayed))
                {
                    if (displayed.presentationRevision == thumbnail->observedPresentationRevision ||
                        RefreshTexturePreviewRegistration(
                            *state.extension, thumbnail->displayed, displayed, thumbnail->texture,
                            thumbnail->capturedUiFrame, thumbnail->observedPresentationRevision))
                        thumbnail->displayedInfo = displayed;
                }
            }
        }

        if (!thumbnail->candidate.IsValid() && !thumbnail->terminal && !thumbnail->publicationSuppressed &&
            (!thumbnail->displayed.IsValid() ||
             (hasPublicationStatus && PreviewPublicationDiffers(thumbnail->displayedInfo, publication))) &&
            PreviewRetryDue(thumbnail->requestAttempted, thumbnail->lastRequestUiFrame, uiFrame))
        {
            thumbnail->requestAttempted = true;
            thumbnail->lastRequestUiFrame = uiFrame;
            const editor::ProjectTexturePreviewRequest request =
                MakeTexturePreviewRequest(editor::ProjectTexturePreviewPriority::Background, thumbnailQuality);
            if (state.extension->assets.RequestTexturePreview(
                    thumbnail->output, request, thumbnail->candidate))
                thumbnail->candidateQuality = thumbnailQuality;
        }

        if (!thumbnail->texture.IsValid() || thumbnail->terminal)
            return false;
        float width = extent;
        float height = extent;
        if (thumbnail->displayedInfo.width != 0u && thumbnail->displayedInfo.height != 0u)
        {
            const float aspect = static_cast<float>(thumbnail->displayedInfo.width) /
                                 static_cast<float>(thumbnail->displayedInfo.height);
            if (aspect > 1.0f)
                height = extent / aspect;
            else if (aspect > 0.0f)
                width = extent * aspect;
        }
        const ImVec2 slotMinimum = ImGui::GetCursorScreenPos();
        ImGui::Dummy({extent, extent});
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 imageMinimum{slotMinimum.x + (extent - width) * 0.5f,
                                  slotMinimum.y + (extent - height) * 0.5f};
        ImGui::GetWindowDrawList()->AddImage(editor::detail::ToImGuiTexture(thumbnail->texture), imageMinimum,
                                             {imageMinimum.x + width, imageMinimum.y + height});
        using DeliveryState = assetStreaming::ProjectAssetDeliveryStageState;
        const bool publishedDiffers = hasPublicationStatus &&
                                      PreviewPublicationDiffers(thumbnail->displayedInfo, publication);
        const bool newerStatePending = hasPublicationStatus &&
                                       (publication.buildState != DeliveryState::Committed ||
                                        publication.committedBuildFingerprint != publication.publishedBuildFingerprint ||
                                        publication.committedContentFingerprint != publication.publishedContentFingerprint ||
                                        (publication.publicationState != DeliveryState::Installed &&
                                         publication.publicationState != DeliveryState::Stale));
        const bool showingOlder = !hasPublicationStatus || thumbnail->candidate.IsValid() ||
                                  thumbnail->publicationSuppressed || publishedDiffers || newerStatePending;
        if (showingOlder)
        {
            constexpr char Marker[] = "!";
            const ImVec2 markerSize = ImGui::CalcTextSize(Marker);
            ImGui::GetWindowDrawList()->AddText(
                {slotMinimum.x + extent - markerSize.x, slotMinimum.y}, ImGui::GetColorU32(ImGuiCol_Text), Marker);
            if (hovered)
            {
                if (!hasPublicationStatus)
                    ImGui::SetTooltip("Showing the last delivered thumbnail; current publication status is unavailable.");
                else if (thumbnail->publicationSuppressed && thumbnail->suppressedFailure)
                    ImGui::SetTooltip("Showing the last delivered thumbnail; the newer published preview failed.");
                else if (thumbnail->publicationSuppressed)
                    ImGui::SetTooltip("Showing the last delivered thumbnail; the newer texture shape is not displayable.");
                else
                    ImGui::SetTooltip("Showing the last delivered thumbnail while a newer authored/published state converges.");
            }
        }
        thumbnail->capturedUiFrame = uiFrame;
        return true;
    }

    void ClearSelection(AssetBrowserPanelState& state, const bool markPersistent = true) noexcept
    {
        const bool hadPersistentSelection = state.selectionIntent != AssetBrowserSelectionIntent::None;
        ResetSelectedTexturePreview(state);
        state.selectedRow = {};
        state.selectedAsset = {};
        state.selectedContentFolder = {};
        state.selectedContentFolderRoot = 0u;
        state.selectedContentFolderPathBytes = 0u;
        state.selectedContentFolderPath[0] = '\0';
        state.selectedSourceIdentity = {};
        state.selectedPrimaryOutput = {};
        state.selectedFreshness = {};
        state.textureSettingsSnapshot = {};
        state.textureSettings = {};
        state.authoredTextureSettings = {};
        state.selectedFreshnessResult = assetStreaming::ProjectAssetPipelineResult::InvalidState;
        state.selectedSourceGeneration = 0u;
        state.selectedAcceptedGeneration = 0u;
        state.selectedImporter = assets::InvalidSourceImporterId;
        state.selectedPrimaryOutputType = resources::InvalidResourceTypeId;
        state.selectedImporterVersion = 0u;
        state.selectedImportState = assets::SourceImportState::NotClassified;
        state.selectedHasAsset = false;
        state.selectedReadOnly = false;
        state.selectedPrimaryOutputAmbiguous = false;
        state.selectedFreshnessEvaluated = false;
        state.mutationTarget[0] = '\0';
        state.targetedOperation = AssetBrowserTargetedOperationKind::None;
        state.targetedOperationOpenRequested = false;
        state.deleteArmed = false;
        state.deleteOpenRequested = false;
        state.textureSettingsLoaded = false;
        state.textureSettingsSupported = false;
        state.textureSettingsDirty = false;
        state.dependencyPageOffset = 0u;
        state.referencerPageOffset = 0u;
        state.outputPageOffset = 0u;
        state.selectionIntent = AssetBrowserSelectionIntent::None;
        if (markPersistent && hadPersistentSelection)
            MarkAssetBrowserPersistentStateDirty(state);
    }

    void ClearSelectionOnContentBackgroundClick(AssetBrowserPanelState& state) noexcept
    {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemHovered())
            return;
        ClearSelection(state);
    }

    [[nodiscard]] bool SelectContentFolder(AssetBrowserPanelState& state,
                                           const assetStreaming::ProjectAssetCatalog& catalog,
                                           const assetStreaming::ProjectAssetCatalogFolderId folder) noexcept
    {
        assetStreaming::ProjectAssetCatalogFolderInfo info;
        char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        vanguard::u32 pathBytes = 0u;
        if (!catalog.GetFolderInfo(folder, info) ||
            !catalog.CopyFolderRelativePath(
                folder, {path, static_cast<vanguard::u32>(sizeof(path))}, pathBytes))
            return false;

        const bool persistentChanged =
            state.selectionIntent != AssetBrowserSelectionIntent::Folder ||
            state.selectedContentFolderRoot != info.root ||
            state.selectedContentFolderPathBytes != pathBytes ||
            std::memcmp(state.selectedContentFolderPath, path, pathBytes) != 0;
        ClearSelection(state, false);
        state.selectedContentFolder = folder;
        state.selectedContentFolderRoot = info.root;
        state.selectedContentFolderPathBytes = pathBytes;
        if (pathBytes != 0u)
            std::memcpy(state.selectedContentFolderPath, path, pathBytes);
        state.selectedContentFolderPath[pathBytes] = '\0';
        state.selectionIntent = AssetBrowserSelectionIntent::Folder;
        if (persistentChanged)
            MarkAssetBrowserPersistentStateDirty(state);
        return true;
    }

    [[nodiscard]] bool SelectRow(AssetBrowserPanelState& state,
                                 const assetStreaming::ProjectAssetCatalogRowId id,
                                 const assetStreaming::ProjectAssetCatalogRow& row) noexcept
    {
        const bool persistentChanged = row.hasAssetIdentity && row.asset.IsValid()
                                           ? state.selectionIntent != AssetBrowserSelectionIntent::Asset ||
                                                 state.selectedAsset != row.asset
                                           : state.selectionIntent != AssetBrowserSelectionIntent::None;
        ResetSelectedTexturePreview(state);
        state.selectedContentFolder = {};
        state.selectedContentFolderRoot = 0u;
        state.selectedContentFolderPathBytes = 0u;
        state.selectedContentFolderPath[0] = '\0';
        state.selectedRow = id;
        state.selectedAsset = row.asset;
        state.selectedSourceIdentity = row.sourceIdentity;
        state.selectedSourceGeneration = state.catalogGeneration;
        state.selectedAcceptedGeneration = 0u;
        state.selectedImporter = row.importer;
        state.selectedPrimaryOutputType = row.primaryOutputType;
        state.selectedImporterVersion = row.availableImporterVersion;
        state.selectedImportState = row.importState;
        state.selectedReadOnly = row.rootReadOnly;
        state.selectedPrimaryOutput = {};
        state.selectedPrimaryOutputAmbiguous = false;
        if (row.hasAssetIdentity && state.extension != nullptr)
        {
            static_cast<void>(state.extension->assets.ResolveUniqueOutputOfType(
                row.asset, row.primaryOutputType, state.selectedPrimaryOutput,
                state.selectedPrimaryOutputAmbiguous));
            static_cast<void>(state.extension->assets.GetAcceptedAssetGeneration(
                row.asset, state.selectedAcceptedGeneration));
        }
        state.selectedHasAsset = row.hasAssetIdentity;
        state.selectionIntent = row.hasAssetIdentity && row.asset.IsValid()
                                    ? AssetBrowserSelectionIntent::Asset
                                    : AssetBrowserSelectionIntent::None;
        state.selectedFreshness = {};
        state.selectedFreshnessResult = assetStreaming::ProjectAssetPipelineResult::InvalidState;
        state.selectedFreshnessEvaluated = false;
        state.mutationTarget[0] = '\0';
        state.targetedOperation = AssetBrowserTargetedOperationKind::None;
        state.targetedOperationOpenRequested = false;
        state.deleteArmed = false;
        state.deleteOpenRequested = false;
        state.textureSettingsSnapshot = {};
        state.textureSettings = {};
        state.authoredTextureSettings = {};
        state.textureSettingsLoaded = false;
        state.textureSettingsSupported = false;
        state.textureSettingsDirty = false;
        state.dependencyPageOffset = 0u;
        state.referencerPageOffset = 0u;
        state.outputPageOffset = 0u;
        if (persistentChanged)
            MarkAssetBrowserPersistentStateDirty(state);
        return true;
    }

    [[nodiscard]] bool RebindSelectedRow(AssetBrowserPanelState& state,
                                         const assetStreaming::ProjectAssetCatalogRowId id,
                                         const assetStreaming::ProjectAssetCatalogRow& row) noexcept
    {
        if (state.extension == nullptr || !state.selectedHasAsset || !state.selectedAsset.IsValid() ||
            row.asset != state.selectedAsset || !row.hasAssetIdentity)
            return SelectRow(state, id, row);

        vanguard::u64 acceptedGeneration = 0u;
        resources::ResourceReference primaryOutput;
        bool primaryOutputAmbiguous = false;
        static_cast<void>(state.extension->assets.ResolveUniqueOutputOfType(
            row.asset, row.primaryOutputType, primaryOutput, primaryOutputAmbiguous));
        const bool sameAcceptedAsset =
            state.selectedAcceptedGeneration != 0u &&
            state.extension->assets.GetAcceptedAssetGeneration(row.asset, acceptedGeneration) &&
            acceptedGeneration == state.selectedAcceptedGeneration &&
            row.sourceIdentity == state.selectedSourceIdentity &&
            row.importer == state.selectedImporter &&
            row.primaryOutputType == state.selectedPrimaryOutputType &&
            row.availableImporterVersion == state.selectedImporterVersion &&
            row.importState == state.selectedImportState &&
            row.rootReadOnly == state.selectedReadOnly &&
            primaryOutput == state.selectedPrimaryOutput &&
            primaryOutputAmbiguous == state.selectedPrimaryOutputAmbiguous;
        if (!sameAcceptedAsset)
            return SelectRow(state, id, row);

        // The catalog publication changed because some project source changed, but
        // this accepted asset did not. Rebind only the borrowed row/generation
        // identity and retain panel-local edits, mutation text and live preview.
        // Build freshness is still invalidated because dependency changes elsewhere
        // in the project may make this asset dirty without changing its accepted
        // source state.
        state.selectedRow = id;
        state.selectedSourceGeneration = state.catalogGeneration;
        state.selectedFreshness = {};
        state.selectedFreshnessResult = assetStreaming::ProjectAssetPipelineResult::InvalidState;
        state.selectedFreshnessEvaluated = false;
        return true;
    }

    [[nodiscard]] bool ApplyPanelNavigation(
        AssetBrowserPanelState& state, const editor::detail::AssetBrowserNavigationResult result,
        const bool revealTree = true) noexcept
    {
        if (result == editor::detail::AssetBrowserNavigationResult::Changed)
        {
            ResetAssetBrowserContentScrollIntent(state);
            if (revealTree)
                editor::detail::RevealAssetBrowserTreeLocation(
                    state.tree, state.root, vanguard::containers::StringView(state.folderPath));
            ClearSelection(state);
            MarkAssetBrowserPersistentStateDirty(state);
            return true;
        }
        return result == editor::detail::AssetBrowserNavigationResult::Unchanged;
    }

    [[nodiscard]] bool NavigatePanelToFolder(
        AssetBrowserPanelState& state, const assetStreaming::ProjectAssetCatalog& catalog,
        const vanguard::u32 root, const vanguard::containers::StringView path) noexcept
    {
        return ApplyPanelNavigation(
            state, editor::detail::NavigateAssetBrowserToFolder(state, catalog, root, path));
    }

    [[nodiscard]] bool NavigatePanelToParentFolder(
        AssetBrowserPanelState& state, const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        return ApplyPanelNavigation(
            state, editor::detail::NavigateAssetBrowserToParentFolder(state, catalog));
    }

    [[nodiscard]] bool NavigatePanelToFolderId(
        AssetBrowserPanelState& state, const assetStreaming::ProjectAssetCatalog& catalog,
        const assetStreaming::ProjectAssetCatalogFolderId folder, const bool revealTree = true) noexcept
    {
        assetStreaming::ProjectAssetCatalogFolderInfo info;
        char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        vanguard::u32 pathBytes = 0u;
        if (!catalog.GetFolderInfo(folder, info) ||
            !catalog.CopyFolderRelativePath(
                folder, {path, static_cast<vanguard::u32>(sizeof(path))}, pathBytes))
            return false;
        return ApplyPanelNavigation(
            state,
            editor::detail::NavigateAssetBrowserToFolder(
                state, catalog, info.root, vanguard::containers::StringView(path, pathBytes)),
            revealTree);
    }

    void DrawFolderBreadcrumb(AssetBrowserPanelState& state,
                              const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        char rootLabel[64]{};
        FormatSourceRootName(state, state.root, rootLabel, sizeof(rootLabel));
        if (ImGui::SmallButton(rootLabel))
        {
            static_cast<void>(NavigatePanelToFolder(state, catalog, state.root, {}));
            return;
        }

        const vanguard::u32 pathLength = static_cast<vanguard::u32>(std::strlen(state.folderPath));
        vanguard::u32 segmentStart = 0u;
        while (segmentStart < pathLength)
        {
            vanguard::u32 segmentEnd = segmentStart;
            while (segmentEnd < pathLength && state.folderPath[segmentEnd] != '/')
                ++segmentEnd;
            if (segmentEnd != segmentStart)
            {
                char segment[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                const vanguard::u32 bytes = std::min<vanguard::u32>(segmentEnd - segmentStart, sizeof(segment) - 1u);
                std::memcpy(segment, state.folderPath + segmentStart, bytes);
                segment[bytes] = '\0';
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(segmentStart));
                if (ImGui::SmallButton(segment))
                {
                    static_cast<void>(NavigatePanelToFolder(
                        state, catalog, state.root,
                        vanguard::containers::StringView(state.folderPath, segmentEnd)));
                    ImGui::PopID();
                    return;
                }
                ImGui::PopID();
            }
            segmentStart = segmentEnd + 1u;
        }
    }

    [[nodiscard]] bool SyncCatalog(AssetBrowserPanelState& state,
                                   const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        const vanguard::u64 previousGeneration = state.catalogGeneration;
        const vanguard::u32 previousRoot = state.root;
        char previousFolderPath[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 previousFolderPathBytes =
            static_cast<vanguard::u32>(std::strlen(state.folderPath));
        if (previousFolderPathBytes != 0u)
            std::memcpy(previousFolderPath, state.folderPath, previousFolderPathBytes);
        const editor::detail::AssetBrowserCatalogSyncPolicyResult sync =
            editor::detail::SyncAssetBrowserBrowsePolicy(state, catalog);
        if (!sync.ready)
            return false;
        const bool navigationChanged =
            previousRoot != state.root || previousFolderPathBytes != std::strlen(state.folderPath) ||
            std::memcmp(previousFolderPath, state.folderPath,
                        std::min<vanguard::u32>(previousFolderPathBytes,
                                                static_cast<vanguard::u32>(std::strlen(state.folderPath)))) != 0;
        if (navigationChanged)
        {
            ResetAssetBrowserContentScrollIntent(state);
            MarkAssetBrowserPersistentStateDirty(state);
        }

        if (!state.tree.initialized)
        {
            state.tree.initialized = true;
            const vanguard::u32 expandedBeforeReveal = state.tree.expanded.Size();
            editor::detail::RevealAssetBrowserTreeLocation(
                state.tree, state.root, vanguard::containers::StringView(state.folderPath));
            if (state.tree.expanded.Size() != expandedBeforeReveal)
                MarkAssetBrowserPersistentStateDirty(state);
        }
        if (previousGeneration != state.catalogGeneration)
        {
            const vanguard::u32 expandedBeforePrune = state.tree.expanded.Size();
            editor::detail::PruneAssetBrowserTreeExpansions(state.tree, catalog);
            if (state.tree.expanded.Size() != expandedBeforePrune)
                MarkAssetBrowserPersistentStateDirty(state);
            state.tree.dirty = true;
            if (state.selectionIntent == AssetBrowserSelectionIntent::Folder)
            {
                const assetStreaming::ProjectAssetCatalogFolderId rebound = catalog.ResolveFolder(
                    state.selectedContentFolderRoot,
                    vanguard::containers::StringView(
                        state.selectedContentFolderPath, state.selectedContentFolderPathBytes));
                if (rebound.IsValid())
                    state.selectedContentFolder = rebound;
                else
                    ClearSelection(state);
            }
        }

        if (sync.selection == editor::detail::AssetBrowserSelectionRefreshPolicy::Clear)
        {
            ClearSelection(state);
        }
        else if (sync.selection == editor::detail::AssetBrowserSelectionRefreshPolicy::Rebind)
        {
            const assetStreaming::ProjectAssetCatalogRow* const row = catalog.GetRow(sync.rebindRow);
            if (row == nullptr || row->asset != state.selectedAsset || !RebindSelectedRow(state, sync.rebindRow, *row))
                ClearSelection(state);
        }
        return true;
    }

    void BeginPanelQuery(AssetBrowserPanelState& state, const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        static_cast<void>(editor::detail::BeginAssetBrowserBrowseQuery(state, catalog));
    }

    void DrawFolderImportContext(AssetBrowserPanelState& state,
                                 const assetStreaming::ProjectAssetCatalog& catalog,
                                 const assetStreaming::ProjectAssetCatalogFolderId folder,
                                 const char* const popupId) noexcept
    {
        if (!ImGui::BeginPopupContextItem(popupId))
            return;

        assetStreaming::ProjectAssetCatalogFolderInfo info;
        char destination[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        vanguard::u32 destinationBytes = 0u;
        const bool validDestination = catalog.GetFolderInfo(folder, info) &&
                                      catalog.CopyFolderRelativePath(
                                          folder, {destination, static_cast<vanguard::u32>(sizeof(destination))},
                                          destinationBytes);
        const bool canImport = validDestination && state.extension != nullptr &&
                               state.extension->assets.CanImportExternalSource(
                                   info.root, vanguard::containers::StringView(destination, destinationBytes));
        if (ImGui::MenuItem("Import...", nullptr, false, canImport))
            static_cast<void>(BeginExternalImport(
                state, info.root, vanguard::containers::StringView(destination, destinationBytes)));
        ImGui::EndPopup();
    }

    void DrawAssetBrowserFolderTree(AssetBrowserPanelState& state,
                                    const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        const assetStreaming::ProjectAssetCatalogStatus status = catalog.GetStatus();
        if (state.tree.dirty || state.tree.visibleGeneration != status.sourceGeneration)
            static_cast<void>(editor::detail::RebuildAssetBrowserTreeVisible(state.tree, catalog));

        ImGui::BeginChild("##AssetBrowserFolderTree", ImVec2(0.0f, 0.0f), false);
        if (state.tree.scrollToSelection)
        {
            vanguard::u32 selectedIndex = ~vanguard::u32{0};
            for (vanguard::u32 index = 0u; index < state.tree.visible.Size(); ++index)
            {
                if (state.tree.visible[index].folder == state.folder)
                {
                    selectedIndex = index;
                    break;
                }
            }
            if (selectedIndex != ~vanguard::u32{0})
            {
                const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                const float viewportHeight = ImGui::GetContentRegionAvail().y;
                const float target = static_cast<float>(selectedIndex) * rowHeight - viewportHeight * 0.5f;
                ImGui::SetScrollY(std::max(0.0f, target));
            }
            state.tree.scrollToSelection = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.tree.visible.Size()));
        while (clipper.Step())
        {
            for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex)
            {
                const editor::detail::AssetBrowserTreeVisibleFolder& node =
                    state.tree.visible[static_cast<vanguard::u32>(visibleIndex)];
                char label[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                if (node.depth == 0u)
                {
                    FormatSourceRootName(state, node.root, label, sizeof(label));
                }
                else
                {
                    const vanguard::containers::StringView name = catalog.GetFolderName(node.folder);
                    const vanguard::u32 bytes = std::min<vanguard::u32>(name.Length(), sizeof(label) - 1u);
                    if (bytes != 0u)
                        std::memcpy(label, name.Data(), bytes);
                    label[bytes] = '\0';
                }

                ImGui::PushID(static_cast<int>(node.folder.value));
                if (node.depth != 0u)
                    ImGui::Indent(static_cast<float>(node.depth) * ImGui::GetStyle().IndentSpacing);

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (node.childFolderCount == 0u)
                    flags |= ImGuiTreeNodeFlags_Leaf;
                const bool selected = node.folder == state.folder;
                const bool highlightExpanded = node.expanded && !selected;
                if (selected || highlightExpanded)
                    flags |= ImGuiTreeNodeFlags_Selected;
                if (node.childFolderCount != 0u)
                    ImGui::SetNextItemOpen(node.expanded, ImGuiCond_Always);
                if (state.tree.focusSelection && node.folder == state.folder)
                {
                    ImGui::SetKeyboardFocusHere();
                    state.tree.focusSelection = false;
                }
                if (highlightExpanded)
                {
                    ImVec4 expandedHeader = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                    expandedHeader.w *= AssetBrowserExpandedFolderHeaderAlpha;
                    ImGui::PushStyleColor(ImGuiCol_Header, expandedHeader);
                }
                const bool open = ImGui::TreeNodeEx(
                    label[0] != '\0' ? label : "<folder>", flags);
                if (highlightExpanded)
                    ImGui::PopStyleColor();
                const bool toggled = ImGui::IsItemToggledOpen();
                if (toggled && node.childFolderCount != 0u && open != node.expanded)
                {
                    char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                    vanguard::u32 pathBytes = 0u;
                    if (catalog.CopyFolderRelativePath(
                            node.folder, {path, static_cast<vanguard::u32>(sizeof(path))}, pathBytes))
                    {
                        const vanguard::containers::StringView location(path, pathBytes);
                        const bool wasExpanded = editor::detail::IsAssetBrowserTreeFolderExpanded(
                            state.tree, node.root, location);
                        if (editor::detail::SetAssetBrowserTreeFolderExpanded(
                                state.tree, node.root, location, open) && wasExpanded != open)
                            MarkAssetBrowserPersistentStateDirty(state);
                    }
                }

                const bool activate =
                    (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !toggled) ||
                    (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && node.folder != state.folder)
                    static_cast<void>(NavigatePanelToFolderId(state, catalog, node.folder, false));

                if (ImGui::IsItemFocused())
                {
                    const vanguard::u32 index = static_cast<vanguard::u32>(visibleIndex);
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && index != 0u)
                    {
                        static_cast<void>(NavigatePanelToFolderId(
                            state, catalog, state.tree.visible[index - 1u].folder, false));
                        state.tree.scrollToSelection = true;
                        state.tree.focusSelection = true;
                    }
                    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && index + 1u < state.tree.visible.Size())
                    {
                        static_cast<void>(NavigatePanelToFolderId(
                            state, catalog, state.tree.visible[index + 1u].folder, false));
                        state.tree.scrollToSelection = true;
                        state.tree.focusSelection = true;
                    }
                    else if (!toggled && ImGui::IsKeyPressed(ImGuiKey_RightArrow) &&
                             node.childFolderCount != 0u && !node.expanded)
                    {
                        char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                        vanguard::u32 pathBytes = 0u;
                        if (catalog.CopyFolderRelativePath(
                                node.folder, {path, static_cast<vanguard::u32>(sizeof(path))}, pathBytes))
                        {
                            const vanguard::containers::StringView location(path, pathBytes);
                            if (!editor::detail::IsAssetBrowserTreeFolderExpanded(state.tree, node.root, location) &&
                                editor::detail::SetAssetBrowserTreeFolderExpanded(
                                    state.tree, node.root, location, true))
                                MarkAssetBrowserPersistentStateDirty(state);
                        }
                    }
                    else if (!toggled && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    {
                        if (node.childFolderCount != 0u && node.expanded)
                        {
                            char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
                            vanguard::u32 pathBytes = 0u;
                            if (catalog.CopyFolderRelativePath(
                                    node.folder, {path, static_cast<vanguard::u32>(sizeof(path))}, pathBytes))
                            {
                                const vanguard::containers::StringView location(path, pathBytes);
                                if (editor::detail::IsAssetBrowserTreeFolderExpanded(state.tree, node.root, location) &&
                                    editor::detail::SetAssetBrowserTreeFolderExpanded(
                                        state.tree, node.root, location, false))
                                    MarkAssetBrowserPersistentStateDirty(state);
                            }
                        }
                        else if (node.depth != 0u)
                        {
                            assetStreaming::ProjectAssetCatalogFolderInfo info;
                            if (catalog.GetFolderInfo(node.folder, info) && info.parent.IsValid())
                            {
                                static_cast<void>(NavigatePanelToFolderId(state, catalog, info.parent, false));
                                state.tree.scrollToSelection = true;
                                state.tree.focusSelection = true;
                            }
                        }
                    }
                }
                DrawFolderImportContext(state, catalog, node.folder, "##AssetTreeFolderContext");
                if (activate)
                    static_cast<void>(NavigatePanelToFolderId(state, catalog, node.folder, false));

                if (node.depth != 0u)
                    ImGui::Unindent(static_cast<float>(node.depth) * ImGui::GetStyle().IndentSpacing);
                ImGui::PopID();
            }
        }
        if (state.tree.truncated)
            ImGui::TextDisabled("Folder tree truncated at %u visible folders.",
                                editor::detail::MaximumAssetBrowserTreeVisibleFolders);
        if (state.tree.expansionLimitReached)
            ImGui::TextDisabled("Folder expansion limit reached (%u branches).",
                                editor::detail::MaximumAssetBrowserTreeExpandedFolders);
        if (state.treePersistenceTruncated)
            ImGui::TextDisabled("Restored folder expansion state was truncated to the session-state byte budget.");
        TrackAssetBrowserScrollIntent(
            state, state.treeScrollFraction, state.treeScrollRestorePending,
            state.treeScrollRestoreGeometryReady, true);
        ImGui::EndChild();
    }

    struct ContentFolderDrawContext final
    {
        AssetBrowserPanelState* state = nullptr;
        const assetStreaming::ProjectAssetCatalog* catalog = nullptr;
        assetStreaming::ProjectAssetCatalogFolderId selected;
    };

    void DrawContentPlaceholder(const char* const label) noexcept
    {
        constexpr float Extent = 24.0f;
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        ImGui::Dummy({Extent, Extent});
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        draw->AddRect(minimum, {minimum.x + Extent, minimum.y + Extent}, ImGui::GetColorU32(ImGuiCol_Border), 2.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText({minimum.x + (Extent - textSize.x) * 0.5f,
                       minimum.y + (Extent - textSize.y) * 0.5f},
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }

    bool DrawContentFolder(const assetStreaming::ProjectAssetCatalogFolderInfo& folder,
                           const vanguard::containers::StringView name, void* const userData) noexcept
    {
        auto& context = *static_cast<ContentFolderDrawContext*>(userData);
        char label[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 labelBytes = std::min<vanguard::u32>(name.Length(), sizeof(label) - 1u);
        if (labelBytes != 0u)
            std::memcpy(label, name.Data(), labelBytes);
        label[labelBytes] = '\0';

        ImGui::PushID(static_cast<int>(folder.id.value));
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
        ImGui::TableSetColumnIndex(0);
        DrawContentPlaceholder("D");
        ImGui::TableSetColumnIndex(1);
        const bool selected = context.state != nullptr && context.state->selectedContentFolder == folder.id;
        const bool activated = ImGui::Selectable(labelBytes == 0u ? "<folder>" : label, selected,
                                                 ImGuiSelectableFlags_SpanAllColumns |
                                                     ImGuiSelectableFlags_AllowDoubleClick);
        const bool open = (activated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
                          (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
        if (activated && context.state != nullptr && context.catalog != nullptr)
            static_cast<void>(SelectContentFolder(*context.state, *context.catalog, folder.id));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && context.state != nullptr && context.catalog != nullptr)
            static_cast<void>(SelectContentFolder(*context.state, *context.catalog, folder.id));
        if (context.state != nullptr && context.catalog != nullptr)
            DrawFolderImportContext(*context.state, *context.catalog, folder.id, "##AssetContentFolderContext");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("Folder");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("-");
        ImGui::TableSetColumnIndex(4);
        ImGui::TextDisabled("%u item%s", folder.childFolderCount + folder.directRowCount,
                            folder.childFolderCount + folder.directRowCount == 1u ? "" : "s");
        ImGui::PopID();
        if (!open)
            return true;
        context.selected = folder.id;
        return false;
    }

    struct RowDrawContext final
    {
        AssetBrowserPanelState* state = nullptr;
        const assetStreaming::ProjectAssetCatalog* catalog = nullptr;
        editor::EditorPanelContext* panelContext = nullptr;
    };

    bool DrawRow(const assetStreaming::ProjectAssetCatalogRowId id,
                 const assetStreaming::ProjectAssetCatalogRow& row, void* const userData) noexcept
    {
        auto& context = *static_cast<RowDrawContext*>(userData);
        const vanguard::containers::StringView name = context.catalog->GetName(id);
        char label[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 labelBytes = std::min<vanguard::u32>(name.Length(), sizeof(label) - 1u);
        if (labelBytes != 0u)
            std::memcpy(label, name.Data(), labelBytes);
        label[labelBytes] = '\0';
        char typeLabel[32]{};
        FormatPrimaryOutputTypeLabel(row.primaryOutputType, typeLabel);

        ImGui::PushID(static_cast<int>(id.value));
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
        ImGui::TableSetColumnIndex(0);
        if (row.primaryOutputType != textures::TextureResourceType || context.panelContext == nullptr ||
            !DrawTextureThumbnail(*context.panelContext, *context.state, row, false, 24.0f))
            DrawContentPlaceholder(row.primaryOutputType == textures::TextureResourceType ? "T" : "A");
        ImGui::TableSetColumnIndex(1);
        const bool selected = context.state->selectedRow == id;
        if (ImGui::Selectable(labelBytes == 0u ? "<asset>" : label, selected, ImGuiSelectableFlags_SpanAllColumns))
            static_cast<void>(SelectRow(*context.state, id, row));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            static_cast<void>(SelectRow(*context.state, id, row));
            if (context.panelContext != nullptr)
            {
                editor::EditorUiFailure failure;
                static_cast<void>(context.panelContext->RequestContextMenu(AssetBrowserContextMenuSurfaceId, &failure));
            }
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("%s", typeLabel);
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%s", ImportStateName(row.importState));
        ImGui::TableSetColumnIndex(4);
        if (row.rootReadOnly)
            ImGui::TextDisabled("Read-only");
        else if (row.issues != assets::SourceIssueNone)
            ImGui::TextDisabled("Issues");
        else
            ImGui::TextDisabled("Writable");
        ImGui::PopID();
        return true;
    }

    struct ContentGridDrawContext final
    {
        AssetBrowserPanelState* state = nullptr;
        const assetStreaming::ProjectAssetCatalog* catalog = nullptr;
        editor::EditorPanelContext* panelContext = nullptr;
        assetStreaming::ProjectAssetCatalogFolderId openFolder;
        vanguard::u32 columns = 1u;
        vanguard::u32 absoluteIndex = 0u;
        float cardHeight = 0.0f;
        float thumbnailExtent = 0.0f;
        float cardPadding = 0.0f;
        bool compact = false;
    };

    void DrawContentGridPlaceholder(const ImVec2 minimum, const float extent, const char* const label) noexcept
    {
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const ImVec2 maximum{minimum.x + extent, minimum.y + extent};
        const float rounding = editor::detail::EditorVisualPx(4.0f);
        const float stroke = std::max(1.0f, editor::detail::EditorVisualPx(1.0f));
        draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), rounding, 0, stroke);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText({minimum.x + (extent - textSize.x) * 0.5f,
                       minimum.y + (extent - textSize.y) * 0.5f},
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }

    void DrawContentGridFolderGlyph(const ImVec2 minimum, const float width, const float height) noexcept
    {
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        // Keep folder identity independent from the editor selection/accent palette.
        // This deliberately tracks the muted warm ochre used by mature content
        // browsers: readable on the dark editor surface without competing with the
        // blue selection/hover language used elsewhere in Vanguard.
        const ImU32 bodyFill = IM_COL32(165, 127, 72, 255);
        const ImU32 tabFill = IM_COL32(143, 105, 57, 255);
        const ImU32 topHighlight = IM_COL32(188, 149, 88, 180);
        const ImU32 border = IM_COL32(82, 61, 37, 255);
        const ImU32 shadow = IM_COL32(0, 0, 0, 72);
        const float horizontalInset = width * 0.025f;
        const float bodyTop = minimum.y + height * 0.26f;
        const ImVec2 bodyMinimum{minimum.x + horizontalInset, bodyTop};
        const ImVec2 bodyMaximum{minimum.x + width - horizontalInset, minimum.y + height};
        const ImVec2 tabMinimum{bodyMinimum.x + width * 0.035f, minimum.y + height * 0.08f};
        const ImVec2 tabMaximum{minimum.x + width * 0.43f, bodyTop + height * 0.06f};
        const float bodyRounding = editor::detail::EditorVisualPx(1.5f);
        const float tabRounding = editor::detail::EditorVisualPx(1.0f);
        const float lineInset = editor::detail::EditorVisualPx(2.0f);
        const float stroke = std::max(1.0f, editor::detail::EditorVisualPx(1.0f));
        const float shadowOffset = std::max(editor::detail::EditorVisualPx(1.0f), height * 0.025f);
        draw->AddRectFilled({bodyMinimum.x + shadowOffset, bodyMinimum.y + shadowOffset},
                            {bodyMaximum.x + shadowOffset, bodyMaximum.y + shadowOffset}, shadow, bodyRounding);
        draw->AddRectFilled(tabMinimum, tabMaximum, tabFill, tabRounding);
        draw->AddRectFilled(bodyMinimum, bodyMaximum, bodyFill, bodyRounding);
        draw->AddLine({bodyMinimum.x + lineInset, bodyMinimum.y + stroke},
                      {bodyMaximum.x - lineInset, bodyMinimum.y + stroke}, topHighlight, stroke);
        draw->AddRect(bodyMinimum, bodyMaximum, border, bodyRounding, 0, stroke);
    }

    void DrawContentGridCardLine(const ImVec2 minimum, const ImVec2 maximum,
                                 const char* const label) noexcept
    {
        const float availableWidth = std::max(0.0f, maximum.x - minimum.x);
        if (availableWidth <= 0.0f)
            return;

        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        if (labelSize.x <= availableWidth)
        {
            draw->AddText({minimum.x + (availableWidth - labelSize.x) * 0.5f, minimum.y},
                          ImGui::GetColorU32(ImGuiCol_Text), label);
            return;
        }

        ImGui::RenderTextEllipsis(draw, minimum,
                                  {maximum.x, minimum.y + ImGui::GetTextLineHeight()}, maximum.x,
                                  label, nullptr, &labelSize);
    }

    void DrawContentGridCardCaption(const ImVec2 minimum, const ImVec2 maximum, const char* const label,
                                    const char* const secondary, const bool compact) noexcept
    {
        DrawContentGridCardLine(minimum, maximum, label);
        if (!compact && secondary != nullptr && secondary[0] != '\0')
        {
            const ImVec2 secondaryMinimum{minimum.x, minimum.y + ImGui::GetTextLineHeightWithSpacing()};
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            DrawContentGridCardLine(secondaryMinimum, maximum, secondary);
            ImGui::PopStyleColor();
        }
    }

    [[nodiscard]] ImVec2 GetContentGridThumbnailMinimum(const ImVec2 cardMinimum, const ImVec2 cardMaximum,
                                                        const float thumbnailExtent,
                                                        const float cardPadding) noexcept
    {
        const float available = std::max(0.0f, cardMaximum.x - cardMinimum.x - cardPadding * 2.0f);
        const float extent = std::min(thumbnailExtent, available);
        return {cardMinimum.x + (cardMaximum.x - cardMinimum.x - extent) * 0.5f,
                cardMinimum.y + cardPadding};
    }

    bool DrawContentGridFolder(const assetStreaming::ProjectAssetCatalogFolderInfo& folder,
                               const vanguard::containers::StringView name, void* const userData) noexcept
    {
        auto& context = *static_cast<ContentGridDrawContext*>(userData);
        if (context.state == nullptr || context.catalog == nullptr)
            return false;
        const vanguard::u32 column = context.absoluteIndex % std::max(1u, context.columns);
        ImGui::TableSetColumnIndex(static_cast<int>(column));

        char label[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 labelBytes = std::min<vanguard::u32>(name.Length(), sizeof(label) - 1u);
        if (labelBytes != 0u)
            std::memcpy(label, name.Data(), labelBytes);
        label[labelBytes] = '\0';
        const char* const displayLabel = labelBytes == 0u ? "<folder>" : label;

        ImGui::PushID("FolderCard");
        ImGui::PushID(static_cast<int>(folder.id.value));
        const bool selected = context.state->selectedContentFolder == folder.id;
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const bool activated = ImGui::Selectable(
            "##Card", selected, ImGuiSelectableFlags_AllowDoubleClick,
            {cardWidth, context.cardHeight});
        const bool doubleClicked = activated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const bool hovered = ImGui::IsItemHovered();
        const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const ImVec2 cardMinimum = ImGui::GetItemRectMin();
        const ImVec2 cardMaximum = ImGui::GetItemRectMax();

        if (activated || rightClicked)
            static_cast<void>(SelectContentFolder(*context.state, *context.catalog, folder.id));
        if (doubleClicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)))
            context.openFolder = folder.id;
        DrawFolderImportContext(*context.state, *context.catalog, folder.id, "##AssetGridFolderContext");

        const float cardInnerWidth = std::max(
            0.0f, cardMaximum.x - cardMinimum.x - context.cardPadding * 2.0f);
        const float folderWidth = std::min(cardInnerWidth, context.thumbnailExtent * 1.24f);
        const float folderHeight = folderWidth * 0.64f;
        const float thumbnailSlotHeight = context.thumbnailExtent;
        const ImVec2 folderMinimum{
            cardMinimum.x + (cardMaximum.x - cardMinimum.x - folderWidth) * 0.5f,
            cardMinimum.y + context.cardPadding +
                std::max(0.0f, (thumbnailSlotHeight - folderHeight) * 0.5f)};
        DrawContentGridFolderGlyph(folderMinimum, folderWidth, folderHeight);
        const ImVec2 textMinimum{cardMinimum.x + context.cardPadding,
                                 cardMinimum.y + context.cardPadding + thumbnailSlotHeight +
                                     editor::detail::EditorVisualPx(6.0f)};
        const ImVec2 textMaximum{cardMaximum.x - context.cardPadding, cardMaximum.y - context.cardPadding};
        DrawContentGridCardCaption(textMinimum, textMaximum, displayLabel, nullptr, true);
        if (hovered && ImGui::CalcTextSize(displayLabel).x > textMaximum.x - textMinimum.x)
            ImGui::SetTooltip("%s", displayLabel);

        ImGui::PopID();
        ImGui::PopID();
        ++context.absoluteIndex;
        return !context.openFolder.IsValid();
    }

    bool DrawContentGridRow(const assetStreaming::ProjectAssetCatalogRowId id,
                            const assetStreaming::ProjectAssetCatalogRow& row, void* const userData) noexcept
    {
        auto& context = *static_cast<ContentGridDrawContext*>(userData);
        if (context.state == nullptr || context.catalog == nullptr || context.panelContext == nullptr)
            return false;
        const vanguard::u32 column = context.absoluteIndex % std::max(1u, context.columns);
        ImGui::TableSetColumnIndex(static_cast<int>(column));

        const vanguard::containers::StringView name = context.catalog->GetName(id);
        char label[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 labelBytes = std::min<vanguard::u32>(name.Length(), sizeof(label) - 1u);
        if (labelBytes != 0u)
            std::memcpy(label, name.Data(), labelBytes);
        label[labelBytes] = '\0';
        const char* const displayLabel = labelBytes == 0u ? "<asset>" : label;
        char typeLabel[32]{};
        if (row.primaryOutputType == textures::TextureResourceType)
            std::snprintf(typeLabel, sizeof(typeLabel), "Texture");
        else if (row.primaryOutputType == resources::InvalidResourceTypeId)
            std::snprintf(typeLabel, sizeof(typeLabel), "Source");
        else
            std::snprintf(typeLabel, sizeof(typeLabel), "0x%08x", row.primaryOutputType);

        ImGui::PushID("AssetCard");
        ImGui::PushID(static_cast<int>(id.value));
        const bool selected = context.state->selectedRow == id;
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const bool activated = ImGui::Selectable(
            "##Card", selected, ImGuiSelectableFlags_AllowDoubleClick,
            {cardWidth, context.cardHeight});
        const bool hovered = ImGui::IsItemHovered();
        const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const ImVec2 cardMinimum = ImGui::GetItemRectMin();
        const ImVec2 cardMaximum = ImGui::GetItemRectMax();

        if (activated || rightClicked)
            static_cast<void>(SelectRow(*context.state, id, row));
        if (rightClicked)
        {
            editor::EditorUiFailure failure;
            static_cast<void>(context.panelContext->RequestContextMenu(AssetBrowserContextMenuSurfaceId, &failure));
        }

        const ImVec2 thumbnailMinimum = GetContentGridThumbnailMinimum(
            cardMinimum, cardMaximum, context.thumbnailExtent, context.cardPadding);
        const float availableThumbnail = std::max(
            0.0f, cardMaximum.x - cardMinimum.x - context.cardPadding * 2.0f);
        const float thumbnailExtent = std::min(context.thumbnailExtent, availableThumbnail);
        ImGui::SetCursorScreenPos(thumbnailMinimum);
        const bool drewThumbnail = row.primaryOutputType == textures::TextureResourceType &&
                                   DrawTextureThumbnail(*context.panelContext, *context.state, row, true, thumbnailExtent);
        if (!drewThumbnail)
            DrawContentGridPlaceholder(thumbnailMinimum, thumbnailExtent,
                                       row.primaryOutputType == textures::TextureResourceType ? "T" : "A");

        const ImVec2 textMinimum{cardMinimum.x + context.cardPadding,
                                 thumbnailMinimum.y + thumbnailExtent + editor::detail::EditorVisualPx(6.0f)};
        const ImVec2 textMaximum{cardMaximum.x - context.cardPadding, cardMaximum.y - context.cardPadding};
        DrawContentGridCardCaption(textMinimum, textMaximum, displayLabel, typeLabel, context.compact);
        if (hovered && ImGui::CalcTextSize(displayLabel).x > textMaximum.x - textMinimum.x)
            ImGui::SetTooltip("%s", displayLabel);

        ImGui::PopID();
        ImGui::PopID();
        ++context.absoluteIndex;
        return true;
    }

    void DrawReferenceDetails(AssetBrowserPanelState& state) noexcept
    {
        if (state.extension == nullptr)
            return;
        assets::AssetReference references[AssetBrowserReferencePageSize]{};
        vanguard::u32 available = 0u;
        bool truncated = false;
        if (state.selectedHasAsset)
        {
            const vanguard::u32 count = state.extension->assets.CopyForwardReferences(
                state.selectedAsset, state.dependencyPageOffset, references, available, truncated);
            ImGui::Text("Dependencies: %u%s", available, truncated ? "+" : "");
            for (vanguard::u32 index = 0u; index < count; ++index)
            {
                const assets::AssetReference& reference = references[index];
                ImGui::BulletText("%016llx type=%08x kind=%u",
                                  static_cast<unsigned long long>(reference.to.GetPath().Id()),
                                  reference.to.ExpectedType(), static_cast<vanguard::u32>(reference.kind));
            }
            ImGui::PushID("dependency-pages");
            if (state.dependencyPageOffset != 0u && ImGui::Button("Previous"))
                state.dependencyPageOffset =
                    state.dependencyPageOffset > AssetBrowserReferencePageSize
                        ? state.dependencyPageOffset - AssetBrowserReferencePageSize
                        : 0u;
            if (state.dependencyPageOffset != 0u)
                ImGui::SameLine();
            if (state.dependencyPageOffset + count < available && ImGui::Button("Next"))
                state.dependencyPageOffset =
                    std::min(state.dependencyPageOffset + AssetBrowserReferencePageSize, available);
            ImGui::PopID();
        }

        if (state.selectedPrimaryOutput.IsValid())
        {
            const vanguard::u32 count = state.extension->assets.CopyBackwardReferences(
                state.selectedPrimaryOutput, state.referencerPageOffset, references, available, truncated);
            ImGui::Text("Referencers: %u%s", available, truncated ? "+" : "");
            for (vanguard::u32 index = 0u; index < count; ++index)
            {
                char identity[33]{};
                FormatAssetId(references[index].from, identity);
                ImGui::BulletText("%s kind=%u", identity, static_cast<vanguard::u32>(references[index].kind));
            }
            ImGui::PushID("referencer-pages");
            if (state.referencerPageOffset != 0u && ImGui::Button("Previous"))
                state.referencerPageOffset =
                    state.referencerPageOffset > AssetBrowserReferencePageSize
                        ? state.referencerPageOffset - AssetBrowserReferencePageSize
                        : 0u;
            if (state.referencerPageOffset != 0u)
                ImGui::SameLine();
            if (state.referencerPageOffset + count < available && ImGui::Button("Next"))
                state.referencerPageOffset =
                    std::min(state.referencerPageOffset + AssetBrowserReferencePageSize, available);
            ImGui::PopID();
        }

        if (state.selectedHasAsset)
        {
            resources::ResourceReference outputs[AssetBrowserReferencePageSize]{};
            const vanguard::u32 count = state.extension->assets.CopyAssetOutputs(
                state.selectedAsset, state.outputPageOffset, outputs, available, truncated);
            ImGui::Text("Outputs: %u%s", available, truncated ? "+" : "");
            for (vanguard::u32 index = 0u; index < count; ++index)
            {
                vanguard::u32 referencerCount = 0u;
                bool referencersTruncated = false;
                const vanguard::containers::ArraySpan<assets::AssetReference> noReferences;
                static_cast<void>(state.extension->assets.CopyBackwardReferences(
                    outputs[index], 0u, noReferences, referencerCount, referencersTruncated));
                ImGui::BulletText("%016llx type=%08x; referencers=%u%s",
                                  static_cast<unsigned long long>(outputs[index].GetPath().Id()),
                                  outputs[index].ExpectedType(), referencerCount,
                                  referencersTruncated ? "+" : "");
            }
            ImGui::PushID("output-pages");
            if (state.outputPageOffset != 0u && ImGui::Button("Previous"))
                state.outputPageOffset =
                    state.outputPageOffset > AssetBrowserReferencePageSize
                        ? state.outputPageOffset - AssetBrowserReferencePageSize
                        : 0u;
            if (state.outputPageOffset != 0u)
                ImGui::SameLine();
            if (state.outputPageOffset + count < available && ImGui::Button("Next"))
                state.outputPageOffset =
                    std::min(state.outputPageOffset + AssetBrowserReferencePageSize, available);
            ImGui::PopID();
        }
    }

    void EnsureTextureSettingsLoaded(AssetBrowserPanelState& state,
                                     const assetStreaming::ProjectAssetCatalogRow& row) noexcept
    {
        if (state.textureSettingsLoaded)
            return;
        state.textureSettingsLoaded = true;
        state.textureSettingsSupported = false;
        state.textureSettingsDirty = false;
        state.textureSettingsSnapshot = {};
        if (state.extension == nullptr || !state.selectedHasAsset ||
            row.primaryOutputType != textures::TextureResourceType || state.selectedAcceptedGeneration == 0u ||
            state.selectedSourceGeneration == 0u)
            return;

        vanguard::u8 encoded[64]{};
        editor::EditorProjectAssetSettingsSnapshot snapshot;
        if (!state.extension->assets.CopyAssetSettings(
                state.selectedAsset, state.selectedAcceptedGeneration, state.selectedSourceGeneration,
                {encoded, static_cast<vanguard::u32>(sizeof(encoded))}, snapshot) ||
            snapshot.settingsVersion != textureTools::TextureBuildSettingsVersion || snapshot.settingsBytes == 0u)
            return;

        textureTools::TextureBuildDescription decoded;
        if (textureTools::DecodeTextureBuildSettings({encoded, snapshot.settingsBytes}, decoded) !=
            textureTools::TextureBuildSettingsResult::Success)
            return;
        state.textureSettingsSnapshot = snapshot;
        state.textureSettings = decoded;
        state.authoredTextureSettings = decoded;
        state.textureSettingsSupported = true;
    }

    [[nodiscard]] bool DrawChannelCombo(const char* const label, textureTools::ImportedChannel& channel) noexcept
    {
        constexpr const char* Names[]{"Red", "Green", "Blue", "Alpha", "Zero", "One"};
        vanguard::i32 selected = static_cast<vanguard::i32>(channel);
        if (!ImGui::Combo(label, &selected, Names, static_cast<int>(sizeof(Names) / sizeof(Names[0]))))
            return false;
        channel = static_cast<textureTools::ImportedChannel>(selected);
        return true;
    }

    [[nodiscard]] vanguard::i32 BuiltInProfileIndex(const textureTools::TextureCookingProfileId profile) noexcept
    {
        constexpr textureTools::TextureCookingProfileId Profiles[]{
            textureTools::profiles::Color, textureTools::profiles::ColorAlpha, textureTools::profiles::Normal,
            textureTools::profiles::Masks, textureTools::profiles::Data, textureTools::profiles::Ui,
            textureTools::profiles::Hdr};
        for (vanguard::i32 index = 0; index < static_cast<vanguard::i32>(sizeof(Profiles) / sizeof(Profiles[0])); ++index)
            if (Profiles[index] == profile)
                return index;
        return -1;
    }

    void DrawTextureSettings(AssetBrowserPanelState& state,
                             const assetStreaming::ProjectAssetCatalogRow& row) noexcept
    {
        if (row.primaryOutputType != textures::TextureResourceType || !state.selectedHasAsset)
            return;
        EnsureTextureSettingsLoaded(state, row);
        ImGui::Separator();
        ImGui::Text("Texture import settings");
        if (!state.textureSettingsSupported)
        {
            ImGui::TextDisabled("Authored texture settings are unavailable or no longer match this catalog generation.");
            return;
        }

        bool changed = false;
        const bool authoredDds = state.authoredTextureSettings.sourceMode == textureTools::TextureBuildSourceMode::PreservedDds;
        if (authoredDds)
            ImGui::TextDisabled("Source mode: Preserved DDS");
        else
        {
            constexpr const char* SourceModes[]{"Image 2D", "Cube cross (horizontal)", "Cube cross (vertical)"};
            vanguard::i32 sourceMode = static_cast<vanguard::i32>(state.textureSettings.sourceMode);
            if (sourceMode < 0 || sourceMode >= static_cast<vanguard::i32>(sizeof(SourceModes) / sizeof(SourceModes[0])))
                sourceMode = 0;
            if (ImGui::Combo("Source mode", &sourceMode, SourceModes,
                             static_cast<int>(sizeof(SourceModes) / sizeof(SourceModes[0]))))
            {
                state.textureSettings.sourceMode = static_cast<textureTools::TextureBuildSourceMode>(sourceMode);
                if (state.textureSettings.profile == 0u)
                    state.textureSettings.profile = textureTools::profiles::Color;
                state.textureSettings.routeFlags = textureTools::TextureBuildRouteFlags::None;
                state.textureSettings.ddsMipTailCount = 0u;
                changed = true;
            }
        }

        constexpr const char* ColorSpaces[]{"Automatic", "Linear", "sRGB"};
        vanguard::i32 colorSpace = static_cast<vanguard::i32>(state.textureSettings.colorSpace);
        if (ImGui::Combo("Color space", &colorSpace, ColorSpaces,
                         static_cast<int>(sizeof(ColorSpaces) / sizeof(ColorSpaces[0]))))
        {
            state.textureSettings.colorSpace = static_cast<textureTools::ImportedColorSpace>(colorSpace);
            changed = true;
        }

        if (state.textureSettings.sourceMode != textureTools::TextureBuildSourceMode::PreservedDds)
        {
            changed = DrawChannelCombo("Red channel", state.textureSettings.channels.red) || changed;
            changed = DrawChannelCombo("Green channel", state.textureSettings.channels.green) || changed;
            changed = DrawChannelCombo("Blue channel", state.textureSettings.channels.blue) || changed;
            changed = DrawChannelCombo("Alpha channel", state.textureSettings.channels.alpha) || changed;

            constexpr const char* ProfileNames[]{"Color", "Color + Alpha", "Normal", "Masks", "Data", "UI", "HDR"};
            constexpr textureTools::TextureCookingProfileId Profiles[]{
                textureTools::profiles::Color, textureTools::profiles::ColorAlpha, textureTools::profiles::Normal,
                textureTools::profiles::Masks, textureTools::profiles::Data, textureTools::profiles::Ui,
                textureTools::profiles::Hdr};
            vanguard::i32 profile = BuiltInProfileIndex(state.textureSettings.profile);
            const char* preview = profile >= 0 ? ProfileNames[profile] : "Custom registered profile";
            if (ImGui::BeginCombo("Cooking profile", preview))
            {
                for (vanguard::i32 index = 0; index < static_cast<vanguard::i32>(sizeof(Profiles) / sizeof(Profiles[0])); ++index)
                {
                    if (ImGui::Selectable(ProfileNames[index], profile == index))
                    {
                        state.textureSettings.profile = Profiles[index];
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            bool streamable = state.textureSettings.routeFlags == textureTools::TextureBuildRouteFlags::Streamable;
            if (ImGui::Checkbox("Stream preserved DDS", &streamable))
            {
                state.textureSettings.routeFlags = streamable ? textureTools::TextureBuildRouteFlags::Streamable
                                                              : textureTools::TextureBuildRouteFlags::None;
                if (!streamable)
                    state.textureSettings.ddsMipTailCount = 1u;
                else if (state.textureSettings.ddsMipTailCount == 0u)
                    state.textureSettings.ddsMipTailCount = 1u;
                changed = true;
            }
            if (streamable)
            {
                vanguard::i32 mipTail = state.textureSettings.ddsMipTailCount;
                if (ImGui::InputInt("DDS mip tail count", &mipTail))
                {
                    mipTail = std::max<vanguard::i32>(1, std::min<vanguard::i32>(255, mipTail));
                    state.textureSettings.ddsMipTailCount = static_cast<vanguard::u8>(mipTail);
                    changed = true;
                }
            }
        }
        if (changed)
            state.textureSettingsDirty = !SameTextureSettings(state.textureSettings, state.authoredTextureSettings);
        ImGui::Text("Authored settings: %s", state.textureSettingsDirty ? "edited locally; not applied" : "applied");
        if (state.textureSettingsDirty)
            ImGui::TextDisabled("Apply writes authored metadata only; build and delivery freshness remain separate.");
    }

    void DrawSelectedDetails(editor::EditorPanelContext& panelContext, AssetBrowserPanelState& state,
                             const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        const assetStreaming::ProjectAssetCatalogRow* const row = catalog.GetRow(state.selectedRow);
        if (row == nullptr)
        {
            if (state.selectedContentFolder.IsValid())
            {
                assetStreaming::ProjectAssetCatalogFolderInfo folderInfo;
                if (catalog.GetFolderInfo(state.selectedContentFolder, folderInfo))
                {
                    const vanguard::containers::StringView folderName = catalog.GetFolderName(state.selectedContentFolder);
                    char rootLabel[64]{};
                    FormatSourceRootName(state, state.selectedContentFolderRoot, rootLabel, sizeof(rootLabel));
                    ImGui::Text("Folder: %.*s", static_cast<int>(folderName.Length()), folderName.Data());
                    if (state.selectedContentFolderPathBytes == 0u)
                        ImGui::TextDisabled("Location: %s", rootLabel);
                    else
                        ImGui::TextDisabled("Location: %s/%.*s", rootLabel,
                                            static_cast<int>(state.selectedContentFolderPathBytes),
                                            state.selectedContentFolderPath);
                    ImGui::TextDisabled("%u direct folder%s, %u direct asset%s",
                                        folderInfo.childFolderCount, folderInfo.childFolderCount == 1u ? "" : "s",
                                        folderInfo.directRowCount, folderInfo.directRowCount == 1u ? "" : "s");
                    return;
                }
            }
            ImGui::TextDisabled("Select an asset to inspect metadata and references.");
            return;
        }
        char path[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        const vanguard::u32 pathBytes =
            catalog.CopyRelativePath(state.selectedRow, {path, static_cast<vanguard::u32>(sizeof(path))});
        if (pathBytes == 0u)
        {
            ImGui::TextDisabled("Selected source path is unavailable.");
            return;
        }
        ImGui::Text("Source: %.*s", static_cast<int>(pathBytes), path);
        ImGui::Text("Import: %s", ImportStateName(row->importState));
        ImGui::Text("Metadata: %s  issues=0x%08x", MetadataResultName(row->metadataResult), row->issues);
        ImGui::Text("Importer: %016llx v%u", static_cast<unsigned long long>(row->importer), row->availableImporterVersion);
        ImGui::Text("Source type: %08x  primary output type: %08x", row->sourceType, row->primaryOutputType);
        if (row->rootReadOnly)
            ImGui::TextDisabled("Source root is read-only");
        if (row->hasAssetIdentity)
        {
            char identity[33]{};
            FormatAssetId(row->asset, identity);
            vanguard::u64 acceptedGeneration = 0u;
            const bool accepted = state.extension != nullptr &&
                                  state.extension->assets.GetAcceptedAssetGeneration(row->asset, acceptedGeneration);
            ImGui::Text("AssetId: %s", identity);
            ImGui::Text("Accepted generation: %s%llu", accepted ? "" : "not accepted / ",
                        static_cast<unsigned long long>(acceptedGeneration));
            if (state.extension != nullptr &&
                !state.extension->assets.IsSourceGenerationCurrent(state.selectedSourceGeneration))
                ImGui::TextDisabled("Selection is stale; wait for the catalog generation to catch up before mutating.");
            else if (accepted && acceptedGeneration != state.selectedAcceptedGeneration)
                ImGui::TextDisabled("Selection is stale; wait for the catalog generation to catch up before mutating.");
        }

        DrawTextureSettings(state, *row);

        if (ImGui::Button("Asset Actions..."))
        {
            editor::EditorUiFailure failure;
            static_cast<void>(panelContext.RequestContextMenu(AssetBrowserContextMenuSurfaceId, &failure));
        }
        if (state.selectedPrimaryOutput.IsValid())
        {
            assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
            const bool tracked = state.extension != nullptr &&
                                 state.extension->assets.GetOutputDeliveryStatus(state.selectedPrimaryOutput, delivery);
            ImGui::Text("Primary output: %016llx",
                        static_cast<unsigned long long>(state.selectedPrimaryOutput.GetPath().Id()));
            ImGui::Text("Delivery build stage: %s  publication: %s  resident: %s",
                        tracked ? DeliveryStateName(delivery.buildState) : "Unknown",
                        tracked ? DeliveryStateName(delivery.publicationState) : "Unknown",
                        tracked ? DeliveryStateName(delivery.residentState) : "Unknown");
            if (!state.selectedFreshnessEvaluated)
                ImGui::TextDisabled("Build freshness: Not evaluated");
            else if (state.selectedFreshnessResult == assetStreaming::ProjectAssetPipelineResult::Success)
                ImGui::Text("Build freshness: %s", DirtyReasonName(state.selectedFreshness.reason));
            else
                ImGui::TextDisabled("Build freshness: unavailable (%u)",
                                    static_cast<vanguard::u32>(state.selectedFreshnessResult));
            if (ImGui::Button("Evaluate build freshness") && state.extension != nullptr)
            {
                state.selectedFreshness = {};
                state.selectedFreshnessResult =
                    state.extension->assets.QueryBuildFreshness(state.selectedPrimaryOutput, state.selectedFreshness);
                state.selectedFreshnessEvaluated = true;
            }
        }
        else if (state.selectedPrimaryOutputAmbiguous)
            ImGui::TextDisabled("Primary output: ambiguous; inspect declared outputs below.");
        else
            ImGui::TextDisabled("Primary output: unavailable; inspect declared outputs below.");
        if (state.selectedPrimaryOutput.IsValid())
        {
            ImGui::SeparatorText("Texture preview");
            DrawSelectedTexturePreview(panelContext, state);
        }
        ImGui::Separator();
        DrawReferenceDetails(state);
    }

    struct AssetBrowserSelectionSummary final
    {
        char name[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        char status[128]{};
    };

    void CopySelectionSummaryName(const vanguard::containers::StringView value,
                                  AssetBrowserSelectionSummary& summary) noexcept
    {
        const vanguard::u32 bytes = std::min<vanguard::u32>(
            value.Length(), static_cast<vanguard::u32>(sizeof(summary.name) - 1u));
        if (bytes != 0u)
            std::memcpy(summary.name, value.Data(), bytes);
        summary.name[bytes] = '\0';
    }

    [[nodiscard]] AssetBrowserSelectionSummary BuildAssetBrowserSelectionSummary(
        const AssetBrowserPanelState& state, const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        AssetBrowserSelectionSummary summary;
        const assetStreaming::ProjectAssetCatalogRow* const row = catalog.GetRow(state.selectedRow);
        if (row != nullptr)
        {
            CopySelectionSummaryName(catalog.GetName(state.selectedRow), summary);
            if (summary.name[0] == '\0')
                std::snprintf(summary.name, sizeof(summary.name), "<asset>");
            char typeLabel[32]{};
            FormatPrimaryOutputTypeLabel(row->primaryOutputType, typeLabel);
            std::snprintf(summary.status, sizeof(summary.status), "%s  |  %s", typeLabel,
                          ImportStateName(row->importState));
            if (row->rootReadOnly)
            {
                const std::size_t used = std::strlen(summary.status);
                std::snprintf(summary.status + used, sizeof(summary.status) - used, "  |  Read-only");
            }
            else if (row->issues != assets::SourceIssueNone)
            {
                const std::size_t used = std::strlen(summary.status);
                std::snprintf(summary.status + used, sizeof(summary.status) - used, "  |  Issues");
            }
            return summary;
        }

        if (state.selectedContentFolder.IsValid())
        {
            CopySelectionSummaryName(catalog.GetFolderName(state.selectedContentFolder), summary);
            if (summary.name[0] == '\0')
                std::snprintf(summary.name, sizeof(summary.name), "<folder>");
            std::snprintf(summary.status, sizeof(summary.status), "Folder");
            return summary;
        }

        std::snprintf(summary.name, sizeof(summary.name), "Nothing selected");
        return summary;
    }

    [[nodiscard]] bool AssetBrowserSelectionSummaryWraps(const AssetBrowserSelectionSummary& summary,
                                                         const float availableWidth) noexcept
    {
        if (summary.status[0] == '\0')
            return false;
        const float padding = editor::detail::EditorVisualPx(8.0f);
        const float gap = editor::detail::EditorVisualPx(8.0f);
        const float labelWidth = ImGui::CalcTextSize("Selection").x;
        const float statusWidth = ImGui::CalcTextSize(summary.status).x;
        const float minimumNameWidth = editor::detail::EditorVisualPx(96.0f);
        return availableWidth < padding * 2.0f + labelWidth + gap + minimumNameWidth + gap + statusWidth;
    }

    [[nodiscard]] float AssetBrowserSelectionStripHeight(const AssetBrowserSelectionSummary& summary,
                                                         const float availableWidth) noexcept
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const float verticalPadding = editor::detail::EditorVisualPx(5.0f);
        const bool wraps = AssetBrowserSelectionSummaryWraps(summary, availableWidth);
        return verticalPadding * 2.0f + lineHeight * (wraps ? 2.0f : 1.0f) +
               (wraps ? ImGui::GetStyle().ItemSpacing.y : 0.0f);
    }

    void DrawAssetBrowserEllipsizedText(const char* const text, const float width) noexcept
    {
        const float clampedWidth = std::max(1.0f, width);
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        const ImVec2 maximum{minimum.x + clampedWidth, minimum.y + ImGui::GetTextLineHeight()};
        const ImVec2 size = ImGui::CalcTextSize(text);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum, maximum, maximum.x, text, nullptr, &size);
        ImGui::Dummy({clampedWidth, ImGui::GetTextLineHeight()});
    }

    void DrawAssetBrowserSelectionStrip(const AssetBrowserSelectionSummary& summary, const float stripHeight) noexcept
    {
        constexpr ImGuiWindowFlags StripFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(editor::detail::EditorVisualPx(8.0f), 0.0f));
        ImGui::BeginChild("##AssetBrowserSelectionStrip", ImVec2(0.0f, stripHeight), false, StripFlags);

        const float gap = editor::detail::EditorVisualPx(8.0f);
        const float labelWidth = ImGui::CalcTextSize("Selection").x;
        const bool wrap = AssetBrowserSelectionSummaryWraps(summary, ImGui::GetWindowWidth());
        const float lineHeight = ImGui::GetTextLineHeight();
        const float contentHeight = lineHeight * (wrap ? 2.0f : 1.0f) +
                                    (wrap ? ImGui::GetStyle().ItemSpacing.y : 0.0f);
        ImGui::SetCursorPosY(std::max(0.0f, (stripHeight - contentHeight) * 0.5f));
        ImGui::TextDisabled("Selection");
        ImGui::SameLine(0.0f, gap);

        if (!wrap)
        {
            const float statusWidth = summary.status[0] != '\0' ? ImGui::CalcTextSize(summary.status).x : 0.0f;
            const float statusGap = summary.status[0] != '\0' ? gap : 0.0f;
            const float nameWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - statusWidth - statusGap);
            DrawAssetBrowserEllipsizedText(summary.name, nameWidth);
            if (summary.status[0] != '\0')
            {
                ImGui::SameLine(0.0f, gap);
                ImGui::TextDisabled("%s", summary.status);
            }
        }
        else
        {
            DrawAssetBrowserEllipsizedText(summary.name, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelWidth + gap);
            ImGui::TextDisabled("%s", summary.status);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void DrawAssetBrowserToolbar(AssetBrowserPanelState& state) noexcept
    {
        PumpExternalImport(state);
        const bool canImportCurrentFolder = state.extension->assets.CanImportExternalSource(
            state.root, vanguard::containers::StringView(state.folderPath));
        if (!canImportCurrentFolder)
            ImGui::BeginDisabled();
        if (ImGui::Button("Import..."))
            static_cast<void>(BeginExternalImport(
                state, state.root, vanguard::containers::StringView(state.folderPath)));
        if (!canImportCurrentFolder)
            ImGui::EndDisabled();
        ImGui::SameLine();

        const float filterRowWidth = ImGui::GetContentRegionAvail().x;
        const bool compactFilters = filterRowWidth < 640.0f;
        if (compactFilters)
            ImGui::SetNextItemWidth(filterRowWidth);
        else
            ImGui::SetNextItemWidth(std::max(180.0f, filterRowWidth - 470.0f));
        bool persistentQueryChanged = ImGui::InputTextWithHint(
            "##AssetSearch", "Search source path", state.search, sizeof(state.search));
        bool queryChanged = persistentQueryChanged;
        if (!compactFilters)
            ImGui::SameLine();
        const bool recursiveChanged = ImGui::Checkbox("Include subfolders", &state.recursive);
        persistentQueryChanged = recursiveChanged || persistentQueryChanged;
        queryChanged = recursiveChanged || queryChanged;
        if (!compactFilters)
            ImGui::SameLine();
        ImGui::SetNextItemWidth(compactFilters ? std::min(220.0f, ImGui::GetContentRegionAvail().x) : 150.0f);
        if (ImGui::BeginCombo("##AssetImportFilter", editor::detail::GetAssetBrowserImportFilterLabel(state.importFilter)))
        {
            if (ImGui::Selectable("All", state.importFilter < 0))
            {
                state.importFilter = -1;
                queryChanged = true;
                persistentQueryChanged = true;
            }
            for (vanguard::u32 filter = 0u; filter < editor::detail::GetAssetBrowserImportFilterCount(); ++filter)
            {
                const vanguard::i32 value = static_cast<vanguard::i32>(filter);
                if (ImGui::Selectable(editor::detail::AssetBrowserImportFilters[filter].label,
                                      state.importFilter == value))
                {
                    state.importFilter = value;
                    queryChanged = true;
                    persistentQueryChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        if (!compactFilters)
            ImGui::SameLine();
        if (ImGui::Button("Refresh query"))
            queryChanged = true;
        if (queryChanged)
        {
            state.queryDirty = true;
            if (persistentQueryChanged)
                ResetAssetBrowserContentScrollIntent(state);
            ClearSelection(state);
        }
        if (persistentQueryChanged)
            MarkAssetBrowserPersistentStateDirty(state);
    }

    void DrawAssetBrowserNavigationPane(AssetBrowserPanelState& state,
                                        const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        ImGui::TextDisabled("Folders");
        ImGui::Separator();
        DrawAssetBrowserFolderTree(state, catalog);
    }

    void DrawAssetBrowserDetailsView(editor::EditorPanelContext& panelContext, AssetBrowserPanelState& state,
                                     const assetStreaming::ProjectAssetCatalog& catalog,
                                     const vanguard::u32 contentFolderCount,
                                     const assetStreaming::ProjectAssetCatalogQueryStatus& queryStatus,
                                     const float contentHeight) noexcept
    {
        ImGui::BeginChild("##AssetRows", ImVec2(0.0f, std::max(1.0f, contentHeight)), true);
        const vanguard::u32 contentEntries = contentFolderCount + queryStatus.matches;
        const bool scrollAssetTable = ImGui::GetContentRegionAvail().x < 600.0f;
        ImGuiTableFlags assetTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                         ImGuiTableFlags_Resizable;
        assetTableFlags |= scrollAssetTable ? (ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit)
                                            : ImGuiTableFlags_SizingStretchProp;
        if (contentEntries != 0u &&
            ImGui::BeginTable("##AssetBrowserContentTable", 5, assetTableFlags, ImVec2(0.0f, 0.0f),
                              scrollAssetTable ? 590.0f : 0.0f))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Name",
                                    scrollAssetTable ? ImGuiTableColumnFlags_WidthFixed
                                                     : ImGuiTableColumnFlags_WidthStretch,
                                    scrollAssetTable ? 220.0f : 1.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("Import", ImGuiTableColumnFlags_WidthFixed, 128.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(contentEntries));
            RowDrawContext rowContext{&state, &catalog, &panelContext};
            ContentFolderDrawContext contentFolderContext{&state, &catalog};
            while (clipper.Step())
            {
                const vanguard::u32 displayStart = static_cast<vanguard::u32>(clipper.DisplayStart);
                const vanguard::u32 displayEnd = static_cast<vanguard::u32>(clipper.DisplayEnd);
                const editor::detail::AssetBrowserContentRange range =
                    editor::detail::SplitAssetBrowserContentRange(displayStart, displayEnd, contentFolderCount);
                if (range.folderCount != 0u)
                {
                    static_cast<void>(catalog.VisitChildFolders(
                        state.folder, range.firstFolder, range.folderCount, DrawContentFolder, &contentFolderContext));
                }
                if (!contentFolderContext.selected.IsValid() && range.assetCount != 0u)
                {
                    static_cast<void>(catalog.VisitQueryMatches(
                        state.query, range.firstAsset, range.assetCount, DrawRow, &rowContext));
                }
                if (contentFolderContext.selected.IsValid())
                    break;
            }
            ImGui::EndTable();
            if (contentFolderContext.selected.IsValid())
                static_cast<void>(NavigatePanelToFolderId(state, catalog, contentFolderContext.selected));
        }
        ClearSelectionOnContentBackgroundClick(state);
        if (ImGui::BeginPopupContextWindow(
                "##AssetBrowserEmptyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            const bool canImport = state.extension->assets.CanImportExternalSource(
                state.root, vanguard::containers::StringView(state.folderPath));
            if (ImGui::MenuItem("Import...", nullptr, false, canImport))
                static_cast<void>(BeginExternalImport(
                    state, state.root, vanguard::containers::StringView(state.folderPath)));
            ImGui::EndPopup();
        }
        SweepTextureThumbnails(state, panelContext.GetUi().GetFrame());
        const vanguard::u32 viewIndex = editor::detail::AssetBrowserContentViewModeIndex(
            editor::detail::AssetBrowserContentViewMode::Details);
        TrackAssetBrowserScrollIntent(
            state, state.contentScrollFractions[viewIndex], state.contentScrollRestorePending[viewIndex],
            state.contentScrollRestoreGeometryReady[viewIndex],
            queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Complete);
        ImGui::EndChild();
    }

    void DrawAssetBrowserGridView(editor::EditorPanelContext& panelContext, AssetBrowserPanelState& state,
                                  const assetStreaming::ProjectAssetCatalog& catalog,
                                  const vanguard::u32 contentFolderCount,
                                  const assetStreaming::ProjectAssetCatalogQueryStatus& queryStatus,
                                  const bool compact, const float contentHeight) noexcept
    {
        ImGui::BeginChild(compact ? "##AssetCompactGrid" : "##AssetGrid",
                          ImVec2(0.0f, std::max(1.0f, contentHeight)), true);

        float& configuredExtentLogical =
            compact ? state.contentView.compactThumbnailExtent : state.contentView.thumbnailExtent;
        const float minimumExtentLogical = compact ? AssetBrowserCompactGridMinimumThumbnailExtent
                                                   : AssetBrowserGridMinimumThumbnailExtent;
        const float maximumExtentLogical = compact ? AssetBrowserCompactGridMaximumThumbnailExtent
                                                   : AssetBrowserGridMaximumThumbnailExtent;
        configuredExtentLogical = std::clamp(
            configuredExtentLogical, minimumExtentLogical, maximumExtentLogical);

        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.KeyCtrl && io.MouseWheel != 0.0f)
        {
            const float previousExtent = configuredExtentLogical;
            configuredExtentLogical = std::clamp(
                configuredExtentLogical + io.MouseWheel * 8.0f,
                minimumExtentLogical, maximumExtentLogical);
            if (configuredExtentLogical != previousExtent)
                MarkAssetBrowserPersistentStateDirty(state);
        }

        const float configuredExtent = editor::detail::EditorVisualPx(configuredExtentLogical);
        const float cardPadding = editor::detail::EditorVisualPx(AssetBrowserGridCardPadding);
        const float cardSpacing = editor::detail::EditorVisualPx(AssetBrowserGridCardSpacing);

        const vanguard::u32 contentEntries = contentFolderCount + queryStatus.matches;
        if (contentEntries == 0u)
        {
            if (queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Complete)
                ImGui::TextDisabled("This folder is empty. Drop files here or use Import...");
        }
        else
        {
            const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            const float desiredCardWidth = configuredExtent + cardPadding * 2.0f;
            const vanguard::u32 columns = std::max<vanguard::u32>(
                1u, static_cast<vanguard::u32>((availableWidth + cardSpacing) /
                                               (desiredCardWidth + cardSpacing)));
            const float textHeight = compact ? ImGui::GetTextLineHeightWithSpacing()
                                             : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
            const float cardHeight = configuredExtent + cardPadding * 2.0f + textHeight +
                                     editor::detail::EditorVisualPx(5.0f);
            const vanguard::u32 rowCount = (contentEntries + columns - 1u) / columns;

            if (ImGui::BeginTable(compact ? "##AssetCompactGridTable" : "##AssetGridTable",
                                  static_cast<int>(columns), ImGuiTableFlags_SizingStretchSame))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rowCount), cardHeight + cardSpacing);
                ContentGridDrawContext context;
                context.state = &state;
                context.catalog = &catalog;
                context.panelContext = &panelContext;
                context.columns = columns;
                context.cardHeight = cardHeight;
                context.thumbnailExtent = configuredExtent;
                context.cardPadding = cardPadding;
                context.compact = compact;

                while (clipper.Step() && !context.openFolder.IsValid())
                {
                    for (int visibleRow = clipper.DisplayStart;
                         visibleRow < clipper.DisplayEnd && !context.openFolder.IsValid(); ++visibleRow)
                    {
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, cardHeight + cardSpacing);
                        const vanguard::u32 rowStart = static_cast<vanguard::u32>(visibleRow) * columns;
                        const vanguard::u32 rowEnd = std::min(contentEntries, rowStart + columns);
                        const editor::detail::AssetBrowserContentRange range =
                            editor::detail::SplitAssetBrowserContentRange(rowStart, rowEnd, contentFolderCount);
                        context.absoluteIndex = rowStart;

                        if (range.folderCount != 0u)
                        {
                            static_cast<void>(catalog.VisitChildFolders(
                                state.folder, range.firstFolder, range.folderCount, DrawContentGridFolder, &context));
                        }
                        if (!context.openFolder.IsValid() && range.assetCount != 0u)
                        {
                            context.absoluteIndex = std::max(rowStart, contentFolderCount);
                            static_cast<void>(catalog.VisitQueryMatches(
                                state.query, range.firstAsset, range.assetCount, DrawContentGridRow, &context));
                        }
                    }
                }
                ImGui::EndTable();
                if (context.openFolder.IsValid())
                    static_cast<void>(NavigatePanelToFolderId(state, catalog, context.openFolder));
            }
        }

        ClearSelectionOnContentBackgroundClick(state);
        if (ImGui::BeginPopupContextWindow(
                compact ? "##AssetCompactGridEmptyContext" : "##AssetGridEmptyContext",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            const bool canImport = state.extension->assets.CanImportExternalSource(
                state.root, vanguard::containers::StringView(state.folderPath));
            if (ImGui::MenuItem("Import...", nullptr, false, canImport))
                static_cast<void>(BeginExternalImport(
                    state, state.root, vanguard::containers::StringView(state.folderPath)));
            ImGui::EndPopup();
        }
        SweepTextureThumbnails(state, panelContext.GetUi().GetFrame());
        const editor::detail::AssetBrowserContentViewMode viewMode =
            compact ? editor::detail::AssetBrowserContentViewMode::CompactGrid
                    : editor::detail::AssetBrowserContentViewMode::Grid;
        const vanguard::u32 viewIndex = editor::detail::AssetBrowserContentViewModeIndex(viewMode);
        TrackAssetBrowserScrollIntent(
            state, state.contentScrollFractions[viewIndex], state.contentScrollRestorePending[viewIndex],
            state.contentScrollRestoreGeometryReady[viewIndex],
            queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Complete);
        ImGui::EndChild();
    }

    [[nodiscard]] const char* AssetBrowserContentViewModeName(
        const editor::detail::AssetBrowserContentViewMode mode) noexcept
    {
        switch (mode)
        {
        case editor::detail::AssetBrowserContentViewMode::Grid:
            return "Grid";
        case editor::detail::AssetBrowserContentViewMode::CompactGrid:
            return "Compact Grid";
        case editor::detail::AssetBrowserContentViewMode::Details:
            return "Details";
        }
        return "Grid";
    }

    void DrawAssetBrowserContentViewControls(AssetBrowserPanelState& state) noexcept
    {
        ImGui::SetNextItemWidth(editor::detail::EditorVisualPx(128.0f));
        if (ImGui::BeginCombo("##AssetContentViewMode", AssetBrowserContentViewModeName(state.contentView.mode)))
        {
            constexpr editor::detail::AssetBrowserContentViewMode Modes[]{
                editor::detail::AssetBrowserContentViewMode::Grid,
                editor::detail::AssetBrowserContentViewMode::CompactGrid,
                editor::detail::AssetBrowserContentViewMode::Details};
            for (const editor::detail::AssetBrowserContentViewMode mode : Modes)
            {
                if (ImGui::Selectable(AssetBrowserContentViewModeName(mode), state.contentView.mode == mode))
                {
                    state.contentView.mode = mode;
                    MarkAssetBrowserPersistentStateDirty(state);
                }
            }
            ImGui::EndCombo();
        }
        if (state.contentView.mode == editor::detail::AssetBrowserContentViewMode::Details)
            return;

        ImGui::SameLine();
        float& extent = state.contentView.mode == editor::detail::AssetBrowserContentViewMode::CompactGrid
                            ? state.contentView.compactThumbnailExtent
                            : state.contentView.thumbnailExtent;
        const float minimum = state.contentView.mode == editor::detail::AssetBrowserContentViewMode::CompactGrid
                                  ? AssetBrowserCompactGridMinimumThumbnailExtent
                                  : AssetBrowserGridMinimumThumbnailExtent;
        const float maximum = state.contentView.mode == editor::detail::AssetBrowserContentViewMode::CompactGrid
                                  ? AssetBrowserCompactGridMaximumThumbnailExtent
                                  : AssetBrowserGridMaximumThumbnailExtent;
        ImGui::SetNextItemWidth(editor::detail::EditorVisualPx(140.0f));
        if (ImGui::SliderFloat("##AssetThumbnailExtent", &extent, minimum, maximum, "%.0f"))
            MarkAssetBrowserPersistentStateDirty(state);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Thumbnail size (logical UI units; Ctrl+Mouse Wheel over the content view)");
    }

    void DrawAssetBrowserContentPane(editor::EditorPanelContext& panelContext, AssetBrowserPanelState& state,
                                     const assetStreaming::ProjectAssetCatalog& catalog) noexcept
    {
        ClaimExternalDropForPanel(panelContext, state);
        if (state.folderPath[0] != '\0')
        {
            if (ImGui::SmallButton("Up"))
                static_cast<void>(NavigatePanelToParentFolder(state, catalog));
            ImGui::SameLine();
        }
        DrawFolderBreadcrumb(state, catalog);
        ImGui::Separator();
        if (state.queryDirty)
        {
            ResetPanelTextureThumbnails(state);
            BeginPanelQuery(state, catalog);
        }
        assetStreaming::ProjectAssetCatalogQueryStatus queryStatus = state.query.GetStatus();
        if (queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Running)
        {
            static_cast<void>(catalog.StepQuery(
                state.query, AssetBrowserQueryRowsPerFrame, AssetBrowserQueryPathBytesPerFrame));
            queryStatus = state.query.GetStatus();
        }

        assetStreaming::ProjectAssetCatalogFolderInfo contentFolderInfo;
        const bool hasContentFolderInfo = catalog.GetFolderInfo(state.folder, contentFolderInfo);
        const bool showContentFolders = hasContentFolderInfo && !state.recursive && state.search[0] == '\0' &&
                                        state.importFilter < 0;
        const vanguard::u32 contentFolderCount = showContentFolders ? contentFolderInfo.childFolderCount : 0u;
        ImGui::Text("%u folder%s, %u asset%s", contentFolderCount, contentFolderCount == 1u ? "" : "s",
                    queryStatus.matches, queryStatus.matches == 1u ? "" : "s");
        if (queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Running)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Searching %u / %u", queryStatus.scanned, queryStatus.candidates);
        }
        if (queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Stale)
        {
            static_cast<void>(editor::detail::FenceStaleAssetBrowserBrowseQuery(state, queryStatus));
            ImGui::TextDisabled("Catalog changed; query will refresh.");
        }
        else if (queryStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Failed)
            ImGui::TextDisabled("Query unavailable within the bounded catalog budget.");

        DrawAssetBrowserContentViewControls(state);

        const AssetBrowserSelectionSummary selectionSummary = BuildAssetBrowserSelectionSummary(state, catalog);
        const float selectionStripHeight = AssetBrowserSelectionStripHeight(
            selectionSummary, ImGui::GetContentRegionAvail().x);
        const float viewSpacing = ImGui::GetStyle().ItemSpacing.y;
        const float selectionSeparatorHeight = std::max(1.0f, editor::detail::EditorVisualPx(2.0f));
        const float contentViewHeight = std::max(
            1.0f, ImGui::GetContentRegionAvail().y - selectionStripHeight - viewSpacing - selectionSeparatorHeight);

        switch (state.contentView.mode)
        {
        case editor::detail::AssetBrowserContentViewMode::Details:
            DrawAssetBrowserDetailsView(panelContext, state, catalog, contentFolderCount, queryStatus,
                                        contentViewHeight);
            break;
        case editor::detail::AssetBrowserContentViewMode::Grid:
            DrawAssetBrowserGridView(panelContext, state, catalog, contentFolderCount, queryStatus, false,
                                     contentViewHeight);
            break;
        case editor::detail::AssetBrowserContentViewMode::CompactGrid:
            DrawAssetBrowserGridView(panelContext, state, catalog, contentFolderCount, queryStatus, true,
                                     contentViewHeight);
            break;
        }
        const ImVec2 separatorMinimum = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            separatorMinimum,
            {separatorMinimum.x + ImGui::GetContentRegionAvail().x, separatorMinimum.y},
            ImGui::GetColorU32(ImGuiCol_Border), selectionSeparatorHeight);
        DrawAssetBrowserSelectionStrip(selectionSummary, selectionStripHeight);
    }

    void DrawAssetBrowserPanel(editor::EditorPanelContext& panelContext, void* const panel, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(panel);
        if (state == nullptr || state->extension == nullptr)
            return;
        // A context-menu command is invoked after this panel body is authored in
        // the same BuildingFrame. If that command cleared the selection after the
        // selected preview authored an ImGui texture identity, its unregister is
        // deliberately deferred until EndFrame/later frame. Retry that deferred
        // release before any early-returning catalog path on the next panel draw.
        if (!state->selectedRow.IsValid() &&
            (state->previewCandidate.IsValid() || state->previewDisplayed.IsValid() ||
             state->previewTexture.IsValid() || state->previewOutput.IsValid()))
            ResetSelectedTexturePreview(*state);
        const assetStreaming::ProjectAssetCatalog* const catalog = state->extension->assets.GetCatalog();
        if (catalog == nullptr)
        {
            ImGui::TextDisabled("Project asset catalog is unavailable.");
            return;
        }
        const assetStreaming::ProjectAssetCatalogStatus catalogStatus = catalog->GetStatus();
        if (!SyncCatalog(*state, *catalog))
        {
            ImGui::TextDisabled("Project asset catalog has no published source generation.");
            return;
        }
        if (catalogStatus.lastResult != assetStreaming::ProjectAssetCatalogResult::Success)
            ImGui::TextDisabled("Catalog refresh failed; showing the previous complete generation.");
        else if (catalogStatus.refreshState != assetStreaming::ProjectAssetCatalogRefreshState::Idle)
            ImGui::TextDisabled("Catalog refresh in progress; showing the previous complete generation.");

        DrawAssetBrowserToolbar(*state);
        DrawExternalImportStatus(*state);
        DrawOperationStatus(*state);
        DrawTargetedOperationDialog(*state);
        DrawDeleteConfirmationDialog(*state);

        const float browserLayoutWidth = ImGui::GetContentRegionAvail().x;
        const bool stackFolderPane = browserLayoutWidth < 560.0f;
        const float folderPaneHeight = std::min(220.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.28f));
        if (stackFolderPane)
        {
            ImGui::BeginChild("##AssetBrowserFolders", ImVec2(0.0f, folderPaneHeight), true);
            DrawAssetBrowserNavigationPane(*state, *catalog);
            ImGui::EndChild();
            ImGui::Separator();
            ImGui::BeginChild("##AssetBrowserContent", ImVec2(0.0f, 0.0f), false);
            DrawAssetBrowserContentPane(panelContext, *state, *catalog);
            ImGui::EndChild();
        }
        else
        {
            const ImGuiTableFlags splitFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
            if (ImGui::BeginTable("##AssetBrowserSplit", 2, splitFlags, ImVec2(0.0f, 0.0f)))
            {
                state->contentView.navigationSplitRatio = std::clamp(
                    state->contentView.navigationSplitRatio,
                    editor::detail::AssetBrowserMinimumNavigationSplitRatio,
                    editor::detail::AssetBrowserMaximumNavigationSplitRatio);
                ImGui::TableSetupColumn(
                    "Folders", ImGuiTableColumnFlags_WidthFixed,
                    browserLayoutWidth * state->contentView.navigationSplitRatio);
                ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch, 1.0f);

                if (ImGuiTable* const table = ImGui::GetCurrentTable(); table != nullptr && table->ColumnsCount >= 2)
                {
                    const float workWidth = std::max(1.0f, table->WorkRect.GetWidth());
                    if (table->LastResizedColumn == 0)
                    {
                        const float resizedRatio = std::clamp(
                            table->Columns[0].WidthRequest / workWidth,
                            editor::detail::AssetBrowserMinimumNavigationSplitRatio,
                            editor::detail::AssetBrowserMaximumNavigationSplitRatio);
                        if (std::fabs(resizedRatio - state->contentView.navigationSplitRatio) > 0.0005f)
                        {
                            state->contentView.navigationSplitRatio = resizedRatio;
                            MarkAssetBrowserPersistentStateDirty(*state);
                        }
                    }
                    else
                    {
                        table->Columns[0].WidthRequest = workWidth * state->contentView.navigationSplitRatio;
                    }
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##AssetBrowserFolders", ImVec2(0.0f, 0.0f), false);
                DrawAssetBrowserNavigationPane(*state, *catalog);
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##AssetBrowserContent", ImVec2(0.0f, 0.0f), false);
                DrawAssetBrowserContentPane(panelContext, *state, *catalog);
                ImGui::EndChild();
                ImGui::EndTable();
            }
        }
    }

    void* CreateAssetBrowserPanel(const editor::EditorPanelInstanceId instance, void* const factoryData) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(AssetBrowserPanelState),
                                       alignof(AssetBrowserPanelState));
        if (!block)
            return nullptr;
        auto* const state = ::new (block.address) AssetBrowserPanelState();
        state->extension = static_cast<AssetBrowserExtensionState*>(factoryData);
        state->instance = instance;
        if (state->extension != nullptr)
        {
            if (instance == AssetBrowserPrimaryInstanceId)
                state->extension->primaryState = state;
            else if (instance == AssetBrowserSecondaryInstanceId)
                state->extension->secondaryState = state;
        }
        return state;
    }

    void DestroyAssetBrowserPanel(void* const panel, void*) noexcept
    {
        if (panel == nullptr)
            return;
        auto* const state = static_cast<AssetBrowserPanelState*>(panel);
        ClearExternalImportWork(*state, true);
        ResetPanelTextureThumbnails(*state);
        ResetSelectedTexturePreview(*state);
        if (state->extension != nullptr)
        {
            if (state->extension->primaryState == state)
                state->extension->primaryState = nullptr;
            if (state->extension->secondaryState == state)
                state->extension->secondaryState = nullptr;
        }
        state->~AssetBrowserPanelState();
        vanguard::memory::MemoryBlock block{state, sizeof(AssetBrowserPanelState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    void AssetBrowserPanelVisibilityChanged(void* const panel, const bool open, void*) noexcept
    {
        if (!open && panel != nullptr)
        {
            auto& state = *static_cast<AssetBrowserPanelState*>(panel);
            ClearExternalImportWork(state, true);
            ResetPanelTextureThumbnails(state);
            ResetSelectedTexturePreview(state);
        }
    }

    editor::EditorCommandState QueryRefreshAssetBrowser(void* const receiverData, void*) noexcept
    {
        return receiverData != nullptr ? editor::EditorCommandState::Enabled() : editor::EditorCommandState::Disabled();
    }

    bool InvokeRefreshAssetBrowser(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (state == nullptr)
            return false;
        state->queryDirty = true;
        ClearSelection(*state);
        return true;
    }

    [[nodiscard]] editor::EditorCommandState QueryCandidateMutation(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || !state->selectedRow.IsValid() || state->selectedHasAsset ||
            state->selectedImportState != assets::SourceImportState::Candidate)
            return editor::EditorCommandState::Unsupported();
        if (state->extension == nullptr || state->selectedReadOnly || !state->selectedSourceIdentity.IsValid() ||
            state->selectedImporter == assets::InvalidSourceImporterId || state->selectedImporterVersion == 0u ||
            state->selectedSourceGeneration == 0u || !state->extension->assets.CanMutateAssets() ||
            !state->extension->assets.CanImportCandidate(state->selectedSourceIdentity, state->selectedImporter,
                                                         state->selectedImporterVersion, state->selectedSourceGeneration))
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled();
    }

    [[nodiscard]] bool HasAdoptedSelection(const AssetBrowserPanelState& state) noexcept
    {
        return state.extension != nullptr && state.selectedRow.IsValid() && state.selectedHasAsset &&
               state.selectedAsset.IsValid() && state.selectedAcceptedGeneration != 0u &&
               state.selectedSourceGeneration != 0u;
    }

    [[nodiscard]] bool HasCurrentAdoptedSelection(const AssetBrowserPanelState& state) noexcept
    {
        if (!HasAdoptedSelection(state))
            return false;
        const bool sourceGenerationCurrent =
            state.extension->assets.IsSourceGenerationCurrent(state.selectedSourceGeneration);
        vanguard::u64 acceptedGeneration = 0u;
        const bool acceptedGenerationResolved =
            state.extension->assets.GetAcceptedAssetGeneration(state.selectedAsset, acceptedGeneration);
        return editor::detail::IsAssetBrowserMutationSelectionCurrent(
            sourceGenerationCurrent, state.selectedAcceptedGeneration, acceptedGenerationResolved, acceptedGeneration);
    }

    [[nodiscard]] bool HasMutableCurrentAdoptedSelection(const AssetBrowserPanelState& state) noexcept
    {
        return !state.selectedReadOnly && HasCurrentAdoptedSelection(state);
    }

    [[nodiscard]] editor::EditorCommandState QueryAdoptedOperation(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || !state->selectedHasAsset)
            return editor::EditorCommandState::Unsupported();
        if (!editor::detail::CanRunAssetBrowserAdoptedOperation(
                HasCurrentAdoptedSelection(*state), state->selectedReadOnly, false,
                state->extension->assets.CanMutateAssets()))
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled();
    }

    [[nodiscard]] editor::EditorCommandState QueryAdoptedMutation(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || !state->selectedHasAsset)
            return editor::EditorCommandState::Unsupported();
        if (!editor::detail::CanRunAssetBrowserAdoptedOperation(
                HasCurrentAdoptedSelection(*state), state->selectedReadOnly, true,
                state->extension->assets.CanMutateAssets()))
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled();
    }

    [[nodiscard]] editor::EditorCommandState QueryOpenSourceLocation(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || !state->selectedRow.IsValid() || !state->selectedSourceIdentity.IsValid())
            return editor::EditorCommandState::Unsupported();
        if (state->extension == nullptr || state->extension->fileDialogs == nullptr ||
            !state->extension->assets.IsSourceGenerationCurrent(state->selectedSourceGeneration))
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled();
    }

    bool InvokeImportAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryCandidateMutation(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        assetStreaming::ProjectAssetPipelineFailure failure;
        assets::AssetId imported;
        const auto result = state->extension->assets.ImportCandidate(
            state->selectedSourceIdentity, state->selectedImporter, state->selectedImporterVersion,
            state->selectedSourceGeneration, imported, &failure);
        RecordOperation(*state, AssetBrowserOperationKind::Import, result, failure, imported);
        if (IsCommittedOperationResult(result))
            ClearSelection(*state);
        return IsCommittedOperationResult(result);
    }

    bool InvokeReimportAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryAdoptedOperation(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        assetStreaming::ProjectAssetPipelineFailure failure;
        const auto result = state->extension->assets.RequestAssetReimport(
            state->selectedAsset, state->selectedAcceptedGeneration, state->selectedSourceGeneration, &failure);
        RecordOperation(*state, AssetBrowserOperationKind::Reimport, result, failure);
        return IsCommittedOperationResult(result);
    }

    bool InvokeOpenSourceLocation(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryOpenSourceLocation(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        filesystem::AbsolutePath sourcePath;
        if (!state->extension->assets.ResolveSourcePhysicalPath(
                state->selectedSourceIdentity, state->selectedSourceGeneration, sourcePath))
            return false;
        editor::EditorFileDialogFailure failure;
        return state->extension->fileDialogs->RevealFileLocation(sourcePath.AsChar(), &failure);
    }

    bool InvokeMoveAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        return state != nullptr && PrepareTargetedOperation(*state, AssetBrowserTargetedOperationKind::Move);
    }

    bool InvokeDuplicateAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        return state != nullptr && PrepareTargetedOperation(*state, AssetBrowserTargetedOperationKind::Duplicate);
    }

    [[nodiscard]] editor::EditorCommandState QueryDeleteAsset(void* const receiverData, void*) noexcept
    {
        return QueryAdoptedMutation(receiverData, nullptr);
    }

    bool InvokeDeleteAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryDeleteAsset(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        state->deleteArmed = true;
        state->deleteOpenRequested = true;
        return true;
    }

    [[nodiscard]] editor::EditorCommandState QueryUndoAsset(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || state->extension == nullptr)
            return editor::EditorCommandState::Unsupported();
        return state->extension->assets.CanUndoAssetOperation() ? editor::EditorCommandState::Enabled()
                                                                : editor::EditorCommandState::Disabled();
    }

    [[nodiscard]] editor::EditorCommandState QueryRedoAsset(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || state->extension == nullptr)
            return editor::EditorCommandState::Unsupported();
        return state->extension->assets.CanRedoAssetOperation() ? editor::EditorCommandState::Enabled()
                                                                : editor::EditorCommandState::Disabled();
    }

    bool InvokeUndoAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryUndoAsset(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        assetStreaming::ProjectAssetPipelineFailure failure;
        const auto result = state->extension->assets.UndoAssetOperation(&failure);
        RecordOperation(*state, AssetBrowserOperationKind::Undo, result, failure);
        if (IsCommittedOperationResult(result))
            ClearSelection(*state);
        return IsCommittedOperationResult(result);
    }

    bool InvokeRedoAsset(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryRedoAsset(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        assetStreaming::ProjectAssetPipelineFailure failure;
        const auto result = state->extension->assets.RedoAssetOperation(&failure);
        RecordOperation(*state, AssetBrowserOperationKind::Redo, result, failure);
        if (IsCommittedOperationResult(result))
            ClearSelection(*state);
        return IsCommittedOperationResult(result);
    }

    [[nodiscard]] editor::EditorCommandState QueryApplyTextureSettings(void* const receiverData, void*) noexcept
    {
        const auto* const state = static_cast<const AssetBrowserPanelState*>(receiverData);
        if (state == nullptr || !state->textureSettingsSupported)
            return editor::EditorCommandState::Unsupported();
        if (!state->textureSettingsDirty || !HasMutableCurrentAdoptedSelection(*state) ||
            !state->extension->assets.CanMutateAssets())
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled();
    }

    bool InvokeApplyTextureSettings(void* const receiverData, void*) noexcept
    {
        auto* const state = static_cast<AssetBrowserPanelState*>(receiverData);
        if (QueryApplyTextureSettings(receiverData, nullptr).kind != editor::EditorCommandStateKind::Enabled)
            return false;
        vanguard::containers::DynamicArray<vanguard::u8> encoded(vanguard::memory::pools::Assets::GetInstance());
        assetStreaming::ProjectAssetPipelineFailure failure;
        if (textureTools::EncodeTextureBuildSettings(state->textureSettings, encoded) !=
            textureTools::TextureBuildSettingsResult::Success)
        {
            failure.result = assetStreaming::ProjectAssetPipelineResult::OperationFailed;
            failure.operation.result = assets::AssetOperationResult::SettingsInvalid;
            failure.operation.source = state->selectedAsset;
            RecordOperation(*state, AssetBrowserOperationKind::ApplyTextureSettings,
                            assetStreaming::ProjectAssetPipelineResult::OperationFailed, failure);
            return false;
        }
        const auto result = state->extension->assets.UpdateAssetSettings(
            state->selectedAsset, state->selectedAcceptedGeneration, state->selectedSourceGeneration,
            textureTools::TextureBuildSettingsVersion,
            {encoded.TypedData(), encoded.Size()}, &failure);
        RecordOperation(*state, AssetBrowserOperationKind::ApplyTextureSettings, result, failure);
        if (IsCommittedOperationResult(result))
            state->selectedSourceGeneration = 0u;
        return IsCommittedOperationResult(result);
    }

    bool ActivateAssetBrowser(editor::EditorExtensionActivationContext& context, const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(AssetBrowserExtensionState),
                                       alignof(AssetBrowserExtensionState));
        if (!block)
            return false;
        auto* const state = ::new (block.address) AssetBrowserExtensionState();
        state->assets = context.GetProjectAssets();
        state->previewPresentation = context.GetProjectTexturePreviewPresentation();
        state->panels = context.GetOwnedPanels();
        state->ui = &context.GetUi();
        state->fileDialogs = context.GetFileDialogs();
        if (state->fileDialogs == nullptr)
        {
            state->previewSampler.Reset();
            state->~AssetBrowserExtensionState();
            vanguard::memory::MemoryBlock failedBlock{state, sizeof(AssetBrowserExtensionState), vanguard::memory::PoolId::Tools};
            vanguard::memory::Free(failedBlock);
            return false;
        }
        rhi::SamplerStateDesc samplerDesc;
        samplerDesc.addressU = rhi::SamplerAddressMode::Clamp;
        samplerDesc.addressV = rhi::SamplerAddressMode::Clamp;
        samplerDesc.addressW = rhi::SamplerAddressMode::Clamp;
        rhi::Failure samplerFailure;
        state->previewSampler = rhi::SamplerState(rhi::AdoptReference, rhi::RequestSamplerState(samplerDesc, &samplerFailure));
        if (!state->previewSampler.IsValid())
        {
            state->~AssetBrowserExtensionState();
            vanguard::memory::MemoryBlock failedBlock{state, sizeof(AssetBrowserExtensionState), vanguard::memory::PoolId::Tools};
            vanguard::memory::Free(failedBlock);
            return false;
        }
        privateState = state;

        editor::EditorCommandFailure commandFailure;
        editor::EditorUiFailure uiFailure;
        editor::EditorCommandContextTypeToken contextToken;
        editor::EditorCommandContextTypeDescriptor contextType;
        contextType.id = AssetBrowserContextTypeId;
        contextType.name = "Asset Browser";
        contextType.tier = editor::EditorCommandContextTier::Local;
        if (!context.RegisterContextType(contextType, contextToken, &commandFailure))
            return false;

        struct CommandRegistration final
        {
            editor::EditorCommandId id;
            const char* name = nullptr;
            editor::EditorCommandQueryCallback query = nullptr;
            editor::EditorCommandInvokeCallback invoke = nullptr;
            editor::EditorShortcutChord shortcut;
        };
        const CommandRegistration commands[]{
            {RefreshAssetBrowserCommandId, "Refresh Asset Browser Query", QueryRefreshAssetBrowser, InvokeRefreshAssetBrowser},
            {ImportAssetCommandId, "Import Candidate", QueryCandidateMutation, InvokeImportAsset},
            {ReimportAssetCommandId, "Reimport Asset", QueryAdoptedOperation, InvokeReimportAsset},
            {OpenSourceLocationCommandId, "Open Source Location", QueryOpenSourceLocation, InvokeOpenSourceLocation},
            {MoveAssetCommandId, "Move / Rename Asset", QueryAdoptedMutation, InvokeMoveAsset},
            {DuplicateAssetCommandId, "Duplicate Asset", QueryAdoptedMutation, InvokeDuplicateAsset},
            {DeleteAssetCommandId, "Delete Asset", QueryDeleteAsset, InvokeDeleteAsset},
            {UndoAssetCommandId, "Undo Asset Operation", QueryUndoAsset, InvokeUndoAsset,
             {vanguard::input::Key::Z, editor::EditorShortcutModifiers::Control}},
            {RedoAssetCommandId, "Redo Asset Operation", QueryRedoAsset, InvokeRedoAsset,
             {vanguard::input::Key::Y, editor::EditorShortcutModifiers::Control}},
            {ApplyTextureSettingsCommandId, "Apply Texture Settings", QueryApplyTextureSettings, InvokeApplyTextureSettings}};
        for (const CommandRegistration& registration : commands)
        {
            editor::EditorCommandToken commandToken;
            const editor::EditorCommandDescriptor command{registration.id, registration.name, editor::EditorCommandFlags::None};
            if (!context.RegisterCommand(command, commandToken, &commandFailure))
                return false;
            editor::EditorCommandHandlerToken handlerToken;
            editor::EditorCommandHandlerDescriptor handler;
            handler.contextType = AssetBrowserContextTypeId;
            handler.command = registration.id;
            handler.query = registration.query;
            handler.invoke = registration.invoke;
            handler.defaultShortcut = registration.shortcut;
            if (!context.RegisterHandler(handler, handlerToken, &commandFailure))
                return false;
        }

        editor::detail::EditorUiSurfaceToken contextSurfaceToken;
        if (!context.RegisterContextMenuSurface(AssetBrowserContextMenuSurfaceId, contextSurfaceToken, &uiFailure))
            return false;

        struct ContributionRegistration final
        {
            editor::EditorUiContributionId id;
            editor::EditorCommandId command;
            const char* label = nullptr;
            vanguard::i32 order = 0;
        };
        const ContributionRegistration contextContributions[]{
            {ImportAssetContextContributionId, ImportAssetCommandId, "Import Candidate", 10},
            {ReimportAssetContextContributionId, ReimportAssetCommandId, "Reimport", 20},
            {OpenSourceLocationContextContributionId, OpenSourceLocationCommandId, "Open Source Location", 25},
            {MoveAssetContextContributionId, MoveAssetCommandId, "Move / Rename...", 30},
            {DuplicateAssetContextContributionId, DuplicateAssetCommandId, "Duplicate...", 40},
            {DeleteAssetContextContributionId, DeleteAssetCommandId, "Delete...", 50},
            {ApplyTextureSettingsContextContributionId, ApplyTextureSettingsCommandId, "Apply Texture Settings", 60},
            {UndoAssetContextContributionId, UndoAssetCommandId, "Undo Asset Operation", 70},
            {RedoAssetContextContributionId, RedoAssetCommandId, "Redo Asset Operation", 80}};
        for (const ContributionRegistration& registration : contextContributions)
        {
            editor::detail::EditorUiContributionToken token;
            editor::detail::EditorUiContributionDesc contribution;
            contribution.id = registration.id;
            contribution.surface = AssetBrowserContextMenuSurfaceId;
            contribution.kind = editor::detail::EditorUiContributionKind::Command;
            contribution.order = registration.order;
            contribution.command = registration.command;
            contribution.label = registration.label;
            if (!context.RegisterSurfaceContribution(contribution, token, &uiFailure))
                return false;
        }

        editor::detail::EditorUiContributionToken undoMainToken;
        editor::detail::EditorUiContributionDesc undoMain;
        undoMain.id = UndoAssetMainMenuContributionId;
        undoMain.surface = editor::detail::EditorMainMenuSurfaceId;
        undoMain.kind = editor::detail::EditorUiContributionKind::Command;
        undoMain.parent = editor::detail::EditorMainMenuEditAnchorId;
        undoMain.order = 300;
        undoMain.command = UndoAssetCommandId;
        undoMain.label = "Undo Asset Operation";
        if (!context.RegisterSurfaceContribution(undoMain, undoMainToken, &uiFailure))
            return false;

        editor::detail::EditorUiContributionToken redoMainToken;
        editor::detail::EditorUiContributionDesc redoMain;
        redoMain.id = RedoAssetMainMenuContributionId;
        redoMain.surface = editor::detail::EditorMainMenuSurfaceId;
        redoMain.kind = editor::detail::EditorUiContributionKind::Command;
        redoMain.parent = editor::detail::EditorMainMenuEditAnchorId;
        redoMain.order = 310;
        redoMain.command = RedoAssetCommandId;
        redoMain.label = "Redo Asset Operation";
        if (!context.RegisterSurfaceContribution(redoMain, redoMainToken, &uiFailure))
            return false;

        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = AssetBrowserPanelTypeId;
        panelType.name = "Asset Browser";
        panelType.create = CreateAssetBrowserPanel;
        panelType.destroy = DestroyAssetBrowserPanel;
        panelType.draw = DrawAssetBrowserPanel;
        panelType.visibilityChanged = AssetBrowserPanelVisibilityChanged;
        panelType.factoryData = state;
        panelType.persistence.stateVersion = AssetBrowserPanelStateVersion;
        panelType.persistence.capture = CaptureAssetBrowserState;
        panelType.persistence.restore = RestoreAssetBrowserState;
        panelType.persistence.migrate = MigrateAssetBrowserState;
        panelType.persistence.getRevision = GetAssetBrowserStateRevision;
        panelType.policy = editor::EditorPanelTypePolicy::Multiple;
        panelType.initialPlacement = editor::EditorPanelInitialPlacement::Bottom;
        const editor::EditorPanelLocalContextBinding localContext{AssetBrowserContextTypeId,
                                                                 editor::EditorCommandContextMode::Inherit};
        if (!context.RegisterPanelType(panelType, localContext, panelTypeToken, &uiFailure))
            return false;

        if (!state->panels.CreatePanel({AssetBrowserPrimaryInstanceId, AssetBrowserPanelTypeId, "Asset Browser", true},
                                       state->primaryPanel, &uiFailure))
            return false;
        return state->panels.CreatePanel({AssetBrowserSecondaryInstanceId, AssetBrowserPanelTypeId, "Asset Browser 2", false},
                                         state->secondaryPanel, &uiFailure);
    }

    void DeactivateAssetBrowser(editor::EditorExtensionActivationContext&, const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<AssetBrowserExtensionState*>(privateState);
        if (state->primaryState != nullptr)
            ClearExternalImportWork(*state->primaryState, true);
        if (state->secondaryState != nullptr)
            ClearExternalImportWork(*state->secondaryState, true);
        ResetAllTextureThumbnails(*state);
        if (state->primaryState != nullptr)
            ResetSelectedTexturePreview(*state->primaryState);
        if (state->secondaryState != nullptr)
            ResetSelectedTexturePreview(*state->secondaryState);
        state->previewSampler.Reset();
        state->ui = nullptr;
        state->fileDialogs = nullptr;
        state->~AssetBrowserExtensionState();
        vanguard::memory::MemoryBlock block{state, sizeof(AssetBrowserExtensionState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor AssetBrowserExtensions[]{
        {AssetBrowserExtensionId,
         "editor-asset-browser",
         "Asset Browser",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivateAssetBrowser,
         DeactivateAssetBrowser,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorAssetBrowserExtensions() noexcept
    {
        return AssetBrowserExtensions;
    }
} // namespace vanguard::editor
