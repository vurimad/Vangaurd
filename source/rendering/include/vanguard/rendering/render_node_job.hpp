#pragma once

namespace vanguard::jobs
{
    class Counter;
    struct JobContext;
    class Task;
}

namespace vanguard::rendering
{
    class RenderFrameInfo;
    class RenderNodeGraph;
    struct RenderNodeImplContext;
    class RenderNodeResourceBindings;
    struct RenderFlowResourceFailure;

    class RenderNodeJob final
    {
    public:
        static void SetJobsRenderFrame(const RenderFrameInfo* frame) noexcept;
        static void ClearJobsRenderFrame(const RenderFrameInfo* expectedFrame) noexcept;
        [[nodiscard]] static const RenderFrameInfo* GetJobsRenderFrame() noexcept;
        [[nodiscard]] static bool RunRenderNodeJobs(const RenderNodeGraph& graph, const RenderNodeImplContext& baseContext,
                                                    RenderNodeResourceBindings& resourceBindings, const jobs::Counter& kickoff,
                                                    const jobs::JobContext& setupContext, jobs::Task&& completion,
                                                    RenderFlowResourceFailure* failure = nullptr) noexcept;
    };
} // namespace vanguard::rendering
