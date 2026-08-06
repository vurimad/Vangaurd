#pragma once

#include <vanguard/resources/resources.hpp>

namespace vanguard::resources
{
    class ResourcePipeline;
    struct PipelineOperation;

    enum class LoadPriority : u8
    {
        Background,
        Low,
        Normal,
        High,
        Critical
    };

    enum class DependencyRequirement : u8
    {
        Required,
        Optional
    };

    struct PipelineConfig
    {
        u32 maximumDependenciesPerResource = 4096;
        u32 maximumFailureTraceDepth = 32;
    };

    class DependencyBuilder final
    {
    public:
        [[nodiscard]] bool Add(ResourceReference reference, DependencyRequirement requirement = DependencyRequirement::Required) noexcept;
        [[nodiscard]] u32 Count() const noexcept;

    private:
        explicit DependencyBuilder(void* state) noexcept;

        void* m_state = nullptr;

        friend class ResourcePipeline;
    };

    class LoadContext final
    {
    public:
        [[nodiscard]] ResourceReference Reference() const noexcept;
        [[nodiscard]] LoadPriority Priority() const noexcept;
        [[nodiscard]] bool IsCancellationRequested() const noexcept;
        [[nodiscard]] u32 DependencyCount() const noexcept;
        [[nodiscard]] ResourceReference DependencyReference(u32 index) const noexcept;
        [[nodiscard]] DependencyRequirement DependencyRequirementAt(u32 index) const noexcept;
        [[nodiscard]] Failure DependencyError(u32 index) const noexcept;
        [[nodiscard]] const ResourceHandle& Dependency(u32 index) const noexcept;

    private:
        explicit LoadContext(void* state) noexcept;

        void* m_state = nullptr;

        friend class ResourcePipeline;
    };

    class PreparationRequest final
    {
    public:
        PreparationRequest() noexcept = default;

        [[nodiscard]] ResourceReference Reference() const noexcept;
        [[nodiscard]] LoadPriority Priority() const noexcept;
        [[nodiscard]] bool IsCancellationRequested() const noexcept;

        // Must be called exactly once by a successfully started preparation,
        // including after its asynchronous cancellation callback runs.
        [[nodiscard]] bool Complete(Failure failure = Failure::None) const noexcept;

    private:
        PreparationRequest(ResourcePipeline* pipeline, PipelineOperation* operation) noexcept;

        ResourcePipeline* m_pipeline = nullptr;
        PipelineOperation* m_operation = nullptr;

        friend class ResourcePipeline;
    };

    using DiscoverDependenciesFunction = Failure (*)(ResourceReference reference, DependencyBuilder& dependencies, void* userData) noexcept;
    using BeginPreparationFunction = bool (*)(const PreparationRequest& request, void* userData) noexcept;
    using CancelPreparationFunction = void (*)(const PreparationRequest& request, void* userData) noexcept;
    using ConstructResourceFunction = ResourceObject* (*)(const LoadContext& context, Failure& failure, void* userData) noexcept;

    struct AsyncLoaderDescriptor
    {
        ResourceTypeId type = InvalidResourceTypeId;
        const char* name = nullptr;
        DiscoverDependenciesFunction discoverDependencies = nullptr;
        ConstructResourceFunction constructResource = nullptr;
        DestroyResourceFunction destroyResource = nullptr;
        void* userData = nullptr;
        BeginPreparationFunction beginPreparation = nullptr;
        CancelPreparationFunction cancelPreparation = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return type != InvalidResourceTypeId && name != nullptr && name[0] != '\0' && discoverDependencies != nullptr &&
                   constructResource != nullptr && destroyResource != nullptr;
        }
    };

    inline constexpr u32 MaximumFailureTraceEntries = 32;

    struct FailureTraceEntry
    {
        ResourceReference resource;
        Failure failure = Failure::None;
    };

    struct FailureTrace
    {
        FailureTraceEntry entries[MaximumFailureTraceEntries];
        u32 count = 0;
        bool truncated = false;
    };

    struct PipelineStats
    {
        u32 registeredLoaders = 0;
        u32 knownOperations = 0;
        u32 activeOperations = 0;
        u32 activeJobs = 0;
        u32 activePreparations = 0;
        u32 externalRequests = 0;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
        u64 priorityPromotions = 0;
        u64 dependencyEdges = 0;
        u64 cancelledOperations = 0;
        u64 completedOperations = 0;
        u64 failedOperations = 0;
    };

    class PipelineRequest final
    {
    public:
        PipelineRequest() noexcept = default;
        PipelineRequest(PipelineRequest&& other) noexcept;
        ~PipelineRequest();

        PipelineRequest(const PipelineRequest&) = delete;
        PipelineRequest& operator=(const PipelineRequest&) = delete;
        PipelineRequest& operator=(PipelineRequest&& other) noexcept;

        [[nodiscard]] ResourceReference Reference() const noexcept;
        [[nodiscard]] LoadPriority Priority() const noexcept;
        [[nodiscard]] State Status() const noexcept;
        [[nodiscard]] Failure Error() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool HasLoaded() const noexcept;
        [[nodiscard]] bool HasFailed() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] bool IsSameOperation(const PipelineRequest& other) const noexcept;

        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] ResourceHandle Acquire() const noexcept;
        [[nodiscard]] bool Promote(LoadPriority priority) noexcept;
        // Cancels only this caller's interest. Shared operations continue
        // while another root request or dependency edge still needs them.
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] bool GetFailureTrace(FailureTrace& trace) const noexcept;
        void Reset() noexcept;

    private:
        PipelineRequest(ResourcePipeline* pipeline, PipelineOperation* operation) noexcept;

        ResourcePipeline* m_pipeline = nullptr;
        PipelineOperation* m_operation = nullptr;
        bool m_hasInterest = false;
        bool m_cancelled = false;

        friend class ResourcePipeline;
    };

    class ResourcePipeline final
    {
    public:
        struct Impl;

        ResourcePipeline() noexcept = default;
        ~ResourcePipeline();

        ResourcePipeline(const ResourcePipeline&) = delete;
        ResourcePipeline& operator=(const ResourcePipeline&) = delete;

        [[nodiscard]] bool Initialize(ResourceRegistry& registry, const PipelineConfig& config = {}) noexcept;
        // Jobs and the registry must outlive the pipeline. Shutdown is a
        // serialized composition-root operation and refuses live requests,
        // handles, operations, or stage jobs.
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterLoader(const AsyncLoaderDescriptor& loader) noexcept;
        [[nodiscard]] bool UnregisterLoader(ResourceTypeId type) noexcept;
        [[nodiscard]] bool HasLoader(ResourceTypeId type) const noexcept;

        // PipelineRequest is one caller interest. Dropping its last interest
        // cancels unfinished work or evicts a completed unreferenced object.
        [[nodiscard]] PipelineRequest Request(ResourceReference reference, LoadPriority priority = LoadPriority::Normal) noexcept;
        [[nodiscard]] PipelineStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;

        static void BeginRegistryLoad(ResourceRegistry& registry, const ResourceRequest& request, void* userData) noexcept;
        static void DestroyRegistryResource(ResourceObject* resource, void* userData) noexcept;

        void OnBeginRegistryLoad(ResourceRegistry& registry, const ResourceRequest& request) noexcept;
        void ReleaseRequestInterest(PipelineOperation* operation, bool cancelled) noexcept;
        [[nodiscard]] bool PromoteRequest(PipelineOperation* operation, LoadPriority priority) noexcept;
        [[nodiscard]] bool BuildFailureTrace(const PipelineOperation* operation, FailureTrace& trace) const noexcept;
        [[nodiscard]] bool CompletePreparation(PipelineOperation* operation, Failure failure) noexcept;

        friend class PipelineRequest;
        friend class PreparationRequest;
    };
} // namespace vanguard::resources
