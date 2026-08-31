#include <vanguard/rendering/render_scene_gpu.hpp>
#include <vanguard/rendering/render_scene_gpu_read.hpp>

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
        inline constexpr u32 TrackedPageShift = 12u;
        inline constexpr u32 TrackedPageSize = 1u << TrackedPageShift;
        inline constexpr u32 TrackedPageMask = TrackedPageSize - 1u;

        void ClearFailure(RenderSceneGpuFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneGpuFailure* const failure, const RenderSceneGpuFailureCode code, const char* const message,
                                const RenderSceneHandle scene = {}, const RenderProxyHandle proxy = {}) noexcept
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

        [[nodiscard]] bool HasFlag(const RenderProxyVisibilityFlags value, const RenderProxyVisibilityFlags flag) noexcept
        {
            return static_cast<u32>(value & flag) != 0;
        }

        void SplitWorldPosition(const f32 position[3], const f32 cellSize, i32 cell[3], f32 local[3]) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const f32 coordinate = std::floor(position[axis] / cellSize);
                cell[axis] = static_cast<i32>(coordinate);
                local[axis] = position[axis] - coordinate * cellSize;
            }
        }

        void BuildRotationAndScale(const RenderProxyTransform& transform, f32 rotation[4], f32* const scale) noexcept
        {
            f32 localScale[3]{};
            f32* const outputScale = scale != nullptr ? scale : localScale;
            f32 matrix[3][3]{};
            const f32* rows[3]{transform.row0, transform.row1, transform.row2};
            for (u32 row = 0; row < 3; ++row)
            {
                const f32 length = std::sqrt(rows[row][0] * rows[row][0] + rows[row][1] * rows[row][1] + rows[row][2] * rows[row][2]);
                outputScale[row] = length > 1.0e-8f ? length : 0.0f;
                const f32 inverse = length > 1.0e-8f ? 1.0f / length : 0.0f;
                matrix[row][0] = rows[row][0] * inverse;
                matrix[row][1] = rows[row][1] * inverse;
                matrix[row][2] = rows[row][2] * inverse;
            }

            const f32 determinant = matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
                                    matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
                                    matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
            if (determinant < 0.0f)
            {
                u32 axis = outputScale[1] > outputScale[0] ? 1u : 0u;
                axis = outputScale[2] > outputScale[axis] ? 2u : axis;
                outputScale[axis] = -outputScale[axis];
                matrix[axis][0] = -matrix[axis][0];
                matrix[axis][1] = -matrix[axis][1];
                matrix[axis][2] = -matrix[axis][2];
            }

            const f32 trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
            if (trace > 0.0f)
            {
                const f32 root = std::sqrt(trace + 1.0f) * 2.0f;
                rotation[3] = 0.25f * root;
                rotation[0] = (matrix[2][1] - matrix[1][2]) / root;
                rotation[1] = (matrix[0][2] - matrix[2][0]) / root;
                rotation[2] = (matrix[1][0] - matrix[0][1]) / root;
            }
            else
            {
                const u32 axis = matrix[1][1] > matrix[0][0] ? (matrix[2][2] > matrix[1][1] ? 2u : 1u) : (matrix[2][2] > matrix[0][0] ? 2u : 0u);
                const u32 next = (axis + 1u) % 3u;
                const u32 last = (axis + 2u) % 3u;
                const f32 root = std::sqrt(1.0f + matrix[axis][axis] - matrix[next][next] - matrix[last][last]) * 2.0f;
                rotation[axis] = 0.25f * root;
                rotation[3] = (matrix[last][next] - matrix[next][last]) / root;
                rotation[next] = (matrix[next][axis] + matrix[axis][next]) / root;
                rotation[last] = (matrix[last][axis] + matrix[axis][last]) / root;
            }
        }

        void AddInstanceFlag(GpuInstanceFlags& value, const GpuInstanceFlags flag) noexcept
        {
            value = static_cast<GpuInstanceFlags>(static_cast<u32>(value) | static_cast<u32>(flag));
        }
    } // namespace

    struct RenderSceneGpuPublisher::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct TrackedProxy
        {
            RenderProxyHandle proxy;
            RenderSceneGpuIdentity identity;
            RenderSceneGpuMeshBinding meshBinding;
            GpuMaterialHandle decalMaterial;
            RenderSceneGpuDirtyFlags dirty = RenderSceneGpuDirtyFlags::None;
            u64 queuedEpoch = 0;
            bool queuedActive = false;
            bool tracked = false;
        };

        struct TrackedPage
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            TrackedProxy slots[TrackedPageSize]{};
        };

        struct DirtySlice
        {
            u32 offset = 0;
            u32 capacity = 0;
            u32 count = 0;
            u32 coalesced = 0;
            u32 publishedOffset = 0;
            u32 publishedCount = 0;
            bool written = false;
        };

        enum class MutationPhase : u8
        {
            Serial,
            Parallel,
            Sealed,
            Publishing
        };

        struct SceneState
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            SceneState() noexcept
                : trackedPages(memory::pools::Rendering::GetInstance()), serialDirtyIndices(memory::pools::Rendering::GetInstance()),
                  serialRangeOffsets(memory::pools::Rendering::GetInstance()), serialRangeWritten(memory::pools::Rendering::GetInstance()),
                  parallelSlices(memory::pools::Rendering::GetInstance()), retirements(memory::pools::Rendering::GetInstance())
            {
            }

            ~SceneState()
            {
                for (u32 index = 0; index < trackedPages.Size(); ++index)
                    if (trackedPages[index] != nullptr)
                        VANGUARD_DELETE(trackedPages[index]);
                memory::Free(parallelDirtyStorage);
            }

            RenderSceneHandle scene;
            mutable concurrency::RWSpinLock lock;
            containers::DynamicArray<TrackedPage*> trackedPages;
            containers::DynamicArray<u32> serialDirtyIndices;
            containers::DynamicArray<u32> serialRangeOffsets;
            containers::DynamicArray<u8> serialRangeWritten;
            containers::DynamicArray<DirtySlice> parallelSlices;
            containers::DynamicArray<RenderSceneGpuRetirement> retirements;
            memory::MemoryBlock parallelDirtyStorage;
            u32* parallelDirtyIndices = nullptr;
            u32 parallelDirtyCapacity = 0;
            u32 publishedChangeCount = 0;
            u64 dirtyEpoch = 1;
            u64 nextParallelSerial = 1;
            u64 parallelSerial = 0;
            u64 publicationSerial = 0;
            MutationPhase phase = MutationPhase::Serial;
            bool written = false;
            RenderSceneGpuStats stats;
        };

        RenderSceneManager* scenes = nullptr;
        GpuSceneLifetime* lifetime = nullptr;
        RenderSceneGpuConfig config;
        SceneState* sceneStates[MaximumRenderScenes]{};
        concurrency::Atomic<u64> rejectedOperations{0};
        u64 nextPublicationSerial = 1;

        [[nodiscard]] SceneState* FindScene(const RenderSceneHandle scene) noexcept
        {
            SceneState* const state = scene.index < MaximumRenderScenes ? sceneStates[scene.index] : nullptr;
            return state != nullptr && state->scene == scene ? state : nullptr;
        }

        [[nodiscard]] const SceneState* FindScene(const RenderSceneHandle scene) const noexcept
        {
            const SceneState* const state = scene.index < MaximumRenderScenes ? sceneStates[scene.index] : nullptr;
            return state != nullptr && state->scene == scene ? state : nullptr;
        }

        [[nodiscard]] TrackedProxy* FindTracked(SceneState& scene, const RenderProxyHandle proxy) noexcept
        {
            const u32 pageIndex = proxy.index >> TrackedPageShift;
            if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                return nullptr;
            TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxy.index & TrackedPageMask];
            return tracked.tracked && tracked.proxy == proxy ? &tracked : nullptr;
        }

        [[nodiscard]] const TrackedProxy* FindTracked(const SceneState& scene, const RenderProxyHandle proxy) const noexcept
        {
            const u32 pageIndex = proxy.index >> TrackedPageShift;
            if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                return nullptr;
            const TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxy.index & TrackedPageMask];
            return tracked.tracked && tracked.proxy == proxy ? &tracked : nullptr;
        }

        [[nodiscard]] TrackedProxy* MaterializeTracked(SceneState& scene, const RenderProxyHandle proxy) noexcept
        {
            const u32 pageIndex = proxy.index >> TrackedPageShift;
            if (pageIndex >= scene.trackedPages.Size())
                return nullptr;
            if (scene.trackedPages[pageIndex] == nullptr)
            {
                scene.trackedPages[pageIndex] = VANGUARD_NEW(TrackedPage);
                if (scene.trackedPages[pageIndex] == nullptr)
                    return nullptr;
            }
            return &scene.trackedPages[pageIndex]->slots[proxy.index & TrackedPageMask];
        }

        [[nodiscard]] bool QueueDirty(SceneState& scene, TrackedProxy& tracked, const RenderSceneGpuDirtyFlags dirty) noexcept
        {
            tracked.dirty = tracked.dirty | dirty;
            if (tracked.queuedEpoch == scene.dirtyEpoch)
            {
                if (!tracked.queuedActive)
                {
                    tracked.queuedActive = true;
                    ++scene.stats.pendingChanges;
                    return true;
                }
                ++scene.stats.coalescedChanges;
                return true;
            }
            tracked.queuedEpoch = scene.dirtyEpoch;
            tracked.queuedActive = true;
            scene.serialDirtyIndices.PushBack(tracked.proxy.index);
            ++scene.stats.pendingChanges;
            return true;
        }

        [[nodiscard]] bool QueueParallelDirty(SceneState& scene, const RenderSceneGpuDirtyBatch& batch, const u32 group, const RenderProxyHandle proxy,
                                              const RenderSceneGpuDirtyFlags dirty) noexcept
        {
            if (scene.phase != MutationPhase::Parallel || scene.parallelSerial != batch.serial || group >= scene.parallelSlices.Size())
                return false;
            TrackedProxy* const tracked = FindTracked(scene, proxy);
            if (tracked == nullptr)
                return false;
            tracked->dirty = tracked->dirty | dirty;
            DirtySlice& slice = scene.parallelSlices[group];
            if (tracked->queuedEpoch == scene.dirtyEpoch)
            {
                if (!tracked->queuedActive)
                    return false;
                ++slice.coalesced;
                return true;
            }
            if (slice.count >= slice.capacity)
                return false;
            tracked->queuedEpoch = scene.dirtyEpoch;
            tracked->queuedActive = true;
            scene.parallelDirtyIndices[slice.offset + slice.count++] = proxy.index;
            return true;
        }

        template <typename Visitor> bool VisitPublished(SceneState& scene, Visitor&& visitor) noexcept
        {
            u32 publicationIndex = 0;
            for (u32 index = 0; index < scene.serialDirtyIndices.Size(); ++index)
            {
                const u32 proxyIndex = scene.serialDirtyIndices[index];
                const u32 pageIndex = proxyIndex >> TrackedPageShift;
                if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                    continue;
                TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                    !visitor(publicationIndex++, tracked))
                    return false;
            }
            for (u32 sliceIndex = 0; sliceIndex < scene.parallelSlices.Size(); ++sliceIndex)
            {
                const DirtySlice& slice = scene.parallelSlices[sliceIndex];
                for (u32 index = 0; index < slice.count; ++index)
                {
                    const u32 proxyIndex = scene.parallelDirtyIndices[slice.offset + index];
                    const u32 pageIndex = proxyIndex >> TrackedPageShift;
                    if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                        continue;
                    TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                    if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                        !visitor(publicationIndex++, tracked))
                        return false;
                }
            }
            return true;
        }

        template <typename Visitor> bool VisitPublished(const SceneState& scene, Visitor&& visitor) const noexcept
        {
            u32 publicationIndex = 0;
            for (u32 index = 0; index < scene.serialDirtyIndices.Size(); ++index)
            {
                const u32 proxyIndex = scene.serialDirtyIndices[index];
                const u32 pageIndex = proxyIndex >> TrackedPageShift;
                if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                    continue;
                const TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                    !visitor(publicationIndex++, tracked))
                    return false;
            }
            for (u32 sliceIndex = 0; sliceIndex < scene.parallelSlices.Size(); ++sliceIndex)
            {
                const DirtySlice& slice = scene.parallelSlices[sliceIndex];
                for (u32 index = 0; index < slice.count; ++index)
                {
                    const u32 proxyIndex = scene.parallelDirtyIndices[slice.offset + index];
                    const u32 pageIndex = proxyIndex >> TrackedPageShift;
                    if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                        continue;
                    const TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                    if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                        !visitor(publicationIndex++, tracked))
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] u32 CountPublishedSerial(const SceneState& scene, const u32 first, const u32 count) const noexcept
        {
            u32 liveCount = 0;
            const u32 available = first < scene.serialDirtyIndices.Size() ? scene.serialDirtyIndices.Size() - first : 0;
            const u32 end = first + (count < available ? count : available);
            for (u32 index = first; index < end; ++index)
            {
                const u32 proxyIndex = scene.serialDirtyIndices[index];
                const u32 pageIndex = proxyIndex >> TrackedPageShift;
                if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                    continue;
                const TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None)
                    ++liveCount;
            }
            return liveCount;
        }

        [[nodiscard]] bool DescribeWriteRange(const SceneState& scene, const u32 rangeIndex, RenderSceneGpuWriteRange& range) const noexcept
        {
            range = {};
            const u32 serialRangeCount = scene.serialRangeWritten.Size();
            if (rangeIndex >= serialRangeCount + scene.parallelSlices.Size())
                return false;
            range.index = rangeIndex;
            if (rangeIndex < serialRangeCount)
            {
                range.firstReservation = scene.serialRangeOffsets[rangeIndex];
                range.objectCount = scene.serialRangeOffsets[rangeIndex + 1u] - range.firstReservation;
                return true;
            }
            const DirtySlice& slice = scene.parallelSlices[rangeIndex - serialRangeCount];
            range.firstReservation = slice.publishedOffset;
            range.objectCount = slice.publishedCount;
            return true;
        }

        template <typename Visitor> bool VisitWriteRange(SceneState& scene, const u32 rangeIndex, Visitor&& visitor) noexcept
        {
            const u32 serialRangeCount = scene.serialRangeWritten.Size();
            if (rangeIndex >= serialRangeCount + scene.parallelSlices.Size())
                return false;
            if (rangeIndex < serialRangeCount)
            {
                const u32 first = rangeIndex * config.maximumObjectsPerWriteRange;
                const u32 available = first < scene.serialDirtyIndices.Size() ? scene.serialDirtyIndices.Size() - first : 0;
                const u32 count = config.maximumObjectsPerWriteRange < available ? config.maximumObjectsPerWriteRange : available;
                const u32 end = first + count;
                u32 reservationIndex = scene.serialRangeOffsets[rangeIndex];
                for (u32 index = first; index < end; ++index)
                {
                    const u32 proxyIndex = scene.serialDirtyIndices[index];
                    const u32 pageIndex = proxyIndex >> TrackedPageShift;
                    if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                        continue;
                    TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                    if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                        !visitor(reservationIndex++, tracked))
                        return false;
                }
                return reservationIndex == scene.serialRangeOffsets[rangeIndex + 1u];
            }

            DirtySlice& slice = scene.parallelSlices[rangeIndex - serialRangeCount];
            u32 reservationIndex = slice.publishedOffset;
            for (u32 index = 0; index < slice.count; ++index)
            {
                const u32 proxyIndex = scene.parallelDirtyIndices[slice.offset + index];
                const u32 pageIndex = proxyIndex >> TrackedPageShift;
                if (pageIndex >= scene.trackedPages.Size() || scene.trackedPages[pageIndex] == nullptr)
                    continue;
                TrackedProxy& tracked = scene.trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                if (tracked.tracked && tracked.queuedEpoch == scene.dirtyEpoch && tracked.dirty != RenderSceneGpuDirtyFlags::None &&
                    !visitor(reservationIndex++, tracked))
                    return false;
            }
            return reservationIndex == slice.publishedOffset + slice.publishedCount;
        }

        void CompletePublished(SceneState& scene) noexcept
        {
            static_cast<void>(VisitPublished(scene,
                                             [](const u32, TrackedProxy& tracked) noexcept
                                             {
                                                 tracked.dirty = RenderSceneGpuDirtyFlags::None;
                                                 tracked.queuedEpoch = 0;
                                                 tracked.queuedActive = false;
                                                 return true;
                                             }));
            scene.serialDirtyIndices.Clear();
            scene.serialRangeOffsets.Clear();
            scene.serialRangeWritten.Clear();
            for (u32 index = 0; index < scene.parallelSlices.Size(); ++index)
                scene.parallelSlices[index] = {};
            scene.retirements.Clear();
            scene.publishedChangeCount = 0;
            scene.publicationSerial = 0;
            scene.parallelSerial = 0;
            ++scene.dirtyEpoch;
            if (scene.dirtyEpoch == 0)
                scene.dirtyEpoch = 1;
            scene.phase = MutationPhase::Serial;
            scene.written = false;
        }
    };

    RenderSceneGpuPublisher::~RenderSceneGpuPublisher()
    {
        if (m_impl != nullptr)
        {
            static_cast<void>(Shutdown());
            if (m_impl != nullptr)
                VANGUARD_DELETE(m_impl);
        }
    }

    bool RenderSceneGpuPublisher::Initialize(RenderSceneManager& scenes, GpuSceneLifetime& lifetime, const RenderSceneGpuConfig& config,
                                             RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::AlreadyInitialized, "RenderScene GPU publisher is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneGpuFailureCode::WrongThread, "RenderScene GPU publisher initialization must run on the main thread");
        if (!scenes.IsInitialized() || !lifetime.IsInitialized() || !std::isfinite(config.worldCellSize) || config.worldCellSize <= 0.0f ||
            config.maximumObjectsPerWriteRange < 64u || config.maximumObjectsPerWriteRange > 65'536u)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidConfiguration,
                        "RenderScene GPU publisher requires initialized owners, a positive world-cell size, and bounded write ranges");

        m_impl = VANGUARD_NEW(Impl);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publisher allocation failed");
        m_impl->scenes = &scenes;
        m_impl->lifetime = &lifetime;
        m_impl->config = config;
        if (!scenes.AttachGpuPublisher(*this))
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publisher must attach before any RenderScene is alive");
        }
        return true;
    }

    bool RenderSceneGpuPublisher::Shutdown(RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneGpuFailureCode::WrongThread, "RenderScene GPU publisher shutdown must run on the main thread");
        for (u32 index = 0; index < MaximumRenderScenes; ++index)
            if (m_impl->sceneStates[index] != nullptr)
                return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publisher shutdown requires every RenderScene to be destroyed");
        if (!m_impl->scenes->DetachGpuPublisher(*this))
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publisher detachment failed");
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool RenderSceneGpuPublisher::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool RenderSceneGpuPublisher::AttachScene(const RenderSceneHandle scene, const u32 maximumProxies, const u32 maximumParallelChanges) noexcept
    {
        if (m_impl == nullptr || !scene.IsValid() || scene.index >= MaximumRenderScenes || maximumProxies == 0 || maximumParallelChanges == 0 ||
            m_impl->sceneStates[scene.index] != nullptr)
            return false;
        Impl::SceneState* const state = VANGUARD_NEW(Impl::SceneState);
        if (state == nullptr)
            return false;
        state->scene = scene;
        const u32 initialCapacity = maximumProxies < 4096u ? maximumProxies : 4096u;
        state->trackedPages.Resize((maximumProxies + TrackedPageMask) >> TrackedPageShift);
        for (u32 index = 0; index < state->trackedPages.Size(); ++index)
            state->trackedPages[index] = nullptr;
        state->serialDirtyIndices.Reserve(initialCapacity);
        const u32 maximumSerialRanges = (maximumProxies + m_impl->config.maximumObjectsPerWriteRange - 1u) / m_impl->config.maximumObjectsPerWriteRange;
        state->serialRangeOffsets.Reserve(maximumSerialRanges + 1u);
        state->serialRangeWritten.Reserve(maximumSerialRanges);
        state->retirements.Reserve(initialCapacity);
        state->parallelDirtyStorage = memory::Allocate(memory::PoolId::Rendering, static_cast<usize>(maximumParallelChanges) * sizeof(u32), alignof(u32));
        if (!state->parallelDirtyStorage)
        {
            VANGUARD_DELETE(state);
            return false;
        }
        state->parallelDirtyIndices = static_cast<u32*>(state->parallelDirtyStorage.address);
        state->parallelDirtyCapacity = maximumParallelChanges;
        m_impl->sceneStates[scene.index] = state;
        return true;
    }

    bool RenderSceneGpuPublisher::DetachScene(const RenderSceneHandle scene) noexcept
    {
        if (m_impl == nullptr)
            return false;
        Impl::SceneState* const state = m_impl->FindScene(scene);
        if (state == nullptr || state->phase != Impl::MutationPhase::Serial || state->serialDirtyIndices.Size() != 0 || state->retirements.Size() != 0)
            return false;
        for (u32 pageIndex = 0; pageIndex < state->trackedPages.Size(); ++pageIndex)
            if (state->trackedPages[pageIndex] != nullptr)
                for (u32 slotIndex = 0; slotIndex < TrackedPageSize; ++slotIndex)
                    if (state->trackedPages[pageIndex]->slots[slotIndex].tracked)
                        return false;
        VANGUARD_DELETE(state);
        m_impl->sceneStates[scene.index] = nullptr;
        return true;
    }

    bool RenderSceneGpuPublisher::ReserveIdentity(const RenderSceneGpuObjectKind kind, RenderSceneGpuIdentity& identity,
                                                  RenderSceneGpuFailure* const failure) noexcept
    {
        identity = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::NotInitialized, "RenderScene GPU publisher is not initialized");
        GpuSceneTableKind table = GpuSceneTableKind::Instance;
        if (kind == RenderSceneGpuObjectKind::Light)
            table = GpuSceneTableKind::Light;
        else if (kind == RenderSceneGpuObjectKind::Decal)
            table = GpuSceneTableKind::Decal;
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_impl->lifetime->Allocate(table, 1, identity.allocation, &lifetimeFailure))
        {
            if (failure != nullptr)
            {
                failure->code = RenderSceneGpuFailureCode::LifetimeFailure;
                failure->message = "persistent GPU Scene identity allocation failed";
                failure->lifetimeFailure = lifetimeFailure;
            }
            return false;
        }
        identity.kind = kind;
        return true;
    }

    void RenderSceneGpuPublisher::CancelIdentity(const RenderSceneGpuIdentity identity) noexcept
    {
        if (m_impl != nullptr && identity.IsValid())
            static_cast<void>(m_impl->lifetime->Cancel(identity.allocation));
    }

    bool RenderSceneGpuPublisher::TrackProxy(const RenderProxyHandle proxy, const RenderSceneGpuIdentity identity) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(proxy.scene) : nullptr;
        if (scene == nullptr || !identity.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Serial)
            return false;
        Impl::TrackedProxy* const tracked = m_impl->MaterializeTracked(*scene, proxy);
        if (tracked == nullptr || tracked->tracked)
            return false;
        const u64 queuedEpoch = tracked->queuedEpoch;
        const bool queuedActive = tracked->queuedActive;
        *tracked = {};
        tracked->proxy = proxy;
        tracked->identity = identity;
        tracked->queuedEpoch = queuedEpoch;
        tracked->queuedActive = queuedActive;
        tracked->tracked = true;
        if (identity.kind == RenderSceneGpuObjectKind::Instance)
        {
            if (!m_impl->scenes->SetGpuInstanceIndex(proxy, identity.allocation.first))
            {
                *tracked = {};
                tracked->queuedEpoch = queuedEpoch;
                tracked->queuedActive = queuedActive;
                return false;
            }
            ++scene->stats.trackedInstances;
        }
        else if (identity.kind == RenderSceneGpuObjectKind::Light)
            ++scene->stats.trackedLights;
        else
            ++scene->stats.trackedDecals;
        if (m_impl->QueueDirty(*scene, *tracked,
                               RenderSceneGpuDirtyFlags::Initial | RenderSceneGpuDirtyFlags::Transform | RenderSceneGpuDirtyFlags::Visibility |
                                   RenderSceneGpuDirtyFlags::Resources | RenderSceneGpuDirtyFlags::Properties))
            return true;

        if (identity.kind == RenderSceneGpuObjectKind::Instance)
        {
            m_impl->scenes->ClearGpuInstanceIndex(proxy);
            --scene->stats.trackedInstances;
        }
        else if (identity.kind == RenderSceneGpuObjectKind::Light)
            --scene->stats.trackedLights;
        else
            --scene->stats.trackedDecals;
        *tracked = {};
        tracked->queuedEpoch = queuedEpoch;
        tracked->queuedActive = queuedActive;
        return false;
    }

    bool RenderSceneGpuPublisher::MarkProxyDirty(const RenderProxyHandle proxy, const RenderSceneGpuDirtyFlags dirty) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(proxy.scene) : nullptr;
        if (scene == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        return scene->phase == Impl::MutationPhase::Serial && tracked != nullptr && m_impl->QueueDirty(*scene, *tracked, dirty);
    }

    bool RenderSceneGpuPublisher::BeginParallelDirty(const RenderSceneHandle sceneHandle, const u32 groupCount, const u32 entryCount,
                                                     RenderSceneGpuDirtyBatch& batch) noexcept
    {
        batch = {};
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(sceneHandle) : nullptr;
        if (scene == nullptr || groupCount == 0 || entryCount == 0 || entryCount > scene->parallelDirtyCapacity)
            return false;

        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Serial)
            return false;
        const u32 entriesPerGroup = (entryCount + groupCount - 1u) / groupCount;
        scene->parallelSlices.Resize(groupCount);
        for (u32 group = 0; group < groupCount; ++group)
        {
            const u32 offset = group * entriesPerGroup;
            const u32 remaining = offset < entryCount ? entryCount - offset : 0;
            const u32 capacity = remaining < entriesPerGroup ? remaining : entriesPerGroup;
            scene->parallelSlices[group] = {offset, capacity, 0, 0};
        }
        scene->parallelSerial = scene->nextParallelSerial++;
        if (scene->parallelSerial == 0)
            scene->parallelSerial = scene->nextParallelSerial++;
        scene->phase = Impl::MutationPhase::Parallel;
        batch = {sceneHandle, scene->parallelSerial, groupCount, entriesPerGroup};
        return true;
    }

    bool RenderSceneGpuPublisher::MarkProxyDirty(const RenderSceneGpuDirtyBatch& batch, const u32 group, const RenderProxyHandle proxy,
                                                 const RenderSceneGpuDirtyFlags dirty) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(batch.scene) : nullptr;
        return scene != nullptr && proxy.scene == batch.scene && group < batch.groupCount && m_impl->QueueParallelDirty(*scene, batch, group, proxy, dirty);
    }

    bool RenderSceneGpuPublisher::EndParallelDirty(const RenderSceneGpuDirtyBatch& batch) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(batch.scene) : nullptr;
        if (scene == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Parallel || scene->parallelSerial != batch.serial)
            return false;
        for (u32 index = 0; index < scene->parallelSlices.Size(); ++index)
        {
            scene->stats.pendingChanges += scene->parallelSlices[index].count;
            scene->stats.coalescedChanges += scene->parallelSlices[index].coalesced;
        }
        scene->phase = Impl::MutationPhase::Sealed;
        return true;
    }

    void RenderSceneGpuPublisher::CancelParallelDirty(const RenderSceneGpuDirtyBatch& batch) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(batch.scene) : nullptr;
        if (scene == nullptr)
            return;
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Parallel || scene->parallelSerial != batch.serial)
            return;
        for (u32 sliceIndex = 0; sliceIndex < scene->parallelSlices.Size(); ++sliceIndex)
        {
            const Impl::DirtySlice& slice = scene->parallelSlices[sliceIndex];
            for (u32 index = 0; index < slice.count; ++index)
            {
                const u32 proxyIndex = scene->parallelDirtyIndices[slice.offset + index];
                const u32 pageIndex = proxyIndex >> TrackedPageShift;
                if (pageIndex >= scene->trackedPages.Size() || scene->trackedPages[pageIndex] == nullptr)
                    continue;
                Impl::TrackedProxy& tracked = scene->trackedPages[pageIndex]->slots[proxyIndex & TrackedPageMask];
                tracked.dirty = RenderSceneGpuDirtyFlags::None;
                tracked.queuedEpoch = 0;
                tracked.queuedActive = false;
            }
            scene->parallelSlices[sliceIndex] = {};
        }
        scene->parallelSerial = 0;
        scene->phase = Impl::MutationPhase::Serial;
    }

    bool RenderSceneGpuPublisher::RetireProxy(const RenderProxyHandle proxy) noexcept
    {
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(proxy.scene) : nullptr;
        if (scene == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (scene->phase != Impl::MutationPhase::Serial || tracked == nullptr)
            return false;
        scene->retirements.PushBack({proxy, tracked->identity});
        ++scene->stats.pendingRetirements;
        if (tracked->queuedEpoch == scene->dirtyEpoch && tracked->queuedActive)
        {
            tracked->dirty = RenderSceneGpuDirtyFlags::None;
            tracked->queuedActive = false;
            --scene->stats.pendingChanges;
        }
        if (tracked->identity.kind == RenderSceneGpuObjectKind::Instance)
        {
            m_impl->scenes->ClearGpuInstanceIndex(proxy);
            --scene->stats.trackedInstances;
        }
        else if (tracked->identity.kind == RenderSceneGpuObjectKind::Light)
            --scene->stats.trackedLights;
        else
            --scene->stats.trackedDecals;
        const u64 queuedEpoch = tracked->queuedEpoch;
        const bool queuedActive = tracked->queuedActive;
        *tracked = {};
        tracked->queuedEpoch = queuedEpoch;
        tracked->queuedActive = queuedActive;
        return true;
    }

    bool RenderSceneGpuPublisher::AllowsMutation(const RenderSceneHandle scene) const noexcept
    {
        if (m_impl == nullptr)
            return true;
        const Impl::SceneState* const state = m_impl->FindScene(scene);
        if (state == nullptr)
            return false;
        concurrency::ScopedSharedLock<concurrency::RWSpinLock> guard(state->lock);
        return state->phase == Impl::MutationPhase::Serial;
    }

    bool RenderSceneGpuPublisher::BindMesh(const RenderProxyHandle proxy, const RenderSceneGpuMeshBinding& binding,
                                           RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::NotInitialized, "RenderScene GPU publisher is not initialized", proxy.scene, proxy);
        if (!binding.renderable.IsValid())
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "mesh GPU binding requires a valid renderable", proxy.scene, proxy);
        Impl::SceneState* const scene = m_impl->FindScene(proxy.scene);
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU scene", proxy.scene, proxy);
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (scene->phase != Impl::MutationPhase::Serial || tracked == nullptr || tracked->identity.kind != RenderSceneGpuObjectKind::Instance)
            return Fail(failure, scene->phase != Impl::MutationPhase::Serial ? RenderSceneGpuFailureCode::Busy : RenderSceneGpuFailureCode::InvalidHandle,
                        "mesh GPU binding requires a tracked instance outside an open publication", proxy.scene, proxy);
        tracked->meshBinding = binding;
        return m_impl->QueueDirty(*scene, *tracked, RenderSceneGpuDirtyFlags::Resources);
    }

    bool RenderSceneGpuPublisher::ClearMeshBinding(const RenderProxyHandle proxy, RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::NotInitialized, "RenderScene GPU publisher is not initialized", proxy.scene, proxy);
        Impl::SceneState* const scene = m_impl->FindScene(proxy.scene);
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU scene", proxy.scene, proxy);
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (scene->phase != Impl::MutationPhase::Serial || tracked == nullptr || tracked->identity.kind != RenderSceneGpuObjectKind::Instance)
            return Fail(failure, scene->phase != Impl::MutationPhase::Serial ? RenderSceneGpuFailureCode::Busy : RenderSceneGpuFailureCode::InvalidHandle,
                        "mesh GPU binding clear requires a tracked instance outside an open publication", proxy.scene, proxy);
        tracked->meshBinding = {};
        return m_impl->QueueDirty(*scene, *tracked, RenderSceneGpuDirtyFlags::Resources);
    }

    bool RenderSceneGpuPublisher::BindDecalMaterial(const RenderProxyHandle proxy, const GpuMaterialHandle material,
                                                    RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !material.IsValid())
            return Fail(failure, m_impl == nullptr ? RenderSceneGpuFailureCode::NotInitialized : RenderSceneGpuFailureCode::InvalidHandle,
                        "decal GPU binding requires an initialized publisher and valid material", proxy.scene, proxy);
        Impl::SceneState* const scene = m_impl->FindScene(proxy.scene);
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU scene", proxy.scene, proxy);
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (scene->phase != Impl::MutationPhase::Serial || tracked == nullptr || tracked->identity.kind != RenderSceneGpuObjectKind::Decal)
            return Fail(failure, scene->phase != Impl::MutationPhase::Serial ? RenderSceneGpuFailureCode::Busy : RenderSceneGpuFailureCode::InvalidHandle,
                        "decal GPU binding requires a tracked decal outside an open publication", proxy.scene, proxy);
        tracked->decalMaterial = material;
        return m_impl->QueueDirty(*scene, *tracked, RenderSceneGpuDirtyFlags::Resources);
    }

    bool RenderSceneGpuPublisher::ClearDecalMaterialBinding(const RenderProxyHandle proxy, RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::NotInitialized, "RenderScene GPU publisher is not initialized", proxy.scene, proxy);
        Impl::SceneState* const scene = m_impl->FindScene(proxy.scene);
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU scene", proxy.scene, proxy);
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (scene->phase != Impl::MutationPhase::Serial || tracked == nullptr || tracked->identity.kind != RenderSceneGpuObjectKind::Decal)
            return Fail(failure, scene->phase != Impl::MutationPhase::Serial ? RenderSceneGpuFailureCode::Busy : RenderSceneGpuFailureCode::InvalidHandle,
                        "decal GPU binding clear requires a tracked decal outside an open publication", proxy.scene, proxy);
        tracked->decalMaterial = {};
        return m_impl->QueueDirty(*scene, *tracked, RenderSceneGpuDirtyFlags::Resources);
    }

    bool RenderSceneGpuPublisher::GetIdentity(const RenderProxyHandle proxy, RenderSceneGpuIdentity& identity) const noexcept
    {
        identity = {};
        const Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(proxy.scene) : nullptr;
        if (scene == nullptr)
            return false;
        concurrency::ScopedSharedLock<concurrency::RWSpinLock> guard(scene->lock);
        const Impl::TrackedProxy* const tracked = m_impl->FindTracked(*scene, proxy);
        if (tracked == nullptr)
            return false;
        identity = tracked->identity;
        return true;
    }

    bool RenderSceneGpuPublisher::Prepare(const RenderSceneHandle sceneHandle, const u64 mutationEpoch, RenderSceneGpuPublication& publication,
                                          RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        publication = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::NotInitialized, "RenderScene GPU publisher is not initialized", sceneHandle);
        if (!m_impl->scenes->IsGpuPublicationReady(sceneHandle, mutationEpoch))
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publication requires the exact completed scene-update epoch",
                        sceneHandle);
        Impl::SceneState* const scene = m_impl->FindScene(sceneHandle);
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU scene", sceneHandle);

        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Serial && scene->phase != Impl::MutationPhase::Sealed)
            return Fail(failure, RenderSceneGpuFailureCode::Busy, "RenderScene GPU mutations are not sealed for publication", sceneHandle);
        const u32 serialRangeCount =
            (scene->serialDirtyIndices.Size() + m_impl->config.maximumObjectsPerWriteRange - 1u) / m_impl->config.maximumObjectsPerWriteRange;
        scene->serialRangeOffsets.Resize(serialRangeCount + 1u);
        scene->serialRangeWritten.Resize(serialRangeCount);
        u32 publishedOffset = 0;
        for (u32 index = 0; index < serialRangeCount; ++index)
        {
            scene->serialRangeOffsets[index] = publishedOffset;
            scene->serialRangeWritten[index] = 0;
            publishedOffset +=
                m_impl->CountPublishedSerial(*scene, index * m_impl->config.maximumObjectsPerWriteRange, m_impl->config.maximumObjectsPerWriteRange);
        }
        scene->serialRangeOffsets[serialRangeCount] = publishedOffset;
        for (u32 index = 0; index < scene->parallelSlices.Size(); ++index)
        {
            Impl::DirtySlice& slice = scene->parallelSlices[index];
            slice.publishedOffset = publishedOffset;
            slice.publishedCount = slice.count;
            slice.written = false;
            publishedOffset += slice.publishedCount;
        }
        scene->publishedChangeCount = publishedOffset;
        if (scene->publishedChangeCount != scene->stats.pendingChanges)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU dirty accounting diverged before publication", sceneHandle);
        scene->publicationSerial = m_impl->nextPublicationSerial++;
        if (scene->publicationSerial == 0)
            scene->publicationSerial = m_impl->nextPublicationSerial++;
        scene->phase = Impl::MutationPhase::Publishing;
        scene->written = false;
        ++scene->stats.publications;
        scene->stats.pendingChanges = 0;
        scene->stats.pendingRetirements = 0;

        publication.scene = sceneHandle;
        publication.mutationEpoch = mutationEpoch;
        publication.serial = scene->publicationSerial;
        const RenderSceneGpuRetirement* const retirements = scene->retirements.Size() != 0 ? &scene->retirements[0] : nullptr;
        publication.changeCount = scene->publishedChangeCount;
        publication.retirements = containers::ArraySpan<const RenderSceneGpuRetirement>(retirements, scene->retirements.Size());
        return true;
    }

    bool RenderSceneGpuPublisher::BuildUploadRequests(const RenderSceneGpuPublication& publication, containers::DynamicArray<GpuSceneUploadRequest>& requests,
                                                      RenderSceneGpuFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        requests.Clear();
        const Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr || scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid or closed RenderScene GPU publication", publication.scene);
        requests.Reserve(scene->publishedChangeCount);
        return m_impl->VisitPublished(*scene,
                                      [&requests](const u32, const Impl::TrackedProxy& tracked) noexcept
                                      {
                                          requests.PushBack({tracked.identity.allocation, 0, 1});
                                          return true;
                                      });
    }

    u32 RenderSceneGpuPublisher::GetWriteRangeCount(const RenderSceneGpuPublication& publication) const noexcept
    {
        const Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr || scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial ||
            scene->publishedChangeCount == 0)
            return 0;
        return scene->serialRangeWritten.Size() + scene->parallelSlices.Size();
    }

    bool RenderSceneGpuPublisher::GetWriteRange(const RenderSceneGpuPublication& publication, const u32 rangeIndex,
                                                RenderSceneGpuWriteRange& range) const noexcept
    {
        range = {};
        const Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        return scene != nullptr && scene->phase == Impl::MutationPhase::Publishing && scene->publicationSerial == publication.serial &&
               scene->publishedChangeCount != 0 && m_impl->DescribeWriteRange(*scene, rangeIndex, range);
    }

    bool RenderSceneGpuPublisher::WriteRange(const RenderSceneGpuPublication& publication,
                                             const containers::ArraySpan<const GpuSceneUploadReservation> reservations, const u32 rangeIndex,
                                             RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr || scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid or closed RenderScene GPU publication", publication.scene);
        if (reservations.Size() != scene->publishedChangeCount)
            return Fail(failure, RenderSceneGpuFailureCode::ReservationMismatch, "RenderScene GPU reservation count does not match the publication",
                        publication.scene);

        RenderSceneGpuWriteRange range;
        if (!m_impl->DescribeWriteRange(*scene, rangeIndex, range))
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU write range", publication.scene);
        if (!range.HasWork())
            return true;
        const u32 serialRangeCount = scene->serialRangeWritten.Size();
        if ((rangeIndex < serialRangeCount && scene->serialRangeWritten[rangeIndex] != 0) ||
            (rangeIndex >= serialRangeCount && scene->parallelSlices[rangeIndex - serialRangeCount].written))
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU write range was already consumed", publication.scene);

        const bool succeeded = m_impl->VisitWriteRange(
            *scene, rangeIndex,
            [this, publication, reservations, failure](const u32 index, Impl::TrackedProxy& tracked) noexcept
            {
                const GpuSceneUploadReservation& reservation = reservations[index];
                if (!reservation.IsValid())
                    return Fail(failure, RenderSceneGpuFailureCode::ReservationMismatch, "RenderScene GPU publication contains an invalid upload reservation",
                                publication.scene, tracked.proxy);
                RenderSceneGpuReadView source;
                if (!m_impl->scenes->ReadGpuProxy(tracked.proxy, source) || !source.HasCommonData())
                    return Fail(failure, RenderSceneGpuFailureCode::SourceUnavailable, "RenderScene proxy is unavailable during its sealed GPU publication",
                                publication.scene, tracked.proxy);

                const RenderProxyTransform& transform = *source.transform;
                const RenderProxyBounds& bounds = *source.bounds;
                const RenderProxyVisibilityFlags visibility = *source.visibility;
                const f32 position[3]{transform.row0[3], transform.row1[3], transform.row2[3]};
                if (tracked.identity.kind == RenderSceneGpuObjectKind::Instance)
                {
                    if (source.payloadKind != RenderProxyPayloadKind::Mesh || reservation.size != sizeof(GpuInstance))
                        return Fail(failure,
                                    source.payloadKind != RenderProxyPayloadKind::Mesh ? RenderSceneGpuFailureCode::SourceUnavailable
                                                                                       : RenderSceneGpuFailureCode::ReservationMismatch,
                                    "GPU instance source or upload reservation is invalid", publication.scene, tracked.proxy);
                    GpuInstance& output = *::new (reservation.destination) GpuInstance{};
                    SplitWorldPosition(position, m_impl->config.worldCellSize, output.worldCell, output.localPosition);
                    const f32 center[3]{(bounds.minimum[0] + bounds.maximum[0]) * 0.5f, (bounds.minimum[1] + bounds.maximum[1]) * 0.5f,
                                        (bounds.minimum[2] + bounds.maximum[2]) * 0.5f};
                    const f32 half[3]{(bounds.maximum[0] - bounds.minimum[0]) * 0.5f, (bounds.maximum[1] - bounds.minimum[1]) * 0.5f,
                                      (bounds.maximum[2] - bounds.minimum[2]) * 0.5f};
                    output.boundsCenterOffset[0] = center[0] - position[0];
                    output.boundsCenterOffset[1] = center[1] - position[1];
                    output.boundsCenterOffset[2] = center[2] - position[2];
                    output.boundsRadius = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
                    output.renderable = tracked.meshBinding.renderable.index;
                    output.materialSet = tracked.meshBinding.materialSet.index;
                    output.generation = tracked.identity.allocation.generation;
                    output.visibilityMask = *source.visibilityMask;
                    output.layerMaskLow = static_cast<u32>(*source.layerMask);
                    output.layerMaskHigh = static_cast<u32>(*source.layerMask >> 32u);
                    BuildRotationAndScale(transform, output.rotation, output.scale);
                    if (tracked.meshBinding.renderable.IsValid() && HasFlag(visibility, RenderProxyVisibilityFlags::Visible))
                        AddInstanceFlag(output.flags, GpuInstanceFlags::Active);
                    if (HasFlag(visibility, RenderProxyVisibilityFlags::CastsShadow))
                        AddInstanceFlag(output.flags, GpuInstanceFlags::CastsShadow);
                    if (HasFlag(visibility, RenderProxyVisibilityFlags::ReceivesDecals))
                        AddInstanceFlag(output.flags, GpuInstanceFlags::ReceivesDecals);
                    if (output.scale[0] * output.scale[1] * output.scale[2] < 0.0f)
                        AddInstanceFlag(output.flags, GpuInstanceFlags::NegativeScale);
                }
                else if (tracked.identity.kind == RenderSceneGpuObjectKind::Light)
                {
                    if (source.payloadKind != RenderProxyPayloadKind::Light || source.lightKind == nullptr || source.lightColor == nullptr ||
                        source.lightIntensity == nullptr || source.lightRange == nullptr || source.lightInnerConeRadians == nullptr ||
                        source.lightOuterConeRadians == nullptr || source.lightCastsShadow == nullptr || reservation.size != sizeof(GpuLight))
                        return Fail(failure,
                                    reservation.size != sizeof(GpuLight) ? RenderSceneGpuFailureCode::ReservationMismatch
                                                                         : RenderSceneGpuFailureCode::SourceUnavailable,
                                    "GPU light source or upload reservation is invalid", publication.scene, tracked.proxy);
                    GpuLight& output = *::new (reservation.destination) GpuLight{};
                    SplitWorldPosition(position, m_impl->config.worldCellSize, output.worldCell, output.localPosition);
                    output.type = static_cast<u32>(*source.lightKind);
                    output.range = *source.lightRange;
                    const f32 directionLength =
                        std::sqrt(transform.row2[0] * transform.row2[0] + transform.row2[1] * transform.row2[1] + transform.row2[2] * transform.row2[2]);
                    const f32 inverseDirection = directionLength > 1.0e-8f ? 1.0f / directionLength : 0.0f;
                    output.direction[0] = transform.row2[0] * inverseDirection;
                    output.direction[1] = transform.row2[1] * inverseDirection;
                    output.direction[2] = transform.row2[2] * inverseDirection;
                    output.innerConeCosine = std::cos(*source.lightInnerConeRadians);
                    output.outerConeCosine = std::cos(*source.lightOuterConeRadians);
                    output.color[0] = source.lightColor[0];
                    output.color[1] = source.lightColor[1];
                    output.color[2] = source.lightColor[2];
                    output.intensity = *source.lightIntensity;
                    output.flags = *source.lightCastsShadow ? 1u : 0u;
                    output.generation = tracked.identity.allocation.generation;
                    output.visibilityMask = *source.visibilityMask;
                }
                else
                {
                    if (source.payloadKind != RenderProxyPayloadKind::Decal || source.decalExtents == nullptr || source.decalFadeDistance == nullptr ||
                        source.decalSortKey == nullptr || reservation.size != sizeof(GpuDecal))
                        return Fail(failure,
                                    reservation.size != sizeof(GpuDecal) ? RenderSceneGpuFailureCode::ReservationMismatch
                                                                         : RenderSceneGpuFailureCode::SourceUnavailable,
                                    "GPU decal source or upload reservation is invalid", publication.scene, tracked.proxy);
                    GpuDecal& output = *::new (reservation.destination) GpuDecal{};
                    SplitWorldPosition(position, m_impl->config.worldCellSize, output.worldCell, output.localPosition);
                    BuildRotationAndScale(transform, output.rotation, nullptr);
                    output.halfExtent[0] = source.decalExtents[0];
                    output.halfExtent[1] = source.decalExtents[1];
                    output.halfExtent[2] = source.decalExtents[2];
                    output.fade = *source.decalFadeDistance;
                    output.material = tracked.decalMaterial.index;
                    output.visibilityMask = *source.visibilityMask;
                    output.generation = tracked.identity.allocation.generation;
                    output.sortBias = static_cast<i32>(*source.decalSortKey);
                    output.flags = tracked.decalMaterial.IsValid() && HasFlag(visibility, RenderProxyVisibilityFlags::Visible) ? 1u : 0u;
                }
                return true;
            });
        if (!succeeded)
            return false;
        if (rangeIndex < serialRangeCount)
            scene->serialRangeWritten[rangeIndex] = 1;
        else
            scene->parallelSlices[rangeIndex - serialRangeCount].written = true;
        return true;
    }

    bool RenderSceneGpuPublisher::FinishWrites(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr || scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid or closed RenderScene GPU publication", publication.scene);
        if (scene->written)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publication writes are already finalized", publication.scene);
        for (u32 index = 0; index < scene->serialRangeWritten.Size(); ++index)
            if (scene->serialRangeOffsets[index + 1u] != scene->serialRangeOffsets[index] && scene->serialRangeWritten[index] == 0)
                return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU serial write range is incomplete", publication.scene);
        for (u32 index = 0; index < scene->parallelSlices.Size(); ++index)
            if (scene->parallelSlices[index].publishedCount != 0 && !scene->parallelSlices[index].written)
                return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU parallel write range is incomplete", publication.scene);
        {
            concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
            scene->stats.writtenObjects += scene->publishedChangeCount;
            scene->written = true;
        }
        return true;
    }

    bool RenderSceneGpuPublisher::Complete(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr || scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid or closed RenderScene GPU publication", publication.scene);
        if (scene->publishedChangeCount != 0 && !scene->written)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publication cannot complete before all changed objects are staged",
                        publication.scene);
        for (u32 index = 0; index < scene->retirements.Size(); ++index)
        {
            const GpuSceneAllocation allocation = scene->retirements[index].identity.allocation;
            const GpuSceneAllocationState state = m_impl->lifetime->GetState(allocation);
            if (state == GpuSceneAllocationState::Invalid || state == GpuSceneAllocationState::Retiring)
                continue;
            GpuSceneLifetimeFailure lifetimeFailure;
            const bool succeeded = state == GpuSceneAllocationState::Allocated ? m_impl->lifetime->Cancel(allocation, &lifetimeFailure)
                                                                               : m_impl->lifetime->Retire(allocation, &lifetimeFailure);
            if (!succeeded)
            {
                if (failure != nullptr)
                {
                    failure->code = RenderSceneGpuFailureCode::LifetimeFailure;
                    failure->scene = publication.scene;
                    failure->proxy = scene->retirements[index].proxy;
                    failure->message = "RenderScene GPU retirement failed";
                    failure->lifetimeFailure = lifetimeFailure;
                }
                return false;
            }
        }
        {
            concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
            if (scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
                return Fail(failure, RenderSceneGpuFailureCode::InvalidState, "RenderScene GPU publication changed while retirements were being closed",
                            publication.scene);
            m_impl->CompletePublished(*scene);
        }
        return true;
    }

    bool RenderSceneGpuPublisher::Cancel(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::SceneState* const scene = m_impl != nullptr ? m_impl->FindScene(publication.scene) : nullptr;
        if (scene == nullptr)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid RenderScene GPU publication", publication.scene);
        concurrency::ScopedLock<concurrency::RWSpinLock> guard(scene->lock);
        if (scene->phase != Impl::MutationPhase::Publishing || scene->publicationSerial != publication.serial)
            return Fail(failure, RenderSceneGpuFailureCode::InvalidHandle, "invalid or closed RenderScene GPU publication", publication.scene);
        scene->publicationSerial = 0;
        scene->written = false;
        scene->phase = Impl::MutationPhase::Sealed;
        scene->stats.pendingChanges = scene->publishedChangeCount;
        scene->stats.pendingRetirements = scene->retirements.Size();
        scene->publishedChangeCount = 0;
        ++scene->stats.cancelledPublications;
        return true;
    }

    RenderSceneGpuStats RenderSceneGpuPublisher::GetStats() const noexcept
    {
        RenderSceneGpuStats result;
        if (m_impl == nullptr)
            return result;
        for (u32 index = 0; index < MaximumRenderScenes; ++index)
        {
            const Impl::SceneState* const scene = m_impl->sceneStates[index];
            if (scene == nullptr)
                continue;
            concurrency::ScopedSharedLock<concurrency::RWSpinLock> guard(scene->lock);
            result.trackedInstances += scene->stats.trackedInstances;
            result.trackedLights += scene->stats.trackedLights;
            result.trackedDecals += scene->stats.trackedDecals;
            result.pendingChanges += scene->stats.pendingChanges;
            result.pendingRetirements += scene->stats.pendingRetirements;
            result.publications += scene->stats.publications;
            result.coalescedChanges += scene->stats.coalescedChanges;
            result.writtenObjects += scene->stats.writtenObjects;
            result.cancelledPublications += scene->stats.cancelledPublications;
            result.rejectedOperations += scene->stats.rejectedOperations;
        }
        result.rejectedOperations += m_impl->rejectedOperations.GetValue();
        return result;
    }
} // namespace vanguard::rendering
