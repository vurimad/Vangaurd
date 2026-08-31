#pragma once

#include <vanguard/application/service.hpp>

namespace vanguard::input
{
    class IInputBackend;
}
namespace vanguard::window
{
    class IWindowBackend;
    class IWindowEventSink;
} // namespace vanguard::window

namespace vanguard::application
{
    struct CommandLineView
    {
        i32 argumentCount = 0;
        const char* const* arguments = nullptr;

        [[nodiscard]] const char* operator[](const i32 index) const noexcept
        {
            return index >= 0 && index < argumentCount && arguments != nullptr ? arguments[index] : nullptr;
        }

        [[nodiscard]] bool HasArgument(const char* const expected) const noexcept
        {
            if (expected == nullptr)
                return false;
            for (i32 index = 0; index < argumentCount; ++index)
            {
                const char* argument = (*this)[index];
                if (argument == nullptr)
                    continue;
                const char* left = argument;
                const char* right = expected;
                while (*left != '\0' && *left == *right)
                {
                    ++left;
                    ++right;
                }
                if (*left == '\0' && *right == '\0')
                    return true;
            }
            return false;
        }
    };

    struct PlatformStartupInfo
    {
        const char* applicationName = nullptr;
        ApplicationProfile profile = ApplicationProfile::None;
        CommandLineView commandLine;
    };

    enum class PlatformResultCode : u8
    {
        Success,
        Failure
    };

    struct PlatformStatus
    {
        PlatformResultCode result = PlatformResultCode::Success;
        const char* message = nullptr;

        [[nodiscard]] static constexpr PlatformStatus Success() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr PlatformStatus Failure(const char* const message) noexcept
        {
            return {PlatformResultCode::Failure, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return result == PlatformResultCode::Success;
        }
    };

    enum class PlatformPumpAction : u8
    {
        Continue,
        ExitRequested,
        Failure
    };

    struct PlatformPumpResult
    {
        PlatformPumpAction action = PlatformPumpAction::Continue;
        i32 exitCode = 0;
        const char* message = nullptr;
    };

    class IPlatformHost
    {
    public:
        virtual ~IPlatformHost() = default;

        IPlatformHost(const IPlatformHost&) = delete;
        IPlatformHost& operator=(const IPlatformHost&) = delete;

        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
        [[nodiscard]] virtual PlatformStatus Initialize(const PlatformStartupInfo& startup) noexcept = 0;
        [[nodiscard]] virtual PlatformPumpResult PumpEvents() noexcept = 0;
        [[nodiscard]] virtual input::IInputBackend* GetInputBackend() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual window::IWindowBackend* GetWindowBackend() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual bool AttachWindowEventSink(window::IWindowEventSink*) noexcept
        {
            return false;
        }
        virtual void DetachWindowEventSink(window::IWindowEventSink*) noexcept {}
        virtual void Shutdown() noexcept = 0;

    protected:
        IPlatformHost() noexcept = default;
    };
} // namespace vanguard::application
