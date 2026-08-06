#include <vanguard/assets/asset_recooker.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/jobs/jobs.hpp>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x52535243u;
    constexpr resources::ResourceTypeId OutputType = 0x524f5554u;

    struct Fixture
    {
        resources::ResourceReference sharedSource;
        resources::ResourceReference leftSource;
        resources::ResourceReference rootSource;
        resources::ResourceReference sharedOutput;
        resources::ResourceReference leftOutput;
        resources::ResourceReference rootOutput;
        u8 sharedByte = 1;
        u8 leftByte = 2;
        u8 rootByte = 3;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<bool> sharedCompileStarted;
        bool failShared = false;
        bool failRoot = false;
        bool blockShared = false;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetRecookerTests] FAILED: %s", message);
            ++g_failures;
        }
    }

    [[nodiscard]] resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }

    [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& collector, void* const userData) noexcept
    {
        const auto& fixture = *static_cast<Fixture*>(userData);
        if (request.source.identity == fixture.leftSource)
        {
            return collector.Add({fixture.sharedOutput, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success;
        }
        if (request.source.identity == fixture.rootSource)
        {
            return collector.Add({fixture.leftOutput, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success;
        }
        return request.source.identity == fixture.sharedSource;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& fixture = *static_cast<Fixture*>(userData);
        static_cast<void>(fixture.compileCalls.Increment());
        if (context.request.source.identity == fixture.sharedSource)
        {
            fixture.sharedCompileStarted.SetValue(true);
            if (fixture.blockShared)
            {
                while (!context.IsCancellationRequested())
                {
                    concurrency::YieldCurrentThread();
                }
                return false;
            }
            if (fixture.failShared)
            {
                return false;
            }
        }
        if (context.request.source.identity == fixture.rootSource && fixture.failRoot)
        {
            return false;
        }
        const u8 bytes[] = {context.request.source.content[0], static_cast<u8>(context.dependencies.Count())};
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes,
                          sizeof(bytes)) == assets::Result::Success;
    }

    [[nodiscard]] assets::BuildRequest MakeRequest(const resources::ResourceReference source, const resources::ResourceReference output,
                                                   const u8& byte) noexcept
    {
        return {{source, {&byte, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};
    }

    [[nodiscard]] bool ResolveOutput(const resources::ResourceReference output, assets::BuildRequest& request,
                                     void* const userData) noexcept
    {
        auto& fixture = *static_cast<Fixture*>(userData);
        if (output == fixture.sharedOutput)
        {
            request = MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedByte);
            return true;
        }
        if (output == fixture.leftOutput)
        {
            request = MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte);
            return true;
        }
        if (output == fixture.rootOutput)
        {
            request = MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootByte);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ResolveGenerated(const assets::BuildDependency& dependency, assets::BuildRequest& request,
                                        void* const userData) noexcept
    {
        return ResolveOutput(dependency.identity, request, userData);
    }

    [[nodiscard]] bool BuildAndPublish(assets::BuildSystem& system, assets::DependencyIndex& index, const assets::BuildRequest& request,
                                       const resources::ResourceReference dependency = {},
                                       const assets::BuildFingerprint& dependencyContent = {}) noexcept
    {
        assets::BuildPlan plan;
        if (system.Prepare(request, plan) != assets::Result::Success)
        {
            return false;
        }
        if (dependency.IsValid() && plan.SetGeneratedDependencyContent(dependency, dependencyContent) != assets::Result::Success)
        {
            return false;
        }
        assets::BuildOutput output;
        return system.Execute(request, plan, output) == assets::Result::Success &&
               index.Publish(request, plan, output) == assets::IndexResult::Success;
    }

    void DeleteIndexFiles(filesystem::Manager& manager, const filesystem::AbsolutePath& root) noexcept
    {
        for (const char* const name : {"asset-dependencies.vadi", "asset-dependencies.vadi.tmp"})
        {
            const filesystem::AbsolutePath path = root.AddFilePath(name);
            if (manager.FileExist(path))
            {
                static_cast<void>(manager.DeleteFile(path));
            }
        }
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetRecookerTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "io initialization");
    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_asset_recooker_tests");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    filesystem::Manager& manager = filesystem::GetManager();
    DeleteIndexFiles(manager, root);
    Check(manager.CreatePath(root), "recooker test root");

    Fixture fixture;
    fixture.sharedSource = Reference("source/recook/shared.asset", SourceType);
    fixture.leftSource = Reference("source/recook/left.asset", SourceType);
    fixture.rootSource = Reference("source/recook/root.asset", SourceType);
    fixture.sharedOutput = Reference("cooked/recook/shared.asset", OutputType);
    fixture.leftOutput = Reference("cooked/recook/left.asset", OutputType);
    fixture.rootOutput = Reference("cooked/recook/root.asset", OutputType);

    assets::BuildSystem buildSystem;
    Check(buildSystem.Initialize(), "build-system initialization");
    const assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.recooker_test"), "assets.recooker_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = root.AsChar();
    indexConfig.settingsFingerprint = crypto::Sha256("recooker-settings", 17);
    assets::DependencyIndex index;
    Check(index.Initialize(indexConfig) == assets::IndexResult::Success, "index initialization");

    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedByte)),
          "baseline shared publication");
    assets::DependencyRecord sharedRecord;
    Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success, "baseline shared lookup");
    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte), fixture.sharedOutput,
                          sharedRecord.contentFingerprint),
          "baseline left publication");
    assets::DependencyRecord leftRecord;
    Check(index.Find(fixture.leftOutput, leftRecord) == assets::IndexResult::Success, "baseline left lookup");
    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootByte), fixture.leftOutput,
                          leftRecord.contentFingerprint),
          "baseline root publication");
    Check(index.Save() == assets::IndexResult::Success, "baseline index save");

    assets::BuildGraph graph;
    Check(graph.Initialize(buildSystem, &ResolveGenerated, &fixture, {}, &index), "indexed graph initialization");
    assets::IncrementalRecooker recooker;
    Check(recooker.Initialize(buildSystem, graph, index, &ResolveOutput, &fixture), "incremental recooker initialization");

    const assets::AssetChange sharedChange{fixture.sharedSource};
    {
        const u32 callsBefore = fixture.compileCalls.GetValue();
        assets::RecookBatch unchanged = recooker.Request({&sharedChange, 1});
        Check(unchanged && unchanged.HasFinished() && unchanged.Status() == assets::RecookState::Succeeded &&
                  unchanged.GetStats().unchangedChanges == 1 && unchanged.GetStats().rootRequests == 0 &&
                  fixture.compileCalls.GetValue() == callsBefore && !index.HasActiveTransaction(),
              "unchanged source event performs no work");
        unchanged.Reset();
    }

    assets::BuildFingerprint committedSharedContent = sharedRecord.contentFingerprint;
    const assets::BuildFingerprint committedLeftContent = leftRecord.contentFingerprint;
    {
        fixture.sharedByte = 9;
        fixture.sharedCompileStarted.SetValue(false);
        const u32 callsBefore = fixture.compileCalls.GetValue();
        assets::RecookBatch rebuilt = recooker.Request({&sharedChange, 1}, assets::BuildPriority::High);
        rebuilt.Wait();
        const assets::RecookStats stats = rebuilt.GetStats();
        Check(rebuilt.Status() == assets::RecookState::Succeeded, "changed leaf batch succeeds");
        Check(stats.dirtySeeds == 1, "changed leaf contributes one dirty seed");
        Check(stats.affectedOutputs == 3, "changed leaf expands through all transitive outputs");
        Check(stats.rootRequests == 1, "changed leaf schedules one minimized root");
        Check(stats.succeededRoots == 1, "changed leaf root succeeds");
        Check(fixture.compileCalls.GetValue() == callsBefore + 2,
              "changed leaf traverses the full graph while preserving a content-identical root DDC hit");
        Check(!index.HasActiveTransaction(), "successful recook closes the index transaction");
        Check(!index.HasChanges(), "successful recook atomically persists the index");
        Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success &&
                  sharedRecord.contentFingerprint != committedSharedContent,
              "successful batch publishes new dependency state");
        committedSharedContent = sharedRecord.contentFingerprint;
        rebuilt.Reset();
    }

    {
        fixture.sharedByte = 10;
        fixture.leftByte = 20;
        fixture.failRoot = true;
        assets::RecookBatch failed = recooker.Request({&sharedChange, 1});
        failed.Wait();
        assets::DependencyRecord afterFailure;
        assets::DependencyRecord leftAfterFailure;
        Check(failed.Status() == assets::RecookState::Failed && failed.Error() == assets::RecookFailure::BuildFailed &&
                  index.Find(fixture.sharedOutput, afterFailure) == assets::IndexResult::Success &&
                  afterFailure.contentFingerprint == committedSharedContent &&
                  index.Find(fixture.leftOutput, leftAfterFailure) == assets::IndexResult::Success &&
                  leftAfterFailure.contentFingerprint == committedLeftContent && !index.HasActiveTransaction() && !index.HasChanges(),
              "failed root rolls back successful staged dependency publications");
        failed.Reset();
        fixture.failRoot = false;
    }

    {
        fixture.sharedByte = 11;
        fixture.blockShared = true;
        fixture.sharedCompileStarted.SetValue(false);
        assets::RecookBatch cancelled = recooker.Request({&sharedChange, 1}, assets::BuildPriority::Background);
        while (!fixture.sharedCompileStarted.GetValue() && !cancelled.HasFinished())
        {
            concurrency::YieldCurrentThread();
        }
        assets::DependencyRecord isolated;
        Check(index.HasActiveTransaction() && index.Find(fixture.sharedOutput, isolated) == assets::IndexResult::Success &&
                  isolated.contentFingerprint == committedSharedContent,
              "active transaction hides staged records from readers");
        Check(cancelled.Cancel(), "batch cancellation accepted");
        cancelled.Wait();
        Check(cancelled.Status() == assets::RecookState::Cancelled && cancelled.Error() == assets::RecookFailure::Cancelled &&
                  !index.HasActiveTransaction() && !index.HasChanges(),
              "cancelled recook rolls back transaction");
        cancelled.Reset();
        fixture.blockShared = false;
    }

    {
        const assets::AssetChange unknown{Reference("source/recook/untracked.asset", SourceType)};
        assets::RecookBatch untracked = recooker.Request({&unknown, 1});
        Check(untracked && untracked.HasFinished() && untracked.Status() == assets::RecookState::Failed &&
                  untracked.Error() == assets::RecookFailure::UntrackedChange,
              "untracked source change fails explicitly");
        untracked.Reset();
    }

    Check(recooker.GetStats().completedBatches == 2 && recooker.GetStats().failedBatches == 2 && recooker.GetStats().cancelledBatches == 1,
          "recook lifetime telemetry");
    Check(recooker.Shutdown(), "recooker shutdown");
    Check(graph.Shutdown(), "graph shutdown");
    Check(index.Shutdown(), "index shutdown");

    assets::DependencyIndex restarted;
    Check(restarted.Initialize(indexConfig) == assets::IndexResult::Success, "transactional index restart");
    assets::DependencyRecord persistedShared;
    Check(restarted.Find(fixture.sharedOutput, persistedShared) == assets::IndexResult::Success &&
              persistedShared.contentFingerprint == committedSharedContent,
          "only successful recook transaction survives restart");
    Check(restarted.Shutdown(), "restarted index shutdown");

    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    DeleteIndexFiles(manager, root);
    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
