#pragma once

#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/containers/containers.hpp>

#include <atomic>

namespace vanguard::editor::detail
{
    inline constexpr EditorUiSurfaceId EditorMainMenuSurfaceId{0x65647569736d6e01ull};
    inline constexpr EditorUiSurfaceId EditorMainToolbarSurfaceId{0x6564756973746201ull};

    inline constexpr EditorUiContributionId EditorMainMenuFileAnchorId{0x656475696d6e6601ull};
    inline constexpr EditorUiContributionId EditorMainMenuEditAnchorId{0x656475696d6e6501ull};
    inline constexpr EditorUiContributionId EditorMainMenuViewAnchorId{0x656475696d6e7601ull};
    inline constexpr EditorUiContributionId EditorMainMenuWindowAnchorId{0x656475696d6e7701ull};
    inline constexpr EditorUiContributionId EditorMainMenuToolsAnchorId{0x656475696d6e7401ull};
    inline constexpr EditorUiContributionId EditorMainMenuHelpAnchorId{0x656475696d6e6801ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutSubmenuId{0x656475696d6c7901ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutSaveAsId{0x656475696d6c7902ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutSaveId{0x656475696d6c7903ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutSwitchId{0x656475696d6c7904ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutRevertId{0x656475696d6c7905ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutRenameId{0x656475696d6c7906ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutDuplicateId{0x656475696d6c7907ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutDeleteId{0x656475696d6c7908ull};
    inline constexpr EditorUiContributionId EditorMainMenuLayoutResetDefaultId{0x656475696d6c790aull};
    inline constexpr EditorUiContributionId EditorMainToolbarStartAnchorId{0x6564756974627301ull};
    inline constexpr EditorUiContributionId EditorMainToolbarEndAnchorId{0x6564756974626501ull};

    enum class EditorUiSurfaceKind : u8
    {
        MainMenu,
        MainToolbar,
        ContextMenu
    };

    enum class EditorUiContributionKind : u8
    {
        Command,
        Tool,
        Submenu,
        Separator
    };

    enum class EditorUiPlacementRelation : u8
    {
        Append,
        Before,
        After
    };

    enum class EditorUiMissingAnchorPolicy : u8
    {
        Required,
        FallbackAppend
    };

    struct EditorUiSurfaceToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorUiSurfaces && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorUiSurfaceToken, EditorUiSurfaceToken) noexcept = default;
    };

    struct EditorUiContributionToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorUiContributions && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorUiContributionToken, EditorUiContributionToken) noexcept = default;
    };

    inline constexpr EditorUiSurfaceToken InvalidEditorUiSurfaceToken{};
    inline constexpr EditorUiContributionToken InvalidEditorUiContributionToken{};

    struct EditorUiSurfaceDesc final
    {
        EditorUiSurfaceId id;
        EditorUiSurfaceKind kind = EditorUiSurfaceKind::ContextMenu;
    };

    struct EditorUiContributionDesc final
    {
        EditorUiContributionId id;
        EditorUiSurfaceId surface;
        EditorUiContributionKind kind = EditorUiContributionKind::Command;
        EditorUiContributionId parent;
        EditorUiContributionId sibling;
        EditorUiPlacementRelation placement = EditorUiPlacementRelation::Append;
        EditorUiMissingAnchorPolicy missingAnchor = EditorUiMissingAnchorPolicy::Required;
        i32 order = 0;
        EditorCommandId command;
        EditorResourceId icon = InvalidEditorResourceId;
        const char* label = nullptr;
    };

    struct EditorUiSurfaceView final
    {
        EditorUiSurfaceToken token;
        EditorUiSurfaceId id;
        EditorUiSurfaceKind kind = EditorUiSurfaceKind::ContextMenu;
        EditorExtensionOwner owner;
        bool builtIn = false;
    };

    struct EditorUiContributionView final
    {
        EditorUiContributionToken token;
        EditorUiContributionId id;
        EditorUiSurfaceId surface;
        EditorUiContributionKind kind = EditorUiContributionKind::Command;
        EditorUiContributionId parent;
        EditorUiContributionId sibling;
        EditorUiPlacementRelation placement = EditorUiPlacementRelation::Append;
        i32 order = 0;
        EditorCommandId command;
        EditorResourceId icon = InvalidEditorResourceId;
        const char* label = nullptr;
        EditorExtensionOwner owner;
        u32 depth = 0;
        bool builtInAnchor = false;
        bool fallbackPlacement = false;
    };

    using EditorUiSurfaceVisitor = void (*)(const EditorUiSurfaceView& surface, void* userData) noexcept;
    using EditorUiContributionVisitor = void (*)(const EditorUiContributionView& contribution, void* userData) noexcept;

    using EditorUiQueryCommandPresentation = bool (*)(EditorCommandId command, EditorCommandPresentation& presentation,
                                                      EditorCommandFailure* failure, void* userData) noexcept;
    using EditorUiInvokeCommand = bool (*)(EditorCommandId command, EditorCommandFailure* failure, void* userData) noexcept;
    using EditorUiValidateCommandOwner = bool (*)(const EditorExtensionOwner& owner, EditorCommandId command,
                                                  EditorCommandFailure* failure, void* userData) noexcept;

    struct EditorUiCommandBridge final
    {
        EditorUiQueryCommandPresentation queryPresentation = nullptr;
        EditorUiInvokeCommand invoke = nullptr;
        EditorUiValidateCommandOwner validateOwner = nullptr;
        void* userData = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return queryPresentation != nullptr && invoke != nullptr && validateOwner != nullptr;
        }
    };

    struct EditorUiCommandBridgeToken final
    {
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorUiCommandBridgeToken, EditorUiCommandBridgeToken) noexcept = default;
    };

    struct EditorUiSurfacePoint final
    {
        float x = 0.0f;
        float y = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(EditorUiSurfacePoint, EditorUiSurfacePoint) noexcept = default;
    };

    struct EditorUiStampedContextMenuRequest final
    {
        EditorPanelHandle panel;
        EditorHostWindowHandle host;
        EditorExtensionOwner owner;
        EditorUiSurfaceId surfaceId;
        EditorUiSurfaceToken surface;
        u64 frame = 0;
        EditorUiSurfacePoint anchor;
    };

    struct EditorUiCommittedContextMenuRequest final
    {
        EditorPanelHandle panel;
        EditorHostWindowHandle host;
        EditorExtensionOwner owner;
        EditorUiSurfaceId surfaceId;
        EditorUiSurfaceToken surface;
        u64 frame = 0;
        EditorUiSurfacePoint anchor;
    };

    struct EditorUiContextMenuItemView final
    {
        EditorUiContributionView contribution;
        EditorCommandPresentation presentation;
        bool hasPresentation = false;
    };

    using EditorUiContextMenuItems =
        containers::StaticArray<EditorUiContextMenuItemView, MaximumEditorUiContributions>;

    struct EditorSurfaceRegistryStatus final
    {
        u32 surfaceCount = 0;
        u32 contributionCount = 0;
        u32 pendingRequestCount = 0;
        u32 fallbackPlacementCount = 0;
        EditorUiContributionId lastFallbackContribution;
        bool hasCommittedRequest = false;
        u32 commandFailureCount = 0;
        u32 repeatedCommandFailureCount = 0;
        EditorUiContributionId lastCommandFailureContribution;
        EditorCommandId lastCommandFailureCommand;
        EditorCommandFailureCode lastCommandFailureCode = EditorCommandFailureCode::None;
    };

    struct EditorSurfaceGenerationSource final
    {
        std::atomic<u32>* surfaceCounter = nullptr;
        std::atomic<u32>* contributionCounter = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return surfaceCounter != nullptr && contributionCounter != nullptr;
        }
    };

    [[nodiscard]] EditorSurfaceGenerationSource GetProductionEditorSurfaceGenerationSource() noexcept;

    class EditorUiCommandBridgeSlot final
    {
    public:
        explicit EditorUiCommandBridgeSlot(const EditorUiState& uiState) noexcept : m_uiState(&uiState) {}

        [[nodiscard]] bool Register(const EditorUiCommandBridge& bridge, EditorUiCommandBridgeToken& token,
                                    EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Unregister(EditorUiCommandBridgeToken token, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] const EditorUiCommandBridge* Get() const noexcept
        {
            return m_token.IsValid() && m_bridge.IsValid() ? &m_bridge : nullptr;
        }
        [[nodiscard]] bool IsAttached() const noexcept { return Get() != nullptr; }
        void Reset() noexcept
        {
            m_bridge = {};
            m_token = {};
        }

    private:
        const EditorUiState* m_uiState = nullptr;
        EditorUiCommandBridge m_bridge;
        EditorUiCommandBridgeToken m_token;
    };

    class EditorSurfaceRegistryCore final
    {
    public:
        EditorSurfaceRegistryCore(const EditorUiState& uiState, EditorSurfaceGenerationSource generations) noexcept;

        EditorSurfaceRegistryCore(const EditorSurfaceRegistryCore&) = delete;
        EditorSurfaceRegistryCore& operator=(const EditorSurfaceRegistryCore&) = delete;

        [[nodiscard]] bool InitializeBuiltIns(EditorUiFailure* failure = nullptr) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool RegisterSurface(const EditorExtensionOwner& owner, const EditorUiSurfaceDesc& desc,
                                           EditorUiSurfaceToken& token, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterSurface(const EditorExtensionOwner& owner, EditorUiSurfaceToken token,
                                             EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterContribution(const EditorExtensionOwner& owner, const EditorUiContributionDesc& desc,
                                                EditorUiContributionToken& token, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterContribution(const EditorExtensionOwner& owner, EditorUiContributionToken token,
                                                  EditorUiFailure* failure = nullptr) noexcept;
        void RevokeOwner(const EditorExtensionOwner& owner) noexcept;

        [[nodiscard]] bool ResolveSurface(EditorUiSurfaceId id, EditorUiSurfaceToken& token, EditorExtensionOwner& owner,
                                          EditorUiSurfaceKind& kind) const noexcept;
        void VisitSurfaces(EditorUiSurfaceVisitor visitor, void* userData = nullptr) const noexcept;
        void VisitContributions(EditorUiSurfaceId surface, EditorUiContributionVisitor visitor,
                                void* userData = nullptr) const noexcept;
        [[nodiscard]] bool QueryContributionPresentation(const EditorUiContributionView& contribution,
                                                         const EditorUiCommandBridge& bridge,
                                                         EditorCommandPresentation& presentation) noexcept;
        [[nodiscard]] bool InvokeContributionCommand(const EditorUiContributionView& contribution,
                                                     const EditorUiCommandBridge& bridge) noexcept;
        void BuildContextMenuItems(EditorUiSurfaceId surface, const EditorUiCommandBridge* bridge,
                                   EditorUiContextMenuItems& items) noexcept;

        [[nodiscard]] bool QueueContextMenuRequest(const EditorUiStampedContextMenuRequest& request,
                                                   EditorUiFailure* failure = nullptr) noexcept;
        void CommitContextMenuRequests(const EditorPanelFocusSnapshot& focus) noexcept;
        void DiscardPendingContextMenuRequests() noexcept;
        void CancelContextMenuRequestsForPanel(EditorPanelHandle panel) noexcept;
        void CancelContextMenuRequestsForHost(EditorHostWindowHandle host) noexcept;
        void CancelContextMenuRequestsForSurface(EditorUiSurfaceToken surface) noexcept;
        void CancelContextMenuRequestsForOwner(const EditorExtensionOwner& owner) noexcept;
        [[nodiscard]] bool PeekCommittedContextMenuRequest(EditorUiCommittedContextMenuRequest& request) const noexcept;
        void ClearCommittedContextMenuRequest() noexcept;

        [[nodiscard]] EditorSurfaceRegistryStatus GetStatus() const noexcept;

    private:
        struct SurfaceRecord final
        {
            EditorUiSurfaceToken token;
            EditorUiSurfaceDesc desc;
            EditorExtensionOwner owner;
            bool active = false;
            bool reachable = false;
            bool builtIn = false;
        };

        struct ContributionRecord final
        {
            EditorUiContributionToken token;
            EditorUiContributionDesc desc;
            EditorExtensionOwner owner;
            char label[MaximumEditorUiSurfaceStringBytes]{};
            u32 depth = 0;
            bool active = false;
            bool reachable = false;
            bool builtInAnchor = false;
            bool fallbackPlacement = false;
        };

        [[nodiscard]] bool CanMutateTopology(EditorUiFailure* failure, EditorUiSurfaceId surface = {},
                                             EditorUiContributionId contribution = {}) const noexcept;
        [[nodiscard]] u32 ReserveSurfaceGeneration() noexcept;
        [[nodiscard]] u32 ReserveContributionGeneration() noexcept;
        [[nodiscard]] u32 FindSurfaceSlot() const noexcept;
        [[nodiscard]] u32 FindContributionSlot() const noexcept;
        [[nodiscard]] SurfaceRecord* FindSurface(EditorUiSurfaceToken token) noexcept;
        [[nodiscard]] const SurfaceRecord* FindSurface(EditorUiSurfaceToken token) const noexcept;
        [[nodiscard]] SurfaceRecord* FindSurface(EditorUiSurfaceId id) noexcept;
        [[nodiscard]] const SurfaceRecord* FindSurface(EditorUiSurfaceId id) const noexcept;
        [[nodiscard]] ContributionRecord* FindContribution(EditorUiContributionToken token) noexcept;
        [[nodiscard]] const ContributionRecord* FindContribution(EditorUiContributionToken token) const noexcept;
        [[nodiscard]] ContributionRecord* FindContribution(EditorUiContributionId id) noexcept;
        [[nodiscard]] const ContributionRecord* FindContribution(EditorUiContributionId id) const noexcept;
        [[nodiscard]] bool IsPlacementOwnerAllowed(const EditorExtensionOwner& owner, const ContributionRecord& anchor) const noexcept;
        [[nodiscard]] const ContributionRecord* SelectNextInPlacementBucket(
            EditorUiSurfaceId surface, EditorUiContributionId parent, EditorUiPlacementRelation placement,
            EditorUiContributionId sibling, const bool (&emitted)[MaximumEditorUiContributions]) const noexcept;
        void VisitPlacementBucket(EditorUiSurfaceId surface, EditorUiContributionId parent,
                                  EditorUiPlacementRelation placement, EditorUiContributionId sibling,
                                  EditorUiContributionVisitor visitor, void* userData,
                                  bool (&emitted)[MaximumEditorUiContributions]) const noexcept;
        void VisitPlacedContribution(const ContributionRecord& contribution, EditorUiContributionVisitor visitor,
                                     void* userData, bool (&emitted)[MaximumEditorUiContributions]) const noexcept;
        void VisitChildren(EditorUiSurfaceId surface, EditorUiContributionId parent, EditorUiContributionVisitor visitor,
                           void* userData, bool (&emitted)[MaximumEditorUiContributions]) const noexcept;
        [[nodiscard]] bool ContextMenuParentHasVisibleContent(
            const EditorUiContextMenuItems& items,
            EditorUiContributionId parent) const noexcept;
        void MarkNormalizedContextMenuChildren(const EditorUiContextMenuItems& items, EditorUiContributionId parent,
                                               bool (&keep)[MaximumEditorUiContributions]) const noexcept;
        void RemovePendingRequestAt(u32 index) noexcept;
        void RecordCommandFailure(const EditorUiContributionView& contribution, EditorCommandFailureCode code) noexcept;

        const EditorUiState* m_uiState = nullptr;
        EditorSurfaceGenerationSource m_generations;
        SurfaceRecord m_surfaces[MaximumEditorUiSurfaces]{};
        ContributionRecord m_contributions[MaximumEditorUiContributions]{};
        containers::StaticArray<EditorUiStampedContextMenuRequest, MaximumPendingEditorContextMenuRequests> m_pendingRequests;
        EditorUiCommittedContextMenuRequest m_committedRequest;
        bool m_hasCommittedRequest = false;
        u32 m_fallbackPlacementCount = 0;
        EditorUiContributionId m_lastFallbackContribution;
        u32 m_commandFailureCount = 0;
        u32 m_repeatedCommandFailureCount = 0;
        EditorUiContributionId m_lastCommandFailureContribution;
        EditorCommandId m_lastCommandFailureCommand;
        EditorCommandFailureCode m_lastCommandFailureCode = EditorCommandFailureCode::None;
        bool m_initialized = false;
    };
} // namespace vanguard::editor::detail
