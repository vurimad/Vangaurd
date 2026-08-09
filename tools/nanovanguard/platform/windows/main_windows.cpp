#include "nanovanguard.hpp"

#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

#include <Windows.h>
#include <Shellapi.h>

namespace
{
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

        [[nodiscard]] char** Arguments() noexcept
        {
            return static_cast<char**>(argumentPointers.address);
        }
    };

    [[nodiscard]] bool ConvertArguments(wchar_t* const* const source, const int count, Utf8CommandLine& destination) noexcept
    {
        if (source == nullptr || count <= 0)
            return false;
        SIZE_T bytes = 0;
        for (int index = 0; index < count; ++index)
        {
            const int required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source[index], -1, nullptr, 0, nullptr, nullptr);
            if (required <= 0 || bytes > static_cast<SIZE_T>(-1) - static_cast<SIZE_T>(required))
                return false;
            bytes += static_cast<SIZE_T>(required);
        }
        if (static_cast<SIZE_T>(count) > static_cast<SIZE_T>(-1) / sizeof(char*))
            return false;
        const SIZE_T pointerBytes = static_cast<SIZE_T>(count) * sizeof(char*);
        destination.argumentPointers = vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, pointerBytes, alignof(char*));
        destination.argumentBytes = vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, bytes, alignof(char));
        if (!destination.argumentPointers || !destination.argumentBytes)
            return false;
        char** const arguments = destination.Arguments();
        char* const utf8Bytes = static_cast<char*>(destination.argumentBytes.address);
        SIZE_T offset = 0;
        for (int index = 0; index < count; ++index)
        {
            arguments[index] = utf8Bytes + offset;
            if (bytes - offset > static_cast<SIZE_T>(0x7fffffff))
                return false;
            const int written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source[index], -1, utf8Bytes + offset,
                                                      static_cast<int>(bytes - offset), nullptr, nullptr);
            if (written <= 0)
                return false;
            offset += static_cast<SIZE_T>(written);
        }
        destination.argumentCount = count;
        return offset == bytes;
    }

    [[nodiscard]] bool WriteConsole(const char* const text, const vanguard::u32 length, void*) noexcept
    {
        const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE)
            return false;
        vanguard::u32 offset = 0;
        while (offset < length)
        {
            DWORD written = 0;
            if (!::WriteFile(output, text + offset, length - offset, &written, nullptr) || written == 0)
                return false;
            offset += written;
        }
        return true;
    }
} // namespace

int wmain()
{
    if (!vanguard::memory::Initialize() || !vanguard::containers::Initialize())
        return static_cast<int>(vanguard::nanovanguard::ExitCode::InternalFailure);
    int argumentCount = 0;
    wchar_t** const wideArguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (wideArguments == nullptr)
        return static_cast<int>(vanguard::nanovanguard::ExitCode::InternalFailure);
    Utf8CommandLine commandLine;
    const bool converted = ConvertArguments(wideArguments, argumentCount, commandLine);
    static_cast<void>(::LocalFree(wideArguments));
    if (!converted)
        return static_cast<int>(vanguard::nanovanguard::ExitCode::InternalFailure);

    vanguard::nanovanguard::Output output(&WriteConsole);
    vanguard::nanovanguard::CommandRegistry registry;
    if (!vanguard::nanovanguard::RegisterBuiltinCommands(registry))
        return static_cast<int>(vanguard::nanovanguard::ExitCode::InternalFailure);
    return static_cast<int>(registry.Dispatch(commandLine.argumentCount, commandLine.Arguments(), output));
}
