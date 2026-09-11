#include <vanguard/filesystem/filesystem.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace vanguard::filesystem
{
    ScanResult ScanFiles(const AbsolutePath& root, containers::DynamicArray<AbsolutePath>& files, const u32 maximumEntries) noexcept
    {
        if (!root.IsDirectoryPath() || root.IsOnlyRootPath() || maximumEntries == 0)
            return ScanResult::InvalidRoot;
#if defined(_WIN32)
        // Also reject links in the configured root's ancestor chain.
        for (AbsolutePath parent = root; !parent.Empty(); parent = paths::ParentAbsolutePath(parent))
        {
            const DWORD attributes = ::GetFileAttributesW(parent.ToUtf16String().AsChar());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return ScanResult::IoFailure;
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return ScanResult::UnsupportedLink;
            if (parent.IsOnlyRootPath())
                break;
        }
        containers::DynamicArray<AbsolutePath> directories(memory::pools::Tools::GetInstance());
        containers::DynamicArray<AbsolutePath> found(memory::pools::Tools::GetInstance());
        directories.PushBack(root);
        if (directories.Size() != 1)
            return ScanResult::LimitExceeded;
        u32 entries = 0;
        for (u32 directoryIndex = 0; directoryIndex < directories.Size(); ++directoryIndex)
        {
            const AbsolutePath directory = directories[directoryIndex];
            const DWORD attributes = ::GetFileAttributesW(directory.ToUtf16String().AsChar());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return ScanResult::IoFailure;
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return ScanResult::UnsupportedLink;
            WIN32_FIND_DATAW data{};
            const auto pattern = directory.AddFilePath("*").ToUtf16String();
            const HANDLE handle = ::FindFirstFileW(pattern.AsChar(), &data);
            if (handle == INVALID_HANDLE_VALUE)
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND)
                {
                    const DWORD current = ::GetFileAttributesW(directory.ToUtf16String().AsChar());
                    if (current == INVALID_FILE_ATTRIBUTES || (current & FILE_ATTRIBUTE_DIRECTORY) == 0)
                        return ScanResult::IoFailure;
                    if ((current & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                        return ScanResult::UnsupportedLink;
                    continue;
                }
                return ScanResult::IoFailure;
            }
            struct SearchGuard
            {
                HANDLE handle;
                ~SearchGuard() { ::FindClose(handle); }
            } guard{handle};
            for (;;)
            {
                const bool dot = data.cFileName[0] == L'.' && (data.cFileName[1] == 0 || (data.cFileName[1] == L'.' && data.cFileName[2] == 0));
                if (!dot)
                {
                    if (++entries > maximumEntries)
                        return ScanResult::LimitExceeded;
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                        return ScanResult::UnsupportedLink;
                    const int length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1, nullptr, 0, nullptr, nullptr);
                    containers::String name;
                    if (length <= 1 || !name.Resize(static_cast<u32>(length)))
                        return ScanResult::LimitExceeded;
                    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1, name.AsChar(), length, nullptr, nullptr) != length)
                        return ScanResult::IoFailure;
                    if (!name.Resize(static_cast<u32>(length - 1)))
                        return ScanResult::LimitExceeded;
                    auto& destination = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? directories : found;
                    const u32 count = destination.Size();
                    destination.PushBack((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? directory.AddDirPath(name) : directory.AddFilePath(name));
                    if (destination.Size() != count + 1u)
                        return ScanResult::LimitExceeded;
                }
                if (::FindNextFileW(handle, &data) == FALSE)
                {
                    if (::GetLastError() != ERROR_NO_MORE_FILES)
                        return ScanResult::IoFailure;
                    break;
                }
            }
        }
        files = static_cast<decltype(found)&&>(found);
        return ScanResult::Success;
#else
        static_cast<void>(files);
        return ScanResult::UnsupportedPlatform;
#endif
    }
} // namespace vanguard::filesystem
