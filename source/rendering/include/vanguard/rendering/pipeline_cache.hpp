#pragma once

#include <vanguard/pipeline_cache/pipeline_cache.hpp>
#include <vanguard/rhi/rhi.hpp>

namespace vanguard::rendering
{
    class PipelineCache;

    class PipelineRequest final
    {
    public:
        PipelineRequest() noexcept = default;
        PipelineRequest(PipelineRequest&&) noexcept = default;
        PipelineRequest& operator=(PipelineRequest&&) noexcept = default;
        ~PipelineRequest() = default;

        PipelineRequest(const PipelineRequest&) = delete;
        PipelineRequest& operator=(const PipelineRequest&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] pipeline_cache::State GetStatus() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool HasSucceeded() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] rhi::PipelineRef GetPipeline() const noexcept;
        [[nodiscard]] pipeline_cache::FailureEvidence GetError() const noexcept;
        [[nodiscard]] u64 GetGeneration() const noexcept;
        void Reset() noexcept;

    private:
        pipeline_cache::PipelineRequest m_request;
        friend class PipelineCache;
    };

    class PipelineCache final
    {
    public:
        PipelineCache() noexcept = default;
        ~PipelineCache() = default;
        PipelineCache(const PipelineCache&) = delete;
        PipelineCache& operator=(const PipelineCache&) = delete;

        [[nodiscard]] bool Initialize(const pipeline_cache::Config& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] pipeline_cache::Result RequestGraphics(const crypto::Digest256& concreteKey, const rhi::GraphicsPipelineDesc& desc,
                                                             PipelineRequest& output,
                                                             pipeline_cache::Priority priority = pipeline_cache::Priority::Normal) noexcept;
        [[nodiscard]] pipeline_cache::Result RequestCompute(const crypto::Digest256& concreteKey, const rhi::ComputePipelineDesc& desc, PipelineRequest& output,
                                                            pipeline_cache::Priority priority = pipeline_cache::Priority::Normal) noexcept;
        [[nodiscard]] pipeline_cache::Result RequestRayTracing(const crypto::Digest256& concreteKey, const rhi::RayTracingPipelineDesc& desc,
                                                               PipelineRequest& output,
                                                               pipeline_cache::Priority priority = pipeline_cache::Priority::Normal) noexcept;

        [[nodiscard]] bool Invalidate(const crypto::Digest256& concreteKey) noexcept;
        [[nodiscard]] u32 InvalidateAll() noexcept;
        void WaitIdle() const noexcept;
        [[nodiscard]] bool TryWaitIdle(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] pipeline_cache::Stats GetStats() const noexcept;

    private:
        pipeline_cache::PipelineCache m_cache;
    };
} // namespace vanguard::rendering
