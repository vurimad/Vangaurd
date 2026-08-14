#include <vanguard/rendering/render_scene_collector.hpp>
#include <vanguard/rendering/render_scene_feedback.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 CollectorPageCapacity = 64;

        void ClearFailure(RenderSceneFailure* const failure) noexcept
        {
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneFailure* const failure, const RenderSceneFailureCode code,
                                const char* const message, const RenderSceneHandle scene = {},
                                const RenderProxyHandle proxy = {}) noexcept
        {
            if (failure != nullptr) *failure = {code, scene, proxy, message};
            return false;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        template <typename Packet> struct PacketPage
        {
            Packet packets[CollectorPageCapacity];
            u32 count = 0;
        };

        template <typename Packet> struct PacketPages
        {
            PacketPages() noexcept : pages(memory::pools::Rendering::GetInstance()) {}

            void Push(const Packet& packet) noexcept
            {
                if (pages.Size() == 0 || pages[pages.Size() - 1u].count == CollectorPageCapacity) pages.PushBack({});
                PacketPage<Packet>& page = pages[pages.Size() - 1u];
                page.packets[page.count++] = packet;
                ++count;
            }

            containers::DynamicArray<PacketPage<Packet>> pages;
            u32 count = 0;
        };

        [[nodiscard]] f32 DistanceToBounds(const f32* const cameraPosition,
                                           const RenderProxyBounds& bounds) noexcept
        {
            const f32 x = (bounds.minimum[0] + bounds.maximum[0]) * 0.5f - cameraPosition[0];
            const f32 y = (bounds.minimum[1] + bounds.maximum[1]) * 0.5f - cameraPosition[1];
            const f32 z = (bounds.minimum[2] + bounds.maximum[2]) * 0.5f - cameraPosition[2];
            return std::sqrt(x * x + y * y + z * z);
        }
    } // namespace

    ViewCollectionOutput::ViewCollectionOutput() noexcept
        : meshes(memory::pools::Rendering::GetInstance()),
          lights(memory::pools::Rendering::GetInstance()),
          decals(memory::pools::Rendering::GetInstance())
    {
    }

    struct RenderSceneCollector::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct ViewStateSlot
        {
            u32 generation = 0;
            ViewProxyState state;
        };

        struct ViewSlot
        {
            ViewSlot() noexcept : proxies(memory::pools::Rendering::GetInstance()) {}

            RenderSceneHandle scene;
            u32 generation = 0;
            bool alive = false;
            bool collectionInFlight = false;
            containers::DynamicArray<ViewStateSlot> proxies;
        };

        struct BatchOutput
        {
            PacketPages<MeshCollectorPacket> meshes;
            PacketPages<LightCollectorPacket> lights;
            PacketPages<DecalCollectorPacket> decals;
            VisibilityQueryResult visibility;
            bool succeeded = false;
        };

        struct Operation
        {
            Operation() noexcept
                : batches(memory::pools::Rendering::GetInstance()),
                  batchOutputs(memory::pools::Rendering::GetInstance()),
                  feedbackInputs(memory::pools::Rendering::GetInstance()),
                  feedbackObservations(memory::pools::Rendering::GetInstance())
            {
            }

            ViewCollectionRequest request;
            SceneReadLease retainedLease;
            containers::DynamicArray<VisibilityQueryBatch> batches;
            containers::DynamicArray<BatchOutput> batchOutputs;
            containers::DynamicArray<VisibilityProbeEvaluationInput> feedbackInputs;
            containers::DynamicArray<VisibilityProbeObservation> feedbackObservations;
            ViewCollectionOutput output;
            jobs::Counter completion;
            concurrency::Atomic<u32> finalized{0};
        };

        struct OperationSlot
        {
            Operation* operation = nullptr;
            u32 generation = 0;
        };

        explicit Impl(RenderSceneManager& sceneManager, const RenderSceneCollectorConfig& value) noexcept
            : scenes(&sceneManager), views(memory::pools::Rendering::GetInstance()),
              operations(memory::pools::Rendering::GetInstance()), collectName("RenderScene view collection"),
              emptyCollectName("RenderScene empty view collection")
        {
            views.Reserve(value.maximumViews);
            operations.Reserve(value.maximumCollections);
            for (u32 index = 0; index < value.maximumViews; ++index) views.PushBack({});
            for (u32 index = 0; index < value.maximumCollections; ++index) operations.PushBack({});
        }

        [[nodiscard]] ViewSlot* ResolveView(const RenderSceneViewHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.index >= views.Size()) return nullptr;
            ViewSlot& view = views[handle.index];
            return view.alive && view.generation == handle.generation ? &view : nullptr;
        }

        [[nodiscard]] const ViewSlot* ResolveView(const RenderSceneViewHandle handle) const noexcept
        {
            return const_cast<Impl*>(this)->ResolveView(handle);
        }

        [[nodiscard]] Operation* ResolveOperation(const RenderSceneCollectionHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.index >= operations.Size()) return nullptr;
            OperationSlot& slot = operations[handle.index];
            return slot.generation == handle.generation ? slot.operation : nullptr;
        }

        [[nodiscard]] const Operation* ResolveOperation(const RenderSceneCollectionHandle handle) const noexcept
        {
            return const_cast<Impl*>(this)->ResolveOperation(handle);
        }

        void UpdateViewState(ViewSlot& view, const RenderProxySnapshot& proxy, const f32 distance,
                             const u64 frameSerial, const u16 selectedLod, const f32 dissolve) noexcept
        {
            if (view.proxies.Size() <= proxy.handle.index) view.proxies.Resize(proxy.handle.index + 1u);
            ViewStateSlot& slot = view.proxies[proxy.handle.index];
            if (slot.generation != proxy.handle.generation)
            {
                slot = {};
                slot.generation = proxy.handle.generation;
                slot.state.proxy = proxy.handle;
            }
            slot.state.previousVisibleFrame = slot.state.lastVisibleFrame;
            slot.state.lastVisibleFrame = frameSerial;
            slot.state.distance = distance;
            slot.state.selectedLod = selectedLod;
            slot.state.dissolve = dissolve;
        }

        template <typename Packet>
        static void ReducePages(const PacketPages<Packet>& source, containers::DynamicArray<Packet>& target,
                                const u32 maximumPackets, u32& overflow) noexcept
        {
            for (u32 pageIndex = 0; pageIndex < source.pages.Size(); ++pageIndex)
            {
                const PacketPage<Packet>& page = source.pages[pageIndex];
                for (u32 packetIndex = 0; packetIndex < page.count; ++packetIndex)
                {
                    if (target.Size() < maximumPackets) target.PushBack(page.packets[packetIndex]);
                    else ++overflow;
                }
            }
        }

        template <typename Packet>
        void UpdateViewStatePages(ViewSlot& view, const PacketPages<Packet>& source,
                                  const u64 frameSerial) noexcept
        {
            for (u32 pageIndex = 0; pageIndex < source.pages.Size(); ++pageIndex)
            {
                const PacketPage<Packet>& page = source.pages[pageIndex];
                for (u32 packetIndex = 0; packetIndex < page.count; ++packetIndex)
                {
                    const Packet& packet = page.packets[packetIndex];
                    if constexpr (requires { packet.selectedLod; packet.dissolve; })
                        UpdateViewState(view, packet.proxy, packet.distance, frameSerial,
                                        packet.selectedLod, packet.dissolve);
                    else
                        UpdateViewState(view, packet.proxy, packet.distance, frameSerial, 0, 0.0f);
                }
            }
        }

        void Finalize(Operation& operation) noexcept
        {
            ViewCollectionResult& result = operation.output.result;
            result.view = operation.request.view;
            result.scene = operation.retainedLease.scene;
            result.version = operation.retainedLease.version;
            result.frameSerial = operation.request.frameSerial;
            result.batchCount = operation.batches.Size();
            result.succeeded = true;
            for (u32 batchIndex = 0; batchIndex < operation.batchOutputs.Size(); ++batchIndex)
            {
                const BatchOutput& batch = operation.batchOutputs[batchIndex];
                result.succeeded = result.succeeded && batch.succeeded;
                result.candidateProxies += batch.visibility.candidateProxies;
                ReducePages(batch.meshes, operation.output.meshes, operation.request.maximumMeshPackets,
                            result.overflowedMeshPackets);
                ReducePages(batch.lights, operation.output.lights, operation.request.maximumLightPackets,
                            result.overflowedLightPackets);
                ReducePages(batch.decals, operation.output.decals, operation.request.maximumDecalPackets,
                            result.overflowedDecalPackets);
            }

            result.meshPackets = operation.output.meshes.Size();
            result.lightPackets = operation.output.lights.Size();
            result.decalPackets = operation.output.decals.Size();
            {
                concurrency::ScopedLock<concurrency::RWLock> viewGuard(viewLock);
                ViewSlot* const view = ResolveView(operation.request.view);
                result.succeeded = result.succeeded && view != nullptr;
                if (view != nullptr)
                    for (u32 batchIndex = 0; batchIndex < operation.batchOutputs.Size(); ++batchIndex)
                    {
                        const BatchOutput& batch = operation.batchOutputs[batchIndex];
                        UpdateViewStatePages(*view, batch.meshes, operation.request.frameSerial);
                        UpdateViewStatePages(*view, batch.lights, operation.request.frameSerial);
                        UpdateViewStatePages(*view, batch.decals, operation.request.frameSerial);
                    }
                if (view != nullptr) view->collectionInFlight = false;
            }
            RenderSceneFailure ignored;
            if (!scenes->ReleaseReadLease(operation.retainedLease, &ignored)) result.succeeded = false;
            result.completed = true;
            VisibilityFeedbackService::Evaluate(operation.request, result, operation.feedbackInputs,
                                                operation.feedbackObservations);
            operation.finalized.SetValue(1);
        }

        void CollectBatch(Operation& operation, const u32 batchIndex) noexcept
        {
            BatchOutput& output = operation.batchOutputs[batchIndex];
            containers::DynamicArray<RenderProxyHandle> candidates(memory::pools::Rendering::GetInstance());
            RenderSceneFailure failure;
            if (!scenes->CollectVisibleProxyBatch(operation.request.visibility, operation.batches[batchIndex],
                                                   candidates, output.visibility, &failure))
                return;

            output.succeeded = true;
            for (u32 candidateIndex = 0; candidateIndex < candidates.Size(); ++candidateIndex)
            {
                const RenderProxyHandle proxy = candidates[candidateIndex];
                RenderProxySnapshot base;
                if (!scenes->ReadProxy(operation.retainedLease, proxy, base, &failure))
                {
                    output.succeeded = false;
                    continue;
                }
                const f32 distance = DistanceToBounds(operation.request.cameraPosition, base.bounds);
                switch (base.payloadKind)
                {
                case RenderProxyPayloadKind::Mesh:
                {
                    MeshCollectorPacket packet;
                    packet.proxy = base;
                    packet.distance = distance;
                    if (scenes->ReadMeshProxy(operation.retainedLease, proxy, packet.mesh, &failure)) output.meshes.Push(packet);
                    else output.succeeded = false;
                    break;
                }
                case RenderProxyPayloadKind::Light:
                {
                    LightCollectorPacket packet;
                    packet.proxy = base;
                    packet.distance = distance;
                    if (scenes->ReadLightProxy(operation.retainedLease, proxy, packet.light, &failure)) output.lights.Push(packet);
                    else output.succeeded = false;
                    break;
                }
                case RenderProxyPayloadKind::Decal:
                {
                    DecalCollectorPacket packet;
                    packet.proxy = base;
                    packet.distance = distance;
                    if (scenes->ReadDecalProxy(operation.retainedLease, proxy, packet.decal, &failure)) output.decals.Push(packet);
                    else output.succeeded = false;
                    break;
                }
                case RenderProxyPayloadKind::None:
                    break;
                }
            }
        }

        RenderSceneManager* scenes = nullptr;
        VisibilityFeedbackService* feedback = nullptr;
        mutable concurrency::RWLock viewLock;
        containers::DynamicArray<ViewSlot> views;
        containers::DynamicArray<OperationSlot> operations;
        jobs::JobName collectName;
        jobs::JobName emptyCollectName;
    };

    namespace
    {
        void DestroyOperation(RenderSceneCollector::Impl::Operation* const operation) noexcept
        {
            if (operation == nullptr) return;
            operation->~Operation();
            memory::MemoryBlock block{operation, sizeof(RenderSceneCollector::Impl::Operation), memory::PoolId::Rendering};
            memory::Free(block);
        }
    } // namespace

    RenderSceneCollector::~RenderSceneCollector()
    {
        if (m_impl != nullptr) static_cast<void>(Shutdown());
    }

    bool RenderSceneCollector::Initialize(RenderSceneManager& scenes, const RenderSceneCollectorConfig& config,
                                          RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr) return Fail(failure, RenderSceneFailureCode::AlreadyInitialized, "RenderSceneCollector is already initialized");
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderSceneCollector must initialize on the main thread");
        if (!scenes.IsInitialized() || !jobs::IsInitialized() || config.maximumViews == 0 ||
            config.maximumViews > MaximumRenderSceneViews || config.maximumCollections == 0 ||
            config.maximumCollections > MaximumRenderSceneCollections)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid RenderSceneCollector configuration");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block) return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderSceneCollector allocation failed");
        m_impl = ::new (block.address) Impl(scenes, config);
        return true;
    }

    bool RenderSceneCollector::Shutdown(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return true;
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderSceneCollector must shutdown on the main thread");
        if (m_impl->feedback != nullptr)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderSceneCollector shutdown requires its frame lifecycle to detach first");
        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
            if (m_impl->operations[index].operation != nullptr)
                return Fail(failure, RenderSceneFailureCode::Busy, "RenderSceneCollector shutdown is blocked by retained collections");
        for (u32 index = 0; index < m_impl->views.Size(); ++index)
            if (m_impl->views[index].alive)
                return Fail(failure, RenderSceneFailureCode::Busy, "RenderSceneCollector shutdown is blocked by live views");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool RenderSceneCollector::IsInitialized() const noexcept { return m_impl != nullptr; }

    bool RenderSceneCollector::CreateView(const RenderSceneHandle scene, RenderSceneViewHandle& view,
                                          RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        view = {};
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized", scene);
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "render view creation must run on the main thread", scene);
        if (!m_impl->scenes->IsAlive(scene)) return Fail(failure, RenderSceneFailureCode::InvalidHandle, "render view requires a live RenderScene", scene);
        concurrency::ScopedLock<concurrency::RWLock> viewGuard(m_impl->viewLock);
        for (u32 index = 0; index < m_impl->views.Size(); ++index)
        {
            Impl::ViewSlot& slot = m_impl->views[index];
            if (slot.alive) continue;
            slot.scene = scene;
            slot.generation = NextGeneration(slot.generation);
            slot.alive = true;
            slot.collectionInFlight = false;
            slot.proxies.Clear();
            view = {index, slot.generation};
            return true;
        }
        return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "render view capacity exceeded", scene);
    }

    bool RenderSceneCollector::DestroyView(const RenderSceneViewHandle view, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized");
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "render view destruction must run on the main thread");
        concurrency::ScopedLock<concurrency::RWLock> viewGuard(m_impl->viewLock);
        Impl::ViewSlot* const slot = m_impl->ResolveView(view);
        if (slot == nullptr) return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale render view handle");
        if (slot->collectionInFlight) return Fail(failure, RenderSceneFailureCode::Busy, "render view destruction is blocked by an in-flight collection", slot->scene);
        slot->alive = false;
        slot->scene = {};
        slot->proxies.Clear();
        return true;
    }

    bool RenderSceneCollector::ReadViewProxyState(const RenderSceneViewHandle view, const RenderProxyHandle proxy,
                                                  ViewProxyState& state, RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        state = {};
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized", proxy.scene, proxy);
        concurrency::ScopedSharedLock<concurrency::RWLock> viewGuard(m_impl->viewLock);
        const Impl::ViewSlot* const slot = m_impl->ResolveView(view);
        if (slot == nullptr || !(slot->scene == proxy.scene)) return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid render view or proxy scene", proxy.scene, proxy);
        if (slot->collectionInFlight) return Fail(failure, RenderSceneFailureCode::Busy, "view state is unavailable while collection is in flight", proxy.scene, proxy);
        if (proxy.index >= slot->proxies.Size() || slot->proxies[proxy.index].generation != proxy.generation)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "view has no state for the proxy generation", proxy.scene, proxy);
        state = slot->proxies[proxy.index].state;
        return true;
    }

    bool RenderSceneCollector::Dispatch(const ViewCollectionRequest& request,
                                        RenderSceneCollectionHandle& collection,
                                        RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        collection = {};
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized", request.visibility.lease.scene);
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "view collection dispatch must run on the main thread", request.visibility.lease.scene);
        concurrency::ScopedLock<concurrency::RWLock> viewGuard(m_impl->viewLock);
        Impl::ViewSlot* const view = m_impl->ResolveView(request.view);
        if (view == nullptr || !(view->scene == request.visibility.lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "view collection request has an invalid view/scene pairing", request.visibility.lease.scene);
        if (view->collectionInFlight) return Fail(failure, RenderSceneFailureCode::Busy, "render view already has an in-flight collection", view->scene);
        if (!request.visibility.lease.IsValid() || request.frameSerial == 0 || request.targetCellsPerJob == 0 ||
            !std::isfinite(request.cameraPosition[0]) || !std::isfinite(request.cameraPosition[1]) ||
            !std::isfinite(request.cameraPosition[2]))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid view collection request", view->scene);

        u32 slotIndex = m_impl->operations.Size();
        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
            if (m_impl->operations[index].operation == nullptr) { slotIndex = index; break; }
        if (slotIndex == m_impl->operations.Size()) return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "view collection capacity exceeded", view->scene);

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl::Operation), alignof(Impl::Operation));
        if (!block) return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "view collection allocation failed", view->scene);
        Impl::Operation* const operation = ::new (block.address) Impl::Operation();
        operation->request = request;
        if (m_impl->feedback != nullptr &&
            !m_impl->feedback->Capture(request, operation->feedbackInputs, failure))
        {
            DestroyOperation(operation);
            return false;
        }
        if (!m_impl->scenes->AcquireReadLease(request.visibility.lease.scene, request.visibility.lease.version,
                                              operation->retainedLease, failure))
        {
            DestroyOperation(operation);
            return false;
        }
        operation->request.visibility.lease = operation->retainedLease;
        operation->request.visibility.maximumResults = ~u32{0};
        VisibilityQueryPlan plan;
        if (!m_impl->scenes->BuildVisibilityQueryPlan(operation->retainedLease, request.targetCellsPerJob,
                                                      operation->batches, plan, failure))
        {
            static_cast<void>(m_impl->scenes->ReleaseReadLease(operation->retainedLease));
            DestroyOperation(operation);
            return false;
        }
        operation->batchOutputs.Resize(operation->batches.Size());

        Impl::OperationSlot& slot = m_impl->operations[slotIndex];
        slot.generation = NextGeneration(slot.generation);
        slot.operation = operation;
        collection = {slotIndex, slot.generation};
        operation->output.result.collection = collection;
        view->collectionInFlight = true;

        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, operation);
        bool dispatched = false;
        if (operation->batches.Size() != 0)
        {
            jobs::ParallelTask collectTask = jobs::ParallelTask::Create(
                [impl = m_impl, operation](const u32 batchIndex, const jobs::JobContext&) noexcept
                { impl->CollectBatch(*operation, batchIndex); });
            jobs::Task epilogue = jobs::Task::Create(
                [impl = m_impl, operation](const jobs::JobContext&) noexcept { impl->Finalize(*operation); });
            dispatched = builder.IsValid() && collectTask && epilogue &&
                         builder.DispatchParallel(m_impl->collectName, operation->batches.Size(),
                                                  static_cast<jobs::ParallelTask&&>(collectTask),
                                                  static_cast<jobs::Task&&>(epilogue), 1);
        }
        else
        {
            jobs::Task task = jobs::Task::Create(
                [impl = m_impl, operation](const jobs::JobContext&) noexcept { impl->Finalize(*operation); });
            dispatched = builder.IsValid() && task &&
                         builder.Dispatch(m_impl->emptyCollectName, static_cast<jobs::Task&&>(task));
        }
        if (!dispatched)
        {
            view->collectionInFlight = false;
            slot.operation = nullptr;
            collection = {};
            static_cast<void>(m_impl->scenes->ReleaseReadLease(operation->retainedLease));
            DestroyOperation(operation);
            return Fail(failure, RenderSceneFailureCode::Busy, "view collection Jobs dispatch failed", view->scene);
        }
        operation->completion = builder.ExtractCounter();
        return true;
    }

    const jobs::Counter* RenderSceneCollector::Completion(const RenderSceneCollectionHandle collection) const noexcept
    {
        const Impl::Operation* const operation = m_impl != nullptr ? m_impl->ResolveOperation(collection) : nullptr;
        return operation != nullptr ? &operation->completion : nullptr;
    }

    bool RenderSceneCollector::IsReady(const RenderSceneCollectionHandle collection) const noexcept
    {
        const Impl::Operation* const operation = m_impl != nullptr ? m_impl->ResolveOperation(collection) : nullptr;
        return operation != nullptr && operation->completion.IsReady();
    }

    bool RenderSceneCollector::Wait(const RenderSceneCollectionHandle collection, const i32 timeoutMilliseconds) const noexcept
    {
        const Impl::Operation* const operation = m_impl != nullptr ? m_impl->ResolveOperation(collection) : nullptr;
        return operation != nullptr && operation->completion.Wait(false, timeoutMilliseconds);
    }

    bool RenderSceneCollector::CopyOutput(const RenderSceneCollectionHandle collection, ViewCollectionOutput& output,
                                          RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        output.result = {};
        output.meshes.Clear();
        output.lights.Clear();
        output.decals.Clear();
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized");
        const Impl::Operation* const operation = m_impl->ResolveOperation(collection);
        if (operation == nullptr) return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale view collection handle");
        if (!operation->completion.IsReady() || operation->finalized.GetValue() == 0)
            return Fail(failure, RenderSceneFailureCode::Busy, "view collection output is not complete", operation->retainedLease.scene);
        output.result = operation->output.result;
        output.result.collection = collection;
        output.meshes = operation->output.meshes;
        output.lights = operation->output.lights;
        output.decals = operation->output.decals;
        return true;
    }

    bool RenderSceneCollector::Release(RenderSceneCollectionHandle& collection,
                                       RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!collection.IsValid()) return true;
        if (m_impl == nullptr) return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneCollector is not initialized");
        if (!concurrency::IsMainThread()) return Fail(failure, RenderSceneFailureCode::WrongThread, "view collection release must run on the main thread");
        if (m_impl->feedback != nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "frame-lifecycle collections retire only through RenderScene end frame");
        if (collection.index >= m_impl->operations.Size()) return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid view collection handle");
        Impl::OperationSlot& slot = m_impl->operations[collection.index];
        if (slot.generation != collection.generation || slot.operation == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale view collection handle");
        if (!slot.operation->completion.IsReady() || slot.operation->finalized.GetValue() == 0)
            return Fail(failure, RenderSceneFailureCode::Busy, "view collection cannot be released before completion");
        DestroyOperation(slot.operation);
        slot.operation = nullptr;
        collection = {};
        return true;
    }

    bool RenderSceneCollector::InspectRetirement(const u64 frameSerial, u32& eligibleCollections,
                                                 u32& pendingCollections,
                                                 RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        eligibleCollections = 0;
        pendingCollections = 0;
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneCollector is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "collection retirement inspection must run on the main thread");
        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
        {
            const Impl::Operation* const operation = m_impl->operations[index].operation;
            if (operation == nullptr || operation->request.frameSerial > frameSerial) continue;
            ++eligibleCollections;
            if (!operation->completion.IsReady() || operation->finalized.GetValue() == 0)
                ++pendingCollections;
        }
        return true;
    }

    bool RenderSceneCollector::RetireThroughFrame(const u64 frameSerial, VisibilityFeedbackService& feedback,
                                                  u32& retiredCollections,
                                                  RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        retiredCollections = 0;
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneCollector is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "collection retirement must run on the main thread");

        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
        {
            Impl::OperationSlot& slot = m_impl->operations[index];
            Impl::Operation* const operation = slot.operation;
            if (operation == nullptr || operation->request.frameSerial > frameSerial) continue;
            if (!operation->completion.IsReady() || operation->finalized.GetValue() == 0)
                return Fail(failure, RenderSceneFailureCode::Busy,
                            "collection retirement requires completed Jobs work",
                            operation->output.result.scene);
            if (!feedback.Accumulate(operation->feedbackObservations, operation->output.result, failure))
                return false;
        }

        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
        {
            Impl::OperationSlot& slot = m_impl->operations[index];
            Impl::Operation* const operation = slot.operation;
            if (operation == nullptr || operation->request.frameSerial > frameSerial) continue;
            DestroyOperation(operation);
            slot.operation = nullptr;
            ++retiredCollections;
        }
        return true;
    }

    void RenderSceneCollector::AttachFeedback(VisibilityFeedbackService* const feedback) noexcept
    {
        if (m_impl != nullptr) m_impl->feedback = feedback;
    }
} // namespace vanguard::rendering
