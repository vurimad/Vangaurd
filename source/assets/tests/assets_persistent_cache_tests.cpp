#include <vanguard/assets/assets.hpp>
#include <vanguard/assets/derived_data_artifact_source.hpp>
#include <vanguard/assets/loose_resource_materializer.hpp>
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

    [[nodiscard]] filesystem::AbsolutePath RecordPath(const filesystem::AbsolutePath& root, const assets::BuildFingerprint& fingerprint) noexcept
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
        const u8 primary[] = {0x56, 0x44, 0x44, 0x43, context.request.source.content[0]};
        const u8 streamable[] = {0xa1, 0xb2, context.request.source.content[0]};
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, primary,
                          sizeof(primary)) == assets::Result::Success &&
               writer.Add(context.request.output, 1, assets::ArtifactFlags::Streamable, 4, streamable, sizeof(streamable)) ==
                   assets::Result::Success;
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

    [[nodiscard]] bool InitializeBuildSystem(assets::BuildSystem& system, const assets::Config& config, const assets::CompilerDescriptor& compiler) noexcept
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
    assets::BuildFingerprint contentFingerprint;
    assets::DependencyRecord materializationRecord;
    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "first persistent build-system initialization");
        assets::BuildOutput built;
        Check(system.Build(request, built) == assets::Result::Success && built.disposition == assets::BuildDisposition::Built &&
                  state.compileCalls.GetValue() == 1,
              "cache miss compiles and publishes");
        fingerprint = built.buildFingerprint;
        contentFingerprint = built.contentFingerprint;
        materializationRecord.output = output;
        materializationRecord.buildFingerprint = built.buildFingerprint;
        materializationRecord.contentFingerprint = built.contentFingerprint;
        materializationRecord.artifacts.Resize(built.artifacts.Size());
        for (u32 index = 0; index < built.artifacts.Size(); ++index)
        {
            const assets::Artifact& artifact = built.artifacts[index];
            materializationRecord.artifacts[index] =
                {artifact.resource, artifact.segment, artifact.flags, artifact.alignmentLog2, artifact.bytes.Size()};
        }
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
        assets::DerivedDataArtifactSource sourceReader;
        assets::DerivedDataArtifactSourceConfig sourceConfig;
        sourceConfig.root = root.AsChar();
        sourceConfig.limits.validationScratchBytes = 2;
        Check(sourceReader.Initialize(sourceConfig), "derived-data artifact source initialization");

        assets::ArtifactSetReader artifactSet;
        Check(sourceReader.Open({fingerprint, contentFingerprint}, artifactSet) == assets::DerivedDataArtifactResult::Success &&
                  artifactSet.IsOpen() && artifactSet.Key().build == fingerprint && artifactSet.Key().content == contentFingerprint &&
                  artifactSet.Artifacts().Count() == 2,
              "bounded canonical reader opens and validates artifact set");
        const assets::CachedArtifactDescriptor* const streamable = artifactSet.Find(output, 1);
        Check(streamable != nullptr && streamable->flags == assets::ArtifactFlags::Streamable && streamable->byteCount == 3,
              "artifact lookup uses resource and segment identity");
        if (streamable != nullptr)
        {
            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            Check(artifactSet.Read(*streamable, bytes) == assets::DerivedDataArtifactResult::Success && bytes.Size() == 3 &&
                      bytes[0] == 0xa1 && bytes[1] == 0xb2 && bytes[2] == sourceBytes[0],
                  "selective artifact read returns byte-exact segment");

            const filesystem::AbsolutePath copiedPath = root.AddFilePath("copied-segment.bin");
            auto copiedFile = manager.CreateFileWriter(copiedPath, filesystem::FOF_Buffered);
            u8 scratch[2] = {};
            Check(copiedFile && artifactSet.CopyTo(*streamable, *copiedFile, scratch) ==
                                    assets::DerivedDataArtifactResult::Success,
                  "bounded artifact copy streams into caller-owned file");
            if (copiedFile)
                copiedFile->Flush();
            copiedFile.Reset();
            Check(manager.GetFileSize(copiedPath) == 3, "bounded artifact copy writes exact extent");
            static_cast<void>(manager.DeleteFile(copiedPath));

            assets::CachedArtifactDescriptor mismatch = *streamable;
            ++mismatch.byteCount;
            Check(artifactSet.Read(mismatch, bytes) == assets::DerivedDataArtifactResult::DescriptorMismatch && bytes.Empty(),
                  "descriptor mismatch is rejected before payload delivery");
            mismatch = *streamable;
            mismatch.flags = assets::ArtifactFlags::MemoryResident;
            Check(artifactSet.Read(mismatch, bytes) == assets::DerivedDataArtifactResult::DescriptorMismatch && bytes.Empty(),
                  "artifact flag mismatch is rejected before payload delivery");
            mismatch = *streamable;
            --mismatch.alignmentLog2;
            Check(artifactSet.Read(mismatch, bytes) == assets::DerivedDataArtifactResult::DescriptorMismatch && bytes.Empty(),
                  "artifact alignment mismatch is rejected before payload delivery");
        }

        assets::BuildOutput reconstructed;
        Check(artifactSet.ReadAll(reconstructed) == assets::DerivedDataArtifactResult::Success && reconstructed.artifacts.Size() == 2 &&
                  reconstructed.contentFingerprint == contentFingerprint && reconstructed.artifacts[0].bytes.Size() == 5 &&
                  reconstructed.artifacts[1].bytes.Size() == 3,
              "shared reader reconstructs complete BuildOutput");

        const filesystem::AbsolutePath looseTarget = root.AddFilePath("materialized.asset");
        const filesystem::AbsolutePath looseTemporary = root.AddFilePath("materialized.asset.tmp");
        const u8 previousTarget[] = {0xde, 0xad, 0xfa, 0xce};
        {
            auto previous = manager.CreateFileWriter(looseTarget, filesystem::FOF_Buffered);
            Check(static_cast<bool>(previous), "existing loose target fixture");
            if (previous)
            {
                previous->Serialize(const_cast<u8*>(previousTarget), sizeof(previousTarget));
                previous->Flush();
            }
        }
        assets::LooseMaterializationLimits looseLimits;
        looseLimits.scratchBytes = 2;
        assets::LooseResourceMaterializer materializer;
        Check(materializer.Materialize(materializationRecord, output, sourceReader, looseTarget, looseTemporary, looseLimits) ==
                  assets::LooseMaterializationResult::Success &&
                  manager.FileExist(looseTarget) && !manager.FileExist(looseTemporary) && manager.GetFileSize(looseTarget) == 8,
              "generic materializer reconstructs and safely replaces loose resource");
        {
            const u8 expected[] = {0x56, 0x44, 0x44, 0x43, sourceBytes[0], 0xa1, 0xb2, sourceBytes[0]};
            u8 actual[sizeof(expected)] = {};
            auto loose = manager.CreateFileReader(looseTarget, filesystem::FOF_Buffered);
            Check(static_cast<bool>(loose), "open reconstructed loose resource");
            if (loose)
                loose->Serialize(actual, sizeof(actual));
            bool equal = true;
            for (u32 index = 0; index < static_cast<u32>(sizeof(expected)); ++index)
                equal = equal && actual[index] == expected[index];
            Check(equal, "loose resource is byte-exact descriptor-order concatenation");
        }

        {
            auto previous = manager.CreateFileWriter(looseTarget, filesystem::FOF_Buffered);
            if (previous)
            {
                previous->Serialize(const_cast<u8*>(previousTarget), sizeof(previousTarget));
                previous->Flush();
            }
        }
        assets::DependencyRecord missingSegment;
        missingSegment.output = materializationRecord.output;
        missingSegment.buildFingerprint = materializationRecord.buildFingerprint;
        missingSegment.contentFingerprint = materializationRecord.contentFingerprint;
        missingSegment.artifacts = materializationRecord.artifacts;
        missingSegment.artifacts[1].segment = 2;
        Check(materializer.Materialize(missingSegment, output, sourceReader, looseTarget, looseTemporary, looseLimits) ==
                  assets::LooseMaterializationResult::MissingSegment &&
                  manager.FileExist(looseTarget) && manager.GetFileSize(looseTarget) == sizeof(previousTarget) &&
                  !manager.FileExist(looseTemporary),
              "missing segment fails before staging and preserves previous target");
        static_cast<void>(manager.DeleteFile(looseTarget));

        assets::BuildFingerprint wrongContent = contentFingerprint;
        wrongContent.bytes[0] ^= 0xffu;
        Check(sourceReader.Open({fingerprint, wrongContent}, artifactSet) == assets::DerivedDataArtifactResult::ContentMismatch &&
                  !artifactSet.IsOpen(),
              "content identity mismatch is explicit");
        assets::BuildFingerprint missing = fingerprint;
        missing.bytes[0] ^= 0xffu;
        Check(sourceReader.Open({missing, {}}, artifactSet) == assets::DerivedDataArtifactResult::NotFound && !artifactSet.IsOpen(),
              "missing artifact set is distinct from corruption");

        const filesystem::AbsolutePath wrongBuildRecord = RecordPath(root, missing);
        const u64 validRecordSize = manager.GetFileSize(record);
        containers::DynamicArray<u8> recordBytes(memory::pools::Assets::GetInstance());
        recordBytes.Resize(static_cast<u32>(validRecordSize));
        auto validRecord = manager.CreateFileReader(record, filesystem::FOF_Buffered);
        Check(validRecord && recordBytes.Size() == validRecordSize, "read valid record for wrong-build proof");
        if (validRecord && recordBytes.Size() == validRecordSize)
            validRecord->Serialize(recordBytes.Data(), recordBytes.Size());
        validRecord.Reset();
        Check(manager.CreatePath(filesystem::paths::ParentAbsolutePath(wrongBuildRecord)),
              "create wrong-build record directory");
        auto wrongRecord = manager.CreateFileWriter(wrongBuildRecord, filesystem::FOF_Buffered);
        Check(static_cast<bool>(wrongRecord), "stage record under mismatched build key");
        if (wrongRecord)
        {
            wrongRecord->Serialize(recordBytes.Data(), recordBytes.Size());
            wrongRecord->Flush();
        }
        wrongRecord.Reset();
        Check(sourceReader.Open({missing, {}}, artifactSet) == assets::DerivedDataArtifactResult::Corrupt && !artifactSet.IsOpen(),
              "record stored under the wrong build identity is corrupt");
        static_cast<void>(manager.DeleteFile(wrongBuildRecord));
        Check(sourceReader.Shutdown(), "derived-data artifact source shutdown");
    }

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "second persistent build-system initialization");
        assets::BuildOutput cached;
        Check(system.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
                  cached.buildFingerprint == fingerprint && cached.artifacts.Size() == 2 && cached.artifacts[0].bytes[4] == sourceBytes[0] &&
                  state.compileCalls.GetValue() == 1,
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
                  firstThread.output.contentFingerprint == secondThread.output.contentFingerprint && state.compileCalls.GetValue() == callsBefore + 2,
              "concurrent identical publishers converge");
        Check(firstSystem.Shutdown() && secondSystem.Shutdown(), "concurrent publisher shutdown");

        containers::DynamicArray<filesystem::AbsolutePath> temporary(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*.tmp"), temporary, true);
        Check(temporary.Empty(), "publication race leaves no temporary files");

        assets::BuildSystem verificationSystem;
        Check(InitializeBuildSystem(verificationSystem, config, compiler), "publication-race verification initialization");
        assets::BuildOutput verified;
        Check(verificationSystem.Build(concurrentRequest, verified) == assets::Result::Success && verified.disposition == assets::BuildDisposition::CacheHit &&
                  state.compileCalls.GetValue() == callsBefore + 2,
              "publication-race result is durable");
        Check(verificationSystem.Shutdown(), "publication-race verification shutdown");
    }

    DeleteTestFiles(manager, root);
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
