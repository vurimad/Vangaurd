#pragma once

#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_service_ids.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/input/input.hpp>

namespace vanguard::editor
{
    class EditorExtensionRegistrationAccess;

    inline constexpr u32 MaximumEditorCommands = 512;
    inline constexpr u32 MaximumEditorCommandContextTypes = 256;
    inline constexpr u32 MaximumEditorCommandHandlers = 1024;
    inline constexpr u32 MaximumActiveEditorCommandContexts = 64;
    inline constexpr u32 MaximumEditorCommandNameBytes = 128;
    inline constexpr engine::FrameParticipantId EditorCommandShortcutFrameParticipantId = 0x6564636d64736801ull;

    struct EditorCommandId final
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandId, EditorCommandId) noexcept = default;
    };

    struct EditorCommandContextTypeId final
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandContextTypeId, EditorCommandContextTypeId) noexcept = default;
    };

    enum class EditorShortcutModifiers : u8
    {
        None = 0,
        Control = 1u << 0u,
        Shift = 1u << 1u,
        Alt = 1u << 2u,
        Gui = 1u << 3u
    };

    [[nodiscard]] constexpr EditorShortcutModifiers operator|(const EditorShortcutModifiers left,
                                                               const EditorShortcutModifiers right) noexcept
    {
        return static_cast<EditorShortcutModifiers>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr EditorShortcutModifiers operator&(const EditorShortcutModifiers left,
                                                               const EditorShortcutModifiers right) noexcept
    {
        return static_cast<EditorShortcutModifiers>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasEditorShortcutModifier(const EditorShortcutModifiers value,
                                                           const EditorShortcutModifiers modifier) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(modifier)) != 0;
    }

    struct EditorShortcutChord final
    {
        input::Key key = input::Key::Unknown;
        EditorShortcutModifiers modifiers = EditorShortcutModifiers::None;

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return key == input::Key::Unknown && modifiers == EditorShortcutModifiers::None;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorShortcutChord, EditorShortcutChord) noexcept = default;
    };

    enum class EditorShortcutOverrideMode : u8
    {
        Default,
        Unbound,
        Custom
    };

    struct EditorShortcutOverride final
    {
        EditorShortcutOverrideMode mode = EditorShortcutOverrideMode::Default;
        EditorShortcutChord chord;

        [[nodiscard]] friend constexpr bool operator==(EditorShortcutOverride, EditorShortcutOverride) noexcept = default;
    };

    inline constexpr u32 MaximumEditorShortcutPreferences = 2048;

    struct EditorShortcutPreferenceRecord final
    {
        EditorCommandContextTypeId contextType;
        EditorCommandId command;
        EditorShortcutOverride shortcutOverride;
    };

    enum class EditorShortcutPreferenceItemResult : u8
    {
        Applied,
        MissingCommand,
        Invalid
    };

    enum class EditorShortcutPreferenceBatchResult : u8
    {
        Applied,
        Conflict,
        Invalid
    };

    struct EditorShortcutBinding final
    {
        EditorShortcutOverrideMode mode = EditorShortcutOverrideMode::Default;
        EditorShortcutChord chord;
        bool bound = false;
        bool allowRepeat = false;
    };

    struct EditorShortcutModifierState final
    {
        void Reset() noexcept
        {
            physicalDown = 0;
            modifiers = EditorShortcutModifiers::None;
        }

        void Apply(const input::RawEvent& event) noexcept
        {
            if (event.type == input::EventType::FocusLost)
            {
                Reset();
                return;
            }
            if (event.type != input::EventType::KeyChanged)
                return;

            u8 physicalBit = 0;
            switch (event.data.key.key)
            {
            case input::Key::LeftControl:
                physicalBit = 1u << 0u;
                break;
            case input::Key::LeftShift:
                physicalBit = 1u << 1u;
                break;
            case input::Key::LeftAlt:
                physicalBit = 1u << 2u;
                break;
            case input::Key::LeftGui:
                physicalBit = 1u << 3u;
                break;
            case input::Key::RightControl:
                physicalBit = 1u << 4u;
                break;
            case input::Key::RightShift:
                physicalBit = 1u << 5u;
                break;
            case input::Key::RightAlt:
                physicalBit = 1u << 6u;
                break;
            case input::Key::RightGui:
                physicalBit = 1u << 7u;
                break;
            default:
                return;
            }

            if (event.data.key.pressed)
                physicalDown = static_cast<u8>(physicalDown | physicalBit);
            else
                physicalDown = static_cast<u8>(physicalDown & static_cast<u8>(~physicalBit));

            u8 logical = 0;
            if ((physicalDown & ((1u << 0u) | (1u << 4u))) != 0)
                logical |= static_cast<u8>(EditorShortcutModifiers::Control);
            if ((physicalDown & ((1u << 1u) | (1u << 5u))) != 0)
                logical |= static_cast<u8>(EditorShortcutModifiers::Shift);
            if ((physicalDown & ((1u << 2u) | (1u << 6u))) != 0)
                logical |= static_cast<u8>(EditorShortcutModifiers::Alt);
            if ((physicalDown & ((1u << 3u) | (1u << 7u))) != 0)
                logical |= static_cast<u8>(EditorShortcutModifiers::Gui);
            modifiers = static_cast<EditorShortcutModifiers>(logical);
        }

        [[nodiscard]] EditorShortcutModifiers GetModifiers() const noexcept { return modifiers; }

    private:
        u8 physicalDown = 0;
        EditorShortcutModifiers modifiers = EditorShortcutModifiers::None;
    };

    inline constexpr EditorCommandId InvalidEditorCommandId{};
    inline constexpr EditorCommandContextTypeId InvalidEditorCommandContextTypeId{};
    inline constexpr EditorCommandContextTypeId EditorWorkspaceCommandContextTypeId{0x6564636d64777301ull};

    enum class EditorCommandFlags : u8
    {
        None = 0,
        AllowRepeat = 1u << 0u
    };

    [[nodiscard]] constexpr EditorCommandFlags operator|(const EditorCommandFlags left, const EditorCommandFlags right) noexcept
    {
        return static_cast<EditorCommandFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasEditorCommandFlag(const EditorCommandFlags value, const EditorCommandFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    struct EditorCommandToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorCommands && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandToken, EditorCommandToken) noexcept = default;
    };

    struct EditorCommandHandlerToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorCommandHandlers && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandHandlerToken, EditorCommandHandlerToken) noexcept = default;
    };

    struct EditorCommandContextTypeToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorCommandContextTypes && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandContextTypeToken, EditorCommandContextTypeToken) noexcept = default;
    };

    struct EditorCommandContextToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumActiveEditorCommandContexts && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorCommandContextToken, EditorCommandContextToken) noexcept = default;
    };

    enum class EditorCommandContextTier : u8
    {
        ControlTool,
        Local,
        Workspace
    };

    enum class EditorCommandContextMode : u8
    {
        Inherit,
        Override
    };

    struct EditorCommandContextTypeDescriptor final
    {
        EditorCommandContextTypeId id;
        const char* name = nullptr;
        EditorCommandContextTier tier = EditorCommandContextTier::Local;
    };

    enum class EditorCommandStateKind : u8
    {
        Unsupported,
        Disabled,
        Enabled
    };

    struct EditorCommandState final
    {
        EditorCommandStateKind kind = EditorCommandStateKind::Unsupported;
        bool checked = false;

        [[nodiscard]] static constexpr EditorCommandState Unsupported() noexcept { return {}; }
        [[nodiscard]] static constexpr EditorCommandState Disabled() noexcept { return {EditorCommandStateKind::Disabled, false}; }
        [[nodiscard]] static constexpr EditorCommandState Enabled(const bool checked = false) noexcept
        {
            return {EditorCommandStateKind::Enabled, checked};
        }
    };

    struct EditorCommandPresentation final
    {
        EditorCommandState state;
        EditorShortcutBinding binding;
    };

    struct EditorCommandDescriptor final
    {
        EditorCommandId id;
        const char* name = nullptr;
        EditorCommandFlags flags = EditorCommandFlags::None;
    };

    using EditorCommandQueryCallback = EditorCommandState (*)(void* receiverData, void* handlerData) noexcept;
    using EditorCommandInvokeCallback = bool (*)(void* receiverData, void* handlerData) noexcept;

    struct EditorCommandHandlerDescriptor final
    {
        EditorCommandContextTypeId contextType = EditorWorkspaceCommandContextTypeId;
        EditorCommandId command;
        EditorCommandQueryCallback query = nullptr;
        EditorCommandInvokeCallback invoke = nullptr;
        void* handlerData = nullptr;
        EditorShortcutChord defaultShortcut;
    };

    enum class EditorCommandFailureCode : u8
    {
        None,
        NotReady,
        InvalidDescriptor,
        InvalidIdentity,
        InvalidFlags,
        DuplicateCommand,
        DuplicateContext,
        DuplicateHandler,
        MissingCommand,
        MissingContext,
        CapacityExceeded,
        InvalidToken,
        InUse,
        ShortcutConflict,
        WrongThread,
        WrongPhase,
        OwnerRevoked,
        CommandUnsupported,
        CommandDisabled,
        HandlerQueryFailure,
        HandlerInvocationFailure,
        RecursiveDispatch,
        CleanupFailure,
        ContextOrderViolation,
        InvalidShortcut
    };

    struct EditorCommandFailure final
    {
        EditorCommandFailureCode code = EditorCommandFailureCode::None;
        EditorCommandId command;
        EditorCommandContextTypeId contextType;
        EditorCommandToken commandToken;
        EditorCommandHandlerToken handlerToken;
        const char* message = nullptr;
    };

    enum class EditorCommandRouteResult : u8
    {
        None,
        Unsupported,
        Disabled,
        Enabled,
        Invoked,
        Failed
    };

    enum class EditorCommandServiceState : u8
    {
        Offline,
        Ready,
        Running,
        Quiesced,
        Failed
    };

    struct EditorCommandServiceStatus final
    {
        EditorCommandServiceState state = EditorCommandServiceState::Offline;
        u32 registeredCommandCount = 0;
        u32 registeredContextCount = 0;
        u32 registeredHandlerCount = 0;
        u32 activeContextCount = 0;
        u64 currentFrame = 0;
        EditorCommandRouteResult lastRoute = EditorCommandRouteResult::None;
        EditorCommandId lastCommand;
        EditorCommandContextTypeId lastContext;
        EditorCommandFailure failure;
    };

    class EditorCommandService : public application::Service
    {
    public:
        ~EditorCommandService() override = default;

        [[nodiscard]] virtual bool PushContext(EditorCommandContextTypeId contextType, void* receiverData, EditorCommandContextMode mode,
                                               EditorCommandContextToken& token, EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool PopContext(EditorCommandContextToken token, EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool Query(EditorCommandId command, EditorCommandState& state, EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool Invoke(EditorCommandId command, EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool SetShortcutOverride(EditorCommandContextTypeId contextType, EditorCommandId command,
                                                       const EditorShortcutOverride& shortcutOverride,
                                                       EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool GetShortcutBinding(EditorCommandContextTypeId contextType, EditorCommandId command,
                                                      EditorShortcutBinding& binding,
                                                      EditorCommandFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool IsInputEventClaimed(u64 frame, u32 eventIndex) const noexcept = 0;
        [[nodiscard]] virtual EditorCommandServiceStatus GetStatus() const noexcept = 0;

    protected:
        EditorCommandService() noexcept = default;

    private:
        [[nodiscard]] virtual bool RegisterCommandForOwner(const EditorExtensionOwner& owner, const EditorCommandDescriptor& descriptor,
                                                          EditorCommandToken& token, EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterCommandForOwner(const EditorExtensionOwner& owner, EditorCommandToken token,
                                                            EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterContextTypeForOwner(const EditorExtensionOwner& owner,
                                                              const EditorCommandContextTypeDescriptor& descriptor,
                                                              EditorCommandContextTypeToken& token,
                                                              EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterContextTypeForOwner(const EditorExtensionOwner& owner, EditorCommandContextTypeToken token,
                                                                EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterHandlerForOwner(const EditorExtensionOwner& owner, const EditorCommandHandlerDescriptor& descriptor,
                                                          EditorCommandHandlerToken& token, EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterHandlerForOwner(const EditorExtensionOwner& owner, EditorCommandHandlerToken token,
                                                            EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool ValidateLocalContextForOwner(const EditorExtensionOwner& owner,
                                                               EditorCommandContextTypeId contextType,
                                                               EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool QueryPresentation(EditorCommandId command, EditorCommandPresentation& presentation,
                                                     EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool ValidateCommandForOwner(const EditorExtensionOwner& owner, EditorCommandId command,
                                                          EditorCommandFailure* failure) noexcept = 0;
        virtual void RevokeOwner(const EditorExtensionOwner& owner) noexcept = 0;

        friend class EditorExtensionRegistrationAccess;
    };

    namespace detail
    {
        inline constexpr EditorCommandId SaveCurrentLayoutAsCommandId{0x65646c61796f7501ull};
        inline constexpr EditorCommandId SaveLayoutCommandId{0x65646c61796f7502ull};
        inline constexpr EditorCommandId SwitchLayoutCommandId{0x65646c61796f7503ull};
        inline constexpr EditorCommandId RevertLayoutCommandId{0x65646c61796f7504ull};
        inline constexpr EditorCommandId RenameLayoutCommandId{0x65646c61796f7505ull};
        inline constexpr EditorCommandId DuplicateLayoutCommandId{0x65646c61796f7506ull};
        inline constexpr EditorCommandId DeleteLayoutCommandId{0x65646c61796f7507ull};
        inline constexpr EditorCommandId ResetDefaultLayoutCommandId{0x65646c61796f7509ull};

        class EditorCommandPersistencePrivateService : public EditorCommandService
        {
        public:
            [[nodiscard]] virtual bool CaptureShortcutPreferences(
                EditorShortcutPreferenceRecord* records, u32 capacity, u32& count,
                EditorCommandFailure* failure = nullptr) const noexcept = 0;
            [[nodiscard]] virtual EditorShortcutPreferenceBatchResult ApplyShortcutPreferenceBatch(
                EditorCommandContextTypeId contextType,
                containers::ArraySpan<const EditorShortcutPreferenceRecord> records,
                containers::ArraySpan<EditorShortcutPreferenceItemResult> results,
                EditorCommandFailure* failure = nullptr) noexcept = 0;
            [[nodiscard]] virtual u64 GetShortcutPreferenceRevision() const noexcept = 0;
            [[nodiscard]] virtual bool RegisterCoreWorkspaceCommand(
                const EditorCommandDescriptor& descriptor, EditorCommandQueryCallback query,
                EditorCommandInvokeCallback invoke, void* handlerData, EditorCommandToken& commandToken,
                EditorCommandHandlerToken& handlerToken, EditorCommandFailure* failure = nullptr) noexcept = 0;
            [[nodiscard]] virtual bool UnregisterCoreWorkspaceCommand(
                EditorCommandToken commandToken, EditorCommandHandlerToken handlerToken,
                EditorCommandFailure* failure = nullptr) noexcept = 0;

        protected:
            EditorCommandPersistencePrivateService() noexcept = default;
        };

        [[nodiscard]] inline EditorCommandPersistencePrivateService* FindEditorCommandPersistencePrivateService(
            application::ServiceContext& context) noexcept
        {
            return static_cast<EditorCommandPersistencePrivateService*>(
                context.FindCapability(EditorCommandCapabilityId));
        }
    } // namespace detail

    [[nodiscard]] bool RegisterEditorCommandService(application::EngineHost& host, application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorCommandService* FindEditorCommandService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorCommandService* FindEditorCommandService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
