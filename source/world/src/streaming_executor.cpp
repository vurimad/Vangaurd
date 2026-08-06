#include <vanguard/world/streaming_executor.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    namespace world = vanguard::world;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateStreamingObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(Type), alignof(Type));
        if (!block) return nullptr;
        return ::new (block.address) Type(static_cast<Args&&>(args)...);
    }

    template<typename Type>
    void DeleteStreamingObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Streaming};
        memory::Free(block);
    }

    [[nodiscard]] constexpr resources::LoadPriority ToLoadPriority(const world::StreamingPriority priority) noexcept
    {
        const u8 value = static_cast<u8>(priority);
        if (value >= static_cast<u8>(world::StreamingPriority::Critical)) return resources::LoadPriority::Critical;
        if (value >= static_cast<u8>(world::StreamingPriority::StaticMesh)) return resources::LoadPriority::High;
        if (value == static_cast<u8>(world::StreamingPriority::Low)) return resources::LoadPriority::Background;
        return resources::LoadPriority::Normal;
    }
} // namespace

namespace vanguard::world
{
    struct WorldStreamingExecutor::Impl
    {
        enum class Phase : u8
        {
            Idle,
            Requesting,
            Resident,
            ReleasePending,
            Failed
        };

        struct Operation
        {
            StreamingNodeKey key;
            resources::ResourceReference resource;
            resources::PipelineRequest request;
            resources::ResourceHandle handle;
            resources::FailureTrace failureTrace;
            resources::Failure failure = resources::Failure::None;
            Phase phase = Phase::Idle;
        };

        Impl(WorldStreamingGrid& streamingGrid, resources::ResourcePipeline& resourcePipeline) noexcept
            : grid(&streamingGrid), pipeline(&resourcePipeline), operations(memory::pools::Streaming::GetInstance()),
              cellLookup(memory::pools::Streaming::GetInstance()), proxyLookup(memory::pools::Streaming::GetInstance()),
              commands(memory::pools::Streaming::GetInstance())
        {
        }

        [[nodiscard]] const containers::HashMap<u64, u32>& Lookup(const StreamingNodeKind kind) const noexcept
        {
            return kind == StreamingNodeKind::Cell ? cellLookup : proxyLookup;
        }

        [[nodiscard]] containers::HashMap<u64, u32>& Lookup(const StreamingNodeKind kind) noexcept
        {
            return kind == StreamingNodeKind::Cell ? cellLookup : proxyLookup;
        }

        [[nodiscard]] Operation* Find(const StreamingNodeKey key) noexcept
        {
            u32 index = 0;
            return Lookup(key.kind).Find(key.id, index) && index < operations.Size() ? &operations[index] : nullptr;
        }

        [[nodiscard]] const Operation* Find(const StreamingNodeKey key) const noexcept
        {
            u32 index = 0;
            return Lookup(key.kind).Find(key.id, index) && index < operations.Size() ? &operations[index] : nullptr;
        }

        [[nodiscard]] Operation* FindOrCreate(const StreamingNodeKey key) noexcept
        {
            if (Operation* const existing = Find(key)) return existing;
            const u32 index = operations.Size();
            operations.EmplaceBack();
            if (operations.Size() != index + 1u || !Lookup(key.kind).Insert(key.id, index).IsSuccessful())
            {
                if (operations.Size() == index + 1u) static_cast<void>(operations.RemoveAt(index));
                return nullptr;
            }
            operations[index].key = key;
            return &operations[index];
        }

        [[nodiscard]] bool Emit(containers::DynamicArray<StreamingResourceEvent>& events,
                                const StreamingResourceEvent& event) noexcept
        {
            const u32 expected = events.Size() + 1u;
            events.PushBack(event);
            return events.Size() == expected;
        }

        [[nodiscard]] bool Poll(containers::DynamicArray<StreamingResourceEvent>& events) noexcept
        {
            for (Operation& operation : operations)
            {
                if (operation.phase != Phase::Requesting || !operation.request.HasFinished()) continue;
                if (operation.request.HasLoaded())
                {
                    operation.handle = operation.request.Acquire();
                    if (!operation.handle)
                    {
                        operation.failure = resources::Failure::InternalError;
                        operation.failureTrace = {};
                        operation.failureTrace.entries[0] = {operation.resource, operation.failure};
                        operation.failureTrace.count = 1;
                        operation.request.Reset();
                        operation.phase = Phase::Failed;
                        ++failedRequests;
                        if (!grid->NotifyStreamInComplete(operation.key, false) ||
                            !Emit(events, {StreamingResourceEventType::ResourceFailed, operation.key, operation.resource,
                                           operation.failure})) return false;
                        continue;
                    }
                    operation.failure = resources::Failure::None;
                    operation.failureTrace = {};
                    operation.request.Reset();
                    operation.phase = Phase::Resident;
                    ++completedRequests;
                    if (!grid->NotifyStreamInComplete(operation.key, true, false) ||
                        !Emit(events, {StreamingResourceEventType::ResourceAvailable, operation.key, operation.resource,
                                       resources::Failure::None})) return false;
                }
                else
                {
                    operation.failure = operation.request.Error();
                    operation.failureTrace = {};
                    if (!operation.request.GetFailureTrace(operation.failureTrace))
                    {
                        operation.failureTrace.entries[0] = {operation.resource, operation.failure};
                        operation.failureTrace.count = 1;
                    }
                    operation.request.Reset();
                    operation.phase = Phase::Failed;
                    ++failedRequests;
                    if (!grid->NotifyStreamInComplete(operation.key, false) ||
                        !Emit(events, {StreamingResourceEventType::ResourceFailed, operation.key, operation.resource,
                                       operation.failure})) return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool BeginStreamIn(const StreamingCommand& command,
                                         containers::DynamicArray<StreamingResourceEvent>& events) noexcept
        {
            Operation* const operation = FindOrCreate(command.key);
            if (operation == nullptr || operation->phase != Phase::Idle)
            {
                if (operation != nullptr)
                {
                    operation->failure = resources::Failure::InternalError;
                    operation->phase = Phase::Failed;
                }
                return grid->NotifyStreamInComplete(command.key, false) &&
                       Emit(events, {StreamingResourceEventType::ResourceFailed, command.key, command.resource,
                                     resources::Failure::InternalError});
            }
            operation->resource = command.resource;
            operation->failure = resources::Failure::None;
            operation->failureTrace = {};
            operation->request = pipeline->Request(command.resource, ToLoadPriority(command.priority));
            ++submittedRequests;
            if (!operation->request)
            {
                operation->failure = resources::Failure::InternalError;
                operation->failureTrace.entries[0] = {operation->resource, operation->failure};
                operation->failureTrace.count = 1;
                operation->phase = Phase::Failed;
                ++failedRequests;
                return grid->NotifyStreamInComplete(command.key, false) &&
                       Emit(events, {StreamingResourceEventType::ResourceFailed, command.key, command.resource,
                                     operation->failure});
            }
            operation->phase = Phase::Requesting;
            return true;
        }

        [[nodiscard]] bool BeginStreamOut(const StreamingCommand& command,
                                          containers::DynamicArray<StreamingResourceEvent>& events) noexcept
        {
            Operation* const operation = Find(command.key);
            if (operation == nullptr) return false;
            if (operation->phase == Phase::Requesting)
            {
                if (!operation->request.Cancel()) return false;
                operation->request.Reset();
                operation->failure = resources::Failure::Cancelled;
                operation->phase = Phase::Idle;
                ++cancelledRequests;
                return grid->NotifyStreamOutComplete(command.key) &&
                       Emit(events, {StreamingResourceEventType::RequestCancelled, command.key, command.resource,
                                     resources::Failure::Cancelled});
            }
            if (operation->phase == Phase::Failed)
            {
                operation->phase = Phase::Idle;
                return grid->NotifyStreamOutComplete(command.key);
            }
            if (operation->phase != Phase::Resident) return false;
            operation->phase = Phase::ReleasePending;
            return Emit(events, {StreamingResourceEventType::ReleaseRequested, command.key, command.resource,
                                 resources::Failure::None});
        }

        WorldStreamingGrid* grid = nullptr;
        resources::ResourcePipeline* pipeline = nullptr;
        containers::DynamicArray<Operation> operations;
        containers::HashMap<u64, u32> cellLookup;
        containers::HashMap<u64, u32> proxyLookup;
        containers::DynamicArray<StreamingCommand> commands;
        u64 submittedRequests = 0;
        u64 completedRequests = 0;
        u64 failedRequests = 0;
        u64 cancelledRequests = 0;
        u64 releasedResources = 0;
    };

    WorldStreamingExecutor::~WorldStreamingExecutor()
    {
        DeleteStreamingObject(m_impl);
        m_impl = nullptr;
    }

    bool WorldStreamingExecutor::Initialize(WorldStreamingGrid& grid, resources::ResourcePipeline& pipeline) noexcept
    {
        if (m_impl != nullptr) return false;
        if (!grid.IsInitialized() || !pipeline.IsInitialized()) return false;
        m_impl = AllocateStreamingObject<Impl>(grid, pipeline);
        return m_impl != nullptr;
    }

    bool WorldStreamingExecutor::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        for (const Impl::Operation& operation : m_impl->operations)
            if (operation.phase != Impl::Phase::Idle) return false;
        DeleteStreamingObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool WorldStreamingExecutor::IsInitialized() const noexcept { return m_impl != nullptr; }

    bool WorldStreamingExecutor::Process(const StreamingProcessInput& input,
                                         containers::DynamicArray<StreamingResourceEvent>& events) noexcept
    {
        events.Clear();
        if (m_impl == nullptr || !m_impl->Poll(events) || !m_impl->grid->Process(input, m_impl->commands)) return false;
        events.Reserve(events.Size() + m_impl->commands.Size());
        for (const StreamingCommand& command : m_impl->commands)
        {
            const bool success = command.type == StreamingCommandType::StreamIn
                                     ? m_impl->BeginStreamIn(command, events)
                                     : m_impl->BeginStreamOut(command, events);
            if (!success) return false;
        }
        return true;
    }

    bool WorldStreamingExecutor::SetReady(const StreamingNodeKey key, const bool ready) noexcept
    {
        if (m_impl == nullptr) return false;
        const Impl::Operation* const operation = m_impl->Find(key);
        return operation != nullptr && operation->phase == Impl::Phase::Resident && m_impl->grid->SetRenderReady(key, ready);
    }

    bool WorldStreamingExecutor::CompleteRelease(const StreamingNodeKey key) noexcept
    {
        if (m_impl == nullptr) return false;
        Impl::Operation* const operation = m_impl->Find(key);
        if (operation == nullptr || operation->phase != Impl::Phase::ReleasePending) return false;
        operation->handle.Reset();
        operation->resource = {};
        operation->failure = resources::Failure::None;
        operation->failureTrace = {};
        operation->phase = Impl::Phase::Idle;
        ++m_impl->releasedResources;
        return m_impl->grid->NotifyStreamOutComplete(key);
    }

    bool WorldStreamingExecutor::FailResident(const StreamingNodeKey key, const resources::Failure failure) noexcept
    {
        if (m_impl == nullptr || failure == resources::Failure::None) return false;
        Impl::Operation* const operation = m_impl->Find(key);
        if (operation == nullptr || operation->phase != Impl::Phase::Resident ||
            !m_impl->grid->NotifyResidentFailed(key)) return false;
        operation->handle.Reset();
        operation->failure = failure;
        operation->failureTrace = {};
        operation->failureTrace.entries[0] = {operation->resource, failure};
        operation->failureTrace.count = 1;
        operation->phase = Impl::Phase::Failed;
        ++m_impl->failedRequests;
        return true;
    }

    const resources::ResourceHandle* WorldStreamingExecutor::Resource(const StreamingNodeKey key) const noexcept
    {
        if (m_impl == nullptr) return nullptr;
        const Impl::Operation* const operation = m_impl->Find(key);
        return operation != nullptr && (operation->phase == Impl::Phase::Resident ||
                                        operation->phase == Impl::Phase::ReleasePending) ? &operation->handle : nullptr;
    }

    resources::Failure WorldStreamingExecutor::LastFailure(const StreamingNodeKey key) const noexcept
    {
        if (m_impl == nullptr) return resources::Failure::None;
        const Impl::Operation* const operation = m_impl->Find(key);
        return operation != nullptr ? operation->failure : resources::Failure::None;
    }

    bool WorldStreamingExecutor::GetFailureTrace(const StreamingNodeKey key, resources::FailureTrace& trace) const noexcept
    {
        trace = {};
        if (m_impl == nullptr) return false;
        const Impl::Operation* const operation = m_impl->Find(key);
        if (operation == nullptr || operation->failure == resources::Failure::None) return false;
        trace = operation->failureTrace;
        return true;
    }

    StreamingExecutorStats WorldStreamingExecutor::GetStats() const noexcept
    {
        StreamingExecutorStats stats;
        if (m_impl == nullptr) return stats;
        stats.knownNodes = m_impl->operations.Size();
        stats.submittedRequests = m_impl->submittedRequests;
        stats.completedRequests = m_impl->completedRequests;
        stats.failedRequests = m_impl->failedRequests;
        stats.cancelledRequests = m_impl->cancelledRequests;
        stats.releasedResources = m_impl->releasedResources;
        for (const Impl::Operation& operation : m_impl->operations)
        {
            stats.requestingNodes += operation.phase == Impl::Phase::Requesting;
            stats.residentNodes += operation.phase == Impl::Phase::Resident;
            stats.releasePendingNodes += operation.phase == Impl::Phase::ReleasePending;
            stats.failedNodes += operation.phase == Impl::Phase::Failed;
        }
        return stats;
    }
} // namespace vanguard::world
