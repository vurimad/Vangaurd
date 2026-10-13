#include <vanguard/vellum/runtime.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/vellum/instance_storage.hpp>
#include <vanguard/vellum/runtime_internal.hpp>
#include <vanguard/vellum/spawn_storage.hpp>

#include <new>

namespace vanguard::vellum
{
    namespace
    {
        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool IsValidConfig(const RuntimeConfig& config) noexcept
        {
            constexpr u64 minimumCookedDocumentBytes =
                detail::CanonicalTreeOffset + detail::TreeBodyHeaderWireSize + detail::TreeNodeWireSize;
            return config.maximumInstances != 0 && config.maximumInstances < InvalidIndex && config.maximumNodesPerDocument != 0 &&
                   config.maximumNodesPerDocument < InvalidIndex && config.maximumTotalNodes >= config.maximumNodesPerDocument &&
                   config.maximumTotalNodes < InvalidIndex && config.maximumHierarchyDepth != 0 &&
                   config.maximumHierarchyDepth <= config.maximumNodesPerDocument &&
                   static_cast<u64>(config.maximumCookedDocumentBytes) >= minimumCookedDocumentBytes &&
                   config.maximumCookedPropertiesPerDocument != 0 &&
                   config.maximumCookedPropertiesPerDocument < InvalidIndex && config.maximumStructuralCommandsPerInstance != 0 &&
                   config.maximumStructuralCommandsPerInstance < InvalidIndex &&
                   config.maximumConcurrentSpawns != 0 && config.maximumConcurrentSpawns < InvalidIndex &&
                   config.maximumMaterializedNodesPerUpdate != 0 && config.maximumMaterializedNodesPerUpdate <= config.maximumNodesPerDocument;
        }

        [[nodiscard]] bool IsOwnerThread(const Runtime::Impl& impl) noexcept
        {
            return impl.ownerThread.IsValid() && impl.ownerThread == concurrency::ThreadId::GetCurrentThread();
        }

        [[nodiscard]] Runtime::Impl* AllocateImpl() noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Runtime, sizeof(Runtime::Impl), alignof(Runtime::Impl));
            return block ? ::new (block.address) Runtime::Impl() : nullptr;
        }

        void DeleteImpl(Runtime::Impl* const impl) noexcept
        {
            if (impl == nullptr)
                return;
            impl->~Impl();
            memory::MemoryBlock block{impl, sizeof(Runtime::Impl), memory::PoolId::Runtime};
            memory::Free(block);
        }
    } // namespace

    Runtime::~Runtime()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "Vellum runtime must be shut down before destruction");
    }

    bool Runtime::Initialize(streaming::ResourceStreamer& streamer, const RuntimeConfig& config, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, FailureCode::AlreadyInitialized, "Vellum runtime is already initialized");
        if (!streamer.IsInitialized())
            return Fail(failure, FailureCode::InvalidDependency, "Vellum runtime requires an initialized ResourceStreamer");
        if (!IsValidConfig(config))
            return Fail(failure, FailureCode::InvalidConfiguration, "Vellum runtime configuration is invalid");

        Impl* const impl = AllocateImpl();
        if (impl == nullptr)
            return Fail(failure, FailureCode::OutOfMemory, "Vellum runtime allocation failed");
        impl->streamer = &streamer;
        impl->ownerThread = concurrency::ThreadId::GetCurrentThread();
        impl->config = config;
        impl->stats.instanceCapacity = config.maximumInstances;
        impl->stats.totalNodeCapacity = config.maximumTotalNodes;
        if (!detail::InitializeInstanceRegistry(*impl, failure))
        {
            DeleteImpl(impl);
            return false;
        }
        if (!detail::InitializeSpawnRegistry(*impl, failure))
        {
            detail::DestroyInstanceRegistry(*impl);
            DeleteImpl(impl);
            return false;
        }
        impl->documentDecoderState.maximumDocumentBytes = config.maximumCookedDocumentBytes;
        impl->documentDecoderState.maximumNodes = config.maximumNodesPerDocument;
        impl->documentDecoderState.maximumProperties = config.maximumCookedPropertiesPerDocument;
        impl->documentDecoderState.maximumHierarchyDepth = config.maximumHierarchyDepth;
        impl->documentDecoder = {DocumentResourceType, "Vellum document", &detail::DecodeDocumentResource,
                                 &detail::DestroyDocumentResource, &impl->documentDecoderState};
        if (!streamer.RegisterDecoder(impl->documentDecoder))
        {
            detail::DestroySpawnRegistry(*impl);
            detail::DestroyInstanceRegistry(*impl);
            DeleteImpl(impl);
            return Fail(failure, FailureCode::ResourceFailure, "Vellum document decoder registration failed");
        }
        impl->documentDecoderRegistered = true;
        impl->stats.state = RuntimeState::Active;
        impl->stats.acceptingWork = true;
        m_impl = impl;
        return true;
    }

    bool Runtime::BeginSemanticUpdate(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum semantic update must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot begin a semantic update in its current state");
        if (m_impl->stats.semanticEpoch == ~u64{0})
            return Fail(failure, FailureCode::CapacityExceeded, "Vellum semantic epoch space is exhausted");

        if (m_impl->structuralCommitActive)
            return Fail(failure, FailureCode::InvalidState, "Vellum cannot advance semantic epoch during structural commit");

        for (u32 index = 0; index < m_impl->config.maximumInstances; ++index)
        {
            detail::InstanceSlot& slot = m_impl->instanceSlots[index];
            if (slot.instance != nullptr && (slot.state == detail::SlotState::Live || slot.state == detail::SlotState::ReservedCreate))
                detail::CompactDirtyWork(*m_impl, *slot.instance);
        }
        detail::PromoteDeferredInstances(*m_impl);
        for (u32 index = 0; index < m_impl->config.maximumInstances; ++index)
        {
            detail::InstanceSlot& slot = m_impl->instanceSlots[index];
            if (slot.instance != nullptr && (slot.state == detail::SlotState::Live || slot.state == detail::SlotState::ReservedCreate))
                detail::PromoteDeferredNodes(*m_impl, *slot.instance);
        }
        detail::PromoteDeferredSpawns(*m_impl);
        ++m_impl->stats.semanticEpoch;
        m_impl->semanticUpdateActive = true;
        bool progressed = true;
        if (m_impl->stats.state == RuntimeState::Active)
            progressed = detail::ProgressSpawns(*m_impl, failure);
        else
            progressed = detail::QuiesceSpawns(*m_impl, failure);
        m_impl->semanticUpdateActive = false;
        return progressed;
    }

    bool Runtime::RequestSpawn(const SpawnDesc& desc, SpawnRequest& request, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum spawn admission must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active || !m_impl->stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting spawn work");
        if (request.IsValid())
            return Fail(failure, FailureCode::InvalidArgument, "Vellum spawn output request already owns an operation");
        SpawnHandle handle;
        if (!detail::RequestSpawn(*m_impl, desc, handle, failure))
            return false;
        request = SpawnRequest(&m_impl->spawnSlots[handle.index], handle);
        return true;
    }

    bool Runtime::CancelSpawn(SpawnRequest& request, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum spawn cancellation must run on its owner thread");
        if (m_impl->structuralCommitActive)
            return Fail(failure, FailureCode::InvalidState, "Vellum spawn cancellation cannot run during structural commit");
        if (!request.IsValid() || request.m_handle.index >= m_impl->config.maximumConcurrentSpawns ||
            request.m_control != &m_impl->spawnSlots[request.m_handle.index])
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn cancellation references another or stale runtime");
        SpawnState outcome = SpawnState::Invalid;
        return detail::CancelSpawn(*m_impl, request.m_handle, outcome, failure);
    }

    bool Runtime::GetSpawnInfo(const SpawnRequest& request, SpawnInfo& info, Failure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum spawn query must run on its owner thread");
        if (!request.IsValid() || request.m_handle.index >= m_impl->config.maximumConcurrentSpawns ||
            request.m_control != &m_impl->spawnSlots[request.m_handle.index])
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn query references another or stale runtime");
        return detail::GetSpawnInfo(*m_impl, request.m_handle, info, failure);
    }

    bool Runtime::TakeSpawnResult(SpawnRequest& request, VellumInstanceHandle& instance, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        instance = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum spawn result transfer must run on its owner thread");
        if (!request.IsValid() || request.m_handle.index >= m_impl->config.maximumConcurrentSpawns ||
            request.m_control != &m_impl->spawnSlots[request.m_handle.index])
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn result references another or stale runtime");
        if (!detail::TakeSpawnResult(*m_impl, request.m_handle, instance, failure))
            return false;
        request.m_control = nullptr;
        request.m_handle = {};
        return true;
    }

    bool Runtime::ReleaseInstance(const VellumInstanceHandle instance, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum instance release must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot release instances in its current state");
        return detail::RetireInstance(*m_impl, instance, failure);
    }

    bool Runtime::QueueCreate(const VellumInstanceHandle instanceHandle, const NodeCreateDesc& desc, VellumNodeHandle& node,
                              Failure* const failure) noexcept
    {
        ClearFailure(failure);
        node = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum mutation admission must run on its owner thread");
        detail::InstanceState* const instance = detail::ResolveInstance(*m_impl, instanceHandle);
        if (instance == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum create references a stale instance");
        return detail::QueueCreate(*m_impl, *instance, desc, node, failure);
    }

    bool Runtime::QueueDestroy(const VellumNodeHandle node, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum mutation admission must run on its owner thread");
        detail::InstanceState* const instance = detail::ResolveInstance(*m_impl, node.instance);
        if (instance == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum destroy references a stale instance");
        return detail::QueueDestroy(*m_impl, *instance, node.local, failure);
    }

    bool Runtime::QueueReparent(const VellumNodeHandle node, const VellumNodeHandle parent, const VellumNodeHandle beforeSibling,
                                Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum mutation admission must run on its owner thread");
        if ((parent.IsValid() && parent.instance != node.instance) || (beforeSibling.IsValid() && beforeSibling.instance != node.instance))
            return Fail(failure, FailureCode::CrossInstance, "Vellum reparent cannot cross instance ownership");
        detail::InstanceState* const instance = detail::ResolveInstance(*m_impl, node.instance);
        if (instance == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum reparent references a stale instance");
        return detail::QueueReparent(*m_impl, *instance, node.local, parent.IsValid() ? parent.local : LocalNodeHandle{},
                                     beforeSibling.IsValid() ? beforeSibling.local : LocalNodeHandle{}, failure);
    }

    bool Runtime::CommitStructuralMutations(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum structural commit must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot commit structural work in its current state");
        if (m_impl->structuralCommitActive)
            return Fail(failure, FailureCode::InvalidState, "Vellum structural commit is non-reentrant");
        const u64 fence = m_impl->nextMutationSequence != 0 ? m_impl->nextMutationSequence - 1u : 0;
        m_impl->structuralCommitActive = true;
        const bool committed = detail::CommitStructural(*m_impl, fence, failure);
        m_impl->structuralCommitActive = false;
        return committed;
    }

    bool Runtime::ResolveProperty(const VellumNodeHandle node, const PropertyId id, PropertyHandle& property, Failure* const failure) const noexcept
    {
        ClearFailure(failure);
        property = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum property resolution must run on its owner thread");
        return detail::ResolveProperty(*m_impl, node, id, property, failure);
    }

    bool Runtime::WriteProperty(const PropertyHandle property, const f32 value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum property write must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active || !m_impl->stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting property writes");
        return detail::WriteProperty(*m_impl, property, value, failure);
    }

    bool Runtime::WriteProperty(const PropertyHandle property, const Visibility value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum property write must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active || !m_impl->stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting property writes");
        return detail::WriteProperty(*m_impl, property, value, failure);
    }

    bool Runtime::WriteProperty(const PropertyHandle property, const Orientation value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum property write must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active || !m_impl->stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting property writes");
        return detail::WriteProperty(*m_impl, property, value, failure);
    }

    bool Runtime::MeasureInstance(const VellumInstanceHandle instance, const LayoutConstraints& constraints, Size& desired,
                                  Failure* const failure) noexcept
    {
        ClearFailure(failure);
        desired = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum measure must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot measure in its current state");
        return detail::MeasureInstance(*m_impl, instance, constraints, desired, failure);
    }

    bool Runtime::ArrangeInstance(const VellumInstanceHandle instance, const Rect& rect, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum arrange must run on its owner thread");
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot arrange in its current state");
        return detail::ArrangeInstance(*m_impl, instance, rect, failure);
    }

    bool Runtime::GetGeometry(const VellumNodeHandle node, Geometry& geometry, Failure* const failure) const noexcept
    {
        ClearFailure(failure);
        geometry = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum geometry query must run on its owner thread");
        return detail::GetGeometry(*m_impl, node, geometry, failure);
    }

    bool Runtime::GetInstanceInfo(const VellumInstanceHandle instance, InstanceInfo& info, Failure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum instance query must run on its owner thread");
        if (!detail::GetInstanceInfo(*m_impl, instance, info))
            return Fail(failure, FailureCode::StaleHandle, "Vellum instance query references a stale instance");
        return true;
    }

    bool Runtime::GetNodeInfo(const VellumNodeHandle node, NodeInfo& info, Failure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum node query must run on its owner thread");
        if (!detail::GetNodeInfo(*m_impl, node, info))
            return Fail(failure, FailureCode::StaleHandle, "Vellum node query references a stale node");
        return true;
    }

    bool Runtime::Quiesce(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum runtime quiesce must run on its semantic owner thread");
        if (m_impl->stats.state == RuntimeState::Drained)
            return true;
        if (m_impl->stats.state != RuntimeState::Active && m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot quiesce from its current state");
        if (m_impl->structuralCommitActive)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot quiesce during structural commit");
        m_impl->stats.acceptingWork = false;
        m_impl->stats.state = RuntimeState::Quiescing;
        if (!detail::QuiesceSpawns(*m_impl, failure))
            return false;
        for (u32 index = 0; index < m_impl->config.maximumInstances; ++index)
        {
            detail::InstanceSlot& slot = m_impl->instanceSlots[index];
            if (slot.instance == nullptr || (slot.state != detail::SlotState::Live && slot.state != detail::SlotState::ReservedCreate))
                continue;
            const VellumInstanceHandle handle{index, slot.generation};
            if (!detail::RetireInstance(*m_impl, handle, failure))
                return false;
        }
        return true;
    }

    bool Runtime::Drain(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, FailureCode::NotInitialized, "Vellum runtime is not initialized");
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum runtime drain must run on its semantic owner thread");
        if (m_impl->stats.state == RuntimeState::Drained)
            return true;
        if (m_impl->stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime must quiesce before drain");
        if (!detail::QuiesceSpawns(*m_impl, failure))
            return false;
        if (m_impl->stats.liveInstances != 0 || m_impl->stats.liveSpawns != 0 || m_impl->stats.pendingStructuralCommands != 0 ||
            m_impl->stats.terminalStructuralCommands != 0)
            return Fail(failure, FailureCode::LiveWorkRemains, "Vellum runtime drain still has live or terminal work");
        m_impl->stats.state = RuntimeState::Drained;
        return true;
    }

    bool Runtime::Shutdown(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!IsOwnerThread(*m_impl))
            return Fail(failure, FailureCode::WrongThread, "Vellum runtime shutdown must run on its semantic owner thread");
        if (m_impl->stats.state != RuntimeState::Drained)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime must quiesce and drain before shutdown");
        if (m_impl->stats.liveInstances != 0 || m_impl->stats.liveSpawns != 0 || m_impl->stats.pendingStructuralCommands != 0 ||
            m_impl->stats.terminalStructuralCommands != 0)
            return Fail(failure, FailureCode::LiveWorkRemains, "Vellum runtime shutdown requires zero live and terminal work");
        if (m_impl->documentDecoderRegistered)
        {
            if (m_impl->streamer == nullptr || !m_impl->streamer->UnregisterDecoder(DocumentResourceType))
                return Fail(failure, FailureCode::ResourceFailure, "Vellum document decoder is still busy");
            m_impl->documentDecoderRegistered = false;
        }

        m_impl->stats.acceptingWork = false;
        m_impl->stats.state = RuntimeState::Uninitialized;
        detail::DestroySpawnRegistry(*m_impl);
        detail::DestroyInstanceRegistry(*m_impl);
        DeleteImpl(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool Runtime::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->stats.state != RuntimeState::Uninitialized;
    }

    RuntimeStats Runtime::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        RuntimeStats stats = m_impl->stats;
        stats.liveDecodedDocuments = m_impl->documentDecoderState.liveDocuments.GetValue();
        stats.peakDecodedDocuments = m_impl->documentDecoderState.peakDocuments.GetValue();
        return stats;
    }
} // namespace vanguard::vellum
