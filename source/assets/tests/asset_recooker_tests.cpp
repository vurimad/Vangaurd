#include "../../../tests/support/scratch_directory.hpp"
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
        resources::ResourceReference firstSource;
        resources::ResourceReference sharedOutput;
        resources::ResourceReference leftOutput;
        resources::ResourceReference rootOutput;
        resources::ResourceReference firstOutput;
        u8 sharedByte = 1;
        u8 leftByte = 2;
        u8 rootByte = 3;
        u8 firstByte = 4;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<bool> sharedCompileStarted;
        bool failShared = false;
        bool failRoot = false;
        bool blockShared = false;
        bool historicalRetired = false;
    };

    struct ValidationProbe
    {
        assets::DependencyIndex* index = nullptr;
        resources::ResourceReference output;
        assets::BuildFingerprint committedContent;
        u32 calls = 0;
        u32 recordCount = 0;
        bool sawCommittedReaderState = false;
        bool reject = false;
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
        return request.source.identity == fixture.sharedSource || request.source.identity == fixture.firstSource;
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
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes, sizeof(bytes)) ==
               assets::Result::Success;
    }

    [[nodiscard]] bool Estimate(const assets::BuildRequest&, const containers::ArraySpan<const assets::BuildDependency>,
                                assets::BuildResourceEstimate& estimate, void*) noexcept
    {
        estimate = {1, 2};
        return true;
    }

    [[nodiscard]] assets::BuildRequest MakeRequest(const resources::ResourceReference source, const resources::ResourceReference output,
                                                   const u8& byte) noexcept
    {
        return {{source, {&byte, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};
    }

    [[nodiscard]] bool ResolveOutput(const resources::ResourceReference output, assets::OwnedBuildRequest& request, void* const userData) noexcept
    {
        auto& fixture = *static_cast<Fixture*>(userData);
        if (fixture.historicalRetired &&
            (output == fixture.sharedOutput || output == fixture.leftOutput || output == fixture.rootOutput))
            return false;
        if (output == fixture.sharedOutput)
        {
            return request.CopyFrom(MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedByte));
        }
        if (output == fixture.leftOutput)
        {
            return request.CopyFrom(MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte));
        }
        if (output == fixture.rootOutput)
        {
            return request.CopyFrom(MakeRequest(fixture.rootSource, fixture.rootOutput, fixture.rootByte));
        }
        if (output == fixture.firstOutput)
        {
            return request.CopyFrom(MakeRequest(fixture.firstSource, fixture.firstOutput, fixture.firstByte));
        }
        return false;
    }

    [[nodiscard]] bool ConfirmHistoricalOutput(const resources::ResourceReference output, void* const userData) noexcept
    {
        const auto& fixture = *static_cast<Fixture*>(userData);
        return fixture.historicalRetired &&
               (output == fixture.sharedOutput || output == fixture.leftOutput || output == fixture.rootOutput);
    }

    [[nodiscard]] bool ResolveGenerated(const assets::BuildDependency& dependency, assets::OwnedBuildRequest& request, void* const userData) noexcept
    {
        return ResolveOutput(dependency.identity, request, userData);
    }

    [[nodiscard]] assets::RecookPreCommitValidationResult ValidatePreCommit(
        const containers::ArraySpan<const assets::DependencyRecord> records, void* const userData) noexcept
    {
        auto& probe = *static_cast<ValidationProbe*>(userData);
        ++probe.calls;
        probe.recordCount = records.Count();
        assets::DependencyRecord committed;
        probe.sawCommittedReaderState = probe.index != nullptr &&
                                         probe.index->Find(probe.output, committed) == assets::IndexResult::Success &&
                                         committed.contentFingerprint == probe.committedContent;
        return probe.reject ? assets::RecookPreCommitValidationResult::Reject
                            : assets::RecookPreCommitValidationResult::Accept;
    }

    [[nodiscard]] bool BuildAndPublish(assets::BuildSystem& system, assets::DependencyIndex& index, const assets::BuildRequest& request,
                                       const resources::ResourceReference dependency = {}, const assets::BuildFingerprint& dependencyContent = {}) noexcept
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
        return system.Execute(request, plan, output) == assets::Result::Success && index.Publish(request, plan, output) == assets::IndexResult::Success;
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

    [[nodiscard]] bool DeleteDirectoryTree(filesystem::Manager& manager, const filesystem::AbsolutePath& directory) noexcept
    {
        bool removed = true;
        containers::DynamicArray<filesystem::AbsolutePath> directories(memory::pools::Assets::GetInstance());
        manager.FindDirectories(directory, directories);
        for (const filesystem::AbsolutePath& child : directories)
            removed = DeleteDirectoryTree(manager, child) && removed;
        containers::DynamicArray<filesystem::AbsolutePath> files(memory::pools::Assets::GetInstance());
        manager.FindFiles(directory, containers::String("*"), files, false);
        for (const filesystem::AbsolutePath& file : files)
            removed = manager.DeleteFile(file) && removed;
        return manager.DeletePath(directory) && removed;
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
    const filesystem::AbsolutePath working = vanguard::tests::ScratchDirectory("asset_recooker_tests");
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_asset_recooker_tests");
    const filesystem::AbsolutePath cacheRoot = root.AddDirPath("ddc");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    filesystem::Manager& manager = filesystem::GetManager();
    DeleteIndexFiles(manager, root);
    static_cast<void>(DeleteDirectoryTree(manager, root));
    Check(manager.CreatePath(root), "recooker test root");

    Fixture fixture;
    fixture.sharedSource = Reference("source/recook/shared.asset", SourceType);
    fixture.leftSource = Reference("source/recook/left.asset", SourceType);
    fixture.rootSource = Reference("source/recook/root.asset", SourceType);
    fixture.firstSource = Reference("source/recook/first.asset", SourceType);
    fixture.sharedOutput = Reference("cooked/recook/shared.asset", OutputType);
    fixture.leftOutput = Reference("cooked/recook/left.asset", OutputType);
    fixture.rootOutput = Reference("cooked/recook/root.asset", OutputType);
    fixture.firstOutput = Reference("cooked/recook/first.asset", OutputType);

    assets::BuildSystem buildSystem;
    assets::Config buildConfig;
    buildConfig.persistentCacheRoot = cacheRoot.AsChar();
    Check(buildSystem.Initialize(buildConfig), "build-system initialization");
    const assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.recooker_test"), "assets.recooker_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture, &Estimate};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = root.AsChar();
    indexConfig.settingsFingerprint = crypto::Sha256("recooker-settings", 17);
    assets::DependencyIndex index;
    Check(index.Initialize(indexConfig) == assets::IndexResult::Success, "index initialization");
    Check(index.BindPersistentProtectionDomain(buildSystem), "bind recooker index to persistent protection domain");

    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedByte)), "baseline shared publication");
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
        const assets::BuildFingerprint previousSharedContent = sharedRecord.contentFingerprint;
        fixture.sharedByte = 6;
        Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.sharedSource, fixture.sharedOutput, fixture.sharedByte)) &&
                  index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success &&
                  sharedRecord.contentFingerprint != previousSharedContent && index.Save() == assets::IndexResult::Success,
              "P2 commit changed Generated child without refreshing its dependant");
        assets::BuildPlan parityPlan;
        Check(buildSystem.Prepare(MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte), parityPlan) == assets::Result::Success &&
                  index.MaterializeGeneratedDependencyContent(parityPlan) == assets::IndexResult::Success &&
                  index.Evaluate(MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte), parityPlan) ==
                      assets::DirtyReason::DependenciesChanged,
              "P2 recooker preparation sees committed Generated content change as DependenciesChanged");
        const resources::ResourceReference explicitOutputs[]{fixture.leftOutput};
        assets::RecookBatch dependencyChanged = recooker.Request({}, {explicitOutputs, 1}, assets::BuildPriority::High);
        dependencyChanged.Wait();
        const assets::RecookStats stats = dependencyChanged.GetStats();
        assets::DependencyRecord refreshedLeft;
        Check(dependencyChanged.GetStatus() == assets::RecookState::Succeeded && stats.explicitOutputs == 1u && stats.dirtySeeds == 1u &&
                  stats.affectedOutputs == 2u && stats.rootRequests == 1u && stats.succeededRoots == 1u &&
                  index.Find(fixture.leftOutput, refreshedLeft) == assets::IndexResult::Success && refreshedLeft.dependencies.Size() == 1u &&
                  refreshedLeft.dependencies[0].content == sharedRecord.contentFingerprint,
              "P2 recooker dirty planning matches committed Generated materialization");
        Check(stats.buildOperations >= stats.rootRequests &&
                  stats.completedBuildOperations == stats.buildOperations,
              "recooker exposes complete BuildGraph operation progress including generated dependencies");
        leftRecord = refreshedLeft;
        dependencyChanged.Reset();
    }

    {
        Check(index.Remove(fixture.sharedOutput) == assets::IndexResult::Success && index.Save() == assets::IndexResult::Success,
              "P2 remove required Generated VADI row for legacy tolerance fixture");
        assets::BuildPlan missingRequiredPlan;
        Check(buildSystem.Prepare(MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte), missingRequiredPlan) == assets::Result::Success &&
                  index.MaterializeGeneratedDependencyContent(missingRequiredPlan) == assets::IndexResult::NotFound &&
                  index.Evaluate(MakeRequest(fixture.leftSource, fixture.leftOutput, fixture.leftByte), missingRequiredPlan) ==
                      assets::DirtyReason::DependenciesChanged,
              "P2 missing Required Generated row remains dirty instead of failing preparation");
        const resources::ResourceReference explicitOutputs[]{fixture.leftOutput};
        assets::RecookBatch missingRequired = recooker.Request({}, {explicitOutputs, 1}, assets::BuildPriority::High);
        missingRequired.Wait();
        const assets::RecookStats stats = missingRequired.GetStats();
        Check(missingRequired.GetStatus() == assets::RecookState::Succeeded && stats.explicitOutputs == 1u && stats.dirtySeeds == 1u &&
                  stats.affectedOutputs == 2u && stats.rootRequests == 1u && stats.succeededRoots == 1u &&
                  index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success,
              "P2 recooker preserves legacy Required-missing tolerance through dirty planning and rebuild");
        Check(index.Find(fixture.leftOutput, leftRecord) == assets::IndexResult::Success,
              "P2 Required-missing recovery refreshes dependent VADI state");
        missingRequired.Reset();
    }

    {
        const u32 callsBefore = fixture.compileCalls.GetValue();
        assets::RecookBatch unchanged = recooker.Request({&sharedChange, 1});
        containers::DynamicArray<assets::DependencyRecord> unchangedCommitted(memory::pools::Assets::GetInstance());
        Check(unchanged && unchanged.HasFinished() && unchanged.GetStatus() == assets::RecookState::Succeeded && unchanged.GetStats().unchangedChanges == 1 &&
                  unchanged.GetStats().rootRequests == 0 && fixture.compileCalls.GetValue() == callsBefore && !index.HasActiveTransaction() &&
                  !unchanged.TakeCommittedRecords(unchangedCommitted) && unchangedCommitted.Empty(),
              "P1 unchanged source event performs no work and exposes no committed-delivery handoff");
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
        Check(rebuilt.GetStatus() == assets::RecookState::Succeeded, "changed leaf batch succeeds");
        Check(stats.dirtySeeds == 1, "changed leaf contributes one dirty seed");
        Check(stats.affectedOutputs == 3, "changed leaf expands through all transitive outputs");
        Check(stats.rootRequests == 1, "changed leaf schedules one minimized root");
        Check(stats.succeededRoots == 1, "changed leaf root succeeds");
        Check(fixture.compileCalls.GetValue() == callsBefore + 2, "changed leaf traverses the full graph while preserving a content-identical root DDC hit");
        Check(!index.HasActiveTransaction(), "successful recook closes the index transaction");
        Check(!index.HasChanges(), "successful recook atomically persists the index");
        Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success && sharedRecord.contentFingerprint != committedSharedContent,
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
        const assets::RecookRootFailureDetail failedRoot = failed.GetRootFailureDetail();
        assets::DependencyRecord afterFailure;
        assets::DependencyRecord leftAfterFailure;
        containers::DynamicArray<assets::DependencyRecord> failedCommitted(memory::pools::Assets::GetInstance());
        Check(failed.GetStatus() == assets::RecookState::Failed && failed.GetError() == assets::RecookFailure::BuildFailed &&
                  index.Find(fixture.sharedOutput, afterFailure) == assets::IndexResult::Success && afterFailure.contentFingerprint == committedSharedContent &&
                  index.Find(fixture.leftOutput, leftAfterFailure) == assets::IndexResult::Success &&
                  leftAfterFailure.contentFingerprint == committedLeftContent && !index.HasActiveTransaction() && !index.HasChanges() &&
                  !failed.TakeCommittedRecords(failedCommitted) && failedCommitted.Empty(),
              "P1 failed root rolls back staged publications and exposes no committed-delivery handoff");
        Check(failedRoot.rootIndex == 0u && failedRoot.output == fixture.rootOutput &&
                  failedRoot.graphFailure == assets::BuildFailure::BuildFailed &&
                  failedRoot.buildResult == assets::Result::CompileFailed &&
                  std::strcmp(assets::ToString(failed.GetError()), "Graph root build failed") == 0 &&
                  std::strcmp(assets::ToString(failedRoot.graphFailure), "Compiler failed") == 0,
              "failed recook retains first graph root, compiler error and readable reason across rollback");
        failed.Reset();
        fixture.failRoot = false;
    }

    {
        // E2.3 P3: an untracked typed source plus an explicit cooked output is
        // a legitimate first cook. It must share the same recooker-owned VADI
        // transaction with ordinary indexed invalidation work.
        fixture.sharedByte = 12;
        const assets::AssetChange firstChange{fixture.firstSource};
        const assets::AssetChange combinedChanges[]{sharedChange, firstChange};
        const resources::ResourceReference explicitOutputs[]{fixture.firstOutput};
        const u64 commitsBefore = index.GetStats().transactionCommits;
        assets::RecookBatch combined = recooker.Request({combinedChanges, 2}, {explicitOutputs, 1}, assets::BuildPriority::High);
        combined.Wait();
        const assets::RecookStats stats = combined.GetStats();
        assets::DependencyRecord firstRecord;
        Check(combined.GetStatus() == assets::RecookState::Succeeded && stats.explicitOutputs == 1u &&
                  stats.firstCookOutputs == 1u && stats.affectedOutputs == 4u && stats.rootRequests == 2u &&
                  index.GetStats().transactionCommits == commitsBefore + 1u &&
                  index.Find(fixture.firstOutput, firstRecord) == assets::IndexResult::Success &&
                  firstRecord.source == fixture.firstSource && !index.HasActiveTransaction() && !index.HasChanges(),
              "P3 first cook and transitive indexed invalidation commit through one minimized recooker transaction");
        Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success,
              "P3 combined transaction refreshes committed shared record");
        committedSharedContent = sharedRecord.contentFingerprint;
        combined.Reset();
    }

    {
        // E2.3 P4: validation receives the complete owned staged closure while
        // ordinary VADI readers still see the last committed records. Rejection
        // rolls every staged publication back before it can become visible.
        fixture.sharedByte = 13;
        ValidationProbe probe;
        probe.index = &index;
        probe.output = fixture.sharedOutput;
        probe.committedContent = committedSharedContent;
        probe.reject = true;
        const assets::RecookPreCommitValidator validator{&ValidatePreCommit, &probe};
        assets::RecookBatch rejected = recooker.Request({&sharedChange, 1}, {}, assets::BuildPriority::High, validator);
        rejected.Wait();
        assets::DependencyRecord afterReject;
        containers::DynamicArray<assets::DependencyRecord> rejectedCommitted(memory::pools::Assets::GetInstance());
        Check(rejected.GetStatus() == assets::RecookState::Failed &&
                  rejected.GetError() == assets::RecookFailure::PreCommitRejected && probe.calls == 1u && probe.recordCount == 3u &&
                  probe.sawCommittedReaderState && index.Find(fixture.sharedOutput, afterReject) == assets::IndexResult::Success &&
                  afterReject.contentFingerprint == committedSharedContent && !index.HasActiveTransaction() && !index.HasChanges() &&
                  !rejected.TakeCommittedRecords(rejectedCommitted) && rejectedCommitted.Empty(),
              "P4 precommit rejection sees staged closure but preserves committed VADI");
        rejected.Reset();

        probe.calls = 0u;
        probe.recordCount = 0u;
        probe.reject = false;
        assets::RecookBatch accepted = recooker.Request({&sharedChange, 1}, {}, assets::BuildPriority::High, validator);
        accepted.Wait();
        containers::DynamicArray<assets::DependencyRecord> committedEvidence(memory::pools::Assets::GetInstance());
        Check(accepted.GetStatus() == assets::RecookState::Succeeded && probe.calls == 1u && probe.recordCount == 3u &&
                  index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success &&
                  sharedRecord.contentFingerprint != committedSharedContent && accepted.TakeCommittedRecords(committedEvidence) &&
                  committedEvidence.Size() == 3u,
              "P1 successful validator batch exposes the exact committed transaction handoff");
        bool foundCommittedShared = false;
        for (const assets::DependencyRecord& record : committedEvidence)
            if (record.output == fixture.sharedOutput)
                foundCommittedShared = record.buildFingerprint == sharedRecord.buildFingerprint &&
                                       record.contentFingerprint == sharedRecord.contentFingerprint;
        Check(foundCommittedShared && !accepted.TakeCommittedRecords(rejectedCommitted),
              "P1 committed transaction handoff preserves exact fingerprints and is take-once");
        committedSharedContent = sharedRecord.contentFingerprint;
        accepted.Reset();

        probe.calls = 0u;
        probe.recordCount = 99u;
        probe.committedContent = committedSharedContent;
        probe.reject = true;
        assets::RecookBatch rejectedNoop = recooker.Request({&sharedChange, 1}, {}, assets::BuildPriority::High, validator);
        Check(rejectedNoop && rejectedNoop.HasFinished() && rejectedNoop.GetStatus() == assets::RecookState::Failed &&
                  rejectedNoop.GetError() == assets::RecookFailure::PreCommitRejected && probe.calls == 1u && probe.recordCount == 0u &&
                  probe.sawCommittedReaderState && !index.HasActiveTransaction(),
              "P4 validator also fences a no-work completion before evidence is retired");
        rejectedNoop.Reset();
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
        containers::DynamicArray<assets::DependencyRecord> cancelledCommitted(memory::pools::Assets::GetInstance());
        Check(cancelled.GetStatus() == assets::RecookState::Cancelled && cancelled.GetError() == assets::RecookFailure::Cancelled && !index.HasActiveTransaction() &&
                  !index.HasChanges() && !cancelled.TakeCommittedRecords(cancelledCommitted) && cancelledCommitted.Empty(),
              "P1 cancelled recook rolls back transaction and exposes no committed-delivery handoff");
        cancelled.Reset();
        fixture.blockShared = false;
    }

    {
        const assets::AssetChange unknown{Reference("source/recook/untracked.asset", SourceType)};
        assets::RecookBatch untracked = recooker.Request({&unknown, 1});
        Check(untracked && untracked.HasFinished() && untracked.GetStatus() == assets::RecookState::Failed &&
                  untracked.GetError() == assets::RecookFailure::UntrackedChange,
              "untracked source change fails explicitly");
        untracked.Reset();
    }

    Check(recooker.GetStats().completedBatches == 6 && recooker.GetStats().failedBatches == 4 && recooker.GetStats().cancelledBatches == 1,
          "recook lifetime telemetry");
    Check(recooker.Shutdown(), "recooker shutdown");
    {
        // A deleted model's committed dependency edges are useful as historical
        // invalidation evidence, but are no longer eligible compiler roots.
        // Only an explicit current-absence witness may turn that closure into
        // a validated no-work completion. Explicit demand remains a hard error.
        fixture.historicalRetired = true;
        fixture.sharedByte = 24u;
        assets::RecookConfig historicalConfig;
        historicalConfig.isConfirmedHistoricalOutput = &ConfirmHistoricalOutput;
        historicalConfig.historicalOutputUserData = &fixture;
        assets::IncrementalRecooker historical;
        Check(historical.Initialize(buildSystem, graph, index, &ResolveOutput, &fixture, historicalConfig),
              "historical output recooker initialization");
        assets::RecookBatch oldOnly = historical.Request({&sharedChange, 1}, assets::BuildPriority::High);
        const assets::RecookStats oldOnlyStats = oldOnly.GetStats();
        Check(oldOnly && oldOnly.HasFinished() && oldOnly.GetStatus() == assets::RecookState::Succeeded &&
                  oldOnlyStats.rootRequests == 0u && !index.HasActiveTransaction(),
              "deleted source's indexed dependency closure is not promoted to a compiler root");
        oldOnly.Reset();
        const resources::ResourceReference demanded[] = {fixture.rootOutput};
        assets::RecookBatch stillDemanded = historical.Request({}, {demanded, 1}, assets::BuildPriority::High);
        Check(stillDemanded && stillDemanded.HasFinished() &&
                  stillDemanded.GetStatus() == assets::RecookState::Failed &&
                  stillDemanded.GetError() == assets::RecookFailure::RequestResolutionFailed,
              "a removed output explicitly demanded by current authored truth remains a hard failure");
        stillDemanded.Reset();
        Check(historical.Shutdown(), "historical recooker shutdown");
        fixture.historicalRetired = false;
    }
    Check(graph.Shutdown(), "graph shutdown");
    Check(index.Shutdown(), "index shutdown");

    assets::DependencyIndex restarted;
    Check(restarted.Initialize(indexConfig) == assets::IndexResult::Success, "transactional index restart");
    assets::DependencyRecord persistedShared;
    Check(restarted.Find(fixture.sharedOutput, persistedShared) == assets::IndexResult::Success && persistedShared.contentFingerprint == committedSharedContent,
          "only successful recook transaction survives restart");
    Check(restarted.Shutdown(), "restarted index shutdown");

    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    Check(DeleteDirectoryTree(manager, root), "recooker test root cleanup");
    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
