#include <vanguard/application/framework.hpp>

namespace vanguard
{
    i32 RunFramework(Application& appInstance, const FrameworkLaunchParameters& parameters, application::IPlatformHost& platform) noexcept
    {
        const ApplicationTraits traits = appInstance.GetTraits();
        application::RunnerSettings settings;
        settings.applicationName = traits.processName;
        settings.profile = traits.profile;
        settings.commandLine = parameters.commandLine;
        settings.maximumShutdownTicks = traits.maximumShutdownTicks;
        settings.diagnosticsFilePath = traits.diagnosticsFilePath;

        application::ApplicationRunner runner;
        const application::RunnerResult result = runner.Run(platform, appInstance, settings);
        if (result)
            return result.exitCode;
        if (result.exitCode != 0)
            return result.exitCode;
        return -static_cast<i32>(result.failure);
    }
} // namespace vanguard
