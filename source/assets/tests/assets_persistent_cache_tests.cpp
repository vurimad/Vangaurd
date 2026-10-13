#include "../../../tests/support/scratch_directory.hpp"
#include "../private/vanguard/assets/derived_data_artifact_source_internal.hpp"
#include <vanguard/assets/assets.hpp>
#include <vanguard/assets/derived_data_artifact_source.hpp>
#include <vanguard/assets/loose_resource_materializer.hpp>
#include <vanguard/assets/project_asset_build_system.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <cstdio>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x53524345u;
    constexpr resources::ResourceTypeId OutputType = 0x434f4f4bu;
    constexpr resources::ResourceTypeId OtherOutputType = 0x4f544852u;

    enum class GeneratedFallbackMode : u8
    {
        Resolve,
        Miss,
        WrongOutput,
        WrongTarget
    };

    struct CompilerState
    {
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<u32> barrierReady;
        bool synchronizeCompile = false;
        resources::ResourceReference generatedDependency;
        u32 generatedFallbackCalls = 0u;
        GeneratedFallbackMode generatedFallbackMode = GeneratedFallbackMode::Resolve;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetsPersistentCacheTests] FAILED: %s", message);
            std::fprintf(stderr, "[assetsPersistentCacheTests] FAILED: %s\n", message);
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

    [[nodiscard]] filesystem::AbsolutePath StagePath(const filesystem::AbsolutePath& root, const assets::BuildFingerprint& fingerprint) noexcept
    {
        char first[3] = {HexDigit(fingerprint.bytes[0] >> 4u), HexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {HexDigit(fingerprint.bytes[1] >> 4u), HexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        char name[74];
        for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
        {
            name[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
            name[index * 2u + 1u] = HexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        constexpr char suffix[] = ".vddc.tmp";
        for (u32 index = 0; index < sizeof(suffix); ++index)
            name[64u + index] = suffix[index];
        return root.AddDirPath(first).AddDirPath(second).AddFilePath(name);
    }

    [[nodiscard]] filesystem::AbsolutePath MalformedRecordPath(const filesystem::AbsolutePath& root,
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
        name[17] = 'g';
        name[64] = '.';
        name[65] = 'v';
        name[66] = 'd';
        name[67] = 'd';
        name[68] = 'c';
        name[69] = '\0';
        return root.AddDirPath(first).AddDirPath(second).AddFilePath(name);
    }

    [[nodiscard]] filesystem::AbsolutePath WrongShardRecordPath(const filesystem::AbsolutePath& root,
                                                                const assets::BuildFingerprint& fingerprint) noexcept
    {
        const u8 wrongFirstByte = static_cast<u8>(fingerprint.bytes[0] ^ 0xffu);
        char first[3] = {HexDigit(wrongFirstByte >> 4u), HexDigit(wrongFirstByte & 0x0fu), '\0'};
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

    [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* const userData) noexcept
    {
        const auto& state = *static_cast<const CompilerState*>(userData);
        if (state.generatedDependency.IsValid() && request.output != state.generatedDependency)
            return dependencies.Add({state.generatedDependency, {}, assets::DependencyRole::Generated,
                                     assets::DependencyRequirement::Required}) == assets::Result::Success;
        return true;
    }

    [[nodiscard]] bool Estimate(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                assets::BuildResourceEstimate& estimate, void*) noexcept
    {
        estimate.compilerTransientBytes = 64u;
        estimate.artifactBytes = 64u;
        return true;
    }

    [[nodiscard]] bool ResolveGeneratedFallback(const assets::BuildDependency& dependency, assets::OwnedBuildRequest& request,
                                                void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        ++state.generatedFallbackCalls;
        if (dependency.identity != state.generatedDependency || state.generatedFallbackMode == GeneratedFallbackMode::Miss)
            return false;
        const u8 sourceBytes[] = {0xc1u, 0xa5u, 0x7eu};
        const u8 settingsBytes[] = {0x31u, 0x32u};
        const resources::ResourceReference output =
            state.generatedFallbackMode == GeneratedFallbackMode::WrongOutput
                ? resources::ResourceReference(resources::ResourcePath::FromString("generated/wrong-output.asset"), OutputType)
                : dependency.identity;
        const assets::TargetPlatform target = state.generatedFallbackMode == GeneratedFallbackMode::WrongTarget
                                                  ? assets::TargetPlatform::WindowsVulkan
                                                  : assets::TargetPlatform::WindowsD3D12;
        const assets::BuildRequest borrowed{
            {resources::ResourceReference(resources::ResourcePath::FromString("generated/fallback.asset"), SourceType),
             {sourceBytes, sizeof(sourceBytes)}, {}},
            output, target, {settingsBytes, sizeof(settingsBytes)}};
        return request.CopyFrom(borrowed);
    }

    [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
    {
        containers::String path(source.AsStringView().Data(), source.AsStringView().Length());
        path.Append(".vmeta", 6u);
        return filesystem::AbsolutePath::CreateFilePath(path);
    }

    [[nodiscard]] bool WriteBytes(filesystem::Manager& manager, const filesystem::AbsolutePath& path, const u8* const bytes,
                                  const u32 size) noexcept
    {
        auto writer = manager.CreateFileWriter(path, filesystem::FOF_Buffered);
        if (!writer)
            return false;
        if (size != 0u)
            writer->Serialize(const_cast<u8*>(bytes), size);
        writer->Flush();
        return !writer->HasErrors() && writer->GetSize() == size;
    }

    [[nodiscard]] bool WriteMetadata(filesystem::Manager& manager, const filesystem::AbsolutePath& source,
                                     const assets::AssetMetadata& metadata) noexcept
    {
        containers::String text;
        return assets::EncodeMetadata(metadata, text) == assets::MetadataResult::Success &&
               WriteBytes(manager, MetadataPath(source), reinterpret_cast<const u8*>(text.AsChar()), text.Length());
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

    void RunGeneratedFallbackTests(filesystem::Manager& manager, const filesystem::AbsolutePath& working,
                                   const assets::CompilerDescriptor& compiler, CompilerState& state) noexcept
    {
        const filesystem::AbsolutePath sourceRoot = working.AddDirPath("vanguard_assets_generated_fallback_sources");
        const filesystem::AbsolutePath cacheRoot = working.AddDirPath("vanguard_assets_generated_fallback_cache");
        const filesystem::AbsolutePath indexRoot = working.AddDirPath("vanguard_assets_generated_fallback_index");
        DeleteTestFiles(manager, sourceRoot);
        DeleteTestFiles(manager, cacheRoot);
        DeleteTestFiles(manager, indexRoot);
        Check(manager.CreatePath(sourceRoot) && manager.CreatePath(cacheRoot) && manager.CreatePath(indexRoot),
              "generated fallback fixture roots creation");

        const filesystem::AbsolutePath sourcePath = sourceRoot.AddFilePath("root.asset");
        const filesystem::AbsolutePath unavailableSourcePath = sourceRoot.AddFilePath("unavailable.asset");
        const u8 rootBytes[] = {0x19u, 0x27u, 0x35u};
        const u8 unavailableBytes[] = {0x73u, 0x81u, 0x92u};
        assets::AssetMetadata metadata;
        metadata.id.bytes[0] = 0x51u;
        metadata.importer = compiler.id;
        metadata.importerVersion = compiler.version;
        metadata.settingsVersion = 1u;
        assets::AssetOutput& first = metadata.outputs.EmplaceBack();
        first.key = "root-a";
        first.type = OutputType;
        assets::AssetOutput& second = metadata.outputs.EmplaceBack();
        second.key = "root-b";
        second.type = OutputType;
        assets::AssetMetadata unavailableMetadata;
        unavailableMetadata.id.bytes[0] = 0x52u;
        unavailableMetadata.importer = compiler.id;
        unavailableMetadata.importerVersion = compiler.version + 1u;
        unavailableMetadata.settingsVersion = 1u;
        assets::AssetOutput& unavailable = unavailableMetadata.outputs.EmplaceBack();
        unavailable.key = "unavailable";
        unavailable.type = OutputType;
        Check(WriteBytes(manager, sourcePath, rootBytes, sizeof(rootBytes)) && WriteMetadata(manager, sourcePath, metadata) &&
                  WriteBytes(manager, unavailableSourcePath, unavailableBytes, sizeof(unavailableBytes)) &&
                  WriteMetadata(manager, unavailableSourcePath, unavailableMetadata),
              "generated fallback fixture sources and metadata write");

        const assets::SourceRoot root{sourceRoot, false};
        const assets::SourceFile sourceFiles[]{{sourcePath, 0u}, {unavailableSourcePath, 0u}};
        assets::SourceDatabase sources;
        Check(sources.Rebuild(manager, {&root, 1u}, {sourceFiles, 2u}) == assets::SourceDatabaseResult::Success,
              "generated fallback fixture catalog rebuild");
        const resources::ResourceReference firstOutput = assets::GetAssetOutputReference(metadata.id, metadata.outputs[0]);
        const resources::ResourceReference secondOutput = assets::GetAssetOutputReference(metadata.id, metadata.outputs[1]);
        const resources::ResourceReference unavailableOutput =
            assets::GetAssetOutputReference(unavailableMetadata.id, unavailableMetadata.outputs[0]);
        Check(sources.LookupOutput(firstOutput) != nullptr && sources.LookupOutput(secondOutput) != nullptr &&
                  sources.LookupOutput(unavailableOutput) != nullptr,
              "generated fallback fixture declares every project-owned output");

        const assets::SourceImporterDescriptor importer = assets::MakeDirectSourceImporterDescriptor(compiler, nullptr);
        assets::ProjectAssetBuildConfig config;
        config.artifactCacheRoot = cacheRoot;
        config.dependencyIndexRoot = indexRoot;
        config.target = assets::TargetPlatform::WindowsD3D12;
        config.settingsFingerprint.bytes[0] = 0x61u;
        config.buildSystem.maximumCacheEntries = 0u;
        config.resolveGeneratedFallback = &ResolveGeneratedFallback;
        config.resolveGeneratedFallbackUserData = &state;
        assets::ProjectAssetBuildSystem project;
        Check(project.Initialize(manager, sources, {&compiler, 1u}, {&importer, 1u}, config) == assets::ProjectAssetBuildResult::Success,
              "project build system accepts one undeclared-generated fallback provider");

        state.generatedFallbackCalls = 0u;
        state.generatedFallbackMode = GeneratedFallbackMode::Resolve;
        state.generatedDependency = secondOutput;
        assets::ProjectAssetBuildResult requestResult = assets::ProjectAssetBuildResult::InvalidArgument;
        assets::GraphRequest declaredRequest = project.Request(firstOutput, assets::BuildPriority::Normal, &requestResult);
        if (declaredRequest)
            declaredRequest.Wait();
        Check(requestResult == assets::ProjectAssetBuildResult::Success && declaredRequest.HasSucceeded() &&
                  state.generatedFallbackCalls == 0u,
              "declared generated dependencies remain authoritative and never consult the fallback provider");
        declaredRequest.Reset();

        state.generatedFallbackCalls = 0u;
        state.generatedDependency = resources::ResourceReference(resources::ResourcePath::FromString("generated/canonical-child.asset"), OutputType);
        const u32 callsBeforeSharedChild = state.compileCalls.GetValue();
        assets::GraphRequest firstRequest = project.Request(firstOutput, assets::BuildPriority::Normal, &requestResult);
        assets::GraphRequest secondRequest = project.Request(secondOutput, assets::BuildPriority::Normal, &requestResult);
        if (firstRequest)
            firstRequest.Wait();
        if (secondRequest)
            secondRequest.Wait();
        Check(requestResult == assets::ProjectAssetBuildResult::Success && firstRequest.HasSucceeded() && secondRequest.HasSucceeded() &&
                  state.generatedFallbackCalls == 2u && state.compileCalls.GetValue() == callsBeforeSharedChild + 3u,
              "stack-local fallback payload is deeply owned and a shared undeclared child coalesces to one compile");
        firstRequest.Reset();
        secondRequest.Reset();

        const u32 callsBeforeManagedPath = state.generatedFallbackCalls;
        state.generatedDependency = resources::ResourceReference(secondOutput.GetPath(), OtherOutputType);
        assets::GraphRequest managedPathRequest = project.Request(secondOutput, assets::BuildPriority::Normal, &requestResult);
        if (managedPathRequest)
            managedPathRequest.Wait();
        Check(requestResult == assets::ProjectAssetBuildResult::Success && managedPathRequest.HasFinished() && !managedPathRequest.HasSucceeded() &&
                  managedPathRequest.GetError() == assets::BuildFailure::ResolutionFailed &&
                  state.generatedFallbackCalls == callsBeforeManagedPath,
              "managed project output paths never fall through to the undeclared generated provider on a wrong-type lookup");
        managedPathRequest.Reset();

        const u32 callsBeforeUnavailableImporter = state.generatedFallbackCalls;
        state.generatedDependency = unavailableOutput;
        assets::GraphRequest unavailableImporterRequest = project.Request(firstOutput, assets::BuildPriority::Normal, &requestResult);
        if (unavailableImporterRequest)
            unavailableImporterRequest.Wait();
        Check(requestResult == assets::ProjectAssetBuildResult::Success && unavailableImporterRequest.HasFinished() &&
                  !unavailableImporterRequest.HasSucceeded() && unavailableImporterRequest.GetError() == assets::BuildFailure::ResolutionFailed &&
                  state.generatedFallbackCalls == callsBeforeUnavailableImporter,
              "managed generated outputs with an unavailable persisted importer fail authoritatively without fallback");
        unavailableImporterRequest.Reset();

        const auto checkRejectedFallback = [&](const char* const path, const GeneratedFallbackMode mode, const char* const message) noexcept
        {
            const u32 callsBefore = state.generatedFallbackCalls;
            state.generatedDependency = resources::ResourceReference(resources::ResourcePath::FromString(path), OutputType);
            state.generatedFallbackMode = mode;
            assets::GraphRequest rejected = project.Request(firstOutput, assets::BuildPriority::Normal, &requestResult);
            if (rejected)
                rejected.Wait();
            Check(requestResult == assets::ProjectAssetBuildResult::Success && rejected.HasFinished() && !rejected.HasSucceeded() &&
                      rejected.GetError() == assets::BuildFailure::ResolutionFailed && state.generatedFallbackCalls == callsBefore + 1u,
                  message);
            rejected.Reset();
        };
        checkRejectedFallback("generated/provider-miss.asset", GeneratedFallbackMode::Miss,
                              "required generated dependency fails when the fallback provider declines it");
        checkRejectedFallback("generated/provider-wrong-output.asset", GeneratedFallbackMode::WrongOutput,
                              "fallback provider cannot substitute a different generated output identity");
        checkRejectedFallback("generated/provider-wrong-target.asset", GeneratedFallbackMode::WrongTarget,
                              "fallback provider cannot escape the project build target");

        state.generatedFallbackMode = GeneratedFallbackMode::Resolve;
        state.generatedDependency = {};
        Check(project.Shutdown() == assets::ProjectAssetBuildResult::Success, "generated fallback project build-system shutdown");

        DeleteTestFiles(manager, sourceRoot);
        DeleteTestFiles(manager, cacheRoot);
        DeleteTestFiles(manager, indexRoot);
    }

    void RunPersistentMaintenanceTests(filesystem::Manager& manager, const filesystem::AbsolutePath& working,
                                       const assets::CompilerDescriptor& compiler) noexcept
    {
        const filesystem::AbsolutePath maintenanceRoot = working.AddDirPath("vanguard_assets_maintenance_tests");
        const filesystem::AbsolutePath maintenanceIndexRoot = working.AddDirPath("vanguard_assets_maintenance_index");
        DeleteTestFiles(manager, maintenanceRoot);
        DeleteTestFiles(manager, maintenanceIndexRoot);
        Check(manager.CreatePath(maintenanceRoot), "maintenance root creation");
        Check(manager.CreatePath(maintenanceIndexRoot), "maintenance index root creation");

        assets::SourceDatabase emptySources;
        assets::SourceImporterDescriptor importer = assets::MakeDirectSourceImporterDescriptor(compiler, nullptr);
        const containers::ArraySpan<const assets::CompilerDescriptor> compilers(&compiler, 1u);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> importers(&importer, 1u);
        assets::ProjectAssetBuildConfig projectConfig;
        projectConfig.artifactCacheRoot = maintenanceRoot;
        projectConfig.dependencyIndexRoot = maintenanceIndexRoot;
        projectConfig.target = assets::TargetPlatform::WindowsD3D12;
        projectConfig.settingsFingerprint.bytes[0] = 0x50u;
        projectConfig.buildSystem.maximumCacheEntries = 0u;

        assets::ProjectAssetBuildSystem project;
        Check(project.Initialize(manager, emptySources, compilers, importers, projectConfig) == assets::ProjectAssetBuildResult::Success,
              "maintenance project build-system initialization");
        assets::BuildSystem* const system = project.GetBuildSystem();
        Check(system != nullptr && project.GetDependencyIndex() != nullptr, "maintenance public project owner exposes live assets owners");

        const auto makeRequest = [](const char* sourcePath, const char* outputPath, const u8 marker) noexcept
        {
            static u8 sourceBytes[8][3] = {};
            static u8 settingsBytes[8][2] = {};
            const u32 slot = marker & 7u;
            sourceBytes[slot][0] = marker;
            sourceBytes[slot][1] = static_cast<u8>(marker + 1u);
            sourceBytes[slot][2] = static_cast<u8>(marker + 2u);
            settingsBytes[slot][0] = marker;
            settingsBytes[slot][1] = static_cast<u8>(marker ^ 0x5au);
            return assets::BuildRequest{
                {resources::ResourceReference(resources::ResourcePath::FromString(sourcePath), SourceType),
                 {sourceBytes[slot], sizeof(sourceBytes[slot])}, {}},
                resources::ResourceReference(resources::ResourcePath::FromString(outputPath), OutputType),
                assets::TargetPlatform::WindowsD3D12,
                {settingsBytes[slot], sizeof(settingsBytes[slot])}};
        };

        assets::PersistentCacheMaintenanceLimits limits;
        limits.maximumCacheBytes = 1u;
        limits.maximumProtectionRecords = 64u;
        limits.maximumProtectionBytes = 64u * sizeof(assets::ArtifactSetKey);
        limits.maximumScanEntries = 64u;
        limits.maximumScanBytes = 16ull * 1024ull * 1024ull;
        limits.maximumDeleteRecords = 16u;
        limits.maximumDiagnosticRows = 2u;

        containers::DynamicArray<assets::PersistentCacheMaintenanceDiagnostic> diagnostics(memory::pools::Assets::GetInstance());
        assets::PersistentCacheMaintenanceReport report;
        const containers::ArraySpan<const assets::ArtifactSetKey> noProtections;

        const assets::BuildRequest publicationRequest =
            makeRequest("source/maintenance-publication.asset", "cooked/maintenance-publication.asset", 0x31u);
        assets::BuildOutput livePublication;
        Check(system != nullptr && system->Build(publicationRequest, livePublication) == assets::Result::Success,
              "maintenance publication fixture build");
        const filesystem::AbsolutePath publicationRecord = RecordPath(maintenanceRoot, livePublication.buildFingerprint);
        const u64 publicationEpoch = project.GetPersistentProtectionEpoch();
        Check(project.MaintainPersistentCache(noProtections, publicationEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.deferredDeletes == 1u && report.removedRecords == 0u && manager.FileExist(publicationRecord) &&
                  diagnostics.Size() == 1u && diagnostics[0].reason == assets::PersistentCacheMaintenanceDiagnosticReason::LeaseBusy,
              "live publication lease defers maintenance deletion through cache authority");
        livePublication.Reset();
        Check(project.MaintainPersistentCache(noProtections, publicationEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.removedRecords == 1u && report.budgetSatisfied && !manager.FileExist(publicationRecord),
              "released publication becomes eligible and is deleted by cache owner");

        const assets::BuildRequest protectedRequest =
            makeRequest("source/maintenance-protected.asset", "cooked/maintenance-protected.asset", 0x32u);
        assets::BuildOutput protectedOutput;
        Check(system != nullptr && system->Build(protectedRequest, protectedOutput) == assets::Result::Success,
              "maintenance protected fixture build");
        const assets::BuildFingerprint protectedBuild = protectedOutput.buildFingerprint;
        assets::BuildFingerprint wrongContent = protectedOutput.contentFingerprint;
        wrongContent.bytes[0] ^= 0xffu;
        const filesystem::AbsolutePath protectedRecord = RecordPath(maintenanceRoot, protectedBuild);
        protectedOutput.Reset();
        const assets::ArtifactSetKey mismatchedContentProtection{protectedBuild, wrongContent};
        const containers::ArraySpan<const assets::ArtifactSetKey> protectedByBuild(&mismatchedContentProtection, 1u);
        const u64 protectedEpoch = project.GetPersistentProtectionEpoch();
        Check(project.MaintainPersistentCache(protectedByBuild, protectedEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.protectedRecords == 1u && report.protectedOverBudget && report.removedRecords == 0u &&
                  manager.FileExist(protectedRecord),
              "VDDC protection identity is build fingerprint even when content fingerprint differs");

        assets::DerivedDataArtifactSource readerSource;
        assets::DerivedDataArtifactSourceConfig readerConfig;
        readerConfig.root = maintenanceRoot.AsChar();
        Check(readerSource.Initialize(readerConfig), "maintenance reader source initialization");
        assets::ArtifactSetReader liveReader;
        Check(readerSource.Open({protectedBuild, {}}, liveReader) == assets::DerivedDataArtifactResult::Success,
              "maintenance reader lease fixture opens");
        Check(project.MaintainPersistentCache(noProtections, protectedEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.deferredDeletes == 1u && manager.FileExist(protectedRecord),
              "live reader lease defers maintenance deletion");
        liveReader.Close();
        Check(readerSource.Shutdown(), "maintenance reader source shutdown");

        const u64 staleEpoch = project.GetPersistentProtectionEpoch();
        const assets::BuildRequest epochRequest =
            makeRequest("source/maintenance-epoch.asset", "cooked/maintenance-epoch.asset", 0x33u);
        assets::BuildPlan epochPlan;
        assets::BuildOutput epochOutput;
        Check(system != nullptr && system->Prepare(epochRequest, epochPlan) == assets::Result::Success &&
                  system->Execute(epochRequest, epochPlan, epochOutput) == assets::Result::Success &&
                  project.GetDependencyIndex()->Publish(epochRequest, epochPlan, epochOutput) == assets::IndexResult::Success,
              "maintenance epoch fixture publishes through VADI authority");
        const assets::BuildFingerprint epochBuild = epochOutput.buildFingerprint;
        const filesystem::AbsolutePath epochRecord = RecordPath(maintenanceRoot, epochBuild);
        const u64 currentEpoch = project.GetPersistentProtectionEpoch();
        Check(currentEpoch != staleEpoch &&
                  project.MaintainPersistentCache(noProtections, staleEpoch, limits, report, diagnostics) ==
                      assets::PersistentCacheMaintenanceResult::ProtectionChanged &&
                  manager.FileExist(epochRecord),
              "stale protection epoch rejects destructive maintenance before scan/delete");
        epochOutput.Reset();

        containers::DynamicArray<assets::ArtifactSetKey> durableProtections(memory::pools::Assets::GetInstance());
        u64 durableEpoch = 0u;
        Check(project.CapturePersistentProtectionSnapshot(durableProtections, 64u, 64u * sizeof(assets::ArtifactSetKey), durableEpoch) ==
                  assets::PersistentCacheMaintenanceResult::Success && durableEpoch == currentEpoch,
              "maintenance captures one epoch-bracketed VADI and graph protection snapshot");
        bool containsEpochBuild = false;
        for (const assets::ArtifactSetKey& key : durableProtections)
            containsEpochBuild = containsEpochBuild || key.build == epochBuild;
        Check(containsEpochBuild, "committed VADI generation participates in durable maintenance protection");

        const filesystem::AbsolutePath foreign = maintenanceRoot.AddFilePath("foreign.keep");
        const filesystem::AbsolutePath foreignTemporary = maintenanceRoot.AddFilePath("foreign.tmp");
        const filesystem::AbsolutePath malformedRecord = MalformedRecordPath(maintenanceRoot, protectedBuild);
        const filesystem::AbsolutePath wrongShardRecord = WrongShardRecordPath(maintenanceRoot, protectedBuild);
        const filesystem::AbsolutePath ownedStage = StagePath(maintenanceRoot, protectedBuild);
        const filesystem::AbsolutePath outsideRoot = working.AddDirPath("vanguard_assets_maintenance_tests_escape");
        const filesystem::AbsolutePath outsideRecord = RecordPath(outsideRoot, protectedBuild);
        const u8 sentinel[] = {0xaa, 0x55};
        DeleteTestFiles(manager, outsideRoot);
        Check(manager.CreatePath(outsideRoot) && manager.CreatePath(filesystem::paths::ParentAbsolutePath(outsideRecord)),
              "maintenance outside-root fixture creation");
        {
            auto writer = manager.CreateFileWriter(foreign, filesystem::FOF_Buffered);
            Check(static_cast<bool>(writer), "maintenance foreign fixture creation");
            if (writer)
            {
                writer->Serialize(const_cast<u8*>(sentinel), sizeof(sentinel));
                writer->Flush();
            }
        }
        {
            const filesystem::AbsolutePath files[] = {foreignTemporary, malformedRecord, wrongShardRecord, outsideRecord};
            for (const filesystem::AbsolutePath& path : files)
            {
                Check(manager.CreatePath(filesystem::paths::ParentAbsolutePath(path)), "maintenance foreign fixture parent creation");
                auto writer = manager.CreateFileWriter(path, filesystem::FOF_Buffered);
                Check(static_cast<bool>(writer), "maintenance foreign fixture file creation");
                if (writer)
                {
                    writer->Serialize(const_cast<u8*>(sentinel), sizeof(sentinel));
                    writer->Flush();
                }
            }
        }
        Check(manager.CreatePath(filesystem::paths::ParentAbsolutePath(ownedStage)), "maintenance stage parent creation");
        {
            auto writer = manager.CreateFileWriter(ownedStage, filesystem::FOF_Buffered);
            Check(static_cast<bool>(writer), "maintenance stage fixture creation");
            if (writer)
            {
                writer->Serialize(const_cast<u8*>(sentinel), sizeof(sentinel));
                writer->Flush();
            }
        }
        assets::ArtifactSetKey keepKeys[] = {{protectedBuild, {}}, {epochBuild, {}}};
        const containers::ArraySpan<const assets::ArtifactSetKey> keepCanonical(keepKeys, 2u);
        limits.maximumCacheBytes = 16ull * 1024ull * 1024ull;
        Check(project.MaintainPersistentCache(keepCanonical, currentEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.foreignFiles == 4u && report.stageFiles == 1u && manager.FileExist(foreign) &&
                  manager.FileExist(foreignTemporary) && manager.FileExist(malformedRecord) && manager.FileExist(wrongShardRecord) &&
                  manager.FileExist(ownedStage),
              "maintenance preserves foreign tmp, unexpected, malformed canonical-looking, wrong-shard, and owned-stage files");

        assets::DerivedDataArtifactSource pathAuthoritySource;
        assets::DerivedDataArtifactSourceConfig pathAuthorityConfig;
        pathAuthorityConfig.root = maintenanceRoot.AsChar();
        Check(pathAuthoritySource.Initialize(pathAuthorityConfig), "maintenance path-authority source initialization");
        assets::detail::PersistentRootAuthority* const pathAuthority =
            assets::detail::PersistentCacheAuthorityAccess::Authority(pathAuthoritySource);
        Check(pathAuthority != nullptr &&
                  filesystem::ValidateMutationPath(maintenanceRoot, outsideRecord) == filesystem::MutationPathResult::InvalidPath &&
                  !assets::detail::PersistentCacheAuthorityAccess::DeleteCanonical(pathAuthority, protectedBuild, outsideRecord) &&
                  manager.FileExist(outsideRecord) && manager.FileExist(protectedRecord),
              "cache authority rejects lexical outside-root mutation candidates without touching either file");
        // Reparse/junction escape creation is intentionally not forced here: it is platform/privilege sensitive.
        // Production ScanFiles/ValidateMutationPath performs the containment and reparse rejection used by the authority seam.
        Check(pathAuthoritySource.Shutdown(), "maintenance path-authority source shutdown");

        const u64 protectedRecordBytes = manager.GetFileSize(protectedRecord);
        const u64 epochRecordBytes = manager.GetFileSize(epochRecord);
        limits.maximumCacheBytes = 1u;
        limits.maximumScanBytes = protectedRecordBytes > epochRecordBytes ? protectedRecordBytes : epochRecordBytes;
        Check(protectedRecordBytes != 0u && epochRecordBytes != 0u && limits.maximumScanBytes != 0u &&
                  project.MaintainPersistentCache(keepCanonical, currentEpoch, limits, report, diagnostics) ==
                      assets::PersistentCacheMaintenanceResult::LimitExceeded &&
                  manager.FileExist(protectedRecord) && manager.FileExist(epochRecord) && manager.FileExist(foreign) &&
                  manager.FileExist(foreignTemporary) && manager.FileExist(malformedRecord) && manager.FileExist(wrongShardRecord) &&
                  manager.FileExist(ownedStage),
              "checked scan-byte accumulation fails closed before any canonical or foreign deletion");
        limits.maximumScanBytes = 16ull * 1024ull * 1024ull;

        limits.maximumScanEntries = 1u;
        Check(project.MaintainPersistentCache(keepCanonical, currentEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::LimitExceeded &&
                  manager.FileExist(foreign) && manager.FileExist(foreignTemporary) && manager.FileExist(malformedRecord) &&
                  manager.FileExist(wrongShardRecord) && manager.FileExist(ownedStage),
              "scan-entry bound fails closed before destructive maintenance");
        limits.maximumScanEntries = 64u;

        // Clear the first fixtures, then prove bounded multi-pass deletion.
        static_cast<void>(manager.DeleteFile(foreign));
        static_cast<void>(manager.DeleteFile(foreignTemporary));
        static_cast<void>(manager.DeleteFile(malformedRecord));
        static_cast<void>(manager.DeleteFile(wrongShardRecord));
        static_cast<void>(manager.DeleteFile(ownedStage));
        static_cast<void>(manager.DeleteFile(outsideRecord));
        DeleteTestFiles(manager, outsideRoot);
        limits.maximumCacheBytes = 1u;
        limits.maximumDeleteRecords = 16u;
        static_cast<void>(project.MaintainPersistentCache(noProtections, currentEpoch, limits, report, diagnostics));

        assets::DerivedDataArtifactSource raceSource;
        assets::DerivedDataArtifactSourceConfig raceConfig;
        raceConfig.root = maintenanceRoot.AsChar();
        Check(raceSource.Initialize(raceConfig), "maintenance coordination source initialization");
        assets::detail::PersistentRootAuthority* const raceAuthority =
            assets::detail::PersistentCacheAuthorityAccess::Authority(raceSource);
        limits.maximumDeleteRecords = 1u;

        const assets::BuildRequest readerRaceRequest =
            makeRequest("source/maintenance-reader-race.asset", "cooked/maintenance-reader-race.asset", 0x36u);
        assets::BuildOutput readerRaceOutput;
        Check(system != nullptr && system->Build(readerRaceRequest, readerRaceOutput) == assets::Result::Success,
              "reader-race fixture build");
        const assets::BuildFingerprint readerRaceBuild = readerRaceOutput.buildFingerprint;
        const filesystem::AbsolutePath readerRacePath = RecordPath(maintenanceRoot, readerRaceBuild);
        readerRaceOutput.Reset();
        assets::ArtifactSetReader raceReader;
        Check(raceSource.Open({readerRaceBuild, {}}, raceReader) == assets::DerivedDataArtifactResult::Success,
              "reader-race opens through shared source authority");
        const u64 readerRaceEpoch = project.GetPersistentProtectionEpoch();
        Check(project.MaintainPersistentCache(noProtections, readerRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.deferredDeletes == 1u &&
                  report.removedRecords == 0u && manager.FileExist(readerRacePath),
              "maintenance delete is refused while a real reader-open lease exists");
        raceReader.Close();
        Check(project.MaintainPersistentCache(noProtections, readerRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.removedRecords == 1u &&
                  !manager.FileExist(readerRacePath),
              "bounded maintenance retry removes reader candidate after lease release");

        const assets::BuildRequest writerRaceRequest =
            makeRequest("source/maintenance-writer-race.asset", "cooked/maintenance-writer-race.asset", 0x37u);
        assets::BuildOutput writerRaceOutput;
        Check(system != nullptr && system->Build(writerRaceRequest, writerRaceOutput) == assets::Result::Success,
              "writer-race fixture build");
        const assets::BuildFingerprint writerRaceBuild = writerRaceOutput.buildFingerprint;
        const filesystem::AbsolutePath writerRacePath = RecordPath(maintenanceRoot, writerRaceBuild);
        writerRaceOutput.Reset();
        assets::detail::PersistentWriterLease writerLease;
        Check(raceAuthority != nullptr &&
                  assets::detail::PersistentCacheAuthorityAccess::AcquireWriter(raceAuthority, writerRaceBuild, writerLease) ==
                      assets::DerivedDataArtifactResult::Success,
              "writer-race acquires exact-key store admission");
        const u64 writerRaceEpoch = project.GetPersistentProtectionEpoch();
        Check(project.MaintainPersistentCache(noProtections, writerRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.deferredDeletes == 1u &&
                  report.removedRecords == 0u && manager.FileExist(writerRacePath),
              "maintenance delete is refused while exact-key writer admission exists");
        writerLease.Reset();
        Check(project.MaintainPersistentCache(noProtections, writerRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.removedRecords == 1u &&
                  !manager.FileExist(writerRacePath),
              "bounded maintenance retry removes writer candidate after admission release");

        const assets::BuildRequest corruptRaceRequest =
            makeRequest("source/maintenance-corrupt-race.asset", "cooked/maintenance-corrupt-race.asset", 0x38u);
        assets::BuildOutput corruptRaceOutput;
        Check(system != nullptr && system->Build(corruptRaceRequest, corruptRaceOutput) == assets::Result::Success,
              "corrupt-lookup race fixture build");
        const assets::BuildFingerprint corruptRaceBuild = corruptRaceOutput.buildFingerprint;
        const filesystem::AbsolutePath corruptRacePath = RecordPath(maintenanceRoot, corruptRaceBuild);
        corruptRaceOutput.Reset();
        const u64 corruptRaceSize = manager.GetFileSize(corruptRacePath);
        containers::DynamicArray<u8> corruptRaceBytes(memory::pools::Assets::GetInstance());
        corruptRaceBytes.Resize(static_cast<u32>(corruptRaceSize));
        auto corruptRaceReader = manager.CreateFileReader(corruptRacePath, filesystem::FOF_Buffered);
        Check(corruptRaceReader && corruptRaceSize != 0u && corruptRaceBytes.Size() == corruptRaceSize,
              "corrupt-lookup race reads canonical record");
        if (corruptRaceReader && !corruptRaceBytes.Empty())
            corruptRaceReader->Serialize(corruptRaceBytes.Data(), corruptRaceBytes.Size());
        corruptRaceReader.Reset();
        if (!corruptRaceBytes.Empty())
        {
            corruptRaceBytes.Back() ^= 0xffu;
            auto corruptRaceWriter = manager.CreateFileWriter(corruptRacePath, filesystem::FOF_Buffered);
            Check(static_cast<bool>(corruptRaceWriter), "corrupt-lookup race injects corruption");
            if (corruptRaceWriter)
            {
                corruptRaceWriter->Serialize(corruptRaceBytes.Data(), corruptRaceBytes.Size());
                corruptRaceWriter->Flush();
            }
        }
        Check(raceAuthority != nullptr &&
                  assets::detail::PersistentCacheAuthorityAccess::AcquireReader(raceAuthority, corruptRaceBuild) ==
                      assets::DerivedDataArtifactResult::Success &&
                  assets::detail::ValidatePersistentRecord(corruptRacePath, {corruptRaceBuild, {}}, raceConfig.limits) ==
                      assets::DerivedDataArtifactResult::Corrupt,
              "corrupt lookup holds the same reader admission used by DerivedDataArtifactSource::Open");
        const u64 corruptRaceEpoch = project.GetPersistentProtectionEpoch();
        Check(project.MaintainPersistentCache(noProtections, corruptRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.deferredDeletes == 1u &&
                  report.removedRecords == 0u && manager.FileExist(corruptRacePath),
              "maintenance delete is refused while corrupt lookup admission is held");
        if (raceAuthority != nullptr)
            assets::detail::PersistentCacheAuthorityAccess::ReleaseReader(raceAuthority, corruptRaceBuild);
        Check(project.MaintainPersistentCache(noProtections, corruptRaceEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success && report.removedRecords == 1u &&
                  !manager.FileExist(corruptRacePath),
              "bounded maintenance retry removes corrupt candidate after lookup admission release");
        Check(raceSource.Shutdown(), "maintenance coordination source shutdown");

        const assets::BuildRequest firstVictimRequest =
            makeRequest("source/maintenance-victim-a.asset", "cooked/maintenance-victim-a.asset", 0x34u);
        const assets::BuildRequest secondVictimRequest =
            makeRequest("source/maintenance-victim-b.asset", "cooked/maintenance-victim-b.asset", 0x35u);
        assets::BuildOutput firstVictim;
        assets::BuildOutput secondVictim;
        Check(system != nullptr && system->Build(firstVictimRequest, firstVictim) == assets::Result::Success,
              "first bounded-delete fixture build");
        const filesystem::AbsolutePath firstVictimPath = RecordPath(maintenanceRoot, firstVictim.buildFingerprint);
        const assets::BuildFingerprint firstVictimBuild = firstVictim.buildFingerprint;
        firstVictim.Reset();
        Check(system != nullptr && system->Build(secondVictimRequest, secondVictim) == assets::Result::Success,
              "second bounded-delete fixture build");
        const filesystem::AbsolutePath secondVictimPath = RecordPath(maintenanceRoot, secondVictim.buildFingerprint);
        const assets::BuildFingerprint secondVictimBuild = secondVictim.buildFingerprint;
        secondVictim.Reset();

        red::DateTime tiedVictimTime;
        tiedVictimTime.SetYear(2024u);
        tiedVictimTime.SetMonth(0u);
        tiedVictimTime.SetDay(0u);
        tiedVictimTime.SetHour(12u);
        Check(tiedVictimTime.IsValid() && manager.SetFileTime(firstVictimPath, tiedVictimTime) &&
                  manager.SetFileTime(secondVictimPath, tiedVictimTime) &&
                  manager.GetFileTimeStamp(firstVictimPath) != 0u &&
                  manager.GetFileTimeStamp(firstVictimPath) == manager.GetFileTimeStamp(secondVictimPath),
              "maintenance victim timestamps are forced equal through the portable filesystem owner seam");
        const auto fingerprintLess = [](const assets::BuildFingerprint& left, const assets::BuildFingerprint& right) noexcept
        {
            for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
            {
                if (left.bytes[index] != right.bytes[index])
                    return left.bytes[index] < right.bytes[index];
            }
            return false;
        };
        const bool firstSortsFirst = fingerprintLess(firstVictimBuild, secondVictimBuild);
        const filesystem::AbsolutePath& firstSortedVictimPath = firstSortsFirst ? firstVictimPath : secondVictimPath;
        const filesystem::AbsolutePath& secondSortedVictimPath = firstSortsFirst ? secondVictimPath : firstVictimPath;

        const u64 victimEpoch = project.GetPersistentProtectionEpoch();
        limits.maximumDeleteRecords = 1u;
        Check(project.MaintainPersistentCache(noProtections, victimEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.removedRecords == 1u && report.deleteBatchLimitReached && !report.budgetSatisfied &&
                  !manager.FileExist(firstSortedVictimPath) && manager.FileExist(secondSortedVictimPath),
              "equal-age maintenance victims use build fingerprint as the deterministic deletion tie-breaker");
        Check(project.MaintainPersistentCache(noProtections, victimEpoch, limits, report, diagnostics) ==
                  assets::PersistentCacheMaintenanceResult::Success &&
                  report.removedRecords == 1u && report.budgetSatisfied && !manager.FileExist(secondSortedVictimPath),
              "explicit next bounded maintenance pass completes remaining victim");

        Check(project.Shutdown() == assets::ProjectAssetBuildResult::Success, "maintenance project build-system shutdown");
        DeleteTestFiles(manager, maintenanceRoot);
        DeleteTestFiles(manager, maintenanceIndexRoot);
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
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    const filesystem::AbsolutePath working = vanguard::tests::ScratchDirectory("assets_persistent_cache_tests");
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_assets_ddc_tests");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    filesystem::Manager& manager = filesystem::GetManager();
    DeleteTestFiles(manager, root);
    Check(manager.CreatePath(root), "persistent cache root creation");

    CompilerState state;
    const assets::CompilerId compilerId = assets::HashCompilerName("assets.persistent_test");
    const assets::CompilerDescriptor compiler{compilerId, "assets.persistent_test", 7, SourceType, OutputType, &Discover, &Compile, &state, &Estimate};
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
                  built.origin == assets::BuildOrigin::CompilerBuilt && state.compileCalls.GetValue() == 1,
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
        built.Reset();
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
        assets::DerivedDataArtifactSource matchingSource;
        Check(matchingSource.Initialize(sourceConfig), "same-root source joins shared coordination authority");
        assets::DerivedDataArtifactSourceConfig mismatchedSourceConfig = sourceConfig;
        --mismatchedSourceConfig.coordination.maximumLiveLeasesPerRoot;
        assets::DerivedDataArtifactSource mismatchedSource;
        Check(!mismatchedSource.Initialize(mismatchedSourceConfig), "same-root incompatible coordination limits are rejected");

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
            const u8 expected[] = {0x56, 0x44, 0x44, 0x43, sourceBytes[0], 0xa1, 0xb2, sourceBytes[0]};
            auto pinned = manager.CreateFileReader(looseTarget, filesystem::FOF_Buffered);
            assets::LooseMaterializationInfo reused;
            Check(static_cast<bool>(pinned) &&
                      materializer.Materialize(materializationRecord, output, sourceReader, looseTarget, looseTemporary,
                                               looseLimits, &reused) == assets::LooseMaterializationResult::Success &&
                      reused.byteCount == sizeof(expected) &&
                      reused.contentCrc64 == vanguard::serialization::Crc64(expected, sizeof(expected)) &&
                      manager.FileExist(looseTarget) && !manager.FileExist(looseTemporary),
                  "byte-exact existing loose target is reused without replacement while pinned");
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

        {
            assets::ArtifactSetReader promotedReader;
            assets::ArtifactSetReader survivingReader;
            Check(sourceReader.Open({fingerprint, contentFingerprint}, promotedReader) == assets::DerivedDataArtifactResult::Success &&
                      sourceReader.Open({fingerprint, contentFingerprint}, survivingReader) == assets::DerivedDataArtifactResult::Success,
                  "same-key readers coexist for exact promotion fixture");
            assets::BuildOutputProtection publication;
            Check(assets::detail::PersistentCacheAuthorityAccess::PromoteReaderToPublication(promotedReader, publication) &&
                      publication.IsValid(),
                  "promotion transfers the exact reader lease to publication protection");
            promotedReader.Close();

            assets::detail::PersistentWriterLease writer;
            auto* const authority = assets::detail::PersistentCacheAuthorityAccess::Authority(sourceReader);
            Check(assets::detail::PersistentCacheAuthorityAccess::AcquireWriter(authority, fingerprint, writer) ==
                      assets::DerivedDataArtifactResult::InvalidState,
                  "surviving reader and publication protection block exclusive admission after promoted reader closes");
            publication.Reset();
            Check(assets::detail::PersistentCacheAuthorityAccess::AcquireWriter(authority, fingerprint, writer) ==
                      assets::DerivedDataArtifactResult::InvalidState,
                  "surviving reader remains represented after publication protection releases");
            survivingReader.Close();
            Check(assets::detail::PersistentCacheAuthorityAccess::AcquireWriter(authority, fingerprint, writer) ==
                      assets::DerivedDataArtifactResult::Success,
                  "exclusive admission succeeds only after the surviving reader closes");
            writer.Reset();
        }
        Check(matchingSource.Shutdown(), "matching derived-data source shutdown");
        Check(sourceReader.Shutdown(), "derived-data artifact source shutdown");
    }

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "second persistent build-system initialization");
        assets::BuildOutput cached;
        Check(system.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
                  cached.origin == assets::BuildOrigin::PersistentCacheHit &&
                  cached.buildFingerprint == fingerprint && cached.artifacts.Size() == 2 && cached.artifacts[0].bytes[4] == sourceBytes[0] &&
                  state.compileCalls.GetValue() == 1,
              "record survives process-lifetime restart");
        Check(system.GetStats().persistentCacheHits == 1, "persistent hit telemetry");
        cached.Reset();
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

    const filesystem::AbsolutePath abandoned = root.AddFilePath("foreign.tmp");
    const filesystem::AbsolutePath ownedStage = StagePath(root, fingerprint);
    {
        const u8 stale[] = {0xde, 0xad};
        auto writer = manager.CreateFileWriter(abandoned);
        Check(static_cast<bool>(writer), "create foreign temporary");
        if (writer)
        {
            writer->Serialize(const_cast<u8*>(stale), sizeof(stale));
            writer->Flush();
            writer.Reset();
        }
        writer = manager.CreateFileWriter(ownedStage);
        Check(static_cast<bool>(writer), "create owned interrupted-publication stage");
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
        Check(manager.FileExist(abandoned), "startup preserves foreign temporary");
        Check(!manager.FileExist(ownedStage), "startup removes exact owned VDDC stage");
        assets::BuildOutput recovered;
        Check(system.Build(request, recovered) == assets::Result::Success && recovered.disposition == assets::BuildDisposition::Built &&
                  recovered.origin == assets::BuildOrigin::CompilerBuilt && state.compileCalls.GetValue() == 2,
              "corrupt record is rejected and rebuilt");
        const assets::Stats stats = system.GetStats();
        Check(stats.persistentCacheRecoveries == 1 && stats.persistentCacheCorruptions == 1 && stats.persistentCacheStores == 1,
              "recovery and corruption telemetry");
        recovered.Reset();
        Check(system.Shutdown(), "recovery shutdown");
    }
    static_cast<void>(manager.DeleteFile(abandoned));

    {
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, config, compiler), "final persistent build-system initialization");
        assets::BuildOutput cached;
        Check(system.Build(request, cached) == assets::Result::Success && cached.disposition == assets::BuildDisposition::CacheHit &&
                  cached.origin == assets::BuildOrigin::PersistentCacheHit && state.compileCalls.GetValue() == 2,
              "rebuilt record is durable and valid");
        cached.Reset();
        Check(system.Shutdown(), "final shutdown");
    }

    {
        assets::Config memoryConfig = config;
        memoryConfig.maximumCacheEntries = 4;
        memoryConfig.maximumCacheBytes = 1024 * 1024;
        assets::BuildSystem system;
        Check(InitializeBuildSystem(system, memoryConfig, compiler), "memory-origin build-system initialization");
        assets::BuildOutput first;
        assets::BuildOutput second;
        const u32 callsBefore = state.compileCalls.GetValue();
        Check(system.Build(request, first) == assets::Result::Success && first.origin == assets::BuildOrigin::PersistentCacheHit,
              "memory-origin fixture first loads persistent VDDC");
        Check(system.Build(request, second) == assets::Result::Success && second.origin == assets::BuildOrigin::MemoryCacheHit &&
                  second.disposition == assets::BuildDisposition::CacheHit && state.compileCalls.GetValue() == callsBefore,
              "second exact build reports memory-cache origin without compiler");
        Check(system.GetStats().memoryCacheHits == 1, "memory hit telemetry is exact");
        first.Reset();
        second.Reset();
        Check(system.Shutdown(), "memory-origin shutdown");
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

        const bool firstSucceeded = firstThread.result == assets::Result::Success;
        const bool secondSucceeded = secondThread.result == assets::Result::Success;
        const bool firstCoordinated = firstSucceeded || firstThread.result == assets::Result::PersistentCacheFailure;
        const bool secondCoordinated = secondSucceeded || secondThread.result == assets::Result::PersistentCacheFailure;
        const bool matchingSuccess = !firstSucceeded || !secondSucceeded ||
                                     (firstThread.output.buildFingerprint == secondThread.output.buildFingerprint &&
                                      firstThread.output.contentFingerprint == secondThread.output.contentFingerprint);
        Check(firstCoordinated && secondCoordinated && (firstSucceeded || secondSucceeded) && matchingSuccess &&
                  state.compileCalls.GetValue() == callsBefore + 2,
              "concurrent identical publishers serialize through the exact-key writer authority");
        firstThread.output.Reset();
        secondThread.output.Reset();
        Check(firstSystem.Shutdown() && secondSystem.Shutdown(), "concurrent publisher shutdown");

        containers::DynamicArray<filesystem::AbsolutePath> temporary(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*.tmp"), temporary, true);
        Check(temporary.Empty(), "publication race leaves no temporary files");

        assets::BuildSystem verificationSystem;
        Check(InitializeBuildSystem(verificationSystem, config, compiler), "publication-race verification initialization");
        assets::BuildOutput verified;
        Check(verificationSystem.Build(concurrentRequest, verified) == assets::Result::Success && verified.disposition == assets::BuildDisposition::CacheHit &&
                  verified.origin == assets::BuildOrigin::PersistentCacheHit && state.compileCalls.GetValue() == callsBefore + 2,
              "publication-race result is durable");
        verified.Reset();
        Check(verificationSystem.Shutdown(), "publication-race verification shutdown");
    }

    RunGeneratedFallbackTests(manager, working, compiler, state);
    RunPersistentMaintenanceTests(manager, working, compiler);

    DeleteTestFiles(manager, root);
    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
