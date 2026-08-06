#include <vanguard/world/streaming_grid.hpp>

#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <cmath>
#include <immintrin.h>
#include <limits>
#include <new>

namespace
{
    using namespace vanguard;
    namespace world = vanguard::world;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateWorldStreamingObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(Type), alignof(Type));
        if (!block) return nullptr;
        return ::new (block.address) Type(static_cast<Args&&>(args)...);
    }

    template<typename Type>
    void DeleteWorldStreamingObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Streaming};
        memory::Free(block);
    }

    struct LocalPosition
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
    };

    [[nodiscard]] bool IsFinite(const LocalPosition position) noexcept
    {
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    class StreamingProxyQuery final
    {
    public:
        static constexpr f32 InvalidRadius = -1.0f;
        static constexpr f32 InfiniteRadius = std::numeric_limits<f32>::max();

        StreamingProxyQuery() noexcept
            : m_x(memory::pools::Streaming::GetInstance()), m_y(memory::pools::Streaming::GetInstance()),
              m_z(memory::pools::Streaming::GetInstance()), m_radiusSquared(memory::pools::Streaming::GetInstance()),
              m_threeDimensional(memory::pools::Streaming::GetInstance())
        {
            Reserve(512);
        }

        void Reserve(const u32 count) noexcept
        {
            m_x.Reserve(count);
            m_y.Reserve(count);
            m_z.Reserve(count);
            m_radiusSquared.Reserve(count);
            m_threeDimensional.Reserve(count);
        }

        [[nodiscard]] bool PushBack(const LocalPosition position, const f32 radius, const bool threeDimensional) noexcept
        {
            const u32 expected = m_x.Size() + 1u;
            m_x.PushBack(position.x);
            m_y.PushBack(position.y);
            m_z.PushBack(position.z);
            m_radiusSquared.PushBack(radius == InvalidRadius || radius == InfiniteRadius ? radius : radius * radius);
            m_threeDimensional.PushBack(threeDimensional ? 1.0f : 0.0f);
            return m_x.Size() == expected && m_y.Size() == expected && m_z.Size() == expected &&
                   m_radiusSquared.Size() == expected && m_threeDimensional.Size() == expected;
        }

        void CollectSingle(const LocalPosition reference, containers::BitSet64Dynamic& output,
                           const f32 distanceScale) const noexcept
        {
            const __m128 referenceX = _mm_set1_ps(reference.x);
            const __m128 referenceY = _mm_set1_ps(reference.y);
            const __m128 referenceZ = _mm_set1_ps(reference.z);
            const __m128 scaleSquared = _mm_set1_ps(distanceScale * distanceScale);
            u8* const mask = reinterpret_cast<u8*>(const_cast<u64*>(output.Data()));
            u32 index = 0;
            while (index + 8u <= m_x.Size())
            {
                const auto collectFour = [&](const u32 offset) noexcept -> u8
                {
                    const __m128 dimensional = _mm_loadu_ps(m_threeDimensional.TypedData() + offset);
                    const __m128 deltaX = _mm_sub_ps(_mm_loadu_ps(m_x.TypedData() + offset), referenceX);
                    const __m128 deltaY = _mm_sub_ps(_mm_loadu_ps(m_y.TypedData() + offset), referenceY);
                    const __m128 deltaZ = _mm_sub_ps(_mm_mul_ps(_mm_loadu_ps(m_z.TypedData() + offset), dimensional),
                                                     _mm_mul_ps(referenceZ, dimensional));
                    const __m128 distanceSquared = _mm_add_ps(_mm_add_ps(_mm_mul_ps(deltaX, deltaX),
                                                                          _mm_mul_ps(deltaY, deltaY)),
                                                               _mm_mul_ps(deltaZ, deltaZ));
                    const __m128 radiusSquared = _mm_mul_ps(_mm_loadu_ps(m_radiusSquared.TypedData() + offset), scaleSquared);
                    return static_cast<u8>(_mm_movemask_ps(_mm_sub_ps(distanceSquared, radiusSquared)));
                };
                mask[index / 8u] = static_cast<u8>((collectFour(index) & 0x0fu) | (collectFour(index + 4u) << 4u));
                index += 8u;
            }
            for (; index < m_x.Size(); ++index)
            {
                const f32 dimensional = m_threeDimensional[index];
                const f32 deltaX = m_x[index] - reference.x;
                const f32 deltaY = m_y[index] - reference.y;
                const f32 deltaZ = (m_z[index] - reference.z) * dimensional;
                const f32 distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
                if (distanceSquared < m_radiusSquared[index] * distanceScale * distanceScale) output.Set(index);
            }
        }

        void CollectMultiple(const containers::ArraySpan<const LocalPosition> references,
                             containers::BitSet64Dynamic& output, const f32 distanceScale) const noexcept
        {
            if (references.Empty()) return;
            const __m128 scaleSquared = _mm_set1_ps(distanceScale * distanceScale);
            u8* const mask = reinterpret_cast<u8*>(const_cast<u64*>(output.Data()));
            u32 index = 0;
            while (index + 8u <= m_x.Size())
            {
                u8 currentMask = 0;
                for (const LocalPosition reference : references)
                {
                    const __m128 referenceX = _mm_set1_ps(reference.x);
                    const __m128 referenceY = _mm_set1_ps(reference.y);
                    const __m128 referenceZ = _mm_set1_ps(reference.z);
                    const auto collectFour = [&](const u32 offset) noexcept -> u8
                    {
                        const __m128 dimensional = _mm_loadu_ps(m_threeDimensional.TypedData() + offset);
                        const __m128 deltaX = _mm_sub_ps(_mm_loadu_ps(m_x.TypedData() + offset), referenceX);
                        const __m128 deltaY = _mm_sub_ps(_mm_loadu_ps(m_y.TypedData() + offset), referenceY);
                        const __m128 deltaZ = _mm_sub_ps(_mm_mul_ps(_mm_loadu_ps(m_z.TypedData() + offset), dimensional),
                                                         _mm_mul_ps(referenceZ, dimensional));
                        const __m128 distanceSquared = _mm_add_ps(_mm_add_ps(_mm_mul_ps(deltaX, deltaX),
                                                                              _mm_mul_ps(deltaY, deltaY)),
                                                                   _mm_mul_ps(deltaZ, deltaZ));
                        const __m128 radiusSquared =
                            _mm_mul_ps(_mm_loadu_ps(m_radiusSquared.TypedData() + offset), scaleSquared);
                        return static_cast<u8>(_mm_movemask_ps(_mm_sub_ps(distanceSquared, radiusSquared)));
                    };
                    currentMask |= static_cast<u8>((collectFour(index) & 0x0fu) | (collectFour(index + 4u) << 4u));
                }
                mask[index / 8u] = currentMask;
                index += 8u;
            }
            for (; index < m_x.Size(); ++index)
            {
                bool inRange = false;
                for (const LocalPosition reference : references)
                {
                    const f32 dimensional = m_threeDimensional[index];
                    const f32 deltaX = m_x[index] - reference.x;
                    const f32 deltaY = m_y[index] - reference.y;
                    const f32 deltaZ = (m_z[index] - reference.z) * dimensional;
                    inRange |= deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ <
                               m_radiusSquared[index] * distanceScale * distanceScale;
                }
                if (inRange) output.Set(index);
            }
        }

    private:
        containers::DynamicArray<f32> m_x;
        containers::DynamicArray<f32> m_y;
        containers::DynamicArray<f32> m_z;
        containers::DynamicArray<f32> m_radiusSquared;
        containers::DynamicArray<f32> m_threeDimensional;
    };

    [[nodiscard]] bool IsFlagSet(const world::WorldCellFlags value, const world::WorldCellFlags flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    [[nodiscard]] bool IsFlagSet(const world::DistantProxyFlags value, const world::DistantProxyFlags flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    [[nodiscard]] bool IsFlagSet(const world::ProxyChildFlags value, const world::ProxyChildFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }
} // namespace

namespace vanguard::world
{
    struct WorldStreamingGrid::Impl
    {
        struct RuntimeNode
        {
            StreamingNodeKey key;
            resources::ResourceReference resource;
            StreamingPriority priority = StreamingPriority::Normal;
            StreamingNodeState state = StreamingNodeState::Unloaded;
            LocalPosition preboostPosition;
            u32 sourceIndex = 0;
            bool streamInAllowed = true;
            bool renderReady = false;
        };

        struct SortEntry
        {
            u32 nodeIndex = 0;
            f32 distanceSquared = 0.0f;
            u8 priority = 0;
        };

        explicit Impl(const StreamingGridConfig& value) noexcept
            : config(value), nodes(memory::pools::Streaming::GetInstance()),
              observerPositions(memory::pools::Streaming::GetInstance()),
              queryMask(memory::pools::Streaming::GetInstance()), retentionMask(memory::pools::Streaming::GetInstance()),
              nearMask(memory::pools::Streaming::GetInstance()), secondaryMask(memory::pools::Streaming::GetInstance()),
              inRangeMask(memory::pools::Streaming::GetInstance()), lockedMask(memory::pools::Streaming::GetInstance()),
              antiStreamingLockedMask(memory::pools::Streaming::GetInstance()),
              toStreamInMask(memory::pools::Streaming::GetInstance()),
              toStreamOutMask(memory::pools::Streaming::GetInstance()),
              candidates(memory::pools::Streaming::GetInstance())
        {
            observerPositions.Reserve(MaximumStreamingObservers);
        }

        [[nodiscard]] LocalPosition ToLocal(const f64* const global) const noexcept
        {
            return {static_cast<f32>(global[0] - origin[0]), static_cast<f32>(global[1] - origin[1]),
                    static_cast<f32>(global[2] - origin[2])};
        }

        [[nodiscard]] u32 FindNodeIndex(const StreamingNodeKey key) const noexcept
        {
            u32 first = key.kind == StreamingNodeKind::Cell ? 0u : cellCount;
            u32 count = key.kind == StreamingNodeKind::Cell ? cellCount : nodes.Size() - cellCount;
            while (count != 0)
            {
                const u32 step = count / 2u;
                const u32 index = first + step;
                if (nodes[index].key.id < key.id) { first = index + 1u; count -= step + 1u; }
                else count = step;
            }
            return first < nodes.Size() && nodes[first].key == key ? first : 0xffffffffu;
        }

        [[nodiscard]] bool IsReplacementReady(const StreamingNodeKey key, const u32 depth = 0) const noexcept
        {
            if (depth > proxyCount) return false;
            const u32 nodeIndex = FindNodeIndex(key);
            if (nodeIndex == 0xffffffffu) return false;
            if (nodes[nodeIndex].renderReady) return true;
            if (key.kind != StreamingNodeKind::DistantProxy) return false;
            const DistantProxyRecord& proxy = world->DistantProxies()[nodes[nodeIndex].sourceIndex];
            if (IsFlagSet(proxy.flags, DistantProxyFlags::ProxyOnly)) return false;
            bool foundRequiredChild = false;
            for (const ProxyChildRecord& child : world->ChildrenOf(proxy))
            {
                if (!IsFlagSet(child.flags, ProxyChildFlags::RequiredForReplacement)) continue;
                foundRequiredChild = true;
                const StreamingNodeKey childKey{child.childId, child.kind == ProxyChildKind::Cell
                                                                   ? StreamingNodeKind::Cell
                                                                   : StreamingNodeKind::DistantProxy};
                if (!IsReplacementReady(childKey, depth + 1u)) return false;
            }
            return foundRequiredChild;
        }

        void RefreshAntiStreamingLocks() noexcept
        {
            antiStreamingLockedMask.ClearAll();
            for (u32 proxyIndex = 0; proxyIndex < proxyCount; ++proxyIndex)
            {
                const DistantProxyRecord& proxy = world->DistantProxies()[proxyIndex];
                if (IsFlagSet(proxy.flags, DistantProxyFlags::ProxyOnly)) continue;
                bool saturated = true;
                for (const ProxyChildRecord& child : world->ChildrenOf(proxy))
                {
                    if (!IsFlagSet(child.flags, ProxyChildFlags::RequiredForReplacement)) continue;
                    const StreamingNodeKey childKey{child.childId, child.kind == ProxyChildKind::Cell
                                                                       ? StreamingNodeKind::Cell
                                                                       : StreamingNodeKind::DistantProxy};
                    saturated &= IsReplacementReady(childKey);
                }
                if (!saturated) antiStreamingLockedMask.Set(cellCount + proxyIndex);
            }
        }

        StreamingGridConfig config;
        const WorldFile* world = nullptr;
        f64 origin[3]{};
        u32 cellCount = 0;
        u32 proxyCount = 0;
        containers::DynamicArray<RuntimeNode> nodes;
        containers::DynamicArray<LocalPosition> observerPositions;
        StreamingProxyQuery mainQuery;
        StreamingProxyQuery retentionQuery;
        StreamingProxyQuery nearQuery;
        StreamingProxyQuery secondaryQuery;
        containers::BitSet64Dynamic queryMask;
        containers::BitSet64Dynamic retentionMask;
        containers::BitSet64Dynamic nearMask;
        containers::BitSet64Dynamic secondaryMask;
        containers::BitSet64Dynamic inRangeMask;
        containers::BitSet64Dynamic lockedMask;
        containers::BitSet64Dynamic antiStreamingLockedMask;
        containers::BitSet64Dynamic toStreamInMask;
        containers::BitSet64Dynamic toStreamOutMask;
        containers::DynamicArray<SortEntry> candidates;
        bool shutdownRequested = false;
    };

    WorldStreamingGrid::~WorldStreamingGrid()
    {
        if (m_impl != nullptr)
        {
            DeleteWorldStreamingObject(m_impl);
            m_impl = nullptr;
        }
    }

    bool WorldStreamingGrid::Initialize(const WorldFile& worldFile, const StreamingGridConfig& config) noexcept
    {
        if (m_impl != nullptr) return true;
        if (!worldFile.IsOpen() || config.maximumStreamInsPerUpdate == 0 ||
            !std::isfinite(config.runtimeDistanceBoost) || config.runtimeDistanceBoost < 0.0f) return false;
        Impl* const impl = AllocateWorldStreamingObject<Impl>(config);
        if (impl == nullptr) return false;
        impl->world = &worldFile;
        impl->cellCount = worldFile.Cells().Size();
        impl->proxyCount = worldFile.DistantProxies().Size();
        for (u32 axis = 0; axis < 3; ++axis) impl->origin[axis] = worldFile.Origin()[axis];
        const u32 nodeCount = impl->cellCount + impl->proxyCount;
        impl->nodes.Reserve(nodeCount);
        impl->mainQuery.Reserve(nodeCount);
        impl->retentionQuery.Reserve(nodeCount);
        impl->nearQuery.Reserve(nodeCount);
        impl->secondaryQuery.Reserve(nodeCount);
        for (u32 index = 0; index < impl->cellCount; ++index)
        {
            const WorldCellRecord& cell = worldFile.Cells()[index];
            const f32 boost = IsFlagSet(cell.flags, WorldCellFlags::AllowDistanceBoosting) ? config.runtimeDistanceBoost : 0.0f;
            const LocalPosition position = impl->ToLocal(cell.streamingReferencePoint);
            if (!IsFinite(position))
            {
                DeleteWorldStreamingObject(impl);
                return false;
            }
            const bool threeDimensional = !IsFlagSet(cell.flags, WorldCellFlags::TwoDimensionalStreaming);
            impl->nodes.PushBack({{cell.cellId, StreamingNodeKind::Cell}, cell.cell, cell.streamingPriority,
                                  StreamingNodeState::Unloaded, position, index, true, false});
            if (!impl->mainQuery.PushBack(position, cell.activationDistance + boost, threeDimensional) ||
                !impl->retentionQuery.PushBack(position, cell.retentionDistance + boost, threeDimensional) ||
                !impl->nearQuery.PushBack({}, StreamingProxyQuery::InvalidRadius, true) ||
                !impl->secondaryQuery.PushBack({}, StreamingProxyQuery::InfiniteRadius, true))
            {
                DeleteWorldStreamingObject(impl);
                return false;
            }
        }
        for (u32 index = 0; index < impl->proxyCount; ++index)
        {
            const DistantProxyRecord& proxy = worldFile.DistantProxies()[index];
            const f32 boost = IsFlagSet(proxy.flags, DistantProxyFlags::AllowDistanceBoosting) ? config.runtimeDistanceBoost : 0.0f;
            const LocalPosition streamingPosition = impl->ToLocal(proxy.streamingReferencePoint);
            const LocalPosition preboostPosition = impl->ToLocal(proxy.preboostStreamingReferencePoint);
            const LocalPosition secondaryPosition = impl->ToLocal(proxy.secondaryReferencePoint);
            if (!IsFinite(streamingPosition) || !IsFinite(preboostPosition) || !IsFinite(secondaryPosition))
            {
                DeleteWorldStreamingObject(impl);
                return false;
            }
            const bool threeDimensional = !IsFlagSet(proxy.flags, DistantProxyFlags::TwoDimensionalStreaming);
            impl->nodes.PushBack({{proxy.proxyId, StreamingNodeKind::DistantProxy}, proxy.mesh, proxy.streamingPriority,
                                  StreamingNodeState::Unloaded, preboostPosition, index, true, false});
            if (!impl->mainQuery.PushBack(streamingPosition, proxy.streamingDistance + boost, threeDimensional) ||
                !impl->retentionQuery.PushBack(streamingPosition, proxy.streamingDistance + boost, threeDimensional) ||
                !impl->nearQuery.PushBack(preboostPosition, proxy.nearHideDistance + boost, threeDimensional) ||
                !impl->secondaryQuery.PushBack(secondaryPosition, proxy.secondaryReferencePointDistance + boost,
                                               threeDimensional))
            {
                DeleteWorldStreamingObject(impl);
                return false;
            }
        }
        if (impl->nodes.Size() != nodeCount)
        {
            DeleteWorldStreamingObject(impl);
            return false;
        }
        impl->queryMask.Resize(nodeCount);
        impl->retentionMask.Resize(nodeCount);
        impl->nearMask.Resize(nodeCount);
        impl->secondaryMask.Resize(nodeCount);
        impl->inRangeMask.Resize(nodeCount);
        impl->lockedMask.Resize(nodeCount);
        impl->antiStreamingLockedMask.Resize(nodeCount);
        impl->toStreamInMask.Resize(nodeCount);
        impl->toStreamOutMask.Resize(nodeCount);
        impl->RefreshAntiStreamingLocks();
        for (u32 index = 0; index < impl->cellCount; ++index)
            if (IsFlagSet(worldFile.Cells()[index].flags, WorldCellFlags::AlwaysLoaded)) impl->lockedMask.Set(index);
        for (u32 index = 0; index < impl->proxyCount; ++index)
            if (IsFlagSet(worldFile.DistantProxies()[index].flags, DistantProxyFlags::KeepResident))
                impl->lockedMask.Set(impl->cellCount + index);
        m_impl = impl;
        return true;
    }

    bool WorldStreamingGrid::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        for (const Impl::RuntimeNode& node : m_impl->nodes)
            if (node.state != StreamingNodeState::Unloaded) return false;
        DeleteWorldStreamingObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool WorldStreamingGrid::IsInitialized() const noexcept { return m_impl != nullptr; }

    bool WorldStreamingGrid::RequestShutdown() noexcept
    {
        if (m_impl == nullptr || m_impl->shutdownRequested) return false;
        m_impl->shutdownRequested = true;
        return true;
    }

    bool WorldStreamingGrid::IsShutdownRequested() const noexcept
    {
        return m_impl != nullptr && m_impl->shutdownRequested;
    }

    bool WorldStreamingGrid::Process(const StreamingProcessInput& input,
                                     containers::DynamicArray<StreamingCommand>& commands) noexcept
    {
        commands.Clear();
        if (m_impl == nullptr || input.observers.Empty() || input.observers.Size() > MaximumStreamingObservers ||
            !std::isfinite(input.globalDistanceScale) ||
            input.globalDistanceScale <= 0.0f) return false;
        for (const f64 coordinate : input.cameraPosition) if (!std::isfinite(coordinate)) return false;
        m_impl->observerPositions.Clear();
        for (const StreamingObserver& observer : input.observers)
        {
            for (const f64 coordinate : observer.predictedPosition) if (!std::isfinite(coordinate)) return false;
            const LocalPosition position = m_impl->ToLocal(observer.predictedPosition);
            if (!IsFinite(position)) return false;
            m_impl->observerPositions.PushBack(position);
        }
        if (m_impl->observerPositions.Size() != input.observers.Size()) return false;
        m_impl->queryMask.ClearAll();
        m_impl->retentionMask.ClearAll();
        m_impl->nearMask.ClearAll();
        m_impl->secondaryMask.ClearAll();
        m_impl->mainQuery.CollectMultiple(m_impl->observerPositions, m_impl->queryMask, input.globalDistanceScale);
        m_impl->retentionQuery.CollectMultiple(m_impl->observerPositions, m_impl->retentionMask,
                                               input.globalDistanceScale);
        for (u32 index = 0; index < m_impl->cellCount; ++index)
            if (m_impl->inRangeMask.Get(index) && m_impl->retentionMask.Get(index)) m_impl->queryMask.Set(index);
        const LocalPosition camera = m_impl->ToLocal(input.cameraPosition);
        if (!IsFinite(camera)) return false;
        m_impl->secondaryQuery.CollectSingle(camera, m_impl->secondaryMask, input.globalDistanceScale);
        m_impl->queryMask &= m_impl->secondaryMask;
        m_impl->nearQuery.CollectSingle(camera, m_impl->nearMask, input.globalDistanceScale);
        m_impl->RefreshAntiStreamingLocks();
        m_impl->nearMask -= m_impl->antiStreamingLockedMask;
        m_impl->queryMask -= m_impl->nearMask;
        m_impl->queryMask |= m_impl->lockedMask;
        if (m_impl->shutdownRequested) m_impl->queryMask.ClearAll();

        m_impl->toStreamInMask = m_impl->queryMask;
        m_impl->toStreamInMask -= m_impl->inRangeMask;
        m_impl->candidates.Clear();
        const LocalPosition primaryObserver = m_impl->observerPositions[0];
        for (u32 index = m_impl->toStreamInMask.FindNextSet(0); index < m_impl->toStreamInMask.Size();
             index = m_impl->toStreamInMask.FindNextSet(index + 1u))
        {
            const Impl::RuntimeNode& node = m_impl->nodes[index];
            if (!node.streamInAllowed || node.state != StreamingNodeState::Unloaded) continue;
            const f32 deltaX = node.preboostPosition.x - primaryObserver.x;
            const f32 deltaY = node.preboostPosition.y - primaryObserver.y;
            const f32 deltaZ = node.preboostPosition.z - primaryObserver.z;
            m_impl->candidates.PushBack({index, deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ,
                                         static_cast<u8>(node.priority)});
        }
        std::sort(m_impl->candidates.Begin(), m_impl->candidates.End(), [](const Impl::SortEntry& left,
                                                                          const Impl::SortEntry& right) noexcept
        {
            if (left.priority != right.priority) return left.priority > right.priority;
            if (left.distanceSquared != right.distanceSquared) return left.distanceSquared < right.distanceSquared;
            return left.nodeIndex < right.nodeIndex;
        });
        const u32 streamInCount = std::min(m_impl->config.maximumStreamInsPerUpdate, m_impl->candidates.Size());
        commands.Reserve(streamInCount + m_impl->inRangeMask.PopulationCount());
        for (u32 index = 0; index < streamInCount; ++index)
        {
            const Impl::SortEntry& candidate = m_impl->candidates[index];
            Impl::RuntimeNode& node = m_impl->nodes[candidate.nodeIndex];
            commands.PushBack({StreamingCommandType::StreamIn, node.key, node.resource, node.priority, candidate.distanceSquared});
            node.state = StreamingNodeState::StreamingIn;
            m_impl->inRangeMask.Set(candidate.nodeIndex);
        }

        m_impl->toStreamOutMask = m_impl->inRangeMask;
        m_impl->toStreamOutMask -= m_impl->queryMask;
        for (u32 index = m_impl->toStreamOutMask.FindNextSet(0); index < m_impl->toStreamOutMask.Size();
             index = m_impl->toStreamOutMask.FindNextSet(index + 1u))
        {
            Impl::RuntimeNode& node = m_impl->nodes[index];
            commands.PushBack({StreamingCommandType::StreamOut, node.key, node.resource, node.priority, 0.0f});
            node.state = StreamingNodeState::StreamingOut;
            node.renderReady = false;
            m_impl->inRangeMask.Clear(index);
        }
        return true;
    }

    bool WorldStreamingGrid::NotifyStreamInComplete(const StreamingNodeKey key, const bool success,
                                                    const bool renderReady) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu || m_impl->nodes[index].state != StreamingNodeState::StreamingIn) return false;
        m_impl->nodes[index].state = success ? StreamingNodeState::Streamed : StreamingNodeState::Failed;
        m_impl->nodes[index].renderReady = success && renderReady;
        return true;
    }

    bool WorldStreamingGrid::NotifyStreamOutComplete(const StreamingNodeKey key) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu || m_impl->nodes[index].state != StreamingNodeState::StreamingOut) return false;
        m_impl->nodes[index].state = StreamingNodeState::Unloaded;
        m_impl->nodes[index].renderReady = false;
        return true;
    }

    bool WorldStreamingGrid::NotifyResidentFailed(const StreamingNodeKey key) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu || m_impl->nodes[index].state != StreamingNodeState::Streamed) return false;
        m_impl->nodes[index].state = StreamingNodeState::Failed;
        m_impl->nodes[index].renderReady = false;
        return true;
    }

    bool WorldStreamingGrid::SetRenderReady(const StreamingNodeKey key, const bool ready) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu || (ready && m_impl->nodes[index].state != StreamingNodeState::Streamed)) return false;
        m_impl->nodes[index].renderReady = ready;
        return true;
    }

    bool WorldStreamingGrid::SetLocked(const StreamingNodeKey key, const bool locked) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu) return false;
        if (locked) m_impl->lockedMask.Set(index);
        else m_impl->lockedMask.Clear(index);
        return true;
    }

    bool WorldStreamingGrid::SetStreamInAllowed(const StreamingNodeKey key, const bool allowed) noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        if (index == 0xffffffffu) return false;
        m_impl->nodes[index].streamInAllowed = allowed;
        return true;
    }

    StreamingNodeState WorldStreamingGrid::State(const StreamingNodeKey key) const noexcept
    {
        if (m_impl == nullptr) return StreamingNodeState::Unloaded;
        const u32 index = m_impl->FindNodeIndex(key);
        return index == 0xffffffffu ? StreamingNodeState::Unloaded : m_impl->nodes[index].state;
    }

    bool WorldStreamingGrid::IsRenderReady(const StreamingNodeKey key) const noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex(key);
        return index != 0xffffffffu && m_impl->nodes[index].renderReady;
    }

    bool WorldStreamingGrid::IsAntiStreamingLocked(const u64 proxyId) const noexcept
    {
        if (m_impl == nullptr) return false;
        const u32 index = m_impl->FindNodeIndex({proxyId, StreamingNodeKind::DistantProxy});
        return index != 0xffffffffu && m_impl->antiStreamingLockedMask.Get(index);
    }

    StreamingGridStats WorldStreamingGrid::GetStats() const noexcept
    {
        StreamingGridStats stats;
        if (m_impl == nullptr) return stats;
        stats.registeredNodes = m_impl->nodes.Size();
        stats.desiredNodes = m_impl->queryMask.PopulationCount();
        stats.inRangeNodes = m_impl->inRangeMask.PopulationCount();
        stats.antiStreamingLockedProxies = m_impl->antiStreamingLockedMask.PopulationCount();
        for (const Impl::RuntimeNode& node : m_impl->nodes)
        {
            stats.streamingNodes += node.state == StreamingNodeState::StreamingIn ||
                                    node.state == StreamingNodeState::StreamingOut;
            stats.streamedNodes += node.state == StreamingNodeState::Streamed;
            stats.failedNodes += node.state == StreamingNodeState::Failed;
        }
        return stats;
    }
} // namespace vanguard::world
