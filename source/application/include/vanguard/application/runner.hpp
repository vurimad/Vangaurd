#pragma once

#include <vanguard/application/state_machine.hpp>

namespace vanguard::application
{
    struct ApplicationStartupContext
    {
        const char* applicationName = nullptr;
        ApplicationProfile profile = ApplicationProfile::None;
        CommandLineView commandLine;
        IPlatformHost* platform = nullptr;
    };

    struct CompositionStatus
    {
        bool succeeded = true;
        const char* message = nullptr;

        [[nodiscard]] static constexpr CompositionStatus Success() noexcept { return {}; }
        [[nodiscard]] static constexpr CompositionStatus Failure(const char* const message) noexcept
        {
            return {false, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return succeeded; }
    };

    class IApplicationComposition
    {
    public:
        virtual ~IApplicationComposition() = default;

        IApplicationComposition(const IApplicationComposition&) = delete;
        IApplicationComposition& operator=(const IApplicationComposition&) = delete;

        [[nodiscard]] virtual CompositionStatus Compose(const ApplicationStartupContext& startup,
                                                        EngineHost& services,
                                                        ApplicationStateMachine& states) noexcept = 0;

    protected:
        IApplicationComposition() noexcept = default;
    };

    struct RunnerSettings
    {
        const char* applicationName = nullptr;
        ApplicationProfile profile = ApplicationProfile::Runtime;
        CommandLineView commandLine;
        u32 maximumShutdownTicks = 600;
    };

    enum class RunnerFailureCode : u8
    {
        None,
        InvalidSettings,
        MemoryBootstrapFailure,
        DiagnosticsBootstrapFailure,
        ContainersBootstrapFailure,
        PlatformInitializationFailure,
        CompositionFailure,
        ServiceGraphFailure,
        ServiceStartupFailure,
        StateMachineFailure,
        PlatformPumpFailure,
        ShutdownTimeout,
        ServiceShutdownFailure
    };

    struct RunnerResult
    {
        i32 exitCode = 0;
        RunnerFailureCode failure = RunnerFailureCode::None;
        const char* message = nullptr;
        HostFailure hostFailure;
        StateMachineFailure stateFailure;

        [[nodiscard]] explicit operator bool() const noexcept { return failure == RunnerFailureCode::None; }
    };

    class ApplicationRunner final
    {
    public:
        [[nodiscard]] RunnerResult Run(IPlatformHost& platform, IApplicationComposition& composition,
                                       const RunnerSettings& settings) noexcept;
    };
} // namespace vanguard::application
