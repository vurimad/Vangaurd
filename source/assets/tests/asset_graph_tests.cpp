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
        Cancellation
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
        u8 rootBytes[1] = {0x10};
        u8 leftBytes[1] = {0x20};
        u8 rightBytes[1] = {0x30};
        u8 sharedBytes[1] = {0x40};
        u8 generation = 1;
        Mode mode = Mode::Success;
        concurrency::Atomic<u32> completedMask;
        concurrency::Atomic<u32> compileCalls;
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
        return collector.Add({identity, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
               assets::Result::Success;
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
        return source == fixture.sharedSource;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& fixture = *static_cast<GraphFixture*>(userData);
        static_cast<void>(fixture.compileCalls.Increment());
        const resources::ResourceReference source = context.request.source.identity;
        u32 completionBit = 0;
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
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, payload,
                          sizeof(payload)) == assets::Result::Success;
    }

    [[nodiscard]] assets::BuildRequest MakeRequest(const resources::ResourceReference source, const resources::ResourceReference output,
                                                   const u8* const bytes, GraphFixture& fixture) noexcept
    {
        return {{source, {bytes, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {&fixture.generation, 1}};
    }

    [[nodiscard]] bool ResolveGenerated(const assets::BuildDependency& dependency, assets::BuildRequest& request,
                                        void* const userData) noexcept
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

    assets::BuildSystem buildSystem;
    Check(buildSystem.Initialize(), "build-system initialization");
    const assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.graph_test"), "assets.graph_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::BuildGraph graph;
    Check(graph.Initialize(buildSystem, &ResolveGenerated, &fixture), "build-graph initialization");

    const assets::BuildRequest rootRequest = MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture);
    assets::GraphRequest first = graph.Request(rootRequest, assets::BuildPriority::High);
    assets::GraphRequest second = graph.Request(rootRequest, assets::BuildPriority::Normal);
    Check(first && second && first.IsSameOperation(second), "identical roots coalesce");
    first.Wait();
    second.Wait();
    assets::BuildOutput output;
    Check(first.HasSucceeded() && second.HasSucceeded() && first.CopyOutput(output) && output.artifacts.Size() == 1 &&
              fixture.compileCalls.GetValue() == 4 && fixture.completedMask.GetValue() == 0x0fu,
          "diamond graph builds in dependency order");
    assets::BuildGraphStats stats = graph.GetStats();
    Check(stats.knownOperations == 4 && stats.dependencyEdges == 4 && stats.issuedRequests == 2 && stats.coalescedRequests >= 2 &&
              stats.completedOperations == 4,
          "graph coalescing and edge telemetry");
    Check(!graph.Shutdown(), "shutdown refuses live requests");
    first.Reset();
    second.Reset();

    fixture.mode = Mode::Failure;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    const u32 failureCallsBefore = fixture.compileCalls.GetValue();
    assets::GraphRequest failed = graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture));
    failed.Wait();
    Check(failed.Status() == assets::BuildState::Failed && failed.Error() == assets::BuildFailure::DependencyFailed &&
              fixture.compileCalls.GetValue() == failureCallsBefore + 1,
          "required dependency failure prevents dependants");
    failed.Reset();

    fixture.mode = Mode::Cycle;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    assets::GraphRequest cycle = graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture));
    cycle.Wait();
    Check(cycle.Status() == assets::BuildState::Failed && cycle.Error() == assets::BuildFailure::DependencyCycle,
          "dependency cycle fails during resolution");
    cycle.Reset();

    fixture.mode = Mode::Cancellation;
    ++fixture.generation;
    fixture.completedMask.SetValue(0);
    assets::GraphRequest cancelled =
        graph.Request(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootBytes, fixture), assets::BuildPriority::Background);
    Check(cancelled.Cancel(), "explicit cancellation accepted");
    cancelled.Wait();
    Check(cancelled.Status() == assets::BuildState::Cancelled && cancelled.Error() == assets::BuildFailure::Cancelled,
          "cancellation propagates through dependency graph");

    stats = graph.GetStats();
    Check(stats.externalRequests == 0 && stats.activeOperations == 0 && stats.failedOperations >= 3 && stats.cancelledOperations >= 1,
          "terminal graph telemetry");
    Check(graph.Shutdown(), "build-graph shutdown");
    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    Check(jobs::Shutdown(), "jobs shutdown");
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
