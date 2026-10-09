#include "../../../tests/support/scratch_directory.hpp"
#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/assets/asset_index.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/jobs/jobs.hpp>


namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x49535243u;
    constexpr resources::ResourceTypeId OutputType = 0x494f5554u;

    struct Fixture
    {
        resources::ResourceReference sharedSource;
        resources::ResourceReference leftSource;
        resources::ResourceReference rootSource;
        resources::ResourceReference softSource;
        resources::ResourceReference materializeSource;
        resources::ResourceReference sharedOutput;
        resources::ResourceReference leftOutput;
        resources::ResourceReference rootOutput;
        resources::ResourceReference softOutput;
        resources::ResourceReference materializeOutput;
        resources::ResourceReference missingOutput;
        assets::DependencyRequirement softRequirement = assets::DependencyRequirement::Soft;
        assets::DependencyRequirement missingRequirement = assets::DependencyRequirement::Required;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetIndexTests] FAILED: %s", message);
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
        if (request.source.identity == fixture.softSource)
        {
            return collector.Add({fixture.sharedOutput, {}, assets::DependencyRole::Generated, fixture.softRequirement}) ==
                   assets::Result::Success;
        }
        if (request.source.identity == fixture.materializeSource)
        {
            if (collector.Add({fixture.sharedOutput, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) !=
                assets::Result::Success)
                return false;
            return collector.Add({fixture.missingOutput, {}, assets::DependencyRole::Generated, fixture.missingRequirement}) ==
                   assets::Result::Success;
        }
        return request.source.identity == fixture.sharedSource;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void*) noexcept
    {
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

    [[nodiscard]] assets::BuildRequest Request(const resources::ResourceReference source, const resources::ResourceReference output,
                                               const u8* const bytes) noexcept
    {
        return {{source, {bytes, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};
    }

    [[nodiscard]] const assets::BuildDependency* FindDependency(const assets::BuildPlan& plan,
                                                                 const resources::ResourceReference identity) noexcept
    {
        for (const assets::BuildDependency& dependency : plan.GetDependencies())
            if (dependency.identity == identity)
                return &dependency;
        return nullptr;
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

    [[nodiscard]] char HexDigit(const u8 value) noexcept
    {
        return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
    }

    [[nodiscard]] filesystem::AbsolutePath RecordPath(const filesystem::AbsolutePath& root,
                                                      const assets::BuildFingerprint& fingerprint) noexcept
    {
        char first[3] = {HexDigit(fingerprint.bytes[0] >> 4u), HexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {HexDigit(fingerprint.bytes[1] >> 4u), HexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        char name[70];
        for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
        {
            name[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
            name[index * 2u + 1u] = HexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        name[64] = '.';
        name[65] = 'v';
        name[66] = 'd';
        name[67] = 'd';
        name[68] = 'c';
        name[69] = '\0';
        return root.AddDirPath(first).AddDirPath(second).AddFilePath(name);
    }

    void DeleteIndexFiles(filesystem::Manager& manager, const filesystem::AbsolutePath& root) noexcept
    {
        const filesystem::AbsolutePath index = root.AddFilePath("asset-dependencies.vadi");
        const filesystem::AbsolutePath temporary = root.AddFilePath("asset-dependencies.vadi.tmp");
        if (manager.FileExist(index))
        {
            static_cast<void>(manager.DeleteFile(index));
        }
        if (manager.FileExist(temporary))
        {
            static_cast<void>(manager.DeleteFile(temporary));
        }
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetIndexTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "io initialization");
    const filesystem::AbsolutePath working = vanguard::tests::ScratchDirectory("asset_index_tests");
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_asset_index_tests");
    const filesystem::AbsolutePath cacheRoot = root.AddDirPath("ddc");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    filesystem::Manager& manager = filesystem::GetManager();
    DeleteIndexFiles(manager, root);
    Check(manager.CreatePath(root), "index root creation");

    Fixture fixture;
    fixture.sharedSource = Reference("source/index/shared.asset", SourceType);
    fixture.leftSource = Reference("source/index/left.asset", SourceType);
    fixture.rootSource = Reference("source/index/root.asset", SourceType);
    fixture.softSource = Reference("source/index/soft.asset", SourceType);
    fixture.materializeSource = Reference("source/index/materialize.asset", SourceType);
    fixture.sharedOutput = Reference("cooked/index/shared.asset", OutputType);
    fixture.leftOutput = Reference("cooked/index/left.asset", OutputType);
    fixture.rootOutput = Reference("cooked/index/root.asset", OutputType);
    fixture.softOutput = Reference("cooked/index/soft.asset", OutputType);
    fixture.materializeOutput = Reference("cooked/index/materialize.asset", OutputType);
    fixture.missingOutput = Reference("cooked/index/missing.asset", OutputType);

    const u8 sharedBytes[] = {1};
    const u8 leftBytes[] = {2};
    const u8 rootBytes[] = {3};
    const u8 softBytes[] = {4};
    const u8 materializeBytes[] = {5};
    const assets::BuildRequest sharedRequest = Request(fixture.sharedSource, fixture.sharedOutput, sharedBytes);
    const assets::BuildRequest leftRequest = Request(fixture.leftSource, fixture.leftOutput, leftBytes);
    const assets::BuildRequest rootRequest = Request(fixture.rootSource, fixture.rootOutput, rootBytes);
    const assets::BuildRequest softRequest = Request(fixture.softSource, fixture.softOutput, softBytes);
    const assets::BuildRequest materializeRequest = Request(fixture.materializeSource, fixture.materializeOutput, materializeBytes);

    assets::BuildSystem buildSystem;
    assets::Config buildConfig;
    buildConfig.persistentCacheRoot = cacheRoot.AsChar();
    Check(buildSystem.Initialize(buildConfig), "build-system initialization");
    assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.index_test"), "assets.index_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture, &Estimate};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::BuildPlan uninitializedMaterializePlan;
    assets::DependencyIndex uninitializedIndex;
    Check(buildSystem.Prepare(materializeRequest, uninitializedMaterializePlan) == assets::Result::Success &&
              uninitializedIndex.MaterializeGeneratedDependencyContent(uninitializedMaterializePlan) == assets::IndexResult::InvalidState,
          "P2 Generated materialization rejects an uninitialized dependency index");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = root.AsChar();
    indexConfig.settingsFingerprint = crypto::Sha256("index-settings-v1", 17);
    assets::DependencyIndex index;
    Check(index.Initialize(indexConfig) == assets::IndexResult::Success, "new index initialization");

    assets::BuildPlan preDomainPlan;
    assets::BuildOutput preDomainOutput;
    Check(buildSystem.Prepare(sharedRequest, preDomainPlan) == assets::Result::Success &&
              buildSystem.Execute(sharedRequest, preDomainPlan, preDomainOutput) == assets::Result::Success &&
              index.Publish(sharedRequest, preDomainPlan, preDomainOutput) == assets::IndexResult::InvalidState,
          "unbound index rejects durable publication");
    Check(index.BindPersistentProtectionDomain(buildSystem), "bind index to persistent protection domain");
    Check(index.Publish(sharedRequest, preDomainPlan, preDomainOutput) == assets::IndexResult::InvalidState,
          "pre-domain publication token cannot be grandfathered into durable VADI");
    preDomainOutput.Reset();

    Check(BuildAndPublish(buildSystem, index, sharedRequest), "publish shared record");
    assets::DependencyRecord sharedRecord;
    Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success, "find shared record");
    Check(BuildAndPublish(buildSystem, index, leftRequest, fixture.sharedOutput, sharedRecord.contentFingerprint), "publish left record");
    assets::DependencyRecord leftRecord;
    Check(index.Find(fixture.leftOutput, leftRecord) == assets::IndexResult::Success, "find left record");
    Check(BuildAndPublish(buildSystem, index, rootRequest, fixture.leftOutput, leftRecord.contentFingerprint), "publish root record");

    assets::BuildPlan requiredMissingPlan;
    Check(buildSystem.Prepare(materializeRequest, requiredMissingPlan) == assets::Result::Success &&
              index.MaterializeGeneratedDependencyContent(requiredMissingPlan) == assets::IndexResult::NotFound,
          "P2 missing Required Generated dependency reports NotFound after materializing available rows");
    const assets::BuildDependency* const requiredAvailable = FindDependency(requiredMissingPlan, fixture.sharedOutput);
    const assets::BuildDependency* const requiredMissing = FindDependency(requiredMissingPlan, fixture.missingOutput);
    Check(requiredMissingPlan.GetDependencies().Count() == 2u && requiredAvailable != nullptr && requiredMissing != nullptr &&
              requiredAvailable->content == sharedRecord.contentFingerprint && requiredMissing->content.IsEmpty(),
          "P2 Required-missing materialization retains available Generated content and leaves only the missing row unresolved");

    fixture.missingRequirement = assets::DependencyRequirement::Optional;
    assets::BuildPlan optionalMissingPlan;
    Check(buildSystem.Prepare(materializeRequest, optionalMissingPlan) == assets::Result::Success &&
              index.MaterializeGeneratedDependencyContent(optionalMissingPlan) == assets::IndexResult::Success,
          "P2 missing Optional Generated dependency is ignored");
    const assets::BuildDependency* const optionalAvailable = FindDependency(optionalMissingPlan, fixture.sharedOutput);
    const assets::BuildDependency* const optionalMissing = FindDependency(optionalMissingPlan, fixture.missingOutput);
    Check(optionalMissingPlan.GetDependencies().Count() == 2u && optionalAvailable != nullptr && optionalMissing != nullptr &&
              optionalAvailable->content == sharedRecord.contentFingerprint && optionalMissing->content.IsEmpty(),
          "P2 Optional-missing materialization still applies available Generated content");
    fixture.missingRequirement = assets::DependencyRequirement::Required;

    assets::BuildPlan softMaterializePlan;
    Check(buildSystem.Prepare(softRequest, softMaterializePlan) == assets::Result::Success &&
              index.MaterializeGeneratedDependencyContent(softMaterializePlan) == assets::IndexResult::Success,
          "P2 Soft Generated dependency materialization succeeds");
    const assets::BuildDependency* const softDependency = FindDependency(softMaterializePlan, fixture.sharedOutput);
    Check(softMaterializePlan.GetDependencies().Count() == 1u && softDependency != nullptr && softDependency->content.IsEmpty(),
          "P2 Soft Generated dependency is skipped even when its committed producer is available");

    Check(index.Save() == assets::IndexResult::Success, "save baseline before preferred Generated materialization fixture");
    const u8 stagedSharedBytes[] = {7};
    const assets::BuildRequest stagedSharedRequest = Request(fixture.sharedSource, fixture.sharedOutput, stagedSharedBytes);
    Check(index.BeginTransaction() == assets::IndexResult::Success, "begin preferred Generated materialization transaction");
    Check(BuildAndPublish(buildSystem, index, stagedSharedRequest), "stage changed Generated dependency record");
    containers::DynamicArray<assets::DependencyRecord> stagedRecords(memory::pools::Assets::GetInstance());
    assets::DependencyRecord committedDuringTransaction;
    Check(index.CaptureTransactionRecords(stagedRecords, 1u, 64u * 1024u) == assets::IndexResult::Success && stagedRecords.Size() == 1u &&
              stagedRecords[0].output == fixture.sharedOutput && stagedRecords[0].contentFingerprint != sharedRecord.contentFingerprint &&
              index.Find(fixture.sharedOutput, committedDuringTransaction) == assets::IndexResult::Success &&
              committedDuringTransaction.contentFingerprint == sharedRecord.contentFingerprint,
          "P2 staged Generated record stays isolated from committed readers");
    assets::BuildPlan committedPreferredPlan;
    Check(buildSystem.Prepare(leftRequest, committedPreferredPlan) == assets::Result::Success &&
              index.MaterializeGeneratedDependencyContent(committedPreferredPlan) == assets::IndexResult::Success &&
              index.Evaluate(leftRequest, committedPreferredPlan) == assets::DirtyReason::UpToDate,
          "P2 committed Generated materialization remains authoritative without preferred rows");
    assets::BuildPlan stagedPreferredPlan;
    Check(buildSystem.Prepare(leftRequest, stagedPreferredPlan) == assets::Result::Success &&
              index.MaterializeGeneratedDependencyContent(
                  stagedPreferredPlan, {stagedRecords.TypedData(), stagedRecords.Size()}) == assets::IndexResult::Success &&
              stagedPreferredPlan.GetDependencies().Count() == 1u &&
              stagedPreferredPlan.GetDependencies()[0].content == stagedRecords[0].contentFingerprint &&
              index.Evaluate(leftRequest, stagedPreferredPlan) == assets::DirtyReason::DependenciesChanged,
          "P2 preferred staged Generated record wins committed VADI materialization");
    Check(index.RollbackTransaction() == assets::IndexResult::Success, "rollback preferred Generated materialization transaction");

    containers::DynamicArray<resources::ResourceReference> references(memory::pools::Assets::GetInstance());
    Check(index.GetDependencies(fixture.leftOutput, references) == assets::IndexResult::Success && references.Size() == 1 &&
              references[0] == fixture.sharedOutput,
          "forward dependency query");
    Check(index.GetDirectDependants(fixture.sharedOutput, references) == assets::IndexResult::Success && references.Size() == 1 &&
              references[0] == fixture.leftOutput,
          "reverse dependency query");
    Check(index.CollectAffected(fixture.sharedSource, references) == assets::IndexResult::Success && references.Size() == 3 &&
              references[0] == fixture.sharedOutput && references[1] == fixture.leftOutput && references[2] == fixture.rootOutput,
          "transitive source invalidation");

    assets::BuildPlan leftPlan;
    Check(buildSystem.Prepare(leftRequest, leftPlan) == assets::Result::Success &&
              leftPlan.SetGeneratedDependencyContent(fixture.sharedOutput, sharedRecord.contentFingerprint) == assets::Result::Success &&
              index.Evaluate(leftRequest, leftPlan) == assets::DirtyReason::UpToDate,
          "exact record evaluates up to date");
    const u8 changedLeftBytes[] = {9};
    const assets::BuildRequest changedLeftRequest = Request(fixture.leftSource, fixture.leftOutput, changedLeftBytes);
    Check(index.Evaluate(changedLeftRequest, leftPlan) == assets::DirtyReason::SourceChanged, "source change is classified");
    assets::BuildFingerprint changedDependency = sharedRecord.contentFingerprint;
    changedDependency.bytes[0] ^= 0xffu;
    Check(leftPlan.SetGeneratedDependencyContent(fixture.sharedOutput, changedDependency) == assets::Result::Success &&
              index.Evaluate(leftRequest, leftPlan) == assets::DirtyReason::DependenciesChanged,
          "dependency change is classified");

    Check(BuildAndPublish(buildSystem, index, softRequest), "publish identity-only soft dependency record");
    assets::DependencyRecord softRecord;
    Check(index.Find(fixture.softOutput, softRecord) == assets::IndexResult::Success && softRecord.dependencies.Size() == 1 &&
              softRecord.dependencies[0].identity == fixture.sharedOutput &&
              softRecord.dependencies[0].requirement == assets::DependencyRequirement::Soft && softRecord.dependencies[0].content.IsEmpty(),
          "soft dependency is indexed without content identity");
    Check(index.GetDirectDependants(fixture.sharedOutput, references) == assets::IndexResult::Success && references.Size() == 1 &&
              references[0] == fixture.leftOutput,
          "soft dependency creates no reverse build invalidation edge");
    Check(index.CollectAffected(fixture.sharedSource, references) == assets::IndexResult::Success && references.Size() == 3 &&
              references[0] == fixture.sharedOutput && references[1] == fixture.leftOutput && references[2] == fixture.rootOutput,
          "soft dependency does not expand incremental-recook closure");
    assets::BuildPlan softPlan;
    assets::BuildFingerprint forbiddenSoftContent;
    forbiddenSoftContent.bytes[0] = 1;
    Check(buildSystem.Prepare(softRequest, softPlan) == assets::Result::Success &&
              index.Evaluate(softRequest, softPlan) == assets::DirtyReason::UpToDate &&
              softPlan.SetGeneratedDependencyContent(fixture.sharedOutput, forbiddenSoftContent) == assets::Result::InvalidArgument,
          "soft dependency cache identity remains identity-only");
    fixture.softRequirement = assets::DependencyRequirement::Optional;
    assets::BuildPlan optionalPlan;
    Check(buildSystem.Prepare(softRequest, optionalPlan) == assets::Result::Success &&
              index.Evaluate(softRequest, optionalPlan) == assets::DirtyReason::DependenciesChanged,
          "dependency requirement kind participates in dirty checking");
    fixture.softRequirement = assets::DependencyRequirement::Soft;

    containers::DynamicArray<assets::ArtifactSetKey> protectionKeys(memory::pools::Assets::GetInstance());
    Check(index.CaptureProtectionKeys(protectionKeys, 4, 4u * sizeof(assets::ArtifactSetKey)) == assets::IndexResult::Success &&
              protectionKeys.Size() == 4,
          "bounded committed VADI protection snapshot exposes exact keys");
    Check(index.CaptureProtectionKeys(protectionKeys, 3, 4u * sizeof(assets::ArtifactSetKey)) == assets::IndexResult::LimitExceeded &&
              protectionKeys.Empty(),
          "committed VADI protection snapshot refuses partial overflow");

    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "replace compiler for version invalidation");
    compiler.version = 2;
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "register new compiler version");
    assets::BuildPlan changedCompilerPlan;
    Check(buildSystem.Prepare(leftRequest, changedCompilerPlan) == assets::Result::Success &&
              changedCompilerPlan.SetGeneratedDependencyContent(fixture.sharedOutput, sharedRecord.contentFingerprint) == assets::Result::Success &&
              index.Evaluate(leftRequest, changedCompilerPlan) == assets::DirtyReason::CompilerChanged,
          "compiler version change is classified");

    Check(!index.Shutdown(), "shutdown refuses unsaved changes");
    Check(index.Save() == assets::IndexResult::Success && !index.HasChanges(), "atomic index save");
    Check(index.BeginTransaction() == assets::IndexResult::Success && BuildAndPublish(buildSystem, index, sharedRequest),
          "stage replacement for VADI protection snapshot");
    Check(index.CaptureProtectionKeys(protectionKeys, 5, 5u * sizeof(assets::ArtifactSetKey)) == assets::IndexResult::Success &&
              protectionKeys.Size() == 5,
          "VADI protection snapshot includes committed and staged exact keys");
    Check(index.RollbackTransaction() == assets::IndexResult::Success, "rollback staged protection fixture");
    Check(index.Shutdown(), "saved index shutdown");

    assets::DependencyIndex restarted;
    Check(restarted.Initialize(indexConfig) == assets::IndexResult::Success, "persistent index restart");
    assets::DependencyRecord persisted;
    Check(restarted.Find(fixture.rootOutput, persisted) == assets::IndexResult::Success && persisted.dependencies.Size() == 1 &&
              persisted.artifacts.Size() == 1,
          "records survive restart");
    assets::DependencyRecord persistedSoft;
    Check(restarted.Find(fixture.softOutput, persistedSoft) == assets::IndexResult::Success && persistedSoft.dependencies.Size() == 1 &&
              persistedSoft.dependencies[0].identity == fixture.sharedOutput &&
              persistedSoft.dependencies[0].requirement == assets::DependencyRequirement::Soft && persistedSoft.dependencies[0].content.IsEmpty(),
          "soft dependency kind and empty content survive restart");

    assets::BuildGraph graph;
    Check(graph.Initialize(buildSystem, nullptr, nullptr, {}, &restarted), "indexed build-graph initialization");
    assets::GraphRequest graphRequest = graph.Request(sharedRequest);
    graphRequest.Wait();
    assets::BuildOutput graphOutput;
    Check(graphRequest.HasSucceeded() && graphRequest.CopyOutput(graphOutput), "graph build with index publication");
    Check(graph.CaptureProtectionKeys(protectionKeys, 1, sizeof(assets::ArtifactSetKey)) == assets::Result::Success &&
              protectionKeys.Size() == 1 && protectionKeys[0].build == graphOutput.buildFingerprint &&
              protectionKeys[0].content == graphOutput.contentFingerprint,
          "quiescent graph exposes bounded retained exact-key protection snapshot");
    Check(graph.CaptureProtectionKeys(protectionKeys, 1, sizeof(assets::ArtifactSetKey) - 1u) == assets::Result::LimitExceeded &&
              protectionKeys.Empty(),
          "graph protection snapshot refuses partial byte overflow");
    graphRequest.Reset();
    Check(graph.Shutdown(), "indexed build-graph shutdown");
    Check(jobs::GetOutstandingJobCount() == 0, "build-graph shutdown drains dispatched job wrappers");
    Check(restarted.GetStats().replacements >= 1, "graph publishes successful operation");

    const filesystem::AbsolutePath graphRecord = RecordPath(cacheRoot, graphOutput.buildFingerprint);
    Check(manager.FileExist(graphRecord) && manager.DeleteFile(graphRecord), "remove exact VDDC after graph adoption for memory restoration fixture");
    const u64 localBuildsBeforeRestore = buildSystem.GetStats().localBuilds;
    assets::BuildGraph restoreGraph;
    Check(restoreGraph.Initialize(buildSystem, nullptr, nullptr, {}, &restarted), "restoration graph initialization");
    assets::GraphRequest restoredRequest = restoreGraph.Request(sharedRequest);
    restoredRequest.Wait();
    assets::BuildOutput restoredOutput;
    Check(restoredRequest.HasSucceeded() && restoredRequest.CopyOutput(restoredOutput) &&
              restoredOutput.origin == assets::BuildOrigin::MemoryCacheHit && manager.FileExist(graphRecord) &&
              buildSystem.GetStats().localBuilds == localBuildsBeforeRestore,
          "durable memory hit restores missing exact VDDC without compiler");
    restoredRequest.Reset();
    Check(restoreGraph.Shutdown(), "restoration graph shutdown");
    Check(restarted.Save() == assets::IndexResult::Success, "save graph publication");
    Check(restarted.Shutdown(), "graph-published index shutdown");

    assets::DependencyIndexConfig incompatibleConfig = indexConfig;
    incompatibleConfig.settingsFingerprint = crypto::Sha256("index-settings-v2", 17);
    assets::DependencyIndex incompatible;
    Check(incompatible.Initialize(incompatibleConfig) == assets::IndexResult::Recovered && incompatible.GetStats().incompatibleFiles == 1 &&
              incompatible.GetStats().records == 0,
          "settings incompatibility explicitly recovers");
    Check(incompatible.Shutdown(), "incompatible index shutdown");

    assets::DependencyIndex corruptionSource;
    Check(corruptionSource.Initialize(indexConfig) == assets::IndexResult::Success, "corruption-source initialization");
    Check(corruptionSource.BindPersistentProtectionDomain(buildSystem), "corruption-source binds persistent protection domain");
    Check(BuildAndPublish(buildSystem, corruptionSource, sharedRequest) && corruptionSource.Save() == assets::IndexResult::Success &&
              corruptionSource.Shutdown(),
          "corruption-source publication");
    const filesystem::AbsolutePath indexPath = root.AddFilePath("asset-dependencies.vadi");
    {
        const u64 size = manager.GetFileSize(indexPath);
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Resize(static_cast<u32>(size));
        auto reader = manager.CreateFileReader(indexPath, filesystem::FOF_Buffered);
        Check(reader && bytes.Size() == size, "open index for corruption injection");
        if (reader && !bytes.Empty())
        {
            reader->Serialize(bytes.Data(), bytes.Size());
            reader.Reset();
            bytes.Back() ^= 0xffu;
            auto writer = manager.CreateFileWriter(indexPath, filesystem::FOF_Buffered);
            Check(static_cast<bool>(writer), "write corrupt index");
            if (writer)
            {
                writer->Serialize(bytes.Data(), bytes.Size());
                writer->Flush();
                writer.Reset();
            }
        }
    }
    assets::DependencyIndex recovered;
    Check(recovered.Initialize(indexConfig) == assets::IndexResult::Recovered && recovered.GetStats().corruptions == 1 &&
              recovered.GetStats().recoveries == 1 && recovered.GetStats().records == 0,
          "integrity corruption explicitly recovers");
    Check(recovered.Shutdown(), "recovered index shutdown");

    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    DeleteIndexFiles(manager, root);
    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
