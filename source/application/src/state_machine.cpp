#include <vanguard/application/state_machine.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateStateObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Runtime, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template<typename Type>
    void DeleteStateObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Runtime};
        memory::Free(block);
    }

    [[nodiscard]] bool ValidName(const char* const name) noexcept
    {
        return name != nullptr && name[0] != '\0';
    }
} // namespace

namespace vanguard::application
{
    StateOperationStatus ApplicationState::OnEnter(StateContext&) noexcept { return StateOperationStatus::Complete(); }
    StateTickStatus ApplicationState::OnTick(StateContext&) noexcept { return StateTickStatus::Success(); }
    StateOperationStatus ApplicationState::OnExit(StateContext&) noexcept { return StateOperationStatus::Complete(); }

    struct ApplicationStateMachine::Impl
    {
        struct StateRecord
        {
            ApplicationStateDescriptor descriptor;
        };

        Impl() noexcept : states(memory::pools::Runtime::GetInstance())
        {
            states.Reserve(MaximumApplicationStates);
        }

        [[nodiscard]] StateRecord* Find(const StateId id) const noexcept
        {
            for (StateRecord* const record : states) if (record->descriptor.id == id) return record;
            return nullptr;
        }

        void ReportFailure(StateMachineFailure* const output, const StateMachineFailureCode code,
                           const StateId state, const StateId requestedState, const char* const message) const noexcept
        {
            if (output != nullptr) *output = {code, state, requestedState, message};
            if (diagnostics::IsInitialized())
                VG_LOG_ERROR(diagnostics::Category::GameStateMachine,
                             "application state failure: code=%u state=%llu requested=%llu message=%s",
                             static_cast<u32>(code), static_cast<unsigned long long>(state),
                             static_cast<unsigned long long>(requestedState), message != nullptr ? message : "unspecified");
        }

        containers::DynamicArray<StateRecord*> states;
        StateRecord* current = nullptr;
        StateId initial = InvalidStateId;
        StateId pending = InvalidStateId;
        StateMachinePhase phase = StateMachinePhase::Building;
        i32 exitCode = 0;
        bool transitionPending = false;
        bool exitRequested = false;
    };

    ApplicationStateMachine::~ApplicationStateMachine()
    {
        if (m_impl == nullptr) return;
        for (Impl::StateRecord* const state : m_impl->states) DeleteStateObject(state);
        DeleteStateObject(m_impl);
        m_impl = nullptr;
    }

    bool ApplicationStateMachine::RegisterState(const ApplicationStateDescriptor& descriptor,
                                                StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (descriptor.id == InvalidStateId || !ValidName(descriptor.name) || descriptor.state == nullptr)
        {
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidArgument, descriptor.id, InvalidStateId,
                            "invalid application state descriptor"};
            return false;
        }
        if (m_impl == nullptr)
        {
            m_impl = AllocateStateObject<Impl>();
            if (m_impl == nullptr)
            {
                if (failure != nullptr)
                    *failure = {StateMachineFailureCode::LimitExceeded, descriptor.id, InvalidStateId,
                                "state-machine allocation failed"};
                return false;
            }
        }
        if (m_impl->phase != StateMachinePhase::Building)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::InvalidPhase, descriptor.id, InvalidStateId,
                                  "state registration is closed");
            return false;
        }
        if (m_impl->Find(descriptor.id) != nullptr)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::DuplicateState, descriptor.id, descriptor.id,
                                  "duplicate application state ID");
            return false;
        }
        if (m_impl->states.Size() >= MaximumApplicationStates)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::LimitExceeded, descriptor.id, InvalidStateId,
                                  "application state limit exceeded");
            return false;
        }
        Impl::StateRecord* const state = AllocateStateObject<Impl::StateRecord>();
        if (state == nullptr)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::LimitExceeded, descriptor.id, InvalidStateId,
                                  "application state record allocation failed");
            return false;
        }
        state->descriptor = descriptor;
        m_impl->states.PushBack(state);
        return true;
    }

    bool ApplicationStateMachine::SetInitialState(const StateId state, StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->phase != StateMachinePhase::Building)
        {
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidPhase, InvalidStateId, state,
                            "initial state can only be selected while building"};
            return false;
        }
        if (m_impl->Find(state) == nullptr)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::UnknownState, InvalidStateId, state,
                                  "initial application state is not registered");
            return false;
        }
        m_impl->initial = state;
        return true;
    }

    bool ApplicationStateMachine::Start(StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->phase != StateMachinePhase::Building || m_impl->initial == InvalidStateId)
        {
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidPhase, InvalidStateId, InvalidStateId,
                            "state machine has no valid initial state"};
            return false;
        }
        m_impl->current = m_impl->Find(m_impl->initial);
        m_impl->phase = StateMachinePhase::Entering;
        return true;
    }

    StateMachineTickResult ApplicationStateMachine::Tick(EngineHost& services, IPlatformHost& platform,
                                                         StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->current == nullptr ||
            (m_impl->phase != StateMachinePhase::Entering && m_impl->phase != StateMachinePhase::Active &&
             m_impl->phase != StateMachinePhase::Exiting))
        {
            if (m_impl != nullptr && m_impl->phase == StateMachinePhase::Stopped) return StateMachineTickResult::Stopped;
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidPhase, CurrentState(), PendingState(),
                            "state machine cannot tick in its current phase"};
            return StateMachineTickResult::Failure;
        }

        StateContext context(*this, services, platform, m_impl->current->descriptor.id);
        if (m_impl->phase == StateMachinePhase::Entering)
        {
            const StateOperationStatus status = m_impl->current->descriptor.state->OnEnter(context);
            if (status.result == StateOperationResult::Failure)
            {
                m_impl->pending = InvalidStateId;
                m_impl->transitionPending = true;
                m_impl->exitRequested = true;
                m_impl->phase = StateMachinePhase::Exiting;
                m_impl->ReportFailure(failure, StateMachineFailureCode::EnterFailure,
                                      m_impl->current->descriptor.id, m_impl->pending,
                                      status.message != nullptr ? status.message : "application state enter failed");
                return StateMachineTickResult::Failure;
            }
            if (status.result == StateOperationResult::Pending) return StateMachineTickResult::Running;
            m_impl->phase = m_impl->transitionPending ? StateMachinePhase::Exiting : StateMachinePhase::Active;
            return StateMachineTickResult::Running;
        }
        if (m_impl->phase == StateMachinePhase::Active)
        {
            const StateTickStatus status = m_impl->current->descriptor.state->OnTick(context);
            if (!status)
            {
                m_impl->pending = InvalidStateId;
                m_impl->transitionPending = true;
                m_impl->exitRequested = true;
                m_impl->phase = StateMachinePhase::Exiting;
                m_impl->ReportFailure(failure, StateMachineFailureCode::TickFailure,
                                      m_impl->current->descriptor.id, m_impl->pending,
                                      status.message != nullptr ? status.message : "application state tick failed");
                return StateMachineTickResult::Failure;
            }
            if (m_impl->transitionPending) m_impl->phase = StateMachinePhase::Exiting;
            return StateMachineTickResult::Running;
        }

        const StateOperationStatus status = m_impl->current->descriptor.state->OnExit(context);
        if (status.result == StateOperationResult::Failure)
        {
            m_impl->phase = StateMachinePhase::Failed;
            m_impl->ReportFailure(failure, StateMachineFailureCode::ExitFailure,
                                  m_impl->current->descriptor.id, m_impl->pending,
                                  status.message != nullptr ? status.message : "application state exit failed");
            return StateMachineTickResult::Failure;
        }
        if (status.result == StateOperationResult::Pending) return StateMachineTickResult::Running;
        if (m_impl->exitRequested)
        {
            m_impl->current = nullptr;
            m_impl->pending = InvalidStateId;
            m_impl->transitionPending = false;
            m_impl->phase = StateMachinePhase::Stopped;
            return StateMachineTickResult::Stopped;
        }
        m_impl->current = m_impl->Find(m_impl->pending);
        m_impl->pending = InvalidStateId;
        m_impl->transitionPending = false;
        m_impl->phase = StateMachinePhase::Entering;
        return StateMachineTickResult::Running;
    }

    bool ApplicationStateMachine::RequestTransition(const StateId state, StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || (m_impl->phase != StateMachinePhase::Entering &&
                                 m_impl->phase != StateMachinePhase::Active))
        {
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidPhase, CurrentState(), state,
                            "transition cannot be requested in the current phase"};
            return false;
        }
        if (m_impl->transitionPending)
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::TransitionAlreadyPending, CurrentState(), state,
                                  "an application state transition is already pending");
            return false;
        }
        if (state == InvalidStateId || m_impl->Find(state) == nullptr || state == CurrentState())
        {
            m_impl->ReportFailure(failure, StateMachineFailureCode::UnknownState, CurrentState(), state,
                                  "requested application state is invalid, unknown, or already active");
            return false;
        }
        m_impl->pending = state;
        m_impl->transitionPending = true;
        m_impl->exitRequested = false;
        return true;
    }

    bool ApplicationStateMachine::RequestExit(const i32 exitCode, StateMachineFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || (m_impl->phase != StateMachinePhase::Entering &&
                                 m_impl->phase != StateMachinePhase::Active &&
                                 m_impl->phase != StateMachinePhase::Exiting))
        {
            if (failure != nullptr)
                *failure = {StateMachineFailureCode::InvalidPhase, CurrentState(), InvalidStateId,
                            "exit cannot be requested in the current phase"};
            return false;
        }
        // Process exit is a higher-priority terminal transition and explicitly supersedes a queued state change.
        m_impl->pending = InvalidStateId;
        m_impl->transitionPending = true;
        m_impl->exitRequested = true;
        m_impl->exitCode = exitCode;
        if (m_impl->phase == StateMachinePhase::Active) m_impl->phase = StateMachinePhase::Exiting;
        return true;
    }

    StateMachinePhase ApplicationStateMachine::Phase() const noexcept
    {
        return m_impl != nullptr ? m_impl->phase : StateMachinePhase::Building;
    }
    StateId ApplicationStateMachine::CurrentState() const noexcept
    {
        return m_impl != nullptr && m_impl->current != nullptr ? m_impl->current->descriptor.id : InvalidStateId;
    }
    StateId ApplicationStateMachine::PendingState() const noexcept
    {
        return m_impl != nullptr ? m_impl->pending : InvalidStateId;
    }
    bool ApplicationStateMachine::ExitRequested() const noexcept
    {
        return m_impl != nullptr && m_impl->exitRequested;
    }
    i32 ApplicationStateMachine::ExitCode() const noexcept { return m_impl != nullptr ? m_impl->exitCode : 0; }

    StateContext::StateContext(ApplicationStateMachine& machine, EngineHost& services, IPlatformHost& platform,
                               const StateId currentState) noexcept
        : m_machine(&machine), m_services(&services), m_platform(&platform), m_currentState(currentState) {}
    EngineHost& StateContext::Services() const noexcept { return *m_services; }
    IPlatformHost& StateContext::Platform() const noexcept { return *m_platform; }
    StateId StateContext::CurrentState() const noexcept { return m_currentState; }
    bool StateContext::RequestTransition(const StateId state, StateMachineFailure* const failure) noexcept
    {
        return m_machine->RequestTransition(state, failure);
    }
    bool StateContext::RequestExit(const i32 exitCode, StateMachineFailure* const failure) noexcept
    {
        return m_machine->RequestExit(exitCode, failure);
    }
} // namespace vanguard::application
