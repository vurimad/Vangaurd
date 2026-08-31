#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/jobs/jobs.hpp>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x47535243u;
    constexpr resources::ResourceTypeId OutputType = 0x474f5554u;

    enum class Mode : u8
    {
        Success,
        Failure,
        Cycle,
        Cancellation,
        Admission
    };

    struct GraphFixture
    {
        resources::ResourceReference rootSource;
        resources::ResourceReference leftSource;
        resources::ResourceReference rightSource;
        resources::ResourceReference sharedSource;
        resources::ResourceReference rootOutput;
        resources::ResourceReference leftOutput;
        resources::ResourceReference rightOutput;
        resources::ResourceReference sharedOutput;
        resources::ResourceReference admissionSourceA;
        resources::ResourceReference admissionSourceB;
        resources::ResourceReference oversizedSource;
        resources::ResourceReference admissionOutputA;
        resources::ResourceReference admissionOutputB;
        resources::ResourceReference oversizedOutput;
        u8 rootBytes[1] = {0x10};
        u8 leftBytes[1] = {0x20};
        u8 rightBytes[1] = {0x30};
        u8 sharedBytes[1] = {0x40};
        u8 generation = 1;
        Mode mode = Mode::Success;
        concurrency::Atomic<u32> completedMask;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<u32> admissionStarts;
        concurrency::Atomic<bool> releaseAdmission;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetGraphTests] FAILED: %s", message);
            ++g_failures;
        }
    }

    [[nodiscard]] resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }

    [[nodiscard]] bool AddGenerated(assets::DependencyCollector& collector, const resources::ResourceReference identity) noexcept
    {
        return collector.Add({identity, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) == assets::Result::Success;
    }

    [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& collector, void* const userData) noexcept
    {
        const auto& fixture = *static_cast<GraphFixture*>(userData);
        const resources::ResourceReference source = request.source.identity;
        if (source == fixture.rootSource)
        {
            return AddGenerated(collector, fixture.leftOutput) && AddGenerated(collector, fixture.rightOutput);
        }
        if (source == fixture.leftSource)
        {
            if (fixture.mode == Mode::Cycle)
            {
                return AddGenerated(collector, fixture.rootOutput);
            }
            return AddGenerated(collector, fixture.sharedOutput);
        }
        if (source == fixture.rightSource)
        {
            return AddGenerated(collector, fixture.sharedOutput);
        }
        return source == fixture.sharedSource || source == fixture.admissionSourceA || source == fixture.admissionSourceB ||
               source == fixture.oversizedSource;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& fixture = *static_cast<GraphFixture*>(userData);
        static_cast<void>(fixture.compileCalls.Increment());
        const resources::ResourceReference source = context.request.source.identity;
        u32 completionBit = 0;
        if (source == fixture.admissionSourceA || source == fixture.admissionSourceB)
        {
            static_cast<void>(fixture.admissionStarts.Increment());
            while (!fixture.releaseAdmission.GetValue() && !context.IsCancellationRequested())
            {
                concurrency::YieldCurrentThread();
            }
            if (context.IsCancellationRequested())
            {
                return false;
            }
            const u8 payload[] = {context.request.source.content[0], fixture.generation,
                                  static_cast<u8>(context.dependencies.Count())};
            return writer.Add(context.request.output, 0,
                              assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, payload,
                              sizeof(payload)) == assets::Result::Success;
        }
        if (source == fixture.sharedSource)
        {
            if (fixture.mode == Mode::Failure)
            {
                return false;
            }
            if (fixture.mode == Mode::Cancellation)
            {
                while (!context.IsCancellationRequested())
                {
                    concurrency::YieldCurrentThread();
                }
                return false;
            }
            completionBit = 1u << 0u;
        }
        else if (source == fixture.leftSource)
        {
            if ((fixture.completedMask.GetValue() & 1u) == 0)
            {
                return false;
            }
            completionBit = 1u << 1u;
        }
        else if (source == fixture.rightSource)
        {
            if ((fixture.completedMask.GetValue() & 1u) == 0)
            {
                return false;
            }
            completionBit = 1u << 2u;
        }
        else if (source == fixture.rootSource)
        {
            if ((fixture.completedMask.GetValue() & 0x6u) != 0x6u)
            {
                return false;
            }
            completionBit = 1u << 3u;
        }
        else
        {
            return false;
        }

        static_cast<void>(fixture.completedMask.Or(completionBit));
        const u8 payload[] = {context.request.source.content[0], fixture.generation, static_cast<u8>(context.dependencies.Count())};
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, payload, sizeof(payload)) ==
               assets::Result::Success;
    }

    [[nodiscard]] bool Estimate(const assets::BuildRequest& request, const containers::ArraySpan<const assets::BuildDependency>,
                                assets::BuildResourceEstimate& estimate, void* const userData) noexcept
    {
        const auto& fixture = *static_cast<GraphFixture*>(userData);
        if (request.source.identity == fixture.admissionSourceA || request.source.identity == fixture.admissionSourceB)
        {
            estimate = {50, 10};
        }
        else if (request.source.identity == fixture.oversizedSource)
        {
            estimate = {100, 1};
        }
        else
        {
            estimate = {1, 3};
        }
        return true;
    }

    [[nodiscard]] assets::BuildRequest MakeRequest(const resources::ResourceReference source, const resources::ResourceReference output, const u8* const bytes,
                                                   GraphFixture& fixture) noexcept
    {
        return {{source, {bytes, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {&fixture.generation, 1}};
    }

    [[nodiscard]] bool ResolveGenerated(const assets::BuildDependency& dependency, assets::BuildRequest& request, void* const userData) noexcept
    {
        auto& fixture = *static_cast<GraphFixture*>(userData);
        if (dependency.identity == fixture.rootOutput)
        {
            request = MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture);
            return true;
        }
        if (dependency.identity == fixture.leftOutput)
        {
            request = MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftBytes, fixture);
            return true;
        }
        if (dependency.identity == fixture.rightOutput)
        {
            request = MakeRequest(fixture.rightSource, fixture.rightOutput, fixture.rightBytes, fixture);
            return true;
        }
        if (dependency.identity == fixture.sharedOutput)
        {
            request = MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedBytes, fixture);
            return true;
        }
        return false;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetGraphTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    GraphFixture fixture;
    fixture.rootSource = Reference("source/graph/root.asset", SourceType);
    fixture.leftSource = Reference("source/graph/left.asset", SourceType);
    fixture.rightSource = Reference("source/graph/right.asset", SourceType);
    fixture.sharedSource = Reference("source/graph/shared.asset", SourceType);
    fixture.rootOutput = Reference("cooked/graph/root.asset", OutputType);
    fixture.leftOutput = Reference("cooked/graph/left.asset", OutputType);
    fixture.rightOutput = Reference("cooked/graph/right.asset", OutputType);
    fixture.sharedOutput = Reference("cooked/graph/shared.asset", OutputType);
    fixture.admissionSourceA = Reference("source/graph/admission-a.asset", SourceType);
    fixture.admissionSourceB = Reference("source/graph/admission-b.asset", SourceType);
    fixture.oversizedSource = Reference("source/graph/oversized.asset", SourceType);
    fixture.admissionOutputA = Reference("cooked/graph/admission-a.asset", OutputType);
    fixture.admissionOutputB = Reference("cooked/graph/admission-b.asset", OutputType);
    fixture.oversizedOutput = Reference("cooked/graph/oversized.asset", OutputType);

    assets::BuildSystem buildSystem;
    Check(buildSystem.Initialize(), "build-system initialization");
    const assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.graph_test"), "assets.graph_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture, &Estimate};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::BuildGraph graph;
    assets::BuildGraphConfig graphConfig;
    graphConfig.maximumActiveExecutionBytes = 60;
    graphConfig.maximumQueuedRequestBytes = 8;
    Check(graph.Initialize(buildSystem, &ResolveGenerated, &fixture, graphConfig), "build-graph initialization");

    const assets::BuildRequest rootRequest = MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture);
    assets::GraphRequest first = graph.Request(rootRequest, assets::BuildPriority::High);
    assets::GraphRequest second = graph.Request(rootRequest, assets::BuildPriority::Normal);
    Check(first && second && first.IsSameOperation(second), "identical roots coalesce");
    first.Wait();
    second.Wait();
    assets::BuildOutput output;
    Check(first.HasSucceeded() && second.HasSucceeded() && first.CopyOutput(output) && output.artifacts.Size() == 1 && fixture.compileCalls.GetValue() == 4 &&
              fixture.completedMask.GetValue() == 0x0fu,
          "diamond graph builds in dependency order");
    assets::BuildGraphStats stats = graph.GetStats();
    Check(stats.knownOperations == 4 && stats.dependencyEdges == 4 && stats.issuedRequests == 2 && stats.coalescedRequests >= 2 &&
              stats.completedOperations == 4 && stats.queuedRequestBytes == 0 && stats.peakQueuedRequestBytes == 8 &&
              stats.peakActiveExecutionBytes <= graphConfig.maximumActiveExecutionBytes && stats.retainedOutputBytes == 3,
          "graph coalescing and edge telemetry");
    Check(!graph.Shutdown(), "shutdown refuses live requests");
    first.Reset();
    second.Reset();
    Check(graph.GetStats().retainedOutputBytes == 0, "last external interest releases retained root artifacts");

    fixture.mode = Mode::Admission;
    ++fixture.generation;
    fixture.releaseAdmission.SetValue(false);
    fixture.admissionStarts.SetValue(0);
    assets::GraphRequest admissionA =
        graph.Request(MakeRequest(fixture.admissionSourceA, fixture.admissionOutputA, fixture.leftBytes, fixture), assets::BuildPriority::Low);
    assets::GraphRequest admissionB =
        graph.Request(MakeRequest(fixture.admissionSourceB, fixture.admissionOutputB, fixture.rightBytes, fixture), assets::BuildPriority::High);
    for (u32 attempt = 0; attempt < 10000 &&
                                (fixture.admissionStarts.GetValue() != 1 || graph.GetStats().waitingForAdmission != 1);
         ++attempt)
    {
        concurrency::YieldCurrentThread();
    }
    stats = graph.GetStats();
    Check(fixture.admissionStarts.GetValue() == 1 && stats.waitingForAdmission == 1 && stats.activeExecutionBytes == 60 &&
              stats.peakActiveExecutionBytes == 60,
          "byte admission runs one fitting compiler and queues the other without blocking a worker");
    fixture.releaseAdmission.SetValue(true);
    admissionA.Wait();
    admissionB.Wait();
    Check(admissionA.HasSucceeded() && admissionB.HasSucceeded() && fixture.admissionStarts.GetValue() == 2 &&
              graph.GetStats().activeExecutionBytes == 0 && graph.GetStats().waitingForAdmission == 0,
          "released execution bytes dispatch the next priority/FIFO waiter");
    admissionA.Reset();
    admissionB.Reset();
    Check(graph.GetStats().retainedOutputBytes == 0, "admission roots release outputs after their handles reset");

    assets::GraphRequest oversized =
        graph.Request(MakeRequest(fixture.oversizedSource, fixture.oversizedOutput, fixture.sharedBytes, fixture));
    oversized.Wait();
    Check(oversized.GetStatus() == assets::BuildState::Failed && oversized.GetError() == assets::BuildFailure::LimitExceeded &&
              oversized.BuildError() == assets::Result::LimitExceeded,
          "one build larger than the execution budget fails instead of bypassing admission");
    oversized.Reset();

    const u8 queuedOverflowBytes[9]{};
    const resources::ResourceReference queuedOverflowSource = Reference("source/graph/queued-overflow.asset", SourceType);
    const resources::ResourceReference queuedOverflowOutput = Reference("cooked/graph/queued-overflow.asset", OutputType);
    const assets::BuildRequest queuedOverflowRequest{
        {queuedOverflowSource, {queuedOverflowBytes, sizeof(queuedOverflowBytes)}, {}}, queuedOverflowOutput, assets::TargetPlatform::WindowsD3D12,
        {&fixture.generation, 1}};
    Check(!graph.Request(queuedOverflowRequest), "queued request bytes are rejected before OwnedBuildRequest copies them");

    fixture.mode = Mode::Failure;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    const u32 failureCallsBefore = fixture.compileCalls.GetValue();
    assets::GraphRequest failed = graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture));
    failed.Wait();
    Check(failed.GetStatus() == assets::BuildState::Failed && failed.GetError() == assets::BuildFailure::DependencyFailed &&
              fixture.compileCalls.GetValue() == failureCallsBefore + 1,
          "required dependency failure prevents dependants");
    failed.Reset();

    fixture.mode = Mode::Cycle;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    assets::GraphRequest cycle = graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture));
    cycle.Wait();
    Check(cycle.GetStatus() == assets::BuildState::Failed && cycle.GetError() == assets::BuildFailure::DependencyCycle, "dependency cycle fails during resolution");
    cycle.Reset();

    fixture.mode = Mode::Cancellation;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    assets::GraphRequest cancelled =
        graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture), assets::BuildPriority::Background);
    Check(cancelled.Cancel(), "explicit cancellation accepted");
    cancelled.Wait();
    Check(cancelled.GetStatus() == assets::BuildState::Cancelled && cancelled.GetError() == assets::BuildFailure::Cancelled,
          "cancellation propagates through dependency graph");

    stats = graph.GetStats();
    Check(stats.externalRequests == 0 && stats.activeOperations == 0 && stats.failedOperations >= 3 && stats.cancelledOperations >= 1,
          "terminal graph telemetry");
    Check(graph.Shutdown(), "build-graph shutdown");
    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");

    assets::CompilerDescriptor unestimatedCompiler = compiler;
    unestimatedCompiler.estimateResources = nullptr;
    Check(buildSystem.RegisterCompiler(unestimatedCompiler) == assets::Result::Success, "unestimated compiler registration for direct-build compatibility");
    assets::BuildGraph guardedGraph;
    Check(guardedGraph.Initialize(buildSystem, &ResolveGenerated, &fixture), "guarded graph initialization");
    assets::GraphRequest unestimated = guardedGraph.Request(rootRequest);
    unestimated.Wait();
    Check(unestimated.GetStatus() == assets::BuildState::Failed && unestimated.GetError() == assets::BuildFailure::ResolutionFailed &&
              unestimated.BuildError() == assets::Result::ResourceEstimationFailed,
          "asynchronous graph execution rejects a compiler with no resource estimate");
    unestimated.Reset();
    Check(guardedGraph.Shutdown(), "guarded graph shutdown");
    Check(buildSystem.UnregisterCompiler(unestimatedCompiler.id) == assets::Result::Success, "unestimated compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    Check(jobs::Shutdown(), "jobs shutdown");
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
