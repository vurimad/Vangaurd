#include <vanguard/filesystem/filesystem.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace vanguard::filesystem
{
    MutationPathResult ValidateMutationRoot(const AbsolutePath& root) noexcept
    {
        if (!root.IsDirectoryPath() || root.IsOnlyRootPath())
            return MutationPathResult::InvalidPath;
#if defined(_WIN32)
        for (AbsolutePath ancestor = root; !ancestor.Empty(); ancestor = paths::ParentAbsolutePath(ancestor))
        {
            const DWORD attributes = ::GetFileAttributesW(ancestor.ToUtf16String().AsChar());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return MutationPathResult::IoFailure;
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return MutationPathResult::UnsupportedLink;
            if (ancestor.IsOnlyRootPath())
                break;
        }
        return MutationPathResult::Success;
#else
        return MutationPathResult::UnsupportedPlatform;
#endif
    }

    MutationPathResult ValidateMutationPath(const AbsolutePath& root, const AbsolutePath& target) noexcept
    {
        if (!root.IsDirectoryPath() || root.IsOnlyRootPath() || !target.IsFilePath() ||
            !target.AsStringView().StartsWithIgnoreCase(root.AsStringView()) || target.AsStringView().Length() <= root.AsStringView().Length())
            return MutationPathResult::InvalidPath;
#if defined(_WIN32)
        const MutationPathResult rootResult = ValidateMutationRoot(root);
        if (rootResult != MutationPathResult::Success)
            return rootResult;
        for (AbsolutePath parent = paths::ParentAbsolutePath(target);
             parent.AsStringView().Length() != root.AsStringView().Length() || !parent.AsStringView().StartsWithIgnoreCase(root.AsStringView());
             parent = paths::ParentAbsolutePath(parent))
        {
            if (parent.Empty() || parent.AsStringView().Length() < root.AsStringView().Length())
                return MutationPathResult::InvalidPath;
            const DWORD attributes = ::GetFileAttributesW(parent.ToUtf16String().AsChar());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                    continue;
                return MutationPathResult::IoFailure;
            }
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return MutationPathResult::IoFailure;
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return MutationPathResult::UnsupportedLink;
        }
        const DWORD targetAttributes = ::GetFileAttributesW(target.ToUtf16String().AsChar());
        const DWORD targetError = targetAttributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_SUCCESS;
        if (targetAttributes == INVALID_FILE_ATTRIBUTES && targetError != ERROR_FILE_NOT_FOUND && targetError != ERROR_PATH_NOT_FOUND)
            return MutationPathResult::IoFailure;
        if (targetAttributes != INVALID_FILE_ATTRIBUTES &&
            ((targetAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || (targetAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0))
            return (targetAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? MutationPathResult::UnsupportedLink : MutationPathResult::IoFailure;
        return MutationPathResult::Success;
#else
        return MutationPathResult::UnsupportedPlatform;
#endif
    }

    namespace
    {
        ScanResult ScanTree(const AbsolutePath& root, containers::DynamicArray<AbsolutePath>& files,
                            containers::DynamicArray<AbsolutePath>* const directoryOutput,
                            const u32 maximumEntries) noexcept
        {
            if (!root.IsDirectoryPath() || root.IsOnlyRootPath() || maximumEntries == 0)
                return ScanResult::InvalidRoot;
#if defined(_WIN32)
            // Also reject links in the configured root's ancestor chain.
            const MutationPathResult rootResult = ValidateMutationRoot(root);
            if (rootResult != MutationPathResult::Success)
                return rootResult == MutationPathResult::UnsupportedLink ? ScanResult::UnsupportedLink : ScanResult::IoFailure;
            containers::DynamicArray<AbsolutePath> pendingDirectories(memory::pools::Tools::GetInstance());
            containers::DynamicArray<AbsolutePath> foundFiles(memory::pools::Tools::GetInstance());
            containers::DynamicArray<AbsolutePath> foundDirectories(memory::pools::Tools::GetInstance());
            pendingDirectories.PushBack(root);
            if (pendingDirectories.Size() != 1)
                return ScanResult::LimitExceeded;
            u32 entries = 0;
            for (u32 directoryIndex = 0; directoryIndex < pendingDirectories.Size(); ++directoryIndex)
            {
                const AbsolutePath directory = pendingDirectories[directoryIndex];
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
                    const bool dot = data.cFileName[0] == L'.' &&
                                     (data.cFileName[1] == 0 || (data.cFileName[1] == L'.' && data.cFileName[2] == 0));
                    if (!dot)
                    {
                        if (++entries > maximumEntries)
                            return ScanResult::LimitExceeded;
                        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                            return ScanResult::UnsupportedLink;
                        const int length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1,
                                                                  nullptr, 0, nullptr, nullptr);
                        containers::String name;
                        if (length <= 1 || !name.Resize(static_cast<u32>(length)))
                            return ScanResult::LimitExceeded;
                        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1,
                                                  name.AsChar(), length, nullptr, nullptr) != length)
                            return ScanResult::IoFailure;
                        if (!name.Resize(static_cast<u32>(length - 1)))
                            return ScanResult::LimitExceeded;
                        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                        {
                            const AbsolutePath found = directory.AddDirPath(name);
                            const u32 pendingCount = pendingDirectories.Size();
                            pendingDirectories.PushBack(found);
                            if (pendingDirectories.Size() != pendingCount + 1u)
                                return ScanResult::LimitExceeded;
                            if (directoryOutput != nullptr)
                            {
                                const u32 foundCount = foundDirectories.Size();
                                foundDirectories.PushBack(found);
                                if (foundDirectories.Size() != foundCount + 1u)
                                    return ScanResult::LimitExceeded;
                            }
                        }
                        else
                        {
                            const u32 foundCount = foundFiles.Size();
                            foundFiles.PushBack(directory.AddFilePath(name));
                            if (foundFiles.Size() != foundCount + 1u)
                                return ScanResult::LimitExceeded;
                        }
                    }
                    if (::FindNextFileW(handle, &data) == FALSE)
                    {
                        if (::GetLastError() != ERROR_NO_MORE_FILES)
                            return ScanResult::IoFailure;
                        break;
                    }
                }
            }
            files = static_cast<decltype(foundFiles)&&>(foundFiles);
            if (directoryOutput != nullptr)
                *directoryOutput = static_cast<decltype(foundDirectories)&&>(foundDirectories);
            return ScanResult::Success;
#else
            static_cast<void>(files);
            static_cast<void>(directoryOutput);
            return ScanResult::UnsupportedPlatform;
#endif
        }
    } // namespace

    ScanResult ScanFiles(const AbsolutePath& root, containers::DynamicArray<AbsolutePath>& files,
                         const u32 maximumEntries) noexcept
    {
        return ScanTree(root, files, nullptr, maximumEntries);
    }

    ScanResult ScanFilesAndDirectories(const AbsolutePath& root, containers::DynamicArray<AbsolutePath>& files,
                                       containers::DynamicArray<AbsolutePath>& directories,
                                       const u32 maximumEntries) noexcept
    {
        return ScanTree(root, files, &directories, maximumEntries);
    }
} // namespace vanguard::filesystem
