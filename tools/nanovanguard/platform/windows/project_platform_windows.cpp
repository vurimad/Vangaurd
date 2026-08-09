#include "../../src/project_platform.hpp"

#include <vanguard/memory/memory.hpp>

#include <Windows.h>
#include <bcrypt.h>

namespace
{
    namespace containers = vanguard::containers;
    namespace memory = vanguard::memory;

    class WideBuffer final
    {
    public:
        WideBuffer() noexcept : m_characters(memory::pools::Tools::GetInstance()) {}

        [[nodiscard]] bool FromUtf8(const containers::StringView text) noexcept
        {
            if (text.Empty() || text.Length() > static_cast<vanguard::u32>(INT_MAX))
                return false;
            const int required =
                ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Length()), nullptr, 0);
            if (required <= 0)
                return false;
            m_characters.Resize(static_cast<vanguard::u32>(required) + 1);
            const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Length()),
                                                      static_cast<wchar_t*>(m_characters.Data()), required);
            if (written != required)
                return false;
            m_characters[required] = L'\0';
            return true;
        }

        [[nodiscard]] wchar_t* Data() noexcept
        {
            return static_cast<wchar_t*>(m_characters.Data());
        }
        [[nodiscard]] const wchar_t* Data() const noexcept
        {
            return static_cast<const wchar_t*>(m_characters.Data());
        }

    private:
        containers::DynamicArray<wchar_t> m_characters;
    };

    [[nodiscard]] bool ToUtf8(const wchar_t* const text, containers::String& output) noexcept
    {
        if (text == nullptr)
            return false;
        int wideLength = 0;
        while (text[wideLength] != L'\0')
            ++wideLength;
        if (wideLength == 0)
        {
            output.Clear();
            return true;
        }
        const int required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, wideLength, nullptr, 0, nullptr, nullptr);
        if (required <= 0 || !output.Resize(static_cast<vanguard::u32>(required)))
            return false;
        return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, wideLength, output.AsChar(), required, nullptr, nullptr) ==
               required;
    }

    [[nodiscard]] bool Join(const containers::StringView directory, const containers::StringView leaf, containers::String& output) noexcept
    {
        output.Clear();
        output.Append(directory);
        if (!output.Empty() && output.Back() != '/' && output.Back() != '\\')
            output.Append('\\');
        output.Append(leaf);
        return true;
    }
} // namespace

namespace vanguard::nanovanguard::platform
{
    bool MakeAbsolutePath(const containers::StringView path, containers::String& absolutePath) noexcept
    {
        WideBuffer source;
        if (!source.FromUtf8(path))
            return false;
        const DWORD required = ::GetFullPathNameW(source.Data(), 0, nullptr, nullptr);
        if (required == 0 || required > 32767)
            return false;
        containers::DynamicArray<wchar_t> resolved(memory::pools::Tools::GetInstance());
        resolved.Resize(required);
        const DWORD written = ::GetFullPathNameW(source.Data(), required, static_cast<wchar_t*>(resolved.Data()), nullptr);
        if (written == 0 || written >= required)
            return false;
        while (written > 3 && (resolved[written - 1] == L'\\' || resolved[written - 1] == L'/'))
            resolved[written - 1] = L'\0';
        return ToUtf8(static_cast<const wchar_t*>(resolved.Data()), absolutePath);
    }

    PathKind GetPathKind(const containers::StringView absolutePath) noexcept
    {
        WideBuffer path;
        if (!path.FromUtf8(absolutePath))
            return PathKind::Failure;
        const DWORD attributes = ::GetFileAttributesW(path.Data());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return ::GetLastError() == ERROR_FILE_NOT_FOUND || ::GetLastError() == ERROR_PATH_NOT_FOUND ? PathKind::Missing
                                                                                                        : PathKind::Failure;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return PathKind::Directory;
        if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0)
            return PathKind::Other;
        return PathKind::File;
    }

    bool CreateDirectory(const containers::StringView absolutePath) noexcept
    {
        WideBuffer path;
        return path.FromUtf8(absolutePath) && ::CreateDirectoryW(path.Data(), nullptr) != FALSE;
    }

    bool RemoveEmptyDirectory(const containers::StringView absolutePath) noexcept
    {
        WideBuffer path;
        return path.FromUtf8(absolutePath) && ::RemoveDirectoryW(path.Data()) != FALSE;
    }

    bool DeleteFile(const containers::StringView absolutePath) noexcept
    {
        WideBuffer path;
        return path.FromUtf8(absolutePath) && ::DeleteFileW(path.Data()) != FALSE;
    }

    bool PublishDirectory(const containers::StringView stagingPath, const containers::StringView destinationPath) noexcept
    {
        WideBuffer staging;
        WideBuffer destination;
        return staging.FromUtf8(stagingPath) && destination.FromUtf8(destinationPath) &&
               ::MoveFileExW(staging.Data(), destination.Data(), MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    bool ReadFile(const containers::StringView absolutePath, containers::String& contents) noexcept
    {
        WideBuffer path;
        if (!path.FromUtf8(absolutePath))
            return false;
        const HANDLE file = ::CreateFileW(path.Data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        bool success = ::GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 && size.QuadPart <= static_cast<LONGLONG>(UINT_MAX) &&
                       contents.Resize(static_cast<u32>(size.QuadPart));
        u32 offset = 0;
        while (success && offset < contents.Length())
        {
            DWORD read = 0;
            const DWORD requested = contents.Length() - offset;
            success = ::ReadFile(file, contents.AsChar() + offset, requested, &read, nullptr) != FALSE && read != 0;
            offset += read;
        }
        success = success && offset == contents.Length();
        static_cast<void>(::CloseHandle(file));
        return success;
    }

    bool WriteFileDurable(const containers::StringView absolutePath, const containers::ArraySpan<const u8> contents) noexcept
    {
        WideBuffer path;
        if (!path.FromUtf8(absolutePath))
            return false;
        const HANDLE file =
            ::CreateFileW(path.Data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        u32 offset = 0;
        bool success = true;
        while (offset < contents.Size())
        {
            DWORD written = 0;
            success = ::WriteFile(file, contents.Data() + offset, contents.Size() - offset, &written, nullptr) != FALSE && written != 0;
            if (!success)
                break;
            offset += written;
        }
        success = success && offset == contents.Size() && ::FlushFileBuffers(file) != FALSE;
        static_cast<void>(::CloseHandle(file));
        return success;
    }

    bool FindProjectFile(const containers::StringView directory, containers::String& projectFile) noexcept
    {
        containers::String pattern;
        static_cast<void>(Join(directory, "*.vproject", pattern));
        WideBuffer widePattern;
        if (!widePattern.FromUtf8(pattern))
            return false;
        WIN32_FIND_DATAW data{};
        const HANDLE search = ::FindFirstFileW(widePattern.Data(), &data);
        if (search == INVALID_HANDLE_VALUE)
            return false;
        containers::String foundName;
        u32 count = 0;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            if (++count > 1)
                break;
            if (!ToUtf8(data.cFileName, foundName))
            {
                count = 2;
                break;
            }
        } while (::FindNextFileW(search, &data) != FALSE);
        static_cast<void>(::FindClose(search));
        return count == 1 && Join(directory, foundName, projectFile);
    }

    bool GenerateRandomBytes(const containers::ArraySpan<u8> bytes) noexcept
    {
        return !bytes.Empty() && ::BCryptGenRandom(nullptr, bytes.Data(), bytes.Size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
    }

    u32 GetLastErrorCode() noexcept
    {
        return static_cast<u32>(::GetLastError());
    }
} // namespace vanguard::nanovanguard::platform
