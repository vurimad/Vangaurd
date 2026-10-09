#include <vanguard/rendering/geometry_frame_work.hpp>

#include <vanguard/profiling/profiling.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/rhi/rhi.hpp>

#include <cstring>
#include <new>
#include <cmath>

namespace vanguard::rendering
{
    namespace
    {
        constexpr usize StorageAlignment = 16;
        static_assert(alignof(GpuView) <= StorageAlignment);
        static_assert(alignof(GpuDirectionalLightSelection) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityResultRange) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityCounters) <= StorageAlignment);
        static_assert(alignof(GpuGeometryResultRange) <= StorageAlignment);
        static_assert(alignof(GpuGeometryShellRange) <= StorageAlignment);
        static_assert(alignof(GeometryPhaseDrawRange) <= StorageAlignment);

        template <typename T> [[nodiscard]] bool AppendStorage(const u32 count, usize& size, usize& offset) noexcept
        {
            const usize aligned = (size + alignof(T) - 1u) & ~(alignof(T) - 1u);
            constexpr usize maximum = ~usize{0};
            if (aligned < size || static_cast<usize>(count) > (maximum - aligned) / sizeof(T))
                return false;
            offset = aligned;
            size = aligned + static_cast<usize>(count) * sizeof(T);
            return true;
        }

        template <typename T> [[nodiscard]] containers::ArraySpan<T> MakeSpan(memory::MemoryBlock& storage, const usize offset, const u32 count) noexcept
        {
            auto* const bytes = static_cast<u8*>(storage.address);
            return {reinterpret_cast<T*>(bytes + offset), count};
        }
    } // namespace

    GeometryFrameWork::~GeometryFrameWork()
    {
        Reset();
    }

    GeometryFrameWorkResult GeometryFrameWork::Prepare(const u64 frameSerial, const GeometryFrameWorkCapacity& capacity) noexcept
    {
        if (IsPrepared())
            return GeometryFrameWorkResult::AlreadyPrepared;
        if (frameSerial == 0 || !capacity.IsValid())
            return GeometryFrameWorkResult::InvalidDescriptor;

        usize viewsOffset = 0;
        usize resultRangesOffset = 0;
        usize countersOffset = 0;
        usize geometryResultsOffset = 0;
        usize shellDrawsOffset = 0;
        usize phaseDrawRangesOffset = 0;
        usize directionalLightsOffset = 0;
        usize storageSize = 0;
        if (!AppendStorage<GpuView>(capacity.views, storageSize, viewsOffset) ||
            !AppendStorage<GpuVisibilityResultRange>(capacity.views, storageSize, resultRangesOffset) ||
            !AppendStorage<GpuVisibilityCounters>(capacity.views, storageSize, countersOffset) ||
            !AppendStorage<GpuGeometryResultRange>(capacity.views, storageSize, geometryResultsOffset) ||
            !AppendStorage<GpuGeometryShellRange>(capacity.shellDraws, storageSize, shellDrawsOffset) ||
            !AppendStorage<GeometryPhaseDrawRange>(capacity.views * MaximumRenderPhases, storageSize, phaseDrawRangesOffset))
            return GeometryFrameWorkResult::CapacityExceeded;

        const bool lightsAppended = AppendStorage<GpuDirectionalLightSelection>(capacity.views, storageSize, directionalLightsOffset);
        if (!lightsAppended)
            return GeometryFrameWorkResult::CapacityExceeded;
        memory::MemoryBlock storage = memory::Allocate(memory::PoolId::Rendering, storageSize, StorageAlignment);
        if (!storage)
            return GeometryFrameWorkResult::CapacityExceeded;

        m_storage = storage;
        m_views = MakeSpan<GpuView>(m_storage, viewsOffset, capacity.views);
        m_directionalLights = MakeSpan<GpuDirectionalLightSelection>(m_storage, directionalLightsOffset, capacity.views);
        for (auto& selection : m_directionalLights)
            new (&selection) GpuDirectionalLightSelection();
        m_resultRanges = MakeSpan<GpuVisibilityResultRange>(m_storage, resultRangesOffset, capacity.views);
        m_counters = MakeSpan<GpuVisibilityCounters>(m_storage, countersOffset, capacity.views);
        m_geometryResults = MakeSpan<GpuGeometryResultRange>(m_storage, geometryResultsOffset, capacity.views);
        m_shellDraws = MakeSpan<GpuGeometryShellRange>(m_storage, shellDrawsOffset, capacity.shellDraws);
        m_phaseDrawRanges = MakeSpan<GeometryPhaseDrawRange>(m_storage, phaseDrawRangesOffset, capacity.views * MaximumRenderPhases);
        for (GpuVisibilityCounters& counters : m_counters)
            counters = {};
        m_capacity = capacity;
        m_frameSerial = frameSerial;
        return GeometryFrameWorkResult::Success;
    }

    void GeometryFrameWork::Reset() noexcept
    {
        m_visibilityPlan = {};
        m_graphResources.Reset();
        m_shellPlanImport = {};
        m_shellPlanBuffer.Reset();
        m_externalBuffers.Clear();
        m_views = {};
        m_directionalLights = {};
        m_resultRanges = {};
        m_counters = {};
        m_geometryResults = {};
        m_shellDraws = {};
        m_phaseDrawRanges = {};
        m_capacity = {};
        m_geometryResultCount = 0;
        m_shellDrawCount = 0;
        m_indirectArgumentCapacity = 0;
        m_catalogRevision = 0;
        m_shellPlanCacheSerial = 0;
        m_shellDrawsPlanned = false;
        m_frameSerial = 0;
        diagnosticSlot = ~u32{0};
        diagnosticReadbackImport = {};
        diagnosticCopyScope = {};
        for (auto& count : diagnosticPipelineBinds) count = 0;
        gpuTimingFrame = {};
        geometryTimingScope = {};
        for (auto& scope : cameraTimingScopes) scope = {};
        gpuTimingResolveScope = {};
        m_outputRegionCount = 0;
        for (auto& mask : m_cameraInputMasks) mask = 0;
        if (m_storage)
            memory::Free(m_storage);
    }

    GeometryFrameWorkResult GeometryFrameWork::BuildDirectVisibilityPlan(const RenderSceneHandle scene,
                                                                         const containers::ArraySpan<const RenderView> views,
                                                                         const u32 instanceTraversalExtent,
                                                                         const u32 visibleCapacityPerView) noexcept
    {
        if (!IsPrepared() || !scene.IsValid() || views.Size() != m_capacity.views || visibleCapacityPerView == 0)
            return GeometryFrameWorkResult::InvalidDescriptor;
        const u64 totalVisible = static_cast<u64>(views.Size()) * visibleCapacityPerView;
        if (totalVisible != m_capacity.visibleInstances || totalVisible >= InvalidGpuSceneIndex)
            return GeometryFrameWorkResult::InvalidVisibilityPlan;

        for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
        {
            if (views[viewIndex].frameSerial != m_frameSerial || !BuildGpuView(views[viewIndex], m_views[viewIndex]))
                return GeometryFrameWorkResult::InvalidDescriptor;
            m_resultRanges[viewIndex] = {viewIndex * visibleCapacityPerView, visibleCapacityPerView, viewIndex, viewIndex};
        }

        GpuVisibilityPlan plan;
        plan.frameSerial = m_frameSerial;
        plan.views = containers::ArraySpan<const GpuView>(m_views.Data(), views.Size());
        plan.results = containers::ArraySpan<const GpuVisibilityResultRange>(m_resultRanges.Data(), views.Size());
        plan.visibleCapacity = static_cast<u32>(totalVisible);
        plan.counterCount = views.Size();
        plan.sceneIndex = scene.index;
        plan.sceneGeneration = scene.generation;
        plan.instanceTraversalExtent = instanceTraversalExtent;
        return CommitVisibilityPlan(plan);
    }

    GeometryFrameWorkResult GeometryFrameWork::CommitVisibilityPlan(const GpuVisibilityPlan& plan) noexcept
    {
        if (!IsPrepared() || !plan.IsValid() || plan.frameSerial != m_frameSerial || plan.views.Data() != m_views.Data() ||
            !plan.candidates.Empty() || !plan.workRanges.Empty() || plan.results.Data() != m_resultRanges.Data() ||
            plan.views.Size() > m_views.Size() ||
            plan.results.Size() > m_resultRanges.Size() || plan.visibleCapacity > m_capacity.visibleInstances || plan.counterCount > m_counters.Size())
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        u32 workOffset = 0;
        u32 binOffset = 0;
        const u32 binsPerView = m_capacity.geometryBins / m_capacity.views;
        const u32 workItemsPerVisible = m_capacity.visibleInstances != 0
                                            ? m_capacity.geometryWorkItems / m_capacity.visibleInstances : 0;
        if (binsPerView == 0 || static_cast<u64>(binsPerView) * m_capacity.views != m_capacity.geometryBins ||
            workItemsPerVisible == 0 || static_cast<u64>(workItemsPerVisible) * m_capacity.visibleInstances != m_capacity.geometryWorkItems)
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        for (u32 resultIndex = 0; resultIndex < plan.results.Size(); ++resultIndex)
        {
            const GpuVisibilityResultRange& visibility = plan.results[resultIndex];
            const u64 requestedWorkCapacity = static_cast<u64>(visibility.visibleCapacity) * workItemsPerVisible;
            if (requestedWorkCapacity >= InvalidGpuSceneIndex || requestedWorkCapacity > m_capacity.geometryWorkItems ||
                workOffset > m_capacity.geometryWorkItems - requestedWorkCapacity ||
                binOffset > m_capacity.geometryBins - binsPerView)
                return GeometryFrameWorkResult::InvalidVisibilityPlan;
            const u32 workCapacity = static_cast<u32>(requestedWorkCapacity);

            GpuGeometryResultRange& result = m_geometryResults[resultIndex];
            result = {workOffset,
                      workCapacity,
                      binOffset,
                      binsPerView,
                      workOffset,
                      workCapacity,
                      0,
                      0,
                      binOffset,
                      binsPerView,
                      0,
                      0};
            workOffset += workCapacity;
            binOffset += binsPerView;
        }
        if (workOffset != m_capacity.geometryWorkItems || binOffset != m_capacity.geometryBins)
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        m_geometryResultCount = plan.results.Size();
        m_visibilityPlan = plan;
        return GeometryFrameWorkResult::Success;
    }

    GeometryFrameWorkResult GeometryFrameWork::PlanShellDraws(const RenderGeometryBatcher* const batcher, GeometryShellPlanCache* const cache) noexcept
    {
        if (!m_visibilityPlan.IsValid() || m_shellDrawsPlanned || m_geometryResultCount != m_visibilityPlan.views.Size())
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        const u64 revision = batcher != nullptr ? batcher->GetCatalogStats().revision : 0;
        const u32 viewCount = m_visibilityPlan.views.Size();
        const u32 binsPerView = m_capacity.geometryBins / m_capacity.views;
        if (binsPerView == 0 || static_cast<u64>(binsPerView) * m_capacity.views != m_capacity.geometryBins)
            return GeometryFrameWorkResult::InvalidVisibilityPlan;

        GeometryShellPlanCache::ViewKey viewKeys[MaximumRenderViewsPerFamily]{};
        for (u32 viewIndex = 0; viewIndex < viewCount; ++viewIndex)
        {
            const GpuView& view = m_visibilityPlan.views[viewIndex];
            viewKeys[viewIndex].phases = static_cast<u64>(view.phaseMaskLow) | (static_cast<u64>(view.phaseMaskHigh) << 32u);
            viewKeys[viewIndex].reverseDepth = (view.flags & static_cast<u32>(RenderViewFlags::ReverseDepth)) != 0;
        }

        if (cache != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(cache->lock);
            bool hit = cache->valid && cache->catalogRevision == revision && cache->viewCount == viewCount &&
                       cache->shellsPerView == m_capacity.shellsPerView && cache->binsPerView == binsPerView &&
                       cache->shellDraws.Size() <= m_shellDraws.Size();
            for (u32 viewIndex = 0; hit && viewIndex < viewCount; ++viewIndex)
                hit = cache->views[viewIndex] == viewKeys[viewIndex];
            if (hit)
            {
                const u64 currentRevision = batcher != nullptr ? batcher->GetCatalogStats().revision : 0;
                if (currentRevision != revision)
                    return GeometryFrameWorkResult::InvalidDescriptor;
                if (!cache->shellDraws.Empty())
                    std::memcpy(m_shellDraws.Data(), cache->shellDraws.Data(), static_cast<usize>(cache->shellDraws.Size()) * sizeof(GpuGeometryShellRange));
                const usize phaseBytes = static_cast<usize>(viewCount) * MaximumRenderPhases * sizeof(GeometryPhaseDrawRange);
                std::memcpy(m_phaseDrawRanges.Data(), cache->phaseDrawRanges, phaseBytes);
                for (u32 viewIndex = 0; viewIndex < viewCount; ++viewIndex)
                {
                    m_geometryResults[viewIndex].argumentOffset = cache->argumentOffsets[viewIndex];
                    m_geometryResults[viewIndex].argumentCapacity = cache->argumentCapacities[viewIndex];
                }
                m_catalogRevision = revision;
                m_shellDrawCount = cache->shellDraws.Size();
                m_indirectArgumentCapacity = cache->indirectArgumentCapacity;
                m_shellPlanCacheSerial = cache->serial;
                m_shellDrawsPlanned = true;
                return GeometryFrameWorkResult::Success;
            }
        }

        u32 drawCount = 0;
        u32 argumentCount = 0;
        for (u32 viewIndex = 0; viewIndex < viewCount; ++viewIndex)
        {
            if (m_visibilityPlan.results[viewIndex].viewIndex != viewIndex)
                return GeometryFrameWorkResult::InvalidVisibilityPlan;
            const u64 phases = viewKeys[viewIndex].phases;
            const u32 firstArgument = argumentCount;
            for (u32 phaseIndex = 0; phaseIndex < MaximumRenderPhases; ++phaseIndex)
            {
                GeometryPhaseDrawRange& range = m_phaseDrawRanges[viewIndex * MaximumRenderPhases + phaseIndex];
                range = {drawCount, 0};
                if (batcher == nullptr || (phases & (u64{1} << phaseIndex)) == 0)
                    continue;
                const RenderPhaseId phase{static_cast<u8>(phaseIndex)};
                const auto shells = batcher->GetPhaseShells(phase);
                for (const GeometryShellCatalogEntry& shell : shells)
                {
                    if (!shell.active || !shell.id.IsValid() || shell.id.index >= m_capacity.shellsPerView || shell.state.phase != phase)
                        return GeometryFrameWorkResult::InvalidDescriptor;
                    if (shell.state.depthTest && shell.state.reverseDepth != viewKeys[viewIndex].reverseDepth)
                        return GeometryFrameWorkResult::IncompatibleDepthConvention;
                    // RHI indirect byte offsets are currently bounded to u32.
                    const u64 end = static_cast<u64>(argumentCount) + shell.binCount;
                    if (drawCount >= m_shellDraws.Size() || end * sizeof(GpuGeometryIndirectArguments) > 0xffffffffull ||
                        end - firstArgument > m_geometryResults[viewIndex].binCapacity)
                        return GeometryFrameWorkResult::CapacityExceeded;
                    m_shellDraws[drawCount] = {shell.id.index, shell.id.generation, viewIndex, phaseIndex,
                                             argumentCount, shell.binCount, drawCount, 0};
                    ++drawCount;
                    ++range.shellCount;
                    argumentCount = static_cast<u32>(end);
                }
            }
            m_geometryResults[viewIndex].argumentOffset = firstArgument;
            m_geometryResults[viewIndex].argumentCapacity = argumentCount - firstArgument;
        }
        if (batcher != nullptr && batcher->GetCatalogStats().revision != revision)
            return GeometryFrameWorkResult::InvalidDescriptor;
        m_catalogRevision = revision;
        m_shellDrawCount = drawCount;
        m_indirectArgumentCapacity = argumentCount;
        m_shellDrawsPlanned = true;
        if (cache != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(cache->lock);
            if (batcher != nullptr && batcher->GetCatalogStats().revision != revision)
                return GeometryFrameWorkResult::InvalidDescriptor;
            cache->shellDraws.Resize(drawCount);
            if (drawCount != 0)
                std::memcpy(cache->shellDraws.Data(), m_shellDraws.Data(), static_cast<usize>(drawCount) * sizeof(GpuGeometryShellRange));
            const usize phaseBytes = static_cast<usize>(viewCount) * MaximumRenderPhases * sizeof(GeometryPhaseDrawRange);
            std::memcpy(cache->phaseDrawRanges, m_phaseDrawRanges.Data(), phaseBytes);
            for (u32 viewIndex = 0; viewIndex < viewCount; ++viewIndex)
            {
                cache->views[viewIndex] = viewKeys[viewIndex];
                cache->argumentOffsets[viewIndex] = m_geometryResults[viewIndex].argumentOffset;
                cache->argumentCapacities[viewIndex] = m_geometryResults[viewIndex].argumentCapacity;
            }
            cache->catalogRevision = revision;
            cache->viewCount = viewCount;
            cache->shellsPerView = m_capacity.shellsPerView;
            cache->binsPerView = binsPerView;
            cache->indirectArgumentCapacity = argumentCount;
            ++cache->serial;
            if (cache->serial == 0)
                ++cache->serial;
            m_shellPlanCacheSerial = cache->serial;
            cache->valid = true;
        }
        return GeometryFrameWorkResult::Success;
    }

    bool GeometryFrameWork::RecordPhase(const RenderGeometryBatcher& batcher, const GeometryAllocator& geometry,
                                       const RenderPhaseId phase, const StaticSurfaceDrawContext& drawContext,
                                       const GeometryPhaseDrawBuffers& buffers, rhi::Failure* const failure) const noexcept
    {
        if (failure != nullptr)
            *failure = {};
        const auto fail = [&](const char* message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = rhi::FailureCode::InvalidArgument;
                u32 index = 0;
                while (message[index] != '\0' && index + 1u < sizeof(failure->message))
                {
                    failure->message[index] = message[index];
                    ++index;
                }
                failure->message[index] = '\0';
            }
            return false;
        };
        if (!m_shellDrawsPlanned || !phase.IsValid() || drawContext.viewIndex >= m_geometryResultCount ||
            batcher.GetCatalogStats().revision != m_catalogRevision)
            return fail("geometry recording requires the joined catalog used for shell planning");
        const GeometryPhaseDrawRange range = m_phaseDrawRanges[drawContext.viewIndex * MaximumRenderPhases + phase.index];
        if (range.shellCount == 0)
            return true;
        const auto shells = batcher.GetPhaseShells(phase);
        if (shells.Size() != range.shellCount || drawContext.tableDirectoryDescriptor == InvalidGpuDescriptorIndex ||
            drawContext.pageDirectoryDescriptor == InvalidGpuDescriptorIndex || drawContext.viewDescriptor == InvalidGpuDescriptorIndex ||
            !std::isfinite(drawContext.worldCellSize) || !(drawContext.worldCellSize > 0.0f))
            return fail("geometry phase catalog or draw context is invalid");
        {
            VG_PROFILE_ZONE_NAMED("Geometry/RecordPhase/ValidateBuffers");
            rhi::BufferDesc instanceDesc, argumentDesc, counterDesc;
            const bool instancesAvailable = rhi::GetBufferDesc(buffers.instances, instanceDesc, failure);
            if (!instancesAvailable)
                return false;
            const bool argumentsAvailable = rhi::GetBufferDesc(buffers.arguments, argumentDesc, failure);
            if (!argumentsAvailable)
                return false;
            const bool countersAvailable = rhi::GetBufferDesc(buffers.counters, counterDesc, failure);
            if (!countersAvailable)
                return false;
            if (instanceDesc.size < static_cast<u64>(m_capacity.geometryWorkItems) * sizeof(MeshDrawInstance) ||
                argumentDesc.size < static_cast<u64>(m_indirectArgumentCapacity) * sizeof(GpuGeometryIndirectArguments) ||
                counterDesc.size < static_cast<u64>(m_shellDrawCount) * sizeof(GpuGeometryShellCounters))
                return fail("geometry draw buffers do not cover the frame reservations");
        }
        struct PipelineBindCount
        {
            u32* destination;
            u32 count = 0;
            ~PipelineBindCount() { if (destination != nullptr) *destination += count; }
        } binds{diagnosticSlot != ~u32{0} ? &diagnosticPipelineBinds[drawContext.viewIndex] : nullptr};
        // Publish once per phase, including failure exits, instead of writing
        // neighboring view counters on every shell in parallel camera jobs.
        for (u32 ordinal = 0; ordinal < shells.Size(); ++ordinal)
        {
            const GeometryShellCatalogEntry& shell = shells[ordinal];
            const GpuGeometryShellRange& plan = m_shellDraws[range.firstShell + ordinal];
            if (!shell.active || shell.id.index != plan.shell || shell.id.generation != plan.generation || shell.binCount != plan.argumentCapacity)
                return fail("geometry shell changed between planning and recording");
            if (plan.argumentCapacity == 0)
                continue;
            {
                VG_PROFILE_ZONE_NAMED("Geometry/RecordPhase/ResolveArenas");
                GeometryVertexArenaView vertices;
                GeometryIndexArenaView indices;
                const bool verticesAvailable = geometry.GetVertexArena(shell.state.vertexArena, vertices);
                if (!verticesAvailable)
                    return fail("geometry shell vertex arena is unavailable");
                const bool indicesAvailable = geometry.GetIndexArena(shell.state.indexArena, indices);
                if (!indicesAvailable)
                    return fail("geometry shell index arena is unavailable");
                if (indices.format != shell.state.indexFormat || vertices.bindingCount == 0 || vertices.bindingCount >= rhi::MaximumVertexBindings)
                    return fail("geometry shell arenas are unavailable or incompatible");
                {
                    VG_PROFILE_ZONE_NAMED("Geometry/RecordPhase/BindState");
                    const bool pipelineSet = rhi::SetPipeline(shell.state.pipeline, failure);
                    if (!pipelineSet)
                        return false;
                    ++binds.count;
                    const bool constantsSet = rhi::SetPushConstants(&drawContext, sizeof(drawContext), failure);
                    if (!constantsSet)
                        return false;
                    for (u32 bindingIndex = 0; bindingIndex < vertices.bindingCount; ++bindingIndex)
                    {
                        const u8 slot = vertices.bindings[bindingIndex].binding;
                        if (slot >= MeshDrawInstanceBinding)
                            return fail("geometry vertex arena overlaps the instance binding");
                        const rhi::VertexBufferBinding binding{vertices.buffers[bindingIndex], 0, slot};
                        const bool verticesBound = rhi::BindVertexBuffers(slot, {&binding, 1}, failure);
                        if (!verticesBound)
                            return false;
                    }
                    const rhi::VertexBufferBinding instanceBinding{buffers.instances, 0, static_cast<u8>(MeshDrawInstanceBinding)};
                    const bool instancesBound = rhi::BindVertexBuffers(MeshDrawInstanceBinding, {&instanceBinding, 1}, failure);
                    if (!instancesBound)
                        return false;
                    const bool indicesBound = rhi::BindIndexBuffer({indices.buffer, 0, indices.format}, failure);
                    if (!indicesBound)
                        return false;
                    const bool argumentsBound = rhi::BindIndirectArguments(buffers.arguments, buffers.counters, failure);
                    if (!argumentsBound)
                        return false;
                }
            }
            {
                VG_PROFILE_ZONE_NAMED("Geometry/RecordPhase/DrawIndirect");
                const bool drawRecorded = rhi::DrawIndexedPrimitiveIndirectCount(static_cast<u64>(plan.argumentOffset) * sizeof(GpuGeometryIndirectArguments),
                                                                                static_cast<u64>(plan.counterIndex) * sizeof(GpuGeometryShellCounters),
                                                                                plan.argumentCapacity, failure);
                if (!drawRecorded)
                    return false;
            }
        }
        return true;
    }
} // namespace vanguard::rendering
