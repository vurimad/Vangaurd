#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <vanguard/filesystem/filesystem_backend.hpp>

namespace vanguard::filesystem
{
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

    bool ReplaceFile(const AbsolutePath& staged, const AbsolutePath& target) noexcept
    {
        if (!IsInitialized() || staged.Empty() || target.Empty() || !staged.IsFilePath() || !target.IsFilePath() || staged == target ||
            paths::ParentAbsolutePath(staged) != paths::ParentAbsolutePath(target) || !GetManager().FileExist(staged))
        {
            return false;
        }
        return SystemIO::MoveFile(staged.AsChar(), target.AsChar());
    }
} // namespace vanguard::filesystem
