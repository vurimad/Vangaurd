#pragma once

#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::filesystem
{
    enum class DirectoryObservationKind : u8
    {
        Added,
        Modified,
        Removed,
        Renamed,
        Overflow
    };

    [[nodiscard]] inline const char* ToString(const DirectoryObservationKind kind) noexcept
    {
        switch (kind)
        {
        case DirectoryObservationKind::Added: return "Added";
        case DirectoryObservationKind::Modified: return "Modified";
        case DirectoryObservationKind::Removed: return "Removed";
        case DirectoryObservationKind::Renamed: return "Renamed";
        case DirectoryObservationKind::Overflow: return "Overflow";
        }
        return "Unknown";
    }

    // A lossy, untrusted hint. It carries no catalog, build or publication
    // authority and must never be applied as an authored change directly.
    // `path` is empty for Overflow; `root` indexes the watched root set.
    struct DirectoryObservation
    {
        AbsolutePath path;
        u32 root = 0;
        DirectoryObservationKind kind = DirectoryObservationKind::Modified;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return kind == DirectoryObservationKind::Overflow ? path.Empty() : path.IsFilePath();
        }
    };

    using DirectoryObservationCallback = void (*)(const DirectoryObservation& observation, void* userData) noexcept;

    struct DirectoryObserverConfig
    {
        u32 notificationBufferBytes = 64u * 1024u;
        u32 maximumRoots = 64;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return notificationBufferBytes >= 4096u && maximumRoots != 0u;
        }
    };

    // Owner-neutral observation seam for configured source roots.
    //
    // Implementations publish hints from their own thread. `Stop()` must return
    // only after no further callback can run, and must be safe to call when not
    // started. `Start()` must reject an already-started observer and must not
    // invoke the callback synchronously during Start.
    class DirectoryObservationSource
    {
    public:
        virtual ~DirectoryObservationSource() = default;

        [[nodiscard]] virtual bool Start(containers::ArraySpan<const AbsolutePath> roots,
                                         DirectoryObservationCallback callback, void* userData,
                                         const DirectoryObserverConfig& config = {}) noexcept = 0;
        [[nodiscard]] virtual bool Stop() noexcept = 0;
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
    };
} // namespace vanguard::filesystem
