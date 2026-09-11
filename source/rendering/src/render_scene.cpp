#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_scene_gpu.hpp>
#include <vanguard/rendering/render_scene_gpu_read.hpp>

#include <vanguard/rendering/render_scene_spatial.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <limits>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(RenderSceneFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneFailure* const failure, const RenderSceneFailureCode code, const char* const message, const RenderSceneHandle scene = {}, const RenderProxyHandle proxy = {}) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->scene = scene;
                failure->proxy = proxy;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool ValidMode(const RenderSceneMode mode) noexcept
        {
            return static_cast<u32>(mode) <= static_cast<u32>(RenderSceneMode::Thumbnail);
        }

        [[nodiscard]] bool ValidOwnership(const RenderSceneOwnership ownership) noexcept
        {
            return static_cast<u32>(ownership) <= static_cast<u32>(RenderSceneOwnership::External);
        }

        [[nodiscard]] bool ValidSpatialMode(const RenderProxySpatialMode mode) noexcept
        {
            return static_cast<u32>(mode) <= static_cast<u32>(RenderProxySpatialMode::Global);
        }

        [[nodiscard]] bool CopyName(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < capacity && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        void CopyNameUnchecked(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            u32 index = 0;
            while (index + 1u < capacity && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            destination[index] = '\0';
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] bool ValidBounds(const RenderProxyBounds& bounds) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) || bounds.minimum[axis] > bounds.maximum[axis])
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidTransform(const RenderProxyTransform& transform) noexcept
        {
            for (u32 index = 0; index < 4; ++index)
            {
                if (!std::isfinite(transform.row0[index]) || !std::isfinite(transform.row1[index]) || !std::isfinite(transform.row2[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool SameTransform(const RenderProxyTransform& left, const RenderProxyTransform& right) noexcept
        {
            for (u32 component = 0; component < 4; ++component)
                if (left.row0[component] != right.row0[component] || left.row1[component] != right.row1[component] || left.row2[component] != right.row2[component])
                    return false;
            return true;
        }

        [[nodiscard]] bool SameBounds(const RenderProxyBounds& left, const RenderProxyBounds& right) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
                if (left.minimum[axis] != right.minimum[axis] || left.maximum[axis] != right.maximum[axis])
                    return false;
            return true;
        }

        [[nodiscard]] bool ValidSpatialConfig(const SpatialWriteIndexConfig& config) noexcept
        {
            if (!std::isfinite(config.cellSize) || config.cellSize <= 0.0f)
                return false;
            const bool anyExtent = config.cellsPerAxis[0] != 0 || config.cellsPerAxis[1] != 0 || config.cellsPerAxis[2] != 0;
            if (anyExtent && !config.HasFiniteExtent())
                return false;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(config.origin[axis]))
                    return false;
                if (config.HasFiniteExtent())
                {
                    const f64 extent = static_cast<f64>(config.cellsPerAxis[axis]) * config.cellSize;
                    if (!std::isfinite(extent) || static_cast<f64>(config.origin[axis]) + extent > std::numeric_limits<f32>::max())
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ValidFrustum(const VisibilityFrustum& frustum) noexcept
        {
            if (frustum.planeCount == 0 || frustum.planeCount > MaximumVisibilityFrustumPlanes)
                return false;
            for (u32 planeIndex = 0; planeIndex < frustum.planeCount; ++planeIndex)
            {
                const VisibilityPlane& plane = frustum.planes[planeIndex];
                if (!std::isfinite(plane.normal[0]) || !std::isfinite(plane.normal[1]) || !std::isfinite(plane.normal[2]) || !std::isfinite(plane.distance))
                    return false;
                const f32 magnitudeSquared = plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] + plane.normal[2] * plane.normal[2];
                if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= 0.0f)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidLightProperties(const RenderLightKind kind, const f32* const color, const f32 intensity, const f32 range, const f32 innerCone, const f32 outerCone) noexcept
        {
            return ValidRenderLight(kind, color, intensity, range, innerCone, outerCone);
        }

        [[nodiscard]] RenderProxyBounds LightBounds(const RenderProxyTransform& transform, const RenderLightKind kind, const f32 range) noexcept
        {
            // Local lights have a world-space spherical cutoff; orientation and
            // transform scale do not shrink the conservative influence bounds.
            const f32 radius = kind == RenderLightKind::Directional ? 0.1f : range;
            return {{transform.row0[3] - radius, transform.row1[3] - radius, transform.row2[3] - radius},
                    {transform.row0[3] + radius, transform.row1[3] + radius, transform.row2[3] + radius}};
        }

        [[nodiscard]] bool SameResourceHandle(const resources::ResourceHandle& left, const resources::ResourceHandle& right) noexcept
        {
            if (left.IsValid() != right.IsValid())
                return false;
            return !left.IsValid() || (left.GetPath() == right.GetPath() && left.GetType() == right.GetType() && left.GetGeneration() == right.GetGeneration());
        }

        [[nodiscard]] constexpr bool HasField(const MeshProxyUpdateFields fields, const MeshProxyUpdateFields field) noexcept
        {
            return (fields & field) != MeshProxyUpdateFields::None;
        }

        [[nodiscard]] constexpr bool HasField(const LightProxyUpdateFields fields, const LightProxyUpdateFields field) noexcept
        {
            return (fields & field) != LightProxyUpdateFields::None;
        }

        [[nodiscard]] constexpr bool HasField(const DecalProxyUpdateFields fields, const DecalProxyUpdateFields field) noexcept
        {
            return (fields & field) != DecalProxyUpdateFields::None;
        }

        [[nodiscard]] constexpr bool ValidFields(const MeshProxyUpdateFields fields) noexcept
        {
            constexpr u8 known = static_cast<u8>(MeshProxyUpdateFields::All) | static_cast<u8>(MeshProxyUpdateFields::Drawable);
            return fields != MeshProxyUpdateFields::None && (static_cast<u8>(fields) & ~known) == 0;
        }

        [[nodiscard]] constexpr bool ValidFields(const LightProxyUpdateFields fields) noexcept
        {
            return fields != LightProxyUpdateFields::None && (static_cast<u8>(fields) & ~static_cast<u8>(LightProxyUpdateFields::All)) == 0;
        }

        [[nodiscard]] constexpr bool ValidFields(const DecalProxyUpdateFields fields) noexcept
        {
            return fields != DecalProxyUpdateFields::None && (static_cast<u8>(fields) & ~static_cast<u8>(DecalProxyUpdateFields::All)) == 0;
        }

        inline constexpr u32 MeshProxyTypeId = 1;
        inline constexpr u32 LightProxyTypeId = 2;
        inline constexpr u32 DecalProxyTypeId = 3;
        inline constexpr u32 InvalidSlotIndex = ~u32{0};
        inline constexpr u32 ProducerPageShift = 12;
        inline constexpr u32 ProducerSlotsPerPage = 1u << ProducerPageShift;
        inline constexpr u32 ProducerPageMask = ProducerSlotsPerPage - 1u;
        inline constexpr u32 RelinkPageShift = 12;
        inline constexpr u32 RelinkSlotsPerPage = 1u << RelinkPageShift;
        inline constexpr u32 RelinkPageMask = RelinkSlotsPerPage - 1u;
    } // namespace

    struct RenderSceneManager::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct ProxySlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            u32 generation = 0;
            u32 typeId = 0;
            RenderProxyPayloadKind payloadKind = RenderProxyPayloadKind::None;
            u32 payloadIndex = InvalidSlotIndex;
            u32 payloadGeneration = 0;
            GpuInstanceIndex gpuInstanceIndex = InvalidGpuSceneIndex;
            u32 nextFree = InvalidSlotIndex;
            bool retirementPending = false;
            u64 producerId = 0;
            u64 producerGeneration = 0;
            RenderProducerHandle producer;
            RenderContributorId contributor;
            u32 previousProducerProxy = InvalidSlotIndex;
            u32 nextProducerProxy = InvalidSlotIndex;
            RenderProxyTransform transform;
            RenderProxyBounds bounds;
            RenderProxySpatialMode spatialMode = RenderProxySpatialMode::None;
            spatial::EntryHandle spatial;
            RenderProxyVisibilityFlags visibility = RenderProxyVisibilityFlags::None;
            u64 layerMask = 0;
            u32 visibilityMask = 0;
            u64 userDataEpoch = 0;
            u64 teleportRevision = 0;
            u64 createdSerial = 0;
            u64 lifecycleRevision = 0;
            char debugName[MaximumRenderProxyNameBytes]{};
        };

        struct PendingRelinkRequest
        {
            RenderProxyRelinkRequest input;
            RenderProxyBounds oldBounds;
            bool active = false;
            bool structuralMove = false;
        };

        struct RelinkState
        {
            inline static constexpr u64 OutstandingMask = (u64{1} << 31u) - 1u;
            inline static constexpr u64 AdmissionClosed = u64{1} << 31u;
            inline static constexpr u32 GenerationShift = 32u;

            struct ProxyRelinkMetadata
            {
                // High 32 bits: proxy generation. Low 31 bits: outstanding requests. Bit 31: admission closed.
                concurrency::Atomic<u64> admission{0};
                u32 dedupStamp = 0;
            };

            enum class CloseProxyResult : u8
            {
                Closed,
                Outstanding,
                Stale
            };

            enum class AcquireProxyResult : u8
            {
                Acquired,
                Closed,
                CapacityExceeded,
                Stale
            };

            struct ProxyRelinkPage
            {
                VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

                ProxyRelinkMetadata slots[RelinkSlotsPerPage];
            };

            RelinkState(const u32 capacity, const u32 maximumProxies) noexcept
                : pendingA(memory::pools::Rendering::GetInstance()), pendingB(memory::pools::Rendering::GetInstance()), proxyPages(memory::pools::Rendering::GetInstance())
            {
                pendingA.Resize(capacity);
                pendingB.Resize(capacity);
                proxyPages.Resize((maximumProxies + RelinkPageMask) >> RelinkPageShift);
                for (u32 index = 0; index < proxyPages.Size(); ++index)
                    proxyPages[index] = nullptr;
            }

            ~RelinkState()
            {
                for (u32 index = 0; index < proxyPages.Size(); ++index)
                    if (proxyPages[index] != nullptr)
                        VANGUARD_DELETE(proxyPages[index]);
            }

            [[nodiscard]] containers::DynamicArray<PendingRelinkRequest>& Queue(const u32 index) noexcept
            {
                return index == 0 ? pendingA : pendingB;
            }

            [[nodiscard]] const containers::DynamicArray<PendingRelinkRequest>& Queue(const u32 index) const noexcept
            {
                return index == 0 ? pendingA : pendingB;
            }

            [[nodiscard]] bool MaterializeProxyPage(const u32 proxyIndex) noexcept
            {
                const u32 pageIndex = proxyIndex >> RelinkPageShift;
                if (pageIndex >= proxyPages.Size())
                    return false;
                if (proxyPages[pageIndex] == nullptr)
                    proxyPages[pageIndex] = VANGUARD_NEW(ProxyRelinkPage);
                return proxyPages[pageIndex] != nullptr;
            }

            [[nodiscard]] ProxyRelinkMetadata* ResolveProxyMetadata(const u32 proxyIndex) noexcept
            {
                const u32 pageIndex = proxyIndex >> RelinkPageShift;
                if (pageIndex >= proxyPages.Size() || proxyPages[pageIndex] == nullptr)
                    return nullptr;
                return &proxyPages[pageIndex]->slots[proxyIndex & RelinkPageMask];
            }

            [[nodiscard]] const ProxyRelinkMetadata* ResolveProxyMetadata(const u32 proxyIndex) const noexcept
            {
                const u32 pageIndex = proxyIndex >> RelinkPageShift;
                if (pageIndex >= proxyPages.Size() || proxyPages[pageIndex] == nullptr)
                    return nullptr;
                return &proxyPages[pageIndex]->slots[proxyIndex & RelinkPageMask];
            }

            [[nodiscard]] bool InitializeProxyMetadata(const RenderProxyHandle proxy) noexcept
            {
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy.index);
                if (metadata == nullptr || (metadata->admission.GetValue() & OutstandingMask) != 0)
                    return false;
                metadata->admission.SetValue(static_cast<u64>(proxy.generation) << GenerationShift);
                metadata->dedupStamp = 0;
                return true;
            }

            [[nodiscard]] ProxyRelinkMetadata* ResolveProxyMetadata(const RenderProxyHandle proxy) noexcept
            {
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy.index);
                return metadata != nullptr && static_cast<u32>(metadata->admission.GetValue() >> GenerationShift) == proxy.generation ? metadata : nullptr;
            }

            [[nodiscard]] const ProxyRelinkMetadata* ResolveProxyMetadata(const RenderProxyHandle proxy) const noexcept
            {
                const ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy.index);
                return metadata != nullptr && static_cast<u32>(metadata->admission.GetValue() >> GenerationShift) == proxy.generation ? metadata : nullptr;
            }

            [[nodiscard]] AcquireProxyResult AcquireOutstanding(const RenderProxyHandle proxy, bool& alreadyOutstanding) noexcept
            {
                alreadyOutstanding = false;
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy.index);
                if (metadata == nullptr)
                    return AcquireProxyResult::Stale;
                u64 admission = metadata->admission.GetValue();
                for (;;)
                {
                    if (static_cast<u32>(admission >> GenerationShift) != proxy.generation)
                        return AcquireProxyResult::Stale;
                    if ((admission & AdmissionClosed) != 0)
                        return AcquireProxyResult::Closed;
                    if ((admission & OutstandingMask) == OutstandingMask)
                        return AcquireProxyResult::CapacityExceeded;
                    const u64 observed = metadata->admission.CompareExchange(admission + 1u, admission);
                    if (observed == admission)
                    {
                        alreadyOutstanding = (admission & OutstandingMask) != 0;
                        return AcquireProxyResult::Acquired;
                    }
                    admission = observed;
                }
            }

            void ReleaseOutstanding(const RenderProxyHandle proxy) noexcept
            {
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy);
                if (metadata != nullptr)
                    static_cast<void>(metadata->admission.PostDecrement());
            }

            [[nodiscard]] CloseProxyResult CloseProxy(const RenderProxyHandle proxy) noexcept
            {
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy.index);
                if (metadata == nullptr)
                    return CloseProxyResult::Stale;
                u64 admission = metadata->admission.GetValue();
                for (;;)
                {
                    if (static_cast<u32>(admission >> GenerationShift) != proxy.generation)
                        return CloseProxyResult::Stale;
                    if ((admission & OutstandingMask) != 0)
                        return CloseProxyResult::Outstanding;
                    if ((admission & AdmissionClosed) != 0)
                        return CloseProxyResult::Closed;
                    const u64 observed = metadata->admission.CompareExchange(admission | AdmissionClosed, admission);
                    if (observed == admission)
                        return CloseProxyResult::Closed;
                    admission = observed;
                }
            }

            void ReopenProxy(const RenderProxyHandle proxy) noexcept
            {
                ProxyRelinkMetadata* const metadata = ResolveProxyMetadata(proxy);
                if (metadata != nullptr)
                    static_cast<void>(metadata->admission.And(~AdmissionClosed));
            }

            void ClearDedupStamps() noexcept
            {
                for (u32 pageIndex = 0; pageIndex < proxyPages.Size(); ++pageIndex)
                    if (proxyPages[pageIndex] != nullptr)
                        for (u32 index = 0; index < RelinkSlotsPerPage; ++index)
                            proxyPages[pageIndex]->slots[index].dedupStamp = 0;
            }

            containers::DynamicArray<PendingRelinkRequest> pendingA;
            containers::DynamicArray<PendingRelinkRequest> pendingB;
            containers::DynamicArray<ProxyRelinkPage*> proxyPages;
            concurrency::RWSpinLock pendingIndexLock;
            concurrency::Atomic<u32> pendingCount{0};
            u32 pendingIndex = 0;
            u32 processIndex = 0;
            u32 processCount = 0;
            u32 uniqueCount = 0;
            u32 structuralMoveCount = 0;
            u32 dedupStamp = 0;
            u64 lastPreparedTick = ~u64{0};
            u64 candidateSerial = 0;
            bool prepared = false;
            concurrency::Atomic<bool> dispatched{false};
            concurrency::Atomic<bool> candidateProduction{false};
        };

        struct MeshPayloadSlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            RenderProxyHandle proxy;
            u32 generation = 0;
            u32 nextFree = InvalidSlotIndex;
            resources::ResourceReference mesh;
            resources::ResourceReference material;
            resources::ResourceHandle meshHandle;
            resources::ResourceHandle materialHandle;
            MeshDrawableBinding activeDrawable;
            MeshDrawableBinding candidateDrawable;
            RenderSceneGpuBindingReceipt bindingReceipt;
            u32 previousBinding = InvalidSlotIndex;
            u32 nextBinding = InvalidSlotIndex;
            u32 submeshMask = 0;
            u32 renderFlags = 0;
            bool bindingQueued = false;
            bool clearingBinding = false;
        };

        struct LightPayloadSlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            RenderProxyHandle proxy;
            // Borrowed immutable allocation identity, like ProxySlot's mesh
            // instance index. The publisher remains the allocation owner.
            GpuLightHandle gpuIdentity;
            u32 generation = 0;
            u32 nextFree = InvalidSlotIndex;
            RenderLightKind kind = RenderLightKind::Point;
            f32 color[3]{};
            f32 intensity = 0.0f;
            f32 range = 0.0f;
            f32 innerConeRadians = 0.0f;
            f32 outerConeRadians = 0.0f;
            bool castsShadow = false;
        };

        struct DecalPayloadSlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            RenderProxyHandle proxy;
            u32 generation = 0;
            u32 nextFree = InvalidSlotIndex;
            resources::ResourceReference material;
            resources::ResourceHandle materialHandle;
            f32 extents[3]{};
            f32 fadeDistance = 0.0f;
            u32 sortKey = 0;
        };

        struct ProducerSlot
        {
            u32 generation = 0;
            u32 firstProxy = InvalidSlotIndex;
            u32 proxyCount = 0;
            bool occupied = false;
        };

        struct ProducerPage
        {
            ProducerSlot slots[ProducerSlotsPerPage]{};
        };

        struct SceneSlot
        {
            SceneSlot() noexcept
                : proxies(memory::pools::Rendering::GetInstance()), meshPayloads(memory::pools::Rendering::GetInstance()), lightPayloads(memory::pools::Rendering::GetInstance()),
                  decalPayloads(memory::pools::Rendering::GetInstance()), producerPages(memory::pools::Rendering::GetInstance())
            {
            }

            RenderSceneState state = RenderSceneState::Vacant;
            RenderSceneMode mode = RenderSceneMode::Runtime;
            RenderSceneOwnership ownership = RenderSceneOwnership::Service;
            u32 generation = 0;
            u32 maximumProxies = 0;
            u32 maximumPendingProxyMutations = 0;
            u32 maximumViews = 0;
            u32 activeProxies = 0;
            u32 firstFreeProxy = InvalidSlotIndex;
            u32 firstFreeMeshPayload = InvalidSlotIndex;
            u32 firstFreeLightPayload = InvalidSlotIndex;
            u32 firstFreeDecalPayload = InvalidSlotIndex;
            u32 firstPendingMeshBinding = InvalidSlotIndex;
            u32 lastPendingMeshBinding = InvalidSlotIndex;
            u32 pendingMeshBindingCount = 0;
            u32 framePipelineSceneIndex = InvalidSlotIndex;
            u64 createdSerial = 0;
            u64 lifecycleRevision = 0;
            u64 currentMutationEpoch = 0;
            u64 preparedMutationEpoch = 0;
            u64 completedMutationEpoch = 0;
            u32 pendingMutationCount = 0;
            u32 preparedMutationCount = 0;
            bool framePrepared = false;
            bool allowFramePipelineParticipation = false;
            bool visibilityFeedbackAttached = false;
            char name[MaximumRenderSceneNameBytes]{};
            containers::DynamicArray<ProxySlot> proxies;
            containers::DynamicArray<MeshPayloadSlot> meshPayloads;
            containers::DynamicArray<LightPayloadSlot> lightPayloads;
            containers::DynamicArray<DecalPayloadSlot> decalPayloads;
            containers::DynamicArray<ProducerPage*> producerPages;
            spatial::WriteIndex spatial;
        };

        template <typename Slot> [[nodiscard]] static u32 AcquireSlot(containers::DynamicArray<Slot>& slots, u32& firstFree) noexcept
        {
            if (firstFree == InvalidSlotIndex)
            {
                const u32 index = slots.Size();
                slots.PushBack({});
                return index;
            }

            const u32 index = firstFree;
            firstFree = slots[index].nextFree;
            slots[index].nextFree = InvalidSlotIndex;
            return index;
        }

        template <typename Slot> static void ReleasePayloadSlot(containers::DynamicArray<Slot>& slots, u32& firstFree, const u32 index) noexcept
        {
            slots[index] = {};
            slots[index].state = RenderProxyState::Retired;
            slots[index].nextFree = firstFree;
            firstFree = index;
        }

        static void ReleaseProxySlot(SceneSlot& scene, const u32 index) noexcept
        {
            ProxySlot& slot = scene.proxies[index];
            slot.state = RenderProxyState::Retired;
            slot.nextFree = scene.firstFreeProxy;
            scene.firstFreeProxy = index;
        }

        static void QueueMeshBinding(SceneSlot& scene, const u32 index) noexcept
        {
            MeshPayloadSlot& payload = scene.meshPayloads[index];
            if (payload.bindingQueued)
                return;
            payload.previousBinding = scene.lastPendingMeshBinding;
            payload.nextBinding = InvalidSlotIndex;
            if (scene.lastPendingMeshBinding != InvalidSlotIndex)
                scene.meshPayloads[scene.lastPendingMeshBinding].nextBinding = index;
            else
                scene.firstPendingMeshBinding = index;
            scene.lastPendingMeshBinding = index;
            payload.bindingQueued = true;
            ++scene.pendingMeshBindingCount;
        }

        static void RemoveMeshBinding(SceneSlot& scene, const u32 index) noexcept
        {
            MeshPayloadSlot& payload = scene.meshPayloads[index];
            if (!payload.bindingQueued)
                return;
            if (payload.previousBinding != InvalidSlotIndex)
                scene.meshPayloads[payload.previousBinding].nextBinding = payload.nextBinding;
            else
                scene.firstPendingMeshBinding = payload.nextBinding;
            if (payload.nextBinding != InvalidSlotIndex)
                scene.meshPayloads[payload.nextBinding].previousBinding = payload.previousBinding;
            else
                scene.lastPendingMeshBinding = payload.previousBinding;
            payload.previousBinding = InvalidSlotIndex;
            payload.nextBinding = InvalidSlotIndex;
            payload.bindingQueued = false;
            --scene.pendingMeshBindingCount;
        }

        [[nodiscard]] static ProducerSlot* ResolveProducer(SceneSlot& scene, const RenderProducerHandle producer, const bool materialize) noexcept
        {
            if (!producer.IsValid())
                return nullptr;
            const u32 pageIndex = producer.index >> ProducerPageShift;
            if (pageIndex >= scene.producerPages.Size())
            {
                if (!materialize)
                    return nullptr;
                scene.producerPages.Resize(pageIndex + 1u);
            }
            ProducerPage*& page = scene.producerPages[pageIndex];
            if (page == nullptr)
            {
                if (!materialize)
                    return nullptr;
                memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(ProducerPage), alignof(ProducerPage));
                if (!block)
                    return nullptr;
                page = ::new (block.address) ProducerPage{};
            }
            ProducerSlot& slot = page->slots[producer.index & ProducerPageMask];
            if (!slot.occupied)
            {
                if (!materialize)
                    return nullptr;
                slot = {};
                slot.generation = producer.generation;
                slot.occupied = true;
            }
            return slot.generation == producer.generation ? &slot : nullptr;
        }

        [[nodiscard]] static const ProducerSlot* ResolveProducer(const SceneSlot& scene, const RenderProducerHandle producer) noexcept
        {
            if (!producer.IsValid())
                return nullptr;
            const u32 pageIndex = producer.index >> ProducerPageShift;
            if (pageIndex >= scene.producerPages.Size() || scene.producerPages[pageIndex] == nullptr)
                return nullptr;
            const ProducerSlot& slot = scene.producerPages[pageIndex]->slots[producer.index & ProducerPageMask];
            return slot.occupied && slot.generation == producer.generation ? &slot : nullptr;
        }

        static void ResetProducerDirectory(SceneSlot& scene) noexcept
        {
            for (ProducerPage* const page : scene.producerPages)
            {
                if (page == nullptr)
                    continue;
                page->~ProducerPage();
                memory::MemoryBlock block{page, sizeof(ProducerPage), memory::PoolId::Rendering};
                memory::Free(block);
            }
            scene.producerPages.Clear();
        }

        [[nodiscard]] static u32 FindProducerProxyIndex(const SceneSlot& scene, const RenderProducerHandle producer, const RenderContributorId contributor) noexcept
        {
            const ProducerSlot* const owner = ResolveProducer(scene, producer);
            if (owner == nullptr || !contributor.IsValid())
                return InvalidSlotIndex;
            u32 proxyIndex = owner->firstProxy;
            while (proxyIndex != InvalidSlotIndex)
            {
                const ProxySlot& proxy = scene.proxies[proxyIndex];
                if (proxy.state == RenderProxyState::Alive && proxy.producer == producer && proxy.contributor == contributor)
                    return proxyIndex;
                proxyIndex = proxy.nextProducerProxy;
            }
            return InvalidSlotIndex;
        }

        [[nodiscard]] static bool LinkProducerProxy(SceneSlot& scene, const RenderProxyHandle proxyHandle) noexcept
        {
            ProxySlot& proxy = scene.proxies[proxyHandle.index];
            ProducerSlot* const producer = ResolveProducer(scene, proxy.producer, true);
            if (producer == nullptr || FindProducerProxyIndex(scene, proxy.producer, proxy.contributor) != InvalidSlotIndex)
                return false;
            proxy.previousProducerProxy = InvalidSlotIndex;
            proxy.nextProducerProxy = producer->firstProxy;
            if (producer->firstProxy != InvalidSlotIndex)
                scene.proxies[producer->firstProxy].previousProducerProxy = proxyHandle.index;
            producer->firstProxy = proxyHandle.index;
            ++producer->proxyCount;
            return true;
        }

        static void UnlinkProducerProxy(SceneSlot& scene, ProxySlot& proxy) noexcept
        {
            if (!proxy.producer.IsValid())
                return;
            ProducerSlot* const producer = ResolveProducer(scene, proxy.producer, false);
            if (producer == nullptr)
                return;
            if (proxy.previousProducerProxy != InvalidSlotIndex)
                scene.proxies[proxy.previousProducerProxy].nextProducerProxy = proxy.nextProducerProxy;
            else
                producer->firstProxy = proxy.nextProducerProxy;
            if (proxy.nextProducerProxy != InvalidSlotIndex)
                scene.proxies[proxy.nextProducerProxy].previousProducerProxy = proxy.previousProducerProxy;
            if (producer->proxyCount != 0)
                --producer->proxyCount;
            if (producer->proxyCount == 0)
                *producer = {};
            proxy.producer = {};
            proxy.contributor = {};
            proxy.previousProducerProxy = InvalidSlotIndex;
            proxy.nextProducerProxy = InvalidSlotIndex;
        }

        explicit Impl(const RenderSceneManagerConfig& value) noexcept : slots(memory::pools::Rendering::GetInstance()), relinkStates(memory::pools::Rendering::GetInstance())
        {
            slots.Reserve(value.maximumScenes);
            relinkStates.Reserve(value.maximumScenes);
            for (u32 index = 0; index < value.maximumScenes; ++index)
            {
                slots.PushBack({});
                relinkStates.PushBack(nullptr);
            }
            stats.capacity = value.maximumScenes;
            stats.initialized = true;
        }

        [[nodiscard]] bool ValidHandle(const RenderSceneHandle scene) const noexcept
        {
            return scene.index < slots.Size() && scene.generation != 0 && slots[scene.index].generation == scene.generation && slots[scene.index].state != RenderSceneState::Vacant;
        }

        [[nodiscard]] bool ValidAliveScene(const RenderSceneHandle scene) const noexcept
        {
            return ValidHandle(scene) && slots[scene.index].state == RenderSceneState::Alive;
        }

        [[nodiscard]] bool ValidFramePipelineScene(const RenderSceneHandle scene, const SceneSlot& slot) const noexcept
        {
            if (!slot.allowFramePipelineParticipation)
                return slot.framePipelineSceneIndex == InvalidSlotIndex;
            return slot.framePipelineSceneIndex < framePipelineSceneCount && framePipelineScenes[slot.framePipelineSceneIndex] == scene;
        }

        [[nodiscard]] bool AddFramePipelineScene(const RenderSceneHandle scene, SceneSlot& slot) noexcept
        {
            if (!slot.allowFramePipelineParticipation)
            {
                slot.framePipelineSceneIndex = InvalidSlotIndex;
                return true;
            }
            if (slot.framePipelineSceneIndex != InvalidSlotIndex || framePipelineSceneCount >= stats.capacity)
                return false;
            slot.framePipelineSceneIndex = framePipelineSceneCount;
            framePipelineScenes[framePipelineSceneCount++] = scene;
            stats.framePipelineScenes = framePipelineSceneCount;
            return true;
        }

        void RemoveFramePipelineScene(SceneSlot& slot) noexcept
        {
            if (!slot.allowFramePipelineParticipation)
            {
                slot.framePipelineSceneIndex = InvalidSlotIndex;
                return;
            }

            const u32 removedIndex = slot.framePipelineSceneIndex;
            const u32 lastIndex = framePipelineSceneCount - 1u;
            if (removedIndex != lastIndex)
            {
                const RenderSceneHandle movedScene = framePipelineScenes[lastIndex];
                framePipelineScenes[removedIndex] = movedScene;
                slots[movedScene.index].framePipelineSceneIndex = removedIndex;
            }
            framePipelineScenes[lastIndex] = {};
            --framePipelineSceneCount;
            stats.framePipelineScenes = framePipelineSceneCount;
            slot.framePipelineSceneIndex = InvalidSlotIndex;
        }

        [[nodiscard]] bool ValidProxy(const RenderProxyHandle proxy) const noexcept
        {
            if (!ValidAliveScene(proxy.scene))
                return false;
            const SceneSlot& scene = slots[proxy.scene.index];
            return proxy.index < scene.proxies.Size() && proxy.generation != 0 && scene.proxies[proxy.index].generation == proxy.generation && scene.proxies[proxy.index].state != RenderProxyState::Vacant;
        }

        [[nodiscard]] bool ValidAliveProxy(const RenderProxyHandle proxy) const noexcept
        {
            return ValidProxy(proxy) && slots[proxy.scene.index].proxies[proxy.index].state == RenderProxyState::Alive;
        }

        [[nodiscard]] bool ValidAlivePayload(const RenderProxyHandle proxy, const RenderProxyPayloadKind kind) const noexcept
        {
            if (!ValidAliveProxy(proxy))
                return false;
            const ProxySlot& slot = slots[proxy.scene.index].proxies[proxy.index];
            if (slot.payloadKind != kind || slot.payloadGeneration != proxy.generation)
                return false;
            switch (kind)
            {
            case RenderProxyPayloadKind::Mesh:
                return slot.payloadIndex < slots[proxy.scene.index].meshPayloads.Size() && slots[proxy.scene.index].meshPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].meshPayloads[slot.payloadIndex].generation == proxy.generation && slots[proxy.scene.index].meshPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::Light:
                return slot.payloadIndex < slots[proxy.scene.index].lightPayloads.Size() && slots[proxy.scene.index].lightPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].lightPayloads[slot.payloadIndex].generation == proxy.generation && slots[proxy.scene.index].lightPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::Decal:
                return slot.payloadIndex < slots[proxy.scene.index].decalPayloads.Size() && slots[proxy.scene.index].decalPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].decalPayloads[slot.payloadIndex].generation == proxy.generation && slots[proxy.scene.index].decalPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::None:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool RegisterMutation(const RenderSceneHandle sceneHandle, SceneSlot& scene, const bool bypassBudget = false) noexcept
        {
            if (scene.framePrepared || CandidateProductionOpen(sceneHandle))
                return false;
            if (scene.pendingMutationCount >= scene.maximumPendingProxyMutations && !bypassBudget)
                return false;
            ++scene.currentMutationEpoch;
            ++scene.pendingMutationCount;
            ++stats.pendingProxyMutations;
            return true;
        }

        [[nodiscard]] bool CandidateProductionOpen(const RenderSceneHandle scene) const noexcept
        {
            return scene.index < relinkStates.Size() && relinkStates[scene.index] != nullptr && relinkStates[scene.index]->candidateProduction.GetValue();
        }

        void RollbackMutation(SceneSlot& scene) noexcept
        {
            --scene.pendingMutationCount;
            --stats.pendingProxyMutations;
        }

        [[nodiscard]] RelinkState* CreateRelinkState(const u32 capacity, const u32 maximumProxies) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(RelinkState), alignof(RelinkState));
            return block ? ::new (block.address) RelinkState(capacity, maximumProxies) : nullptr;
        }

        void DestroyRelinkState(RelinkState*& state) noexcept
        {
            if (state == nullptr)
                return;
            state->~RelinkState();
            memory::MemoryBlock block{state, sizeof(RelinkState), memory::PoolId::Rendering};
            memory::Free(block);
            state = nullptr;
        }

        void CancelPreparedSceneUpdate(SceneSlot& scene, RelinkState& relinks) noexcept
        {
            containers::DynamicArray<PendingRelinkRequest>& requests = relinks.Queue(relinks.processIndex);
            for (u32 index = 0; index < relinks.processCount; ++index)
            {
                PendingRelinkRequest& request = requests[index];
                if (request.active)
                    relinks.ReleaseOutstanding(request.input.proxy);
                request = {};
            }

            // Direct main-thread mutations were applied before preparation. Only the undispatched
            // relink batch is discarded, and every admission reference is released above.
            scene.completedMutationEpoch = scene.preparedMutationEpoch;
            scene.framePrepared = false;
            scene.preparedMutationCount = 0;
            relinks.processCount = 0;
            relinks.uniqueCount = 0;
            relinks.structuralMoveCount = 0;
            relinks.prepared = false;
        }

        void ApplySpatialDelta(const spatial::CounterDelta& delta) noexcept
        {
            if (delta.activeEntries >= 0)
                stats.activeSpatialEntries += static_cast<u32>(delta.activeEntries);
            else
            {
                const u32 removed = static_cast<u32>(-delta.activeEntries);
                stats.activeSpatialEntries = removed <= stats.activeSpatialEntries ? stats.activeSpatialEntries - removed : 0;
            }
            if (delta.dirtyCells >= 0)
                stats.dirtySpatialCells += static_cast<u32>(delta.dirtyCells);
            else
            {
                const u32 repaired = static_cast<u32>(-delta.dirtyCells);
                stats.dirtySpatialCells = repaired <= stats.dirtySpatialCells ? stats.dirtySpatialCells - repaired : 0;
            }
            stats.spatialFastMoves += delta.fastMoves;
            stats.spatialStructuralMoves += delta.structuralMoves;
            stats.spatialRepairedCells += delta.repairedCells;
            stats.spatialOutOfRangeProxies += delta.outOfRangeProxies;
        }

        void ApplySpatialRemoveResult(const spatial::RemoveResult& result) noexcept
        {
            if (!result.movedProxyRelocated || !ValidAliveProxy(result.movedProxy))
                return;
            slots[result.movedProxy.scene.index].proxies[result.movedProxy.index].spatial.objectIndex = result.movedObjectIndex;
        }

        void ConfigureSpatialIndex(SceneSlot& scene, const SpatialWriteIndexConfig& config) noexcept
        {
            spatial::CounterDelta delta;
            spatial::Reset(scene.spatial, config, &delta);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool SpatialBoundsAccepted(const SceneSlot& scene, const RenderProxyBounds& bounds) const noexcept
        {
            return spatial::BoundsAccepted(scene.spatial, bounds);
        }

        [[nodiscard]] bool InsertSpatialEntry(SceneSlot& scene, const RenderProxyHandle proxy, ProxySlot& slot) noexcept
        {
            spatial::CounterDelta delta;
            const spatial::InsertResult result = spatial::Insert(scene.spatial, proxy, slot.bounds, slot.spatialMode, slot.spatial, &delta);
            ApplySpatialDelta(delta);
            return result != spatial::InsertResult::Rejected;
        }

        void RemoveSpatialEntry(SceneSlot& scene, ProxySlot& slot) noexcept
        {
            spatial::CounterDelta delta;
            spatial::RemoveResult result;
            spatial::Remove(scene.spatial, slot.spatial, &result, &delta);
            ApplySpatialRemoveResult(result);
            ApplySpatialDelta(delta);
        }

        void MoveSpatialEntry(SceneSlot& scene, const RenderProxyHandle proxy, ProxySlot& slot, const RenderProxyBounds& oldBounds) noexcept
        {
            spatial::CounterDelta delta;
            spatial::RemoveResult result;
            spatial::Move(scene.spatial, proxy, oldBounds, slot.bounds, slot.spatialMode, slot.spatial, &result, &delta);
            ApplySpatialRemoveResult(result);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool ResolveSpatialProxyBounds(const RenderProxyHandle proxy, RenderProxyBounds& bounds) const noexcept
        {
            if (!ValidAliveProxy(proxy))
                return false;
            bounds = slots[proxy.scene.index].proxies[proxy.index].bounds;
            return true;
        }

        [[nodiscard]] bool ValidateSpatialProxyEntry(const RenderProxyHandle proxy, spatial::EntryHandle& entry, RenderProxyBounds& bounds) const noexcept
        {
            if (!ValidAliveProxy(proxy))
                return false;
            const ProxySlot& slot = slots[proxy.scene.index].proxies[proxy.index];
            entry = slot.spatial;
            bounds = slot.bounds;
            return true;
        }

        static bool ResolveSpatialProxyBoundsThunk(void* const userData, const RenderProxyHandle proxy, RenderProxyBounds& bounds) noexcept
        {
            return static_cast<const Impl*>(userData)->ResolveSpatialProxyBounds(proxy, bounds);
        }

        static bool ValidateSpatialProxyEntryThunk(void* const userData, const RenderProxyHandle proxy, spatial::EntryHandle& entry, RenderProxyBounds& bounds) noexcept
        {
            return static_cast<const Impl*>(userData)->ValidateSpatialProxyEntry(proxy, entry, bounds);
        }

        static bool ResolveLiveVisibilityProxyThunk(void* const userData, const RenderProxyHandle proxy, spatial::VisibilityProxyReadView& view) noexcept
        {
            const Impl* const impl = static_cast<const Impl*>(userData);
            view = {};
            if (!impl->ValidAliveProxy(proxy))
                return false;
            const ProxySlot& slot = impl->slots[proxy.scene.index].proxies[proxy.index];
            view.bounds = &slot.bounds;
            view.visibility = &slot.visibility;
            view.layerMask = &slot.layerMask;
            view.visibilityMask = &slot.visibilityMask;
            view.payloadKind = &slot.payloadKind;
            view.gpuInstanceIndex = &slot.gpuInstanceIndex;
            view.global = slot.spatialMode == RenderProxySpatialMode::Global;
            return true;
        }

        void RepairSpatialIndex(SceneSlot& scene) noexcept
        {
            spatial::CounterDelta delta;
            spatial::Repair(scene.spatial, this, ResolveSpatialProxyBoundsThunk, &delta);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool ValidateSpatialIndexInternal(const SceneSlot& scene, SpatialWriteIndexStats* const outStats) const noexcept
        {
            return spatial::Validate(scene.spatial, const_cast<Impl*>(this), ValidateSpatialProxyEntryThunk, outStats);
        }

        containers::DynamicArray<SceneSlot> slots;
        containers::DynamicArray<RelinkState*> relinkStates;
        RenderSceneHandle framePipelineScenes[MaximumRenderScenes]{};
        u32 framePipelineSceneCount = 0;
        mutable concurrency::RWLock publicationLock;
        RenderSceneManagerStats stats;
        RenderCameraStorage* cameraStorage = nullptr;
        RenderSceneGpuPublisher* gpuPublisher = nullptr;
        u32 nextMeshBindingScene = 0;
        u64 nextCreatedSerial = 1;
        u64 nextProxyCreatedSerial = 1;
    };

    RenderSceneManager::~RenderSceneManager()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool RenderSceneManager::Initialize(const RenderSceneManagerConfig& config, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderSceneFailureCode::AlreadyInitialized, "RenderSceneManager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderSceneManager must initialize on the main thread");
        if (config.maximumScenes == 0 || config.maximumScenes > MaximumRenderScenes)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid RenderSceneManager scene capacity");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderSceneManager implementation allocation failed");
        Impl* const impl = ::new (block.address) Impl(config);
        if (impl->slots.Size() != config.maximumScenes)
        {
            impl->~Impl();
            memory::Free(block);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderSceneManager slot reservation failed");
        }
        m_impl = impl;
        return true;
    }

    bool RenderSceneManager::Shutdown(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderSceneManager must shutdown on the main thread");
        if (m_impl->stats.activeScenes != 0 || m_impl->stats.destroyingScenes != 0 || m_impl->framePipelineSceneCount != 0)
            return Fail(failure, RenderSceneFailureCode::ScenesRemainAlive, "RenderSceneManager shutdown blocked by live scenes");
        if (m_impl->stats.activeProxies != 0)
            return Fail(failure, RenderSceneFailureCode::ProxiesRemainAlive, "RenderSceneManager shutdown blocked by live proxies");
        if (m_impl->cameraStorage != nullptr)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderSceneManager shutdown blocked by its camera storage");
        if (m_impl->gpuPublisher != nullptr)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderSceneManager shutdown blocked by its GPU publisher");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool RenderSceneManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool RenderSceneManager::CreateScene(const RenderSceneDesc& desc, RenderSceneHandle& scene, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        scene = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderScene creation must run on the main thread");
        }
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!ValidMode(desc.mode) || !ValidOwnership(desc.ownership) || !ValidSpatialConfig(desc.spatial) || desc.maximumProxies == 0 || desc.maximumProxies > MaximumRenderProxySlotsPerScene ||
            desc.maximumPendingProxyMutations == 0 || desc.maximumViews == 0 || desc.maximumViews > MaximumRenderViews)
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid RenderScene creation descriptor");
        }

        u32 slotIndex = MaximumRenderScenes;
        for (u32 index = 0; index < m_impl->slots.Size(); ++index)
        {
            if (m_impl->slots[index].state == RenderSceneState::Vacant)
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == MaximumRenderScenes)
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "maximum RenderScene count exceeded");
        }

        Impl::SceneSlot& slot = m_impl->slots[slotIndex];
        char copiedName[MaximumRenderSceneNameBytes]{};
        if (!CopyName(copiedName, MaximumRenderSceneNameBytes, desc.name))
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "RenderScene name is missing or too long");
        }

        Impl::RelinkState* relinkState = m_impl->CreateRelinkState(desc.maximumPendingProxyMutations, desc.maximumProxies);
        if (relinkState == nullptr)
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderScene relink queue allocation failed");
        }
        m_impl->DestroyRelinkState(m_impl->relinkStates[slotIndex]);
        m_impl->relinkStates[slotIndex] = relinkState;

        slot.state = RenderSceneState::Alive;
        slot.mode = desc.mode;
        slot.ownership = desc.ownership;
        slot.generation = NextGeneration(slot.generation);
        slot.maximumProxies = desc.maximumProxies;
        slot.maximumPendingProxyMutations = desc.maximumPendingProxyMutations;
        slot.maximumViews = desc.maximumViews;
        slot.activeProxies = 0;
        slot.createdSerial = m_impl->nextCreatedSerial++;
        slot.lifecycleRevision = 1;
        // Zero is reserved as an invalid/unpublished version by RenderViewFamily. Scene
        // construction establishes the first observable scene version even before a proxy mutates.
        slot.currentMutationEpoch = 1;
        slot.preparedMutationEpoch = 0;
        slot.completedMutationEpoch = 0;
        slot.pendingMutationCount = 0;
        slot.preparedMutationCount = 0;
        slot.framePrepared = false;
        slot.allowFramePipelineParticipation = desc.allowFramePipelineParticipation;
        slot.visibilityFeedbackAttached = false;
        slot.firstFreeProxy = InvalidSlotIndex;
        slot.firstFreeMeshPayload = InvalidSlotIndex;
        slot.firstFreeLightPayload = InvalidSlotIndex;
        slot.firstFreeDecalPayload = InvalidSlotIndex;
        slot.framePipelineSceneIndex = InvalidSlotIndex;
        Impl::ResetProducerDirectory(slot);
        slot.proxies.Clear();
        slot.meshPayloads.Clear();
        slot.lightPayloads.Clear();
        slot.decalPayloads.Clear();
        m_impl->ConfigureSpatialIndex(slot, desc.spatial);
        CopyNameUnchecked(slot.name, MaximumRenderSceneNameBytes, copiedName);

        scene = {slotIndex, slot.generation};
        if (!m_impl->AddFramePipelineScene(scene, slot))
        {
            slot.state = RenderSceneState::Vacant;
            m_impl->DestroyRelinkState(m_impl->relinkStates[slotIndex]);
            scene = {};
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene frame-pipeline directory insertion failed");
        }
        if (m_impl->cameraStorage != nullptr && !m_impl->cameraStorage->AttachScene(scene, desc.maximumViews))
        {
            m_impl->RemoveFramePipelineScene(slot);
            slot.state = RenderSceneState::Vacant;
            m_impl->DestroyRelinkState(m_impl->relinkStates[slotIndex]);
            scene = {};
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderScene camera storage allocation failed");
        }
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AttachScene(scene, desc.maximumProxies, desc.maximumPendingProxyMutations))
        {
            if (m_impl->cameraStorage != nullptr)
                static_cast<void>(m_impl->cameraStorage->DetachScene(scene));
            m_impl->RemoveFramePipelineScene(slot);
            slot.state = RenderSceneState::Vacant;
            m_impl->DestroyRelinkState(m_impl->relinkStates[slotIndex]);
            scene = {};
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderScene GPU publication state allocation failed");
        }
        ++m_impl->stats.activeScenes;
        ++m_impl->stats.createdScenes;
        return true;
    }

    bool RenderSceneManager::DestroyScene(const RenderSceneHandle scene, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderScene destruction must run on the main thread", scene);
        }
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(scene))
        {
            ++m_impl->stats.failedDestroys;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle", scene);
        }

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (slot.state != RenderSceneState::Alive)
        {
            ++m_impl->stats.failedDestroys;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene is not alive", scene);
        }

        if (slot.activeProxies != 0)
            return Fail(failure, RenderSceneFailureCode::ProxiesRemainAlive, "RenderScene destruction blocked by live proxies", scene);
        Impl::RelinkState* const relinkState = m_impl->relinkStates[scene.index];
        if (relinkState != nullptr && (relinkState->dispatched.GetValue() || relinkState->candidateProduction.GetValue()))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene destruction blocked by scene-update jobs", scene);
        if (slot.visibilityFeedbackAttached)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene destruction blocked by its visibility feedback service", scene);
        if (!m_impl->ValidFramePipelineScene(scene, slot))
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene frame-pipeline directory is inconsistent", scene);
        if (m_impl->cameraStorage != nullptr && !m_impl->cameraStorage->CanDetachScene(scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene destruction blocked by live cameras", scene);
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->DetachScene(scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene destruction waits for pending GPU publication or retirement", scene);
        if (m_impl->cameraStorage != nullptr && !m_impl->cameraStorage->DetachScene(scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene destruction blocked by live cameras", scene);
        m_impl->RemoveFramePipelineScene(slot);
        slot.state = RenderSceneState::Vacant;
        slot.mode = RenderSceneMode::Runtime;
        slot.ownership = RenderSceneOwnership::Service;
        slot.maximumProxies = 0;
        slot.maximumPendingProxyMutations = 0;
        slot.maximumViews = 0;
        slot.createdSerial = 0;
        ++slot.lifecycleRevision;
        slot.allowFramePipelineParticipation = false;
        slot.visibilityFeedbackAttached = false;
        slot.framePipelineSceneIndex = InvalidSlotIndex;
        slot.firstFreeProxy = InvalidSlotIndex;
        slot.firstFreeMeshPayload = InvalidSlotIndex;
        slot.firstFreeLightPayload = InvalidSlotIndex;
        slot.firstFreeDecalPayload = InvalidSlotIndex;
        slot.name[0] = '\0';
        Impl::ResetProducerDirectory(slot);
        slot.proxies.Clear();
        slot.meshPayloads.Clear();
        slot.lightPayloads.Clear();
        slot.decalPayloads.Clear();
        m_impl->ConfigureSpatialIndex(slot, {});
        if (slot.pendingMutationCount != 0)
        {
            m_impl->stats.pendingProxyMutations -= slot.pendingMutationCount;
            slot.pendingMutationCount = 0;
        }
        m_impl->DestroyRelinkState(m_impl->relinkStates[scene.index]);
        --m_impl->stats.activeScenes;
        ++m_impl->stats.destroyedScenes;
        return true;
    }

    bool RenderSceneManager::CreateProxy(const RenderProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderProxy creation must run on the main thread", desc.scene);
        }
        if (!m_impl->ValidAliveScene(desc.scene))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for RenderProxy creation", desc.scene);
        }
        if (desc.typeId == 0 || !ValidTransform(desc.transform) || !ValidBounds(desc.bounds) || !ValidSpatialMode(desc.spatialMode) || (desc.producer.IsValid() != desc.contributor.IsValid()))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid RenderProxy creation descriptor", desc.scene);
        }

        Impl::SceneSlot& scene = m_impl->slots[desc.scene.index];
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(desc.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy creation is blocked by an open GPU publication", desc.scene);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(desc.scene))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy creation is blocked between scene preparation and commit", desc.scene);
        }
        if (desc.spatialMode == RenderProxySpatialMode::Bounds && !m_impl->SpatialBoundsAccepted(scene, desc.bounds))
        {
            ++m_impl->stats.failedProxyCreates;
            ++scene.spatial.stats.outOfRangeProxies;
            ++m_impl->stats.spatialOutOfRangeProxies;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "RenderProxy bounds are outside the configured spatial extent", desc.scene);
        }
        if (scene.activeProxies >= scene.maximumProxies)
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "maximum RenderProxy count exceeded", desc.scene);
        }
        if (desc.producer.IsValid())
        {
            if (Impl::FindProducerProxyIndex(scene, desc.producer, desc.contributor) != InvalidSlotIndex)
                return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy producer contributor already exists", desc.scene);
        }

        char copiedName[MaximumRenderProxyNameBytes]{};
        if (!CopyName(copiedName, MaximumRenderProxyNameBytes, desc.debugName))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "RenderProxy debug name is missing or too long", desc.scene);
        }
        const u32 slotIndex = Impl::AcquireSlot(scene.proxies, scene.firstFreeProxy);
        Impl::RelinkState* const relinks = m_impl->relinkStates[desc.scene.index];
        if (relinks == nullptr || !relinks->MaterializeProxyPage(slotIndex))
        {
            Impl::ReleaseProxySlot(scene, slotIndex);
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy relink metadata allocation failed", desc.scene);
        }
        Impl::ProxySlot& slot = scene.proxies[slotIndex];
        const u32 admittedGeneration = NextGeneration(slot.generation);
        slot = {};
        slot.state = RenderProxyState::Alive;
        slot.generation = admittedGeneration;
        slot.typeId = desc.typeId;
        slot.producerId = desc.producerId;
        slot.producerGeneration = desc.producerGeneration;
        slot.producer = desc.producer;
        slot.contributor = desc.contributor;
        slot.transform = desc.transform;
        slot.bounds = desc.bounds;
        slot.spatialMode = desc.spatialMode;
        slot.spatial = {};
        slot.visibility = desc.visibility;
        slot.layerMask = desc.layerMask;
        slot.visibilityMask = desc.visibilityMask;
        slot.userDataEpoch = desc.userDataEpoch;
        slot.teleportRevision = 0;
        slot.createdSerial = m_impl->nextProxyCreatedSerial++;
        slot.lifecycleRevision = 1;
        CopyNameUnchecked(slot.debugName, MaximumRenderProxyNameBytes, copiedName);

        proxy = {desc.scene, slotIndex, slot.generation};
        if (!relinks->InitializeProxyMetadata(proxy))
        {
            Impl::ReleaseProxySlot(scene, slotIndex);
            proxy = {};
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy relink metadata was not retired", desc.scene);
        }
        if (!m_impl->RegisterMutation(desc.scene, scene))
        {
            Impl::ReleaseProxySlot(scene, slotIndex);
            proxy = {};
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy creation mutation budget exceeded", desc.scene, proxy);
        }
        if (!m_impl->InsertSpatialEntry(scene, proxy, slot))
        {
            Impl::ReleaseProxySlot(scene, slotIndex);
            proxy = {};
            m_impl->RollbackMutation(scene);
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "RenderProxy spatial insertion failed", desc.scene, proxy);
        }
        if (slot.producer.IsValid() && !Impl::LinkProducerProxy(scene, proxy))
        {
            m_impl->RemoveSpatialEntry(scene, slot);
            Impl::ReleaseProxySlot(scene, slotIndex);
            proxy = {};
            m_impl->RollbackMutation(scene);
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy producer linkage failed", desc.scene, proxy);
        }
        ++scene.activeProxies;
        ++scene.lifecycleRevision;
        ++m_impl->stats.activeProxies;
        ++m_impl->stats.createdProxies;
        return true;
    }

    bool RenderSceneManager::CreateMeshProxy(const MeshProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        const MeshDrawableInfo* const drawable = desc.drawable != nullptr ? desc.drawable->GetDrawable() : nullptr;
        if (!desc.mesh.IsValid() || (desc.drawable != nullptr && (drawable == nullptr || !drawable->renderable.IsValid())))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "MeshProxy requires a valid mesh reference and any supplied drawable must be ready", desc.proxy.scene);
        MeshDrawableBinding retainedDrawable;
        if (desc.drawable != nullptr && !desc.drawable->Retain(retainedDrawable))
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "MeshProxy could not retain its drawable binding", desc.proxy.scene);

        RenderSceneGpuIdentity gpuIdentity;
        if (m_impl != nullptr && m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->ReserveIdentity(RenderSceneGpuObjectKind::Instance, gpuIdentity))
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "MeshProxy GPU identity allocation failed", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0)
            baseDesc.typeId = MeshProxyTypeId;
        if (!CreateProxy(baseDesc, proxy, failure))
        {
            if (m_impl != nullptr && m_impl->gpuPublisher != nullptr)
                m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
            return false;
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        const u32 payloadIndex = Impl::AcquireSlot(scene.meshPayloads, scene.firstFreeMeshPayload);
        Impl::MeshPayloadSlot& payload = scene.meshPayloads[payloadIndex];
        payload = {};
        payload.state = RenderProxyState::Alive;
        payload.proxy = proxy;
        payload.generation = proxy.generation;
        payload.mesh = desc.mesh;
        payload.material = desc.material;
        payload.meshHandle = desc.meshHandle;
        payload.materialHandle = desc.materialHandle;
        payload.candidateDrawable = static_cast<MeshDrawableBinding&&>(retainedDrawable);
        payload.submeshMask = desc.submeshMask;
        payload.renderFlags = desc.renderFlags;

        base.payloadKind = RenderProxyPayloadKind::Mesh;
        base.payloadIndex = payloadIndex;
        base.payloadGeneration = proxy.generation;
        ++m_impl->stats.activeMeshPayloads;
        ++m_impl->stats.createdPayloads;
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->TrackProxy(proxy, gpuIdentity))
        {
            m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
            static_cast<void>(DestroyProxy(proxy));
            proxy = {};
            return Fail(failure, RenderSceneFailureCode::InvalidState, "MeshProxy GPU identity tracking failed", desc.proxy.scene);
        }
        if (m_impl->gpuPublisher != nullptr && drawable != nullptr)
        {
            RenderSceneGpuFailure gpuFailure;
            const RenderSceneGpuMeshBinding binding{drawable->renderable, {}};
            if (!m_impl->gpuPublisher->BindMesh(proxy, binding, payload.bindingReceipt, &gpuFailure))
            {
                static_cast<void>(DestroyProxy(proxy));
                proxy = {};
                return Fail(failure, gpuFailure.code == RenderSceneGpuFailureCode::Busy ? RenderSceneFailureCode::Busy : RenderSceneFailureCode::InvalidState,
                            gpuFailure.message != nullptr ? gpuFailure.message : "MeshProxy GPU drawable binding failed", desc.proxy.scene);
            }
            Impl::QueueMeshBinding(scene, payloadIndex);
        }
        else if (drawable != nullptr)
            payload.activeDrawable = static_cast<MeshDrawableBinding&&>(payload.candidateDrawable);
        return true;
    }

    bool RenderSceneManager::CreateLightProxy(const LightProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (!ValidLightProperties(desc.kind, desc.color, desc.intensity, desc.range, desc.innerConeRadians, desc.outerConeRadians))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "LightProxy requires non-negative intensity and range", desc.proxy.scene);

        RenderSceneGpuIdentity gpuIdentity;
        if (m_impl != nullptr && m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->ReserveIdentity(RenderSceneGpuObjectKind::Light, gpuIdentity))
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "LightProxy GPU identity allocation failed", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0)
            baseDesc.typeId = LightProxyTypeId;
        if (desc.kind == RenderLightKind::Directional)
            baseDesc.spatialMode = RenderProxySpatialMode::Global;
        baseDesc.bounds = LightBounds(baseDesc.transform, desc.kind, desc.range);
        if (!CreateProxy(baseDesc, proxy, failure))
        {
            if (m_impl != nullptr && m_impl->gpuPublisher != nullptr)
                m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
            return false;
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        const u32 payloadIndex = Impl::AcquireSlot(scene.lightPayloads, scene.firstFreeLightPayload);
        Impl::LightPayloadSlot& payload = scene.lightPayloads[payloadIndex];
        payload = {};
        payload.state = RenderProxyState::Alive;
        payload.proxy = proxy;
        payload.generation = proxy.generation;
        payload.kind = desc.kind;
        payload.color[0] = desc.color[0];
        payload.color[1] = desc.color[1];
        payload.color[2] = desc.color[2];
        payload.intensity = desc.intensity;
        payload.range = desc.range;
        payload.innerConeRadians = desc.innerConeRadians;
        payload.outerConeRadians = desc.outerConeRadians;
        payload.castsShadow = desc.castsShadow;

        base.payloadKind = RenderProxyPayloadKind::Light;
        base.payloadIndex = payloadIndex;
        base.payloadGeneration = proxy.generation;
        ++m_impl->stats.activeLightPayloads;
        ++m_impl->stats.createdPayloads;
        if (m_impl->gpuPublisher != nullptr)
        {
            const bool tracked = m_impl->gpuPublisher->TrackProxy(proxy, gpuIdentity);
            if (!tracked)
            {
                m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
                static_cast<void>(DestroyProxy(proxy));
                proxy = {};
                return Fail(failure, RenderSceneFailureCode::InvalidState, "LightProxy GPU identity tracking failed", desc.proxy.scene);
            }
        }
        payload.gpuIdentity = gpuIdentity.allocation.AsSlotHandle<GpuLightHandle>();
        return true;
    }

    bool RenderSceneManager::CreateDecalProxy(const DecalProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (!desc.material.IsValid() || !std::isfinite(desc.extents[0]) || !std::isfinite(desc.extents[1]) || !std::isfinite(desc.extents[2]) || !std::isfinite(desc.fadeDistance) || desc.extents[0] < 0.0f ||
            desc.extents[1] < 0.0f || desc.extents[2] < 0.0f || desc.fadeDistance < 0.0f)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "DecalProxy requires a valid material and non-negative extents", desc.proxy.scene);

        RenderSceneGpuIdentity gpuIdentity;
        if (m_impl != nullptr && m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->ReserveIdentity(RenderSceneGpuObjectKind::Decal, gpuIdentity))
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "DecalProxy GPU identity allocation failed", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0)
            baseDesc.typeId = DecalProxyTypeId;
        if (!CreateProxy(baseDesc, proxy, failure))
        {
            if (m_impl != nullptr && m_impl->gpuPublisher != nullptr)
                m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
            return false;
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];

        const u32 payloadIndex = Impl::AcquireSlot(scene.decalPayloads, scene.firstFreeDecalPayload);
        Impl::DecalPayloadSlot& payload = scene.decalPayloads[payloadIndex];
        payload = {};
        payload.state = RenderProxyState::Alive;
        payload.proxy = proxy;
        payload.generation = proxy.generation;
        payload.material = desc.material;
        payload.materialHandle = desc.materialHandle;
        payload.extents[0] = desc.extents[0];
        payload.extents[1] = desc.extents[1];
        payload.extents[2] = desc.extents[2];
        payload.fadeDistance = desc.fadeDistance;
        payload.sortKey = desc.sortKey;

        base.payloadKind = RenderProxyPayloadKind::Decal;
        base.payloadIndex = payloadIndex;
        base.payloadGeneration = proxy.generation;
        ++m_impl->stats.activeDecalPayloads;
        ++m_impl->stats.createdPayloads;
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->TrackProxy(proxy, gpuIdentity))
        {
            m_impl->gpuPublisher->CancelIdentity(gpuIdentity);
            static_cast<void>(DestroyProxy(proxy));
            proxy = {};
            return Fail(failure, RenderSceneFailureCode::InvalidState, "DecalProxy GPU identity tracking failed", desc.proxy.scene);
        }
        return true;
    }

    bool RenderSceneManager::CreateProducerMeshProxy(const RenderProducerHandle producer, const RenderContributorId contributor, const MeshProxyDesc& desc, RenderProxyHandle& proxy,
                                                     RenderSceneFailure* const failure) noexcept
    {
        MeshProxyDesc producerDesc = desc;
        producerDesc.proxy.producer = producer;
        producerDesc.proxy.contributor = contributor;
        return CreateMeshProxy(producerDesc, proxy, failure);
    }

    bool RenderSceneManager::CreateProducerLightProxy(const RenderProducerHandle producer, const RenderContributorId contributor, const LightProxyDesc& desc, RenderProxyHandle& proxy,
                                                      RenderSceneFailure* const failure) noexcept
    {
        LightProxyDesc producerDesc = desc;
        producerDesc.proxy.producer = producer;
        producerDesc.proxy.contributor = contributor;
        return CreateLightProxy(producerDesc, proxy, failure);
    }

    bool RenderSceneManager::CreateProducerDecalProxy(const RenderProducerHandle producer, const RenderContributorId contributor, const DecalProxyDesc& desc, RenderProxyHandle& proxy,
                                                      RenderSceneFailure* const failure) noexcept
    {
        DecalProxyDesc producerDesc = desc;
        producerDesc.proxy.producer = producer;
        producerDesc.proxy.contributor = contributor;
        return CreateDecalProxy(producerDesc, proxy, failure);
    }

    bool RenderSceneManager::DestroyProxy(const RenderProxyHandle proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderProxy destruction must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderProxy handle", proxy.scene, proxy);
        }

        RenderSceneGpuIdentity gpuIdentity;
        const bool gpuTracked = m_impl->gpuPublisher != nullptr && m_impl->gpuPublisher->GetIdentity(proxy, gpuIdentity);
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy destruction is blocked by an open GPU publication", proxy.scene, proxy);

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::RelinkState* const relinks = m_impl->relinkStates[proxy.scene.index];
        if (relinks == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene relink state is unavailable", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy destruction is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        if (slot.state == RenderProxyState::Destroying)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::PendingDestroy, "RenderProxy destruction is already pending", proxy.scene, proxy);
        }
        if (slot.state != RenderProxyState::Alive)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy is not alive", proxy.scene, proxy);
        }
        const Impl::RelinkState::CloseProxyResult closeResult = relinks->CloseProxy(proxy);
        if (closeResult == Impl::RelinkState::CloseProxyResult::Outstanding)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy destruction waits for outstanding relinks", proxy.scene, proxy);
        if (closeResult == Impl::RelinkState::CloseProxyResult::Stale)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy relink metadata is stale or unavailable", proxy.scene, proxy);
        if (!m_impl->RegisterMutation(proxy.scene, scene, true))
        {
            relinks->ReopenProxy(proxy);
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy destruction mutation budget exceeded", proxy.scene, proxy);
        }
        if (gpuTracked && !m_impl->gpuPublisher->RetireProxy(proxy))
        {
            m_impl->RollbackMutation(scene);
            relinks->ReopenProxy(proxy);
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy GPU retirement journaling failed", proxy.scene, proxy);
        }

        m_impl->RemoveSpatialEntry(scene, slot);
        switch (slot.payloadKind)
        {
        case RenderProxyPayloadKind::Mesh:
            if (slot.payloadIndex < scene.meshPayloads.Size() && scene.meshPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                Impl::RemoveMeshBinding(scene, slot.payloadIndex);
                Impl::ReleasePayloadSlot(scene.meshPayloads, scene.firstFreeMeshPayload, slot.payloadIndex);
                --m_impl->stats.activeMeshPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::Light:
            if (slot.payloadIndex < scene.lightPayloads.Size() && scene.lightPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                Impl::ReleasePayloadSlot(scene.lightPayloads, scene.firstFreeLightPayload, slot.payloadIndex);
                --m_impl->stats.activeLightPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::Decal:
            if (slot.payloadIndex < scene.decalPayloads.Size() && scene.decalPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                Impl::ReleasePayloadSlot(scene.decalPayloads, scene.firstFreeDecalPayload, slot.payloadIndex);
                --m_impl->stats.activeDecalPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::None:
            break;
        }
        slot.payloadKind = RenderProxyPayloadKind::None;
        slot.payloadIndex = InvalidSlotIndex;
        slot.payloadGeneration = 0;
        ++slot.lifecycleRevision;
        Impl::UnlinkProducerProxy(scene, slot);
        Impl::ReleaseProxySlot(scene, proxy.index);
        ++scene.lifecycleRevision;
        --scene.activeProxies;
        --m_impl->stats.activeProxies;
        ++m_impl->stats.destroyedProxies;
        return true;
    }

    bool RenderSceneManager::DestroyProducerContribution(const RenderSceneHandle sceneHandle, const RenderProducerHandle producer, const RenderContributorId contributor, RenderProxyHandle* const destroyed,
                                                         RenderSceneFailure* const failure) noexcept
    {
        if (destroyed != nullptr)
            *destroyed = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", sceneHandle);
        if (!m_impl->ValidAliveScene(sceneHandle) || !producer.IsValid() || !contributor.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene producer contribution", sceneHandle);
        const Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        const u32 proxyIndex = Impl::FindProducerProxyIndex(scene, producer, contributor);
        if (proxyIndex == InvalidSlotIndex)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "RenderScene producer contribution is not present", sceneHandle);
        const RenderProxyHandle proxy{sceneHandle, proxyIndex, scene.proxies[proxyIndex].generation};
        if (!DestroyProxy(proxy, failure))
            return false;
        if (destroyed != nullptr)
            *destroyed = proxy;
        return true;
    }

    bool RenderSceneManager::DestroyProducer(const RenderSceneHandle sceneHandle, const RenderProducerHandle producer, containers::DynamicArray<RenderProducerProxy>* const destroyed,
                                             RenderSceneFailure* const failure) noexcept
    {
        if (destroyed != nullptr)
            destroyed->Clear();
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", sceneHandle);
        if (!m_impl->ValidAliveScene(sceneHandle) || !producer.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene producer", sceneHandle);
        Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        const Impl::ProducerSlot* owner = Impl::ResolveProducer(scene, producer);
        if (owner == nullptr)
            return true;
        if (destroyed != nullptr)
            destroyed->Reserve(owner->proxyCount);
        while ((owner = Impl::ResolveProducer(scene, producer)) != nullptr && owner->firstProxy != InvalidSlotIndex)
        {
            const u32 proxyIndex = owner->firstProxy;
            const Impl::ProxySlot& slot = scene.proxies[proxyIndex];
            const RenderProxyHandle proxy{sceneHandle, proxyIndex, slot.generation};
            const RenderContributorId contributor = slot.contributor;
            if (!DestroyProxy(proxy, failure))
                return false;
            if (destroyed != nullptr)
                destroyed->PushBack({producer, contributor, proxy});
        }
        return true;
    }

    // WARNING: This cold reset scans every proxy in the scene. Destruction is not transactional;
    // callers must process the returned partial result if a later proxy destruction fails.
    bool RenderSceneManager::DestroySceneProducers(const RenderSceneHandle sceneHandle, const u64 producerGeneration, containers::DynamicArray<RenderProducerProxy>& destroyed,
                                                   RenderSceneFailure* const failure) noexcept
    {
        destroyed.Clear();
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", sceneHandle);
        if (!m_impl->ValidAliveScene(sceneHandle))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene for producer reset", sceneHandle);
        Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        destroyed.Reserve(scene.activeProxies);
        for (u32 index = 0; index < scene.proxies.Size(); ++index)
        {
            const Impl::ProxySlot& slot = scene.proxies[index];
            if (slot.state != RenderProxyState::Alive || !slot.producer.IsValid() || slot.producerGeneration != producerGeneration)
                continue;
            const RenderProducerProxy detached{slot.producer, slot.contributor, {sceneHandle, index, slot.generation}};
            if (!DestroyProxy(detached.proxy, failure))
                return false;
            destroyed.PushBack(detached);
        }
        return true;
    }

    bool RenderSceneManager::UpdateProxyTransform(const RenderProxyHandle proxy, const RenderProxyTransform& transform, const RenderProxyBounds& bounds, const u64 producerGeneration,
                                                  RenderSceneFailure* const failure) noexcept
    {
        RenderProxyRelinkRequest request;
        request.proxy = proxy;
        request.transform = transform;
        request.bounds = bounds;
        request.producerGeneration = producerGeneration;
        return ScheduleRelink(request, failure);
    }

    bool RenderSceneManager::BeginProxyRetirement(const RenderProxyHandle proxy, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "proxy retirement requires the owner thread", proxy.scene, proxy);
        if (m_impl == nullptr || !m_impl->ValidAliveProxy(proxy))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "proxy retirement requires a live proxy", proxy.scene, proxy);
        auto& slot = m_impl->slots[proxy.scene.index].proxies[proxy.index];
        if (slot.retirementPending)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy is already pending retirement", proxy.scene, proxy);
        if (!UpdateProxyVisibility(proxy, RenderProxyVisibilityFlags::None, 0, failure))
            return false;
        slot.retirementPending = true;
        return true;
    }

    bool RenderSceneManager::UpdateProxyVisibility(const RenderProxyHandle proxy, const RenderProxyVisibilityFlags visibility, const u32 visibilityMask, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderProxy visibility update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& currentVisibility = scene.proxies[proxy.index];
        if (currentVisibility.visibility == visibility && currentVisibility.visibilityMask == visibilityMask)
            return true;
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy visibility update is blocked by an open GPU publication", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy visibility update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy visibility mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.visibility = visibility;
        slot.visibilityMask = visibilityMask;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        if (m_impl->gpuPublisher != nullptr)
            static_cast<void>(m_impl->gpuPublisher->MarkProxyDirty(proxy, RenderSceneGpuDirtyFlags::Visibility));
        return true;
    }

    bool RenderSceneManager::UpdateProxyLayerMask(const RenderProxyHandle proxy, const u64 layerMask, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderProxy layer update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& currentLayer = scene.proxies[proxy.index];
        if (currentLayer.layerMask == layerMask)
            return true;
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy layer update is blocked by an open GPU publication", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy layer update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy layer mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.layerMask = layerMask;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        if (m_impl->gpuPublisher != nullptr)
            static_cast<void>(m_impl->gpuPublisher->MarkProxyDirty(proxy, RenderSceneGpuDirtyFlags::Visibility));
        return true;
    }

    bool RenderSceneManager::UpdateProxyUserDataEpoch(const RenderProxyHandle proxy, const u64 userDataEpoch, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderProxy user-data update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& currentUserData = scene.proxies[proxy.index];
        if (currentUserData.userDataEpoch == userDataEpoch)
            return true;
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy user-data update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy user-data mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.userDataEpoch = userDataEpoch;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateMeshProxy(const RenderProxyHandle proxy, const MeshProxyUpdate& update, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "MeshProxy payload update must run on the main thread", proxy.scene, proxy);
        }
        if (!ValidFields(update.fields) || (HasField(update.fields, MeshProxyUpdateFields::Resources) && !update.mesh.IsValid()))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid MeshProxy payload update", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy does not own a live mesh payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "MeshProxy payload update is blocked by an open GPU publication", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "MeshProxy payload update is blocked between scene preparation and commit", proxy.scene, proxy);
        }

        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::MeshPayloadSlot& payload = scene.meshPayloads[base.payloadIndex];
        const bool drawableChanged = HasField(update.fields, MeshProxyUpdateFields::Drawable);
        const MeshDrawableInfo* const drawable = drawableChanged && update.drawable != nullptr ? update.drawable->GetDrawable() : nullptr;
        if (drawableChanged && update.drawable != nullptr && (drawable == nullptr || !drawable->renderable.IsValid()))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "mesh drawable replacement is not ready", proxy.scene, proxy);
        MeshDrawableBinding retainedDrawable;
        if (drawableChanged && update.drawable != nullptr && !update.drawable->Retain(retainedDrawable))
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "mesh drawable replacement could not be retained", proxy.scene, proxy);
        const bool resourcesChanged =
            HasField(update.fields, MeshProxyUpdateFields::Resources) && (payload.mesh != update.mesh || payload.material != update.material || !SameResourceHandle(payload.meshHandle, update.meshHandle) ||
                                                                          !SameResourceHandle(payload.materialHandle, update.materialHandle));
        const bool submeshesChanged = HasField(update.fields, MeshProxyUpdateFields::SubmeshSelection) && payload.submeshMask != update.submeshMask;
        const bool flagsChanged = HasField(update.fields, MeshProxyUpdateFields::RenderFlags) && payload.renderFlags != update.renderFlags;
        if (!resourcesChanged && !submeshesChanged && !flagsChanged && !drawableChanged)
            return true;
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "MeshProxy payload mutation budget exceeded", proxy.scene, proxy);
        }
        const bool bindingPublication = m_impl->gpuPublisher != nullptr && (drawableChanged || resourcesChanged);
        if (m_impl->gpuPublisher != nullptr && !bindingPublication && (submeshesChanged || flagsChanged) &&
            !m_impl->gpuPublisher->MarkProxyDirty(proxy, RenderSceneGpuDirtyFlags::Properties))
        {
            m_impl->RollbackMutation(scene);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "mesh property update GPU journal admission failed", proxy.scene, proxy);
        }
        if (bindingPublication)
        {
            RenderSceneGpuFailure gpuFailure;
            RenderSceneGpuBindingReceipt bindingReceipt;
            const bool bound = drawableChanged && drawable != nullptr
                                   ? m_impl->gpuPublisher->BindMesh(proxy, {drawable->renderable, {}}, bindingReceipt, &gpuFailure)
                                   : m_impl->gpuPublisher->ClearMeshBinding(proxy, bindingReceipt, &gpuFailure);
            if (!bound)
            {
                m_impl->RollbackMutation(scene);
                return Fail(failure, gpuFailure.code == RenderSceneGpuFailureCode::Busy ? RenderSceneFailureCode::Busy : RenderSceneFailureCode::InvalidState,
                            gpuFailure.message != nullptr ? gpuFailure.message : "mesh drawable publication failed", proxy.scene, proxy);
            }
            payload.bindingReceipt = bindingReceipt;
            payload.candidateDrawable = static_cast<MeshDrawableBinding&&>(retainedDrawable);
            payload.clearingBinding = !drawableChanged || drawable == nullptr;
            Impl::QueueMeshBinding(scene, base.payloadIndex);
        }
        else if (drawableChanged || resourcesChanged)
        {
            payload.activeDrawable = static_cast<MeshDrawableBinding&&>(retainedDrawable);
            payload.candidateDrawable.Reset();
            payload.bindingReceipt = {};
            payload.clearingBinding = update.drawable == nullptr;
        }
        if (resourcesChanged)
        {
            payload.mesh = update.mesh;
            payload.material = update.material;
            payload.meshHandle = update.meshHandle;
            payload.materialHandle = update.materialHandle;
        }
        if (submeshesChanged)
            payload.submeshMask = update.submeshMask;
        if (flagsChanged)
            payload.renderFlags = update.renderFlags;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::ResolveMeshBindings(const u32 maximumChecks, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread() || maximumChecks == 0)
            return Fail(failure, m_impl == nullptr ? RenderSceneFailureCode::NotInitialized :
                                 (!concurrency::IsMainThread() ? RenderSceneFailureCode::WrongThread : RenderSceneFailureCode::InvalidDescriptor),
                        "mesh binding resolution requires an initialized owner thread and a positive budget");
        if (m_impl->gpuPublisher == nullptr)
            return true;
        constexpr u32 BatchCapacity = 256;
        RenderSceneGpuBindingReceipt receipts[BatchCapacity];
        RenderSceneGpuBindingStatus statuses[BatchCapacity];
        u32 payloadIndices[BatchCapacity];
        u32 remaining = maximumChecks < BatchCapacity ? maximumChecks : BatchCapacity;
        const u32 sceneCount = m_impl->slots.Size();
        if (sceneCount == 0)
            return true;
        u32 sceneIndex = m_impl->nextMeshBindingScene < sceneCount ? m_impl->nextMeshBindingScene : 0;
        for (u32 visited = 0; visited < sceneCount && remaining != 0; ++visited)
        {
            Impl::SceneSlot& scene = m_impl->slots[sceneIndex];
            if (scene.state == RenderSceneState::Alive && scene.pendingMeshBindingCount != 0)
            {
                const u32 count = scene.pendingMeshBindingCount < remaining ? scene.pendingMeshBindingCount : remaining;
                u32 payloadIndex = scene.firstPendingMeshBinding;
                for (u32 index = 0; index < count; ++index)
                {
                    Impl::MeshPayloadSlot& payload = scene.meshPayloads[payloadIndex];
                    payloadIndices[index] = payloadIndex;
                    receipts[index] = payload.bindingReceipt;
                    payloadIndex = payload.nextBinding;
                }
                const RenderSceneHandle sceneHandle{sceneIndex, scene.generation};
                if (!m_impl->gpuPublisher->PollBindings(sceneHandle, {receipts, count}, {statuses, count}))
                    return Fail(failure, RenderSceneFailureCode::InvalidState, "mesh binding acceptance could not be read", sceneHandle);
                for (u32 index = 0; index < count; ++index)
                {
                    Impl::MeshPayloadSlot& payload = scene.meshPayloads[payloadIndices[index]];
                    if (statuses[index] == RenderSceneGpuBindingStatus::Accepted)
                    {
                        if (payload.clearingBinding)
                            payload.activeDrawable.Reset();
                        else
                            payload.activeDrawable = static_cast<MeshDrawableBinding&&>(payload.candidateDrawable);
                        payload.candidateDrawable.Reset();
                        payload.bindingReceipt = {};
                        payload.clearingBinding = false;
                        Impl::RemoveMeshBinding(scene, payloadIndices[index]);
                    }
                    else if (statuses[index] == RenderSceneGpuBindingStatus::Stale)
                        return Fail(failure, RenderSceneFailureCode::InvalidState, "mesh binding receipt was superseded before acceptance", sceneHandle, payload.proxy);
                    else
                    {
                        Impl::RemoveMeshBinding(scene, payloadIndices[index]);
                        Impl::QueueMeshBinding(scene, payloadIndices[index]);
                    }
                }
                remaining -= count;
            }
            sceneIndex = sceneIndex + 1u < sceneCount ? sceneIndex + 1u : 0u;
        }
        m_impl->nextMeshBindingScene = sceneIndex;
        return true;
    }

    bool RenderSceneManager::RetainMeshDrawable(const RenderProxyHandle proxy, MeshDrawableBinding& output, RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? RenderSceneFailureCode::NotInitialized : RenderSceneFailureCode::WrongThread,
                        "accepted mesh binding retention requires the owner thread", proxy.scene, proxy);
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "accepted mesh binding requires a live mesh proxy", proxy.scene, proxy);
        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& base = scene.proxies[proxy.index];
        const Impl::MeshPayloadSlot& payload = scene.meshPayloads[base.payloadIndex];
        if (!payload.activeDrawable.Retain(output))
            return Fail(failure, RenderSceneFailureCode::Busy, "mesh proxy has no accepted drawable binding", proxy.scene, proxy);
        return true;
    }

    bool RenderSceneManager::UpdateLightProxy(const RenderProxyHandle proxy, const LightProxyUpdate& update, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "LightProxy payload update must run on the main thread", proxy.scene, proxy);
        }
        if (!ValidFields(update.fields))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid LightProxy payload update", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Light))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy does not own a live light payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "LightProxy payload update is blocked by an open GPU publication", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "LightProxy payload update is blocked between scene preparation and commit", proxy.scene, proxy);
        }

        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::LightPayloadSlot& payload = scene.lightPayloads[base.payloadIndex];
        const RenderLightKind kind = HasField(update.fields, LightProxyUpdateFields::Kind) ? update.kind : payload.kind;
        const f32* const color = HasField(update.fields, LightProxyUpdateFields::Color) ? update.color : payload.color;
        const f32 intensity = HasField(update.fields, LightProxyUpdateFields::Photometry) ? update.intensity : payload.intensity;
        const f32 range = HasField(update.fields, LightProxyUpdateFields::Photometry) ? update.range : payload.range;
        const f32 innerCone = HasField(update.fields, LightProxyUpdateFields::Cones) ? update.innerConeRadians : payload.innerConeRadians;
        const f32 outerCone = HasField(update.fields, LightProxyUpdateFields::Cones) ? update.outerConeRadians : payload.outerConeRadians;
        if (!ValidLightProperties(kind, color, intensity, range, innerCone, outerCone))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid LightProxy payload values", proxy.scene, proxy);
        }

        const bool kindChanged = HasField(update.fields, LightProxyUpdateFields::Kind) && payload.kind != update.kind;
        const bool colorChanged = HasField(update.fields, LightProxyUpdateFields::Color) && (payload.color[0] != update.color[0] || payload.color[1] != update.color[1] || payload.color[2] != update.color[2]);
        const bool photometryChanged = HasField(update.fields, LightProxyUpdateFields::Photometry) && (payload.intensity != update.intensity || payload.range != update.range);
        const bool conesChanged = HasField(update.fields, LightProxyUpdateFields::Cones) && (payload.innerConeRadians != update.innerConeRadians || payload.outerConeRadians != update.outerConeRadians);
        const bool shadowChanged = HasField(update.fields, LightProxyUpdateFields::Shadow) && payload.castsShadow != update.castsShadow;
        const bool filteringChanged = update.updateFiltering &&
            (base.visibility != update.visibility || base.visibilityMask != update.visibilityMask || base.layerMask != update.layerMask);
        const bool influenceChanged = kindChanged || range != payload.range;
        const RenderProxyBounds bounds = influenceChanged ? LightBounds(base.transform, kind, range) : base.bounds;
        const RenderProxySpatialMode spatialMode = kindChanged
            ? (kind == RenderLightKind::Directional ? RenderProxySpatialMode::Global
                                                   : (base.spatialMode == RenderProxySpatialMode::Global ? RenderProxySpatialMode::Bounds : base.spatialMode))
            : base.spatialMode;
        if (!ValidBounds(bounds) || (spatialMode == RenderProxySpatialMode::Bounds && !m_impl->SpatialBoundsAccepted(scene, bounds)))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "local light bounds are outside the scene spatial extent", proxy.scene, proxy);
        auto* const relinks = m_impl->relinkStates[proxy.scene.index];
        if (influenceChanged)
        {
            // Use the existing per-proxy admission gate: a load/check alone
            // races a worker acquiring a relink with the old influence shape.
            if (relinks == nullptr || relinks->CloseProxy(proxy) != Impl::RelinkState::CloseProxyResult::Closed)
                return Fail(failure, RenderSceneFailureCode::Busy, "light influence update waits for outstanding transform relinks", proxy.scene, proxy);
        }

        if (!kindChanged && !colorChanged && !photometryChanged && !conesChanged && !shadowChanged && !filteringChanged)
            return true;
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            if (influenceChanged)
                relinks->ReopenProxy(proxy);
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "LightProxy payload mutation budget exceeded", proxy.scene, proxy);
        }
        const RenderSceneGpuDirtyFlags dirty = RenderSceneGpuDirtyFlags::Properties |
            (influenceChanged ? RenderSceneGpuDirtyFlags::Transform : RenderSceneGpuDirtyFlags::None) |
            (filteringChanged ? RenderSceneGpuDirtyFlags::Visibility : RenderSceneGpuDirtyFlags::None);
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->MarkProxyDirty(proxy, dirty))
        {
            m_impl->RollbackMutation(scene);
            if (influenceChanged)
                relinks->ReopenProxy(proxy);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "light update GPU journal admission failed", proxy.scene, proxy);
        }
        if (influenceChanged)
        {
            const RenderProxyBounds oldBounds = base.bounds;
            base.bounds = bounds;
            base.spatialMode = spatialMode;
            m_impl->MoveSpatialEntry(scene, proxy, base, oldBounds);
        }
        if (filteringChanged)
        {
            base.visibility = update.visibility;
            base.visibilityMask = update.visibilityMask;
            base.layerMask = update.layerMask;
        }
        if (kindChanged)
            payload.kind = update.kind;
        if (colorChanged)
        {
            payload.color[0] = update.color[0];
            payload.color[1] = update.color[1];
            payload.color[2] = update.color[2];
        }
        if (photometryChanged)
        {
            payload.intensity = update.intensity;
            payload.range = update.range;
        }
        if (conesChanged)
        {
            payload.innerConeRadians = update.innerConeRadians;
            payload.outerConeRadians = update.outerConeRadians;
        }
        if (shadowChanged)
            payload.castsShadow = update.castsShadow;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        if (influenceChanged)
            relinks->ReopenProxy(proxy);
        return true;
    }

    bool RenderSceneManager::UpdateDecalProxy(const RenderProxyHandle proxy, const DecalProxyUpdate& update, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread, "DecalProxy payload update must run on the main thread", proxy.scene, proxy);
        }
        if (!ValidFields(update.fields) || (HasField(update.fields, DecalProxyUpdateFields::Material) && !update.material.IsValid()) ||
            (HasField(update.fields, DecalProxyUpdateFields::Extents) &&
             (!std::isfinite(update.extents[0]) || !std::isfinite(update.extents[1]) || !std::isfinite(update.extents[2]) || update.extents[0] < 0.0f || update.extents[1] < 0.0f || update.extents[2] < 0.0f)) ||
            (HasField(update.fields, DecalProxyUpdateFields::FadeDistance) && (!std::isfinite(update.fadeDistance) || update.fadeDistance < 0.0f)))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid DecalProxy payload update", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Decal))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderProxy does not own a live decal payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (m_impl->gpuPublisher != nullptr && !m_impl->gpuPublisher->AllowsMutation(proxy.scene))
            return Fail(failure, RenderSceneFailureCode::Busy, "DecalProxy payload update is blocked by an open GPU publication", proxy.scene, proxy);
        if (scene.framePrepared || m_impl->CandidateProductionOpen(proxy.scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy, "DecalProxy payload update is blocked between scene preparation and commit", proxy.scene, proxy);
        }

        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::DecalPayloadSlot& payload = scene.decalPayloads[base.payloadIndex];
        const bool materialChanged = HasField(update.fields, DecalProxyUpdateFields::Material) && (payload.material != update.material || !SameResourceHandle(payload.materialHandle, update.materialHandle));
        const bool extentsChanged =
            HasField(update.fields, DecalProxyUpdateFields::Extents) && (payload.extents[0] != update.extents[0] || payload.extents[1] != update.extents[1] || payload.extents[2] != update.extents[2]);
        const bool fadeChanged = HasField(update.fields, DecalProxyUpdateFields::FadeDistance) && payload.fadeDistance != update.fadeDistance;
        const bool sortChanged = HasField(update.fields, DecalProxyUpdateFields::SortKey) && payload.sortKey != update.sortKey;
        if (!materialChanged && !extentsChanged && !fadeChanged && !sortChanged)
            return true;
        if (!m_impl->RegisterMutation(proxy.scene, scene))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "DecalProxy payload mutation budget exceeded", proxy.scene, proxy);
        }
        if (materialChanged)
        {
            payload.material = update.material;
            payload.materialHandle = update.materialHandle;
        }
        if (extentsChanged)
        {
            payload.extents[0] = update.extents[0];
            payload.extents[1] = update.extents[1];
            payload.extents[2] = update.extents[2];
        }
        if (fadeChanged)
            payload.fadeDistance = update.fadeDistance;
        if (sortChanged)
            payload.sortKey = update.sortKey;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        if (m_impl->gpuPublisher != nullptr)
        {
            if (extentsChanged || fadeChanged || sortChanged)
                static_cast<void>(m_impl->gpuPublisher->MarkProxyDirty(proxy, RenderSceneGpuDirtyFlags::Properties));
        }
        return true;
    }

    bool RenderSceneManager::ScheduleRelink(const RenderProxyRelinkRequest& input, RenderSceneFailure* const failure) noexcept
    {
        RenderProxyRelinkRequest request = input;
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", request.proxy.scene, request.proxy);
        if (!request.proxy.IsValid() || !ValidTransform(request.transform) || !ValidBounds(request.bounds))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid RenderProxy relink request", request.proxy.scene, request.proxy);

        concurrency::ScopedSharedLock<concurrency::RWLock> sceneGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(request.proxy.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for relink admission", request.proxy.scene, request.proxy);
        Impl::RelinkState* const relinks = m_impl->relinkStates[request.proxy.scene.index];
        if (relinks == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene relink state is unavailable", request.proxy.scene, request.proxy);
        bool alreadyOutstanding = false;
        const Impl::RelinkState::AcquireProxyResult admission = relinks->AcquireOutstanding(request.proxy, alreadyOutstanding);
        if (admission == Impl::RelinkState::AcquireProxyResult::Stale)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid, stale, or non-alive RenderProxy handle", request.proxy.scene, request.proxy);
        if (admission == Impl::RelinkState::AcquireProxyResult::Closed)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderProxy relink admission is closed for a structural mutation", request.proxy.scene, request.proxy);
        if (admission == Impl::RelinkState::AcquireProxyResult::CapacityExceeded)
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderProxy outstanding relink capacity exceeded", request.proxy.scene, request.proxy);

        if (!m_impl->ValidAliveProxy(request.proxy))
        {
            relinks->ReleaseOutstanding(request.proxy);
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid, stale, or non-alive RenderProxy handle", request.proxy.scene, request.proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[request.proxy.scene.index];
        const Impl::ProxySlot& proxy = scene.proxies[request.proxy.index];
        if (proxy.payloadKind == RenderProxyPayloadKind::Light)
        {
            const auto& light = scene.lightPayloads[proxy.payloadIndex];
            request.bounds = LightBounds(request.transform, light.kind, light.range);
        }
        if (!ValidBounds(request.bounds) || (proxy.spatialMode == RenderProxySpatialMode::Bounds && !m_impl->SpatialBoundsAccepted(scene, request.bounds)))
        {
            relinks->ReleaseOutstanding(request.proxy);
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "RenderProxy relink moves bounds outside the configured spatial extent", request.proxy.scene, request.proxy);
        }
        if (!alreadyOutstanding && !relinks->dispatched.GetValue() && !request.teleport && SameTransform(proxy.transform, request.transform) && SameBounds(proxy.bounds, request.bounds))
        {
            relinks->ReleaseOutstanding(request.proxy);
            return true;
        }
        concurrency::ScopedSharedLock<concurrency::RWSpinLock> indexGuard(relinks->pendingIndexLock);
        containers::DynamicArray<Impl::PendingRelinkRequest>& queue = relinks->Queue(relinks->pendingIndex);
        const u32 index = relinks->pendingCount.PostIncrement();
        if (index >= queue.Size())
        {
            static_cast<void>(relinks->pendingCount.PostDecrement());
            relinks->ReleaseOutstanding(request.proxy);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "RenderScene relink request capacity exceeded", request.proxy.scene, request.proxy);
        }

        Impl::PendingRelinkRequest& pending = queue[index];
        pending.input = request;
        pending.oldBounds = {};
        pending.active = true;
        pending.structuralMove = false;
        return true;
    }

    bool RenderSceneManager::PrepareSceneUpdate(const RenderSceneHandle scene, const u64 tickCounter, RenderSceneFramePrepareResult& result, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "RenderScene update preparation must run on the main thread", scene);
        if (!m_impl->ValidAliveScene(scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for update preparation", scene);

        Impl::RelinkState* const relinks = m_impl->relinkStates[scene.index];
        if (relinks == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene relink state is unavailable", scene);
        if (relinks->dispatched.GetValue() || relinks->candidateProduction.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "previous RenderScene update jobs have not completed", scene);
        if (relinks->prepared)
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene update is already prepared", scene);

        if (relinks->lastPreparedTick != tickCounter)
        {
            concurrency::ScopedLock<concurrency::RWSpinLock> indexGuard(relinks->pendingIndexLock);
            relinks->processIndex = relinks->pendingIndex;
            relinks->pendingIndex ^= 1u;
            const u32 submitted = relinks->pendingCount.Exchange(0);
            const u32 capacity = relinks->Queue(relinks->processIndex).Size();
            relinks->processCount = submitted < capacity ? submitted : capacity;
            relinks->lastPreparedTick = tickCounter;
        }

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        m_impl->RepairSpatialIndex(slot);
        const u32 commandMutationCount = slot.pendingMutationCount;
        if (slot.pendingMutationCount != 0)
        {
            m_impl->stats.pendingProxyMutations -= slot.pendingMutationCount;
            slot.pendingMutationCount = 0;
        }
        slot.framePrepared = true;
        slot.preparedMutationEpoch = slot.currentMutationEpoch;
        slot.preparedMutationCount = commandMutationCount + relinks->processCount;
        relinks->uniqueCount = 0;
        relinks->structuralMoveCount = 0;
        relinks->prepared = true;

        result.scene = scene;
        result.mutationEpoch = slot.preparedMutationEpoch;
        result.drainedMutations = slot.preparedMutationCount;
        result.completedSynchronously = relinks->processCount == 0;
        ++m_impl->stats.preparedFrames;
        return true;
    }

    bool RenderSceneManager::ExecuteSceneUpdate(const RenderSceneHandle scene, jobs::Builder& builder, RenderSceneUpdateResult& result, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for scene update", scene);
        Impl::RelinkState* const relinks = m_impl->relinkStates[scene.index];
        if (relinks == nullptr || !relinks->prepared || relinks->dispatched.GetValue())
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene update must be prepared exactly once before execution", scene);
        if (!builder.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidState, "RenderScene update requires a valid Jobs builder", scene);

        Impl* const impl = m_impl;
        Impl::SceneSlot& sceneSlot = impl->slots[scene.index];
        const u32 processIndex = relinks->processIndex;
        const u32 processCount = relinks->processCount;
        result.scene = scene;
        result.mutationEpoch = sceneSlot.preparedMutationEpoch;
        const u64 completedMutationEpoch = sceneSlot.preparedMutationEpoch;
        result.submittedRelinks = processCount;
        if (processCount == 0)
        {
            relinks->prepared = false;
            sceneSlot.completedMutationEpoch = sceneSlot.preparedMutationEpoch;
            sceneSlot.framePrepared = false;
            sceneSlot.preparedMutationCount = 0;
            result.dispatched = false;
            return true;
        }

        const u32 groupCount = jobs::GetWorkerCount() + 1u;
        const u32 batchSize = (processCount + groupCount - 1u) / groupCount;
        RenderSceneGpuDirtyBatch gpuDirtyBatch;
        if (impl->gpuPublisher != nullptr && !impl->gpuPublisher->BeginParallelDirty(scene, groupCount, processCount, gpuDirtyBatch))
        {
            impl->CancelPreparedSceneUpdate(sceneSlot, *relinks);
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene GPU dirty batch could not be opened", scene);
        }

        u32 duplicateStamp = ++relinks->dedupStamp;
        if (duplicateStamp == 0)
        {
            relinks->ClearDedupStamps();
            duplicateStamp = ++relinks->dedupStamp;
        }

        relinks->prepared = false;
        relinks->dispatched.SetValue(true);
        static jobs::JobName updateName{"RenderScene.UpdateState"};
        static jobs::JobName relinkName{"RenderScene.UpdateState.Relink"};
        jobs::Task updateTask = jobs::Task::Create(
            [impl, relinks, scene, processIndex, processCount, duplicateStamp, groupCount, batchSize, completedMutationEpoch, gpuDirtyBatch](const jobs::JobContext& context) noexcept
            {
                containers::DynamicArray<Impl::PendingRelinkRequest>& requests = relinks->Queue(processIndex);
                u32 unique = 0;
                for (u32 index = processCount; index-- > 0;)
                {
                    Impl::PendingRelinkRequest& request = requests[index];
                    const RenderProxyHandle proxyHandle = request.input.proxy;
                    if (!request.active)
                        continue;
                    Impl::RelinkState::ProxyRelinkMetadata* const metadata = relinks->ResolveProxyMetadata(proxyHandle);
                    if (proxyHandle.scene != scene || metadata == nullptr)
                    {
                        request.active = false;
                        continue;
                    }
                    if (metadata->dedupStamp == duplicateStamp)
                    {
                        relinks->ReleaseOutstanding(proxyHandle);
                        request.active = false;
                        continue;
                    }
                    metadata->dedupStamp = duplicateStamp;
                    ++unique;
                }
                relinks->uniqueCount = unique;

                const auto ApplyGroup = [impl, relinks, scene, processIndex, processCount, batchSize, gpuDirtyBatch](const u32 group) noexcept
                {
                    containers::DynamicArray<Impl::PendingRelinkRequest>& groupRequests = relinks->Queue(processIndex);
                    const u32 first = group * batchSize;
                    const u32 proposedEnd = first + batchSize;
                    const u32 end = proposedEnd < processCount ? proposedEnd : processCount;
                    for (u32 index = first; index < end; ++index)
                    {
                        Impl::PendingRelinkRequest& request = groupRequests[index];
                        if (!request.active || !impl->ValidAliveProxy(request.input.proxy))
                            continue;
                        Impl::SceneSlot& slot = impl->slots[scene.index];
                        Impl::ProxySlot& proxy = slot.proxies[request.input.proxy.index];
                        request.oldBounds = proxy.bounds;
                        proxy.transform = request.input.transform;
                        proxy.bounds = request.input.bounds;
                        if (request.input.producerGeneration != 0)
                            proxy.producerGeneration = request.input.producerGeneration;
                        if (request.input.teleport)
                            ++proxy.teleportRevision;
                        request.structuralMove = proxy.spatialMode == RenderProxySpatialMode::Bounds && spatial::QuickConditionalMove(slot.spatial, proxy.spatial, proxy.bounds);
                        ++proxy.lifecycleRevision;
                        if (impl->gpuPublisher != nullptr)
                            static_cast<void>(impl->gpuPublisher->MarkProxyDirty(gpuDirtyBatch, group, request.input.proxy, RenderSceneGpuDirtyFlags::Transform));
                    }
                };
                const auto Finish = [impl, relinks, scene, processIndex, processCount, completedMutationEpoch, gpuDirtyBatch]() noexcept
                {
                    Impl::SceneSlot& slot = impl->slots[scene.index];
                    containers::DynamicArray<Impl::PendingRelinkRequest>& finishRequests = relinks->Queue(processIndex);
                    u32 structuralMoves = 0;
                    for (u32 index = 0; index < processCount; ++index)
                    {
                        Impl::PendingRelinkRequest& request = finishRequests[index];
                        if (request.active && request.structuralMove && impl->ValidAliveProxy(request.input.proxy))
                        {
                            impl->MoveSpatialEntry(slot, request.input.proxy, slot.proxies[request.input.proxy.index], request.oldBounds);
                            ++structuralMoves;
                        }
                        if (request.active)
                            relinks->ReleaseOutstanding(request.input.proxy);
                        request = {};
                    }
                    if (structuralMoves != 0)
                        impl->RepairSpatialIndex(slot);
                    if (impl->gpuPublisher != nullptr)
                        static_cast<void>(impl->gpuPublisher->EndParallelDirty(gpuDirtyBatch));
                    relinks->structuralMoveCount = structuralMoves;
                    ++slot.currentMutationEpoch;
                    ++slot.lifecycleRevision;
                    slot.completedMutationEpoch = completedMutationEpoch;
                    slot.framePrepared = false;
                    slot.preparedMutationCount = 0;
                    relinks->processCount = 0;
                    relinks->dispatched.SetValue(false);
                };

                jobs::ParallelTask relinkTask = jobs::ParallelTask::Create([ApplyGroup](const u32 group, const jobs::JobContext&) noexcept { ApplyGroup(group); });
                jobs::Task epilogue = jobs::Task::Create([Finish](const jobs::JobContext&) noexcept { Finish(); });
                jobs::Builder childBuilder(context);
                if (relinkTask && epilogue && childBuilder.IsValid() && childBuilder.DispatchParallel(relinkName, groupCount, std::move(relinkTask), std::move(epilogue), 1u))
                    return;

                // Allocation/dispatch failure inside the owned root cannot reopen the scene. Complete the bounded work serially
                // and run the same epilogue so every retained request and GPU dirty batch has exactly one release path.
                for (u32 group = 0; group < groupCount; ++group)
                    ApplyGroup(group);
                Finish();
            });
        if (!updateTask || !builder.Dispatch(updateName, std::move(updateTask)))
        {
            if (impl->gpuPublisher != nullptr)
                impl->gpuPublisher->CancelParallelDirty(gpuDirtyBatch);
            relinks->dispatched.SetValue(false);
            impl->CancelPreparedSceneUpdate(sceneSlot, *relinks);
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene update-state job dispatch failed", scene);
        }

        result.dispatched = true;
        return true;
    }

    bool RenderSceneManager::CollectVisibleProxies(const VisibilityQueryRequest& request, containers::DynamicArray<RenderProxyHandle>& proxies, VisibilityQueryResult& result,
                                                   RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        proxies.Clear();
        result = {};
        result.scene = request.scene;
        result.mutationEpoch = request.mutationEpoch;
        if (!request.scene.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene handle for visibility query", request.scene);
        if (request.useBounds && !ValidBounds(request.bounds))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility query bounds", request.scene);
        if (request.useFrustum && !ValidFrustum(request.frustum))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility query frustum", request.scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", request.scene);

        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(request.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for visibility query", request.scene);
        const Impl::RelinkState* const relinks = m_impl->relinkStates[request.scene.index];
        const Impl::SceneSlot& scene = m_impl->slots[request.scene.index];
        if ((relinks != nullptr && relinks->dispatched.GetValue()) || scene.framePrepared || scene.completedMutationEpoch != request.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility query does not reference the completed scene-update epoch", request.scene);

        spatial::CollectLive(scene.spatial, request, m_impl, Impl::ResolveLiveVisibilityProxyThunk, proxies, result);
        return true;
    }

    bool RenderSceneManager::BuildVisibilityQueryPlan(const RenderSceneHandle scene, const u64 mutationEpoch, const u32 targetCellsPerBatch, containers::DynamicArray<VisibilityQueryBatch>& batches,
                                                      VisibilityQueryPlan& plan, RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        batches.Clear();
        plan = {};
        plan.scene = scene;
        plan.mutationEpoch = mutationEpoch;
        if (!scene.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene handle for visibility query planning", scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", scene);

        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for visibility query planning", scene);
        const Impl::RelinkState* const relinks = m_impl->relinkStates[scene.index];
        const Impl::SceneSlot& sceneSlot = m_impl->slots[scene.index];
        if ((relinks != nullptr && relinks->dispatched.GetValue()) || sceneSlot.framePrepared || sceneSlot.completedMutationEpoch != mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility query plan does not reference the completed scene-update epoch", scene);

        spatial::BuildLiveBatches(sceneSlot.spatial, scene, mutationEpoch, targetCellsPerBatch, batches, plan);
        return true;
    }

    bool RenderSceneManager::CollectVisibleProxyBatch(const VisibilityQueryRequest& request, const VisibilityQueryBatch& batch, containers::DynamicArray<RenderProxyHandle>& proxies,
                                                      VisibilityQueryResult& result, RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        proxies.Clear();
        result = {};
        result.scene = request.scene;
        result.mutationEpoch = request.mutationEpoch;
        if (!request.scene.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene handle for visibility query batch", request.scene);
        if (!batch.IsValid() || !(batch.scene == request.scene) || batch.mutationEpoch != request.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "visibility query batch does not belong to the requested scene-update epoch", request.scene);
        if (request.maximumResults != ~u32{0})
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "per-batch visibility collection requires an unbounded local result; apply limits during reduction", request.scene);
        if (request.useBounds && !ValidBounds(request.bounds))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility query batch bounds", request.scene);
        if (request.useFrustum && !ValidFrustum(request.frustum))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility query batch frustum", request.scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", request.scene);

        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(request.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for visibility query batch", request.scene);
        const Impl::RelinkState* const relinks = m_impl->relinkStates[request.scene.index];
        const Impl::SceneSlot& scene = m_impl->slots[request.scene.index];
        if ((relinks != nullptr && relinks->dispatched.GetValue()) || scene.framePrepared || scene.completedMutationEpoch != request.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility query batch does not reference the completed scene-update epoch", request.scene);
        const u32 traversalSlots = spatial::TraversalCount(scene.spatial);
        if (batch.firstCell >= traversalSlots)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "visibility query batch is outside the completed spatial state", request.scene);

        spatial::CollectLiveRange(scene.spatial, request, batch, m_impl, Impl::ResolveLiveVisibilityProxyThunk, proxies, result);
        return true;
    }

    bool RenderSceneManager::PrepareGpuVisibilityCandidates(const VisibilityQueryRequest& request, const u32 targetCandidatesPerBatch, const containers::ArraySpan<RenderSceneGpuCandidateBatch> batchStorage,
                                                            RenderSceneGpuCandidatePlan& plan, RenderSceneFailure* const failure) noexcept
    {
        if (!PrepareGpuVisibilityCandidatePlan(request, targetCandidatesPerBatch, {}, plan, failure))
            return false;
        if (!BuildGpuVisibilityCandidateBatches(plan, batchStorage, failure))
        {
            RenderSceneFailure completionFailure;
            static_cast<void>(CompleteGpuVisibilityCandidates(plan, &completionFailure));
            plan.serial = 0;
            return false;
        }
        return true;
    }

    bool RenderSceneManager::PrepareGpuVisibilityCandidatePlan(const VisibilityQueryRequest& request, const u32 targetCandidatesPerBatch,
                                                               const containers::ArraySpan<const RenderView> views, RenderSceneGpuCandidatePlan& plan,
                                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        plan = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", request.scene);
        if (!request.scene.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid RenderScene handle for GPU visibility candidate planning", request.scene);
        if (targetCandidatesPerBatch == 0 || request.payloadFilter != VisibilityQueryPayloadFilter::Mesh || request.maximumResults != ~u32{0} ||
            (request.useBounds && !ValidBounds(request.bounds)) || (request.useFrustum && !ValidFrustum(request.frustum)))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid GPU visibility candidate planning request", request.scene);

        // Validate each family query once before sealing, not in each batch or
        // proxy visit. The spatial layout itself is independent of view filters.
        for (const RenderView& view : views)
            if (!ValidFrustum(view.frustum))
                return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid GPU visibility view-family frustum", request.scene);

        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(request.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for GPU visibility candidate planning", request.scene);
        Impl::RelinkState* const relinks = m_impl->relinkStates[request.scene.index];
        Impl::SceneSlot& scene = m_impl->slots[request.scene.index];
        if (relinks == nullptr || relinks->dispatched.GetValue() || scene.framePrepared || scene.completedMutationEpoch != request.mutationEpoch || scene.pendingMutationCount != 0)
            return Fail(failure, RenderSceneFailureCode::Busy, "GPU visibility candidates require the exact sealed scene-update epoch", request.scene);
        if (relinks->candidateProduction.CompareExchange(true, false))
            return Fail(failure, RenderSceneFailureCode::Busy, "GPU visibility candidate production is already active", request.scene);

        u64 serial = ++relinks->candidateSerial;
        if (serial == 0)
            serial = ++relinks->candidateSerial;
        if (!spatial::PrepareGpuCandidatePlan(scene.spatial, request.scene, request.mutationEpoch, serial, targetCandidatesPerBatch, plan))
        {
            relinks->candidateProduction.SetValue(false);
            plan.serial = 0;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "GPU visibility candidate capacity calculation overflowed", request.scene);
        }
        plan.request = request;
        return true;
    }

    bool RenderSceneManager::BuildGpuVisibilityCandidateBatches(const RenderSceneGpuCandidatePlan& plan,
                                                                const containers::ArraySpan<RenderSceneGpuCandidateBatch> batchStorage,
                                                                RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", plan.scene);
        if (!plan.IsValid() || plan.batchCount > batchStorage.Size())
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "GPU visibility candidate batch storage capacity was exceeded", plan.scene);

        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(plan.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for GPU visibility candidate batching", plan.scene);
        const Impl::RelinkState* const relinks = m_impl->relinkStates[plan.scene.index];
        const Impl::SceneSlot& scene = m_impl->slots[plan.scene.index];
        if (relinks == nullptr || !relinks->candidateProduction.GetValue() || relinks->candidateSerial != plan.serial ||
            scene.completedMutationEpoch != plan.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "GPU visibility candidate plan is not the active sealed scene epoch", plan.scene);
        if (!spatial::BuildGpuCandidateBatches(scene.spatial, plan, batchStorage))
            return Fail(failure, RenderSceneFailureCode::InvalidState, "GPU visibility candidate batches do not match their sealed plan", plan.scene);
        return true;
    }

    bool RenderSceneManager::WriteGpuVisibilityCandidateBatch(const RenderSceneGpuCandidatePlan& plan, const RenderSceneGpuCandidateBatch& batch, const GpuVisibilityCandidateReservation& reservation,
                                                              GpuVisibilityCandidateRange& range, RenderSceneGpuCandidateBatchResult& result, RenderSceneFailure* const failure) const noexcept
    {
        return WriteGpuVisibilityCandidateBatch(plan, plan.request, batch, reservation, range, result, failure);
    }

    bool RenderSceneManager::WriteGpuVisibilityCandidateBatch(const RenderSceneGpuCandidatePlan& plan, const VisibilityQueryRequest& request,
                                                              const RenderSceneGpuCandidateBatch& batch, const GpuVisibilityCandidateReservation& reservation,
                                                              GpuVisibilityCandidateRange& range, RenderSceneGpuCandidateBatchResult& result,
                                                              RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        result = {};
        range = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", request.scene);
        if (!plan.IsValid() || !batch.IsValid() || !reservation.IsValid() || request.scene != plan.scene || batch.scene != plan.scene || request.mutationEpoch != plan.mutationEpoch ||
            batch.mutationEpoch != plan.mutationEpoch || batch.planSerial != plan.serial || plan.requiredCandidateCapacity > reservation.capacity ||
            plan.requiredWorkRangeCapacity > reservation.workRangeCapacity || batch.destinationOffset > plan.requiredCandidateCapacity ||
            batch.traversalCandidateCount > plan.requiredCandidateCapacity - batch.destinationOffset || batch.destinationOffset > reservation.capacity ||
            batch.traversalCandidateCount > reservation.capacity - batch.destinationOffset)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid GPU visibility candidate batch or reservation", request.scene);
        if (!m_impl->ValidAliveScene(plan.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale RenderScene handle for GPU visibility candidate production", plan.scene);

        const Impl::RelinkState* const relinks = m_impl->relinkStates[plan.scene.index];
        const Impl::SceneSlot& scene = m_impl->slots[plan.scene.index];
        if (relinks == nullptr || !relinks->candidateProduction.GetValue() || relinks->candidateSerial != plan.serial || scene.completedMutationEpoch != plan.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "GPU visibility candidate plan is not the active sealed scene epoch", plan.scene);

        spatial::WriteGpuCandidateRange(scene.spatial, request, batch, m_impl, Impl::ResolveLiveVisibilityProxyThunk, reservation.destination + batch.destinationOffset, range, result);
        if (!result.completed)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        result.unresolvedGpuIdentities != 0 ? "GPU visibility candidate encountered a mesh proxy without a stable GPU identity"
                                                            : "GPU visibility candidate batch exceeded the sealed spatial traversal",
                        plan.scene);
        return true;
    }

    bool RenderSceneManager::CompleteGpuVisibilityCandidates(const RenderSceneGpuCandidatePlan& plan, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "RenderSceneManager is not initialized", plan.scene);
        if (!plan.IsValid() || !m_impl->ValidAliveScene(plan.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid GPU visibility candidate plan", plan.scene);
        Impl::RelinkState* const relinks = m_impl->relinkStates[plan.scene.index];
        if (relinks == nullptr || relinks->candidateSerial != plan.serial || !relinks->candidateProduction.Exchange(false))
            return Fail(failure, RenderSceneFailureCode::InvalidState, "GPU visibility candidate plan is stale or already completed", plan.scene);
        return true;
    }

    bool RenderSceneManager::CollectDirectionalLights(const RenderSceneGpuCandidatePlan& plan, const containers::ArraySpan<const RenderView> views,
                                                       containers::ArraySpan<GpuDirectionalLightSelection> selections, RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !plan.IsValid() || views.Empty() || views.Size() > MaximumRenderViewsPerFamily || views.Size() != selections.Size())
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "directional selection requires a sealed family and matching storage", plan.scene);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(plan.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "directional selection scene is unavailable", plan.scene);
        const auto* relinks = m_impl->relinkStates[plan.scene.index];
        const auto& scene = m_impl->slots[plan.scene.index];
        if (relinks == nullptr || !relinks->candidateProduction.GetValue() || relinks->candidateSerial != plan.serial || scene.completedMutationEpoch != plan.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "directional selection requires the active scene epoch seal", plan.scene);
        for (auto& selection : selections)
            selection = {};
        for (const RenderProxyHandle proxy : scene.spatial.globalProxies)
        {
            if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Light))
                continue;
            const auto& base = scene.proxies[proxy.index];
            const auto& light = scene.lightPayloads[base.payloadIndex];
            if (light.kind != RenderLightKind::Directional || (base.visibility & RenderProxyVisibilityFlags::Visible) != RenderProxyVisibilityFlags::Visible ||
                (base.visibility & RenderProxyVisibilityFlags::QueryOnly) != RenderProxyVisibilityFlags::None || light.intensity <= 0.0f ||
                (light.color[0] == 0.0f && light.color[1] == 0.0f && light.color[2] == 0.0f))
                continue;
            for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
            {
                if ((base.layerMask & views[viewIndex].layerMask) == 0 || (base.visibilityMask & views[viewIndex].visibilityMask) == 0)
                    continue;
                if (!light.gpuIdentity.IsValid())
                    return Fail(failure, RenderSceneFailureCode::InvalidState, "selected directional light has no GPU identity", plan.scene);
                auto& selection = selections[viewIndex];
                if (selection.count == MaximumDirectionalLightsPerView)
                    return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "directional lights exceed the per-view lighting budget", plan.scene);
                selection.lights[selection.count++] = light.gpuIdentity;
            }
        }
        return true;
    }

    bool RenderSceneManager::IsAlive(const RenderSceneHandle scene) const noexcept
    {
        return m_impl != nullptr && m_impl->ValidHandle(scene) && m_impl->slots[scene.index].state == RenderSceneState::Alive;
    }

    bool RenderSceneManager::IsProxyAlive(const RenderProxyHandle proxy) const noexcept
    {
        return m_impl != nullptr && m_impl->ValidAliveProxy(proxy);
    }

    bool RenderSceneManager::FindProducerProxy(const RenderSceneHandle sceneHandle, const RenderProducerHandle producer, const RenderContributorId contributor, RenderProxyHandle& proxy) const noexcept
    {
        proxy = {};
        if (m_impl == nullptr || !m_impl->ValidAliveScene(sceneHandle))
            return false;
        const Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        const u32 index = Impl::FindProducerProxyIndex(scene, producer, contributor);
        if (index == InvalidSlotIndex)
            return false;
        proxy = {sceneHandle, index, scene.proxies[index].generation};
        return true;
    }

    u32 RenderSceneManager::GetProducerProxyCount(const RenderSceneHandle sceneHandle, const RenderProducerHandle producer) const noexcept
    {
        if (m_impl == nullptr || !m_impl->ValidAliveScene(sceneHandle))
            return 0;
        const Impl::ProducerSlot* const owner = Impl::ResolveProducer(m_impl->slots[sceneHandle.index], producer);
        return owner != nullptr ? owner->proxyCount : 0;
    }

    bool RenderSceneManager::GetProducerProxies(const RenderSceneHandle sceneHandle, const RenderProducerHandle producer, containers::ArraySpan<RenderProducerProxy> proxies, u32& count) const noexcept
    {
        count = 0;
        if (m_impl == nullptr || !m_impl->ValidAliveScene(sceneHandle))
            return false;
        const Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        const Impl::ProducerSlot* const owner = Impl::ResolveProducer(scene, producer);
        if (owner == nullptr)
            return true;
        count = owner->proxyCount;
        if (proxies.Size() < owner->proxyCount)
            return false;
        u32 written = 0;
        u32 proxyIndex = owner->firstProxy;
        while (proxyIndex != InvalidSlotIndex && written < owner->proxyCount)
        {
            const Impl::ProxySlot& slot = scene.proxies[proxyIndex];
            proxies[written] = RenderProducerProxy{producer, slot.contributor, {sceneHandle, proxyIndex, slot.generation}};
            ++written;
            proxyIndex = slot.nextProducerProxy;
        }
        count = written;
        return written == owner->proxyCount;
    }

    containers::ArraySpan<const RenderSceneHandle> RenderSceneManager::GetFramePipelineScenes() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const RenderSceneHandle>(m_impl->framePipelineScenes, m_impl->framePipelineSceneCount) : containers::ArraySpan<const RenderSceneHandle>{};
    }

    bool RenderSceneManager::GetSnapshot(const RenderSceneHandle scene, RenderSceneSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidHandle(scene))
            return false;
        const Impl::SceneSlot& slot = m_impl->slots[scene.index];
        snapshot.handle = scene;
        snapshot.mode = slot.mode;
        snapshot.state = slot.state;
        snapshot.ownership = slot.ownership;
        snapshot.spatial = slot.spatial.stats;
        snapshot.maximumProxies = slot.maximumProxies;
        snapshot.maximumPendingProxyMutations = slot.maximumPendingProxyMutations;
        snapshot.maximumViews = slot.maximumViews;
        snapshot.activeProxies = slot.activeProxies;
        snapshot.pendingProxyMutations = slot.pendingMutationCount;
        snapshot.pendingMeshBindings = slot.pendingMeshBindingCount;
        snapshot.createdSerial = slot.createdSerial;
        snapshot.lifecycleRevision = slot.lifecycleRevision;
        snapshot.currentMutationEpoch = slot.currentMutationEpoch;
        snapshot.preparedMutationEpoch = slot.preparedMutationEpoch;
        snapshot.completedMutationEpoch = slot.completedMutationEpoch;
        snapshot.framePrepared = slot.framePrepared;
        snapshot.allowFramePipelineParticipation = slot.allowFramePipelineParticipation;
        CopyNameUnchecked(snapshot.name, MaximumRenderSceneNameBytes, slot.name);
        return true;
    }

    const char* RenderSceneManager::GetRenderingBlockReason(const RenderSceneHandle scene) const noexcept
    {
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene))
            return "RenderScene is unavailable";
        return m_impl->cameraStorage != nullptr ? m_impl->cameraStorage->GetRenderingBlockReason(scene) : "RenderScene camera storage is unavailable";
    }

    void RenderSceneManager::TickWhileLoading(const RenderSceneHandle scene, const bool isFirstFrame) noexcept
    {
        if (m_impl != nullptr && m_impl->ValidAliveScene(scene) && m_impl->cameraStorage != nullptr)
            m_impl->cameraStorage->TickWhileLoading(scene, isFirstFrame);
    }

    bool RenderSceneManager::AttachCameraStorage(RenderCameraStorage& storage) noexcept
    {
        if (m_impl == nullptr || m_impl->cameraStorage != nullptr || m_impl->stats.activeScenes != 0)
            return false;
        m_impl->cameraStorage = &storage;
        return true;
    }

    bool RenderSceneManager::DetachCameraStorage(RenderCameraStorage& storage) noexcept
    {
        if (m_impl == nullptr || m_impl->cameraStorage != &storage || m_impl->stats.activeScenes != 0)
            return false;
        m_impl->cameraStorage = nullptr;
        return true;
    }

    bool RenderSceneManager::AttachGpuPublisher(RenderSceneGpuPublisher& publisher) noexcept
    {
        if (m_impl == nullptr || m_impl->gpuPublisher != nullptr || m_impl->stats.activeScenes != 0)
            return false;
        m_impl->gpuPublisher = &publisher;
        return true;
    }

    bool RenderSceneManager::DetachGpuPublisher(RenderSceneGpuPublisher& publisher) noexcept
    {
        if (m_impl == nullptr || m_impl->gpuPublisher != &publisher || m_impl->stats.activeScenes != 0)
            return false;
        m_impl->gpuPublisher = nullptr;
        return true;
    }

    bool RenderSceneManager::IsGpuPublicationReady(const RenderSceneHandle sceneHandle, const u64 mutationEpoch) const noexcept
    {
        if (m_impl == nullptr || !m_impl->ValidAliveScene(sceneHandle))
            return false;
        const Impl::SceneSlot& scene = m_impl->slots[sceneHandle.index];
        return !scene.framePrepared && scene.completedMutationEpoch == mutationEpoch;
    }

    bool RenderSceneManager::ReadGpuProxy(const RenderProxyHandle proxy, RenderSceneGpuReadView& view) const noexcept
    {
        view = {};
        if (m_impl == nullptr || !m_impl->ValidAliveProxy(proxy))
            return false;

        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& base = scene.proxies[proxy.index];
        view.transform = &base.transform;
        view.bounds = &base.bounds;
        view.visibility = &base.visibility;
        view.layerMask = &base.layerMask;
        view.visibilityMask = &base.visibilityMask;
        view.payloadKind = base.payloadKind;

        if (base.payloadKind == RenderProxyPayloadKind::Mesh)
        {
            if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh))
                return false;
        }
        else if (base.payloadKind == RenderProxyPayloadKind::Light)
        {
            if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Light))
                return false;
            const Impl::LightPayloadSlot& light = scene.lightPayloads[base.payloadIndex];
            view.lightKind = &light.kind;
            view.lightColor = light.color;
            view.lightIntensity = &light.intensity;
            view.lightRange = &light.range;
            view.lightInnerConeRadians = &light.innerConeRadians;
            view.lightOuterConeRadians = &light.outerConeRadians;
            view.lightCastsShadow = &light.castsShadow;
        }
        else if (base.payloadKind == RenderProxyPayloadKind::Decal)
        {
            if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Decal))
                return false;
            const Impl::DecalPayloadSlot& decal = scene.decalPayloads[base.payloadIndex];
            view.decalExtents = decal.extents;
            view.decalFadeDistance = &decal.fadeDistance;
            view.decalSortKey = &decal.sortKey;
        }
        return true;
    }

    bool RenderSceneManager::SetGpuInstanceIndex(const RenderProxyHandle proxy, const GpuInstanceIndex instanceIndex) noexcept
    {
        if (m_impl == nullptr || instanceIndex == InvalidGpuSceneIndex || !m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh))
            return false;
        Impl::ProxySlot& slot = m_impl->slots[proxy.scene.index].proxies[proxy.index];
        if (slot.gpuInstanceIndex != InvalidGpuSceneIndex)
            return false;
        slot.gpuInstanceIndex = instanceIndex;
        return true;
    }

    void RenderSceneManager::ClearGpuInstanceIndex(const RenderProxyHandle proxy) noexcept
    {
        if (m_impl != nullptr && m_impl->ValidAliveProxy(proxy))
            m_impl->slots[proxy.scene.index].proxies[proxy.index].gpuInstanceIndex = InvalidGpuSceneIndex;
    }

    bool RenderSceneManager::AttachVisibilityFeedback(const RenderSceneHandle scene) noexcept
    {
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene))
            return false;
        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (slot.visibilityFeedbackAttached)
            return false;
        slot.visibilityFeedbackAttached = true;
        return true;
    }

    bool RenderSceneManager::DetachVisibilityFeedback(const RenderSceneHandle scene) noexcept
    {
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene))
            return false;
        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (!slot.visibilityFeedbackAttached)
            return false;
        slot.visibilityFeedbackAttached = false;
        return true;
    }

    bool RenderSceneManager::ValidateSpatialIndex(const RenderSceneHandle scene, SpatialWriteIndexStats* const stats) const noexcept
    {
        if (stats != nullptr)
            *stats = {};
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene))
            return false;
        return m_impl->ValidateSpatialIndexInternal(m_impl->slots[scene.index], stats);
    }

    RenderSceneManagerStats RenderSceneManager::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : RenderSceneManagerStats{};
    }
} // namespace vanguard::rendering
