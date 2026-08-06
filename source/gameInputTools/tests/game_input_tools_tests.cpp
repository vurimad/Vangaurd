#include <vanguard/game_input_tools/game_input_tools.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstdio>

namespace
{
    using namespace vanguard;
    u32 failures = 0;
    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        std::printf("FAIL: %s\n", message); ++failures;
    }
    [[nodiscard]] resources::ResourceReference Reference(const char* path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }
}

int main()
{
    using namespace vanguard;
    namespace git = game_input_tools;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "gameInputToolsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");

    constexpr char sourceText[] =
        "vinput 1\n"
        "# References may precede declarations.\n"
        "binding move.forward gameplay move key 26 scalar 1.0 0.5 0.4 1\n"
        "curve move 1.0 1.0\n"
        "context gameplay player 0 1\n"
        "action jump button 10 0.30 0.20 2 0.20 0.25 0.40 0.10 0.0 1.0 1.0 0 1\n"
        "action move axis1d 0 0 0 0 0 0 0 0 0.10 0.95 1.0 0 0\n"
        "curve move 0.0 0.0\n"
        "binding jump.keyboard gameplay jump key 44 scalar 1.0 0.5 0.4 1\n"
        "binding move.sprint gameplay move key 26 scalar 1.0 0.5 0.4 1 key 225\n";
    const containers::ArraySpan<const u8> source{reinterpret_cast<const u8*>(sourceText), sizeof(sourceText) - 1u};
    containers::DynamicArray<u8> cooked(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter writer(cooked);
    git::SourceDiagnostic diagnostic;
    Check(git::CompileSourceMapping(source, writer, &diagnostic) == git::SourceResult::Success,
          "compile editor source mapping");
    filesystem::MemoryFileReader reader(cooked, 0);
    game_input::MappingFile mapping;
    Check(mapping.Open(reader) == game_input::MappingResult::Success && mapping.Contexts().Size() == 1 &&
          mapping.Actions().Size() == 2 && mapping.Bindings().Size() == 3,
          "compiled source opens as validated vinput");
    game_input::ActionMap installed;
    Check(mapping.Install(installed) == game_input::MappingResult::Success && installed.GetStats().compiled,
          "compiled source installs into runtime map");

    constexpr char invalidText[] = "vinput 1\naction broken wrong 0 0 0 0 0 0 0 0 0 1 1 0 0\n";
    containers::DynamicArray<u8> invalidOutput(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter invalidWriter(invalidOutput);
    const containers::ArraySpan<const u8> invalidSource{reinterpret_cast<const u8*>(invalidText), sizeof(invalidText) - 1u};
    Check(git::CompileSourceMapping(invalidSource, invalidWriter, &diagnostic) == git::SourceResult::InvalidEnum &&
          diagnostic.line == 2 && diagnostic.column != 0 && diagnostic.message != nullptr,
          "source compiler returns actionable line diagnostic");

    assets::BuildSystem builds;
    assets::Config config; config.maximumCacheEntries = 4; config.maximumCacheBytes = 1024 * 1024;
    Check(builds.Initialize(config), "asset build system initialization");
    Check(git::RegisterMappingCompiler(builds) == assets::Result::Success,
          "input mapping compiler registration");
    const resources::ResourceReference sourceReference = Reference("input/default.inputmap", git::SourceMappingResourceType);
    const resources::ResourceReference outputReference = Reference("input/default.vinput", game_input::MappingResourceType);
    const assets::BuildRequest request{{sourceReference, source, {}}, outputReference,
                                       assets::TargetPlatform::WindowsD3D12, {}};
    assets::BuildOutput first;
    Check(builds.Build(request, first) == assets::Result::Success && first.disposition == assets::BuildDisposition::Built &&
          first.artifacts.Size() == 1 && first.artifacts[0].resource == outputReference &&
          assets::HasFlag(first.artifacts[0].flags, assets::ArtifactFlags::Primary),
          "registered compiler emits primary vinput artifact");
    assets::BuildOutput cached;
    Check(builds.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
          cached.buildFingerprint == first.buildFingerprint,
          "unchanged mapping source resolves through DDC cache");
    if (!first.artifacts.Empty())
    {
        filesystem::MemoryFileReader artifactReader(first.artifacts[0].bytes, 0);
        game_input::MappingFile artifact;
        Check(artifact.Open(artifactReader) == game_input::MappingResult::Success,
              "DDC artifact is runtime-loadable vinput");
    }
    Check(builds.Shutdown(), "asset build system shutdown");
    diagnostics::Shutdown();
    std::printf(failures == 0 ? "gameInputTools tests passed\n" : "gameInputTools tests failed: %u\n", failures);
    return failures == 0 ? 0 : 1;
}
