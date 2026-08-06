#include <vanguard/engine/frame_pipeline_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/system/time.hpp>

#include <cmath>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;

    engine::FramePipelineService* g_activeFramePipeline = nullptr;

    [[nodiscard]] vanguard::u64 CurrentFrameNumber() noexcept
    {
        return g_activeFramePipeline != nullptr ? g_activeFramePipeline->GetStats().frames : 0;
    }

    [[nodiscard]] vanguard::u64 ReadDefaultClock(void*) noexcept
    {
        return vanguard::system::MonotonicTicks();
    }

    [[nodiscard]] bool ValidPhase(const engine::FramePhase phase) noexcept
    {
        return static_cast<vanguard::u32>(phase) < static_cast<vanguard::u32>(engine::FramePhase::Count);
    }

    [[nodiscard]] bool ValidConfig(const engine::FramePipelineConfig& config, const bool customClock) noexcept
    {
        if (config.clock.read == nullptr || config.clock.frequency == 0 ||
            !std::isfinite(config.maximumDeltaSeconds) || config.maximumDeltaSeconds <= 0.0f ||
            !std::isfinite(config.fixedDeltaSeconds) || config.fixedDeltaSeconds <= 0.0f ||
            !std::isfinite(config.timeScale) || config.timeScale < 0.0f || config.maximumFixedStepsPerFrame == 0)
            return false;
        if (config.pacing == engine::FramePacingMode::CpuTarget &&
            (!std::isfinite(config.targetFramesPerSecond) || config.targetFramesPerSecond <= 0.0f || customClock))
            return false;
        return static_cast<vanguard::u32>(config.pacing) <=
               static_cast<vanguard::u32>(engine::FramePacingMode::Presentation);
    }

    class ManagedFramePipelineService final : public engine::FramePipelineService
    {
    public:
        struct ParticipantRecord
        {
            engine::FrameParticipantDescriptor descriptor;
            engine::FrameParticipantStats stats;
            engine::FrameParticipantId dependencies[engine::MaximumFrameDependencies]{};
            vanguard::u32 dependencyCount = 0;
        };

        ManagedFramePipelineService() noexcept
            : m_participants(vanguard::memory::pools::Engine::GetInstance()),
              m_schedule(vanguard::memory::pools::Engine::GetInstance())
        {
            m_participants.Reserve(engine::MaximumFrameParticipants);
            m_schedule.Reserve(engine::MaximumFrameParticipants);
        }

        [[nodiscard]] bool Configure(const engine::FramePipelineConfig& requested,
                                     engine::FrameFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_state != engine::FramePipelineState::Building)
                return Fail(failure, engine::FrameFailureCode::InvalidState, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "frame pipeline configuration is immutable after schedule compilation");

            engine::FramePipelineConfig config = requested;
            const bool customClock = config.clock.read != nullptr;
            if (!customClock)
            {
                config.clock.read = ReadDefaultClock;
                config.clock.frequency = vanguard::system::MonotonicFrequency();
                config.clock.userData = nullptr;
            }
            if (!ValidConfig(config, customClock))
                return Fail(failure, engine::FrameFailureCode::InvalidConfiguration,
                            engine::FramePhase::PlatformEvents, engine::InvalidFrameParticipantId,
                            engine::InvalidFrameParticipantId, "invalid frame timing or pacing configuration");
            m_config = config;
            m_customClock = customClock;
            m_configured = true;
            m_stats.paused = m_paused;
            return true;
        }

        [[nodiscard]] bool RegisterParticipant(const engine::FrameParticipantDescriptor& descriptor,
                                               engine::FrameFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_state != engine::FramePipelineState::Building)
                return Fail(failure, engine::FrameFailureCode::InvalidState, descriptor.phase, descriptor.id,
                            engine::InvalidFrameParticipantId,
                            "frame participants can be registered only before schedule compilation");
            if (descriptor.id == engine::InvalidFrameParticipantId || descriptor.name == nullptr ||
                descriptor.name[0] == '\0' || !ValidPhase(descriptor.phase) || descriptor.execute == nullptr ||
                descriptor.profiles == app::ApplicationProfile::None ||
                descriptor.after.Size() > engine::MaximumFrameDependencies)
                return Fail(failure, engine::FrameFailureCode::InvalidDescriptor, descriptor.phase, descriptor.id,
                            engine::InvalidFrameParticipantId, "invalid frame participant descriptor");
            if (descriptor.affinity != app::ThreadAffinity::MainThread)
                return Fail(failure, engine::FrameFailureCode::UnsupportedAffinity, descriptor.phase, descriptor.id,
                            engine::InvalidFrameParticipantId,
                            "only explicit main-thread frame participants are supported in this scheduler revision");
            if (m_participants.Size() >= engine::MaximumFrameParticipants)
                return Fail(failure, engine::FrameFailureCode::LimitExceeded, descriptor.phase, descriptor.id,
                            engine::InvalidFrameParticipantId, "maximum frame participant count exceeded");
            for (const ParticipantRecord& record : m_participants)
                if (record.descriptor.id == descriptor.id)
                    return Fail(failure, engine::FrameFailureCode::DuplicateParticipant, descriptor.phase,
                                descriptor.id, descriptor.id, "duplicate frame participant identifier");

            ParticipantRecord record;
            record.descriptor = descriptor;
            record.descriptor.after = {};
            record.stats.id = descriptor.id;
            record.stats.name = descriptor.name;
            record.stats.phase = descriptor.phase;
            record.dependencyCount = descriptor.after.Size();
            for (vanguard::u32 index = 0; index < record.dependencyCount; ++index)
            {
                const engine::FrameParticipantId dependency = descriptor.after[index];
                if (dependency == engine::InvalidFrameParticipantId || dependency == descriptor.id)
                    return Fail(failure, engine::FrameFailureCode::InvalidDescriptor, descriptor.phase,
                                descriptor.id, dependency, "invalid frame participant dependency");
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (record.dependencies[previous] == dependency)
                        return Fail(failure, engine::FrameFailureCode::InvalidDescriptor, descriptor.phase,
                                    descriptor.id, dependency, "duplicate frame participant dependency");
                record.dependencies[index] = dependency;
            }
            const vanguard::u32 expected = m_participants.Size() + 1u;
            m_participants.PushBack(record);
            m_stats.registeredParticipants = m_participants.Size();
            return m_participants.Size() == expected;
        }

        [[nodiscard]] bool Compile(engine::FrameFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_state == engine::FramePipelineState::Compiled) return true;
            if (m_state != engine::FramePipelineState::Building)
                return Fail(failure, engine::FrameFailureCode::InvalidState, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "frame pipeline cannot compile in its current state");
            if (!m_configured)
            {
                engine::FramePipelineConfig defaults;
                if (!Configure(defaults, failure)) return false;
            }

            m_schedule.Clear();
            for (vanguard::u32 phaseIndex = 0; phaseIndex < static_cast<vanguard::u32>(engine::FramePhase::Count);
                 ++phaseIndex)
                m_phaseOffsets[phaseIndex] = 0;

            bool scheduled[engine::MaximumFrameParticipants]{};
            vanguard::u32 activeCount = 0;
            for (vanguard::u32 index = 0; index < m_participants.Size(); ++index)
            {
                const ParticipantRecord& record = m_participants[index];
                if (!app::HasProfile(record.descriptor.profiles, m_profile)) continue;
                ++activeCount;
                for (vanguard::u32 dependencyIndex = 0; dependencyIndex < record.dependencyCount; ++dependencyIndex)
                {
                    const engine::FrameParticipantId dependencyId = record.dependencies[dependencyIndex];
                    const ParticipantRecord* const dependency = FindParticipant(dependencyId);
                    if (dependency == nullptr || !app::HasProfile(dependency->descriptor.profiles, m_profile))
                        return Fail(failure, engine::FrameFailureCode::UnknownDependency, record.descriptor.phase,
                                    record.descriptor.id, dependencyId,
                                    "frame participant dependency is absent from the active profile");
                    if (static_cast<vanguard::u32>(dependency->descriptor.phase) >
                        static_cast<vanguard::u32>(record.descriptor.phase))
                        return Fail(failure, engine::FrameFailureCode::DependencyOnLaterPhase,
                                    record.descriptor.phase, record.descriptor.id, dependencyId,
                                    "frame participant depends on a later global phase");
                }
            }

            for (vanguard::u32 phaseIndex = 0; phaseIndex < static_cast<vanguard::u32>(engine::FramePhase::Count);
                 ++phaseIndex)
            {
                m_phaseOffsets[phaseIndex] = m_schedule.Size();
                const engine::FramePhase phase = static_cast<engine::FramePhase>(phaseIndex);
                while (true)
                {
                    vanguard::u32 candidate = engine::MaximumFrameParticipants;
                    engine::FrameParticipantId candidateId = ~engine::FrameParticipantId{0};
                    bool phaseHasUnscheduled = false;
                    for (vanguard::u32 index = 0; index < m_participants.Size(); ++index)
                    {
                        const ParticipantRecord& record = m_participants[index];
                        if (scheduled[index] || record.descriptor.phase != phase ||
                            !app::HasProfile(record.descriptor.profiles, m_profile))
                            continue;
                        phaseHasUnscheduled = true;
                        if (!DependenciesScheduled(record, scheduled)) continue;
                        if (record.descriptor.id < candidateId)
                        {
                            candidate = index;
                            candidateId = record.descriptor.id;
                        }
                    }
                    if (candidate == engine::MaximumFrameParticipants)
                    {
                        if (phaseHasUnscheduled)
                            return Fail(failure, engine::FrameFailureCode::DependencyCycle, phase,
                                        engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                                        "frame participant dependency cycle detected");
                        break;
                    }
                    m_schedule.PushBack(static_cast<vanguard::u16>(candidate));
                    scheduled[candidate] = true;
                }
            }
            m_phaseOffsets[static_cast<vanguard::u32>(engine::FramePhase::Count)] = m_schedule.Size();
            if (m_schedule.Size() != activeCount)
                return Fail(failure, engine::FrameFailureCode::DependencyCycle, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "frame schedule compilation did not consume every active participant");

            m_lastTick = m_config.clock.read(m_config.clock.userData);
            m_state = engine::FramePipelineState::Compiled;
            m_stats.scheduledParticipants = m_schedule.Size();
            m_stats.state = m_state;
            m_lastFailure = {};
            return true;
        }

        [[nodiscard]] bool RunFrame(engine::FrameFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_state != engine::FramePipelineState::Compiled || !vanguard::concurrency::IsMainThread())
                return Fail(failure, engine::FrameFailureCode::InvalidState, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "compiled frame pipeline must execute on the main thread");

            const vanguard::u64 frameStartTick = m_config.clock.read(m_config.clock.userData);
            if (frameStartTick < m_lastTick)
            {
                m_state = engine::FramePipelineState::Failed;
                m_stats.state = m_state;
                return Fail(failure, engine::FrameFailureCode::ClockFailure, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "monotonic frame clock moved backwards");
            }
            const vanguard::f64 rawDelta = static_cast<vanguard::f64>(frameStartTick - m_lastTick) /
                                           static_cast<vanguard::f64>(m_config.clock.frequency);
            if (!std::isfinite(rawDelta))
            {
                m_state = engine::FramePipelineState::Failed;
                m_stats.state = m_state;
                return Fail(failure, engine::FrameFailureCode::ClockFailure, engine::FramePhase::PlatformEvents,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "frame clock produced a non-finite delta");
            }
            m_lastTick = frameStartTick;

            if (m_pauseChangePending)
            {
                m_paused = m_pendingPaused;
                m_stats.paused = m_paused;
                m_pauseChangePending = false;
            }
            if (m_timeScaleChangePending)
            {
                m_config.timeScale = m_pendingTimeScale;
                m_timeScaleChangePending = false;
            }

            const vanguard::f32 rawDeltaSeconds = static_cast<vanguard::f32>(rawDelta);
            const vanguard::f32 realDeltaSeconds = rawDeltaSeconds < m_config.maximumDeltaSeconds
                ? rawDeltaSeconds : m_config.maximumDeltaSeconds;
            if (rawDelta > m_config.maximumDeltaSeconds)
                m_stats.discardedRealTimeSeconds += rawDelta - m_config.maximumDeltaSeconds;
            const vanguard::f32 simulationDeltaSeconds = m_paused ? 0.0f : realDeltaSeconds * m_config.timeScale;
            m_fixedAccumulator += simulationDeltaSeconds;
            vanguard::u32 fixedStepCount = static_cast<vanguard::u32>(
                m_fixedAccumulator / static_cast<vanguard::f64>(m_config.fixedDeltaSeconds));
            if (fixedStepCount > m_config.maximumFixedStepsPerFrame)
                fixedStepCount = m_config.maximumFixedStepsPerFrame;
            m_fixedAccumulator -= static_cast<vanguard::f64>(fixedStepCount) * m_config.fixedDeltaSeconds;
            if (m_fixedAccumulator >= m_config.fixedDeltaSeconds)
            {
                const vanguard::u64 droppedSteps = static_cast<vanguard::u64>(
                    m_fixedAccumulator / static_cast<vanguard::f64>(m_config.fixedDeltaSeconds));
                const vanguard::f64 dropped = static_cast<vanguard::f64>(droppedSteps) * m_config.fixedDeltaSeconds;
                m_fixedAccumulator -= dropped;
                m_stats.droppedSimulationSeconds += dropped;
            }

            engine::FrameContext context;
            context.frame = m_stats.frames;
            context.realTimeSeconds = m_stats.realTimeSeconds + realDeltaSeconds;
            context.simulationTimeSeconds = m_stats.simulationTimeSeconds + simulationDeltaSeconds;
            context.fixedTimeSeconds = m_stats.fixedTimeSeconds;
            context.rawDeltaSeconds = rawDeltaSeconds;
            context.realDeltaSeconds = realDeltaSeconds;
            context.simulationDeltaSeconds = simulationDeltaSeconds;
            context.fixedDeltaSeconds = m_config.fixedDeltaSeconds;
            context.interpolationAlpha = static_cast<vanguard::f32>(
                m_fixedAccumulator / static_cast<vanguard::f64>(m_config.fixedDeltaSeconds));
            context.fixedStepCount = fixedStepCount;
            context.paused = m_paused;

            for (vanguard::u32 phaseIndex = 0; phaseIndex < static_cast<vanguard::u32>(engine::FramePhase::Count);
                 ++phaseIndex)
                m_stats.lastPhaseSeconds[phaseIndex] = 0.0f;
            for (ParticipantRecord& participant : m_participants)
            {
                participant.stats.lastFrameSeconds = 0.0f;
                participant.stats.lastFrameInvocations = 0;
            }

            m_state = engine::FramePipelineState::Executing;
            bool succeeded = true;
            for (vanguard::u32 phaseIndex = 0; phaseIndex < static_cast<vanguard::u32>(engine::FramePhase::Count);
                 ++phaseIndex)
            {
                const engine::FramePhase phase = static_cast<engine::FramePhase>(phaseIndex);
                if (phase == engine::FramePhase::FixedSimulation)
                {
                    const vanguard::f32 variableDelta = context.simulationDeltaSeconds;
                    context.simulationDeltaSeconds = context.fixedDeltaSeconds;
                    for (vanguard::u32 step = 0; step < fixedStepCount && succeeded; ++step)
                    {
                        context.fixedStep = step;
                        context.fixedTimeSeconds = m_stats.fixedTimeSeconds +
                                                   static_cast<vanguard::f64>(step + 1u) * context.fixedDeltaSeconds;
                        succeeded = ExecutePhase(phase, context, failure);
                    }
                    context.simulationDeltaSeconds = variableDelta;
                }
                else
                {
                    succeeded = ExecutePhase(phase, context, failure);
                }
                if (!succeeded) break;
            }

            vanguard::memory::ResetFramePools();
            vanguard::memory::PrepareMetricsForNextFrame();
            if (!succeeded)
            {
                m_state = engine::FramePipelineState::Failed;
                m_stats.state = m_state;
                return false;
            }

            const vanguard::u64 executionEndTick = m_config.clock.read(m_config.clock.userData);
            if (executionEndTick < frameStartTick)
            {
                m_state = engine::FramePipelineState::Failed;
                m_stats.state = m_state;
                return Fail(failure, engine::FrameFailureCode::ClockFailure, engine::FramePhase::EndFrame,
                            engine::InvalidFrameParticipantId, engine::InvalidFrameParticipantId,
                            "monotonic clock moved backwards during frame execution");
            }
            m_stats.lastExecutionSeconds = static_cast<vanguard::f32>(
                static_cast<vanguard::f64>(executionEndTick - frameStartTick) / m_config.clock.frequency);
            m_stats.lastRawDeltaSeconds = rawDeltaSeconds;
            m_stats.lastRealDeltaSeconds = realDeltaSeconds;
            m_stats.lastSimulationDeltaSeconds = simulationDeltaSeconds;
            m_stats.lastFixedSteps = fixedStepCount;
            m_stats.fixedSteps += fixedStepCount;
            m_stats.realTimeSeconds += realDeltaSeconds;
            m_stats.simulationTimeSeconds += simulationDeltaSeconds;
            m_stats.fixedTimeSeconds += static_cast<vanguard::f64>(fixedStepCount) * m_config.fixedDeltaSeconds;
            ++m_stats.frames;
            m_state = engine::FramePipelineState::Compiled;
            m_stats.state = m_state;
            PaceFrame(frameStartTick);
            return true;
        }

        [[nodiscard]] bool SetPaused(const bool paused) noexcept override
        {
            if (m_state == engine::FramePipelineState::Failed) return false;
            if (m_state == engine::FramePipelineState::Executing)
            {
                m_pendingPaused = paused;
                m_pauseChangePending = true;
                return true;
            }
            m_paused = paused;
            m_stats.paused = paused;
            return true;
        }

        [[nodiscard]] bool SetTimeScale(const vanguard::f32 scale) noexcept override
        {
            if (m_state == engine::FramePipelineState::Failed || !std::isfinite(scale) || scale < 0.0f)
                return false;
            if (m_state == engine::FramePipelineState::Executing)
            {
                m_pendingTimeScale = scale;
                m_timeScaleChangePending = true;
                return true;
            }
            m_config.timeScale = scale;
            return true;
        }

        [[nodiscard]] engine::FramePipelineState State() const noexcept override { return m_state; }
        [[nodiscard]] engine::FramePipelineStats GetStats() const noexcept override { return m_stats; }
        void VisitParticipantStats(const engine::FrameParticipantVisitor visitor, void* const userData) const noexcept override
        {
            if (visitor == nullptr) return;
            for (const ParticipantRecord& participant : m_participants)
                if (app::HasProfile(participant.descriptor.profiles, m_profile)) visitor(participant.stats, userData);
        }
        [[nodiscard]] const engine::FrameFailure& LastFailure() const noexcept override { return m_lastFailure; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_profile = context.Profile();
            engine::FramePipelineConfig defaults;
            engine::FrameFailure failure;
            return Configure(defaults, &failure)
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                           : "frame pipeline configuration failed");
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return m_state != engine::FramePipelineState::Executing
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("frame pipeline is executing during quiesce");
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (g_activeFramePipeline != nullptr)
                return app::LifecycleStatus::Failure("another frame pipeline is already active");
            g_activeFramePipeline = this;
            vanguard::diagnostics::SetFrameNumberRetriever(CurrentFrameNumber);
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_state == engine::FramePipelineState::Executing)
                return app::LifecycleStatus::Failure("frame pipeline is executing during shutdown");
            if (g_activeFramePipeline == this)
            {
                vanguard::diagnostics::SetFrameNumberRetriever(nullptr);
                g_activeFramePipeline = nullptr;
            }
            m_schedule.Clear();
            m_participants.Clear();
            m_state = engine::FramePipelineState::Building;
            m_stats.state = m_state;
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] const ParticipantRecord* FindParticipant(const engine::FrameParticipantId id) const noexcept
        {
            for (const ParticipantRecord& record : m_participants)
                if (record.descriptor.id == id) return &record;
            return nullptr;
        }

        [[nodiscard]] bool DependenciesScheduled(const ParticipantRecord& record,
                                                 const bool* const scheduled) const noexcept
        {
            for (vanguard::u32 dependencyIndex = 0; dependencyIndex < record.dependencyCount; ++dependencyIndex)
            {
                const engine::FrameParticipantId dependencyId = record.dependencies[dependencyIndex];
                for (vanguard::u32 recordIndex = 0; recordIndex < m_participants.Size(); ++recordIndex)
                {
                    const ParticipantRecord& dependency = m_participants[recordIndex];
                    if (dependency.descriptor.id != dependencyId) continue;
                    if (dependency.descriptor.phase == record.descriptor.phase && !scheduled[recordIndex]) return false;
                    break;
                }
            }
            return true;
        }

        [[nodiscard]] bool ExecutePhase(const engine::FramePhase phase, const engine::FrameContext& context,
                                        engine::FrameFailure* const failure) noexcept
        {
            const vanguard::u32 phaseIndex = static_cast<vanguard::u32>(phase);
            for (vanguard::u32 index = m_phaseOffsets[phaseIndex]; index < m_phaseOffsets[phaseIndex + 1u]; ++index)
            {
                ParticipantRecord& participant = m_participants[m_schedule[index]];
                const vanguard::u64 beginTick = m_config.clock.read(m_config.clock.userData);
                const engine::FrameParticipantStatus status =
                    participant.descriptor.execute(context, participant.descriptor.userData);
                const vanguard::u64 endTick = m_config.clock.read(m_config.clock.userData);
                if (endTick < beginTick)
                    return Fail(failure, engine::FrameFailureCode::ClockFailure, phase,
                                participant.descriptor.id, engine::InvalidFrameParticipantId,
                                "monotonic clock moved backwards while timing a frame participant",
                                participant.descriptor.name);
                const vanguard::f32 elapsed = static_cast<vanguard::f32>(
                    static_cast<vanguard::f64>(endTick - beginTick) / m_config.clock.frequency);
                participant.stats.lastFrameSeconds += elapsed;
                participant.stats.totalSeconds += elapsed;
                participant.stats.maximumSeconds = elapsed > participant.stats.maximumSeconds
                    ? elapsed : participant.stats.maximumSeconds;
                ++participant.stats.lastFrameInvocations;
                ++participant.stats.invocations;
                m_stats.lastPhaseSeconds[phaseIndex] += elapsed;
                if (status) continue;
                return Fail(failure, engine::FrameFailureCode::ParticipantFailure, phase,
                            participant.descriptor.id, engine::InvalidFrameParticipantId,
                            status.message != nullptr ? status.message : "frame participant failed",
                            participant.descriptor.name);
            }
            return true;
        }

        void PaceFrame(const vanguard::u64 frameStartTick) noexcept
        {
            if (m_config.pacing != engine::FramePacingMode::CpuTarget || m_customClock) return;
            const vanguard::f64 targetSeconds = 1.0 / m_config.targetFramesPerSecond;
            const vanguard::u64 targetTicks = frameStartTick + static_cast<vanguard::u64>(
                targetSeconds * static_cast<vanguard::f64>(m_config.clock.frequency));
            vanguard::u64 current = m_config.clock.read(m_config.clock.userData);
            if (current >= targetTicks) return;
            const vanguard::f64 remainingSeconds = static_cast<vanguard::f64>(targetTicks - current) /
                                                   static_cast<vanguard::f64>(m_config.clock.frequency);
            if (remainingSeconds > 0.002)
            {
                const vanguard::u32 sleepMilliseconds =
                    static_cast<vanguard::u32>((remainingSeconds - 0.001) * 1000.0);
                if (sleepMilliseconds != 0) vanguard::concurrency::SleepOnCurrentThread(sleepMilliseconds);
            }
            do
            {
                vanguard::concurrency::YieldCurrentThread();
                current = m_config.clock.read(m_config.clock.userData);
            } while (current < targetTicks);
        }

        void ClearOutput(engine::FrameFailure* const failure) const noexcept
        {
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(engine::FrameFailure* const output, const engine::FrameFailureCode code,
                                const engine::FramePhase phase, const engine::FrameParticipantId participant,
                                const engine::FrameParticipantId related, const char* const message,
                                const char* const name = nullptr) noexcept
        {
            m_lastFailure = {code, m_stats.frames, phase, participant, related, name, message};
            if (output != nullptr) *output = m_lastFailure;
            return false;
        }

        vanguard::containers::DynamicArray<ParticipantRecord> m_participants;
        vanguard::containers::DynamicArray<vanguard::u16> m_schedule;
        vanguard::u32 m_phaseOffsets[static_cast<vanguard::u32>(engine::FramePhase::Count) + 1u]{};
        engine::FramePipelineConfig m_config;
        engine::FramePipelineStats m_stats;
        engine::FrameFailure m_lastFailure;
        app::ApplicationProfile m_profile = app::ApplicationProfile::None;
        engine::FramePipelineState m_state = engine::FramePipelineState::Building;
        vanguard::u64 m_lastTick = 0;
        vanguard::f64 m_fixedAccumulator = 0.0;
        bool m_configured = false;
        bool m_customClock = false;
        bool m_paused = false;
        bool m_pendingPaused = false;
        bool m_pauseChangePending = false;
        bool m_timeScaleChangePending = false;
        vanguard::f32 m_pendingTimeScale = 1.0f;
    };

    app::Service* CreateFramePipelineService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Engine, sizeof(ManagedFramePipelineService), alignof(ManagedFramePipelineService));
        return block ? ::new (block.address) ManagedFramePipelineService() : nullptr;
    }

    void DestroyFramePipelineService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedFramePipelineService*>(service)->~ManagedFramePipelineService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedFramePipelineService), vanguard::memory::PoolId::Engine};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterFramePipelineService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {JobsServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{FramePipelineCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = FramePipelineServiceId;
        descriptor.name = "framePipeline";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor |
                              application::ApplicationProfile::Tool | application::ApplicationProfile::Server |
                              application::ApplicationProfile::Headless | application::ApplicationProfile::Test;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 1};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateFramePipelineService;
        descriptor.destroy = DestroyFramePipelineService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    FramePipelineService* FindFramePipelineService(application::EngineHost& host) noexcept
    {
        return static_cast<FramePipelineService*>(host.FindCapability(FramePipelineCapabilityId));
    }

    FramePipelineService* FindFramePipelineService(application::ServiceContext& context) noexcept
    {
        return static_cast<FramePipelineService*>(context.FindCapability(FramePipelineCapabilityId));
    }
}
