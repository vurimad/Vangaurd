#include <vanguard/rendering/render_scene.hpp>

#include <vanguard/rendering/render_scene_spatial.hpp>

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
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneFailure* const failure, const RenderSceneFailureCode code,
                                const char* const message, const RenderSceneHandle scene = {},
                                const RenderProxyHandle proxy = {}) noexcept
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
            return static_cast<u32>(mode) <= static_cast<u32>(RenderProxySpatialMode::Bounds);
        }

        [[nodiscard]] bool CopyName(char* const destination, const u32 capacity,
                                    const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0') return false;
            u32 index = 0;
            while (index + 1u < capacity && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0') return false;
            destination[index] = '\0';
            return true;
        }

        void CopyNameUnchecked(char* const destination, const u32 capacity,
                               const char* const source) noexcept
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
                if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                    bounds.minimum[axis] > bounds.maximum[axis])
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidTransform(const RenderProxyTransform& transform) noexcept
        {
            for (u32 index = 0; index < 4; ++index)
            {
                if (!std::isfinite(transform.row0[index]) || !std::isfinite(transform.row1[index]) ||
                    !std::isfinite(transform.row2[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidSpatialConfig(const SpatialWriteIndexConfig& config) noexcept
        {
            if (!std::isfinite(config.cellSize) || config.cellSize <= 0.0f) return false;
            const bool anyExtent = config.cellsPerAxis[0] != 0 || config.cellsPerAxis[1] != 0 ||
                                   config.cellsPerAxis[2] != 0;
            if (anyExtent && !config.HasFiniteExtent()) return false;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(config.origin[axis])) return false;
                if (config.HasFiniteExtent())
                {
                    const f64 extent = static_cast<f64>(config.cellsPerAxis[axis]) * config.cellSize;
                    if (!std::isfinite(extent) ||
                        static_cast<f64>(config.origin[axis]) + extent > std::numeric_limits<f32>::max())
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ValidFrustum(const VisibilityFrustum& frustum) noexcept
        {
            if (frustum.planeCount == 0 || frustum.planeCount > MaximumVisibilityFrustumPlanes) return false;
            for (u32 planeIndex = 0; planeIndex < frustum.planeCount; ++planeIndex)
            {
                const VisibilityPlane& plane = frustum.planes[planeIndex];
                if (!std::isfinite(plane.normal[0]) || !std::isfinite(plane.normal[1]) ||
                    !std::isfinite(plane.normal[2]) || !std::isfinite(plane.distance))
                    return false;
                const f32 magnitudeSquared = plane.normal[0] * plane.normal[0] +
                                             plane.normal[1] * plane.normal[1] +
                                             plane.normal[2] * plane.normal[2];
                if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= 0.0f) return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidLightProperties(const RenderLightKind kind, const f32* const color,
                                                const f32 intensity, const f32 range,
                                                const f32 innerCone, const f32 outerCone) noexcept
        {
            if (static_cast<u32>(kind) > static_cast<u32>(RenderLightKind::Spot) ||
                !std::isfinite(intensity) || !std::isfinite(range) || intensity < 0.0f || range < 0.0f ||
                !std::isfinite(innerCone) || !std::isfinite(outerCone) || innerCone < 0.0f ||
                outerCone < innerCone)
                return false;
            return std::isfinite(color[0]) && std::isfinite(color[1]) && std::isfinite(color[2]);
        }

        inline constexpr u32 MeshProxyTypeId = 1;
        inline constexpr u32 LightProxyTypeId = 2;
        inline constexpr u32 DecalProxyTypeId = 3;
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
            u32 payloadIndex = ~u32{0};
            u32 payloadGeneration = 0;
            u64 producerId = 0;
            u64 producerGeneration = 0;
            RenderProxyTransform transform;
            RenderProxyBounds bounds;
            RenderProxySpatialMode spatialMode = RenderProxySpatialMode::None;
            spatial::EntryHandle spatial;
            RenderProxyVisibilityFlags visibility = RenderProxyVisibilityFlags::None;
            u64 layerMask = 0;
            u32 visibilityMask = 0;
            u64 userDataEpoch = 0;
            u64 createdSerial = 0;
            u64 lifecycleRevision = 0;
            char debugName[MaximumRenderProxyNameBytes]{};
        };

        struct ProxyMutationPacket
        {
            RenderProxyMutationKind kind = RenderProxyMutationKind::Create;
            RenderProxyHandle proxy;
            u64 serial = 0;
            u64 lifecycleRevision = 0;
        };

        struct PublishedSceneVersion
        {
            PublishedSceneVersion() noexcept
                : proxies(memory::pools::Rendering::GetInstance()),
                  proxyLookup(memory::pools::Rendering::GetInstance()),
                  spatialCells(memory::pools::Rendering::GetInstance()),
                  spatialCellProxies(memory::pools::Rendering::GetInstance()),
                  meshPayloads(memory::pools::Rendering::GetInstance()),
                  lightPayloads(memory::pools::Rendering::GetInstance()),
                  decalPayloads(memory::pools::Rendering::GetInstance()),
                  meshPayloadLookup(memory::pools::Rendering::GetInstance()),
                  lightPayloadLookup(memory::pools::Rendering::GetInstance()),
                  decalPayloadLookup(memory::pools::Rendering::GetInstance()),
                  readerEpochs(memory::pools::Rendering::GetInstance())
            {
            }

            RenderSceneVersion version;
            RenderSceneHandle scene;
            u64 mutationEpoch = 0;
            u64 lifecycleRevision = 0;
            u32 readerCount = 0;
            bool retired = false;
            containers::DynamicArray<RenderProxySnapshot> proxies;
            containers::DynamicArray<u32> proxyLookup;
            containers::DynamicArray<spatial::CellSnapshot> spatialCells;
            containers::DynamicArray<RenderProxyHandle> spatialCellProxies;
            containers::DynamicArray<MeshProxySnapshot> meshPayloads;
            containers::DynamicArray<LightProxySnapshot> lightPayloads;
            containers::DynamicArray<DecalProxySnapshot> decalPayloads;
            containers::DynamicArray<u32> meshPayloadLookup;
            containers::DynamicArray<u32> lightPayloadLookup;
            containers::DynamicArray<u32> decalPayloadLookup;
            containers::DynamicArray<u64> readerEpochs;
        };

        struct MeshPayloadSlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            RenderProxyHandle proxy;
            u32 generation = 0;
            resources::ResourceReference mesh;
            resources::ResourceReference material;
            resources::ResourceHandle meshHandle;
            resources::ResourceHandle materialHandle;
            u32 submeshMask = 0;
            u32 renderFlags = 0;
        };

        struct LightPayloadSlot
        {
            RenderProxyState state = RenderProxyState::Vacant;
            RenderProxyHandle proxy;
            u32 generation = 0;
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
            resources::ResourceReference material;
            resources::ResourceHandle materialHandle;
            f32 extents[3]{};
            f32 fadeDistance = 0.0f;
            u32 sortKey = 0;
        };

        struct SceneSlot
        {
            SceneSlot() noexcept
                : proxies(memory::pools::Rendering::GetInstance()),
                  mutations(memory::pools::Rendering::GetInstance()),
                  publishedVersions(memory::pools::Rendering::GetInstance()),
                  meshPayloads(memory::pools::Rendering::GetInstance()),
                  lightPayloads(memory::pools::Rendering::GetInstance()),
                  decalPayloads(memory::pools::Rendering::GetInstance())
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
            u64 createdSerial = 0;
            u64 lifecycleRevision = 0;
            u64 currentMutationEpoch = 0;
            u64 preparedMutationEpoch = 0;
            u32 preparedMutationCount = 0;
            bool framePrepared = false;
            bool allowFramePipelineParticipation = false;
            char name[MaximumRenderSceneNameBytes]{};
            containers::DynamicArray<ProxySlot> proxies;
            containers::DynamicArray<ProxyMutationPacket> mutations;
            containers::DynamicArray<PublishedSceneVersion> publishedVersions;
            containers::DynamicArray<MeshPayloadSlot> meshPayloads;
            containers::DynamicArray<LightPayloadSlot> lightPayloads;
            containers::DynamicArray<DecalPayloadSlot> decalPayloads;
            spatial::WriteIndex spatial;
        };

        explicit Impl(const RenderSceneManagerConfig& value) noexcept
            : slots(memory::pools::Rendering::GetInstance())
        {
            slots.Reserve(value.maximumScenes);
            for (u32 index = 0; index < value.maximumScenes; ++index) slots.PushBack({});
            stats.capacity = value.maximumScenes;
            stats.initialized = true;
        }

        [[nodiscard]] bool ValidHandle(const RenderSceneHandle scene) const noexcept
        {
            return scene.index < slots.Size() && scene.generation != 0 &&
                   slots[scene.index].generation == scene.generation &&
                   slots[scene.index].state != RenderSceneState::Vacant;
        }

        [[nodiscard]] bool ValidAliveScene(const RenderSceneHandle scene) const noexcept
        {
            return ValidHandle(scene) && slots[scene.index].state == RenderSceneState::Alive;
        }

        [[nodiscard]] bool ValidProxy(const RenderProxyHandle proxy) const noexcept
        {
            if (!ValidAliveScene(proxy.scene)) return false;
            const SceneSlot& scene = slots[proxy.scene.index];
            return proxy.index < scene.proxies.Size() && proxy.generation != 0 &&
                   scene.proxies[proxy.index].generation == proxy.generation &&
                   scene.proxies[proxy.index].state != RenderProxyState::Vacant;
        }

        [[nodiscard]] bool ValidAliveProxy(const RenderProxyHandle proxy) const noexcept
        {
            return ValidProxy(proxy) &&
                   slots[proxy.scene.index].proxies[proxy.index].state == RenderProxyState::Alive;
        }

        [[nodiscard]] bool ValidAlivePayload(const RenderProxyHandle proxy,
                                             const RenderProxyPayloadKind kind) const noexcept
        {
            if (!ValidAliveProxy(proxy)) return false;
            const ProxySlot& slot = slots[proxy.scene.index].proxies[proxy.index];
            if (slot.payloadKind != kind || slot.payloadGeneration != proxy.generation) return false;
            switch (kind)
            {
            case RenderProxyPayloadKind::Mesh:
                return slot.payloadIndex < slots[proxy.scene.index].meshPayloads.Size() &&
                       slots[proxy.scene.index].meshPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].meshPayloads[slot.payloadIndex].generation == proxy.generation &&
                       slots[proxy.scene.index].meshPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::Light:
                return slot.payloadIndex < slots[proxy.scene.index].lightPayloads.Size() &&
                       slots[proxy.scene.index].lightPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].lightPayloads[slot.payloadIndex].generation == proxy.generation &&
                       slots[proxy.scene.index].lightPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::Decal:
                return slot.payloadIndex < slots[proxy.scene.index].decalPayloads.Size() &&
                       slots[proxy.scene.index].decalPayloads[slot.payloadIndex].state == RenderProxyState::Alive &&
                       slots[proxy.scene.index].decalPayloads[slot.payloadIndex].generation == proxy.generation &&
                       slots[proxy.scene.index].decalPayloads[slot.payloadIndex].proxy == proxy;
            case RenderProxyPayloadKind::None:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool AppendMutation(SceneSlot& scene, const RenderProxyHandle proxy,
                                          const RenderProxyMutationKind kind) noexcept
        {
            if (scene.framePrepared) return false;
            if (scene.mutations.Size() >= scene.maximumPendingProxyMutations &&
                kind != RenderProxyMutationKind::Destroy)
                return false;
            ++scene.currentMutationEpoch;
            scene.mutations.PushBack({kind, proxy, nextMutationSerial++, scene.lifecycleRevision});
            ++stats.pendingProxyMutations;
            return true;
        }

        void RecomputeRetainedSceneVersions() noexcept
        {
            u32 retained = 0;
            for (u32 sceneIndex = 0; sceneIndex < slots.Size(); ++sceneIndex)
                retained += slots[sceneIndex].publishedVersions.Size();
            stats.retainedSceneVersions = retained;
        }

        [[nodiscard]] PublishedSceneVersion* FindPublishedVersion(SceneSlot& scene,
                                                                  const RenderSceneVersion version) noexcept
        {
            for (u32 index = 0; index < scene.publishedVersions.Size(); ++index)
                if (scene.publishedVersions[index].version == version) return &scene.publishedVersions[index];
            return nullptr;
        }

        [[nodiscard]] const PublishedSceneVersion* FindPublishedVersion(const SceneSlot& scene,
                                                                         const RenderSceneVersion version) const noexcept
        {
            for (u32 index = 0; index < scene.publishedVersions.Size(); ++index)
                if (scene.publishedVersions[index].version == version) return &scene.publishedVersions[index];
            return nullptr;
        }

        [[nodiscard]] static u32 FindReaderEpoch(const PublishedSceneVersion& version,
                                                 const u64 readerEpoch) noexcept
        {
            for (u32 index = 0; index < version.readerEpochs.Size(); ++index)
                if (version.readerEpochs[index] == readerEpoch) return index;
            return ~u32{0};
        }

        [[nodiscard]] const PublishedSceneVersion* ResolveLease(const SceneReadLease& lease) const noexcept
        {
            if (!lease.IsValid() || !ValidHandle(lease.scene)) return nullptr;
            const PublishedSceneVersion* const version = FindPublishedVersion(slots[lease.scene.index], lease.version);
            if (version == nullptr || FindReaderEpoch(*version, lease.readerEpoch) == ~u32{0}) return nullptr;
            return version;
        }

        [[nodiscard]] u64 AllocateReaderEpoch() noexcept
        {
            u64 epoch = nextReaderEpoch++;
            if (epoch == 0) epoch = nextReaderEpoch++;
            return epoch;
        }

        [[nodiscard]] RenderProxySnapshot* ResolvePublishedProxy(PublishedSceneVersion& version,
                                                                 const RenderProxyHandle proxy) noexcept
        {
            if (proxy.index >= version.proxyLookup.Size()) return nullptr;
            const u32 publishedIndex = version.proxyLookup[proxy.index];
            if (publishedIndex >= version.proxies.Size()) return nullptr;
            RenderProxySnapshot& snapshot = version.proxies[publishedIndex];
            return snapshot.handle == proxy ? &snapshot : nullptr;
        }

        [[nodiscard]] const RenderProxySnapshot* ResolvePublishedProxy(const PublishedSceneVersion& version,
                                                                       const RenderProxyHandle proxy) const noexcept
        {
            if (proxy.index >= version.proxyLookup.Size()) return nullptr;
            const u32 publishedIndex = version.proxyLookup[proxy.index];
            if (publishedIndex >= version.proxies.Size()) return nullptr;
            const RenderProxySnapshot& snapshot = version.proxies[publishedIndex];
            return snapshot.handle == proxy ? &snapshot : nullptr;
        }

        void BuildProxySnapshot(const RenderSceneHandle scene, const u32 proxyIndex,
                                const ProxySlot& slot, RenderProxySnapshot& snapshot) const noexcept
        {
            snapshot = {};
            snapshot.handle = {scene, proxyIndex, slot.generation};
            snapshot.state = slot.state;
            snapshot.typeId = slot.typeId;
            snapshot.producerId = slot.producerId;
            snapshot.producerGeneration = slot.producerGeneration;
            snapshot.transform = slot.transform;
            snapshot.bounds = slot.bounds;
            snapshot.spatialMode = slot.spatialMode;
            snapshot.payloadKind = slot.payloadKind;
            snapshot.visibility = slot.visibility;
            snapshot.layerMask = slot.layerMask;
            snapshot.visibilityMask = slot.visibilityMask;
            snapshot.userDataEpoch = slot.userDataEpoch;
            snapshot.createdSerial = slot.createdSerial;
            snapshot.lifecycleRevision = slot.lifecycleRevision;
            CopyNameUnchecked(snapshot.debugName, MaximumRenderProxyNameBytes, slot.debugName);
        }

        void BuildMeshSnapshot(const MeshPayloadSlot& slot, MeshProxySnapshot& snapshot) const noexcept
        {
            snapshot.proxy = slot.proxy;
            snapshot.payloadGeneration = slot.generation;
            snapshot.mesh = slot.mesh;
            snapshot.material = slot.material;
            snapshot.meshHandle = slot.meshHandle;
            snapshot.materialHandle = slot.materialHandle;
            snapshot.submeshMask = slot.submeshMask;
            snapshot.renderFlags = slot.renderFlags;
        }

        void BuildLightSnapshot(const LightPayloadSlot& slot, LightProxySnapshot& snapshot) const noexcept
        {
            snapshot.proxy = slot.proxy;
            snapshot.payloadGeneration = slot.generation;
            snapshot.kind = slot.kind;
            snapshot.color[0] = slot.color[0];
            snapshot.color[1] = slot.color[1];
            snapshot.color[2] = slot.color[2];
            snapshot.intensity = slot.intensity;
            snapshot.range = slot.range;
            snapshot.innerConeRadians = slot.innerConeRadians;
            snapshot.outerConeRadians = slot.outerConeRadians;
            snapshot.castsShadow = slot.castsShadow;
        }

        void BuildDecalSnapshot(const DecalPayloadSlot& slot, DecalProxySnapshot& snapshot) const noexcept
        {
            snapshot.proxy = slot.proxy;
            snapshot.payloadGeneration = slot.generation;
            snapshot.material = slot.material;
            snapshot.materialHandle = slot.materialHandle;
            snapshot.extents[0] = slot.extents[0];
            snapshot.extents[1] = slot.extents[1];
            snapshot.extents[2] = slot.extents[2];
            snapshot.fadeDistance = slot.fadeDistance;
            snapshot.sortKey = slot.sortKey;
        }

        void ApplySpatialDelta(const spatial::CounterDelta& delta) noexcept
        {
            if (delta.activeEntries >= 0) stats.activeSpatialEntries += static_cast<u32>(delta.activeEntries);
            else
            {
                const u32 removed = static_cast<u32>(-delta.activeEntries);
                stats.activeSpatialEntries = removed <= stats.activeSpatialEntries ?
                                                 stats.activeSpatialEntries - removed :
                                                 0;
            }
            if (delta.dirtyCells >= 0) stats.dirtySpatialCells += static_cast<u32>(delta.dirtyCells);
            else
            {
                const u32 repaired = static_cast<u32>(-delta.dirtyCells);
                stats.dirtySpatialCells = repaired <= stats.dirtySpatialCells ?
                                              stats.dirtySpatialCells - repaired :
                                              0;
            }
            stats.spatialFastMoves += delta.fastMoves;
            stats.spatialStructuralMoves += delta.structuralMoves;
            stats.spatialRepairedCells += delta.repairedCells;
            stats.spatialOutOfRangeProxies += delta.outOfRangeProxies;
        }

        void ApplySpatialRemoveResult(const spatial::RemoveResult& result) noexcept
        {
            if (!result.movedProxyRelocated || !ValidAliveProxy(result.movedProxy)) return;
            slots[result.movedProxy.scene.index].proxies[result.movedProxy.index].spatial.objectIndex =
                result.movedObjectIndex;
        }

        void ConfigureSpatialIndex(SceneSlot& scene, const SpatialWriteIndexConfig& config) noexcept
        {
            spatial::CounterDelta delta;
            spatial::Reset(scene.spatial, config, &delta);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool SpatialBoundsAccepted(const SceneSlot& scene,
                                                 const RenderProxyBounds& bounds) const noexcept
        {
            return spatial::BoundsAccepted(scene.spatial, bounds);
        }

        [[nodiscard]] bool InsertSpatialEntry(SceneSlot& scene, const RenderProxyHandle proxy,
                                              ProxySlot& slot) noexcept
        {
            spatial::CounterDelta delta;
            const spatial::InsertResult result =
                spatial::Insert(scene.spatial, proxy, slot.bounds, slot.spatialMode, slot.spatial, &delta);
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

        void MoveSpatialEntry(SceneSlot& scene, const RenderProxyHandle proxy, ProxySlot& slot,
                              const RenderProxyBounds& oldBounds) noexcept
        {
            spatial::CounterDelta delta;
            spatial::RemoveResult result;
            spatial::Move(scene.spatial, proxy, oldBounds, slot.bounds, slot.spatialMode, slot.spatial, &result, &delta);
            ApplySpatialRemoveResult(result);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool ResolveSpatialProxyBounds(const RenderProxyHandle proxy,
                                                     RenderProxyBounds& bounds) const noexcept
        {
            if (!ValidAliveProxy(proxy)) return false;
            bounds = slots[proxy.scene.index].proxies[proxy.index].bounds;
            return true;
        }

        [[nodiscard]] bool ValidateSpatialProxyEntry(const RenderProxyHandle proxy,
                                                     spatial::EntryHandle& entry,
                                                     RenderProxyBounds& bounds) const noexcept
        {
            if (!ValidAliveProxy(proxy)) return false;
            const ProxySlot& slot = slots[proxy.scene.index].proxies[proxy.index];
            entry = slot.spatial;
            bounds = slot.bounds;
            return true;
        }

        static bool ResolveSpatialProxyBoundsThunk(void* const userData, const RenderProxyHandle proxy,
                                                   RenderProxyBounds& bounds) noexcept
        {
            return static_cast<const Impl*>(userData)->ResolveSpatialProxyBounds(proxy, bounds);
        }

        static bool ValidateSpatialProxyEntryThunk(void* const userData, const RenderProxyHandle proxy,
                                                   spatial::EntryHandle& entry,
                                                   RenderProxyBounds& bounds) noexcept
        {
            return static_cast<const Impl*>(userData)->ValidateSpatialProxyEntry(proxy, entry, bounds);
        }

        void RepairSpatialIndex(SceneSlot& scene) noexcept
        {
            spatial::CounterDelta delta;
            spatial::Repair(scene.spatial, this, ResolveSpatialProxyBoundsThunk, &delta);
            ApplySpatialDelta(delta);
        }

        [[nodiscard]] bool ValidateSpatialIndexInternal(const SceneSlot& scene,
                                                        SpatialWriteIndexStats* const outStats) const noexcept
        {
            return spatial::Validate(scene.spatial, const_cast<Impl*>(this), ValidateSpatialProxyEntryThunk, outStats);
        }

        containers::DynamicArray<SceneSlot> slots;
        mutable concurrency::RWLock publicationLock;
        RenderSceneManagerStats stats;
        u64 nextCreatedSerial = 1;
        u64 nextProxyCreatedSerial = 1;
        u64 nextMutationSerial = 1;
        u64 nextSceneVersion = 1;
        u64 nextCompletionToken = 1;
        u64 nextReaderEpoch = 1;
    };

    RenderSceneManager::~RenderSceneManager()
    {
        if (m_impl != nullptr) static_cast<void>(Shutdown());
    }

    bool RenderSceneManager::Initialize(const RenderSceneManagerConfig& config,
                                        RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderSceneFailureCode::AlreadyInitialized,
                        "RenderSceneManager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderSceneManager must initialize on the main thread");
        if (config.maximumScenes == 0 || config.maximumScenes > MaximumRenderScenes)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid RenderSceneManager scene capacity");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderSceneManager implementation allocation failed");
        Impl* const impl = ::new (block.address) Impl(config);
        if (impl->slots.Size() != config.maximumScenes)
        {
            impl->~Impl();
            memory::Free(block);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderSceneManager slot reservation failed");
        }
        m_impl = impl;
        return true;
    }

    bool RenderSceneManager::Shutdown(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderSceneManager must shutdown on the main thread");
        if (m_impl->stats.activeScenes != 0 || m_impl->stats.destroyingScenes != 0)
            return Fail(failure, RenderSceneFailureCode::ScenesRemainAlive,
                        "RenderSceneManager shutdown blocked by live scenes");
        if (m_impl->stats.activeProxies != 0)
            return Fail(failure, RenderSceneFailureCode::ProxiesRemainAlive,
                        "RenderSceneManager shutdown blocked by live proxies");
        if (m_impl->stats.liveReadLeases != 0)
            return Fail(failure, RenderSceneFailureCode::ReadersRemainAlive,
                        "RenderSceneManager shutdown blocked by live scene read leases");

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

    bool RenderSceneManager::CreateScene(const RenderSceneDesc& desc, RenderSceneHandle& scene,
                                         RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        scene = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderScene creation must run on the main thread");
        }
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!ValidMode(desc.mode) || !ValidOwnership(desc.ownership) || !ValidSpatialConfig(desc.spatial) ||
            desc.maximumProxies == 0 ||
            desc.maximumProxies > MaximumRenderProxySlotsPerScene ||
            desc.maximumPendingProxyMutations == 0 || desc.maximumViews == 0)
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid RenderScene creation descriptor");
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
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "maximum RenderScene count exceeded");
        }

        Impl::SceneSlot& slot = m_impl->slots[slotIndex];
        char copiedName[MaximumRenderSceneNameBytes]{};
        if (!CopyName(copiedName, MaximumRenderSceneNameBytes, desc.name))
        {
            ++m_impl->stats.failedCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderScene name is missing or too long");
        }

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
        slot.currentMutationEpoch = 0;
        slot.preparedMutationEpoch = 0;
        slot.preparedMutationCount = 0;
        slot.framePrepared = false;
        slot.allowFramePipelineParticipation = desc.allowFramePipelineParticipation;
        slot.proxies.Clear();
        slot.mutations.Clear();
        slot.publishedVersions.Clear();
        slot.meshPayloads.Clear();
        slot.lightPayloads.Clear();
        slot.decalPayloads.Clear();
        m_impl->ConfigureSpatialIndex(slot, desc.spatial);
        m_impl->RecomputeRetainedSceneVersions();
        CopyNameUnchecked(slot.name, MaximumRenderSceneNameBytes, copiedName);

        scene = {slotIndex, slot.generation};
        ++m_impl->stats.activeScenes;
        ++m_impl->stats.createdScenes;
        return true;
    }

    bool RenderSceneManager::DestroyScene(const RenderSceneHandle scene,
                                          RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderScene destruction must run on the main thread", scene);
        }
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(scene))
        {
            ++m_impl->stats.failedDestroys;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle", scene);
        }

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (slot.state != RenderSceneState::Alive)
        {
            ++m_impl->stats.failedDestroys;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderScene is not alive", scene);
        }

        if (slot.activeProxies != 0)
            return Fail(failure, RenderSceneFailureCode::ProxiesRemainAlive,
                        "RenderScene destruction blocked by live proxies", scene);
        for (u32 index = 0; index < slot.publishedVersions.Size(); ++index)
        {
            if (slot.publishedVersions[index].readerCount != 0)
                return Fail(failure, RenderSceneFailureCode::ReadersRemainAlive,
                            "RenderScene destruction blocked by live read leases", scene);
        }

        slot.state = RenderSceneState::Vacant;
        slot.mode = RenderSceneMode::Runtime;
        slot.ownership = RenderSceneOwnership::Service;
        slot.maximumProxies = 0;
        slot.maximumPendingProxyMutations = 0;
        slot.maximumViews = 0;
        slot.createdSerial = 0;
        ++slot.lifecycleRevision;
        slot.allowFramePipelineParticipation = false;
        slot.name[0] = '\0';
        slot.proxies.Clear();
        slot.meshPayloads.Clear();
        slot.lightPayloads.Clear();
        slot.decalPayloads.Clear();
        m_impl->ConfigureSpatialIndex(slot, {});
        if (slot.mutations.Size() != 0)
        {
            m_impl->stats.pendingProxyMutations -= slot.mutations.Size();
            slot.mutations.Clear();
        }
        slot.publishedVersions.Clear();
        m_impl->RecomputeRetainedSceneVersions();
        --m_impl->stats.activeScenes;
        ++m_impl->stats.destroyedScenes;
        return true;
    }

    bool RenderSceneManager::CreateProxy(const RenderProxyDesc& desc, RenderProxyHandle& proxy,
                                         RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy creation must run on the main thread", desc.scene);
        }
        if (!m_impl->ValidAliveScene(desc.scene))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for RenderProxy creation", desc.scene);
        }
        if (desc.typeId == 0 || !ValidTransform(desc.transform) || !ValidBounds(desc.bounds) ||
            !ValidSpatialMode(desc.spatialMode))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid RenderProxy creation descriptor", desc.scene);
        }

        Impl::SceneSlot& scene = m_impl->slots[desc.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy creation is blocked between scene preparation and commit", desc.scene);
        }
        if (desc.spatialMode == RenderProxySpatialMode::Bounds && !m_impl->SpatialBoundsAccepted(scene, desc.bounds))
        {
            ++m_impl->stats.failedProxyCreates;
            ++scene.spatial.stats.outOfRangeProxies;
            ++m_impl->stats.spatialOutOfRangeProxies;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderProxy bounds are outside the configured spatial extent", desc.scene);
        }
        if (scene.activeProxies >= scene.maximumProxies)
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "maximum RenderProxy count exceeded", desc.scene);
        }

        char copiedName[MaximumRenderProxyNameBytes]{};
        if (!CopyName(copiedName, MaximumRenderProxyNameBytes, desc.debugName))
        {
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderProxy debug name is missing or too long", desc.scene);
        }

        u32 slotIndex = scene.proxies.Size();
        for (u32 index = 0; index < scene.proxies.Size(); ++index)
        {
            if (scene.proxies[index].state == RenderProxyState::Vacant ||
                scene.proxies[index].state == RenderProxyState::Retired)
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == scene.proxies.Size()) scene.proxies.PushBack({});

        Impl::ProxySlot& slot = scene.proxies[slotIndex];
        const u32 admittedGeneration = NextGeneration(slot.generation);
        slot.state = RenderProxyState::Alive;
        slot.generation = admittedGeneration;
        slot.typeId = desc.typeId;
        slot.producerId = desc.producerId;
        slot.producerGeneration = desc.producerGeneration;
        slot.transform = desc.transform;
        slot.bounds = desc.bounds;
        slot.spatialMode = desc.spatialMode;
        slot.spatial = {};
        slot.visibility = desc.visibility;
        slot.layerMask = desc.layerMask;
        slot.visibilityMask = desc.visibilityMask;
        slot.userDataEpoch = desc.userDataEpoch;
        slot.createdSerial = m_impl->nextProxyCreatedSerial++;
        slot.lifecycleRevision = 1;
        CopyNameUnchecked(slot.debugName, MaximumRenderProxyNameBytes, copiedName);

        proxy = {desc.scene, slotIndex, slot.generation};
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::Create))
        {
            slot = {};
            slot.generation = admittedGeneration;
            proxy = {};
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy creation mutation budget exceeded", desc.scene, proxy);
        }
        if (!m_impl->InsertSpatialEntry(scene, proxy, slot))
        {
            slot = {};
            slot.generation = admittedGeneration;
            proxy = {};
            --m_impl->stats.pendingProxyMutations;
            scene.mutations.PopBack();
            ++m_impl->stats.failedProxyCreates;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderProxy spatial insertion failed", desc.scene, proxy);
        }
        ++scene.activeProxies;
        ++scene.lifecycleRevision;
        ++m_impl->stats.activeProxies;
        ++m_impl->stats.createdProxies;
        return true;
    }

    bool RenderSceneManager::CreateMeshProxy(const MeshProxyDesc& desc, RenderProxyHandle& proxy,
                                             RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (!desc.mesh.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "MeshProxy requires a valid mesh resource reference", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0) baseDesc.typeId = MeshProxyTypeId;
        if (!CreateProxy(baseDesc, proxy, failure)) return false;

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        u32 payloadIndex = scene.meshPayloads.Size();
        for (u32 index = 0; index < scene.meshPayloads.Size(); ++index)
        {
            if (scene.meshPayloads[index].state != RenderProxyState::Alive)
            {
                payloadIndex = index;
                break;
            }
        }
        if (payloadIndex == scene.meshPayloads.Size()) scene.meshPayloads.PushBack({});

        Impl::MeshPayloadSlot& payload = scene.meshPayloads[payloadIndex];
        payload.state = RenderProxyState::Alive;
        payload.proxy = proxy;
        payload.generation = proxy.generation;
        payload.mesh = desc.mesh;
        payload.material = desc.material;
        payload.meshHandle = desc.meshHandle;
        payload.materialHandle = desc.materialHandle;
        payload.submeshMask = desc.submeshMask;
        payload.renderFlags = desc.renderFlags;

        base.payloadKind = RenderProxyPayloadKind::Mesh;
        base.payloadIndex = payloadIndex;
        base.payloadGeneration = proxy.generation;
        ++m_impl->stats.activeMeshPayloads;
        ++m_impl->stats.createdPayloads;
        return true;
    }

    bool RenderSceneManager::CreateLightProxy(const LightProxyDesc& desc, RenderProxyHandle& proxy,
                                              RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (!ValidLightProperties(desc.kind, desc.color, desc.intensity, desc.range,
                                  desc.innerConeRadians, desc.outerConeRadians))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "LightProxy requires non-negative intensity and range", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0) baseDesc.typeId = LightProxyTypeId;
        if (!CreateProxy(baseDesc, proxy, failure)) return false;

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        u32 payloadIndex = scene.lightPayloads.Size();
        for (u32 index = 0; index < scene.lightPayloads.Size(); ++index)
        {
            if (scene.lightPayloads[index].state != RenderProxyState::Alive)
            {
                payloadIndex = index;
                break;
            }
        }
        if (payloadIndex == scene.lightPayloads.Size()) scene.lightPayloads.PushBack({});

        Impl::LightPayloadSlot& payload = scene.lightPayloads[payloadIndex];
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
        return true;
    }

    bool RenderSceneManager::CreateDecalProxy(const DecalProxyDesc& desc, RenderProxyHandle& proxy,
                                              RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        proxy = {};
        if (!desc.material.IsValid() || !std::isfinite(desc.extents[0]) || !std::isfinite(desc.extents[1]) ||
            !std::isfinite(desc.extents[2]) || !std::isfinite(desc.fadeDistance) ||
            desc.extents[0] < 0.0f || desc.extents[1] < 0.0f || desc.extents[2] < 0.0f ||
            desc.fadeDistance < 0.0f)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "DecalProxy requires a valid material and non-negative extents", desc.proxy.scene);

        RenderProxyDesc baseDesc = desc.proxy;
        if (baseDesc.typeId == 0) baseDesc.typeId = DecalProxyTypeId;
        if (!CreateProxy(baseDesc, proxy, failure)) return false;

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        u32 payloadIndex = scene.decalPayloads.Size();
        for (u32 index = 0; index < scene.decalPayloads.Size(); ++index)
        {
            if (scene.decalPayloads[index].state != RenderProxyState::Alive)
            {
                payloadIndex = index;
                break;
            }
        }
        if (payloadIndex == scene.decalPayloads.Size()) scene.decalPayloads.PushBack({});

        Impl::DecalPayloadSlot& payload = scene.decalPayloads[payloadIndex];
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
        return true;
    }

    bool RenderSceneManager::DestroyProxy(const RenderProxyHandle proxy,
                                          RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy destruction must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderProxy handle", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy destruction is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        if (slot.state == RenderProxyState::Destroying)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::PendingDestroy,
                        "RenderProxy destruction is already pending", proxy.scene, proxy);
        }
        if (slot.state != RenderProxyState::Alive)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy is not alive", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::Destroy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy destruction mutation budget exceeded", proxy.scene, proxy);
        }

        m_impl->RemoveSpatialEntry(scene, slot);
        switch (slot.payloadKind)
        {
        case RenderProxyPayloadKind::Mesh:
            if (slot.payloadIndex < scene.meshPayloads.Size() &&
                scene.meshPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                scene.meshPayloads[slot.payloadIndex] = {};
                --m_impl->stats.activeMeshPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::Light:
            if (slot.payloadIndex < scene.lightPayloads.Size() &&
                scene.lightPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                scene.lightPayloads[slot.payloadIndex] = {};
                --m_impl->stats.activeLightPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::Decal:
            if (slot.payloadIndex < scene.decalPayloads.Size() &&
                scene.decalPayloads[slot.payloadIndex].state == RenderProxyState::Alive)
            {
                scene.decalPayloads[slot.payloadIndex] = {};
                --m_impl->stats.activeDecalPayloads;
                ++m_impl->stats.destroyedPayloads;
            }
            break;
        case RenderProxyPayloadKind::None:
            break;
        }
        slot.payloadKind = RenderProxyPayloadKind::None;
        slot.payloadIndex = ~u32{0};
        slot.payloadGeneration = 0;
        slot.state = RenderProxyState::Retired;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        --scene.activeProxies;
        --m_impl->stats.activeProxies;
        ++m_impl->stats.destroyedProxies;
        return true;
    }

    bool RenderSceneManager::UpdateProxyTransform(const RenderProxyHandle proxy,
                                                  const RenderProxyTransform& transform,
                                                  const RenderProxyBounds& bounds,
                                                  const u64 producerGeneration,
                                                  RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy transform update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        if (!ValidTransform(transform) || !ValidBounds(bounds))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid RenderProxy bounds", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy transform update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (m_impl->slots[proxy.scene.index].proxies[proxy.index].spatialMode == RenderProxySpatialMode::Bounds &&
            !m_impl->SpatialBoundsAccepted(scene, bounds))
        {
            ++m_impl->stats.failedProxyMutations;
            ++scene.spatial.stats.outOfRangeProxies;
            ++m_impl->stats.spatialOutOfRangeProxies;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderProxy transform moves bounds outside the configured spatial extent", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::TransformAndBounds))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy transform mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        const RenderProxyBounds oldBounds = slot.bounds;
        slot.transform = transform;
        slot.bounds = bounds;
        if (producerGeneration != 0) slot.producerGeneration = producerGeneration;
        m_impl->MoveSpatialEntry(scene, proxy, slot, oldBounds);
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateProxyVisibility(const RenderProxyHandle proxy,
                                                   const RenderProxyVisibilityFlags visibility,
                                                   const u32 visibilityMask,
                                                   RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy visibility update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy visibility update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::Visibility))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy visibility mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.visibility = visibility;
        slot.visibilityMask = visibilityMask;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateProxyLayerMask(const RenderProxyHandle proxy, const u64 layerMask,
                                                  RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy layer update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy layer update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::LayerMask))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy layer mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.layerMask = layerMask;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateProxyUserDataEpoch(const RenderProxyHandle proxy, const u64 userDataEpoch,
                                                      RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderProxy user-data update must run on the main thread", proxy.scene, proxy);
        }
        if (!m_impl->ValidAliveProxy(proxy))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid, stale, or non-alive RenderProxy handle", proxy.scene, proxy);
        }
        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderProxy user-data update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::UserDataEpoch))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "RenderProxy user-data mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& slot = scene.proxies[proxy.index];
        slot.userDataEpoch = userDataEpoch;
        ++slot.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateMeshProxyResources(const RenderProxyHandle proxy,
                                                      const resources::ResourceReference mesh,
                                                      const resources::ResourceReference material,
                                                      const resources::ResourceHandle& meshHandle,
                                                      const resources::ResourceHandle& materialHandle,
                                                      RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "MeshProxy resource update must run on the main thread", proxy.scene, proxy);
        }
        if (!mesh.IsValid())
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "MeshProxy resource update requires a valid mesh reference", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a live mesh payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "MeshProxy resource update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::MeshResources))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "MeshProxy resource mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::MeshPayloadSlot& payload = scene.meshPayloads[base.payloadIndex];
        payload.mesh = mesh;
        payload.material = material;
        payload.meshHandle = meshHandle;
        payload.materialHandle = materialHandle;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateLightProxyProperties(const RenderProxyHandle proxy,
                                                        const LightProxySnapshot& properties,
                                                        RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "LightProxy property update must run on the main thread", proxy.scene, proxy);
        }
        if (!ValidLightProperties(properties.kind, properties.color, properties.intensity, properties.range,
                                  properties.innerConeRadians, properties.outerConeRadians))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "LightProxy property update requires non-negative intensity and range", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Light))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a live light payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "LightProxy property update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::LightProperties))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "LightProxy property mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::LightPayloadSlot& payload = scene.lightPayloads[base.payloadIndex];
        payload.kind = properties.kind;
        payload.color[0] = properties.color[0];
        payload.color[1] = properties.color[1];
        payload.color[2] = properties.color[2];
        payload.intensity = properties.intensity;
        payload.range = properties.range;
        payload.innerConeRadians = properties.innerConeRadians;
        payload.outerConeRadians = properties.outerConeRadians;
        payload.castsShadow = properties.castsShadow;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::UpdateDecalProxyMaterial(const RenderProxyHandle proxy,
                                                      const resources::ResourceReference material,
                                                      const resources::ResourceHandle& materialHandle,
                                                      RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", proxy.scene, proxy);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "DecalProxy material update must run on the main thread", proxy.scene, proxy);
        }
        if (!material.IsValid())
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "DecalProxy material update requires a valid material reference", proxy.scene, proxy);
        }
        if (!m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Decal))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a live decal payload", proxy.scene, proxy);
        }

        Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        if (scene.framePrepared)
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "DecalProxy material update is blocked between scene preparation and commit", proxy.scene, proxy);
        }
        if (!m_impl->AppendMutation(scene, proxy, RenderProxyMutationKind::DecalMaterial))
        {
            ++m_impl->stats.failedProxyMutations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "DecalProxy material mutation budget exceeded", proxy.scene, proxy);
        }
        Impl::ProxySlot& base = scene.proxies[proxy.index];
        Impl::DecalPayloadSlot& payload = scene.decalPayloads[base.payloadIndex];
        payload.material = material;
        payload.materialHandle = materialHandle;
        ++base.lifecycleRevision;
        ++scene.lifecycleRevision;
        return true;
    }

    bool RenderSceneManager::PrepareSceneFrame(const RenderSceneHandle scene,
                                               RenderSceneFramePrepareResult& result,
                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderScene frame preparation must run on the main thread", scene);
        }
        if (!m_impl->ValidAliveScene(scene))
        {
            ++m_impl->stats.failedPublishes;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for frame preparation", scene);
        }

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (slot.framePrepared)
        {
            ++m_impl->stats.failedPublishes;
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "RenderScene frame is already prepared and must be committed before preparing again", scene);
        }
        result.scene = scene;
        result.mutationEpoch = slot.currentMutationEpoch;
        result.drainedMutations = slot.mutations.Size();
        result.completedSynchronously = true;
        slot.preparedMutationEpoch = slot.currentMutationEpoch;
        slot.preparedMutationCount = slot.mutations.Size();
        slot.framePrepared = true;
        m_impl->RepairSpatialIndex(slot);
        if (slot.mutations.Size() != 0)
        {
            m_impl->stats.pendingProxyMutations -= slot.mutations.Size();
            slot.mutations.Clear();
        }
        ++m_impl->stats.preparedFrames;
        return true;
    }

    bool RenderSceneManager::CommitScene(const RenderSceneHandle scene, RenderSceneCommitResult& result,
                                         RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", scene);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderScene commit must run on the main thread", scene);
        }
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(scene))
        {
            ++m_impl->stats.failedPublishes;
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for commit", scene);
        }

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (!slot.framePrepared)
        {
            ++m_impl->stats.failedPublishes;
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderScene commit requires a prepared frame", scene);
        }

        for (u32 index = 0; index < slot.publishedVersions.Size(); ++index)
            slot.publishedVersions[index].retired = true;

        slot.publishedVersions.PushBack({});
        Impl::PublishedSceneVersion& published = slot.publishedVersions[slot.publishedVersions.Size() - 1u];
        published.version = {m_impl->nextSceneVersion++};
        published.scene = scene;
        published.mutationEpoch = slot.preparedMutationEpoch;
        published.lifecycleRevision = slot.lifecycleRevision;
        published.readerCount = 0;
        published.retired = false;
        published.proxies.Clear();
        published.proxyLookup.Clear();
        published.proxyLookup.Resize(slot.proxies.Size());
        for (u32 index = 0; index < published.proxyLookup.Size(); ++index) published.proxyLookup[index] = ~u32{0};
        published.spatialCells.Clear();
        published.spatialCellProxies.Clear();
        published.meshPayloads.Clear();
        published.lightPayloads.Clear();
        published.decalPayloads.Clear();
        published.meshPayloadLookup.Clear();
        published.lightPayloadLookup.Clear();
        published.decalPayloadLookup.Clear();
        published.meshPayloadLookup.Resize(slot.proxies.Size());
        published.lightPayloadLookup.Resize(slot.proxies.Size());
        published.decalPayloadLookup.Resize(slot.proxies.Size());
        for (u32 index = 0; index < slot.proxies.Size(); ++index)
        {
            published.meshPayloadLookup[index] = ~u32{0};
            published.lightPayloadLookup[index] = ~u32{0};
            published.decalPayloadLookup[index] = ~u32{0};
        }

        for (u32 proxyIndex = 0; proxyIndex < slot.proxies.Size(); ++proxyIndex)
        {
            const Impl::ProxySlot& proxy = slot.proxies[proxyIndex];
            if (proxy.state != RenderProxyState::Alive) continue;
            RenderProxySnapshot snapshot;
            m_impl->BuildProxySnapshot(scene, proxyIndex, proxy, snapshot);
            published.proxyLookup[proxyIndex] = published.proxies.Size();
            published.proxies.PushBack(snapshot);
            switch (proxy.payloadKind)
            {
            case RenderProxyPayloadKind::Mesh:
                if (m_impl->ValidAlivePayload(snapshot.handle, RenderProxyPayloadKind::Mesh))
                {
                    published.meshPayloadLookup[proxyIndex] = published.meshPayloads.Size();
                    MeshProxySnapshot mesh;
                    m_impl->BuildMeshSnapshot(slot.meshPayloads[proxy.payloadIndex], mesh);
                    published.meshPayloads.PushBack(mesh);
                }
                break;
            case RenderProxyPayloadKind::Light:
                if (m_impl->ValidAlivePayload(snapshot.handle, RenderProxyPayloadKind::Light))
                {
                    published.lightPayloadLookup[proxyIndex] = published.lightPayloads.Size();
                    LightProxySnapshot light;
                    m_impl->BuildLightSnapshot(slot.lightPayloads[proxy.payloadIndex], light);
                    published.lightPayloads.PushBack(light);
                }
                break;
            case RenderProxyPayloadKind::Decal:
                if (m_impl->ValidAlivePayload(snapshot.handle, RenderProxyPayloadKind::Decal))
                {
                    published.decalPayloadLookup[proxyIndex] = published.decalPayloads.Size();
                    DecalProxySnapshot decal;
                    m_impl->BuildDecalSnapshot(slot.decalPayloads[proxy.payloadIndex], decal);
                    published.decalPayloads.PushBack(decal);
                }
                break;
            case RenderProxyPayloadKind::None:
                break;
            }
        }

        spatial::PublishCells(slot.spatial, published.proxyLookup, published.proxies,
                              published.spatialCells, published.spatialCellProxies);

        result.scene = scene;
        result.version = published.version;
        result.completion = {m_impl->nextCompletionToken++};
        result.mutationEpoch = published.mutationEpoch;
        result.proxyCount = published.proxies.Size();
        result.completedSynchronously = true;
        slot.framePrepared = false;
        slot.preparedMutationCount = 0;
        ++m_impl->stats.committedVersions;
        m_impl->RecomputeRetainedSceneVersions();
        return true;
    }

    bool RenderSceneManager::AcquireLatestReadLease(const RenderSceneHandle scene, SceneReadLease& lease,
                                                    RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", scene);
        if (lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "cannot overwrite a live RenderScene read lease", lease.scene);
        lease = {};
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidAliveScene(scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for read lease", scene);

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        if (slot.publishedVersions.Size() == 0)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "RenderScene has no committed versions", scene);

        Impl::PublishedSceneVersion& version = slot.publishedVersions[slot.publishedVersions.Size() - 1u];
        const u64 readerEpoch = m_impl->AllocateReaderEpoch();
        version.readerEpochs.PushBack(readerEpoch);
        ++version.readerCount;
        ++m_impl->stats.liveReadLeases;
        lease.scene = scene;
        lease.version = version.version;
        lease.readerEpoch = readerEpoch;
        lease.proxyCount = version.proxies.Size();
        lease.spatialCellCount = version.spatialCells.Size();
        return true;
    }

    bool RenderSceneManager::AcquireReadLease(const RenderSceneHandle scene, const RenderSceneVersion version,
                                              SceneReadLease& lease,
                                              RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", scene);
        if (lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "cannot overwrite a live RenderScene read lease", lease.scene);
        lease = {};
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!version.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid RenderScene version requested", scene);
        if (!m_impl->ValidAliveScene(scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for exact read lease", scene);

        Impl::SceneSlot& slot = m_impl->slots[scene.index];
        Impl::PublishedSceneVersion* const published = m_impl->FindPublishedVersion(slot, version);
        if (published == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "requested RenderScene version is not retained", scene);

        const u64 readerEpoch = m_impl->AllocateReaderEpoch();
        published->readerEpochs.PushBack(readerEpoch);
        ++published->readerCount;
        ++m_impl->stats.liveReadLeases;
        lease.scene = scene;
        lease.version = published->version;
        lease.readerEpoch = readerEpoch;
        lease.proxyCount = published->proxies.Size();
        lease.spatialCellCount = published->spatialCells.Size();
        return true;
    }

    bool RenderSceneManager::ReleaseReadLease(SceneReadLease& lease,
                                              RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!lease.IsValid()) return true;
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", lease.scene);
        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for read lease release", lease.scene);

        Impl::SceneSlot& slot = m_impl->slots[lease.scene.index];
        Impl::PublishedSceneVersion* const version = m_impl->FindPublishedVersion(slot, lease.version);
        const u32 readerIndex = version != nullptr ? m_impl->FindReaderEpoch(*version, lease.readerEpoch) : ~u32{0};
        if (version == nullptr || readerIndex == ~u32{0})
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "released RenderScene version is not retained", lease.scene);

        version->readerEpochs.RemoveAt(readerIndex);
        --version->readerCount;
        --m_impl->stats.liveReadLeases;
        lease = {};
        return true;
    }

    bool RenderSceneManager::RetirePublishedVersions(RenderSceneVersionRetirementResult& result,
                                                     RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderScene version retirement must run on the main thread");

        concurrency::ScopedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        for (u32 sceneIndex = 0; sceneIndex < m_impl->slots.Size(); ++sceneIndex)
        {
            Impl::SceneSlot& scene = m_impl->slots[sceneIndex];
            for (u32 versionIndex = 0; versionIndex < scene.publishedVersions.Size();)
            {
                const Impl::PublishedSceneVersion& version = scene.publishedVersions[versionIndex];
                if (!version.retired)
                {
                    ++versionIndex;
                    continue;
                }
                if (version.readerCount != 0)
                {
                    ++result.versionsBlockedByReaders;
                    ++versionIndex;
                    continue;
                }
                scene.publishedVersions.RemoveAt(versionIndex);
                ++result.reclaimedVersions;
            }
            result.retainedVersions += scene.publishedVersions.Size();
        }
        result.liveReadLeases = m_impl->stats.liveReadLeases;
        m_impl->RecomputeRetainedSceneVersions();
        return true;
    }

    bool RenderSceneManager::ReadProxy(const SceneReadLease& lease, const RenderProxyHandle proxy,
                                       RenderProxySnapshot& snapshot,
                                       RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        if (!lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid RenderScene read lease", lease.scene, proxy);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", lease.scene, proxy);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!(proxy.scene == lease.scene))
            return Fail(failure, RenderSceneFailureCode::WrongScene,
                        "RenderProxy does not belong to the read lease scene", lease.scene, proxy);
        if (!m_impl->ValidHandle(lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for read lease", lease.scene, proxy);

        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "RenderScene read lease version is not retained", lease.scene, proxy);

        const RenderProxySnapshot* const resolved = m_impl->ResolvePublishedProxy(*version, proxy);
        if (resolved != nullptr)
        {
            snapshot = *resolved;
            return true;
        }

        return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                    "RenderProxy is not present in the leased RenderScene version", lease.scene, proxy);
    }

    bool RenderSceneManager::ReadMeshProxy(const SceneReadLease& lease, const RenderProxyHandle proxy,
                                           MeshProxySnapshot& snapshot,
                                           RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        RenderProxySnapshot base;
        if (!ReadProxy(lease, proxy, base, failure)) return false;
        if (base.payloadKind != RenderProxyPayloadKind::Mesh)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a mesh payload in this scene version", lease.scene, proxy);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "RenderScene read lease version is not retained", lease.scene, proxy);
        if (proxy.index < version->meshPayloadLookup.Size())
        {
            const u32 index = version->meshPayloadLookup[proxy.index];
            if (index < version->meshPayloads.Size() && version->meshPayloads[index].proxy == proxy)
            {
                snapshot = version->meshPayloads[index];
                return true;
            }
        }
        return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                    "MeshProxy payload is not present in the leased RenderScene version", lease.scene, proxy);
    }

    bool RenderSceneManager::ReadLightProxy(const SceneReadLease& lease, const RenderProxyHandle proxy,
                                            LightProxySnapshot& snapshot,
                                            RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        RenderProxySnapshot base;
        if (!ReadProxy(lease, proxy, base, failure)) return false;
        if (base.payloadKind != RenderProxyPayloadKind::Light)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a light payload in this scene version", lease.scene, proxy);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "RenderScene read lease version is not retained", lease.scene, proxy);
        if (proxy.index < version->lightPayloadLookup.Size())
        {
            const u32 index = version->lightPayloadLookup[proxy.index];
            if (index < version->lightPayloads.Size() && version->lightPayloads[index].proxy == proxy)
            {
                snapshot = version->lightPayloads[index];
                return true;
            }
        }
        return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                    "LightProxy payload is not present in the leased RenderScene version", lease.scene, proxy);
    }

    bool RenderSceneManager::ReadDecalProxy(const SceneReadLease& lease, const RenderProxyHandle proxy,
                                            DecalProxySnapshot& snapshot,
                                            RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        RenderProxySnapshot base;
        if (!ReadProxy(lease, proxy, base, failure)) return false;
        if (base.payloadKind != RenderProxyPayloadKind::Decal)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "RenderProxy does not own a decal payload in this scene version", lease.scene, proxy);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "RenderScene read lease version is not retained", lease.scene, proxy);
        if (proxy.index < version->decalPayloadLookup.Size())
        {
            const u32 index = version->decalPayloadLookup[proxy.index];
            if (index < version->decalPayloads.Size() && version->decalPayloads[index].proxy == proxy)
            {
                snapshot = version->decalPayloads[index];
                return true;
            }
        }
        return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                    "DecalProxy payload is not present in the leased RenderScene version", lease.scene, proxy);
    }

    bool RenderSceneManager::CollectVisibleProxies(const VisibilityQueryRequest& request,
                                                   containers::DynamicArray<RenderProxyHandle>& proxies,
                                                   VisibilityQueryResult& result,
                                                   RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        proxies.Clear();
        result = {};
        result.scene = request.lease.scene;
        result.version = request.lease.version;
        if (!request.lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid RenderScene read lease for visibility query", request.lease.scene);
        if (request.useBounds && !ValidBounds(request.bounds))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility query bounds", request.lease.scene);
        if (request.useFrustum && !ValidFrustum(request.frustum))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility query frustum", request.lease.scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", request.lease.scene);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(request.lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for visibility query", request.lease.scene);

        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(request.lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "visibility query version is not retained by a live read lease", request.lease.scene);

        spatial::Collect(request, version->spatialCells, version->spatialCellProxies,
                         version->proxyLookup, version->proxies, proxies, result);
        return true;
    }

    bool RenderSceneManager::BuildVisibilityQueryPlan(const SceneReadLease& lease, const u32 targetCellsPerBatch,
                                                      containers::DynamicArray<VisibilityQueryBatch>& batches,
                                                      VisibilityQueryPlan& plan,
                                                      RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        batches.Clear();
        plan = {};
        plan.scene = lease.scene;
        plan.version = lease.version;
        if (!lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid RenderScene read lease for visibility query planning", lease.scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", lease.scene);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for visibility query planning", lease.scene);

        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "visibility query plan version is not retained by a live read lease", lease.scene);

        plan.scene = lease.scene;
        plan.version = lease.version;
        spatial::BuildBatches(version->spatialCells.Size(), targetCellsPerBatch, batches, plan);
        return true;
    }

    bool RenderSceneManager::CollectVisibleProxyBatch(const VisibilityQueryRequest& request,
                                                      const VisibilityQueryBatch& batch,
                                                      containers::DynamicArray<RenderProxyHandle>& proxies,
                                                      VisibilityQueryResult& result,
                                                      RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        proxies.Clear();
        result = {};
        result.scene = request.lease.scene;
        result.version = request.lease.version;
        if (!request.lease.IsValid())
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid RenderScene read lease for visibility query batch", request.lease.scene);
        if (!batch.IsValid() || !(batch.scene == request.lease.scene) || !(batch.version == request.lease.version))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "visibility query batch does not belong to the retained scene version", request.lease.scene);
        if (request.maximumResults != ~u32{0})
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "per-batch visibility collection requires an unbounded local result; apply limits during reduction",
                        request.lease.scene);
        if (request.useBounds && !ValidBounds(request.bounds))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility query batch bounds", request.lease.scene);
        if (request.useFrustum && !ValidFrustum(request.frustum))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility query batch frustum", request.lease.scene);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "RenderSceneManager is not initialized", request.lease.scene);
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(request.lease.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale RenderScene handle for visibility query batch", request.lease.scene);

        const Impl::PublishedSceneVersion* const version = m_impl->ResolveLease(request.lease);
        if (version == nullptr)
            return Fail(failure, RenderSceneFailureCode::VersionNotFound,
                        "visibility query batch version is not retained by a live read lease", request.lease.scene);
        if (batch.firstCell >= version->spatialCells.Size())
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "visibility query batch is outside the retained spatial snapshot", request.lease.scene);

        spatial::CollectRange(request, version->spatialCells, version->spatialCellProxies,
                              version->proxyLookup, version->proxies, batch, proxies, result);
        return true;
    }

    bool RenderSceneManager::IsAlive(const RenderSceneHandle scene) const noexcept
    {
        return m_impl != nullptr && m_impl->ValidHandle(scene) &&
               m_impl->slots[scene.index].state == RenderSceneState::Alive;
    }

    bool RenderSceneManager::IsProxyAlive(const RenderProxyHandle proxy) const noexcept
    {
        return m_impl != nullptr && m_impl->ValidAliveProxy(proxy);
    }

    bool RenderSceneManager::IsProxyRetained(const RenderProxyHandle proxy) const noexcept
    {
        if (m_impl == nullptr || !proxy.IsValid()) return false;
        concurrency::ScopedSharedLock<concurrency::RWLock> publicationGuard(m_impl->publicationLock);
        if (!m_impl->ValidHandle(proxy.scene)) return false;
        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        for (const Impl::PublishedSceneVersion& version : scene.publishedVersions)
            if (m_impl->ResolvePublishedProxy(version, proxy) != nullptr) return true;
        return false;
    }

    bool RenderSceneManager::Snapshot(const RenderSceneHandle scene,
                                      RenderSceneSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidHandle(scene)) return false;
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
        snapshot.pendingProxyMutations = slot.mutations.Size();
        snapshot.createdSerial = slot.createdSerial;
        snapshot.lifecycleRevision = slot.lifecycleRevision;
        snapshot.allowFramePipelineParticipation = slot.allowFramePipelineParticipation;
        CopyNameUnchecked(snapshot.name, MaximumRenderSceneNameBytes, slot.name);
        return true;
    }

    bool RenderSceneManager::SnapshotProxy(const RenderProxyHandle proxy,
                                           RenderProxySnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidProxy(proxy)) return false;
        const Impl::ProxySlot& slot = m_impl->slots[proxy.scene.index].proxies[proxy.index];
        snapshot.handle = proxy;
        snapshot.state = slot.state;
        snapshot.typeId = slot.typeId;
        snapshot.producerId = slot.producerId;
        snapshot.producerGeneration = slot.producerGeneration;
        snapshot.transform = slot.transform;
        snapshot.bounds = slot.bounds;
        snapshot.visibility = slot.visibility;
        snapshot.layerMask = slot.layerMask;
        snapshot.visibilityMask = slot.visibilityMask;
        snapshot.userDataEpoch = slot.userDataEpoch;
        snapshot.createdSerial = slot.createdSerial;
        snapshot.lifecycleRevision = slot.lifecycleRevision;
        CopyNameUnchecked(snapshot.debugName, MaximumRenderProxyNameBytes, slot.debugName);
        return true;
    }

    bool RenderSceneManager::ValidateSpatialIndex(const RenderSceneHandle scene,
                                                  SpatialWriteIndexStats* const stats) const noexcept
    {
        if (stats != nullptr) *stats = {};
        if (m_impl == nullptr || !m_impl->ValidAliveScene(scene)) return false;
        return m_impl->ValidateSpatialIndexInternal(m_impl->slots[scene.index], stats);
    }

    bool RenderSceneManager::SnapshotMeshProxy(const RenderProxyHandle proxy,
                                               MeshProxySnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Mesh)) return false;
        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& base = scene.proxies[proxy.index];
        m_impl->BuildMeshSnapshot(scene.meshPayloads[base.payloadIndex], snapshot);
        return true;
    }

    bool RenderSceneManager::SnapshotLightProxy(const RenderProxyHandle proxy,
                                                LightProxySnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Light)) return false;
        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& base = scene.proxies[proxy.index];
        m_impl->BuildLightSnapshot(scene.lightPayloads[base.payloadIndex], snapshot);
        return true;
    }

    bool RenderSceneManager::SnapshotDecalProxy(const RenderProxyHandle proxy,
                                                DecalProxySnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || !m_impl->ValidAlivePayload(proxy, RenderProxyPayloadKind::Decal)) return false;
        const Impl::SceneSlot& scene = m_impl->slots[proxy.scene.index];
        const Impl::ProxySlot& base = scene.proxies[proxy.index];
        m_impl->BuildDecalSnapshot(scene.decalPayloads[base.payloadIndex], snapshot);
        return true;
    }

    RenderSceneManagerStats RenderSceneManager::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : RenderSceneManagerStats{};
    }
} // namespace vanguard::rendering
