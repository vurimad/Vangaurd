#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <vanguard/filesystem/filesystem_backend.hpp>

namespace vanguard::filesystem
{
    namespace
    {
#if defined(RED_PLATFORM_WINPC)
        [[nodiscard]] bool MakeSystemPath(const AbsolutePath& path, UniChar (&systemPath)[::io::REDIO_MAX_PATH_LENGTH]) noexcept
        {
            return !path.Empty() && path.IsFilePath() && red::EngineStringToFileSystemString(path.AsChar(), systemPath, ::io::REDIO_MAX_PATH_LENGTH);
        }
#endif
    } // namespace

    bool Initialize(const Config& config) noexcept
    {
        if (!vanguard::memory::IsInitialized() || !vanguard::diagnostics::IsInitialized() || !vanguard::containers::IsInitialized() ||
            !vanguard::io::IsInitialized())
        {
            return false;
        }

        return backend::Initialize(config);
    }

    void Shutdown() noexcept
    {
        backend::Shutdown();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    Manager& GetManager() noexcept
    {
        return backend::GetManager();
    }

    bool ReplaceFile(Manager& manager, const AbsolutePath& staged, const AbsolutePath& target) noexcept
    {
        if (staged.Empty() || target.Empty() || !staged.IsFilePath() || !target.IsFilePath() || staged == target ||
            paths::ParentAbsolutePath(staged) != paths::ParentAbsolutePath(target) || !manager.FileExist(staged))
        {
            return false;
        }
        return SystemIO::MoveFile(staged.AsChar(), target.AsChar());
    }

    bool ReplaceFile(const AbsolutePath& staged, const AbsolutePath& target) noexcept
    {
        return IsInitialized() && ReplaceFile(GetManager(), staged, target);
    }

    PublishNewFileResult PublishNewFile(Manager& manager, const AbsolutePath& staged, const AbsolutePath& target) noexcept
    {
        if (staged.Empty() || target.Empty() || !staged.IsFilePath() || !target.IsFilePath() || staged == target ||
            paths::ParentAbsolutePath(staged) != paths::ParentAbsolutePath(target) || !manager.FileExist(staged))
            return PublishNewFileResult::InvalidArgument;
#if defined(RED_PLATFORM_WINPC)
        UniChar stagedSystem[::io::REDIO_MAX_PATH_LENGTH]{};
        UniChar targetSystem[::io::REDIO_MAX_PATH_LENGTH]{};
        if (!MakeSystemPath(staged, stagedSystem) || !MakeSystemPath(target, targetSystem))
            return PublishNewFileResult::InvalidArgument;
        if (::MoveFileExW(stagedSystem, targetSystem, MOVEFILE_WRITE_THROUGH) != FALSE)
            return PublishNewFileResult::Success;
        const DWORD error = ::GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                   ? PublishNewFileResult::AlreadyExists
                   : PublishNewFileResult::IoFailure;
#else
        static_cast<void>(manager);
        return PublishNewFileResult::UnsupportedPlatform;
#endif
    }

    PublishNewFileResult PublishNewFile(const AbsolutePath& staged, const AbsolutePath& target) noexcept
    {
        return IsInitialized() ? PublishNewFile(GetManager(), staged, target) : PublishNewFileResult::InvalidArgument;
    }

    ExclusiveFileWriteResult WriteExclusiveFile(const AbsolutePath& path, const void* const bytes, const usize byteCount) noexcept
    {
        if (path.Empty() || !path.IsFilePath() || (byteCount != 0 && bytes == nullptr))
            return ExclusiveFileWriteResult::OpenFailed;
#if defined(RED_PLATFORM_WINPC)
        UniChar systemPath[::io::REDIO_MAX_PATH_LENGTH]{};
        if (!MakeSystemPath(path, systemPath))
            return ExclusiveFileWriteResult::OpenFailed;

        const HANDLE file = ::CreateFileW(systemPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = ::GetLastError();
            return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                ? ExclusiveFileWriteResult::AlreadyExists
                : ExclusiveFileWriteResult::OpenFailed;
        }

        const auto* cursor = static_cast<const u8*>(bytes);
        usize remaining = byteCount;
        while (remaining != 0)
        {
            const DWORD batch = remaining > static_cast<usize>(0xffffffffu) ? 0xffffffffu : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (::WriteFile(file, cursor, batch, &written, nullptr) == FALSE || written != batch)
            {
                static_cast<void>(::CloseHandle(file));
                return ExclusiveFileWriteResult::WriteFailed;
            }
            cursor += written;
            remaining -= written;
        }

        if (::FlushFileBuffers(file) == FALSE)
        {
            static_cast<void>(::CloseHandle(file));
            return ExclusiveFileWriteResult::FlushFailed;
        }
        if (::CloseHandle(file) == FALSE)
            return ExclusiveFileWriteResult::FlushFailed;
        return ExclusiveFileWriteResult::Success;
#else
        static_cast<void>(bytes);
        static_cast<void>(byteCount);
        return ExclusiveFileWriteResult::UnsupportedPlatform;
#endif
    }

    CompleteFileReadResult ReadCompleteFile(const AbsolutePath& path, void* const destination, const usize capacity, usize& byteCount) noexcept
    {
        byteCount = 0;
        if (path.Empty() || !path.IsFilePath() || (capacity != 0 && destination == nullptr))
            return CompleteFileReadResult::OpenFailed;
#if defined(RED_PLATFORM_WINPC)
        UniChar systemPath[::io::REDIO_MAX_PATH_LENGTH]{};
        if (!MakeSystemPath(path, systemPath))
            return CompleteFileReadResult::OpenFailed;

        const HANDLE file = ::CreateFileW(systemPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = ::GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                ? CompleteFileReadResult::Missing
                : CompleteFileReadResult::OpenFailed;
        }

        LARGE_INTEGER size{};
        if (::GetFileSizeEx(file, &size) == FALSE || size.QuadPart < 0)
        {
            static_cast<void>(::CloseHandle(file));
            return CompleteFileReadResult::ReadFailed;
        }
        const u64 fileSize = static_cast<u64>(size.QuadPart);
        if (fileSize > static_cast<u64>(capacity))
        {
            static_cast<void>(::CloseHandle(file));
            return CompleteFileReadResult::LimitExceeded;
        }

        auto* cursor = static_cast<u8*>(destination);
        u64 remaining = fileSize;
        while (remaining != 0)
        {
            const DWORD batch = remaining > 0xffffffffull ? 0xffffffffu : static_cast<DWORD>(remaining);
            DWORD read = 0;
            if (::ReadFile(file, cursor, batch, &read, nullptr) == FALSE || read != batch)
            {
                static_cast<void>(::CloseHandle(file));
                return CompleteFileReadResult::ReadFailed;
            }
            cursor += read;
            remaining -= read;
        }
        if (::CloseHandle(file) == FALSE)
            return CompleteFileReadResult::ReadFailed;
        byteCount = static_cast<usize>(fileSize);
        return CompleteFileReadResult::Success;
#else
        static_cast<void>(destination);
        static_cast<void>(capacity);
        return CompleteFileReadResult::UnsupportedPlatform;
#endif
    }
} // namespace vanguard::filesystem
