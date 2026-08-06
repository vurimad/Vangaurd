#pragma once

#include <vanguard/engine/engine_services.hpp>

namespace vanguard::engine
{
    using FrameParticipantId = u64;

    inline constexpr FrameParticipantId InvalidFrameParticipantId = 0;
    inline constexpr u32 MaximumFrameParticipants = 256;
    inline constexpr u32 MaximumFrameDependencies = 16;

    /// Global order is an engine contract. Participants may refine ordering only within this order.
    enum class FramePhase : u8
    {
        PlatformEvents,
        Input,
        BeginFrame,
        PreSimulation,
        FixedSimulation,
        Simulation,
        WorldStreaming,
        PostSimulation,
        Presentation,
        Render,
        EndFrame,
        Count
    };

    enum class FramePipelineState : u8
    {
        Building,
        Compiled,
        Executing,
        Failed
    };

    enum class FrameFailureCode : u8
    {
        None,
        InvalidConfiguration,
        InvalidDescriptor,
        LimitExceeded,
        DuplicateParticipant,
        UnknownDependency,
        DependencyOnLaterPhase,
        DependencyCycle,
        UnsupportedAffinity,
        InvalidState,
        ClockFailure,
        ParticipantFailure
    };

    struct FrameFailure
    {
        FrameFailureCode code = FrameFailureCode::None;
        u64 frame = 0;
        FramePhase phase = FramePhase::PlatformEvents;
        FrameParticipantId participant = InvalidFrameParticipantId;
        FrameParticipantId relatedParticipant = InvalidFrameParticipantId;
        const char* participantName = nullptr;
        const char* message = nullptr;
    };

    struct FrameContext
    {
        u64 frame = 0;
        f64 realTimeSeconds = 0.0;
        f64 simulationTimeSeconds = 0.0;
        f64 fixedTimeSeconds = 0.0;
        f32 rawDeltaSeconds = 0.0f;
        f32 realDeltaSeconds = 0.0f;
        f32 simulationDeltaSeconds = 0.0f;
        f32 fixedDeltaSeconds = 0.0f;
        f32 interpolationAlpha = 0.0f;
        u32 fixedStep = 0;
        u32 fixedStepCount = 0;
        bool paused = false;
    };

    enum class FrameParticipantResult : u8
    {
        Success,
        Failure
    };

    struct FrameParticipantStatus
    {
        FrameParticipantResult result = FrameParticipantResult::Success;
        const char* message = nullptr;

        [[nodiscard]] static constexpr FrameParticipantStatus Success() noexcept { return {}; }
        [[nodiscard]] static constexpr FrameParticipantStatus Failure(const char* const message) noexcept
        {
            return {FrameParticipantResult::Failure, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return result == FrameParticipantResult::Success;
        }
    };

    using ExecuteFrameParticipant = FrameParticipantStatus (*)(const FrameContext& context, void* userData) noexcept;
    using ReadFrameClock = u64 (*)(void* userData) noexcept;

    struct FrameParticipantDescriptor
    {
        /// The descriptor is copied during registration, including the dependency IDs. The participant name,
        /// callback, and user-data target remain caller-owned and must outlive the Frame Pipeline service.
        FrameParticipantId id = InvalidFrameParticipantId;
        const char* name = nullptr;
        FramePhase phase = FramePhase::Simulation;
        application::ApplicationProfile profiles = application::ApplicationProfile::All;
        application::ThreadAffinity affinity = application::ThreadAffinity::MainThread;
        containers::ArraySpan<const FrameParticipantId> after;
        ExecuteFrameParticipant execute = nullptr;
        void* userData = nullptr;
    };

    struct FrameClock
    {
        ReadFrameClock read = nullptr;
        u64 frequency = 0;
        void* userData = nullptr;
    };

    enum class FramePacingMode : u8
    {
        Disabled,
        CpuTarget,
        Presentation
    };

    struct FramePipelineConfig
    {
        FrameClock clock;
        f32 maximumDeltaSeconds = 0.1f;
        f32 fixedDeltaSeconds = 1.0f / 60.0f;
        f32 timeScale = 1.0f;
        u32 maximumFixedStepsPerFrame = 4;
        FramePacingMode pacing = FramePacingMode::CpuTarget;
        f32 targetFramesPerSecond = 60.0f;
    };

    struct FramePipelineStats
    {
        u64 frames = 0;
        u64 fixedSteps = 0;
        f64 realTimeSeconds = 0.0;
        f64 simulationTimeSeconds = 0.0;
        f64 fixedTimeSeconds = 0.0;
        f64 droppedSimulationSeconds = 0.0;
        f64 discardedRealTimeSeconds = 0.0;
        f32 lastRawDeltaSeconds = 0.0f;
        f32 lastRealDeltaSeconds = 0.0f;
        f32 lastSimulationDeltaSeconds = 0.0f;
        f32 lastExecutionSeconds = 0.0f;
        u32 lastFixedSteps = 0;
        u32 registeredParticipants = 0;
        u32 scheduledParticipants = 0;
        f32 lastPhaseSeconds[static_cast<u32>(FramePhase::Count)]{};
        FramePipelineState state = FramePipelineState::Building;
        bool paused = false;
    };

    struct FrameParticipantStats
    {
        FrameParticipantId id = InvalidFrameParticipantId;
        const char* name = nullptr;
        FramePhase phase = FramePhase::Simulation;
        u64 invocations = 0;
        f64 totalSeconds = 0.0;
        f32 lastFrameSeconds = 0.0f;
        f32 maximumSeconds = 0.0f;
        u32 lastFrameInvocations = 0;
    };

    using FrameParticipantVisitor = void (*)(const FrameParticipantStats& participant, void* userData) noexcept;

    class FramePipelineService : public application::Service
    {
    public:
        ~FramePipelineService() override = default;

        [[nodiscard]] virtual bool Configure(const FramePipelineConfig& config,
                                             FrameFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RegisterParticipant(const FrameParticipantDescriptor& descriptor,
                                                       FrameFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool Compile(FrameFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RunFrame(FrameFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual bool SetPaused(bool paused) noexcept = 0;
        [[nodiscard]] virtual bool SetTimeScale(f32 scale) noexcept = 0;
        [[nodiscard]] virtual FramePipelineState State() const noexcept = 0;
        [[nodiscard]] virtual FramePipelineStats GetStats() const noexcept = 0;
        virtual void VisitParticipantStats(FrameParticipantVisitor visitor, void* userData = nullptr) const noexcept = 0;
        [[nodiscard]] virtual const FrameFailure& LastFailure() const noexcept = 0;

    protected:
        FramePipelineService() noexcept = default;
    };

    [[nodiscard]] FramePipelineService* FindFramePipelineService(application::EngineHost& host) noexcept;
    [[nodiscard]] FramePipelineService* FindFramePipelineService(application::ServiceContext& context) noexcept;
}
