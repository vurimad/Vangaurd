#pragma once

#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rhi
{
    inline constexpr u32 MaximumGpuCounterBufferedFrames = 16;
    inline constexpr u32 MaximumGpuCounterScopeNameLength = 64;
    inline constexpr u32 InvalidGpuCounterIndex = 0xffffffffu;

    struct GpuCounterScopeId
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const GpuCounterScopeId&, const GpuCounterScopeId&) noexcept = default;
    };

    [[nodiscard]] constexpr GpuCounterScopeId MakeGpuCounterScopeId(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return {};
        u64 hash = 1469598103934665603ull;
        for (const char* character = name; *character != '\0'; ++character)
        {
            hash ^= static_cast<u8>(*character);
            hash *= 1099511628211ull;
        }
        return {hash != 0 ? hash : 1};
    }

    struct GpuCounterFrameHandle
    {
        u32 index = InvalidGpuCounterIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGpuCounterIndex && generation != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const GpuCounterFrameHandle&, const GpuCounterFrameHandle&) noexcept = default;
    };

    struct GpuCounterScopeToken
    {
        GpuCounterFrameHandle frame;
        CommandListRef commandList;
        u32 sampleIndex = InvalidGpuCounterIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return frame.IsValid() && commandList.IsValid() && sampleIndex != InvalidGpuCounterIndex;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
    };

    enum class GpuCounterFrameState : u8
    {
        Empty,
        Recording,
        AwaitingSubmission,
        PendingGpu,
        Ready,
        Failed
    };

    struct GpuCounterConfig
    {
        u32 bufferedFrames = 8;
        u32 maximumScopesPerFrame = 512;
        u32 maximumRegisteredScopes = 256;
        QueueType queue = QueueType::Graphics;
        bool collectPipelineStatistics = false;
    };

    struct GpuCounterSample
    {
        GpuCounterScopeId scope;
        u64 gpuBegin = 0;
        u64 gpuEnd = 0;
        u64 cpuBegin = 0;
        u64 cpuEnd = 0;
        PipelineStatistics pipelineStatistics;
        u32 cpuThread = 0;
        bool valid = false;
    };

    struct GpuCounterFrameView
    {
        GpuCounterFrameHandle handle;
        const GpuCounterSample* samples = nullptr;
        TimestampCalibration calibration;
        GpuFence completion;
        u64 frameNumber = 0;
        u64 gpuBegin = 0;
        u64 gpuEnd = 0;
        u32 sampleCount = 0;
        u32 droppedSamples = 0;
        QueueType queue = QueueType::Graphics;
        bool hasPipelineStatistics = false;
    };

    struct GpuCounterStats
    {
        u32 registeredScopes = 0;
        u32 recordingFrames = 0;
        u32 awaitingSubmissionFrames = 0;
        u32 pendingGpuFrames = 0;
        u32 readyFrames = 0;
        u64 recordedFrames = 0;
        u64 collectedFrames = 0;
        u64 consumedFrames = 0;
        u64 discardedFrames = 0;
        u64 droppedSamples = 0;
        u64 frameAllocationFailures = 0;
        u64 recordingFailures = 0;
    };

    [[nodiscard]] constexpr u64 GpuCounterTicksToNanoseconds(const u64 ticks, const u64 frequency) noexcept
    {
        return frequency == 0 ? 0 : (ticks / frequency) * 1'000'000'000ull + ((ticks % frequency) * 1'000'000'000ull) / frequency;
    }

    class GpuCounterSystem final
    {
    public:
        GpuCounterSystem() noexcept = default;
        ~GpuCounterSystem();
        GpuCounterSystem(const GpuCounterSystem&) = delete;
        GpuCounterSystem& operator=(const GpuCounterSystem&) = delete;

        [[nodiscard]] bool Initialize(const GpuCounterConfig& config = {}, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterScope(GpuCounterScopeId scope, const char* name, Failure* failure = nullptr) noexcept;
        [[nodiscard]] const char* GetScopeName(GpuCounterScopeId scope) const noexcept;

        [[nodiscard]] bool BeginFrame(u64 frameNumber, GpuCounterFrameHandle& frame, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginScope(GpuCounterFrameHandle frame, GpuCounterScopeId scope, GpuCounterScopeToken& token, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndScope(GpuCounterScopeToken& token, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndFrame(GpuCounterFrameHandle frame, Failure* failure = nullptr) noexcept;

        // The fence must be the actual completion returned by the submission containing EndFrame's resolve commands.
        [[nodiscard]] bool CommitFrame(GpuCounterFrameHandle frame, GpuFence completion, Failure* failure = nullptr) noexcept;
        // Discard is valid only when the associated recording work was not submitted, or after a ready/failed result is abandoned.
        [[nodiscard]] bool DiscardFrame(GpuCounterFrameHandle frame, Failure* failure = nullptr) noexcept;

        // Collection polls fences and maps only completed query ranges; it never waits for the GPU.
        [[nodiscard]] u32 Collect(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetOldestReadyFrame(GpuCounterFrameView& frame, Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ConsumeFrame(GpuCounterFrameHandle frame, Failure* failure = nullptr) noexcept;
        [[nodiscard]] GpuCounterFrameState GetFrameState(GpuCounterFrameHandle frame) const noexcept;
        [[nodiscard]] GpuCounterStats GetStats() const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rhi
