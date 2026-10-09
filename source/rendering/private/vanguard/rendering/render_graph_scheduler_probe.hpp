#pragma once

#include <vanguard/rendering/render_node_graph.hpp>

namespace vanguard::rendering
{
    class NodeGraphFactory;
    struct RenderNodeImplContext;
    struct RenderFlowResourceFailure;

    // Explicitly opt-in Profile-only representative renderer probe. Normal Profile
    // runs must measure the real renderer and therefore leave this disabled unless
    // VANGUARD_RENDER_GRAPH_SCHEDULER_PROBE=1 is present in the process environment.
    [[nodiscard]] bool IsRenderGraphSchedulerProbeEnabled() noexcept;
    void AppendRenderGraphSchedulerProbe(NodeGraphFactory& factory, RenderNodeGraph::ItemId startRender,
                                         RenderNodeGraph::ItemId endRender) noexcept;
    [[nodiscard]] bool UseRenderGraphSchedulerProbeResources(const RenderNodeImplContext& context, u32 phase,
                                                             RenderFlowResourceFailure* failure) noexcept;
} // namespace vanguard::rendering
