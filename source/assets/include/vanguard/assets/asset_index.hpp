#pragma once

#include <vanguard/assets/assets.hpp>

namespace vanguard::assets
{
    enum class IndexResult : u8
    {
        Success,
        Recovered,
        NotFound,
        InvalidArgument,
        InvalidState,
        Incompatible,
        Corrupt,
        IoFailure,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(IndexResult result) noexcept;
    [[nodiscard]] constexpr bool IsSuccess(const IndexResult result) noexcept
    {
        return result == IndexResult::Success || result == IndexResult::Recovered;
    }

    enum class DirtyReason : u8
    {
        UpToDate,
        Missing,
        SourceChanged,
        CompilerChanged,
        DependenciesChanged
    };

    struct IndexedArtifact
    {
        resources::ResourceReference resource;
        u32 segment = 0;
        ArtifactFlags flags = ArtifactFlags::None;
        u8 alignmentLog2 = 4;
        u64 byteCount = 0;
    };

    class DependencyRecord final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

    public:
        DependencyRecord() noexcept;

        resources::ResourceReference source;
        resources::ResourceReference output;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        CompilerId compiler = InvalidCompilerId;
        u32 compilerVersion = 0;
        BuildFingerprint sourceInputFingerprint;
        BuildFingerprint buildFingerprint;
        BuildFingerprint contentFingerprint;
        containers::DynamicArray<BuildDependency> dependencies;
        containers::DynamicArray<IndexedArtifact> artifacts;
    };

    struct DependencyIndexConfig
    {
        // Absolute directory containing the Vanguard index. The path is copied.
        const char* root = nullptr;
        BuildFingerprint settingsFingerprint;
        u32 maximumRecords = 262144;
        u32 maximumDependenciesPerRecord = 4096;
        u32 maximumArtifactsPerRecord = 4096;
        u64 maximumSerializedBytes = 512ull * 1024ull * 1024ull;
        // Ordinary startup may discard invalid persistence and rebuild from
        // source/build authority. Safety-critical owners that still depend on
        // exact historical rows (for example a P5 predecessor cohort) disable
        // this so corruption/incompatibility or a temp-only image remains on
        // disk and initialization fails closed instead of erasing evidence.
        bool allowDestructiveRecovery = true;
    };

    struct DependencyIndexStats
    {
        u32 records = 0;
        u64 dependencies = 0;
        u64 artifacts = 0;
        u64 publications = 0;
        u64 replacements = 0;
        u64 removals = 0;
        u64 saves = 0;
        u64 loads = 0;
        u64 recoveries = 0;
        u64 corruptions = 0;
        u64 incompatibleFiles = 0;
        u64 ioFailures = 0;
        u64 transactionCommits = 0;
        u64 transactionRollbacks = 0;
    };

    // Exact loose/publication evidence used to release a durable retirement
    // protection batch. A batch is released only after every successor witness
    // captured with that retirement is present in the caller's visible set.
    struct DependencyPublicationWitness
    {
        resources::ResourceReference output;
        BuildFingerprint buildFingerprint;
        BuildFingerprint contentFingerprint;
        // True when the successor output path already existed in the committed
        // pre-transaction index. Restart uses this to distinguish unchanged
        // predecessor members from newly introduced cohort outputs.
        bool predecessorPresent = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return output.IsValid() && output.IsTyped() && !buildFingerprint.IsEmpty() && !contentFingerprint.IsEmpty();
        }
    };

    struct DependencyRetirementProtectionBatch
    {
        u32 recordOffset = 0u;
        u32 recordCount = 0u;
        u32 successorOffset = 0u;
        u32 successorCount = 0u;
    };

    [[nodiscard]] BuildFingerprint ComputeSourceInputFingerprint(const BuildRequest& request) noexcept;

    class DependencyIndex final
    {
    public:
        struct Impl;

        DependencyIndex() noexcept = default;
        ~DependencyIndex();

        DependencyIndex(const DependencyIndex&) = delete;
        DependencyIndex& operator=(const DependencyIndex&) = delete;

        // Loads the existing index when present. Corrupt or incompatible data
        // is explicitly reported as Recovered and removed so it cannot poison
        // subsequent editor sessions.
        [[nodiscard]] IndexResult Initialize(const DependencyIndexConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Joins this index to the one persistent VDDC protection domain that
        // may publish durable VADI rows for the managed root. Binding is only
        // accepted while the index is clean and transaction-free.
        [[nodiscard]] bool BindPersistentProtectionDomain(BuildSystem& buildSystem) noexcept;

        [[nodiscard]] IndexResult Publish(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output) noexcept;
        [[nodiscard]] IndexResult Remove(resources::ResourceReference output) noexcept;
        void Clear() noexcept;

        [[nodiscard]] IndexResult Find(resources::ResourceReference output, DependencyRecord& record) const noexcept;
        [[nodiscard]] IndexResult GetDependencies(resources::ResourceReference output,
                                                  containers::DynamicArray<resources::ResourceReference>& dependencies) const noexcept;
        [[nodiscard]] IndexResult GetDirectDependants(resources::ResourceReference input,
                                                      containers::DynamicArray<resources::ResourceReference>& dependants) const noexcept;
        [[nodiscard]] IndexResult CollectAffected(resources::ResourceReference changed,
                                                  containers::DynamicArray<resources::ResourceReference>& affected) const noexcept;
        // Materializes committed Generated-dependency content into a prepared
        // plan. Preferred records (for example staged transaction rows) win over
        // committed rows. Missing Optional Generated dependencies are ignored;
        // missing Required Generated dependencies are left unresolved and yield
        // NotFound after all available rows have been applied. Soft Generated
        // dependencies are never materialized here.
        [[nodiscard]] IndexResult MaterializeGeneratedDependencyContent(
            BuildPlan& plan, containers::ArraySpan<const DependencyRecord> preferredRecords = {}) const noexcept;
        [[nodiscard]] DirtyReason Evaluate(const BuildRequest& request, const BuildPlan& plan) const noexcept;
        // Bounded immutable exact-key snapshot for cache-lifecycle protection.
        // Includes committed records and any currently staged transaction rows;
        // no mutable index storage escapes this owner.
        [[nodiscard]] IndexResult CaptureProtectionKeys(containers::DynamicArray<ArtifactSetKey>& keys,
                                                        u32 maximumRecords, u64 maximumBytes) const noexcept;
        // Deep-copy durable last-known-good rows retained by topology retirement.
        // These rows are not returned by Find/Evaluate and therefore cannot become
        // current build authority, but their exact VDDC keys remain protected.
        [[nodiscard]] IndexResult CaptureRetiredProtectionRecords(containers::DynamicArray<DependencyRecord>& records,
                                                                 u32 maximumRecords, u64 maximumBytes) const noexcept;
        // Captures the exact persisted retirement journal, preserving batch
        // boundaries for restart restore. All returned arrays own their data.
        [[nodiscard]] IndexResult CaptureRetirementProtectionSnapshot(
            containers::DynamicArray<DependencyRetirementProtectionBatch>& batches,
            containers::DynamicArray<DependencyRecord>& records,
            containers::DynamicArray<DependencyPublicationWitness>& successors,
            u32 maximumRecords, u64 maximumBytes) const noexcept;
        // Removes only retirement batches whose complete exact successor set is
        // already visible. The cleanup is durably saved before protection drops;
        // a save failure leaves every retained row protected for retry/restart.
        [[nodiscard]] IndexResult ReleaseSatisfiedRetirementProtections(
            containers::ArraySpan<const DependencyPublicationWitness> visible) noexcept;

        [[nodiscard]] IndexResult Save() noexcept;
        [[nodiscard]] bool HasChanges() const noexcept;
        [[nodiscard]] IndexResult BeginTransaction() noexcept;
        // Stage retirement of an existing exact typed output. Retirement remains
        // invisible to committed readers until CommitTransaction durably saves
        // it together with all staged replacement/build records. Rollback
        // discards the retirement without touching the committed index.
        [[nodiscard]] IndexResult StageRemoval(resources::ResourceReference output) noexcept;
        // Declares the exact current output set whose successful visibility is
        // required before this transaction's retired rows may lose LKG protection.
        // Every successor must also be staged by this transaction.
        [[nodiscard]] IndexResult StageRetirementSuccessors(
            containers::ArraySpan<const resources::ResourceReference> outputs) noexcept;
        // Deep-copy the currently staged transaction records for validation.
        // Normal Find/Evaluate readers remain committed-only. The returned
        // snapshot owns all dependency/artifact arrays, so callers may perform
        // filesystem/importer/compiler work after this function releases the
        // index lock. Hard bounds prevent unbounded pre-commit capture.
        [[nodiscard]] IndexResult CaptureTransactionRecords(containers::DynamicArray<DependencyRecord>& records,
                                                            u32 maximumRecords, u64 maximumBytes) const noexcept;
        [[nodiscard]] IndexResult CommitTransaction() noexcept;
        [[nodiscard]] IndexResult RollbackTransaction() noexcept;
        [[nodiscard]] bool HasActiveTransaction() const noexcept;
        [[nodiscard]] bool HasRetirementProtections() const noexcept;
        [[nodiscard]] DependencyIndexStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool DetachPersistentProtectionDomain(BuildSystem& buildSystem) noexcept;
        Impl* m_impl = nullptr;

        friend class BuildGraph;
        friend class BuildSystem;
    };
} // namespace vanguard::assets
