#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/rendering/gpu_scene_visibility.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/viewport.hpp>

namespace vanguard::rendering
{
    class GeometryFrameWork;

    // GPU wire record: existing totals plus one reduction of shell counters.
    struct alignas(16) GeometryGpuDiagnostics
    {
        GpuVisibilityCounters visibility;
        GpuGeometryCounters geometry;
        u32 emittedArguments = 0;
        u32 argumentOverflow = 0;
        u32 nonemptyShells = 0;
        u32 reserved = 0;
    };
    static_assert(sizeof(GeometryGpuDiagnostics) == 64);
    static_assert(offsetof(GeometryGpuDiagnostics, geometry) == 16);
    static_assert(offsetof(GeometryGpuDiagnostics, emittedArguments) == 48);
    static_assert(sizeof(GpuGeometryShellRange) == 32);
    static_assert(offsetof(GpuGeometryShellRange, argumentCapacity) == 20);
    static_assert(offsetof(GpuGeometryShellRange, counterIndex) == 24);
    static_assert(sizeof(GpuGeometryShellCounters) == 16);

    struct GeometryViewDiagnostics
    {
        RenderViewId view;
        u32 candidates = 0;
        u32 unresolvedGpuIdentities = 0;
        u32 firstShell = 0;
        u32 plannedShells = 0;
        u32 pipelineBinds = 0; // Recorded CPU binds, not GPU-visible shells.
        u32 visibleCapacity = 0;
        u32 workCapacity = 0;
        u32 instanceCapacity = 0;
        u32 argumentCapacity = 0;
        GeometryGpuDiagnostics gpu;
    };

    struct GeometryFrameDiagnostics
    {
        u64 frameSerial = 0;
        EngineViewportHandle viewport;
        RenderSceneHandle scene;
        u32 viewCount = 0;
        bool frameFailed = false;
        bool gpuAvailable = false;
        GeometryViewDiagnostics views[MaximumRenderViewsPerFamily]{};
    };

    enum class GeometryDiagnosticsPoll : u8 { Empty, Pending, Ready, Failed };

    // Three samples maximum. No frame/scene payload is retained. Poll has one
    // consumer; reservation/terminal publication may run on renderer jobs.
    class GeometryDiagnosticReadbacks final
    {
    public:
        static constexpr u32 SlotCount = 3;
        static constexpr u32 InvalidSlot = ~u32{0};
        [[nodiscard]] u32 Reserve(const GeometryFrameWork& work, const RenderFrameInfo& frame) noexcept;
        void Cancel(u32 slot) noexcept; // Before any node can record the copy.
        void Reset() noexcept; // Joined renderer shutdown; polling must be stopped.
        void Complete(u32 slot, const GeometryFrameWork& work, containers::ArraySpan<const CommandScopeExecutionReceipt> receipts, bool failed) noexcept;
        [[nodiscard]] GeometryDiagnosticsPoll Poll(GeometryFrameDiagnostics& output, rhi::Failure* failure = nullptr) noexcept;
        [[nodiscard]] rhi::BufferRef GetBuffer(u32 slot) const noexcept { return m_slots[slot].buffer.GetRef(); }
        [[nodiscard]] const GeometryFrameDiagnostics& GetReport(u32 slot) const noexcept { return m_slots[slot].report; }
        [[nodiscard]] u64 GetDroppedSamples() const noexcept { return m_dropped.GetValue(); }
        void NoteDroppedSample() noexcept { static_cast<void>(m_dropped.Increment()); }
    private:
        enum : u32 { Free, Reserved, Submitted, Reading, Lost, Quarantined };
        struct Slot
        {
            concurrency::Atomic<u32> state{Free};
            rhi::Buffer buffer;
            rhi::GpuFence fence;
            GeometryFrameDiagnostics report;
        };
        Slot m_slots[SlotCount];
        concurrency::Atomic<u64> m_dropped{0};
    };
}
