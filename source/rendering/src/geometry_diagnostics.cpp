#include <vanguard/rendering/geometry_diagnostics.hpp>
#include <vanguard/rendering/geometry_frame_work.hpp>
#include <cstring>

namespace vanguard::rendering
{
    u32 GeometryDiagnosticReadbacks::Reserve(const GeometryFrameWork& work, const RenderFrameInfo& frame) noexcept
    {
        for (u32 index = 0; index < SlotCount; ++index)
        {
            Slot& slot = m_slots[index];
            // One CAS per attempted sample slot, never per scene element.
            const u32 previous = slot.state.CompareExchange(Reserved, Free);
            if (previous != Free) continue;
            if (!slot.buffer.IsValid())
            {
                rhi::BufferDesc desc;
                desc.size = MaximumRenderViewsPerFamily * sizeof(GeometryGpuDiagnostics);
                desc.memoryType = rhi::MemoryType::Readback;
                desc.usage = rhi::BufferUsage::CopyDestination;
                desc.initialState = rhi::ResourceState::CopyDestination;
                slot.buffer = rhi::Buffer::Create(desc);
                if (!slot.buffer.IsValid())
                {
                    slot.state.SetValue(Free);
                    break;
                }
            }
            slot.report = {};
            slot.fence = {};
            slot.report.frameSerial = work.GetFrameSerial();
            slot.report.viewport = frame.GetEngineViewport();
            slot.report.scene = frame.GetViewFamily().IsValid() ? frame.GetViewFamily().GetScene() : frame.GetViewSetup().scene;
            const auto& plan = work.GetVisibilityPlan();
            slot.report.viewCount = plan.views.Size();
            for (u32 view = 0; view < plan.views.Size(); ++view)
            {
                auto& report = slot.report.views[view];
                report.view = {plan.views[view].viewIndex, plan.views[view].viewGeneration};
                report.candidates = work.GetCandidateViews()[view].diagnosticCandidates;
                report.unresolvedGpuIdentities = work.GetCandidateViews()[view].diagnosticUnresolvedIdentities;
                report.visibleCapacity = plan.results[view].visibleCapacity;
                const auto& geometry = work.GetGeometryResults()[view];
                report.workCapacity = geometry.workCapacity;
                report.instanceCapacity = geometry.instanceCapacity;
                report.argumentCapacity = geometry.argumentCapacity;
            }
            // Existing dense plan is grouped by view and phase. Scan it once.
            const auto shells = work.GetShellDraws();
            for (u32 shell = 0; shell < shells.Size(); ++shell)
            {
                auto& report = slot.report.views[shells[shell].resultIndex];
                if (report.plannedShells == 0) report.firstShell = shell;
                ++report.plannedShells;
            }
            return index;
        }
        static_cast<void>(m_dropped.Increment());
        return InvalidSlot;
    }

    void GeometryDiagnosticReadbacks::Cancel(const u32 index) noexcept
    {
        if (index != InvalidSlot) m_slots[index].state.SetValue(Free);
    }

    void GeometryDiagnosticReadbacks::Reset() noexcept
    {
        // RHI owns fence-safe destruction, including device-loss retirement.
        // This is called with other renderer owners before the retirement flush.
        for (Slot& slot : m_slots)
        {
            slot.buffer.Reset();
            slot.report = {};
            slot.fence = {};
            slot.state.SetValue(Free);
        }
        m_dropped.SetValue(0);
    }

    void GeometryDiagnosticReadbacks::Complete(const u32 index, const GeometryFrameWork& work,
        const containers::ArraySpan<const CommandScopeExecutionReceipt> receipts, const bool failed) noexcept
    {
        if (index == InvalidSlot) return;
        Slot& slot = m_slots[index];
        slot.report.frameFailed = failed;
        for (u32 view = 0; view < slot.report.viewCount; ++view)
            slot.report.views[view].pipelineBinds = work.diagnosticPipelineBinds[view];
        for (const auto& receipt : receipts)
        {
            if (!work.diagnosticCopyScope.IsValid() || receipt.scope != work.diagnosticCopyScope) continue;
            if (receipt.completion == CommandScopeCompletionKind::Submitted && receipt.fence.IsValid())
            {
                slot.fence = receipt.fence;
                slot.state.SetValue(Submitted);
                return;
            }
            slot.report.frameFailed = true;
            slot.state.SetValue(receipt.completion == CommandScopeCompletionKind::DiscardedBeforeSubmission ? Submitted : Lost);
            return;
        }
        slot.report.frameFailed = true;
        // No copy recorded is safe to recycle. A recorded copy lacking terminal
        // evidence stays quarantined instead of being overwritten by a new frame.
        slot.state.SetValue(work.diagnosticCopyScope.IsValid() ? Lost : Submitted);
    }

    GeometryDiagnosticsPoll GeometryDiagnosticReadbacks::Poll(GeometryFrameDiagnostics& output, rhi::Failure* failure) noexcept
    {
        output = {};
        if (failure != nullptr) *failure = {};
        bool pending = false;
        for (Slot& slot : m_slots)
        {
            const u32 state = slot.state.GetValue();
            if (state == Reserved) { pending = true; continue; }
            if (state != Submitted && state != Lost) continue;
            const bool deviceLost = rhi::TestDeviceState() != rhi::DeviceState::Operational;
            if (state != Lost && !deviceLost && slot.fence.IsValid())
            {
                const bool complete = rhi::IsGpuFenceComplete(slot.fence);
                if (!complete) { pending = true; continue; }
            }
            slot.state.SetValue(Reading); // Poll is a single-consumer API.
            output = slot.report;
            if (state == Lost || deviceLost)
            {
                output.frameFailed = true;
                slot.state.SetValue(Quarantined);
                return GeometryDiagnosticsPoll::Failed;
            }
            if (output.frameFailed || !slot.fence.IsValid())
            {
                slot.state.SetValue(Free);
                return GeometryDiagnosticsPoll::Failed;
            }
            const u64 size = output.viewCount * sizeof(GeometryGpuDiagnostics);
            const void* mapped = rhi::LockBuffer(slot.buffer, 0, size, failure);
            if (mapped == nullptr)
            {
                slot.state.SetValue(Free);
                return GeometryDiagnosticsPoll::Failed;
            }
            for (u32 view = 0; view < output.viewCount; ++view)
                std::memcpy(&output.views[view].gpu, static_cast<const u8*>(mapped) + view * sizeof(GeometryGpuDiagnostics), sizeof(GeometryGpuDiagnostics));
            rhi::UnlockBuffer(slot.buffer);
            output.gpuAvailable = true;
            slot.state.SetValue(Free);
            return GeometryDiagnosticsPoll::Ready;
        }
        return pending ? GeometryDiagnosticsPoll::Pending : GeometryDiagnosticsPoll::Empty;
    }
}
