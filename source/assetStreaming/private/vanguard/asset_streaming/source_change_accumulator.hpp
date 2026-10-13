#pragma once

#include <vanguard/asset_streaming/project_asset_observation.hpp>
#include <vanguard/assets/asset_file_transaction.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/filesystem/directory_observation.hpp>

namespace vanguard::asset_streaming
{
    struct SourceChangeAdvance
    {
        bool reconcile = false;
        bool overflow = false;
        bool hasPending = false;
        u32 readyPaths = 0;
        // Snapshot of all observations represented by this reconciliation.
        // The owner must pass it back when acknowledging completion so a newer
        // overflow cannot be cleared by an older successful rescan.
        u64 generation = 0;
    };

    struct SourceChangeSnapshot
    {
        u64 generation = 0;
        bool overflow = false;
    };

    using StableSourceChangeCommitFunction = bool (*)(void* userData) noexcept;

    // Owner-thread accumulator fed by DirectoryObservationSource callbacks.
    //
    // Publish() is the only callback entry point and is thread-safe. It never
    // mutates the source catalog, publications or the build graph; it only
    // records coalesced path hints and explicit rescan-required state. Only
    // Advance/RequeueReady/AcknowledgeReconcile are called on the owner thread.
    class SourceChangeAccumulator final
    {
    public:
        SourceChangeAccumulator() noexcept;
        SourceChangeAccumulator(const SourceChangeAccumulator&) = delete;
        SourceChangeAccumulator& operator=(const SourceChangeAccumulator&) = delete;

        [[nodiscard]] bool Configure(const ProjectAssetObservationConfig& config, containers::ArraySpan<const assets::SourceRoot> roots,
                                     const filesystem::AbsolutePath& journalPath) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool Publish(const filesystem::DirectoryObservation& observation) noexcept;

        [[nodiscard]] SourceChangeAdvance Advance() noexcept;
        // Return promoted paths to the pending set after a Busy or failed
        // reconciliation so no observed change is lost.
        void RequeueReady() noexcept;
        // Confirm a successful reconciliation through the exact observation
        // generation represented by the completed work.
        void AcknowledgeReconcile(u64 generation) noexcept;

        // Cheap owner-thread completion predicate used by TickObservation.
        // Unlike GetStatus(), this never performs telemetry traversal.
        [[nodiscard]] bool HasRescanRequired() const noexcept;
        // Cheap thread-safe fence used by E2.3 P4 around direct pre-commit
        // freshness validation. Hints are not freshness authority; a changed
        // generation or overflow only forces a conservative stale rejection.
        [[nodiscard]] SourceChangeSnapshot GetChangeSnapshot() const noexcept;
        // Executes only the final already-prepared commit while holding the same
        // mutex used by Publish(). This closes the tracked-callback gap between a
        // final generation/overflow check and visibility/source-transaction
        // linearization. Expensive resolve/planning/staging/prepare work must be
        // completed before entering this seam. Lock ordering is accumulator ->
        // downstream owner.
        [[nodiscard]] bool CommitIfUnchanged(SourceChangeSnapshot expected, StableSourceChangeCommitFunction commit,
                                             void* userData) const noexcept;
        // Variant for a source transaction that has already applied its owned
        // authored paths before the short durable publication step. Directory
        // observation can report those exact paths as changed by the transaction
        // itself. Such hints may cross this fence, but every newer non-owned path,
        // missing hint provenance, or overflow remains a stale rejection.
        //
        // On success, accepted receives the exact observation generation covered
        // by the publication. A subsequent authoritative Rescan() may acknowledge
        // through that generation without hiding newer callbacks.
        [[nodiscard]] bool CommitIfUnchangedOrOwnedChanges(
            SourceChangeSnapshot expected, containers::ArraySpan<const filesystem::AbsolutePath> ownedPaths,
            StableSourceChangeCommitFunction commit, void* userData, SourceChangeSnapshot& accepted) const noexcept;
        // Called only after a full authoritative source rescan. Pending/ready
        // hints through generation are then represented by that rescan; newer
        // hints remain active.
        void AcknowledgeAuthoritativeRescan(u64 generation) noexcept;
        [[nodiscard]] ProjectAssetObservationStatus GetStatus() const noexcept;

    private:
        struct PendingPath
        {
            filesystem::AbsolutePath path;
            u32 root = 0;
            u32 firstSeenTick = 0;
            u32 lastSeenTick = 0;
            u64 generation = 0;
        };

        [[nodiscard]] bool IsTransactionArtifact(const filesystem::AbsolutePath& path) const noexcept;
        [[nodiscard]] i32 FindRoot(const filesystem::AbsolutePath& path) const noexcept;
        [[nodiscard]] bool RemovePendingAt(u32 index) noexcept;

        mutable concurrency::Mutex m_mutex;
        ProjectAssetObservationConfig m_config;
        containers::DynamicArray<filesystem::AbsolutePath> m_roots;
        filesystem::AbsolutePath m_journalPath;
        filesystem::AbsolutePath m_journalStagePath;
        containers::DynamicArray<PendingPath> m_pending;
        containers::HashMap<containers::String, u32> m_index;
        containers::DynamicArray<PendingPath> m_ready;
        u32 m_ticks = 0;
        u32 m_scanCursor = 0;
        u32 m_activePendingPaths = 0;
        bool m_overflow = false;
        bool m_rootCaptureIncomplete = false;
        bool m_configured = false;
        u64 m_changeGeneration = 0;
        u64 m_overflowGeneration = 0;
        // Physical pending entries at or below this generation are already
        // represented by a completed/active authoritative rescan. They are
        // reclaimed incrementally under maximumPathsPerTick rather than bulk
        // cleared on the owner tick.
        u64 m_discardThroughGeneration = 0;
        u64 m_hintsReceived = 0;
        u64 m_hintsCoalesced = 0;
        u64 m_hintsSuppressed = 0;
        u64 m_hintsRejected = 0;
        u64 m_rescansRequested = 0;
        u64 m_rescansCompleted = 0;
    };
} // namespace vanguard::asset_streaming
