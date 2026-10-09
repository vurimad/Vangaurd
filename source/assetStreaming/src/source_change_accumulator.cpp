#include <vanguard/asset_streaming/source_change_accumulator.hpp>

#include <vanguard/memory/pool.hpp>

namespace vanguard::asset_streaming
{
    namespace
    {
        [[nodiscard]] containers::String PathKey(const filesystem::AbsolutePath& path) noexcept
        {
            const containers::StringView view = path.AsStringView();
            containers::String key(view.Data(), view.Length());
            for (u32 index = 0; index < key.Length(); ++index)
            {
                char& character = key[index];
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
            }
            return key;
        }
    } // namespace

    SourceChangeAccumulator::SourceChangeAccumulator() noexcept
        : m_roots(memory::pools::Assets::GetInstance()), m_pending(memory::pools::Assets::GetInstance()),
          m_index(memory::pools::Assets::GetInstance()), m_ready(memory::pools::Assets::GetInstance())
    {
    }

    bool SourceChangeAccumulator::Configure(const ProjectAssetObservationConfig& config,
                                            const containers::ArraySpan<const assets::SourceRoot> roots,
                                            const filesystem::AbsolutePath& journalPath) noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        m_config = config;
        m_roots.Clear();
        m_roots.Reserve(roots.Count());
        for (const assets::SourceRoot& root : roots)
            m_roots.PushBack(root.directory);
        // Observation cannot safely start with a partial root map: a rescan can
        // repair catalog state, but it cannot reconstruct the missing callback
        // routing. Fail atomically and let the caller retry configuration.
        m_rootCaptureIncomplete = m_roots.Size() != roots.Count();
        if (m_rootCaptureIncomplete)
        {
            m_roots.Clear();
            m_configured = false;
            m_overflow = false;
            return false;
        }
        m_changeGeneration = 0u;
        m_overflowGeneration = 0u;
        m_overflow = false;
        m_journalPath = journalPath;
        m_journalStagePath = {};
        if (!journalPath.Empty())
        {
            const containers::StringView view = journalPath.AsStringView();
            containers::String stage(view.Data(), view.Length());
            stage.Append(".stage", 6);
            m_journalStagePath = filesystem::AbsolutePath::CreateFilePath(containers::StringView(stage.AsChar(), stage.Length()));
        }
        m_pending.Clear();
        m_index.Clear();
        m_ready.Clear();
        m_pending.Reserve(config.maximumPendingPaths);
        m_index.Reserve(config.maximumPendingPaths);
        m_ready.Reserve(config.maximumPathsPerTick);
        if (m_pending.Capacity() < config.maximumPendingPaths || m_index.Capacity() < config.maximumPendingPaths ||
            m_ready.Capacity() < config.maximumPathsPerTick)
        {
            m_roots.Clear();
            m_pending.Clear();
            m_index.Clear();
            m_ready.Clear();
            m_configured = false;
            return false;
        }
        m_ticks = 0;
        m_scanCursor = 0;
        m_activePendingPaths = 0;
        m_configured = true;
        m_discardThroughGeneration = 0;
        m_hintsReceived = 0;
        m_hintsCoalesced = 0;
        m_hintsSuppressed = 0;
        m_hintsRejected = 0;
        m_rescansRequested = 0;
        m_rescansCompleted = 0;
        return true;
    }

    void SourceChangeAccumulator::Reset() noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        m_pending.Clear();
        m_index.Clear();
        m_ready.Clear();
        m_roots.Clear();
        m_journalPath = {};
        m_journalStagePath = {};
        m_ticks = 0;
        m_scanCursor = 0;
        m_activePendingPaths = 0;
        m_overflow = false;
        m_rootCaptureIncomplete = false;
        m_configured = false;
        m_changeGeneration = 0;
        m_overflowGeneration = 0;
        m_discardThroughGeneration = 0;
        m_hintsReceived = 0;
        m_hintsCoalesced = 0;
        m_hintsSuppressed = 0;
        m_hintsRejected = 0;
        m_rescansRequested = 0;
        m_rescansCompleted = 0;
    }

    bool SourceChangeAccumulator::IsTransactionArtifact(const filesystem::AbsolutePath& path) const noexcept
    {
        const containers::StringView name = filesystem::paths::GetFileName(path);
        if (name.EndsWithIgnoreCase(assets::AssetTransactionStageSuffix) ||
            name.EndsWithIgnoreCase(assets::AssetTransactionBackupSuffix))
            return true;
        if (!m_journalPath.Empty() && path == m_journalPath)
            return true;
        return !m_journalStagePath.Empty() && path == m_journalStagePath;
    }

    i32 SourceChangeAccumulator::FindRoot(const filesystem::AbsolutePath& path) const noexcept
    {
        const containers::StringView view = path.AsStringView();
        for (u32 index = 0; index < m_roots.Size(); ++index)
        {
            const containers::StringView root = m_roots[index].AsStringView();
            if (view.Length() > root.Length() && view.StartsWithIgnoreCase(root))
                return static_cast<i32>(index);
        }
        return -1;
    }

    bool SourceChangeAccumulator::Publish(const filesystem::DirectoryObservation& observation) noexcept
    {
        if (!observation.IsValid())
            return false;
        if (observation.kind == filesystem::DirectoryObservationKind::Overflow)
        {
            concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
            if (!m_configured)
                return false;
            ++m_hintsReceived;
            ++m_changeGeneration;
            m_overflowGeneration = m_changeGeneration;
            if (observation.root >= m_roots.Size())
                ++m_hintsRejected;
            m_overflow = true;
            return true;
        }

        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        if (!m_configured)
            return false;
        ++m_hintsReceived;
        if (IsTransactionArtifact(observation.path))
        {
            ++m_hintsSuppressed;
            return true;
        }
        const i32 root = FindRoot(observation.path);
        if (root < 0 || observation.root >= m_roots.Size() || static_cast<u32>(root) != observation.root)
        {
            ++m_hintsRejected;
            // A malformed/mismatched root hint cannot be trusted as a
            // path-local update. Request an authoritative rescan instead of
            // silently discarding a possibly real source change.
            ++m_changeGeneration;
            m_overflowGeneration = m_changeGeneration;
            m_overflow = true;
            return true;
        }

        ++m_changeGeneration;
        const containers::String key = PathKey(observation.path);
        u32 existing = 0;
        if (m_index.Find(key, existing) && existing < m_pending.Size() && m_pending[existing].path == observation.path)
        {
            PendingPath& entry = m_pending[existing];
            if (entry.generation <= m_discardThroughGeneration)
            {
                // This physical slot was subsumed by an older authoritative
                // rescan and is being reactivated by a genuinely newer hint.
                entry.firstSeenTick = m_ticks;
                ++m_activePendingPaths;
            }
            entry.lastSeenTick = m_ticks;
            entry.generation = m_changeGeneration;
            ++m_hintsCoalesced;
            return true;
        }

        if (m_pending.Size() >= m_config.maximumPendingPaths)
        {
            m_overflowGeneration = m_changeGeneration;
            m_overflow = true;
            return true;
        }
        PendingPath entry;
        entry.path = observation.path;
        entry.root = static_cast<u32>(root);
        entry.firstSeenTick = m_ticks;
        entry.lastSeenTick = m_ticks;
        entry.generation = m_changeGeneration;
        const u32 previous = m_pending.Size();
        m_pending.PushBack(static_cast<PendingPath&&>(entry));
        if (m_pending.Size() != previous + 1u)
        {
            m_overflowGeneration = m_changeGeneration;
            m_overflow = true;
            return true;
        }
        if (!m_index.Insert(key, previous).IsSuccessful())
        {
            m_pending.Resize(previous);
            m_overflowGeneration = m_changeGeneration;
            m_overflow = true;
            return true;
        }
        ++m_activePendingPaths;
        return true;
    }

    bool SourceChangeAccumulator::RemovePendingAt(const u32 index) noexcept
    {
        if (index >= m_pending.Size())
            return false;
        const containers::String removedKey = PathKey(m_pending[index].path);
        const u32 last = m_pending.Size() - 1u;
        containers::String movedKey;
        if (index != last)
            movedKey = PathKey(m_pending[last].path);
        if (!m_index.Remove(removedKey).IsSuccessful() || !m_pending.RemoveAtReorder(index).IsSuccessful())
            return false;
        if (index < m_pending.Size())
        {
            u32* const movedIndex = m_index.FindPtr(movedKey);
            if (movedIndex == nullptr)
                return false;
            *movedIndex = index;
        }
        if (m_pending.Empty())
            m_scanCursor = 0;
        else if (m_scanCursor >= m_pending.Size())
            m_scanCursor = 0;
        return true;
    }

    SourceChangeAdvance SourceChangeAccumulator::Advance() noexcept
    {
        SourceChangeAdvance result;
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        if (!m_configured)
            return result;
        ++m_ticks;

        if (m_overflow)
        {
            // The authoritative rescan represents every observation visible at
            // this instant. Make those entries logically inactive in O(1), then
            // reclaim their physical slots incrementally on later owner ticks.
            m_discardThroughGeneration = m_changeGeneration;
            m_activePendingPaths = 0;
            m_scanCursor = 0;
            // ready is bounded to maximumPathsPerTick by configuration.
            m_ready.Clear();
            ++m_rescansRequested;
            result.reconcile = true;
            result.overflow = true;
            result.hasPending = !m_pending.Empty();
            result.generation = m_changeGeneration;
            return result;
        }

        u32 promoted = 0;
        u32 inspected = 0;
        const u32 maximumInspections = m_config.maximumPathsPerTick;
        while (inspected < maximumInspections && !m_pending.Empty())
        {
            if (m_scanCursor >= m_pending.Size())
                m_scanCursor = 0;
            const u32 index = m_scanCursor;
            PendingPath& entry = m_pending[index];
            ++inspected;

            if (entry.generation <= m_discardThroughGeneration)
            {
                if (!RemovePendingAt(index))
                {
                    ++m_changeGeneration;
                    m_overflowGeneration = m_changeGeneration;
                    m_overflow = true;
                    break;
                }
                continue;
            }

            const u32 quiet = m_ticks - entry.lastSeenTick;
            const u32 total = m_ticks - entry.firstSeenTick;
            const bool due = quiet >= m_config.settleTicks || total >= m_config.maximumDeferralTicks;
            if (due && promoted < m_config.maximumPathsPerTick)
            {
                const u32 previous = m_ready.Size();
                m_ready.PushBack(entry);
                if (m_ready.Size() != previous + 1u)
                {
                    m_ready.Resize(previous);
                    ++m_changeGeneration;
                    m_overflowGeneration = m_changeGeneration;
                    m_overflow = true;
                    break;
                }
                if (m_activePendingPaths != 0u)
                    --m_activePendingPaths;
                if (!RemovePendingAt(index))
                {
                    ++m_changeGeneration;
                    m_overflowGeneration = m_changeGeneration;
                    m_overflow = true;
                    break;
                }
                ++promoted;
                continue;
            }
            ++m_scanCursor;
        }

        if (m_overflow)
        {
            // Any allocation failure degrades to one authoritative full rescan;
            // represented paths become logically inactive without bulk owner
            // destruction/reindexing.
            m_discardThroughGeneration = m_changeGeneration;
            m_activePendingPaths = 0;
            m_scanCursor = 0;
            m_ready.Clear();
            result.reconcile = true;
            result.overflow = true;
            result.readyPaths = 0;
            result.hasPending = !m_pending.Empty();
            ++m_rescansRequested;
            result.generation = m_changeGeneration;
            return result;
        }

        result.reconcile = !m_ready.Empty();
        result.overflow = false;
        result.readyPaths = m_ready.Size();
        // Physical stale entries left by an overflow are owner housekeeping and
        // keep TickObservation pending until they are reclaimed incrementally.
        result.hasPending = !m_pending.Empty();
        if (result.reconcile)
        {
            ++m_rescansRequested;
            result.generation = m_changeGeneration;
        }
        return result;
    }

    void SourceChangeAccumulator::RequeueReady() noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        for (PendingPath& entry : m_ready)
        {
            const containers::String key = PathKey(entry.path);
            u32 existing = 0;
            if (m_index.Find(key, existing) && existing < m_pending.Size() && m_pending[existing].path == entry.path)
            {
                PendingPath& pending = m_pending[existing];
                if (pending.generation <= m_discardThroughGeneration)
                {
                    pending.firstSeenTick = m_ticks;
                    pending.lastSeenTick = m_ticks;
                    pending.generation = entry.generation;
                    ++m_activePendingPaths;
                }
                // A newer hint for the same path already supersedes this ready
                // entry, so never overwrite its generation/timestamps.
                continue;
            }
            if (m_pending.Size() >= m_config.maximumPendingPaths)
            {
                ++m_changeGeneration;
                m_overflowGeneration = m_changeGeneration;
                m_overflow = true;
                break;
            }
            entry.firstSeenTick = m_ticks;
            entry.lastSeenTick = m_ticks;
            const u32 previous = m_pending.Size();
            m_pending.PushBack(static_cast<PendingPath&&>(entry));
            if (m_pending.Size() != previous + 1u)
            {
                ++m_changeGeneration;
                m_overflowGeneration = m_changeGeneration;
                m_overflow = true;
                break;
            }
            if (!m_index.Insert(key, previous).IsSuccessful())
            {
                m_pending.Resize(previous);
                ++m_changeGeneration;
                m_overflowGeneration = m_changeGeneration;
                m_overflow = true;
                break;
            }
            ++m_activePendingPaths;
        }
        // bounded to maximumPathsPerTick
        m_ready.Clear();
    }

    void SourceChangeAccumulator::AcknowledgeReconcile(const u64 generation) noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        m_ready.Clear();
        // Do not acknowledge an overflow published while reconciliation was
        // running. Partial root capture is also sticky because a successful
        // rescan cannot reconstruct an allocation-failed root map.
        if (!m_rootCaptureIncomplete && m_overflowGeneration <= generation)
            m_overflow = false;
        // Generation zero is reserved for the observation-start catch-up pass.
        // It is lifecycle synchronization rather than a response to a reported
        // filesystem change, so keep the public change-rescan counters focused
        // on real accumulated observations.
        if (generation != 0u)
            ++m_rescansCompleted;
    }

    bool SourceChangeAccumulator::HasRescanRequired() const noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        return m_overflow || !m_ready.Empty() || m_activePendingPaths != 0u || !m_pending.Empty();
    }

    SourceChangeSnapshot SourceChangeAccumulator::GetChangeSnapshot() const noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        return {m_changeGeneration, m_overflow};
    }

    bool SourceChangeAccumulator::CommitIfUnchanged(const SourceChangeSnapshot expected,
                                                    const StableSourceChangeCommitFunction commit,
                                                    void* const userData) const noexcept
    {
        if (commit == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        if (m_changeGeneration != expected.generation || m_overflow != expected.overflow || m_overflow)
            return false;
        return commit(userData);
    }

    ProjectAssetObservationStatus SourceChangeAccumulator::GetStatus() const noexcept
    {
        concurrency::ScopedLock<concurrency::Mutex> lock(m_mutex);
        ProjectAssetObservationStatus status;
        status.overflow = m_overflow;
        status.rescanRequired = m_overflow || !m_ready.Empty() || m_activePendingPaths != 0u || !m_pending.Empty();
        status.reconcilePending = !m_ready.Empty();
        status.watchedRoots = m_roots.Size();
        status.pendingPaths = m_activePendingPaths;
        status.readyPaths = m_ready.Size();
        // Every logically pending path is deferred until bounded owner scanning
        // promotes it. This keeps status reporting O(1) even under large bursts.
        status.deferredPaths = m_activePendingPaths;
        status.ticks = m_ticks;
        status.hintsReceived = m_hintsReceived;
        status.hintsCoalesced = m_hintsCoalesced;
        status.hintsSuppressed = m_hintsSuppressed;
        status.hintsRejected = m_hintsRejected;
        status.rescansRequested = m_rescansRequested;
        status.rescansCompleted = m_rescansCompleted;
        return status;
    }
} // namespace vanguard::asset_streaming
