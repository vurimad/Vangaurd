#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <atomic>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;

    std::atomic<vanguard::u32> g_commandTokenGeneration{0};
    std::atomic<vanguard::u32> g_handlerTokenGeneration{0};
    std::atomic<vanguard::u32> g_contextTypeTokenGeneration{0};
    std::atomic<vanguard::u32> g_contextTokenGeneration{0};
    constexpr editor::EditorExtensionOwner CoreUserStateCommandOwner{
        {0x65647573746c7901ull}, 1u};

    [[nodiscard]] vanguard::u32 NextTokenGeneration(std::atomic<vanguard::u32>& counter) noexcept
    {
        vanguard::u32 current = counter.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u32{0})
                return 0;
            if (counter.compare_exchange_weak(current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }

    [[nodiscard]] bool ValidName(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return false;
        vanguard::u32 length = 0;
        while (name[length] != '\0')
            if (++length >= editor::MaximumEditorCommandNameBytes)
                return false;
        return true;
    }

    [[nodiscard]] bool ValidFlags(const editor::EditorCommandFlags flags) noexcept
    {
        constexpr vanguard::u8 known = static_cast<vanguard::u8>(editor::EditorCommandFlags::AllowRepeat);
        return (static_cast<vanguard::u8>(flags) & static_cast<vanguard::u8>(~known)) == 0;
    }

    [[nodiscard]] bool ValidContextTier(const editor::EditorCommandContextTier tier) noexcept
    {
        return static_cast<vanguard::u8>(tier) <= static_cast<vanguard::u8>(editor::EditorCommandContextTier::Workspace);
    }

    [[nodiscard]] bool ValidContextMode(const editor::EditorCommandContextMode mode) noexcept
    {
        return static_cast<vanguard::u8>(mode) <= static_cast<vanguard::u8>(editor::EditorCommandContextMode::Override);
    }

    [[nodiscard]] bool ValidState(const editor::EditorCommandStateKind state) noexcept
    {
        return static_cast<vanguard::u8>(state) <= static_cast<vanguard::u8>(editor::EditorCommandStateKind::Enabled);
    }

    [[nodiscard]] bool ValidShortcutModifiers(const editor::EditorShortcutModifiers modifiers) noexcept
    {
        constexpr vanguard::u8 known =
            static_cast<vanguard::u8>(editor::EditorShortcutModifiers::Control) |
            static_cast<vanguard::u8>(editor::EditorShortcutModifiers::Shift) |
            static_cast<vanguard::u8>(editor::EditorShortcutModifiers::Alt) |
            static_cast<vanguard::u8>(editor::EditorShortcutModifiers::Gui);
        return (static_cast<vanguard::u8>(modifiers) & static_cast<vanguard::u8>(~known)) == 0;
    }

    [[nodiscard]] bool IsShortcutModifierKey(const vanguard::input::Key key) noexcept
    {
        switch (key)
        {
        case vanguard::input::Key::LeftControl:
        case vanguard::input::Key::LeftShift:
        case vanguard::input::Key::LeftAlt:
        case vanguard::input::Key::LeftGui:
        case vanguard::input::Key::RightControl:
        case vanguard::input::Key::RightShift:
        case vanguard::input::Key::RightAlt:
        case vanguard::input::Key::RightGui:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsKnownShortcutKey(const vanguard::input::Key key) noexcept
    {
        const vanguard::u32 value = static_cast<vanguard::u32>(key);
        return (value >= static_cast<vanguard::u32>(vanguard::input::Key::A) &&
                value <= static_cast<vanguard::u32>(vanguard::input::Key::VolumeDown)) ||
               (value >= static_cast<vanguard::u32>(vanguard::input::Key::KeypadComma) &&
                value <= static_cast<vanguard::u32>(vanguard::input::Key::ExSel)) ||
               (value >= static_cast<vanguard::u32>(vanguard::input::Key::Keypad00) &&
                value <= static_cast<vanguard::u32>(vanguard::input::Key::KeypadHexadecimal)) ||
               (value >= static_cast<vanguard::u32>(vanguard::input::Key::LeftControl) &&
                value <= static_cast<vanguard::u32>(vanguard::input::Key::RightGui)) ||
               (value >= static_cast<vanguard::u32>(vanguard::input::Key::Mode) &&
                value <= static_cast<vanguard::u32>(vanguard::input::Key::EndCall));
    }

    [[nodiscard]] bool ValidShortcutChord(const editor::EditorShortcutChord chord) noexcept
    {
        return IsKnownShortcutKey(chord.key) && !IsShortcutModifierKey(chord.key) &&
               ValidShortcutModifiers(chord.modifiers);
    }

    [[nodiscard]] bool ValidShortcutOverride(const editor::EditorShortcutOverride& shortcutOverride) noexcept
    {
        switch (shortcutOverride.mode)
        {
        case editor::EditorShortcutOverrideMode::Default:
        case editor::EditorShortcutOverrideMode::Unbound:
            return shortcutOverride.chord.IsEmpty();
        case editor::EditorShortcutOverrideMode::Custom:
            return ValidShortcutChord(shortcutOverride.chord);
        default:
            return false;
        }
    }

    void CopyName(char (&destination)[editor::MaximumEditorCommandNameBytes], const char* const source) noexcept
    {
        vanguard::u32 index = 0;
        while (source[index] != '\0' && index + 1u < editor::MaximumEditorCommandNameBytes)
        {
            destination[index] = source[index];
            ++index;
        }
        destination[index] = '\0';
    }

    class ManagedEditorCommandService final : public editor::detail::EditorCommandPersistencePrivateService
    {
    public:
        [[nodiscard]] bool RegisterCommandForOwner(const editor::EditorExtensionOwner& owner,
                                                   const editor::EditorCommandDescriptor& descriptor,
                                                   editor::EditorCommandToken& token,
                                                   editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            ClearFailure(failure);
            if (!CanMutate(failure, descriptor.id))
                return false;
            if (!owner.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command owner identity is invalid", descriptor.id);
            if (!descriptor.id.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity, "editor command identity is invalid", descriptor.id);
            if (!ValidName(descriptor.name))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor, "editor command name is invalid", descriptor.id);
            if (!ValidFlags(descriptor.flags))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidFlags, "editor command flags are invalid", descriptor.id);
            if (FindCommand(descriptor.id) != nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::DuplicateCommand, "editor command identity is already registered", descriptor.id);

            vanguard::u32 index = InvalidIndex;
            for (vanguard::u32 candidate = 0; candidate < m_commands.Size(); ++candidate)
            {
                const CommandRecord& record = m_commands[candidate];
                if (!record.active)
                {
                    index = candidate;
                    break;
                }
            }
            const bool appendSlot = index == InvalidIndex;
            if (appendSlot && m_commands.Size() >= editor::MaximumEditorCommands)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded, "editor command capacity is exhausted", descriptor.id);
            if (appendSlot)
                index = m_commands.Size();
            const vanguard::u32 generation = NextTokenGeneration(g_commandTokenGeneration);
            if (generation == 0)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded, "editor command token generation is exhausted", descriptor.id);
            if (appendSlot)
                static_cast<void>(m_commands.EmplaceBack());
            CommandRecord& record = m_commands[index];
            record.generation = generation;
            record.descriptor = descriptor;
            CopyName(record.name, descriptor.name);
            record.descriptor.name = record.name;
            record.owner = owner;
            record.reachable = true;
            record.active = true;
            token = {index, generation};
            ++m_status.registeredCommandCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool UnregisterCommandForOwner(const editor::EditorExtensionOwner& owner,
                                                     const editor::EditorCommandToken token,
                                                     editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            CommandRecord* const command = FindCommandRecord(token);
            if (command == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidToken, "editor command token is stale or invalid", {}, {}, token);
            if (!(command->owner == owner))
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command token does not belong to the requested owner", command->descriptor.id, {}, token);
            for (const HandlerRecord& handler : m_handlers)
                if (handler.active && handler.descriptor.command == command->descriptor.id)
                    return Fail(failure, editor::EditorCommandFailureCode::InUse, "editor command still has a live handler", command->descriptor.id,
                                handler.descriptor.contextType, token, handler.token);

            command->active = false;
            command->reachable = false;
            command->descriptor = {};
            command->owner = {};
            command->name[0] = '\0';
            if (m_status.registeredCommandCount != 0)
                --m_status.registeredCommandCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool RegisterContextTypeForOwner(const editor::EditorExtensionOwner& owner,
                                                       const editor::EditorCommandContextTypeDescriptor& descriptor,
                                                       editor::EditorCommandContextTypeToken& token,
                                                       editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            ClearFailure(failure);
            if (!CanMutate(failure, {}, descriptor.id))
                return false;
            if (!owner.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command context owner identity is invalid", {}, descriptor.id);
            if (!descriptor.id.IsValid() || descriptor.id == editor::EditorWorkspaceCommandContextTypeId)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor command context identity is invalid or reserved", {}, descriptor.id);
            if (!ValidName(descriptor.name))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor command context name is invalid", {}, descriptor.id);
            if (!ValidContextTier(descriptor.tier) || descriptor.tier == editor::EditorCommandContextTier::Workspace)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor command context tier is invalid or reserved", {}, descriptor.id);
            if (FindContextType(descriptor.id) != nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::DuplicateContext,
                            "editor command context identity is already registered", {}, descriptor.id);
            if (m_status.registeredContextCount >= editor::MaximumEditorCommandContextTypes)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                            "editor command context capacity is exhausted", {}, descriptor.id);

            vanguard::u32 index = InvalidIndex;
            for (vanguard::u32 candidate = 0; candidate < m_contextTypes.Size(); ++candidate)
                if (!m_contextTypes[candidate].active)
                {
                    index = candidate;
                    break;
                }
            const bool appendSlot = index == InvalidIndex;
            if (appendSlot)
            {
                if (m_contextTypes.Size() >= editor::MaximumEditorCommandContextTypes - 1u)
                    return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                                "editor command context capacity is exhausted", {}, descriptor.id);
                index = m_contextTypes.Size();
            }

            const vanguard::u32 generation = NextTokenGeneration(g_contextTypeTokenGeneration);
            if (generation == 0)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                            "editor command context token generation is exhausted", {}, descriptor.id);
            if (appendSlot)
                static_cast<void>(m_contextTypes.EmplaceBack());
            ContextTypeRecord& record = m_contextTypes[index];
            record.generation = generation;
            record.token = {index, generation};
            record.descriptor = descriptor;
            CopyName(record.name, descriptor.name);
            record.descriptor.name = record.name;
            record.owner = owner;
            record.reachable = true;
            record.active = true;
            token = record.token;
            ++m_status.registeredContextCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool UnregisterContextTypeForOwner(const editor::EditorExtensionOwner& owner,
                                                         const editor::EditorCommandContextTypeToken token,
                                                         editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            ContextTypeRecord* const context = FindContextTypeRecord(token);
            if (context == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidToken,
                            "editor command context token is stale or invalid");
            if (!(context->owner == owner))
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command context token does not belong to the requested owner", {}, context->descriptor.id);
            for (const ActiveContextRecord& active : m_activeContexts)
                if (active.active && active.contextType == context->descriptor.id)
                    return Fail(failure, editor::EditorCommandFailureCode::InUse,
                                "editor command context type still has a live active context", {}, context->descriptor.id);
            for (const HandlerRecord& handler : m_handlers)
                if (handler.active && handler.descriptor.contextType == context->descriptor.id)
                    return Fail(failure, editor::EditorCommandFailureCode::InUse,
                                "editor command context type still has a live handler", handler.descriptor.command,
                                context->descriptor.id, {}, handler.token);

            context->active = false;
            context->reachable = false;
            context->descriptor = {};
            context->token = {};
            context->owner = {};
            context->name[0] = '\0';
            if (m_status.registeredContextCount > 1u)
                --m_status.registeredContextCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool RegisterHandlerForOwner(const editor::EditorExtensionOwner& owner,
                                                   const editor::EditorCommandHandlerDescriptor& descriptor,
                                                   editor::EditorCommandHandlerToken& token,
                                                   editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            ClearFailure(failure);
            if (!CanMutate(failure, descriptor.command, descriptor.contextType))
                return false;
            if (!owner.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command handler owner identity is invalid", descriptor.command, descriptor.contextType);
            if (!descriptor.command.IsValid() || !descriptor.contextType.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity, "editor command handler identity is invalid", descriptor.command,
                            descriptor.contextType);
            if (!IsRegisteredContextType(descriptor.contextType))
                return Fail(failure, editor::EditorCommandFailureCode::MissingContext, "editor command context type is not registered", descriptor.command,
                            descriptor.contextType);
            if (descriptor.query == nullptr || descriptor.invoke == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor, "editor command handler callbacks are invalid", descriptor.command,
                            descriptor.contextType);
            if (!descriptor.defaultShortcut.IsEmpty() && !ValidShortcutChord(descriptor.defaultShortcut))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidShortcut,
                            "editor command handler default shortcut is invalid", descriptor.command, descriptor.contextType);
            if (FindCommand(descriptor.command) == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingCommand, "editor command handler references an unknown command", descriptor.command,
                            descriptor.contextType);
            if (FindHandler(descriptor.contextType, descriptor.command) != nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::DuplicateHandler, "editor command handler is already registered", descriptor.command,
                            descriptor.contextType);
            if (!descriptor.defaultShortcut.IsEmpty())
            {
                const HandlerRecord* const conflict = FindShortcutConflict(descriptor.contextType, descriptor.defaultShortcut, nullptr);
                if (conflict != nullptr)
                    return Fail(failure, editor::EditorCommandFailureCode::ShortcutConflict,
                                "editor command handler default shortcut conflicts in its context", descriptor.command,
                                descriptor.contextType, {}, conflict->token);
            }

            vanguard::u32 index = InvalidIndex;
            for (vanguard::u32 candidate = 0; candidate < m_handlers.Size(); ++candidate)
            {
                const HandlerRecord& record = m_handlers[candidate];
                if (!record.active)
                {
                    index = candidate;
                    break;
                }
            }
            const bool appendSlot = index == InvalidIndex;
            if (appendSlot && m_handlers.Size() >= editor::MaximumEditorCommandHandlers)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded, "editor command handler capacity is exhausted", descriptor.command,
                            descriptor.contextType);
            if (appendSlot)
                index = m_handlers.Size();
            const vanguard::u32 generation = NextTokenGeneration(g_handlerTokenGeneration);
            if (generation == 0)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded, "editor command handler token generation is exhausted", descriptor.command,
                            descriptor.contextType);
            if (appendSlot)
                static_cast<void>(m_handlers.EmplaceBack());
            HandlerRecord& record = m_handlers[index];
            record.generation = generation;
            record.token = {index, generation};
            record.descriptor = descriptor;
            record.owner = owner;
            record.shortcutOverride = {};
            record.reachable = true;
            record.active = true;
            token = record.token;
            ++m_status.registeredHandlerCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool PushContext(const editor::EditorCommandContextTypeId contextType, void* const receiverData,
                                       const editor::EditorCommandContextMode mode, editor::EditorCommandContextToken& token,
                                       editor::EditorCommandFailure* const failure) noexcept override
        {
            token = {};
            ClearFailure(failure);
            if (!CanMutate(failure, {}, contextType))
                return false;
            if (!contextType.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor command active context identity is invalid", {}, contextType);
            if (contextType == editor::EditorWorkspaceCommandContextTypeId)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor workspace context is always present and cannot be pushed", {}, contextType);
            ContextTypeRecord* const type = FindContextType(contextType);
            if (type == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingContext,
                            "editor command active context type is not registered", {}, contextType);
            if (!ValidContextMode(mode))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor command active context mode is invalid", {}, contextType);
            if (m_status.activeContextCount >= editor::MaximumActiveEditorCommandContexts)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                            "editor command active context capacity is exhausted", {}, contextType);
            if (m_nextActivationOrder == ~vanguard::u64{0})
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                            "editor command context activation order is exhausted", {}, contextType);

            vanguard::u32 index = InvalidIndex;
            for (vanguard::u32 candidate = 0; candidate < m_activeContexts.Size(); ++candidate)
                if (!m_activeContexts[candidate].active)
                {
                    index = candidate;
                    break;
                }
            const bool appendSlot = index == InvalidIndex;
            if (appendSlot)
            {
                if (m_activeContexts.Size() >= editor::MaximumActiveEditorCommandContexts)
                    return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                                "editor command active context capacity is exhausted", {}, contextType);
                index = m_activeContexts.Size();
            }
            const vanguard::u32 generation = NextTokenGeneration(g_contextTokenGeneration);
            if (generation == 0)
                return Fail(failure, editor::EditorCommandFailureCode::CapacityExceeded,
                            "editor command active context token generation is exhausted", {}, contextType);
            if (appendSlot)
                static_cast<void>(m_activeContexts.EmplaceBack());
            ActiveContextRecord& record = m_activeContexts[index];
            record.contextType = contextType;
            record.tier = type->descriptor.tier;
            record.mode = mode;
            record.receiverData = receiverData;
            record.activationOrder = ++m_nextActivationOrder;
            record.generation = generation;
            record.token = {index, generation};
            record.active = true;
            token = record.token;
            m_contextStack.EmplaceBack(token);
            ++m_status.activeContextCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool PopContext(const editor::EditorCommandContextToken token,
                                      editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            ActiveContextRecord* const context = FindActiveContext(token);
            if (context == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidToken,
                            "editor command active context token is stale or invalid");

            if (m_contextStack.Size() == 0 || m_contextStack[m_contextStack.Size() - 1u] != token)
                return Fail(failure, editor::EditorCommandFailureCode::ContextOrderViolation,
                            "editor command active contexts must be popped in exact reverse order", {}, context->contextType);

            static_cast<void>(m_contextStack.PopBack());
            context->active = false;
            context->contextType = {};
            context->receiverData = nullptr;
            context->activationOrder = 0;
            context->token = {};
            if (m_status.activeContextCount != 0)
                --m_status.activeContextCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool UnregisterHandlerForOwner(const editor::EditorExtensionOwner& owner,
                                                     const editor::EditorCommandHandlerToken token,
                                                     editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            HandlerRecord* const handler = FindHandlerRecord(token);
            if (handler == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidToken, "editor command handler token is stale or invalid", {}, {}, {}, token);
            if (!(handler->owner == owner))
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor command handler token does not belong to the requested owner",
                            handler->descriptor.command, handler->descriptor.contextType, {}, token);
            handler->active = false;
            handler->reachable = false;
            handler->descriptor = {};
            handler->token = {};
            handler->owner = {};
            handler->shortcutOverride = {};
            if (m_status.registeredHandlerCount != 0)
                --m_status.registeredHandlerCount;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool ValidateLocalContextForOwner(const editor::EditorExtensionOwner& owner,
                                                       const editor::EditorCommandContextTypeId contextType,
                                                       editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return Fail(failure, editor::EditorCommandFailureCode::WrongThread,
                            "editor panel context association validation requires the main thread", {}, contextType);
            if (m_status.state != editor::EditorCommandServiceState::Running)
                return Fail(failure, editor::EditorCommandFailureCode::WrongPhase,
                            "editor panel context association validation requires a running command service", {}, contextType);
            if (!owner.IsValid() || !contextType.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor panel context association identity is invalid", {}, contextType);
            const ContextTypeRecord* const context = FindContextType(contextType);
            if (context == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingContext,
                            "editor panel context association references an unavailable context", {}, contextType);
            if (context->descriptor.tier != editor::EditorCommandContextTier::Local)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor panel context association must reference a Local context", {}, contextType);
            if (!(context->owner == owner))
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor panel context association must use a context owned by the same extension", {}, contextType);
            return true;
        }

        [[nodiscard]] bool ValidateCommandForOwner(const editor::EditorExtensionOwner& owner,
                                                  const editor::EditorCommandId command,
                                                  editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!vanguard::concurrency::IsMainThread())
                return Fail(failure, editor::EditorCommandFailureCode::WrongThread,
                            "editor surface command validation requires the main thread", command);
            if (m_status.state != editor::EditorCommandServiceState::Running)
                return Fail(failure, editor::EditorCommandFailureCode::WrongPhase,
                            "editor surface command validation requires a running command service", command);
            if (!owner.IsValid() || !command.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor surface command identity is invalid", command);
            const CommandRecord* const record = FindCommand(command);
            if (record == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingCommand,
                            "editor surface contribution references an unavailable command", command);
            if (!(record->owner == owner))
                return Fail(failure, editor::EditorCommandFailureCode::OwnerRevoked,
                            "editor surface contribution must reference a command owned by the same extension", command);
            return true;
        }

        void RevokeOwner(const editor::EditorExtensionOwner& owner) noexcept override
        {
            if (!owner.IsValid())
                return;

            for (CommandRecord& command : m_commands)
                if (command.active && command.owner == owner)
                    command.reachable = false;

            for (ContextTypeRecord& context : m_contextTypes)
                if (context.active && context.owner == owner)
                    context.reachable = false;

            for (HandlerRecord& handler : m_handlers)
            {
                if (!handler.active || !(handler.owner == owner))
                    continue;
                handler.reachable = false;
                handler.descriptor.query = nullptr;
                handler.descriptor.invoke = nullptr;
                handler.descriptor.handlerData = nullptr;
            }

            for (ActiveContextRecord& active : m_activeContexts)
            {
                if (!active.active)
                    continue;
                const ContextTypeRecord* const context = FindContextTypeRecord(active.contextType);
                if (context == nullptr || !(context->owner == owner))
                    continue;
                active.active = false;
                active.contextType = {};
                active.receiverData = nullptr;
                active.activationOrder = 0;
                active.token = {};
                if (m_status.activeContextCount != 0)
                    --m_status.activeContextCount;
            }

            vanguard::u32 write = 0;
            for (vanguard::u32 read = 0; read < m_contextStack.Size(); ++read)
            {
                if (FindActiveContext(m_contextStack[read]) == nullptr)
                    continue;
                if (write != read)
                    m_contextStack[write] = m_contextStack[read];
                ++write;
            }
            while (m_contextStack.Size() > write)
                static_cast<void>(m_contextStack.PopBack());
        }

        [[nodiscard]] bool Query(const editor::EditorCommandId command, editor::EditorCommandState& state,
                                 editor::EditorCommandFailure* const failure) noexcept override
        {
            state = editor::EditorCommandState::Unsupported();
            ClearFailure(failure);
            if (!BeginRoute(command, failure))
                return false;

            ResolvedRoute route;
            const bool resolved = Resolve(command, state, route, failure);
            m_dispatching = false;
            if (!resolved)
                return false;
            RecordRouteSuccess(command, route.contextType, RouteForState(state));
            return true;
        }

        [[nodiscard]] bool QueryPresentation(const editor::EditorCommandId command,
                                             editor::EditorCommandPresentation& presentation,
                                             editor::EditorCommandFailure* const failure) noexcept override
        {
            presentation = {};
            presentation.state = editor::EditorCommandState::Unsupported();
            ClearFailure(failure);
            if (!BeginRoute(command, failure))
                return false;

            ResolvedRoute route;
            if (!Resolve(command, presentation.state, route, failure))
            {
                m_dispatching = false;
                return false;
            }

            if (route.handler != nullptr)
            {
                presentation.binding.mode = route.handler->shortcutOverride.mode;
                ResolveShortcut(*route.handler, presentation.binding.bound, presentation.binding.chord);
                const CommandRecord* const commandRecord = FindCommand(command);
                presentation.binding.allowRepeat =
                    commandRecord != nullptr &&
                    editor::HasEditorCommandFlag(commandRecord->descriptor.flags, editor::EditorCommandFlags::AllowRepeat);
            }
            else
            {
                presentation.binding = {};
            }

            m_dispatching = false;
            RecordRouteSuccess(command, route.contextType, RouteForState(presentation.state));
            return true;
        }

        [[nodiscard]] bool Invoke(const editor::EditorCommandId command, editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!BeginRoute(command, failure))
                return false;

            editor::EditorCommandState state;
            ResolvedRoute route;
            if (!Resolve(command, state, route, failure))
            {
                m_dispatching = false;
                return false;
            }
            if (route.handler == nullptr || state.kind == editor::EditorCommandStateKind::Unsupported)
            {
                m_dispatching = false;
                return RouteFailure(failure, editor::EditorCommandFailureCode::CommandUnsupported, "editor command is unsupported", command,
                                    route.contextType, {}, route.handler != nullptr ? route.handler->token : editor::EditorCommandHandlerToken{},
                                    editor::EditorCommandRouteResult::Unsupported);
            }
            if (state.kind == editor::EditorCommandStateKind::Disabled)
            {
                m_dispatching = false;
                return RouteFailure(failure, editor::EditorCommandFailureCode::CommandDisabled, "editor command is disabled", command,
                                    route.contextType, {}, route.handler->token, editor::EditorCommandRouteResult::Disabled);
            }

            const bool invoked = InvokeExactHandler(*route.handler, route.receiverData, failure);
            m_dispatching = false;
            return invoked;
        }

        [[nodiscard]] bool SetShortcutOverride(const editor::EditorCommandContextTypeId contextType,
                                               const editor::EditorCommandId command,
                                               const editor::EditorShortcutOverride& shortcutOverride,
                                               editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure, command, contextType))
                return false;
            if (!contextType.IsValid() || !command.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor shortcut override identity is invalid", command, contextType);
            if (!IsRegisteredContextType(contextType))
                return Fail(failure, editor::EditorCommandFailureCode::MissingContext,
                            "editor shortcut override context is not registered", command, contextType);
            if (FindCommand(command) == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingCommand,
                            "editor shortcut override command is not registered", command, contextType);
            HandlerRecord* const handler = FindHandler(contextType, command);
            if (handler == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor shortcut override requires a live handler", command, contextType);
            if (!ValidShortcutOverride(shortcutOverride))
                return Fail(failure, editor::EditorCommandFailureCode::InvalidShortcut,
                            "editor shortcut override is invalid", command, contextType, {}, handler->token);

            editor::EditorShortcutChord candidate;
            bool bound = false;
            switch (shortcutOverride.mode)
            {
            case editor::EditorShortcutOverrideMode::Default:
                candidate = handler->descriptor.defaultShortcut;
                bound = !candidate.IsEmpty();
                break;
            case editor::EditorShortcutOverrideMode::Unbound:
                break;
            case editor::EditorShortcutOverrideMode::Custom:
                candidate = shortcutOverride.chord;
                bound = true;
                break;
            default:
                return Fail(failure, editor::EditorCommandFailureCode::InvalidShortcut,
                            "editor shortcut override mode is invalid", command, contextType, {}, handler->token);
            }

            if (bound)
            {
                const HandlerRecord* const conflict = FindShortcutConflict(contextType, candidate, handler);
                if (conflict != nullptr)
                    return Fail(failure, editor::EditorCommandFailureCode::ShortcutConflict,
                                "editor shortcut override conflicts in its context", command, contextType, {}, conflict->token);
            }

            if (handler->shortcutOverride == shortcutOverride)
            {
                m_status.failure = {};
                return true;
            }
            handler->shortcutOverride = shortcutOverride;
            AdvanceShortcutPreferenceRevision();
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool CaptureShortcutPreferences(
            editor::EditorShortcutPreferenceRecord* const records, const vanguard::u32 capacity,
            vanguard::u32& count, editor::EditorCommandFailure* const failure) const noexcept override
        {
            ClearFailure(failure);
            count = 0;
            if (!vanguard::concurrency::IsMainThread())
                return FailConst(failure, editor::EditorCommandFailureCode::WrongThread,
                                 "editor shortcut preference capture requires the main thread");
            if (m_status.state != editor::EditorCommandServiceState::Running)
                return FailConst(failure, editor::EditorCommandFailureCode::WrongPhase,
                                 "editor shortcut preference capture requires a running service");
            if (m_shortcutPreferenceRevision == 0)
                return FailConst(failure, editor::EditorCommandFailureCode::CleanupFailure,
                                 "editor shortcut preference revision space is exhausted");

            for (const HandlerRecord& handler : m_handlers)
                if (handler.active && handler.reachable &&
                    handler.shortcutOverride.mode != editor::EditorShortcutOverrideMode::Default)
                    ++count;
            if (count > capacity || (count != 0 && records == nullptr))
                return FailConst(failure, count > capacity ? editor::EditorCommandFailureCode::CapacityExceeded
                                                           : editor::EditorCommandFailureCode::InvalidDescriptor,
                                 "editor shortcut preference capture destination is insufficient");

            vanguard::u32 write = 0;
            for (const HandlerRecord& handler : m_handlers)
            {
                if (!handler.active || !handler.reachable ||
                    handler.shortcutOverride.mode == editor::EditorShortcutOverrideMode::Default)
                    continue;
                records[write++] = {handler.descriptor.contextType, handler.descriptor.command,
                                    handler.shortcutOverride};
            }
            for (vanguard::u32 index = 1; index < count; ++index)
            {
                const editor::EditorShortcutPreferenceRecord value = records[index];
                vanguard::u32 insertion = index;
                while (insertion != 0)
                {
                    const editor::EditorShortcutPreferenceRecord& previous = records[insertion - 1u];
                    if (previous.contextType.value < value.contextType.value ||
                        (previous.contextType == value.contextType &&
                         previous.command.value <= value.command.value))
                        break;
                    records[insertion] = previous;
                    --insertion;
                }
                records[insertion] = value;
            }
            return true;
        }

        [[nodiscard]] editor::EditorShortcutPreferenceBatchResult ApplyShortcutPreferenceBatch(
            const editor::EditorCommandContextTypeId contextType,
            const vanguard::containers::ArraySpan<const editor::EditorShortcutPreferenceRecord> records,
            const vanguard::containers::ArraySpan<editor::EditorShortcutPreferenceItemResult> results,
            editor::EditorCommandFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure, {}, contextType) || !contextType.IsValid() ||
                records.Size() > editor::MaximumEditorShortcutPreferences ||
                records.Size() != results.Size() ||
                (!records.Empty() && (records.Data() == nullptr || results.Data() == nullptr)))
            {
                if (failure != nullptr && failure->code == editor::EditorCommandFailureCode::None)
                    static_cast<void>(Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                                           "editor shortcut preference batch is invalid", {}, contextType));
                return editor::EditorShortcutPreferenceBatchResult::Invalid;
            }

            for (vanguard::u32 index = 0; index < results.Size(); ++index)
                results[index] = editor::EditorShortcutPreferenceItemResult::Invalid;

            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorShortcutPreferenceRecord& record = records[index];
                if (!(record.contextType == contextType) || !record.command.IsValid() ||
                    record.shortcutOverride.mode == editor::EditorShortcutOverrideMode::Default ||
                    !ValidShortcutOverride(record.shortcutOverride))
                {
                    static_cast<void>(Fail(failure, editor::EditorCommandFailureCode::InvalidShortcut,
                                           "editor shortcut preference batch contains an invalid record",
                                           record.command, contextType));
                    return editor::EditorShortcutPreferenceBatchResult::Invalid;
                }
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (records[previous].command == record.command)
                    {
                        static_cast<void>(Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                                               "editor shortcut preference batch contains a duplicate command",
                                               record.command, contextType));
                        return editor::EditorShortcutPreferenceBatchResult::Invalid;
                    }
            }

            if (!IsRegisteredContextType(contextType))
            {
                for (vanguard::u32 index = 0; index < results.Size(); ++index)
                    results[index] = editor::EditorShortcutPreferenceItemResult::MissingCommand;
                return editor::EditorShortcutPreferenceBatchResult::Applied;
            }

            HandlerRecord* liveHandlers[editor::MaximumEditorShortcutPreferences]{};
            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                const editor::EditorShortcutPreferenceRecord& record = records[index];
                if (FindCommand(record.command) == nullptr)
                {
                    results[index] = editor::EditorShortcutPreferenceItemResult::MissingCommand;
                    continue;
                }
                HandlerRecord* const handler = FindHandler(contextType, record.command);
                if (handler == nullptr)
                {
                    results[index] = editor::EditorShortcutPreferenceItemResult::MissingCommand;
                    continue;
                }
                liveHandlers[index] = handler;
            }

            const auto overrideFor = [&](const HandlerRecord& handler) noexcept {
                for (vanguard::u32 index = 0; index < records.Size(); ++index)
                    if (liveHandlers[index] == &handler)
                        return records[index].shortcutOverride;
                return handler.shortcutOverride;
            };
            for (vanguard::u32 leftIndex = 0; leftIndex < m_handlers.Size(); ++leftIndex)
            {
                const HandlerRecord& left = m_handlers[leftIndex];
                if (!left.active || !left.reachable || !(left.descriptor.contextType == contextType))
                    continue;
                const editor::EditorShortcutOverride leftOverride = overrideFor(left);
                bool leftBound = false;
                editor::EditorShortcutChord leftChord;
                ResolveShortcut(left.descriptor.defaultShortcut, leftOverride, leftBound, leftChord);
                if (!leftBound)
                    continue;
                for (vanguard::u32 rightIndex = leftIndex + 1u; rightIndex < m_handlers.Size(); ++rightIndex)
                {
                    const HandlerRecord& right = m_handlers[rightIndex];
                    if (!right.active || !right.reachable || !(right.descriptor.contextType == contextType))
                        continue;
                    const editor::EditorShortcutOverride rightOverride = overrideFor(right);
                    bool rightBound = false;
                    editor::EditorShortcutChord rightChord;
                    ResolveShortcut(right.descriptor.defaultShortcut, rightOverride, rightBound, rightChord);
                    if (rightBound && rightChord == leftChord)
                    {
                        static_cast<void>(Fail(failure, editor::EditorCommandFailureCode::ShortcutConflict,
                                               "editor shortcut preference batch conflicts in its context",
                                               right.descriptor.command, contextType, {}, right.token));
                        return editor::EditorShortcutPreferenceBatchResult::Conflict;
                    }
                }
            }

            bool changed = false;
            for (vanguard::u32 index = 0; index < records.Size(); ++index)
            {
                HandlerRecord* const handler = liveHandlers[index];
                if (handler == nullptr)
                    continue;
                changed = changed || !(handler->shortcutOverride == records[index].shortcutOverride);
                handler->shortcutOverride = records[index].shortcutOverride;
                results[index] = editor::EditorShortcutPreferenceItemResult::Applied;
            }
            if (changed)
                AdvanceShortcutPreferenceRevision();
            m_status.failure = {};
            return editor::EditorShortcutPreferenceBatchResult::Applied;
        }

        [[nodiscard]] vanguard::u64 GetShortcutPreferenceRevision() const noexcept override
        {
            return m_shortcutPreferenceRevision;
        }

        [[nodiscard]] bool RegisterCoreWorkspaceCommand(
            const editor::EditorCommandDescriptor& descriptor, const editor::EditorCommandQueryCallback query,
            const editor::EditorCommandInvokeCallback invoke, void* const handlerData,
            editor::EditorCommandToken& commandToken, editor::EditorCommandHandlerToken& handlerToken,
            editor::EditorCommandFailure* const failure) noexcept override
        {
            commandToken = {};
            handlerToken = {};
            if (query == nullptr || invoke == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor core workspace command callbacks are invalid", descriptor.id);
            if (!RegisterCommandForOwner(CoreUserStateCommandOwner, descriptor, commandToken, failure))
                return false;
            const editor::EditorCommandHandlerDescriptor handler{
                editor::EditorWorkspaceCommandContextTypeId, descriptor.id, query, invoke, handlerData, {}};
            if (RegisterHandlerForOwner(CoreUserStateCommandOwner, handler, handlerToken, failure))
                return true;
            editor::EditorCommandFailure cleanupFailure;
            static_cast<void>(UnregisterCommandForOwner(CoreUserStateCommandOwner, commandToken, &cleanupFailure));
            commandToken = {};
            return false;
        }

        [[nodiscard]] bool UnregisterCoreWorkspaceCommand(
            const editor::EditorCommandToken commandToken, const editor::EditorCommandHandlerToken handlerToken,
            editor::EditorCommandFailure* const failure) noexcept override
        {
            if (!handlerToken.IsValid() || !commandToken.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidToken,
                            "editor core workspace command tokens are invalid");
            if (!UnregisterHandlerForOwner(CoreUserStateCommandOwner, handlerToken, failure))
                return false;
            if (UnregisterCommandForOwner(CoreUserStateCommandOwner, commandToken, failure))
                return true;
            return false;
        }

        [[nodiscard]] bool GetShortcutBinding(const editor::EditorCommandContextTypeId contextType,
                                              const editor::EditorCommandId command,
                                              editor::EditorShortcutBinding& binding,
                                              editor::EditorCommandFailure* const failure) noexcept override
        {
            binding = {};
            ClearFailure(failure);
            if (!CanMutate(failure, command, contextType))
                return false;
            if (!contextType.IsValid() || !command.IsValid())
                return Fail(failure, editor::EditorCommandFailureCode::InvalidIdentity,
                            "editor shortcut binding identity is invalid", command, contextType);
            if (!IsRegisteredContextType(contextType))
                return Fail(failure, editor::EditorCommandFailureCode::MissingContext,
                            "editor shortcut binding context is not registered", command, contextType);
            const CommandRecord* const commandRecord = FindCommand(command);
            if (commandRecord == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::MissingCommand,
                            "editor shortcut binding command is not registered", command, contextType);
            const HandlerRecord* const handler = FindHandler(contextType, command);
            if (handler == nullptr)
                return Fail(failure, editor::EditorCommandFailureCode::InvalidDescriptor,
                            "editor shortcut binding requires a live handler", command, contextType);

            binding.mode = handler->shortcutOverride.mode;
            ResolveShortcut(*handler, binding.bound, binding.chord);
            binding.allowRepeat =
                editor::HasEditorCommandFlag(commandRecord->descriptor.flags, editor::EditorCommandFlags::AllowRepeat);
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool IsInputEventClaimed(const vanguard::u64 frame, const vanguard::u32 eventIndex) const noexcept override
        {
            if (frame != m_claimFrame || eventIndex >= m_claimEventCount)
                return false;
            const vanguard::u64 mask = vanguard::u64{1} << (eventIndex & 63u);
            return (m_claimedInputEvents[eventIndex >> 6u] & mask) != 0;
        }

        [[nodiscard]] editor::EditorCommandServiceStatus GetStatus() const noexcept override
        {
            return m_status;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            Reset();
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command initialization requires the main thread");
            m_framePipeline = static_cast<vanguard::engine::FramePipelineService*>(context.FindCapability(vanguard::engine::FramePipelineCapabilityId));
            m_input = static_cast<vanguard::engine::InputService*>(context.FindCapability(vanguard::engine::InputCapabilityId));
            m_ui = static_cast<editor::EditorUiService*>(context.FindCapability(editor::EditorUiCapabilityId));
            if (m_framePipeline == nullptr || m_input == nullptr || m_ui == nullptr)
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongPhase, "editor command dependencies are unavailable during initialization");
            constexpr vanguard::engine::FrameParticipantId dependencies[]{editor::EditorUiFrameParticipantId};
            vanguard::engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorCommandShortcutFrameParticipantId;
            descriptor.name = "editorCommands.shortcuts";
            descriptor.phase = vanguard::engine::FramePhase::BeginFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteShortcutFrame;
            descriptor.userData = this;
            vanguard::engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongPhase,
                                        frameFailure.message != nullptr ? frameFailure.message : "editor command shortcut frame registration failed");
            m_status.state = editor::EditorCommandServiceState::Ready;
            m_status.registeredContextCount = 1;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command start requires the main thread");
            if (m_status.state != editor::EditorCommandServiceState::Ready)
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongPhase, "editor command start requires initialized state");
            m_status.state = editor::EditorCommandServiceState::Running;
            m_status.failure = {};
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command quiesce requires the main thread");
            if (m_dispatching)
                return LifecycleFailure(editor::EditorCommandFailureCode::CleanupFailure, "editor command dispatch is active during quiesce");
            const bool leakedOwnedRegistrations = NeutralizeUnexpectedOwnedRegistrations();
            ClearActiveContexts();
            m_shortcutModifiers.Reset();
            ClearInputClaims();
            if (leakedOwnedRegistrations)
                return LifecycleFailure(editor::EditorCommandFailureCode::CleanupFailure,
                                        "editor command quiesce found live extension-owned registrations");
            m_status.state = editor::EditorCommandServiceState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command drain requires the main thread");
            return !m_dispatching && m_status.activeContextCount == 0 && !HasExtensionOwnedRegistrations()
                       ? app::LifecycleStatus::Success()
                       : LifecycleFailure(editor::EditorCommandFailureCode::CleanupFailure,
                                          "editor command transient or extension-owned state survived quiesce");
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command stop requires the main thread");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorCommandFailureCode::WrongThread, "editor command shutdown requires the main thread");
            Reset();
            return app::LifecycleStatus::Success();
        }

    private:
        static constexpr vanguard::u32 InvalidIndex = ~vanguard::u32{0};
        static constexpr vanguard::u32 InputClaimWordCount = (vanguard::input::MaximumInputEventsPerFrame + 63u) / 64u;

        struct CommandRecord final
        {
            editor::EditorCommandDescriptor descriptor;
            char name[editor::MaximumEditorCommandNameBytes]{};
            editor::EditorExtensionOwner owner;
            vanguard::u32 generation = 0;
            bool reachable = false;
            bool active = false;
        };

        struct HandlerRecord final
        {
            editor::EditorCommandHandlerDescriptor descriptor;
            editor::EditorCommandHandlerToken token;
            editor::EditorExtensionOwner owner;
            editor::EditorShortcutOverride shortcutOverride;
            vanguard::u32 generation = 0;
            bool reachable = false;
            bool active = false;
        };

        struct ContextTypeRecord final
        {
            editor::EditorCommandContextTypeDescriptor descriptor;
            char name[editor::MaximumEditorCommandNameBytes]{};
            editor::EditorCommandContextTypeToken token;
            editor::EditorExtensionOwner owner;
            vanguard::u32 generation = 0;
            bool reachable = false;
            bool active = false;
        };

        struct ActiveContextRecord final
        {
            editor::EditorCommandContextTypeId contextType;
            editor::EditorCommandContextTier tier = editor::EditorCommandContextTier::Local;
            editor::EditorCommandContextMode mode = editor::EditorCommandContextMode::Inherit;
            void* receiverData = nullptr;
            vanguard::u64 activationOrder = 0;
            editor::EditorCommandContextToken token;
            vanguard::u32 generation = 0;
            bool active = false;
        };

        struct ResolvedRoute final
        {
            HandlerRecord* handler = nullptr;
            void* receiverData = nullptr;
            editor::EditorCommandContextTypeId contextType = editor::EditorWorkspaceCommandContextTypeId;
        };

        enum class ShortcutDispatchResult : vanguard::u8
        {
            Continue,
            StopUnclaimed,
            Claimed,
            Failed
        };

        static vanguard::engine::FrameParticipantStatus ExecuteShortcutFrame(const vanguard::engine::FrameContext& context,
                                                                             void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorCommandService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("editor command shortcut service is unavailable");
            return service->RouteShortcutFrame(context);
        }

        [[nodiscard]] vanguard::engine::FrameParticipantStatus RouteShortcutFrame(const vanguard::engine::FrameContext& context) noexcept
        {
            BeginInputClaims(context.frame);
            m_status.currentFrame = context.frame;
            if (!vanguard::concurrency::IsMainThread())
                return vanguard::engine::FrameParticipantStatus::Failure("editor shortcut routing requires the main thread");
            if (m_status.state != editor::EditorCommandServiceState::Running || m_input == nullptr || m_ui == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("editor shortcut routing requires running input and UI dependencies");

            const editor::EditorUiInputCapture capture = m_ui->GetInputCapture();
            const bool uiCaptured = capture.frame == context.frame && (capture.keyboardCaptured || capture.textInputCaptured);
            const vanguard::containers::ArraySpan<const vanguard::input::RawEvent> events = m_input->GetEvents();
            if (events.Size() > vanguard::input::MaximumInputEventsPerFrame)
                return vanguard::engine::FrameParticipantStatus::Failure("editor shortcut routing input span exceeds its fixed claim capacity");
            m_claimEventCount = events.Size();

            for (vanguard::u32 eventIndex = 0; eventIndex < events.Size(); ++eventIndex)
            {
                const vanguard::input::RawEvent& event = events[eventIndex];
                m_shortcutModifiers.Apply(event);
                if (event.type != vanguard::input::EventType::KeyChanged || event.deviceType != vanguard::input::DeviceType::Keyboard ||
                    !event.data.key.pressed || IsShortcutModifierKey(event.data.key.key) || uiCaptured)
                    continue;

                const editor::EditorShortcutChord chord{event.data.key.key, m_shortcutModifiers.GetModifiers()};
                const ShortcutDispatchResult result = DispatchShortcutChord(chord, event.data.key.repeated);
                if (result == ShortcutDispatchResult::Claimed)
                    ClaimInputEvent(eventIndex);
                else if (result == ShortcutDispatchResult::Failed)
                    return vanguard::engine::FrameParticipantStatus::Failure(
                        m_status.failure.message != nullptr ? m_status.failure.message : "editor shortcut routing failed");
            }
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        void ClearInputClaims() noexcept
        {
            for (vanguard::u64& word : m_claimedInputEvents)
                word = 0;
            m_claimFrame = 0;
            m_claimEventCount = 0;
        }

        void BeginInputClaims(const vanguard::u64 frame) noexcept
        {
            for (vanguard::u64& word : m_claimedInputEvents)
                word = 0;
            m_claimFrame = frame;
            m_claimEventCount = 0;
        }

        void ClaimInputEvent(const vanguard::u32 eventIndex) noexcept
        {
            if (eventIndex >= vanguard::input::MaximumInputEventsPerFrame)
                return;
            m_claimedInputEvents[eventIndex >> 6u] |= vanguard::u64{1} << (eventIndex & 63u);
        }

        [[nodiscard]] HandlerRecord* FindShortcutHandler(const editor::EditorCommandContextTypeId contextType,
                                                         const editor::EditorShortcutChord chord) noexcept
        {
            for (HandlerRecord& handler : m_handlers)
            {
                if (!handler.active || !handler.reachable || handler.descriptor.contextType != contextType)
                    continue;
                bool bound = false;
                editor::EditorShortcutChord effective;
                ResolveShortcut(handler, bound, effective);
                if (bound && effective == chord)
                    return &handler;
            }
            return nullptr;
        }

        [[nodiscard]] bool QueryExactHandler(HandlerRecord& handler, void* const receiverData,
                                             editor::EditorCommandState& state,
                                             editor::EditorCommandFailure* const failure) noexcept
        {
            state = handler.descriptor.query(receiverData, handler.descriptor.handlerData);
            if (!ValidState(state.kind))
                return RouteFailure(failure, editor::EditorCommandFailureCode::HandlerQueryFailure,
                                    "editor command handler returned an invalid state", handler.descriptor.command,
                                    handler.descriptor.contextType, {}, handler.token);
            if (state.kind != editor::EditorCommandStateKind::Enabled)
                state.checked = false;
            return true;
        }

        [[nodiscard]] bool InvokeExactHandler(HandlerRecord& handler, void* const receiverData,
                                              editor::EditorCommandFailure* const failure) noexcept
        {
            if (!handler.descriptor.invoke(receiverData, handler.descriptor.handlerData))
                return RouteFailure(failure, editor::EditorCommandFailureCode::HandlerInvocationFailure,
                                    "editor command handler invocation failed", handler.descriptor.command,
                                    handler.descriptor.contextType, {}, handler.token);
            RecordRouteSuccess(handler.descriptor.command, handler.descriptor.contextType,
                               editor::EditorCommandRouteResult::Invoked);
            return true;
        }

        [[nodiscard]] ShortcutDispatchResult DispatchShortcutHandler(HandlerRecord& handler, void* const receiverData,
                                                                     const bool repeated) noexcept
        {
            const CommandRecord* const command = FindCommand(handler.descriptor.command);
            if (command == nullptr)
            {
                RecordRouteSuccess(handler.descriptor.command, handler.descriptor.contextType,
                                   editor::EditorCommandRouteResult::Unsupported);
                return ShortcutDispatchResult::Claimed;
            }

            if (repeated && !editor::HasEditorCommandFlag(command->descriptor.flags, editor::EditorCommandFlags::AllowRepeat))
            {
                m_status.lastCommand = handler.descriptor.command;
                m_status.lastContext = handler.descriptor.contextType;
                m_status.lastRoute = editor::EditorCommandRouteResult::None;
                m_status.failure = {};
                return ShortcutDispatchResult::Claimed;
            }

            m_dispatching = true;
            editor::EditorCommandState state;
            editor::EditorCommandFailure failure;
            if (!QueryExactHandler(handler, receiverData, state, &failure))
            {
                m_dispatching = false;
                return ShortcutDispatchResult::Failed;
            }
            if (state.kind == editor::EditorCommandStateKind::Unsupported)
            {
                m_dispatching = false;
                RecordRouteSuccess(handler.descriptor.command, handler.descriptor.contextType,
                                   editor::EditorCommandRouteResult::Unsupported);
                return ShortcutDispatchResult::Claimed;
            }
            if (state.kind == editor::EditorCommandStateKind::Disabled)
            {
                m_dispatching = false;
                RecordRouteSuccess(handler.descriptor.command, handler.descriptor.contextType,
                                   editor::EditorCommandRouteResult::Disabled);
                return ShortcutDispatchResult::Claimed;
            }
            const bool invoked = InvokeExactHandler(handler, receiverData, &failure);
            m_dispatching = false;
            return invoked ? ShortcutDispatchResult::Claimed : ShortcutDispatchResult::Failed;
        }

        [[nodiscard]] ShortcutDispatchResult ResolveShortcutTier(const editor::EditorCommandContextTier tier,
                                                                 const editor::EditorShortcutChord chord,
                                                                 const bool repeated) noexcept
        {
            for (vanguard::u32 stackIndex = m_contextStack.Size(); stackIndex > 0; --stackIndex)
            {
                ActiveContextRecord* const active = FindActiveContext(m_contextStack[stackIndex - 1u]);
                if (active == nullptr)
                {
                    static_cast<void>(Fail(nullptr, editor::EditorCommandFailureCode::CleanupFailure,
                                           "editor shortcut active context stack contains a stale token"));
                    return ShortcutDispatchResult::Failed;
                }
                if (active->tier != tier)
                    continue;

                HandlerRecord* const handler = FindShortcutHandler(active->contextType, chord);
                if (handler == nullptr)
                {
                    if (active->mode == editor::EditorCommandContextMode::Override)
                        return ShortcutDispatchResult::StopUnclaimed;
                    continue;
                }
                return DispatchShortcutHandler(*handler, active->receiverData, repeated);
            }
            return ShortcutDispatchResult::Continue;
        }

        [[nodiscard]] ShortcutDispatchResult DispatchShortcutChord(const editor::EditorShortcutChord chord,
                                                                   const bool repeated) noexcept
        {
            ShortcutDispatchResult result = ResolveShortcutTier(editor::EditorCommandContextTier::ControlTool, chord, repeated);
            if (result != ShortcutDispatchResult::Continue)
                return result;
            result = ResolveShortcutTier(editor::EditorCommandContextTier::Local, chord, repeated);
            if (result != ShortcutDispatchResult::Continue)
                return result;
            HandlerRecord* const workspace = FindShortcutHandler(editor::EditorWorkspaceCommandContextTypeId, chord);
            return workspace != nullptr ? DispatchShortcutHandler(*workspace, nullptr, repeated) : ShortcutDispatchResult::Continue;
        }

        void Reset() noexcept
        {
            m_commands.Clear();
            m_contextTypes.Clear();
            m_handlers.Clear();
            m_activeContexts.Clear();
            m_contextStack.Clear();
            m_nextActivationOrder = 0;
            m_shortcutPreferenceRevision = 1;
            m_dispatching = false;
            m_shortcutModifiers.Reset();
            ClearInputClaims();
            m_framePipeline = nullptr;
            m_input = nullptr;
            m_ui = nullptr;
            m_status = {};
        }

        void ClearActiveContexts() noexcept
        {
            for (ActiveContextRecord& context : m_activeContexts)
            {
                context.active = false;
                context.contextType = {};
                context.receiverData = nullptr;
                context.activationOrder = 0;
                context.token = {};
            }
            m_contextStack.Clear();
            m_status.activeContextCount = 0;
        }

        [[nodiscard]] bool HasExtensionOwnedRegistrations() const noexcept
        {
            for (const CommandRecord& command : m_commands)
                if (command.active && command.owner.IsValid())
                    return true;
            for (const ContextTypeRecord& context : m_contextTypes)
                if (context.active && context.owner.IsValid())
                    return true;
            for (const HandlerRecord& handler : m_handlers)
                if (handler.active && handler.owner.IsValid())
                    return true;
            return false;
        }

        [[nodiscard]] bool NeutralizeUnexpectedOwnedRegistrations() noexcept
        {
            bool found = false;
            for (CommandRecord& command : m_commands)
            {
                if (!command.active || !command.owner.IsValid())
                    continue;
                found = true;
                command.reachable = false;
            }
            for (ContextTypeRecord& context : m_contextTypes)
            {
                if (!context.active || !context.owner.IsValid())
                    continue;
                found = true;
                context.reachable = false;
            }
            for (HandlerRecord& handler : m_handlers)
            {
                if (!handler.active || !handler.owner.IsValid())
                    continue;
                found = true;
                handler.reachable = false;
                handler.descriptor.query = nullptr;
                handler.descriptor.invoke = nullptr;
                handler.descriptor.handlerData = nullptr;
            }
            return found;
        }

        static void ClearFailure(editor::EditorCommandFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(editor::EditorCommandFailure* const output, const editor::EditorCommandFailureCode code, const char* const message,
                                const editor::EditorCommandId command = {}, const editor::EditorCommandContextTypeId context = {},
                                const editor::EditorCommandToken commandToken = {}, const editor::EditorCommandHandlerToken handlerToken = {},
                                const bool recordStatus = true) noexcept
        {
            const editor::EditorCommandFailure failure{code, command, context, commandToken, handlerToken, message};
            if (output != nullptr)
                *output = failure;
            if (recordStatus)
                m_status.failure = failure;
            return false;
        }

        [[nodiscard]] bool RouteFailure(editor::EditorCommandFailure* const output, const editor::EditorCommandFailureCode code, const char* const message,
                                        const editor::EditorCommandId command, const editor::EditorCommandContextTypeId context = {},
                                        const editor::EditorCommandToken commandToken = {}, const editor::EditorCommandHandlerToken handlerToken = {},
                                        const editor::EditorCommandRouteResult route = editor::EditorCommandRouteResult::Failed,
                                        const bool recordStatus = true) noexcept
        {
            if (recordStatus)
            {
                m_status.lastCommand = command;
                m_status.lastContext = context;
                m_status.lastRoute = route;
            }
            return Fail(output, code, message, command, context, commandToken, handlerToken, recordStatus);
        }

        [[nodiscard]] app::LifecycleStatus LifecycleFailure(const editor::EditorCommandFailureCode code, const char* const message) noexcept
        {
            m_status.state = editor::EditorCommandServiceState::Failed;
            m_status.failure = {code, {}, {}, {}, {}, message};
            return app::LifecycleStatus::Failure(message);
        }

        [[nodiscard]] bool CanMutate(editor::EditorCommandFailure* const failure, const editor::EditorCommandId command = {},
                                     const editor::EditorCommandContextTypeId context = {}) noexcept
        {
            if (!vanguard::concurrency::IsMainThread())
                return Fail(failure, editor::EditorCommandFailureCode::WrongThread, "editor command registry mutation requires the main thread", command, context,
                            {}, {}, false);
            if (m_dispatching)
                return Fail(failure, editor::EditorCommandFailureCode::RecursiveDispatch, "editor command registry mutation is forbidden during dispatch", command,
                            context);
            if (m_status.state != editor::EditorCommandServiceState::Running)
                return Fail(failure, editor::EditorCommandFailureCode::WrongPhase, "editor command registry mutation requires a running service", command, context);
            return true;
        }

        static void ResolveShortcut(const editor::EditorShortcutChord defaultShortcut,
                                    const editor::EditorShortcutOverride& shortcutOverride,
                                    bool& bound,
                                    editor::EditorShortcutChord& chord) noexcept
        {
            bound = false;
            chord = {};
            switch (shortcutOverride.mode)
            {
            case editor::EditorShortcutOverrideMode::Default:
                chord = defaultShortcut;
                bound = !chord.IsEmpty();
                return;
            case editor::EditorShortcutOverrideMode::Unbound:
                return;
            case editor::EditorShortcutOverrideMode::Custom:
                chord = shortcutOverride.chord;
                bound = true;
                return;
            default:
                return;
            }
        }

        static void ResolveShortcut(const HandlerRecord& handler, bool& bound,
                                    editor::EditorShortcutChord& chord) noexcept
        {
            ResolveShortcut(handler.descriptor.defaultShortcut, handler.shortcutOverride, bound, chord);
        }

        void AdvanceShortcutPreferenceRevision() noexcept
        {
            if (m_shortcutPreferenceRevision == 0)
                return;
            if (m_shortcutPreferenceRevision == ~vanguard::u64{0})
            {
                m_shortcutPreferenceRevision = 0;
                return;
            }
            ++m_shortcutPreferenceRevision;
        }

        [[nodiscard]] bool FailConst(editor::EditorCommandFailure* const failure,
                                     const editor::EditorCommandFailureCode code,
                                     const char* const message) const noexcept
        {
            if (failure != nullptr)
                *failure = {code, {}, {}, {}, {}, message};
            return false;
        }

        [[nodiscard]] const HandlerRecord* FindShortcutConflict(
            const editor::EditorCommandContextTypeId contextType, const editor::EditorShortcutChord chord,
            const HandlerRecord* const exclude) const noexcept
        {
            for (const HandlerRecord& handler : m_handlers)
            {
                if (!handler.active || !handler.reachable || &handler == exclude ||
                    handler.descriptor.contextType != contextType)
                    continue;
                bool bound = false;
                editor::EditorShortcutChord effective;
                ResolveShortcut(handler, bound, effective);
                if (bound && effective == chord)
                    return &handler;
            }
            return nullptr;
        }

        [[nodiscard]] bool BeginRoute(const editor::EditorCommandId command, editor::EditorCommandFailure* const failure) noexcept
        {
            if (!vanguard::concurrency::IsMainThread())
                return RouteFailure(failure, editor::EditorCommandFailureCode::WrongThread, "editor command routing requires the main thread", command, {}, {}, {},
                                    editor::EditorCommandRouteResult::Failed, false);
            if (m_dispatching)
                return RouteFailure(failure, editor::EditorCommandFailureCode::RecursiveDispatch, "recursive editor command routing is forbidden", command);
            if (m_status.state != editor::EditorCommandServiceState::Running)
                return RouteFailure(failure, editor::EditorCommandFailureCode::WrongPhase, "editor command routing requires a running service", command);
            if (!command.IsValid())
                return RouteFailure(failure, editor::EditorCommandFailureCode::InvalidIdentity, "editor command identity is invalid", command);
            m_dispatching = true;
            return true;
        }

        enum class ContextResolveResult : vanguard::u8
        {
            Continue,
            Selected,
            Failed
        };

        [[nodiscard]] ContextResolveResult ResolveTier(const editor::EditorCommandContextTier tier,
                                                       const editor::EditorCommandId command, editor::EditorCommandState& state,
                                                       ResolvedRoute& route, editor::EditorCommandFailure* const failure) noexcept
        {
            for (vanguard::u32 stackIndex = m_contextStack.Size(); stackIndex > 0; --stackIndex)
            {
                ActiveContextRecord* const active = FindActiveContext(m_contextStack[stackIndex - 1u]);
                if (active == nullptr)
                {
                    static_cast<void>(RouteFailure(failure, editor::EditorCommandFailureCode::CleanupFailure,
                                                   "editor command active context stack contains a stale token", command));
                    return ContextResolveResult::Failed;
                }
                if (active->tier != tier)
                    continue;

                HandlerRecord* const handler = FindHandler(active->contextType, command);
                if (handler == nullptr)
                {
                    if (active->mode == editor::EditorCommandContextMode::Override)
                    {
                        state = editor::EditorCommandState::Unsupported();
                        route = {nullptr, active->receiverData, active->contextType};
                        return ContextResolveResult::Selected;
                    }
                    continue;
                }

                editor::EditorCommandState candidate;
                if (!QueryExactHandler(*handler, active->receiverData, candidate, failure))
                    return ContextResolveResult::Failed;
                if (candidate.kind == editor::EditorCommandStateKind::Unsupported &&
                    active->mode == editor::EditorCommandContextMode::Inherit)
                    continue;

                state = candidate;
                route = {handler, active->receiverData, active->contextType};
                return ContextResolveResult::Selected;
            }
            return ContextResolveResult::Continue;
        }

        [[nodiscard]] bool Resolve(const editor::EditorCommandId command, editor::EditorCommandState& state, ResolvedRoute& route,
                                   editor::EditorCommandFailure* const failure) noexcept
        {
            state = editor::EditorCommandState::Unsupported();
            route = {};
            const CommandRecord* const commandRecord = FindCommand(command);
            if (commandRecord == nullptr)
            {
                m_dispatching = false;
                return RouteFailure(failure, editor::EditorCommandFailureCode::MissingCommand, "editor command is not registered", command);
            }

            const ContextResolveResult control = ResolveTier(editor::EditorCommandContextTier::ControlTool, command, state, route, failure);
            if (control == ContextResolveResult::Failed)
            {
                m_dispatching = false;
                return false;
            }
            if (control == ContextResolveResult::Selected)
                return true;

            const ContextResolveResult local = ResolveTier(editor::EditorCommandContextTier::Local, command, state, route, failure);
            if (local == ContextResolveResult::Failed)
            {
                m_dispatching = false;
                return false;
            }
            if (local == ContextResolveResult::Selected)
                return true;

            route.contextType = editor::EditorWorkspaceCommandContextTypeId;
            route.handler = FindHandler(editor::EditorWorkspaceCommandContextTypeId, command);
            route.receiverData = nullptr;
            if (route.handler == nullptr)
                return true;

            if (!QueryExactHandler(*route.handler, nullptr, state, failure))
            {
                m_dispatching = false;
                return false;
            }
            return true;
        }

        void RecordRouteSuccess(const editor::EditorCommandId command, const editor::EditorCommandContextTypeId context,
                                const editor::EditorCommandRouteResult route) noexcept
        {
            m_status.lastCommand = command;
            m_status.lastContext = context;
            m_status.lastRoute = route;
            m_status.failure = {};
        }

        [[nodiscard]] static editor::EditorCommandRouteResult RouteForState(const editor::EditorCommandState& state) noexcept
        {
            switch (state.kind)
            {
            case editor::EditorCommandStateKind::Unsupported:
                return editor::EditorCommandRouteResult::Unsupported;
            case editor::EditorCommandStateKind::Disabled:
                return editor::EditorCommandRouteResult::Disabled;
            case editor::EditorCommandStateKind::Enabled:
                return editor::EditorCommandRouteResult::Enabled;
            }
            return editor::EditorCommandRouteResult::Failed;
        }

        [[nodiscard]] CommandRecord* FindCommand(const editor::EditorCommandId id) noexcept
        {
            for (CommandRecord& command : m_commands)
                if (command.active && command.reachable && command.descriptor.id == id)
                    return &command;
            return nullptr;
        }

        [[nodiscard]] const CommandRecord* FindCommand(const editor::EditorCommandId id) const noexcept
        {
            for (const CommandRecord& command : m_commands)
                if (command.active && command.reachable && command.descriptor.id == id)
                    return &command;
            return nullptr;
        }

        [[nodiscard]] CommandRecord* FindCommandRecord(const editor::EditorCommandToken token) noexcept
        {
            return token.IsValid() && token.index < m_commands.Size() && m_commands[token.index].active &&
                           m_commands[token.index].generation == token.generation
                       ? &m_commands[token.index]
                       : nullptr;
        }

        [[nodiscard]] HandlerRecord* FindHandler(const editor::EditorCommandContextTypeId context, const editor::EditorCommandId command) noexcept
        {
            for (HandlerRecord& handler : m_handlers)
                if (handler.active && handler.reachable && handler.descriptor.contextType == context && handler.descriptor.command == command)
                    return &handler;
            return nullptr;
        }

        [[nodiscard]] HandlerRecord* FindHandlerRecord(const editor::EditorCommandHandlerToken token) noexcept
        {
            return token.IsValid() && token.index < m_handlers.Size() && m_handlers[token.index].active &&
                           m_handlers[token.index].generation == token.generation
                       ? &m_handlers[token.index]
                       : nullptr;
        }

        [[nodiscard]] bool IsRegisteredContextType(const editor::EditorCommandContextTypeId id) noexcept
        {
            return id == editor::EditorWorkspaceCommandContextTypeId || FindContextType(id) != nullptr;
        }

        [[nodiscard]] ContextTypeRecord* FindContextType(const editor::EditorCommandContextTypeId id) noexcept
        {
            for (ContextTypeRecord& context : m_contextTypes)
                if (context.active && context.reachable && context.descriptor.id == id)
                    return &context;
            return nullptr;
        }

        [[nodiscard]] const ContextTypeRecord* FindContextTypeRecord(const editor::EditorCommandContextTypeId id) const noexcept
        {
            for (const ContextTypeRecord& context : m_contextTypes)
                if (context.active && context.descriptor.id == id)
                    return &context;
            return nullptr;
        }

        [[nodiscard]] ContextTypeRecord* FindContextTypeRecord(const editor::EditorCommandContextTypeToken token) noexcept
        {
            return token.IsValid() && token.index < m_contextTypes.Size() && m_contextTypes[token.index].active &&
                           m_contextTypes[token.index].generation == token.generation
                       ? &m_contextTypes[token.index]
                       : nullptr;
        }

        [[nodiscard]] ActiveContextRecord* FindActiveContext(const editor::EditorCommandContextToken token) noexcept
        {
            return token.IsValid() && token.index < m_activeContexts.Size() && m_activeContexts[token.index].active &&
                           m_activeContexts[token.index].generation == token.generation
                       ? &m_activeContexts[token.index]
                       : nullptr;
        }

        vanguard::containers::StaticArray<CommandRecord, editor::MaximumEditorCommands> m_commands;
        vanguard::containers::StaticArray<ContextTypeRecord, editor::MaximumEditorCommandContextTypes - 1u> m_contextTypes;
        vanguard::containers::StaticArray<HandlerRecord, editor::MaximumEditorCommandHandlers> m_handlers;
        vanguard::containers::StaticArray<ActiveContextRecord, editor::MaximumActiveEditorCommandContexts> m_activeContexts;
        vanguard::containers::StaticArray<editor::EditorCommandContextToken, editor::MaximumActiveEditorCommandContexts> m_contextStack;
        editor::EditorShortcutModifierState m_shortcutModifiers;
        vanguard::u64 m_claimedInputEvents[InputClaimWordCount]{};
        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::InputService* m_input = nullptr;
        editor::EditorUiService* m_ui = nullptr;
        vanguard::u64 m_nextActivationOrder = 0;
        vanguard::u64 m_shortcutPreferenceRevision = 1;
        vanguard::u64 m_claimFrame = 0;
        vanguard::u32 m_claimEventCount = 0;
        bool m_dispatching = false;
        editor::EditorCommandServiceStatus m_status;
    };

    app::Service* CreateEditorCommandService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(ManagedEditorCommandService), alignof(ManagedEditorCommandService));
        return block ? ::new (block.address) ManagedEditorCommandService() : nullptr;
    }

    void DestroyEditorCommandService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorCommandService*>(service)->~ManagedEditorCommandService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorCommandService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorCommandService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required},
            {engine::InputServiceId, application::DependencyKind::Required},
            {EditorUiServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorCommandCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorCommandServiceId;
        descriptor.name = "editorCommands";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 3};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorCommandService;
        descriptor.destroy = DestroyEditorCommandService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorCommandService* FindEditorCommandService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorCommandService*>(host.FindCapability(EditorCommandCapabilityId));
    }

    EditorCommandService* FindEditorCommandService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorCommandService*>(context.FindCapability(EditorCommandCapabilityId));
    }
} // namespace vanguard::editor
