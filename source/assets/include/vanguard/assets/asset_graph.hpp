#pragma once

#include <vanguard/assets/assets.hpp>

namespace vanguard::assets
{
    class BuildGraph;
    class DependencyIndex;
    struct BuildOperation;

    enum class BuildPriority : u8
    {
        Background,
        Low,
        Normal,
        High,
        Critical
    };

    enum class BuildState : u8
    {
        Resolving,
        Queued,
        Building,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class BuildFailure : u8
    {
        None,
        InvalidRequest,
        ResolutionFailed,
        DependencyCycle,
        DependencyFailed,
        Cancelled,
        BuildFailed,
        OutOfMemory,
        LimitExceeded,
        SchedulingFailed,
        IndexPublicationFailed
    };

    struct BuildGraphConfig
    {
        u32 maximumKnownOperations = 65536;
        u32 maximumGeneratedDependenciesPerOperation = 4096;
    };

    // Called only for generated dependencies discovered by BuildSystem::Prepare.
    // The returned request is copied before the callback returns. Its output
    // identity must exactly match dependency.identity.
    using ResolveGeneratedDependencyFunction = bool (*)(const BuildDependency& dependency, BuildRequest& request, void* userData) noexcept;

    struct BuildGraphStats
    {
        u32 knownOperations = 0;
        u32 activeOperations = 0;
        u32 externalRequests = 0;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
        u64 dependencyEdges = 0;
        u64 completedOperations = 0;
        u64 failedOperations = 0;
        u64 cancelledOperations = 0;
    };

    class GraphRequest final
    {
    public:
        GraphRequest() noexcept = default;
        GraphRequest(GraphRequest&& other) noexcept;
        GraphRequest& operator=(GraphRequest&& other) noexcept;
        ~GraphRequest();

        GraphRequest(const GraphRequest&) = delete;
        GraphRequest& operator=(const GraphRequest&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] bool IsSameOperation(const GraphRequest& other) const noexcept;
        [[nodiscard]] BuildState Status() const noexcept;
        [[nodiscard]] BuildFailure Error() const noexcept;
        [[nodiscard]] Result BuildError() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool HasSucceeded() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] bool CopyOutput(BuildOutput& output) const noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        void Reset() noexcept;

    private:
        GraphRequest(BuildGraph* graph, BuildOperation* operation) noexcept;

        BuildGraph* m_graph = nullptr;
        BuildOperation* m_operation = nullptr;
        bool m_hasInterest = false;

        friend class BuildGraph;
    };

    class BuildGraph final
    {
    public:
        struct Impl;

        BuildGraph() noexcept = default;
        ~BuildGraph();

        BuildGraph(const BuildGraph&) = delete;
        BuildGraph& operator=(const BuildGraph&) = delete;

        [[nodiscard]] bool Initialize(BuildSystem& buildSystem, ResolveGeneratedDependencyFunction resolver,
                                      void* resolverUserData = nullptr, const BuildGraphConfig& config = {},
                                      DependencyIndex* dependencyIndex = nullptr) noexcept;
        // BuildSystem, Jobs, resolver state, and an optional DependencyIndex
        // must outlive the graph.
        // Shutdown refuses live requests or unfinished operations.
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsBoundTo(const BuildSystem& buildSystem, const DependencyIndex& dependencyIndex) const noexcept;

        // Dependency discovery and topological classification occur before
        // this returns. Compilation, cache access, and fan-in run on Jobs.
        [[nodiscard]] GraphRequest Request(const BuildRequest& request, BuildPriority priority = BuildPriority::Normal) noexcept;
        [[nodiscard]] BuildGraphStats GetStats() const noexcept;

    private:
        void ReleaseInterest(BuildOperation* operation, bool explicitCancellation) noexcept;
        [[nodiscard]] bool CopyOperationOutput(const BuildOperation* operation, BuildOutput& output) const noexcept;

        Impl* m_impl = nullptr;

        friend class GraphRequest;
    };
} // namespace vanguard::assets
