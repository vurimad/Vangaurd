#include <vanguard/platform/windows/windows_framework.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/platform/windows/windows_platform_host.hpp>

#include <Windows.h>
#include <Shellapi.h>

namespace
{
    constexpr vanguard::i32 CommandLineFailure = -100;
    constexpr vanguard::i32 CommandLineEncodingFailure = -101;
    constexpr vanguard::i32 CommandLineStorageFailure = -102;
    constexpr vanguard::i32 MemoryBootstrapFailure = -103;

    struct Utf8CommandLine final
    {
        vanguard::memory::MemoryBlock argumentPointers;
        vanguard::memory::MemoryBlock argumentBytes;
        int argumentCount = 0;

        ~Utf8CommandLine()
        {
            vanguard::memory::Free(argumentBytes);
            vanguard::memory::Free(argumentPointers);
        }

        Utf8CommandLine() noexcept = default;
        Utf8CommandLine(const Utf8CommandLine&) = delete;
        Utf8CommandLine& operator=(const Utf8CommandLine&) = delete;

        [[nodiscard]] const char* const* Arguments() const noexcept
        {
            return static_cast<const char* const*>(argumentPointers.address);
        }
    };

    [[nodiscard]] bool ConvertCommandLine(wchar_t* const* wideArguments, const int argumentCount,
                                          Utf8CommandLine& output) noexcept
    {
        if (wideArguments == nullptr || argumentCount <= 0) return false;

        vanguard::usize byteCount = 0;
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            const int requiredBytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[argumentIndex],
                                                            -1, nullptr, 0, nullptr, nullptr);
            if (requiredBytes <= 0 || byteCount > static_cast<vanguard::usize>(-1) -
                                                     static_cast<vanguard::usize>(requiredBytes))
                return false;
            byteCount += static_cast<vanguard::usize>(requiredBytes);
        }

        const vanguard::usize pointerCount = static_cast<vanguard::usize>(argumentCount);
        if (pointerCount > static_cast<vanguard::usize>(-1) / sizeof(const char*)) return false;
        output.argumentPointers = vanguard::memory::Allocate(vanguard::memory::PoolId::Runtime,
                                                             pointerCount * sizeof(const char*), alignof(const char*));
        output.argumentBytes = vanguard::memory::Allocate(vanguard::memory::PoolId::Runtime, byteCount, alignof(char));
        if (!output.argumentPointers || !output.argumentBytes) return false;

        auto** arguments = static_cast<const char**>(output.argumentPointers.address);
        auto* destination = static_cast<char*>(output.argumentBytes.address);
        vanguard::usize destinationOffset = 0;
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            arguments[argumentIndex] = destination + destinationOffset;
            const vanguard::usize remainingBytes = byteCount - destinationOffset;
            if (remainingBytes > static_cast<vanguard::usize>(0x7fffffff)) return false;
            const int writtenBytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[argumentIndex],
                                                           -1, destination + destinationOffset,
                                                           static_cast<int>(remainingBytes), nullptr, nullptr);
            if (writtenBytes <= 0) return false;
            destinationOffset += static_cast<vanguard::usize>(writtenBytes);
        }
        output.argumentCount = argumentCount;
        return destinationOffset == byteCount;
    }
}

namespace vanguard::platform::windows
{
    i32 RunFramework(Application& appInstance) noexcept
    {
        int argumentCount = 0;
        wchar_t** const wideArguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
        if (wideArguments == nullptr || argumentCount <= 0)
        {
            if (wideArguments != nullptr) static_cast<void>(::LocalFree(wideArguments));
            return CommandLineFailure;
        }
        if (!memory::IsInitialized() && !memory::Initialize())
        {
            static_cast<void>(::LocalFree(wideArguments));
            return MemoryBootstrapFailure;
        }

        Utf8CommandLine commandLine;
        const bool converted = ConvertCommandLine(wideArguments, argumentCount, commandLine);
        static_cast<void>(::LocalFree(wideArguments));
        if (!converted)
            return commandLine.argumentPointers && commandLine.argumentBytes ? CommandLineEncodingFailure
                                                                             : CommandLineStorageFailure;

        FrameworkLaunchParameters parameters;
        parameters.commandLine = {commandLine.argumentCount, commandLine.Arguments()};
        WindowsPlatformHost platform;
        return vanguard::RunFramework(appInstance, parameters, platform);
    }
} // namespace vanguard::platform::windows
