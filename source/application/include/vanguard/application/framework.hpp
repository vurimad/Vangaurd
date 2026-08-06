#pragma once

#include <vanguard/application/runner.hpp>

namespace vanguard
{
    struct ApplicationTraits
    {
        const char* processName = nullptr;
        application::ApplicationProfile profile = application::ApplicationProfile::Runtime;
        u32 maximumShutdownTicks = 600;
    };

    class Application : public application::IApplicationComposition
    {
    public:
        ~Application() override = default;

        [[nodiscard]] virtual ApplicationTraits GetTraits() const noexcept = 0;

    protected:
        Application() noexcept = default;
    };

    struct FrameworkLaunchParameters
    {
        application::CommandLineView commandLine;
    };

    // Product entry points explicitly provide their native platform host. This keeps platform selection visible at
    // the only layer that knows the native executable ABI while all framework machinery remains portable.
    [[nodiscard]] i32 RunFramework(Application& appInstance,
                                   const FrameworkLaunchParameters& parameters,
                                   application::IPlatformHost& platform) noexcept;
} // namespace vanguard
