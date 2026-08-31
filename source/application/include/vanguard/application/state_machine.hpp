#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/application/platform_host.hpp>

namespace vanguard::application
{
    using StateId = u64;
    inline constexpr StateId InvalidStateId = 0;
    inline constexpr u32 MaximumApplicationStates = 64;

    enum class StateOperationResult : u8
    {
        Complete,
        Pending,
        Failure
    };

    struct StateOperationStatus
    {
        StateOperationResult result = StateOperationResult::Complete;
        const char* message = nullptr;

        [[nodiscard]] static constexpr StateOperationStatus Complete() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr StateOperationStatus Pending() noexcept
        {
            return {StateOperationResult::Pending, nullptr};
        }
        [[nodiscard]] static constexpr StateOperationStatus Failure(const char* const message) noexcept
        {
            return {StateOperationResult::Failure, message};
        }
    };

    struct StateTickStatus
    {
        bool succeeded = true;
        const char* message = nullptr;

        [[nodiscard]] static constexpr StateTickStatus Success() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr StateTickStatus Failure(const char* const message) noexcept
        {
            return {false, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return succeeded;
        }
    };

    enum class StateMachinePhase : u8
    {
        Building,
        Entering,
        Active,
        Exiting,
        Stopped,
        Failed
    };

    enum class StateMachineFailureCode : u8
    {
        None,
        InvalidArgument,
        InvalidPhase,
        LimitExceeded,
        DuplicateState,
        UnknownState,
        TransitionAlreadyPending,
        EnterFailure,
        TickFailure,
        ExitFailure
    };

    struct StateMachineFailure
    {
        StateMachineFailureCode code = StateMachineFailureCode::None;
        StateId state = InvalidStateId;
        StateId requestedState = InvalidStateId;
        const char* message = nullptr;
    };

    class ApplicationStateMachine;

    class StateContext final
    {
    public:
        [[nodiscard]] EngineHost& GetServices() const noexcept;
        [[nodiscard]] IPlatformHost& GetPlatform() const noexcept;
        [[nodiscard]] StateId GetCurrentState() const noexcept;
        [[nodiscard]] bool RequestTransition(StateId state, StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestExit(i32 exitCode = 0, StateMachineFailure* failure = nullptr) noexcept;

    private:
        StateContext(ApplicationStateMachine& machine, EngineHost& services, IPlatformHost& platform, StateId currentState) noexcept;

        ApplicationStateMachine* m_machine = nullptr;
        EngineHost* m_services = nullptr;
        IPlatformHost* m_platform = nullptr;
        StateId m_currentState = InvalidStateId;

        friend class ApplicationStateMachine;
    };

    class ApplicationState
    {
    public:
        virtual ~ApplicationState() = default;

        ApplicationState(const ApplicationState&) = delete;
        ApplicationState& operator=(const ApplicationState&) = delete;

    protected:
        ApplicationState() noexcept = default;
        virtual StateOperationStatus OnEnter(StateContext& context) noexcept;
        virtual StateTickStatus OnTick(StateContext& context) noexcept;
        virtual StateOperationStatus OnExit(StateContext& context) noexcept;

    private:
        friend class ApplicationStateMachine;
    };

    struct ApplicationStateDescriptor
    {
        StateId id = InvalidStateId;
        const char* name = nullptr;
        ApplicationState* state = nullptr;
    };

    enum class StateMachineTickResult : u8
    {
        Running,
        Stopped,
        Failure
    };

    class ApplicationStateMachine final
    {
    public:
        struct Impl;

        ApplicationStateMachine() noexcept = default;
        ~ApplicationStateMachine();

        ApplicationStateMachine(const ApplicationStateMachine&) = delete;
        ApplicationStateMachine& operator=(const ApplicationStateMachine&) = delete;

        [[nodiscard]] bool RegisterState(const ApplicationStateDescriptor& descriptor, StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetInitialState(StateId state, StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Start(StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] StateMachineTickResult Tick(EngineHost& services, IPlatformHost& platform, StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestTransition(StateId state, StateMachineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestExit(i32 exitCode = 0, StateMachineFailure* failure = nullptr) noexcept;

        [[nodiscard]] StateMachinePhase GetPhase() const noexcept;
        [[nodiscard]] StateId GetCurrentState() const noexcept;
        [[nodiscard]] StateId GetPendingState() const noexcept;
        [[nodiscard]] bool IsExitRequested() const noexcept;
        [[nodiscard]] i32 ExitCode() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::application
