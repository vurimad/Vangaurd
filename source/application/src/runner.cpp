#include <vanguard/application/runner.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

namespace
{
    [[nodiscard]] bool ValidSettings(const vanguard::application::RunnerSettings& settings) noexcept
    {
        return settings.applicationName != nullptr && settings.applicationName[0] != '\0' &&
               settings.profile != vanguard::application::ApplicationProfile::None &&
               settings.commandLine.argumentCount >= 0 &&
               (settings.commandLine.argumentCount == 0 || settings.commandLine.arguments != nullptr);
    }
}

namespace vanguard::application
{
    RunnerResult ApplicationRunner::Run(IPlatformHost& platform, IApplicationComposition& composition,
                                        const RunnerSettings& settings) noexcept
    {
        RunnerResult result;
        if (!ValidSettings(settings))
        {
            result.failure = RunnerFailureCode::InvalidSettings;
            result.message = "invalid application runner settings";
            return result;
        }
        if (!memory::IsInitialized() && !memory::Initialize())
        {
            result.failure = RunnerFailureCode::MemoryBootstrapFailure;
            result.message = "root memory bootstrap failed";
            return result;
        }

        const bool diagnosticsOwned = !diagnostics::IsInitialized();
        if (diagnosticsOwned && !diagnostics::Initialize(diagnostics::Mode::Asynchronous, settings.applicationName))
        {
            result.failure = RunnerFailureCode::DiagnosticsBootstrapFailure;
            result.message = "diagnostics bootstrap failed";
            return result;
        }
        const bool diagnosticsFileOpened = diagnosticsOwned && settings.diagnosticsFilePath != nullptr &&
                                           diagnostics::OpenFileSink(settings.diagnosticsFilePath);
        if (!containers::IsInitialized() && !containers::Initialize())
        {
            result.failure = RunnerFailureCode::ContainersBootstrapFailure;
            result.message = "containers bootstrap failed";
            if (diagnosticsOwned) diagnostics::Shutdown();
            return result;
        }

        concurrency::InitializeMainThread();
        concurrency::SetCurrentThreadName("VanguardMain");
        bool platformInitialized = false;
        {
            EngineHost services;
            ApplicationStateMachine states;
            const PlatformStartupInfo platformStartup{settings.applicationName, settings.profile, settings.commandLine};
            const PlatformStatus platformStatus = platform.Initialize(platformStartup);
            if (!platformStatus)
            {
                result.failure = RunnerFailureCode::PlatformInitializationFailure;
                result.message = platformStatus.message != nullptr ? platformStatus.message : "platform initialization failed";
            }
            else
            {
                platformInitialized = true;
                const ApplicationStartupContext startup{
                    settings.applicationName, settings.profile, settings.commandLine, &platform};
                const CompositionStatus compositionStatus = composition.Compose(startup, services, states);
                if (!compositionStatus)
                {
                    result.failure = RunnerFailureCode::CompositionFailure;
                    result.message = compositionStatus.message != nullptr ? compositionStatus.message : "application composition failed";
                }
                else if (!services.Compile(settings.profile, &result.hostFailure))
                {
                    result.failure = RunnerFailureCode::ServiceGraphFailure;
                    result.message = result.hostFailure.message;
                }
                else if (!services.Start(&result.hostFailure))
                {
                    result.failure = RunnerFailureCode::ServiceStartupFailure;
                    result.message = result.hostFailure.message;
                }
                else if (!states.Start(&result.stateFailure))
                {
                    result.failure = RunnerFailureCode::StateMachineFailure;
                    result.message = result.stateFailure.message;
                }
                else
                {
                    bool platformPumpAvailable = true;
                    bool shutdownRequested = false;
                    u32 shutdownTicks = 0;
                    while (states.Phase() != StateMachinePhase::Stopped && states.Phase() != StateMachinePhase::Failed)
                    {
                        if (platformPumpAvailable)
                        {
                            const PlatformPumpResult pump = platform.PumpEvents();
                            if (pump.action == PlatformPumpAction::ExitRequested)
                            {
                                StateMachineFailure exitFailure;
                                static_cast<void>(states.RequestExit(pump.exitCode, &exitFailure));
                                shutdownRequested = true;
                            }
                            else if (pump.action == PlatformPumpAction::Failure)
                            {
                                result.failure = RunnerFailureCode::PlatformPumpFailure;
                                result.message = pump.message != nullptr ? pump.message : "platform event pump failed";
                                StateMachineFailure exitFailure;
                                static_cast<void>(states.RequestExit(pump.exitCode, &exitFailure));
                                shutdownRequested = true;
                                platformPumpAvailable = false;
                            }
                        }

                        StateMachineFailure tickFailure;
                        const StateMachineTickResult tick = states.Tick(services, platform, &tickFailure);
                        if (tick == StateMachineTickResult::Failure)
                        {
                            if (result.failure == RunnerFailureCode::None)
                            {
                                result.failure = RunnerFailureCode::StateMachineFailure;
                                result.message = tickFailure.message;
                                result.stateFailure = tickFailure;
                            }
                            if (states.Phase() != StateMachinePhase::Exiting) break;
                            shutdownRequested = true;
                        }
                        if (tick == StateMachineTickResult::Stopped) break;
                        if (states.ExitRequested()) shutdownRequested = true;
                        if (shutdownRequested && settings.maximumShutdownTicks != 0 &&
                            ++shutdownTicks > settings.maximumShutdownTicks)
                        {
                            result.failure = RunnerFailureCode::ShutdownTimeout;
                            result.message = "application state shutdown exceeded its tick budget";
                            break;
                        }
                    }
                    result.exitCode = states.ExitCode();
                }

                if (services.State() == HostState::Running)
                {
                    HostFailure shutdownFailure;
                    if (!services.Shutdown(&shutdownFailure) && result.failure == RunnerFailureCode::None)
                    {
                        result.failure = RunnerFailureCode::ServiceShutdownFailure;
                        result.message = shutdownFailure.message;
                        result.hostFailure = shutdownFailure;
                    }
                }
            }
            if (platformInitialized) platform.Shutdown();
        }

        if (result.failure != RunnerFailureCode::None)
            VG_LOG_ERROR(diagnostics::Category::Engine, "application runner failed: code=%u message=%s",
                         static_cast<u32>(result.failure), result.message != nullptr ? result.message : "unspecified");
        if (diagnosticsFileOpened)
        {
            diagnostics::Flush(diagnostics::FlushMode::Synchronous);
            diagnostics::CloseFileSink();
        }
        if (diagnosticsOwned) diagnostics::Shutdown();
        return result;
    }
} // namespace vanguard::application
