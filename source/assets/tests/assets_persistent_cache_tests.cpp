#include <vanguard/assets/assets.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x53524345u;
    constexpr resources::ResourceTypeId OutputType = 0x434f4f4bu;

    struct CompilerState
    {
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<u32> barrierReady;
        bool synchronizeCompile = false;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetsPersistentCacheTests] FAILED: %s", message);
            ++g_failures;
        }
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

    void DeleteTestFiles(filesystem::Manager& manager, const filesystem::AbsolutePath& root) noexcept
    {
        containers::DynamicArray<filesystem::AbsolutePath> files(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*"), files, true);
        for (const filesystem::AbsolutePath& file : files)
        {
            static_cast<void>(manager.DeleteFile(file));
        }
        for (const filesystem::AbsolutePath& file : files)
        {
            const filesystem::AbsolutePath leafDirectory = filesystem::paths::ParentAbsolutePath(file);
            const filesystem::AbsolutePath fanoutDirectory = filesystem::paths::ParentAbsolutePath(leafDirectory);
            static_cast<void>(manager.DeletePath(leafDirectory));
            if (fanoutDirectory != root)
            {
                static_cast<void>(manager.DeletePath(fanoutDirectory));
            }
        }
        containers::DynamicArray<filesystem::AbsolutePath> firstLevel(memory::pools::Assets::GetInstance());
        manager.FindDirectories(root, firstLevel);
        for (const filesystem::AbsolutePath& first : firstLevel)
        {
            containers::DynamicArray<filesystem::AbsolutePath> secondLevel(memory::pools::Assets::GetInstance());
            manager.FindDirectories(first, secondLevel);
            for (const filesystem::AbsolutePath& second : secondLevel)
            {
                static_cast<void>(manager.DeletePath(second));
            }
            static_cast<void>(manager.DeletePath(first));
        }
        static_cast<void>(manager.DeletePath(root));
    }

    [[nodiscard]] bool Discover(const assets::BuildRequest&, assets::DependencyCollector&, void*) noexcept
    {
        return true;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        static_cast<void>(state.compileCalls.Increment());
        if (state.synchronizeCompile)
        {
            static_cast<void>(state.barrierReady.Increment());
            while (state.barrierReady.GetValue() != 2)
            {
                concurrency::YieldCurrentThread();
            }
        }
        const u8 payload[] = {0x56, 0x44, 0x44, 0x43, context.request.source.content[0]};
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, payload,
                          sizeof(payload)) == assets::Result::Success;
    }

    class BuildThread final : public concurrency::Thread
    {
    public:
        BuildThread(const char* const name, assets::BuildSystem& system, const assets::BuildRequest& request) noexcept
            : concurrency::Thread(name), m_system(&system), m_request(&request)
        {
        }

        void ThreadFunction() noexcept override
        {
            result = m_system->Build(*m_request, output);
        }

        assets::Result result = assets::Result::InvalidState;
        assets::BuildOutput output;

    private:
        assets::BuildSystem* m_system;
        const assets::BuildRequest* m_request;
    };

    [[nodiscard]] bool InitializeBuildSystem(assets::BuildSystem& system, const assets::Config& config,
                                             const assets::CompilerDescriptor& compiler) noexcept
    {
        return system.Initialize(config) && system.RegisterCompiler(compiler) == assets::Result::Success;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetsPersistentCacheTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");

    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_assets_ddc_tests");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    filesystem::Manager& manager = filesystem::GetManager();
    DeleteTestFiles(manager, root);
    Check(manager.CreatePath(root), "persistent cache root creation");

    CompilerState state;
    const assets::CompilerId compilerId = assets::HashCompilerName("assets.persistent_test");
    const assets::CompilerDescriptor compiler{compilerId, "assets.persistent_test", 7, SourceType, OutputType, &Discover, &Compile, &state};
    assets::Config config;
    config.maximumCacheEntries = 0;
    config.persistentCacheRoot = root.AsChar();

    const u8 sourceBytes[] = {0x2a, 0x10, 0x20};
    const resources::ResourceReference source(resources::ResourcePath::FromString("source/persistent.asset"), SourceType);
    const resources::ResourceReference output(resources::ResourcePath::FromString("cooked/persistent.asset"), OutputType);
    const assets::BuildRequest request{{source, {sourceBytes, sizeof(sourceBytes)}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};

    assets::BuildFingerprint fingerprint;
    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "first persistent build-system initialization");
        assets::BuildOutput built;
        Check(system.Build(request, built) == assets::Result::Success && built.disposition == assets::BuildDisposition::Built &&
                  state.compileCalls.GetValue() == 1,
              "cache miss compiles and publishes");
        fingerprint = built.buildFingerprint;
        const assets::Stats stats = system.GetStats();
        Check(stats.persistentCacheMisses == 1 && stats.persistentCacheStores == 1, "persistent miss and store telemetry");
        Check(system.Shutdown(), "first shutdown");
    }

    const filesystem::AbsolutePath record = RecordPath(root, fingerprint);
    Check(manager.FileExist(record), "content-addressed record exists");
    {
        containers::DynamicArray<filesystem::AbsolutePath> temporary(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*.tmp"), temporary, true);
        Check(temporary.Empty(), "successful publication leaves no temporary files");
    }

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "second persistent build-system initialization");
        assets::BuildOutput cached;
        Check(system.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
                  cached.buildFingerprint == fingerprint && cached.artifacts.Size() == 1 &&
                  cached.artifacts[0].bytes[4] == sourceBytes[0] && state.compileCalls.GetValue() == 1,
              "record survives process-lifetime restart");
        Check(system.GetStats().persistentCacheHits == 1, "persistent hit telemetry");
        Check(system.Shutdown(), "second shutdown");
    }

    {
        const u64 recordSize = manager.GetFileSize(record);
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Resize(static_cast<u32>(recordSize));
        auto reader = manager.CreateFileReader(record, filesystem::FOF_Buffered);
        Check(reader && bytes.Size() == recordSize, "open record for corruption test");
        if (reader && !bytes.Empty())
        {
            reader->Serialize(bytes.Data(), bytes.Size());
            reader.Reset();
            bytes.Back() ^= 0xffu;
            auto writer = manager.CreateFileWriter(record, filesystem::FOF_Buffered);
            Check(static_cast<bool>(writer), "open record to inject corruption");
            if (writer)
            {
                writer->Serialize(bytes.Data(), bytes.Size());
                writer->Flush();
                writer.Reset();
            }
        }
    }

    const filesystem::AbsolutePath abandoned = root.AddFilePath("abandoned.tmp");
    {
        const u8 stale[] = {0xde, 0xad};
        auto writer = manager.CreateFileWriter(abandoned);
        Check(static_cast<bool>(writer), "create interrupted-publication temporary");
        if (writer)
        {
            writer->Serialize(const_cast<u8*>(stale), sizeof(stale));
            writer->Flush();
            writer.Reset();
        }
    }

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "corruption-recovery build-system initialization");
        Check(!manager.FileExist(abandoned), "startup removes abandoned temporary");
        assets::BuildOutput recovered;
        Check(system.Build(request, recovered) == assets::Result::Success && recovered.disposition == assets::BuildDisposition::Built &&
                  state.compileCalls.GetValue() == 2,
              "corrupt record is rejected and rebuilt");
        const assets::Stats stats = system.GetStats();
        Check(stats.persistentCacheRecoveries == 1 && stats.persistentCacheCorruptions == 1 && stats.persistentCacheStores == 1,
              "recovery and corruption telemetry");
        Check(system.Shutdown(), "recovery shutdown");
    }

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "final persistent build-system initialization");
        assets::BuildOutput cached;
        Check(system.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
                  state.compileCalls.GetValue() == 2,
              "rebuilt record is durable and valid");
        Check(system.Shutdown(), "final shutdown");
    }

    {
        const u8 concurrentSettings[] = {0xc0, 0xde};
        assets::BuildRequest concurrentRequest = request;
        concurrentRequest.settings = {concurrentSettings, sizeof(concurrentSettings)};
        assets::BuildSystem firstSystem;
        assets::BuildSystem secondSystem;
        Check(InitializeBuildSystem(firstSystem, config, compiler) && InitializeBuildSystem(secondSystem, config, compiler),
              "concurrent publisher initialization");

        const u32 callsBefore = state.compileCalls.GetValue();
        state.barrierReady.SetValue(0);
        state.synchronizeCompile = true;
        BuildThread firstThread("DDC publisher A", firstSystem, concurrentRequest);
        BuildThread secondThread("DDC publisher B", secondSystem, concurrentRequest);
        firstThread.InitThread();
        secondThread.InitThread();
        firstThread.JoinThread();
        secondThread.JoinThread();
        state.synchronizeCompile = false;

        Check(firstThread.result == assets::Result::Success && secondThread.result == assets::Result::Success &&
                  firstThread.output.buildFingerprint == secondThread.output.buildFingerprint &&
                  firstThread.output.contentFingerprint == secondThread.output.contentFingerprint &&
                  state.compileCalls.GetValue() == callsBefore + 2,
              "concurrent identical publishers converge");
        Check(firstSystem.Shutdown() && secondSystem.Shutdown(), "concurrent publisher shutdown");

        containers::DynamicArray<filesystem::AbsolutePath> temporary(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*.tmp"), temporary, true);
        Check(temporary.Empty(), "publication race leaves no temporary files");

        assets::BuildSystem verificationSystem;
        Check(InitializeBuildSystem(verificationSystem, config, compiler), "publication-race verification initialization");
        assets::BuildOutput verified;
        Check(verificationSystem.Build(concurrentRequest, verified) == assets::Result::Success &&
                  verified.disposition == assets::BuildDisposition::CacheHit && state.compileCalls.GetValue() == callsBefore + 2,
              "publication-race result is durable");
        Check(verificationSystem.Shutdown(), "publication-race verification shutdown");
    }

    DeleteTestFiles(manager, root);
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
