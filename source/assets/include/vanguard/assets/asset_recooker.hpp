#pragma once

#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/assets/asset_index.hpp>

namespace vanguard::assets
{
    class IncrementalRecooker;
    struct RecookOperation;

    enum class RecookState : u8
    {
        Building,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class RecookFailure : u8
    {
        None,
        InvalidChange,
        UntrackedChange,
        RequestResolutionFailed,
        DependencyPreparationFailed,
        IndexQueryFailed,
        TransactionFailed,
        BuildFailed,
        Cancelled,
        OutOfMemory,
        LimitExceeded
    };

    struct AssetChange
    {
        resources::ResourceReference identity;
    };

    struct RecookConfig
    {
        u32 maximumChangesPerBatch = 4096;
        u32 maximumAffectedOutputsPerBatch = 65536;
        u32 maximumRootRequestsPerBatch = 4096;
    };

    struct RecookStats
    {
        u32 inputChanges = 0;
        u32 unchangedChanges = 0;
        u32 dirtySeeds = 0;
        u32 affectedOutputs = 0;
        u32 rootRequests = 0;
        u32 succeededRoots = 0;
        u32 failedRoots = 0;
        u64 completedBatches = 0;
        u64 failedBatches = 0;
        u64 cancelledBatches = 0;
    };

    // Resolves a cooked output identity to its current source bytes, metadata,
    // settings, and target. The request is copied before the callback returns.
    using ResolveIndexedBuildRequestFunction = bool (*)(resources::ResourceReference output, BuildRequest& request, void* userData) noexcept;

    class RecookBatch final
    {
    public:
        RecookBatch() noexcept = default;
        RecookBatch(RecookBatch&& other) noexcept;
        RecookBatch& operator=(RecookBatch&& other) noexcept;
        ~RecookBatch();

        RecookBatch(const RecookBatch&) = delete;
        RecookBatch& operator=(const RecookBatch&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] RecookState GetStatus() const noexcept;
        [[nodiscard]] RecookFailure GetError() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool Poll() noexcept;
        void Wait() noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] RecookStats GetStats() const noexcept;
        void Reset() noexcept;

    private:
        RecookBatch(IncrementalRecooker* owner, RecookOperation* operation) noexcept;

        IncrementalRecooker* m_owner = nullptr;
        RecookOperation* m_operation = nullptr;

        friend class IncrementalRecooker;
    };

    class IncrementalRecooker final
    {
    public:
        struct Impl;

        IncrementalRecooker() noexcept = default;
        ~IncrementalRecooker();

        IncrementalRecooker(const IncrementalRecooker&) = delete;
        IncrementalRecooker& operator=(const IncrementalRecooker&) = delete;

        [[nodiscard]] bool Initialize(BuildSystem& buildSystem, BuildGraph& graph, DependencyIndex& index, ResolveIndexedBuildRequestFunction resolver,
                                      void* resolverUserData = nullptr, const RecookConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] RecookBatch Request(containers::ArraySpan<const AssetChange> changes, BuildPriority priority = BuildPriority::Normal) noexcept;
        [[nodiscard]] RecookStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool Poll(RecookOperation& operation, bool wait) noexcept;
        [[nodiscard]] bool Cancel(RecookOperation& operation) noexcept;
        void Release(RecookOperation& operation) noexcept;

        Impl* m_impl = nullptr;

        friend class RecookBatch;
    };
} // namespace vanguard::assets
