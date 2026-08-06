#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    struct TestClock
    {
        vanguard::u64 ticks = 1;
    };

    [[nodiscard]] vanguard::u64 ReadTestClock(void* const userData) noexcept
    {
        return static_cast<TestClock*>(userData)->ticks;
    }

    [[nodiscard]] vanguard::engine::FrameParticipantStatus ExecuteParticipant(
        const vanguard::engine::FrameContext&, void*) noexcept
    {
        return vanguard::engine::FrameParticipantStatus::Success();
    }

    [[nodiscard]] bool Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return true;
        std::fprintf(stderr, "[framePipelineCycleTests] FAILED: %s\n", message);
        return false;
    }
}

int main()
{
    if (!Check(vanguard::memory::Initialize(), "memory initialization") ||
        !Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous,
                                                 "framePipelineCycleTests"),
               "diagnostics initialization") ||
        !Check(vanguard::containers::Initialize(), "containers initialization"))
        return 1;

    vanguard::application::EngineHost host;
    vanguard::application::HostFailure hostFailure;
    if (!Check(vanguard::engine::RegisterEngineModule(host, &hostFailure) &&
                   vanguard::engine::RegisterIoService(host, &hostFailure) &&
                   vanguard::engine::RegisterFilesystemService(host, &hostFailure) &&
                   vanguard::engine::RegisterJobsService(host, &hostFailure) &&
                   vanguard::engine::RegisterFramePipelineService(host, &hostFailure) &&
                   host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure) &&
                   host.Start(&hostFailure),
               "Frame Pipeline host startup"))
    {
        vanguard::diagnostics::Shutdown();
        return 1;
    }

    vanguard::engine::FramePipelineService* const pipeline = vanguard::engine::FindFramePipelineService(host);
    TestClock clock;
    vanguard::engine::FramePipelineConfig config;
    config.clock = {&ReadTestClock, 1'000, &clock};
    config.pacing = vanguard::engine::FramePacingMode::Disabled;
    vanguard::engine::FrameFailure failure;
    bool passed = Check(pipeline != nullptr && pipeline->Configure(config, &failure),
                        "deterministic Frame Pipeline configuration");

    constexpr vanguard::engine::FrameParticipantId FirstParticipant = 1;
    constexpr vanguard::engine::FrameParticipantId SecondParticipant = 2;
    const vanguard::engine::FrameParticipantId firstAfter[]{SecondParticipant};
    const vanguard::engine::FrameParticipantId secondAfter[]{FirstParticipant};
    vanguard::engine::FrameParticipantDescriptor first;
    first.id = FirstParticipant;
    first.name = "cycleFirst";
    first.after = {firstAfter, 1};
    first.execute = &ExecuteParticipant;
    vanguard::engine::FrameParticipantDescriptor second;
    second.id = SecondParticipant;
    second.name = "cycleSecond";
    second.after = {secondAfter, 1};
    second.execute = &ExecuteParticipant;
    passed = Check(pipeline != nullptr && pipeline->RegisterParticipant(first, &failure) &&
                       pipeline->RegisterParticipant(second, &failure) && !pipeline->Compile(&failure) &&
                       failure.code == vanguard::engine::FrameFailureCode::DependencyCycle,
                   "same-phase dependency cycle rejection") && passed;
    passed = Check(host.Shutdown(&hostFailure), "Frame Pipeline host shutdown") && passed;
    vanguard::diagnostics::Shutdown();
    return passed ? 0 : 1;
}
