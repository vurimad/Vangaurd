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
        resources::ResourceReference sharedOutput;
        resources::ResourceReference leftOutput;
        resources::ResourceReference rootOutput;
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
    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_asset_index_tests");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    filesystem::Manager& manager = filesystem::GetManager();
    DeleteIndexFiles(manager, root);
    Check(manager.CreatePath(root), "index root creation");

    Fixture fixture;
    fixture.sharedSource = Reference("source/index/shared.asset", SourceType);
    fixture.leftSource = Reference("source/index/left.asset", SourceType);
    fixture.rootSource = Reference("source/index/root.asset", SourceType);
    fixture.sharedOutput = Reference("cooked/index/shared.asset", OutputType);
    fixture.leftOutput = Reference("cooked/index/left.asset", OutputType);
    fixture.rootOutput = Reference("cooked/index/root.asset", OutputType);

    const u8 sharedBytes[] = {1};
    const u8 leftBytes[] = {2};
    const u8 rootBytes[] = {3};
    const assets::BuildRequest sharedRequest = Request(fixture.sharedSource, fixture.sharedOutput, sharedBytes);
    const assets::BuildRequest leftRequest = Request(fixture.leftSource, fixture.leftOutput, leftBytes);
    const assets::BuildRequest rootRequest = Request(fixture.rootSource, fixture.rootOutput, rootBytes);

    assets::BuildSystem buildSystem;
    Check(buildSystem.Initialize(), "build-system initialization");
    assets::CompilerDescriptor compiler{
        assets::HashCompilerName("assets.index_test"), "assets.index_test", 1, SourceType, OutputType, &Discover, &Compile, &fixture, &Estimate};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = root.AsChar();
    indexConfig.settingsFingerprint = crypto::Sha256("index-settings-v1", 17);
    assets::DependencyIndex index;
    Check(index.Initialize(indexConfig) == assets::IndexResult::Success, "new index initialization");

    Check(BuildAndPublish(buildSystem, index, sharedRequest), "publish shared record");
    assets::DependencyRecord sharedRecord;
    Check(index.Find(fixture.sharedOutput, sharedRecord) == assets::IndexResult::Success, "find shared record");
    Check(BuildAndPublish(buildSystem, index, leftRequest, fixture.sharedOutput, sharedRecord.contentFingerprint), "publish left record");
    assets::DependencyRecord leftRecord;
    Check(index.Find(fixture.leftOutput, leftRecord) == assets::IndexResult::Success, "find left record");
    Check(BuildAndPublish(buildSystem, index, rootRequest, fixture.leftOutput, leftRecord.contentFingerprint), "publish root record");

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
    Check(index.Shutdown(), "saved index shutdown");

    assets::DependencyIndex restarted;
    Check(restarted.Initialize(indexConfig) == assets::IndexResult::Success, "persistent index restart");
    assets::DependencyRecord persisted;
    Check(restarted.Find(fixture.rootOutput, persisted) == assets::IndexResult::Success && persisted.dependencies.Size() == 1 &&
              persisted.artifacts.Size() == 1,
          "records survive restart");

    assets::BuildGraph graph;
    Check(graph.Initialize(buildSystem, nullptr, nullptr, {}, &restarted), "indexed build-graph initialization");
    assets::GraphRequest graphRequest = graph.Request(sharedRequest);
    graphRequest.Wait();
    Check(graphRequest.HasSucceeded(), "graph build with index publication");
    graphRequest.Reset();
    Check(graph.Shutdown(), "indexed build-graph shutdown");
    Check(restarted.GetStats().replacements >= 1, "graph publishes successful operation");
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
