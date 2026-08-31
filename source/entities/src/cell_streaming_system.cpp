#include <vanguard/entities/cell_streaming_system.hpp>

#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    namespace entity = vanguard::entities;

    template <typename Type, typename... Args> [[nodiscard]] Type* AllocateCellStreamingObject(const memory::PoolId pool, Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(pool, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template <typename Type> void DeleteCellStreamingObject(const memory::PoolId pool, Type* const object) noexcept
    {
        if (object == nullptr)
            return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), pool};
        memory::Free(block);
    }

    [[nodiscard]] resources::Failure ToResourceFailure(const world::Result result) noexcept
    {
        switch (result)
        {
        case world::Result::Success:
            return resources::Failure::None;
        case world::Result::InvalidMagic:
        case world::Result::InvalidLayout:
            return resources::Failure::DeserializationFailure;
        case world::Result::UnsupportedVersion:
            return resources::Failure::UnsupportedVersion;
        case world::Result::IntegrityFailure:
            return resources::Failure::IntegrityFailure;
        case world::Result::IoFailure:
            return resources::Failure::IoFailure;
        default:
            return resources::Failure::DeserializationFailure;
        }
    }

    [[nodiscard]] resources::ResourceObject* DecodeCell(const resources::ResourceReference reference, const void* const data, const usize size,
                                                        const resources::LoadContext& loadContext, resources::Failure& failure, void* const userData) noexcept
    {
        failure = resources::Failure::None;
        if (reference.ExpectedType() != world::CellResourceType || userData == nullptr)
        {
            failure = resources::Failure::UnknownType;
            return nullptr;
        }
        auto* const resource = AllocateCellStreamingObject<world::CellResource>(memory::PoolId::World);
        if (resource == nullptr)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        const auto* const decoderContext = static_cast<const entity::CellDecoderContext*>(userData);
        const world::Result result = resource->Open(data, size, decoderContext->limits);
        if (result == world::Result::Success && resource->BindDependencies(loadContext))
            return resource;
        failure = ToResourceFailure(result);
        if (result == world::Result::Success)
            failure = resources::Failure::DependencyFailure;
        DeleteCellStreamingObject(memory::PoolId::World, resource);
        return nullptr;
    }

    void DestroyCell(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteCellStreamingObject(memory::PoolId::World, static_cast<world::CellResource*>(resource));
    }

    [[nodiscard]] resources::Failure ToResourceFailure(const entity::Result result) noexcept
    {
        switch (result)
        {
        case entity::Result::OutOfMemory:
            return resources::Failure::OutOfMemory;
        case entity::Result::UnsupportedSchemaVersion:
            return resources::Failure::UnsupportedVersion;
        case entity::Result::SchemaFailure:
        case entity::Result::InvalidPrefab:
        case entity::Result::InvalidReference:
            return resources::Failure::DeserializationFailure;
        case entity::Result::MissingPrefab:
        case entity::Result::UnknownSchema:
            return resources::Failure::DependencyFailure;
        default:
            return resources::Failure::InternalError;
        }
    }
} // namespace

namespace vanguard::entities
{
    streaming::DecoderDescriptor MakeCellDecoder(CellDecoderContext& context) noexcept
    {
        return {world::CellResourceType, "Vanguard cell decoder", &DecodeCell, &DestroyCell, &context};
    }

    struct CellStreamingSystem::Impl
    {
        enum class Phase : u8
        {
            AwaitingActivation,
            AwaitingReferencePublication,
            PreparingRelease,
            Active,
            AwaitingRelease,
            FailureRelease
        };

        struct Operation
        {
            world::StreamingNodeKey key;
            u32 generation = 0;
            Phase phase = Phase::AwaitingActivation;
            resources::Failure failure = resources::Failure::None;
        };

        Impl() noexcept
            : operations(memory::pools::Streaming::GetInstance()), events(memory::pools::Streaming::GetInstance()),
              observers(memory::pools::Streaming::GetInstance()), completed(memory::pools::Streaming::GetInstance())
        {
            operations.Reserve(1024);
            events.Reserve(1024);
            observers.Reserve(world::MaximumStreamingObservers);
            completed.Reserve(1024);
        }

        [[nodiscard]] Operation* Find(const u64 cellId) const noexcept
        {
            Operation* operation = nullptr;
            return operations.Find(cellId, operation) ? operation : nullptr;
        }

        ComponentRegistry components;
        EntityReferenceRegistry references;
        CellMaterializer materializer;
        containers::HashMap<u64, Operation*> operations;
        containers::DynamicArray<world::StreamingResourceEvent> events;
        containers::DynamicArray<world::StreamingObserver> observers;
        containers::DynamicArray<u64> completed;
        world::StreamingProcessInput input;
        bool hasInput = false;
        bool failed = false;
        u64 queuedCells = 0;
        u64 activatedCells = 0;
        u64 releasedCells = 0;
        u64 cancelledCells = 0;
        u64 downstreamFailures = 0;
    };

    namespace
    {
        struct CellPrefabResolverContext
        {
            const world::CellResource* resource = nullptr;
            PrefabResolver fallback = nullptr;
            void* fallbackUserData = nullptr;
        };

        [[nodiscard]] const prefabs::PrefabFile* ResolveCellPrefab(const resources::ResourceReference reference, void* const userData) noexcept
        {
            const auto* const context = static_cast<const CellPrefabResolverContext*>(userData);
            if (context == nullptr || context->resource == nullptr)
                return nullptr;
            if (const prefabs::PrefabFile* const prefab = context->resource->ResolvePrefab(reference))
                return prefab;
            return context->fallback != nullptr ? context->fallback(reference, context->fallbackUserData) : nullptr;
        }
    } // namespace

    CellStreamingSystem::CellStreamingSystem(const CellStreamingSystemConfig& config) noexcept
        : RuntimeSystem({CellStreamingSystemId, "CellStreamingSystem", game::RuntimeSystemFlags::All}), m_config(config)
    {
    }

    CellStreamingSystem::~CellStreamingSystem()
    {
        if (m_impl == nullptr)
            return;
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
            DeleteCellStreamingObject(memory::PoolId::Streaming, iterator.Value());
        DeleteCellStreamingObject(memory::PoolId::Gameplay, m_impl);
        m_impl = nullptr;
    }

    bool CellStreamingSystem::OnInitialize(game::GameWorld& gameWorld) noexcept
    {
        if (m_impl != nullptr || m_config.executor == nullptr || !m_config.executor->IsInitialized() || m_config.componentDirectory == nullptr ||
            !m_config.componentDirectory->IsInitialized())
            return false;
        Impl* const impl = AllocateCellStreamingObject<Impl>(memory::PoolId::Gameplay);
        if (impl == nullptr || !impl->components.Initialize(gameWorld.GetEntities()) || !impl->references.Initialize(gameWorld.GetEntities()))
        {
            DeleteCellStreamingObject(memory::PoolId::Gameplay, impl);
            return false;
        }
        if ((m_config.registerComponents != nullptr && !m_config.registerComponents(impl->components, m_config.userData)) ||
            m_config.resources == nullptr || m_config.resourcePipeline == nullptr ||
            !impl->materializer.Initialize(impl->components, *m_config.componentDirectory, impl->references, *m_config.resources,
                                           *m_config.resourcePipeline,
                                           m_config.materialization))
        {
            static_cast<void>(impl->references.Shutdown());
            static_cast<void>(impl->components.Shutdown());
            DeleteCellStreamingObject(memory::PoolId::Gameplay, impl);
            return false;
        }
        m_impl = impl;
        return true;
    }

    void CellStreamingSystem::OnUninitialize(game::GameWorld&) noexcept
    {
        if (m_impl == nullptr)
            return;
        if (!m_impl->operations.Empty() || !m_impl->materializer.Shutdown() || !m_impl->references.Shutdown() || !m_impl->components.Shutdown())
        {
            m_impl->failed = true;
            return;
        }
        DeleteCellStreamingObject(memory::PoolId::Gameplay, m_impl);
        m_impl = nullptr;
    }

    bool CellStreamingSystem::SetProcessInput(const world::StreamingProcessInput& input) noexcept
    {
        if (m_impl == nullptr || input.observers.Size() > world::MaximumStreamingObservers || input.globalDistanceScale <= 0.0f)
            return false;
        m_impl->observers.Clear();
        for (const world::StreamingObserver& observer : input.observers)
            m_impl->observers.PushBack(observer);
        m_impl->input = input;
        m_impl->input.observers = containers::ArraySpan<const world::StreamingObserver>(m_impl->observers);
        m_impl->hasInput = true;
        return true;
    }

    void CellStreamingSystem::OnBeginFrame(game::GameWorld&, const f32) noexcept
    {
        if (m_impl == nullptr || m_impl->failed || !m_impl->hasInput || !m_config.executor->Process(m_impl->input, m_impl->events))
        {
            if (m_impl != nullptr && m_impl->hasInput)
                m_impl->failed = true;
            return;
        }
        for (const world::StreamingResourceEvent& event : m_impl->events)
        {
            if (event.key.kind != world::StreamingNodeKind::Cell)
            {
                if (m_config.forwardNonCellEvent != nullptr)
                    m_config.forwardNonCellEvent(event, m_config.userData);
                continue;
            }
            if (event.type == world::StreamingResourceEventType::ResourceAvailable)
            {
                const resources::ResourceHandle* const handle = m_config.executor->GetResource(event.key);
                const auto* const resource =
                    handle != nullptr && handle->GetType() == world::CellResourceType ? static_cast<const world::CellResource*>(handle->Get()) : nullptr;
                if (resource == nullptr || resource->GetFile().GetCellId() != event.key.id || m_impl->Find(event.key.id) != nullptr)
                {
                    m_impl->failed |= !m_config.executor->FailResident(event.key, resources::Failure::DeserializationFailure);
                    ++m_impl->downstreamFailures;
                    continue;
                }
                Impl::Operation* const operation = AllocateCellStreamingObject<Impl::Operation>(memory::PoolId::Streaming);
                if (operation == nullptr)
                {
                    m_impl->failed |= !m_config.executor->FailResident(event.key, resources::Failure::OutOfMemory);
                    ++m_impl->downstreamFailures;
                    continue;
                }
                operation->key = event.key;
                operation->generation = handle->GetGeneration();
                CellPrefabResolverContext resolver{resource, m_config.resolvePrefab, m_config.userData};
                const Result result = m_impl->materializer.QueueCell(resource->GetFile(), operation->generation, &ResolveCellPrefab, &resolver);
                if (result != Result::Success || !m_impl->operations.Insert(event.key.id, operation).IsSuccessful())
                {
                    if (result == Result::Success)
                        static_cast<void>(m_impl->materializer.Cancel(event.key.id, operation->generation));
                    DeleteCellStreamingObject(memory::PoolId::Streaming, operation);
                    m_impl->failed |= !m_config.executor->FailResident(event.key, ToResourceFailure(result));
                    ++m_impl->downstreamFailures;
                    continue;
                }
                ++m_impl->queuedCells;
            }
            else if (event.type == world::StreamingResourceEventType::ReleaseRequested)
            {
                Impl::Operation* const operation = m_impl->Find(event.key.id);
                if (operation == nullptr)
                {
                    m_impl->failed = true;
                    continue;
                }
                Result result = Result::InvalidState;
                const CellState state = m_impl->materializer.GetState(event.key.id);
                if (state == CellState::PendingActivation || state == CellState::PendingReferences)
                {
                    result = m_impl->materializer.Cancel(event.key.id, operation->generation);
                    ++m_impl->cancelledCells;
                }
                else if (state == CellState::Active || state == CellState::Failed)
                    result = m_impl->materializer.QueueRelease(event.key.id, operation->generation);
                if (result == Result::NotReady)
                {
                    operation->phase = Impl::Phase::PreparingRelease;
                    continue;
                }
                if (result != Result::Success)
                {
                    m_impl->failed = true;
                    continue;
                }
                if (m_impl->materializer.GetState(event.key.id) == CellState::Unknown)
                {
                    m_impl->completed.PushBack(event.key.id);
                    if (!m_config.executor->CompleteRelease(event.key))
                        m_impl->failed = true;
                }
                else
                    operation->phase = Impl::Phase::AwaitingRelease;
            }
        }
        for (const u64 cellId : m_impl->completed)
        {
            Impl::Operation* const operation = m_impl->Find(cellId);
            static_cast<void>(m_impl->operations.Remove(cellId));
            DeleteCellStreamingObject(memory::PoolId::Streaming, operation);
        }
        m_impl->completed.Clear();
    }

    void CellStreamingSystem::OnAfterWorldFlush(game::GameWorld&) noexcept
    {
        if (m_impl == nullptr || m_impl->failed)
            return;
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
        {
            Impl::Operation* const operation = iterator.Value();
            if (operation->phase != Impl::Phase::PreparingRelease)
                continue;
            const CellState state = m_impl->materializer.GetState(operation->key.id);
            const Result result = state == CellState::Active || state == CellState::Failed
                                      ? m_impl->materializer.QueueRelease(operation->key.id, operation->generation)
                                      : m_impl->materializer.Cancel(operation->key.id, operation->generation);
            if (result == Result::NotReady)
                continue;
            if (result != Result::Success)
            {
                m_impl->failed = true;
                return;
            }
            if (m_impl->materializer.GetState(operation->key.id) == CellState::Unknown)
            {
                m_impl->completed.PushBack(operation->key.id);
                if (!m_config.executor->CompleteRelease(operation->key))
                {
                    m_impl->failed = true;
                    return;
                }
            }
            else
                operation->phase = Impl::Phase::AwaitingRelease;
        }
        for (u32 pass = 0; pass < 2; ++pass)
        {
            for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
            {
                Impl::Operation* const operation = iterator.Value();
                if (operation->phase != Impl::Phase::AwaitingActivation)
                    continue;
                const Result result = m_impl->materializer.PrepareActivation(operation->key.id, operation->generation);
                if (result == Result::Success)
                {
                    operation->phase = Impl::Phase::AwaitingReferencePublication;
                }
                else if (result != Result::NotReady)
                {
                    operation->failure = ToResourceFailure(result);
                    if (m_impl->materializer.QueueRelease(operation->key.id, operation->generation) != Result::Success)
                    {
                        m_impl->failed = true;
                        return;
                    }
                    operation->phase = Impl::Phase::FailureRelease;
                }
            }
        }
        // No identity from this batch is published until every currently prepared cell has
        // finished component attachment.
        for (u32 pass = 0; pass < 2; ++pass)
        {
            for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
            {
                Impl::Operation* const operation = iterator.Value();
                if (operation->phase != Impl::Phase::AwaitingReferencePublication)
                    continue;
                const Result result = m_impl->materializer.PublishActivation(operation->key.id, operation->generation);
                if (result == Result::Success)
                {
                    operation->phase = Impl::Phase::Active;
                    ++m_impl->activatedCells;
                }
                else if (result != Result::NotReady)
                {
                    operation->failure = ToResourceFailure(result);
                    if (m_impl->materializer.QueueRelease(operation->key.id, operation->generation) != Result::Success)
                    {
                        m_impl->failed = true;
                        return;
                    }
                    operation->phase = Impl::Phase::FailureRelease;
                }
            }
        }
        // Incremental groups use the same coordinator-wide attach barrier as base-cell activation:
        // first finish every available group's component attachment, then publish identities.
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
        {
            Impl::Operation* const operation = iterator.Value();
            if (operation->phase != Impl::Phase::Active)
                continue;
            const Result groupResult = m_impl->materializer.PrepareActivationGroups(operation->key.id, operation->generation);
            if (groupResult != Result::Success && groupResult != Result::NotReady)
            {
                m_impl->failed = true;
                return;
            }
        }
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
        {
            Impl::Operation* const operation = iterator.Value();
            if (operation->phase != Impl::Phase::Active)
                continue;
            const Result groupResult = m_impl->materializer.PublishActivationGroups(operation->key.id, operation->generation);
            if (groupResult != Result::Success && groupResult != Result::NotReady)
            {
                m_impl->failed = true;
                return;
            }
        }
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
        {
            Impl::Operation* const operation = iterator.Value();
            if (operation->phase == Impl::Phase::Active)
            {
                const bool ready = m_impl->materializer.GetState(operation->key.id) == CellState::Active;
                if (!m_config.executor->SetReady(operation->key, ready))
                {
                    m_impl->failed = true;
                    return;
                }
                continue;
            }
            if (operation->phase != Impl::Phase::AwaitingRelease && operation->phase != Impl::Phase::FailureRelease)
                continue;
            const Result result = m_impl->materializer.CompleteRelease(operation->key.id, operation->generation);
            if (result == Result::NotReady)
                continue;
            if (result != Result::Success)
            {
                m_impl->failed = true;
                return;
            }
            const bool acknowledged = operation->phase == Impl::Phase::AwaitingRelease ? m_config.executor->CompleteRelease(operation->key)
                                                                                       : m_config.executor->FailResident(operation->key, operation->failure);
            if (!acknowledged)
            {
                m_impl->failed = true;
                return;
            }
            m_impl->completed.PushBack(operation->key.id);
            ++m_impl->releasedCells;
            if (operation->phase == Impl::Phase::FailureRelease)
                ++m_impl->downstreamFailures;
        }
        for (const u64 cellId : m_impl->completed)
        {
            Impl::Operation* const operation = m_impl->Find(cellId);
            static_cast<void>(m_impl->operations.Remove(cellId));
            DeleteCellStreamingObject(memory::PoolId::Streaming, operation);
        }
        m_impl->completed.Clear();
    }

    Result CellStreamingSystem::AcquireActivationGroup(const u64 cellId, const u64 groupId, const ActivationOwnerId ownerId) noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        Impl::Operation* const operation = m_impl->Find(cellId);
        if (operation == nullptr || operation->phase != Impl::Phase::Active)
            return Result::InvalidState;
        const resources::ResourceHandle* const handle = m_config.executor->GetResource(operation->key);
        const auto* const resource =
            handle != nullptr && handle->GetType() == world::CellResourceType ? static_cast<const world::CellResource*>(handle->Get()) : nullptr;
        if (resource == nullptr)
            return Result::InvalidState;
        CellPrefabResolverContext resolver{resource, m_config.resolvePrefab, m_config.userData};
        return m_impl->materializer.AcquireActivationGroup(resource->GetFile(), operation->generation, groupId, ownerId, &ResolveCellPrefab, &resolver);
    }

    Result CellStreamingSystem::ReleaseActivationGroup(const u64 cellId, const u64 groupId, const ActivationOwnerId ownerId) noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        Impl::Operation* const operation = m_impl->Find(cellId);
        if (operation == nullptr || operation->phase != Impl::Phase::Active)
            return Result::InvalidState;
        return m_impl->materializer.ReleaseActivationGroup(cellId, operation->generation, groupId, ownerId);
    }

    ComponentRegistry* CellStreamingSystem::GetComponents() noexcept
    {
        return m_impl != nullptr ? &m_impl->components : nullptr;
    }
    EntityReferenceRegistry* CellStreamingSystem::GetReferences() noexcept
    {
        return m_impl != nullptr ? &m_impl->references : nullptr;
    }
    CellMaterializer* CellStreamingSystem::GetMaterializer() noexcept
    {
        return m_impl != nullptr ? &m_impl->materializer : nullptr;
    }

    CellStreamingSystemStats CellStreamingSystem::GetStats() const noexcept
    {
        CellStreamingSystemStats stats;
        if (m_impl == nullptr)
            return stats;
        stats.trackedCells = m_impl->operations.Size();
        stats.queuedCells = m_impl->queuedCells;
        stats.activatedCells = m_impl->activatedCells;
        stats.releasedCells = m_impl->releasedCells;
        stats.cancelledCells = m_impl->cancelledCells;
        stats.downstreamFailures = m_impl->downstreamFailures;
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
        {
            const Impl::Operation* const operation = iterator.Value();
            stats.awaitingActivation += operation->phase == Impl::Phase::AwaitingActivation ||
                                        operation->phase == Impl::Phase::AwaitingReferencePublication;
            stats.activeCells += operation->phase == Impl::Phase::Active;
            stats.awaitingRelease += operation->phase == Impl::Phase::PreparingRelease || operation->phase == Impl::Phase::AwaitingRelease ||
                                     operation->phase == Impl::Phase::FailureRelease;
            stats.failedCells += operation->phase == Impl::Phase::FailureRelease;
        }
        return stats;
    }

    const char* CellStreamingSystem::ReadinessBlocker() const noexcept
    {
        if (m_impl == nullptr)
            return "cell streaming is not initialized";
        if (m_impl->failed)
            return "cell streaming lifecycle failure";
        for (auto iterator = m_impl->operations.Begin(), end = m_impl->operations.End(); iterator != end; ++iterator)
            if (iterator.Value()->phase == Impl::Phase::AwaitingActivation ||
                iterator.Value()->phase == Impl::Phase::AwaitingReferencePublication)
                return "streamed cells are awaiting activation";
        return nullptr;
    }
} // namespace vanguard::entities
