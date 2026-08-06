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

        [[nodiscard]] IndexResult Publish(const BuildRequest& request, const BuildPlan& plan, const BuildOutput& output) noexcept;
        [[nodiscard]] IndexResult Remove(resources::ResourceReference output) noexcept;
        void Clear() noexcept;

        [[nodiscard]] IndexResult Find(resources::ResourceReference output, DependencyRecord& record) const noexcept;
        [[nodiscard]] IndexResult GetDependencies(resources::ResourceReference output,
                                                  containers::DynamicArray<resources::ResourceReference>& dependencies) const noexcept;
        [[nodiscard]] IndexResult GetDirectDependants(resources::ResourceReference input,
                                                      containers::DynamicArray<resources::ResourceReference>& dependants) const noexcept;
        [[nodiscard]] IndexResult CollectAffected(resources::ResourceReference changed,
                                                  containers::DynamicArray<resources::ResourceReference>& affected) const noexcept;
        [[nodiscard]] DirtyReason Evaluate(const BuildRequest& request, const BuildPlan& plan) const noexcept;

        [[nodiscard]] IndexResult Save() noexcept;
        [[nodiscard]] bool HasChanges() const noexcept;
        [[nodiscard]] IndexResult BeginTransaction() noexcept;
        [[nodiscard]] IndexResult CommitTransaction() noexcept;
        [[nodiscard]] IndexResult RollbackTransaction() noexcept;
        [[nodiscard]] bool HasActiveTransaction() const noexcept;
        [[nodiscard]] DependencyIndexStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
