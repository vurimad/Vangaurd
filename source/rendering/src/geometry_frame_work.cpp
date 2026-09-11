#include <vanguard/rendering/geometry_frame_work.hpp>

#include <vanguard/system/assert.hpp>
#include <vanguard/rhi/rhi.hpp>

#include <new>
#include <cmath>

namespace vanguard::rendering
{
    namespace
    {
        constexpr usize StorageAlignment = 16;
        static_assert(alignof(GpuVisibilityPlanBuilder) <= StorageAlignment);
        static_assert(alignof(GpuView) <= StorageAlignment);
        static_assert(alignof(GpuDirectionalLightSelection) <= StorageAlignment);
        static_assert(alignof(GpuInstanceIndex) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityWorkRange) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityResultRange) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityCounters) <= StorageAlignment);
        static_assert(alignof(GpuGeometryExpansionWorkRange) <= StorageAlignment);
        static_assert(alignof(GpuGeometryWorkRange) <= StorageAlignment);
        static_assert(alignof(GpuGeometryPrefixWorkRange) <= StorageAlignment);
        static_assert(alignof(GpuGeometryResultRange) <= StorageAlignment);
        static_assert(alignof(GpuGeometryShellRange) <= StorageAlignment);
        static_assert(alignof(GeometryPhaseDrawRange) <= StorageAlignment);
        static_assert(alignof(RenderSceneGpuCandidateBatch) <= StorageAlignment);
        static_assert(alignof(GeometryCandidateView) <= StorageAlignment);
        static_assert(alignof(GpuVisibilityCandidateRange) <= StorageAlignment);
        static_assert(alignof(RenderSceneGpuCandidateBatchResult) <= StorageAlignment);
        static_assert(alignof(RenderSceneFailure) <= StorageAlignment);

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
        usize candidatesOffset = 0;
        usize workRangesOffset = 0;
        usize resultRangesOffset = 0;
        usize countersOffset = 0;
        usize expansionWorkRangesOffset = 0;
        usize geometryWorkRangesOffset = 0;
        usize binPrefixRangesOffset = 0;
        usize geometryResultsOffset = 0;
        usize shellDrawsOffset = 0;
        usize phaseDrawRangesOffset = 0;
        usize candidateBatchesOffset = 0;
        usize candidateViewsOffset = 0;
        usize candidateRangesOffset = 0;
        usize candidateResultsOffset = 0;
        usize candidateFailuresOffset = 0;
        usize visibilityBuilderOffset = 0;
        usize directionalLightsOffset = 0;
        usize storageSize = 0;
        const u32 candidateWrites = capacity.views * capacity.candidateBatches;
        if (!AppendStorage<GpuVisibilityPlanBuilder>(1u, storageSize, visibilityBuilderOffset) ||
            !AppendStorage<GpuView>(capacity.views, storageSize, viewsOffset) ||
            !AppendStorage<GpuInstanceIndex>(capacity.candidates, storageSize, candidatesOffset) ||
            !AppendStorage<GpuVisibilityWorkRange>(capacity.workRanges, storageSize, workRangesOffset) ||
            !AppendStorage<GpuVisibilityResultRange>(capacity.views, storageSize, resultRangesOffset) ||
            !AppendStorage<GpuVisibilityCounters>(capacity.views, storageSize, countersOffset) ||
            !AppendStorage<GpuGeometryExpansionWorkRange>(capacity.expansionWorkRanges, storageSize, expansionWorkRangesOffset) ||
            !AppendStorage<GpuGeometryWorkRange>(capacity.geometryWorkRanges, storageSize, geometryWorkRangesOffset) ||
            !AppendStorage<GpuGeometryPrefixWorkRange>(capacity.binPrefixRanges, storageSize, binPrefixRangesOffset) ||
            !AppendStorage<GpuGeometryResultRange>(capacity.views, storageSize, geometryResultsOffset) ||
            !AppendStorage<GpuGeometryShellRange>(capacity.shellDraws, storageSize, shellDrawsOffset) ||
            !AppendStorage<GeometryPhaseDrawRange>(capacity.views * MaximumRenderPhases, storageSize, phaseDrawRangesOffset) ||
            !AppendStorage<RenderSceneGpuCandidateBatch>(capacity.candidateBatches, storageSize, candidateBatchesOffset) ||
            !AppendStorage<GeometryCandidateView>(capacity.views, storageSize, candidateViewsOffset) ||
            !AppendStorage<GpuVisibilityCandidateRange>(candidateWrites, storageSize, candidateRangesOffset) ||
            !AppendStorage<RenderSceneGpuCandidateBatchResult>(candidateWrites, storageSize, candidateResultsOffset) ||
            !AppendStorage<RenderSceneFailure>(candidateWrites, storageSize, candidateFailuresOffset))
            return GeometryFrameWorkResult::CapacityExceeded;

        const bool lightsAppended = AppendStorage<GpuDirectionalLightSelection>(capacity.views, storageSize, directionalLightsOffset);
        if (!lightsAppended)
            return GeometryFrameWorkResult::CapacityExceeded;
        memory::MemoryBlock storage = memory::Allocate(memory::PoolId::Rendering, storageSize, StorageAlignment);
        if (!storage)
            return GeometryFrameWorkResult::CapacityExceeded;

        m_storage = storage;
        m_visibilityBuilder = new (static_cast<u8*>(m_storage.address) + visibilityBuilderOffset) GpuVisibilityPlanBuilder();
        m_views = MakeSpan<GpuView>(m_storage, viewsOffset, capacity.views);
        m_directionalLights = MakeSpan<GpuDirectionalLightSelection>(m_storage, directionalLightsOffset, capacity.views);
        for (auto& selection : m_directionalLights)
            new (&selection) GpuDirectionalLightSelection();
        m_candidates = MakeSpan<GpuInstanceIndex>(m_storage, candidatesOffset, capacity.candidates);
        m_workRanges = MakeSpan<GpuVisibilityWorkRange>(m_storage, workRangesOffset, capacity.workRanges);
        m_resultRanges = MakeSpan<GpuVisibilityResultRange>(m_storage, resultRangesOffset, capacity.views);
        m_counters = MakeSpan<GpuVisibilityCounters>(m_storage, countersOffset, capacity.views);
        m_expansionWorkRanges = MakeSpan<GpuGeometryExpansionWorkRange>(m_storage, expansionWorkRangesOffset, capacity.expansionWorkRanges);
        m_geometryWorkRanges = MakeSpan<GpuGeometryWorkRange>(m_storage, geometryWorkRangesOffset, capacity.geometryWorkRanges);
        m_binPrefixRanges = MakeSpan<GpuGeometryPrefixWorkRange>(m_storage, binPrefixRangesOffset, capacity.binPrefixRanges);
        m_geometryResults = MakeSpan<GpuGeometryResultRange>(m_storage, geometryResultsOffset, capacity.views);
        m_shellDraws = MakeSpan<GpuGeometryShellRange>(m_storage, shellDrawsOffset, capacity.shellDraws);
        m_phaseDrawRanges = MakeSpan<GeometryPhaseDrawRange>(m_storage, phaseDrawRangesOffset, capacity.views * MaximumRenderPhases);
        m_candidateBatches = MakeSpan<RenderSceneGpuCandidateBatch>(m_storage, candidateBatchesOffset, capacity.candidateBatches);
        m_candidateViews = MakeSpan<GeometryCandidateView>(m_storage, candidateViewsOffset, capacity.views);
        for (u32 viewIndex = 0; viewIndex < capacity.views; ++viewIndex)
            new (m_candidateViews.Data() + viewIndex) GeometryCandidateView();
        m_candidateRanges = MakeSpan<GpuVisibilityCandidateRange>(m_storage, candidateRangesOffset, candidateWrites);
        m_candidateResults = MakeSpan<RenderSceneGpuCandidateBatchResult>(m_storage, candidateResultsOffset, candidateWrites);
        m_candidateFailures = MakeSpan<RenderSceneFailure>(m_storage, candidateFailuresOffset, candidateWrites);
        for (GpuVisibilityCounters& counters : m_counters)
            counters = {};
        m_capacity = capacity;
        m_frameSerial = frameSerial;
        return GeometryFrameWorkResult::Success;
    }

    void GeometryFrameWork::Reset() noexcept
    {
        if (m_visibilityBuilder != nullptr)
        {
            m_visibilityBuilder->Cancel();
            m_visibilityBuilder->~GpuVisibilityPlanBuilder();
            m_visibilityBuilder = nullptr;
        }
        m_visibilityPlan = {};
        m_candidatePlan = {};
        m_graphResources.Reset();
        m_externalBuffers.Clear();
        m_views = {};
        m_directionalLights = {};
        m_candidates = {};
        m_workRanges = {};
        m_resultRanges = {};
        m_counters = {};
        m_expansionWorkRanges = {};
        m_geometryWorkRanges = {};
        m_binPrefixRanges = {};
        m_geometryResults = {};
        m_shellDraws = {};
        m_phaseDrawRanges = {};
        m_candidateBatches = {};
        m_candidateViews = {};
        m_candidateRanges = {};
        m_candidateResults = {};
        m_candidateFailures = {};
        m_capacity = {};
        m_expansionWorkRangeCount = 0;
        m_geometryWorkRangeCount = 0;
        m_binPrefixRangeCount = 0;
        m_prefixBlockCount = 0;
        m_geometryResultCount = 0;
        m_shellDrawCount = 0;
        m_indirectArgumentCapacity = 0;
        m_catalogRevision = 0;
        m_shellDrawsPlanned = false;
        m_frameSerial = 0;
        diagnosticSlot = ~u32{0};
        diagnosticReadbackImport = {};
        diagnosticCopyScope = {};
        for (auto& count : diagnosticPipelineBinds) count = 0;
        m_outputRegionCount = 0;
        for (auto& mask : m_cameraInputMasks) mask = 0;
        if (m_storage)
            memory::Free(m_storage);
    }

    GpuVisibilityBuildStorage GeometryFrameWork::GetVisibilityBuildStorage() noexcept
    {
        return {m_views, m_candidates, m_workRanges, m_resultRanges};
    }

    GpuVisibilityPlanBuilder& GeometryFrameWork::GetVisibilityBuilder() noexcept
    {
        if (m_visibilityBuilder == nullptr)
            VG_FATAL("geometry frame work must be prepared before accessing its visibility builder");
        return *m_visibilityBuilder;
    }

    GeometryFrameWorkResult GeometryFrameWork::CommitVisibilityPlan(const GpuVisibilityPlan& plan) noexcept
    {
        if (!IsPrepared() || !plan.IsValid() || plan.frameSerial != m_frameSerial || plan.views.Data() != m_views.Data() ||
            plan.candidates.Data() != m_candidates.Data() || plan.workRanges.Data() != m_workRanges.Data() || plan.results.Data() != m_resultRanges.Data() ||
            plan.views.Size() > m_views.Size() || plan.candidates.Size() > m_candidates.Size() || plan.workRanges.Size() > m_workRanges.Size() ||
            plan.results.Size() > m_resultRanges.Size() || plan.visibleCapacity > m_capacity.visibleInstances || plan.counterCount > m_counters.Size())
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        u32 expansionRangeCount = 0;
        u32 geometryRangeCount = 0;
        u32 binPrefixRangeCount = 0;
        u32 prefixBlockCount = 0;
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
            const u32 expansionBlockOffset = prefixBlockCount;
            for (u32 ordinal = 0; ordinal < visibility.visibleCapacity;)
            {
                if (expansionRangeCount >= m_expansionWorkRanges.Size() || prefixBlockCount >= m_capacity.prefixBlocks)
                    return GeometryFrameWorkResult::InvalidVisibilityPlan;
                const u32 count = visibility.visibleCapacity - ordinal < GpuVisibilityThreadsPerGroup
                                      ? visibility.visibleCapacity - ordinal : GpuVisibilityThreadsPerGroup;
                m_expansionWorkRanges[expansionRangeCount++] = {ordinal, count, resultIndex, prefixBlockCount++};
                ordinal += count;
            }
            for (u32 ordinal = 0; ordinal < workCapacity;)
            {
                if (geometryRangeCount >= m_geometryWorkRanges.Size())
                    return GeometryFrameWorkResult::InvalidVisibilityPlan;
                const u32 count = workCapacity - ordinal < GpuVisibilityThreadsPerGroup
                                      ? workCapacity - ordinal : GpuVisibilityThreadsPerGroup;
                m_geometryWorkRanges[geometryRangeCount++] = {ordinal, count, resultIndex, 0};
                ordinal += count;
            }
            const u32 expansionBlockCount = prefixBlockCount - expansionBlockOffset;
            const u32 binBlockOffset = prefixBlockCount;
            for (u32 ordinal = 0; ordinal < binsPerView;)
            {
                if (binPrefixRangeCount >= m_binPrefixRanges.Size() || prefixBlockCount >= m_capacity.prefixBlocks)
                    return GeometryFrameWorkResult::InvalidVisibilityPlan;
                const u32 count = binsPerView - ordinal < GpuVisibilityThreadsPerGroup
                                      ? binsPerView - ordinal : GpuVisibilityThreadsPerGroup;
                m_binPrefixRanges[binPrefixRangeCount++] = {ordinal, count, resultIndex, prefixBlockCount++};
                ordinal += count;
            }
            result = {workOffset,
                      workCapacity,
                      binOffset,
                      binsPerView,
                      workOffset,
                      workCapacity,
                      binOffset,
                      binsPerView,
                      expansionBlockOffset,
                      expansionBlockCount,
                      binBlockOffset,
                      prefixBlockCount - binBlockOffset};
            workOffset += workCapacity;
            binOffset += binsPerView;
        }
        if (workOffset != m_capacity.geometryWorkItems || binOffset != m_capacity.geometryBins)
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        m_expansionWorkRangeCount = expansionRangeCount;
        m_geometryWorkRangeCount = geometryRangeCount;
        m_binPrefixRangeCount = binPrefixRangeCount;
        m_prefixBlockCount = prefixBlockCount;
        m_geometryResultCount = plan.results.Size();
        m_visibilityPlan = plan;
        return GeometryFrameWorkResult::Success;
    }

    GeometryFrameWorkResult GeometryFrameWork::PlanShellDraws(const RenderGeometryBatcher* const batcher) noexcept
    {
        if (!m_visibilityPlan.IsValid() || m_shellDrawsPlanned || m_geometryResultCount != m_visibilityPlan.views.Size())
            return GeometryFrameWorkResult::InvalidVisibilityPlan;
        const u64 revision = batcher != nullptr ? batcher->GetCatalogStats().revision : 0;
        u32 drawCount = 0;
        u32 argumentCount = 0;
        for (u32 viewIndex = 0; viewIndex < m_visibilityPlan.views.Size(); ++viewIndex)
        {
            if (m_visibilityPlan.results[viewIndex].viewIndex != viewIndex)
                return GeometryFrameWorkResult::InvalidVisibilityPlan;
            const GpuView& view = m_visibilityPlan.views[viewIndex];
            const u64 phases = static_cast<u64>(view.phaseMaskLow) | (static_cast<u64>(view.phaseMaskHigh) << 32u);
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
                    const bool reverseDepth = (view.flags & static_cast<u32>(RenderViewFlags::ReverseDepth)) != 0;
                    if (shell.state.depthTest && shell.state.reverseDepth != reverseDepth)
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
            const bool drawRecorded = rhi::DrawIndexedPrimitiveIndirectCount(static_cast<u64>(plan.argumentOffset) * sizeof(GpuGeometryIndirectArguments),
                                                                            static_cast<u64>(plan.counterIndex) * sizeof(GpuGeometryShellCounters),
                                                                            plan.argumentCapacity, failure);
            if (!drawRecorded)
                return false;
        }
        return true;
    }
} // namespace vanguard::rendering
