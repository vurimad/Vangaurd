#pragma once

#include <vanguard/filesystem/directory_observation.hpp>

namespace vanguard::asset_streaming
{
    // Windows platform backend for configured source roots.
    //
    // One overlapped ReadDirectoryChangesW worker is created per root. Hints are
    // published from those worker threads. A root that is temporarily missing or
    // loses its watch emits Overflow and is retried until it recovers or Stop()
    // is requested. Stop() signals, cancels and joins every worker before
    // returning, so no callback can run afterwards.
    //
    // On platforms without an implementation Start() returns false; callers may
    // still use an injected DirectoryObservationSource.
    class PlatformDirectoryObserver final : public filesystem::DirectoryObservationSource
    {
    public:
        PlatformDirectoryObserver() noexcept = default;
        ~PlatformDirectoryObserver() override;

        PlatformDirectoryObserver(const PlatformDirectoryObserver&) = delete;
        PlatformDirectoryObserver& operator=(const PlatformDirectoryObserver&) = delete;

        [[nodiscard]] bool Start(containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                 filesystem::DirectoryObservationCallback callback, void* userData,
                                 const filesystem::DirectoryObserverConfig& config) noexcept override;
        [[nodiscard]] bool Stop() noexcept override;
        [[nodiscard]] bool IsRunning() const noexcept override;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::asset_streaming
