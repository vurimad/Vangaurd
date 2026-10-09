#pragma once

#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/containers/containers.hpp>

#include <atomic>

namespace vanguard::editor::detail
{
    struct EditorPanelGenerationSource final
    {
        std::atomic<u32>* typeCounter = nullptr;
        std::atomic<u32>* instanceCounter = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return typeCounter != nullptr && instanceCounter != nullptr;
        }
    };

    [[nodiscard]] EditorPanelGenerationSource GetProductionEditorPanelGenerationSource() noexcept;

    enum class EditorPanelInvalidationReason : u8
    {
        Close,
        Destroy,
        OwnerRevoke
    };

    using EditorPanelInvalidationCallback = bool (*)(EditorPanelHandle panel, EditorPanelInvalidationReason reason,
                                                     void* userData, EditorUiFailure* failure) noexcept;

    struct EditorPanelAuthoringView final
    {
        EditorPanelHandle handle;
        EditorExtensionOwner owner;
        EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        const char* title = nullptr;
        void* state = nullptr;
        DrawEditorPanel draw = nullptr;
        void* factoryData = nullptr;
        bool placeholder = false;
        EditorPanelInitialPlacement initialPlacement = EditorPanelInitialPlacement::Center;
    };

    using EditorPanelAuthoringVisitor = void (*)(const EditorPanelAuthoringView& panel, void* userData) noexcept;

    struct EditorPanelUserStateRecord final
    {
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        EditorPanelInstanceId instance = InvalidEditorPanelInstanceId;
        u16 fallbackTitleByteCount = 0;
        bool open = false;
        char fallbackTitle[MaximumEditorPanelNameBytes]{};
    };

    struct EditorPanelUserStateRestoreStatus final
    {
        u32 restored = 0;
        u32 created = 0;
        u32 placeholders = 0;
        u32 skipped = 0;
        EditorUiFailure firstSkippedFailure;
    };

    struct EditorPanelPersistentStateCapture final
    {
        EditorPanelStateResult result = EditorPanelStateResult::NoState;
        u32 version = 0;
        u32 byteCount = 0;
    };

    class EditorPanelRegistryCore final : public EditorPanelRegistry
    {
    public:
        EditorPanelRegistryCore(const EditorUiState& uiState, EditorPanelGenerationSource generations,
                                EditorPanelInvalidationCallback invalidationCallback = nullptr,
                                void* invalidationUserData = nullptr) noexcept;
        ~EditorPanelRegistryCore() override;

        EditorPanelRegistryCore(const EditorPanelRegistryCore&) = delete;
        EditorPanelRegistryCore& operator=(const EditorPanelRegistryCore&) = delete;

        [[nodiscard]] bool RegisterType(const EditorPanelTypeDesc& desc, EditorPanelTypeToken& token,
                                        EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool UnregisterType(EditorPanelTypeToken token, EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool CreatePanel(const EditorPanelDesc& desc, EditorPanelHandle& panel,
                                       EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool CreatePlaceholder(const EditorPanelPlaceholderDesc& desc, EditorPanelHandle& panel,
                                             EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool DestroyPanel(EditorPanelHandle panel, EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool SetOpen(EditorPanelHandle panel, bool open, EditorUiFailure* failure = nullptr) noexcept override;
        [[nodiscard]] bool IsOpen(EditorPanelHandle panel) const noexcept override;
        void VisitPanels(EditorPanelVisitor visitor, void* userData = nullptr) const noexcept override;

        void VisitOpenForAuthoring(EditorPanelAuthoringVisitor visitor, void* userData = nullptr) noexcept;
        [[nodiscard]] bool ResolveFocusReceiver(EditorPanelHandle panel, void*& receiverData) const noexcept;
        [[nodiscard]] bool CaptureUserState(EditorPanelUserStateRecord* records, u32 capacity, u32& count,
                                            EditorUiFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool RestoreUserState(containers::ArraySpan<const EditorPanelUserStateRecord> records,
                                            EditorPanelUserStateRestoreStatus& status,
                                            EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReconcilePresentationUserState(
            containers::ArraySpan<const EditorPanelUserStateRecord> records,
            EditorPanelUserStateRestoreStatus& status, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] EditorPanelPersistentStateCapture CapturePersistentState(
            EditorPanelTypeId type, EditorPanelInstanceId instance, EditorPanelStateScope scope,
            void* destination, u32 capacity, EditorUiFailure* failure = nullptr) const noexcept;
        [[nodiscard]] EditorPanelStateResult RestorePersistentState(
            EditorPanelTypeId type, EditorPanelInstanceId instance, EditorPanelStateScope scope,
            u32 version, containers::ArraySpan<const u8> bytes,
            void* migrationDestination, u32 migrationCapacity,
            EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RefreshPersistentStateRevisions(EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] u64 GetUserStateRevision() const noexcept { return m_userStateRevision; }
        void CommitPendingVisibility() noexcept;
        void DiscardPendingVisibility() noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] bool RegisterTypeForOwner(const EditorExtensionOwner* owner, const EditorPanelTypeDesc& desc,
                                                EditorPanelTypeToken& token, EditorUiFailure* failure) noexcept override;
        [[nodiscard]] bool UnregisterTypeForOwner(const EditorExtensionOwner* owner, EditorPanelTypeToken token,
                                                  EditorUiFailure* failure) noexcept override;
        [[nodiscard]] bool CanRevokeOwner(const EditorExtensionOwner& owner) const noexcept override;
        void RevokeOwner(const EditorExtensionOwner& owner) noexcept override;

        struct PanelTypeRecord final
        {
            EditorPanelTypeToken token;
            EditorPanelTypeDesc desc;
            EditorExtensionOwner owner;
            containers::String name;
            bool active = false;
            bool reachable = false;
        };

        struct PanelRecord final
        {
            EditorPanelHandle handle;
            EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
            EditorPanelTypeId type = InvalidEditorPanelTypeId;
            EditorExtensionOwner owner;
            containers::String title;
            void* state = nullptr;
            bool open = false;
            bool active = false;
            bool reachable = false;
            bool placeholder = false;
            bool pendingVisibility = false;
            bool pendingOpen = false;
            u64 persistentStateRevision = 0;
            bool persistentStateRevisionFaulted = false;
        };

        [[nodiscard]] bool CanMutateTopology(EditorUiFailure* failure) const noexcept;
        [[nodiscard]] bool CanMutateVisibility(EditorUiFailure* failure) const noexcept;
        [[nodiscard]] u32 ReserveTypeGeneration() noexcept;
        [[nodiscard]] u32 ReserveInstanceGeneration() noexcept;

        [[nodiscard]] PanelTypeRecord* FindType(EditorPanelTypeId id) noexcept;
        [[nodiscard]] const PanelTypeRecord* FindType(EditorPanelTypeId id) const noexcept;
        [[nodiscard]] PanelTypeRecord* FindPhysicalType(EditorPanelTypeId id) noexcept;
        [[nodiscard]] const PanelTypeRecord* FindPhysicalType(EditorPanelTypeId id) const noexcept;
        [[nodiscard]] PanelTypeRecord* FindType(EditorPanelTypeToken token) noexcept;
        [[nodiscard]] PanelRecord* FindPanel(EditorPanelHandle panel) noexcept;
        [[nodiscard]] const PanelRecord* FindPanel(EditorPanelHandle panel) const noexcept;
        [[nodiscard]] PanelRecord* FindPhysicalPanel(EditorPanelHandle panel) noexcept;
        [[nodiscard]] const PanelRecord* FindPhysicalPanel(EditorPanelHandle panel) const noexcept;
        [[nodiscard]] PanelRecord* FindPanelById(EditorPanelInstanceId id) noexcept;
        [[nodiscard]] const PanelRecord* FindPanelById(EditorPanelInstanceId id) const noexcept;
        [[nodiscard]] PanelRecord* FindPhysicalPanelById(EditorPanelInstanceId id) noexcept;
        [[nodiscard]] const PanelRecord* FindPhysicalPanelById(EditorPanelInstanceId id) const noexcept;
        [[nodiscard]] bool HasPhysicalPlaceholderForType(EditorPanelTypeId id) const noexcept;

        [[nodiscard]] u32 FindTypeSlot() const noexcept;
        [[nodiscard]] u32 FindPanelSlot() const noexcept;
        void AdvanceUserStateRevision() noexcept;
        void DestroyPanelState(PanelRecord& panel) noexcept;

        const EditorUiState* m_uiState = nullptr;
        EditorPanelGenerationSource m_generations;
        EditorPanelInvalidationCallback m_invalidationCallback = nullptr;
        void* m_invalidationUserData = nullptr;
        containers::DynamicArray<PanelTypeRecord> m_panelTypes;
        containers::DynamicArray<PanelRecord> m_panels;
        u64 m_userStateRevision = 1;
        mutable u32 m_persistenceCallbackDepth = 0;
    };

} // namespace vanguard::editor::detail
