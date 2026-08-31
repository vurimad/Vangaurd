#include <vanguard/assets/assets.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x534f5552u;
    constexpr resources::ResourceTypeId OutputType = 0x434f4f4bu;
    constexpr resources::ResourceTypeId IncludeType = 0x494e434cu;
    constexpr resources::ResourceTypeId AuxiliaryType = 0x41555849u;

    enum class CompilerMode : u8
    {
        Normal,
        DuplicateDependency,
        MissingPrimary,
        WriterLimitFailure
    };

    struct CompilerState
    {
        resources::ResourceReference firstDependency;
        resources::ResourceReference secondDependency;
        resources::ResourceReference auxiliaryOutput;
        assets::BuildFingerprint firstContent;
        assets::BuildFingerprint secondContent;
        u32 discoverCalls = 0;
        u32 compileCalls = 0;
        bool reverseDependencies = false;
        CompilerMode mode = CompilerMode::Normal;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetsTests] FAILED: %s", message);
            ++g_failures;
        }
    }

    [[nodiscard]] resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }

    [[nodiscard]] bool AddDependency(assets::DependencyCollector& collector, const resources::ResourceReference identity,
                                     const assets::BuildFingerprint& content, const assets::DependencyRole role) noexcept
    {
        return collector.Add({identity, content, role, assets::DependencyRequirement::Required}) == assets::Result::Success;
    }

    bool DiscoverDependencies(const assets::BuildRequest&, assets::DependencyCollector& collector, void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        ++state.discoverCalls;
        if (state.mode == CompilerMode::DuplicateDependency)
        {
            static_cast<void>(AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source));
            static_cast<void>(
                collector.Add({state.firstDependency, state.firstContent, assets::DependencyRole::Source, assets::DependencyRequirement::Required}));
            return true;
        }

        if (state.reverseDependencies)
        {
            return AddDependency(collector, state.secondDependency, state.secondContent, assets::DependencyRole::Generated) &&
                   AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source);
        }
        return AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source) &&
               AddDependency(collector, state.secondDependency, state.secondContent, assets::DependencyRole::Generated);
    }

    bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        ++state.compileCalls;
        const u8 primary[] = {static_cast<u8>(context.request.target),
                              context.request.source.content.Empty() ? static_cast<u8>(0) : context.request.source.content[0], state.firstContent.bytes[0],
                              state.secondContent.bytes[0]};
        const assets::ArtifactFlags primaryFlags = state.mode == CompilerMode::MissingPrimary
                                                       ? assets::ArtifactFlags::MemoryResident
                                                       : assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident;
        if (writer.Add(context.request.output, 1, primaryFlags, 8, primary, sizeof(primary)) != assets::Result::Success)
        {
            return false;
        }

        const u8 auxiliary[] = {0x41, 0x55, 0x58};
        if (writer.Add(state.auxiliaryOutput, 7, assets::ArtifactFlags::Streamable, 4, auxiliary, sizeof(auxiliary)) != assets::Result::Success)
        {
            return false;
        }
        if (state.mode == CompilerMode::WriterLimitFailure)
        {
            const u8 overflow[] = {0x4f, 0x56};
            return writer.Add(state.auxiliaryOutput, 8, assets::ArtifactFlags::Streamable, 4, overflow, sizeof(overflow)) ==
                   assets::Result::Success;
        }
        return true;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");

    CompilerState state;
    state.firstDependency = Reference("shaders/includes/common.inc", IncludeType);
    state.secondDependency = Reference("generated/layouts/common.layout", IncludeType);
    state.auxiliaryOutput = Reference("cooked/test.asset.meta", AuxiliaryType);
    const char firstDependencyBytes[] = "first dependency";
    const char secondDependencyBytes[] = "second dependency";
    state.firstContent = crypto::Sha256(firstDependencyBytes, sizeof(firstDependencyBytes) - 1u);
    state.secondContent = crypto::Sha256(secondDependencyBytes, sizeof(secondDependencyBytes) - 1u);

    const assets::CompilerId compilerId = assets::HashCompilerName("assets.test_compiler");
    assets::CompilerDescriptor compiler{compilerId, "assets.test_compiler", 1, SourceType, OutputType, &DiscoverDependencies, &Compile, &state};

    assets::Config config;
    config.maximumCacheEntries = 4;
    config.maximumCacheBytes = 1024 * 1024;
    config.maximumArtifactBytesPerBuild = 8;
    assets::BuildSystem system;
    Check(system.Initialize(config), "build system initialization");
    Check(system.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");
    Check(system.RegisterCompiler(compiler) == assets::Result::CompilerAlreadyRegistered, "ambiguous compiler registration rejected");

    const u8 sourceBytes[] = {1, 2, 3, 4};
    const u8 changedSourceBytes[] = {9, 2, 3, 4};
    const u8 metadataBytes[] = {7, 8};
    const u8 settingsA[] = {0x10};
    const u8 settingsB[] = {0x20};
    const resources::ResourceReference source = Reference("source/test.asset", SourceType);
    const resources::ResourceReference output = Reference("cooked/test.asset", OutputType);

    const u8 capacityPayload[] = {1, 2, 3, 4};
    assets::ArtifactWriter capacityWriter(output, 2, 6);
    Check(capacityWriter.GetMaximumArtifactCount() == 2 && capacityWriter.GetRemainingArtifactCount() == 2 &&
              capacityWriter.GetMaximumByteCount() == 6 && capacityWriter.GetRemainingByteCount() == 6,
          "artifact writer exposes its initial capacity");
    Check(capacityWriter.Add(output, 0, assets::ArtifactFlags::Primary, 4, capacityPayload, sizeof(capacityPayload)) == assets::Result::Success &&
              capacityWriter.GetRemainingArtifactCount() == 1 && capacityWriter.GetRemainingByteCount() == 2,
          "artifact writer reports remaining capacity after a write");
    Check(capacityWriter.Add(state.auxiliaryOutput, 0, assets::ArtifactFlags::Streamable, 4, capacityPayload, 3) ==
                  assets::Result::LimitExceeded &&
              capacityWriter.GetStatus() == assets::Result::LimitExceeded &&
              capacityWriter.Add(state.auxiliaryOutput, 1, assets::ArtifactFlags::Streamable, 4, capacityPayload, 1) ==
                  assets::Result::LimitExceeded,
          "artifact writer preserves a sticky capacity failure");

    assets::BuildRequest request{{source, {sourceBytes, 4}, {metadataBytes, 2}}, output, assets::TargetPlatform::WindowsD3D12, {settingsA, 1}};

    assets::BuildOutput first;
    Check(system.Build(request, first) == assets::Result::Success && first.disposition == assets::BuildDisposition::Built && first.artifacts.Size() == 2 &&
              !first.buildFingerprint.IsEmpty() && !first.contentFingerprint.IsEmpty() && state.compileCalls == 1,
          "first request builds a multi-artifact output");

    state.reverseDependencies = true;
    assets::BuildOutput reordered;
    Check(system.Build(request, reordered) == assets::Result::Success && reordered.disposition == assets::BuildDisposition::CacheHit &&
              reordered.buildFingerprint == first.buildFingerprint && reordered.contentFingerprint == first.contentFingerprint && state.compileCalls == 1,
          "dependency order does not change cache identity");

    assets::BuildRequest changedSource = request;
    changedSource.source.content = {changedSourceBytes, 4};
    assets::BuildOutput sourceInvalidated;
    Check(system.Build(changedSource, sourceInvalidated) == assets::Result::Success && sourceInvalidated.disposition == assets::BuildDisposition::Built &&
              sourceInvalidated.buildFingerprint != first.buildFingerprint && state.compileCalls == 2,
          "source content invalidates derived data");

    const char changedDependencyBytes[] = "changed dependency";
    state.firstContent = crypto::Sha256(changedDependencyBytes, sizeof(changedDependencyBytes) - 1u);
    assets::BuildOutput dependencyInvalidated;
    Check(system.Build(request, dependencyInvalidated) == assets::Result::Success && dependencyInvalidated.disposition == assets::BuildDisposition::Built &&
              dependencyInvalidated.buildFingerprint != first.buildFingerprint && state.compileCalls == 3,
          "dependency content invalidates derived data");

    assets::BuildRequest platformChanged = request;
    platformChanged.target = assets::TargetPlatform::WindowsVulkan;
    assets::BuildOutput platformInvalidated;
    Check(system.Build(platformChanged, platformInvalidated) == assets::Result::Success && platformInvalidated.disposition == assets::BuildDisposition::Built &&
              state.compileCalls == 4,
          "target platform participates in build fingerprint");

    assets::BuildRequest settingsChanged = request;
    settingsChanged.settings = {settingsB, 1};
    assets::BuildOutput settingsInvalidated;
    Check(system.Build(settingsChanged, settingsInvalidated) == assets::Result::Success && settingsInvalidated.disposition == assets::BuildDisposition::Built &&
              state.compileCalls == 5,
          "build settings participate in build fingerprint");

    state.mode = CompilerMode::DuplicateDependency;
    assets::BuildRequest duplicateRequest = request;
    const u8 duplicateSettings[] = {0x31};
    duplicateRequest.settings = {duplicateSettings, 1};
    assets::BuildOutput duplicateOutput;
    Check(system.Build(duplicateRequest, duplicateOutput) == assets::Result::DuplicateDependency, "duplicate dependencies fail deterministically");

    state.mode = CompilerMode::MissingPrimary;
    assets::BuildRequest invalidArtifactRequest = request;
    const u8 invalidSettings[] = {0x32};
    invalidArtifactRequest.settings = {invalidSettings, 1};
    assets::BuildOutput invalidArtifactOutput;
    Check(system.Build(invalidArtifactRequest, invalidArtifactOutput) == assets::Result::InvalidArtifact, "compiler must emit an explicit primary artifact");

    state.mode = CompilerMode::WriterLimitFailure;
    assets::BuildRequest writerLimitRequest = request;
    const u8 writerLimitSettings[] = {0x33};
    writerLimitRequest.settings = {writerLimitSettings, 1};
    assets::BuildOutput writerLimitOutput;
    Check(system.Build(writerLimitRequest, writerLimitOutput) == assets::Result::LimitExceeded,
          "artifact writer failure is returned instead of generic compiler failure");
    state.mode = CompilerMode::Normal;

    Check(system.UnregisterCompiler(compilerId) == assets::Result::Success, "compiler unregistration");
    compiler.version = 2;
    Check(system.RegisterCompiler(compiler) == assets::Result::Success, "new compiler version registration");
    assets::BuildOutput versionInvalidated;
    Check(system.Build(request, versionInvalidated) == assets::Result::Success && versionInvalidated.disposition == assets::BuildDisposition::Built &&
              versionInvalidated.buildFingerprint != dependencyInvalidated.buildFingerprint && state.compileCalls == 8,
          "compiler version invalidates derived data");

    const assets::Stats stats = system.GetStats();
    Check(stats.registeredCompilers == 1 && stats.activeBuilds == 0 && stats.buildRequests == 10 && stats.localBuilds == 6 && stats.cacheHits == 1 &&
              stats.cacheMisses == 6 && stats.failedBuilds == 3 && stats.cacheEntries == 4 && stats.cacheStores == 6 && stats.cacheEvictions == 2,
          "build and cache telemetry");

    Check(system.UnregisterCompiler(compilerId) == assets::Result::Success, "final compiler unregistration");
    Check(system.Shutdown(), "build system shutdown");

    if (g_failures == 0)
    {
        VG_LOG_INFO(diagnostics::Category::FunctionalTests, "[assetsTests] Vanguard cooking core checks passed");
    }
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
