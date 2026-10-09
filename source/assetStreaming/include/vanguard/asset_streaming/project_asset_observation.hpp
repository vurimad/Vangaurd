#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::asset_streaming
{
    // E2.1 owner-thread observation policy. All bounds are expressed in owner
    // ticks, not wall-clock time, so behavior is deterministic.
    struct ProjectAssetObservationConfig
    {
        // Hard cap on distinct paths tracked at once. Exceeding it degrades to
        // an authoritative full rescan instead of losing a possible change.
        u32 maximumPendingPaths = 4096;
        // Maximum paths promoted from the accumulator into one reconciliation.
        u32 maximumPathsPerTick = 64;
        // Maximum publication/reconciliation records inspected on the owner
        // thread per tick after authoritative background inventory completes.
        u32 maximumReconcileOperationsPerTick = 64;
        // A path must be quiet for this many ticks before it is promoted.
        u32 settleTicks = 2;
        // A path is forced due after this many ticks since it was first seen, so
        // a continuous burst or partial write cannot defer forever.
        u32 maximumDeferralTicks = 8;
        u32 notificationBufferBytes = 64u * 1024u;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumPendingPaths != 0u && maximumPathsPerTick != 0u && maximumReconcileOperationsPerTick != 0u && settleTicks != 0u &&
                   maximumDeferralTicks >= settleTicks && notificationBufferBytes >= 4096u;
        }
    };

    // Queryable observation state for later headless and editor consumers. This
    // is reporting only; it carries no catalog or build authority.
    struct ProjectAssetObservationStatus
    {
        bool running = false;
        bool overflow = false;
        bool rescanRequired = false;
        bool reconcilePending = false;
        u32 watchedRoots = 0;
        u32 pendingPaths = 0;
        u32 deferredPaths = 0;
        u32 readyPaths = 0;
        // Budgeted owner-thread reconciliation records completed by the current
        // observation pass. This is reset when a pass completes.
        u32 reconcileOperationsCompleted = 0;
        u64 ticks = 0;
        u64 hintsReceived = 0;
        u64 hintsCoalesced = 0;
        u64 hintsSuppressed = 0;
        u64 hintsRejected = 0;
        u64 rescansRequested = 0;
        u64 rescansCompleted = 0;
    };
} // namespace vanguard::asset_streaming
