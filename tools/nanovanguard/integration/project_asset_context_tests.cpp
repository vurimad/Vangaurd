#include "../../../tests/support/scratch_directory.hpp"

#include <vanguard/nanovanguard/project_asset_context.hpp>
#include <vanguard/nanovanguard/project_asset_commands.hpp>
#include <vanguard/nanovanguard/command_cancellation.hpp>
#include <vanguard/nanovanguard/nanovanguard.hpp>
#include <vanguard/nanovanguard/project_platform.hpp>

#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/directory_observation.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/textures.hpp>

#include <cstdio>
#include <cstring>

namespace
{
    using namespace vanguard;

    u32 g_failures = 0;

    struct Buffer
    {
        char data[65536]{};
        u32 length = 0;
    };

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        ++g_failures;
        std::fprintf(stderr, "[nanovanguardProjectAssetTests] FAILED: %s\n", message);
    }

    [[nodiscard]] bool Capture(const char* const text, const u32 length, void* const userData) noexcept
    {
        Buffer& buffer = *static_cast<Buffer*>(userData);
        if (length > sizeof(buffer.data) - buffer.length - 1u)
            return false;
        for (u32 index = 0; index < length; ++index)
            buffer.data[buffer.length++] = text[index];
        buffer.data[buffer.length] = '\0';
        return true;
    }

    [[nodiscard]] bool Contains(const char* text, const char* token) noexcept
    {
        for (; text != nullptr && *text != '\0'; ++text)
        {
            const char* left = text;
            const char* right = token;
            while (*left != '\0' && *right != '\0' && *left == *right)
            {
                ++left;
                ++right;
            }
            if (*right == '\0')
                return true;
        }
        return false;
    }

    [[nodiscard]] u32 Count(const char* text, const char* token) noexcept
    {
        u32 count = 0u;
        if (text == nullptr || token == nullptr || token[0] == '\0')
            return 0u;
        for (const char* cursor = text; *cursor != '\0'; ++cursor)
        {
            const char* left = cursor;
            const char* right = token;
            while (*left != '\0' && *right != '\0' && *left == *right)
            {
                ++left;
                ++right;
            }
            if (*right == '\0')
                ++count;
        }
        return count;
    }

    struct FreshnessFailureCapture
    {
        Buffer buffer;
        filesystem::AbsolutePath source;
        bool mutationAttempted = false;
        bool sourceDeleted = false;
    };

    [[nodiscard]] bool CaptureAndDeleteSourceAfterProjectStatus(const char* const text, const u32 length,
                                                                void* const userData) noexcept
    {
        FreshnessFailureCapture& capture = *static_cast<FreshnessFailureCapture*>(userData);
        if (!Capture(text, length, &capture.buffer))
            return false;
        if (!capture.mutationAttempted && Contains(text, "\"scope\":\"project\""))
        {
            capture.mutationAttempted = true;
            capture.sourceDeleted = nanovanguard::platform::DeleteFile(capture.source.AsStringView());
        }
        return true;
    }

    struct ActiveWatchCancellationCapture
    {
        Buffer buffer;
        bool requested = false;
    };

    [[nodiscard]] bool CaptureAndCancelActiveWatch(const char* const text, const u32 length,
                                                   void* const userData) noexcept
    {
        ActiveWatchCancellationCapture& capture = *static_cast<ActiveWatchCancellationCapture*>(userData);
        if (!Capture(text, length, &capture.buffer))
            return false;
        if (!capture.requested && Contains(text, "\"event\":\"progress\"") &&
            Contains(text, "\"scope\":\"automatic-recook\"") &&
            (Contains(text, "\"state\":\"planning\"") || Contains(text, "\"state\":\"building\"")))
        {
            capture.requested = true;
            nanovanguard::RequestCommandCancellation();
        }
        return true;
    }

    [[nodiscard]] bool DeleteDirectoryTree(filesystem::Manager& files,
                                           const filesystem::AbsolutePath& directory) noexcept
    {
        bool removed = true;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Tools::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            removed = DeleteDirectoryTree(files, child) && removed;
        containers::DynamicArray<filesystem::AbsolutePath> storedFiles(memory::pools::Tools::GetInstance());
        files.FindFiles(directory, containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            removed = files.DeleteFile(storedFile) && removed;
        return files.DeletePath(directory) && removed;
    }

    [[nodiscard]] bool DirectoryExists(filesystem::Manager& files,
                                       const filesystem::AbsolutePath& directory) noexcept
    {
        const filesystem::AbsolutePath parent = filesystem::paths::ParentAbsolutePath(directory);
        if (parent.Empty())
            return false;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Tools::GetInstance());
        files.FindDirectories(parent, children);
        for (const filesystem::AbsolutePath& child : children)
            if (child == directory)
                return true;
        return false;
    }

    [[nodiscard]] projects::ProjectWorkspace MakeWorkspace(const filesystem::AbsolutePath& root) noexcept
    {
        projects::ProjectWorkspace workspace;
        workspace.root = root;
        workspace.projectFile = root.AddFilePath("headless.vproject");
        workspace.assets = root.AddDirPath("Assets");
        workspace.derivedData = root.AddDirPath("DerivedData");
        workspace.intermediate = root.AddDirPath("Intermediate");
        workspace.saved = root.AddDirPath("Saved");
        workspace.builds = root.AddDirPath("Builds");
        workspace.config = root.AddDirPath("Config");
        workspace.plugins = root.AddDirPath("Plugins");
        return workspace;
    }

    [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
    {
        const containers::StringView view = source.AsStringView();
        containers::String path(view.Data(), view.Length());
        path.Append(".vmeta", 6u);
        return filesystem::AbsolutePath::CreateFilePath(path);
    }

    [[nodiscard]] bool ReadAssetMetadata(const filesystem::AbsolutePath& source,
                                         assets::AssetMetadata& metadata) noexcept;

    [[nodiscard]] bool AuthorTextureFixture(const filesystem::AbsolutePath& root,
                                            const char* const fileName = "red.png") noexcept
    {
        namespace nano = vanguard::nanovanguard;
        namespace tt = vanguard::texture_tools;
        constexpr u8 redPng[]{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                              0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
                              0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf,
                              0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00,
                              0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath(fileName);
        if (!nano::platform::WriteFileDurable(source.AsStringView(), {redPng, static_cast<u32>(sizeof(redPng))}))
            return false;

        containers::DynamicArray<u8> settings(memory::pools::Assets::GetInstance());
        if (!tt::IsInitialized() && !tt::Initialize())
            return false;
        tt::TextureBuildDescription description;
        description.sourceMode = tt::TextureBuildSourceMode::Image2D;
        description.colorSpace = tt::ImportedColorSpace::SRgb;
        description.profile = tt::profiles::Ui;
        if (tt::EncodeTextureBuildSettings(description, settings) != tt::TextureBuildSettingsResult::Success)
            return false;

        assets::AssetMetadata metadata;
        if (!assets::GenerateAssetId(metadata.id))
            return false;
        metadata.importer = assets::HashCompilerName("vanguard.texture.compiler");
        metadata.importerVersion = tt::TextureAssetCompilerVersion;
        metadata.settingsVersion = 1u;
        for (const u8 byte : settings)
            metadata.settings.PushBack(byte);
        assets::AssetOutput assetOutput;
        assetOutput.key = "main";
        assetOutput.type = textures::TextureResourceType;
        metadata.outputs.PushBack(static_cast<assets::AssetOutput&&>(assetOutput));
        containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        return nano::platform::WriteFileDurable(MetadataPath(source).AsStringView(),
                                                {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
    }

    [[nodiscard]] bool RewriteTextureSourceBlue(const filesystem::AbsolutePath& source) noexcept
    {
        // Independent valid 1x1 RGBA PNG. Keeping the metadata untouched makes
        // the P5 source-change proof isolate source-content drift.
        constexpr u8 bluePng[]{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                               0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
                               0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60,
                               0xf8, 0xff, 0x1f, 0x00, 0x03, 0x02, 0x01, 0xff, 0xe6, 0x77, 0x0b, 0xae, 0x00, 0x00, 0x00, 0x00,
                               0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        auto writer = filesystem::GetManager().CreateFileWriter(source);
        if (!writer)
            return false;
        writer->Serialize(const_cast<u8*>(bluePng), sizeof(bluePng));
        writer->Flush();
        return !writer->HasErrors();
    }

    [[nodiscard]] bool RewriteTextureProfile(const filesystem::AbsolutePath& source,
                                             const texture_tools::TextureCookingProfileId profile) noexcept
    {
        assets::AssetMetadata metadata;
        if (!ReadAssetMetadata(source, metadata))
            return false;
        containers::DynamicArray<u8> settings(memory::pools::Assets::GetInstance());
        texture_tools::TextureBuildDescription description;
        description.sourceMode = texture_tools::TextureBuildSourceMode::Image2D;
        description.colorSpace = texture_tools::ImportedColorSpace::SRgb;
        description.profile = profile;
        if (texture_tools::EncodeTextureBuildSettings(description, settings) != texture_tools::TextureBuildSettingsResult::Success)
            return false;
        metadata.settings.Clear();
        for (const u8 byte : settings)
            metadata.settings.PushBack(byte);
        containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        auto writer = filesystem::GetManager().CreateFileWriter(MetadataPath(source));
        if (!writer)
            return false;
        writer->Serialize(encoded.AsChar(), encoded.Length());
        writer->Flush();
        return !writer->HasErrors();
    }

    [[nodiscard]] bool ReadAssetMetadata(const filesystem::AbsolutePath& source,
                                         assets::AssetMetadata& metadata) noexcept
    {
        containers::String text;
        return nanovanguard::platform::ReadFile(MetadataPath(source).AsStringView(), text) &&
               assets::ParseMetadata(text, metadata) == assets::MetadataResult::Success;
    }

    [[nodiscard]] bool AppearsBefore(const char* const text, const char* const first,
                                     const char* const second) noexcept
    {
        const char* const firstPosition = std::strstr(text, first);
        const char* const secondPosition = std::strstr(text, second);
        return firstPosition != nullptr && secondPosition != nullptr && firstPosition < secondPosition;
    }

    struct P5CompilerProbe final
    {
        assets::CompilerDescriptor delegate;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<bool> block;
        concurrency::Atomic<bool> started;
        concurrency::Atomic<bool> release;

        [[nodiscard]] static bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies,
                                           void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            return probe.delegate.discoverDependencies != nullptr &&
                   probe.delegate.discoverDependencies(request, dependencies, probe.delegate.userData);
        }

        [[nodiscard]] static bool Estimate(const assets::BuildRequest& request,
                                           const containers::ArraySpan<const assets::BuildDependency> dependencies,
                                           assets::BuildResourceEstimate& estimate, void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            return probe.delegate.estimateResources != nullptr &&
                   probe.delegate.estimateResources(request, dependencies, estimate, probe.delegate.userData);
        }

        [[nodiscard]] static bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& artifacts,
                                          void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            static_cast<void>(probe.compileCalls.Increment());
            probe.started.SetValue(true);
            while (probe.block.GetValue() && !probe.release.GetValue() && !context.IsCancellationRequested())
                concurrency::YieldCurrentThread();
            if (context.IsCancellationRequested())
                return false;
            return probe.delegate.compile != nullptr && probe.delegate.compile(context, artifacts, probe.delegate.userData);
        }

        [[nodiscard]] assets::CompilerDescriptor Wrap(const assets::CompilerDescriptor& descriptor) noexcept
        {
            delegate = descriptor;
            assets::CompilerDescriptor wrapped = descriptor;
            wrapped.discoverDependencies = &Discover;
            wrapped.compile = &Compile;
            wrapped.estimateResources = &Estimate;
            wrapped.userData = this;
            return wrapped;
        }
    };

    // Test-only mirror of ProjectAssetContext's healthy GPU-free ownership graph.
    // The forwarding compiler descriptor lets P5 count/barrier the real
    // TextureAssetCompiler without adding product API.
    class P5ProjectAssetHarness final
    {
    public:
        [[nodiscard]] bool Open(const projects::ProjectWorkspace& projectWorkspace, const bool dependencyDrift = false) noexcept
        {
            if (open || retainedAfterUnsafeShutdown ||
                !asset_streaming::MakeProjectAssetPipelineConfig(projectWorkspace.derivedData, projectWorkspace.intermediate,
                                                                          "windows-x64", config))
                return false;

            if (!diagnostics::IsInitialized())
            {
                if (!diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguard-p5"))
                    return false;
                ownsDiagnostics = true;
            }
            if (!vanguard::io::IsInitialized())
            {
                if (!vanguard::io::Initialize())
                    return false;
                ownsIo = true;
            }
            if (!reflection::IsInitialized() && !reflection::Initialize())
                return false;
            const filesystem::Config filesystemConfig{projectWorkspace.root, projectWorkspace.root, projectWorkspace.derivedData};
            if (!filesystem::IsInitialized())
            {
                if (!filesystem::Initialize(filesystemConfig))
                    return false;
                ownsFilesystem = true;
            }
            else
            {
                filesystem::Manager& inherited = filesystem::GetManager();
                if (inherited.GetEngineRoot() != filesystemConfig.engineRoot || inherited.GetGameRoot() != filesystemConfig.gameRoot ||
                    inherited.GetCacheDirectory() != filesystemConfig.cacheRoot)
                    return false;
            }
            if (!jobs::IsInitialized())
            {
                if (!jobs::Initialize(jobs::ToolConfig()))
                    return false;
                ownsJobs = true;
            }
            if (!texture_tools::IsInitialized() && !texture_tools::Initialize())
                return false;
            if (dependencyDrift)
            {
                const texture_tools::TextureCookingProfile* const color = texture_tools::FindCookingProfile(texture_tools::profiles::Color);
                if (color == nullptr)
                    return false;
                texture_tools::TextureCookingProfile extra = *color;
                extra.id = 0x7035657874726101ull; // "p5extra" + version tag.
                if (texture_tools::RegisterCookingProfile(extra) != texture_tools::ProfileRegistrationResult::Success)
                    return false;
            }
            if (!compiler.Initialize())
                return false;

            filesystem::Manager& files = filesystem::GetManager();
            const assets::SourceRoot sourceRoot{projectWorkspace.assets, false};
            if (sources.Rescan(files, {&sourceRoot, 1u}) != assets::SourceDatabaseResult::Success ||
                !files.CreatePath(projectWorkspace.derivedData.AddDirPath("ProjectAssets")) ||
                !files.CreatePath(projectWorkspace.intermediate.AddDirPath("ProjectAssets")) || !registry.Initialize() ||
                !resourcePipeline.Initialize(registry) || !streamer.Initialize(resourcePipeline))
                return false;

            const assets::CompilerDescriptor wrappedCompiler = probe.Wrap(compiler.GetDescriptor());
            const assets::SourceImporterDescriptor importer = compiler.GetImporterDescriptor();
            asset_streaming::ProjectAssetPipelineFailure failure;
            const asset_streaming::ProjectAssetPipelineResult result =
                pipeline.Initialize(files, sources, streamer, {&wrappedCompiler, 1u}, {&importer, 1u}, config, &startupRestore, &failure);
            if (result != asset_streaming::ProjectAssetPipelineResult::Success)
                return false;
            open = true;
            return true;
        }

        [[nodiscard]] bool Close() noexcept
        {
            if (retainedAfterUnsafeShutdown)
                return false;
            if (pipeline.IsInitialized() && pipeline.Shutdown() != asset_streaming::ProjectAssetPipelineResult::Success)
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (compiler.IsInitialized() && !compiler.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (streamer.IsInitialized() && !streamer.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (resourcePipeline.IsInitialized() && !resourcePipeline.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (registry.IsInitialized() && !registry.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (ownsJobs && jobs::IsInitialized() && !jobs::Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (ownsFilesystem && filesystem::IsInitialized())
                filesystem::Shutdown();
            if (ownsIo && vanguard::io::IsInitialized())
                vanguard::io::Shutdown();
            if (ownsDiagnostics && diagnostics::IsInitialized())
                diagnostics::Shutdown();
            open = false;
            return true;
        }

        ~P5ProjectAssetHarness() { static_cast<void>(Close()); }

        asset_streaming::ProjectAssetPipelineConfig config;
        P5CompilerProbe probe;
        texture_tools::TextureAssetCompiler compiler;
        assets::SourceDatabase sources;
        resources::ResourceRegistry registry;
        resources::ResourcePipeline resourcePipeline;
        streaming::ResourceStreamer streamer;
        asset_streaming::ProjectAssetPipeline pipeline;
        asset_streaming::ProjectAssetRestoreReport startupRestore;
        bool ownsDiagnostics = false;
        bool ownsIo = false;
        bool ownsFilesystem = false;
        bool ownsJobs = false;
        bool open = false;
        bool retainedAfterUnsafeShutdown = false;
    };

    [[nodiscard]] bool CloseAndDestroyP5Harness(P5ProjectAssetHarness*& harness) noexcept
    {
        if (harness == nullptr || !harness->Close())
            return false;
        VANGUARD_DELETE(harness);
        harness = nullptr;
        return true;
    }

    [[nodiscard]] bool RenderEquivalentTerminal(const nanovanguard::SemanticEventKind kind,
                                                const char* const operation, const char* const reason) noexcept
    {
        Buffer human;
        Buffer json;
        nanovanguard::Output humanOutput(&Capture, &human);
        nanovanguard::Output jsonOutput(&Capture, &json);
        nanovanguard::SemanticEvent event;
        event.event = kind;
        event.operation = operation;
        if (!event.AddString("reason", reason) ||
            !nanovanguard::WriteSemanticEvent(humanOutput, nanovanguard::OutputFormat::Human, event) ||
            !nanovanguard::WriteSemanticEvent(jsonOutput, nanovanguard::OutputFormat::JsonLines, event))
            return false;
        const char* const token = nanovanguard::SemanticEventToken(kind);
        return Contains(human.data, operation) && Contains(human.data, token) && Contains(human.data, reason) &&
               Contains(json.data, operation) && Contains(json.data, token) && Contains(json.data, reason) &&
               Contains(json.data, "\"schema\":1");
    }

    [[nodiscard]] bool CreateCommandFixture(nanovanguard::CommandRegistry& registry, nanovanguard::Output& output,
                                            Buffer& buffer, const filesystem::AbsolutePath& root,
                                            const bool multipleTargets = false) noexcept
    {
        if (!nanovanguard::RegisterBuiltinCommands(registry) || !nanovanguard::RegisterProjectAssetCommands(registry))
            return false;
        buffer = {};
        const char* single[]{"nanovanguard", "project", "create", "P3 Fixture", "--destination", root.AsChar(), "--target", "windows-x64"};
        const char* multiple[]{"nanovanguard", "project", "create", "P3 Fixture", "--destination", root.AsChar(), "--target", "windows-x64",
                               "--target", "linux-x64"};
        const nanovanguard::ExitCode created = multipleTargets ? registry.Dispatch(10, multiple, output) : registry.Dispatch(8, single, output);
        return created == nanovanguard::ExitCode::Success && (!multipleTargets ? AuthorTextureFixture(root) : true);
    }

    class ManualObservationSource final : public filesystem::DirectoryObservationSource
    {
    public:
        [[nodiscard]] bool Start(const containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                 const filesystem::DirectoryObservationCallback callback, void* const userData,
                                 const filesystem::DirectoryObserverConfig&) noexcept override
        {
            if (m_running)
                return false;
            m_rootCount = roots.Count();
            m_callback = callback;
            m_userData = userData;
            m_running = true;
            return true;
        }

        [[nodiscard]] bool Stop() noexcept override
        {
            m_running = false;
            m_callback = nullptr;
            m_userData = nullptr;
            return true;
        }

        [[nodiscard]] bool IsRunning() const noexcept override { return m_running; }

        void Emit(const filesystem::DirectoryObservationKind kind, const filesystem::AbsolutePath& path) noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.path = path;
            observation.kind = kind;
            m_callback(observation, m_userData);
        }

        void EmitOverflow() noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.kind = filesystem::DirectoryObservationKind::Overflow;
            m_callback(observation, m_userData);
        }

        u32 m_rootCount = 0u;

    private:
        filesystem::DirectoryObservationCallback m_callback = nullptr;
        void* m_userData = nullptr;
        bool m_running = false;
    };

    [[nodiscard]] bool IsAssetCommandMode(const char* const mode) noexcept
    {
        return mode != nullptr && (std::strcmp(mode, "asset-status") == 0 || std::strcmp(mode, "asset-rescan") == 0 ||
                                   std::strcmp(mode, "asset-build") == 0 || std::strcmp(mode, "asset-reimport") == 0 ||
                                   std::strcmp(mode, "asset-watch-cancel") == 0 || std::strcmp(mode, "asset-invalid-source") == 0 ||
                                   std::strcmp(mode, "asset-output-key") == 0 || std::strcmp(mode, "asset-target-ambiguous") == 0 ||
                                   std::strcmp(mode, "asset-target-undeclared") == 0 || std::strcmp(mode, "asset-target-unsupported") == 0 ||
                                   std::strcmp(mode, "asset-status-diagnostics") == 0 || std::strcmp(mode, "asset-status-freshness-failure") == 0 ||
                                   std::strcmp(mode, "asset-reimport-failure") == 0 || std::strcmp(mode, "asset-watch-failure") == 0 ||
                                   std::strcmp(mode, "asset-watch-active-cancel") == 0 ||
                                   std::strcmp(mode, "watch-overflow") == 0 ||
                                   std::strcmp(mode, "asset-maintain-vddc") == 0 || std::strcmp(mode, "asset-maintain-loose") == 0 ||
                                   std::strcmp(mode, "asset-maintain-all") == 0 || std::strcmp(mode, "asset-maintain-cancel") == 0 ||
                                   std::strcmp(mode, "asset-maintain-invalid") == 0 ||
                                   std::strcmp(mode, "asset-move") == 0 || std::strcmp(mode, "asset-move-restart-prepare") == 0 ||
                                   std::strcmp(mode, "asset-move-restart-verify") == 0 || std::strcmp(mode, "asset-move-conflict") == 0 ||
                                   std::strcmp(mode, "asset-duplicate") == 0 ||
                                   std::strcmp(mode, "asset-retarget") == 0 || std::strcmp(mode, "asset-retarget-invalid") == 0 ||
                                   std::strcmp(mode, "asset-delete-reject") == 0 || std::strcmp(mode, "asset-delete-replace") == 0 ||
                                   std::strcmp(mode, "asset-delete-invalid-policy") == 0 || std::strcmp(mode, "asset-p4-help") == 0 ||
                                   std::strcmp(mode, "asset-p5-cold-matrix") == 0 ||
                                   std::strcmp(mode, "asset-p5-restart-prepare") == 0 ||
                                   std::strcmp(mode, "asset-p5-restart-verify") == 0 ||
                                   std::strcmp(mode, "asset-p5-dependency-change") == 0 ||
                                   std::strcmp(mode, "asset-p5-active-ownership") == 0 ||
                                   std::strcmp(mode, "asset-p5-protocol-equivalence") == 0);
    }
} // namespace

int main(const int argc, const char* const* const argv)
{
    using namespace vanguard;
    namespace nano = vanguard::nanovanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");

    const filesystem::AbsolutePath scratch = tests::ScratchDirectory("nanovanguard_project_asset_context_tests");
    const filesystem::AbsolutePath root = scratch.AddDirPath("project");
    const filesystem::AbsolutePath alternateRoot = scratch.AddDirPath("alternate");
    const projects::ProjectWorkspace workspace = MakeWorkspace(root);
    const bool rollbackMode = argc == 2 && std::strcmp(argv[1], "rollback") == 0;
    const bool inheritedMode = argc == 2 && std::strcmp(argv[1], "inherited") == 0;
    const bool successMode = argc == 1 || (argc == 2 && std::strcmp(argv[1], "success") == 0);
    const bool assetCommandMode = argc == 2 && IsAssetCommandMode(argv[1]);
    const bool moveRestartPrepare = argc == 2 && std::strcmp(argv[1], "asset-move-restart-prepare") == 0;
    const bool moveRestartVerify = argc == 2 && std::strcmp(argv[1], "asset-move-restart-verify") == 0;
    const bool p5RestartPrepare = argc == 2 && std::strcmp(argv[1], "asset-p5-restart-prepare") == 0;
    const bool p5RestartVerify = argc == 2 && std::strcmp(argv[1], "asset-p5-restart-verify") == 0;
    const bool p5DependencyChange = argc == 2 && std::strcmp(argv[1], "asset-p5-dependency-change") == 0;
    Check(successMode || rollbackMode || inheritedMode || assetCommandMode, "mode is supported");

    filesystem::Manager setupFiles(scratch, scratch, scratch);
    if (!moveRestartVerify && !p5RestartVerify && !p5DependencyChange && DirectoryExists(setupFiles, root))
        Check(DeleteDirectoryTree(setupFiles, root), "previous project fixture cleanup");
    if (DirectoryExists(setupFiles, alternateRoot))
        Check(DeleteDirectoryTree(setupFiles, alternateRoot), "previous alternate fixture cleanup");

    Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
          "bootstrap facilities begin unowned");

    if (assetCommandMode)
    {
        namespace nano = vanguard::nanovanguard;
        const bool ambiguousTarget = std::strcmp(argv[1], "asset-target-ambiguous") == 0;
        const bool unsupportedTarget = std::strcmp(argv[1], "asset-target-unsupported") == 0;
        nano::CommandRegistry registry;
        Buffer buffer;
        nano::Output output(&Capture, &buffer);
        const bool p5ProtocolEquivalence = std::strcmp(argv[1], "asset-p5-protocol-equivalence") == 0;
        if (!moveRestartVerify && !p5RestartVerify && !p5DependencyChange && !p5ProtocolEquivalence)
            Check(CreateCommandFixture(registry, output, buffer, root, ambiguousTarget || unsupportedTarget), "P3 command fixture creation");
        const bool needsReplacementFixture = std::strcmp(argv[1], "asset-retarget") == 0 ||
                                             std::strcmp(argv[1], "asset-delete-replace") == 0;
        if (needsReplacementFixture)
            Check(AuthorTextureFixture(root, "blue.png"), "P4 replacement texture fixture creation");

        if (moveRestartVerify)
        {
            const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("moved.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(moved, persisted), "restart verification reads moved identity from prior process");
            nano::ProjectAssetContext context;
            nano::ProjectAssetContextFailure failure;
            Check(context.Open(workspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::Success,
                  "fresh process context opens after asset move");
            if (context.IsOpen() && context.GetPipeline() != nullptr && context.GetSources() != nullptr)
            {
                const assets::SourceRecord* const sourceRecord = context.GetSources()->LookupAsset(persisted.id);
                const asset_streaming::ProjectAssetPipeline& pipeline = *context.GetPipeline();
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(persisted.id);
                Check(sourceRecord != nullptr && sourceRecord->path == moved && accepted != nullptr &&
                          accepted->asset == persisted.id && accepted->physicalPath == moved &&
                          accepted->sourceIdentity == sourceRecord->sourceIdentity,
                      "fresh process reconstructs moved current source and accepted identity");
                Check(pipeline.GetUndoAssetOperationCount() == 0u && pipeline.GetRedoAssetOperationCount() == 0u,
                      "fresh process does not replay session-local mutation history");
                const asset_streaming::ProjectAssetVddcMaintenanceStatus vddc = pipeline.GetVddcMaintenanceStatus();
                const asset_streaming::ProjectAssetLooseMaintenanceStatus loose = pipeline.GetLooseMaintenanceStatus();
                Check(vddc.state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle && !vddc.retryRequired &&
                          loose.state == asset_streaming::ProjectAssetLooseMaintenanceState::Idle && !loose.retryRequired,
                      "fresh process does not replay transient maintenance state");
            }
            Check(context.Close() == nano::ProjectAssetContextResult::Success,
                  "fresh process restart verification closes cleanly");
        }
        else if (p5RestartVerify)
        {
            // E2.6 P5 row 2. A fresh process wraps the real compiler before any
            // build-system registration. Zero callback entries therefore proves
            // the warm request was satisfied by durable cache truth, not memory
            // state or a hidden compiler fallback.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(source, persisted), "P5 restart verification reads persisted production texture identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace),
                  "P5 fresh process opens the persisted production-texture project");
            if (harness != nullptr && harness->open)
            {
                asset_streaming::ProjectAssetPipeline& pipeline = harness->pipeline;
                Check(harness->probe.compileCalls.GetValue() == 0u,
                      "P5 clean restart performs zero production compiler calls during restore");
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(persisted.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 restart restores the accepted production-texture output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 clean restart restores committed VADI as UpToDate before a new request");
                    asset_streaming::ProjectAssetOutputDeliveryStatus restored;
                    Check(pipeline.GetOutputDeliveryStatus(outputReference, restored) &&
                              restored.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                              restored.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                              restored.committedBuildFingerprint == restored.publishedBuildFingerprint &&
                              restored.committedContentFingerprint == restored.publishedContentFingerprint &&
                              setupFiles.FileExist(restored.publishedPhysicalPath),
                          "P5 clean restart restores exact last-valid loose publication evidence");

                    asset_streaming::ProjectAssetRequest request = pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 clean restart accepts an exact warm build request");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 clean restart completes the exact warm request");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 0u &&
                              pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 exact warm request is a zero-compiler persistent hit and leaves authoritative VADI UpToDate");
                }
            }
            Check(CloseAndDestroyP5Harness(harness),
                  "P5 clean-restart verification closes cleanly");
        }
        else if (p5DependencyChange)
        {
            // The prepare/restart pair leaves a healthy persisted texture source. A
            // fresh process adds one otherwise-unused valid cooking profile before
            // TextureAssetCompiler freezes configuration. The real compiler's
            // generated Tool dependency fingerprints the registry, so this is a
            // production dependency drift without modifying product code.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(source, persisted), "P5 dependency-drift verification reads persisted identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace, true), "P5 dependency-drift process opens with one benign profile addition");
            if (harness != nullptr && harness->open)
            {
                const asset_streaming::ProjectAssetState* const accepted = harness->pipeline.LookupAcceptedAsset(persisted.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 dependency-drift process restores the production output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(harness->pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::DependenciesChanged,
                          "P5 real texture Tool dependency drift evaluates DependenciesChanged");
                    asset_streaming::ProjectAssetRequest request = harness->pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 dependency-drift rebuild is accepted");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 dependency-drift rebuild publishes through the shared owner");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 1u &&
                              harness->pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 dependency drift invokes the production compiler exactly once and restores UpToDate");
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 dependency-drift process closes cleanly");
        }
        else if (ambiguousTarget)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(6, command, output);
            Check(result == nano::ExitCode::InvalidProject, "ambiguous project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "ambiguous-project-target"),
                  "ambiguous target has exactly one semantic failed terminal");
        }
        else if (std::strcmp(argv[1], "asset-target-undeclared") == 0)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--target", "linux-x64", "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(8, command, output);
            Check(result == nano::ExitCode::InvalidProject, "explicit undeclared project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "undeclared-project-target") &&
                      !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                  "explicit undeclared target has exactly one semantic failed terminal");
        }
        else if (unsupportedTarget)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--target", "linux-x64", "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(8, command, output);
            Check(result == nano::ExitCode::InvalidProject, "explicit declared unsupported project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "unsupported-project-target") &&
                      !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                  "explicit declared unsupported target has exactly one semantic failed terminal");
        }
        else if (std::strcmp(argv[1], "watch-overflow") == 0)
        {
            nano::ProjectAssetContext context;
            nano::ProjectAssetContextFailure failure;
            Check(context.Open(MakeWorkspace(root), "windows-x64", &failure) == nano::ProjectAssetContextResult::Success,
                  "watch overflow context opens");
            if (context.IsOpen())
            {
                asset_streaming::ProjectAssetPipeline* const pipeline = context.GetPipeline();
                ManualObservationSource observation;
                asset_streaming::ProjectAssetObservationConfig config;
                config.maximumPendingPaths = 4u;
                config.maximumPathsPerTick = 1u;
                config.maximumReconcileOperationsPerTick = 1u;
                config.settleTicks = 2u;
                config.maximumDeferralTicks = 4u;
                Check(pipeline != nullptr && pipeline->EnableObservation(observation, config) == asset_streaming::ProjectAssetPipelineResult::Success,
                      "headless watch uses injected production observation owner");
                bool startupDrained = false;
                for (u32 tick = 0u; tick < 200000u && !startupDrained; ++tick)
                {
                    const asset_streaming::ProjectAssetPipelineResult observed = pipeline->TickObservation();
                    startupDrained = observed == asset_streaming::ProjectAssetPipelineResult::Success;
                    Check(observed == asset_streaming::ProjectAssetPipelineResult::Success || observed == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "headless watch startup catch-up remains bounded");
                    if (!startupDrained)
                        concurrency::YieldCurrentThread();
                }
                Check(startupDrained, "headless watch drains generation-zero catch-up before automatic recook admission");
                Check(pipeline->EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "headless watch enables existing automatic recook coordinator");
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                for (u32 index = 0u; index < 32u; ++index)
                    observation.Emit(filesystem::DirectoryObservationKind::Modified, source);
                const asset_streaming::ProjectAssetObservationStatus storm = pipeline->GetObservationStatus();
                Check(storm.pendingPaths <= config.maximumPendingPaths && storm.hintsReceived >= 32u && storm.hintsCoalesced != 0u,
                      "event storm remains bounded and coalesces duplicate paths");
                observation.EmitOverflow();
                bool observationDrained = false;
                bool automaticTerminal = false;
                for (u32 tick = 0u; tick < 200000u && (!observationDrained || !automaticTerminal); ++tick)
                {
                    const asset_streaming::ProjectAssetPipelineResult observed = pipeline->TickObservation();
                    observationDrained = observed == asset_streaming::ProjectAssetPipelineResult::Success;
                    Check(observed == asset_streaming::ProjectAssetPipelineResult::Success || observed == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "bounded observation tick remains valid");
                    const asset_streaming::ProjectAssetPipelineResult recook = pipeline->TickAutomaticRecook();
                    Check(recook == asset_streaming::ProjectAssetPipelineResult::Success || recook == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "bounded automatic recook tick remains valid");
                    const asset_streaming::ProjectAssetAutomaticRecookStatus automatic = pipeline->GetAutomaticRecookStatus();
                    automaticTerminal = automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle;
                    if (!observationDrained || !automaticTerminal)
                        concurrency::YieldCurrentThread();
                }
                const asset_streaming::ProjectAssetObservationStatus recovered = pipeline->GetObservationStatus();
                Check(observationDrained && recovered.pendingPaths == 0u && recovered.rescansCompleted >= 1u && !recovered.rescanRequired,
                      "overflow reaches authoritative bounded recovery without pending paths");
                Check(pipeline->DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "watch overflow automatic coordinator disables cleanly");
                Check(pipeline->DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "watch overflow observation disables cleanly");
                Check(context.Close() == nano::ProjectAssetContextResult::Success, "watch overflow context closes cleanly");
            }
        }
        else if (std::strcmp(argv[1], "asset-p5-cold-matrix") == 0 || p5RestartPrepare)
        {
            // E2.6 P5 rows 1/2. Exercise the actual headless composition and
            // production texture importer/compiler rather than a test compiler.
            // The cold-matrix variant adds a source after Open and proves the
            // authoritative rescan -> VADI -> loose-publication chain. The
            // restart-prepare variant builds the original source and deliberately
            // leaves the approved scratch project for restart-verify.
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace), "P5 production-texture harness opens");
            if (harness != nullptr && harness->open)
            {
                const filesystem::AbsolutePath derivedOwner = workspace.derivedData.AddDirPath("ProjectAssets");
                const filesystem::AbsolutePath intermediateOwner = workspace.intermediate.AddDirPath("ProjectAssets");
                Check(harness->config.build.artifactCacheRoot == derivedOwner.AddDirPath("VDDC") &&
                          harness->config.build.dependencyIndexRoot == derivedOwner.AddDirPath("Index") &&
                          harness->config.publication.looseRoot == intermediateOwner.AddDirPath("Loose") &&
                          harness->config.operationJournalPath == intermediateOwner.AddFilePath("operations.vtxn"),
                      "P5 generated/cache/journal authority remains contained in the approved scratch workspace");
                asset_streaming::ProjectAssetPipeline& pipeline = harness->pipeline;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath(
                    p5RestartPrepare ? "red.png" : "cold.png");
                if (!p5RestartPrepare)
                {
                    Check(AuthorTextureFixture(root, "cold.png"),
                          "P5 authors an external production-texture source after the owner is live");
                    Check(pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success,
                          "P5 authoritative rescan accepts and classifies the external texture add");
                }

                assets::AssetMetadata metadata;
                Check(ReadAssetMetadata(source, metadata), "P5 reads accepted production-texture identity");
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(metadata.id);
                Check(accepted != nullptr && accepted->presence == asset_streaming::ProjectAssetPresence::Present &&
                          accepted->importState == assets::SourceImportState::MetadataSelected && accepted->outputs.Count() == 1u,
                      "P5 production texture is present, metadata-selected and owns one accepted output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::Missing,
                          "P5 cold production output is Missing before first build");

                    asset_streaming::ProjectAssetRequest request = pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 production build request is accepted by the shared owner");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 production texture compiles, commits VADI and publishes loose output");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 1u,
                          "P5 cold production build enters the real texture compiler exactly once");

                    asset_streaming::ProjectAssetOutputDeliveryStatus delivered;
                    Check(pipeline.GetOutputDeliveryStatus(outputReference, delivered) &&
                              delivered.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                              delivered.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                              delivered.committedBuildFingerprint == delivered.publishedBuildFingerprint &&
                              delivered.committedContentFingerprint == delivered.publishedContentFingerprint &&
                              !delivered.publishedPhysicalPath.Empty() && setupFiles.FileExist(delivered.publishedPhysicalPath) &&
                              filesystem::ValidateMutationPath(harness->config.publication.looseRoot, delivered.publishedPhysicalPath) ==
                                  filesystem::MutationPathResult::Success,
                          "P5 production build has exact committed fingerprints and an owned loose publication");
                    Check(setupFiles.FileExist(harness->config.build.dependencyIndexRoot.AddFilePath("asset-dependencies.vadi")),
                          "P5 production build persists the authoritative VADI");
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 production output is UpToDate after VADI commit/publication");

                    if (!p5RestartPrepare)
                    {
                        Check(RewriteTextureSourceBlue(source) &&
                                  pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::SourceChanged,
                              "P5 production source-content change evaluates SourceChanged");
                        asset_streaming::ProjectAssetRequest sourceRequest = pipeline.Request(outputReference);
                        Check(sourceRequest.IsValid(), "P5 source-change rebuild is accepted");
                        if (sourceRequest.IsValid())
                        {
                            sourceRequest.Wait();
                            Check(sourceRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                                  "P5 source-change rebuild publishes successfully");
                            sourceRequest.Reset();
                        }
                        Check(harness->probe.compileCalls.GetValue() == 2u &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::UpToDate,
                              "P5 source change recompiles exactly once and returns UpToDate");

                        Check(RewriteTextureProfile(source, texture_tools::profiles::Color) &&
                                  pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::SourceChanged,
                              "P5 production settings change evaluates SourceChanged against VADI");
                        asset_streaming::ProjectAssetRequest settingsRequest = pipeline.Request(outputReference);
                        Check(settingsRequest.IsValid(), "P5 settings-change rebuild is accepted");
                        if (settingsRequest.IsValid())
                        {
                            settingsRequest.Wait();
                            Check(settingsRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                                  "P5 settings-change rebuild publishes successfully");
                            settingsRequest.Reset();
                        }
                        Check(harness->probe.compileCalls.GetValue() == 3u &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::UpToDate,
                              "P5 settings change recompiles exactly once and returns UpToDate");
                    }
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 production-texture harness closes cleanly");
        }
        else if (std::strcmp(argv[1], "asset-p5-active-ownership") == 0)
        {
            // E2.6 P5 rows 11/14/15: hold the *production* texture compiler at
            // its callback boundary, prove every conflicting owner refuses
            // admission, cancel through the graph owner, then prove an active
            // maintenance shutdown and clean reinitialize on the same closure.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata metadata;
            Check(ReadAssetMetadata(source, metadata), "P5 active-owner fixture reads production texture identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace), "P5 active-owner production harness opens");
            if (harness != nullptr && harness->open)
            {
                const asset_streaming::ProjectAssetState* const accepted = harness->pipeline.LookupAcceptedAsset(metadata.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 active-owner fixture resolves one production output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    harness->probe.block.SetValue(true);
                    harness->probe.started.SetValue(false);
                    harness->probe.release.SetValue(false);
                    asset_streaming::ProjectAssetRequest request = harness->pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 active production build request starts");
                    for (u32 spin = 0u; spin < 2000000u && !harness->probe.started.GetValue() && !request.HasFinished(); ++spin)
                        concurrency::YieldCurrentThread();
                    const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("blocked-move.png");
                    Check(harness->probe.started.GetValue() &&
                              harness->pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.MoveAsset(metadata.id, moved) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
                          "P5 live production build excludes rescan, E1F mutation and both maintenance owners");
                    Check(request.Cancel(), "P5 active production build cancellation is accepted");
                    request.Wait();
                    request.Reset();
                    harness->probe.block.SetValue(false);
                    harness->probe.release.SetValue(true);
                    Check(harness->pipeline.CanMutateSources() && harness->probe.compileCalls.GetValue() == 1u,
                          "P5 cancelled production build drains to a quiescent mutation owner");

                    Check(harness->pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                              harness->pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
                          "P5 shutdown drains an active maintenance owner after production-build cancellation");
                    const assets::CompilerDescriptor wrappedCompiler = harness->probe.Wrap(harness->compiler.GetDescriptor());
                    const assets::SourceImporterDescriptor importer = harness->compiler.GetImporterDescriptor();
                    Check(harness->pipeline.Initialize(filesystem::GetManager(), harness->sources, harness->streamer,
                                                      {&wrappedCompiler, 1u}, {&importer, 1u}, harness->config) ==
                              asset_streaming::ProjectAssetPipelineResult::Success &&
                              harness->pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle &&
                              harness->pipeline.GetLooseMaintenanceStatus().state == asset_streaming::ProjectAssetLooseMaintenanceState::Idle,
                          "P5 clean reinitialize has no transient maintenance replay after active-work teardown");
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 active-owner production harness closes cleanly");
        }
        else if (std::strcmp(argv[1], "asset-p5-protocol-equivalence") == 0)
        {
            // E2.6 P5 row 18: every asset handler constructs one SemanticEvent
            // and selects a renderer only at WriteSemanticEvent. Render the same
            // representative terminal objects both ways here. Actual command exit
            // mappings are covered by the one-command-per-process modes above;
            // reopening ProjectAssetContext in-process is intentionally invalid
            // because the I/O backend is one-shot.
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Completed, "asset-build", "success"),
                  "P5 completed terminal preserves operation/event/reason semantics across human and JSONL");
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Failed, "asset-status", "invalid-source-path"),
                  "P5 failed terminal preserves operation/event/reason semantics across human and JSONL");
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Cancelled, "asset-watch", "cancelled"),
                  "P5 cancelled terminal preserves operation/event/reason semantics across human and JSONL");
        }
        else
        {
            buffer = {};
            nano::ResetCommandCancellation();
            nano::ExitCode result = nano::ExitCode::InternalFailure;
            if (std::strcmp(argv[1], "asset-status") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "Textures\\red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success, "asset status succeeds");
                Check(Contains(buffer.data, "\"source\":\"textures/red.png\"") && Contains(buffer.data, "\"key\":\"main\"") &&
                          Contains(buffer.data, "\"freshness\":\"missing\""),
                      "asset status resolves canonical source and exact output key with stable freshness token");
                Check(Contains(buffer.data, "\"scope\":\"vddc-maintenance\"") && Contains(buffer.data, "\"scope\":\"loose-maintenance\""),
                      "asset status reports both maintenance lifecycle snapshots without exposing maintenance commands");
                Check(Count(buffer.data, "\"event\":\"completed\"") == 1u && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset status emits exactly one completed terminal");
            }
            else if (std::strcmp(argv[1], "asset-status-freshness-failure") == 0)
            {
                FreshnessFailureCapture freshnessCapture;
                freshnessCapture.source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                nano::Output freshnessOutput(&CaptureAndDeleteSourceAfterProjectStatus, &freshnessCapture);
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "Textures/red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, freshnessOutput);
                Check(freshnessCapture.mutationAttempted && freshnessCapture.sourceDeleted,
                      "freshness failure fixture deletes source only after project status proves the command context opened");
                Check(result == nano::ExitCode::OperationFailed && Contains(freshnessCapture.buffer.data, "\"query\":\"freshness\"") &&
                          Count(freshnessCapture.buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(freshnessCapture.buffer.data, "\"event\":\"completed\"") &&
                          !Contains(freshnessCapture.buffer.data, "\"event\":\"cancelled\""),
                      "freshness query failure preserves diagnostic evidence and terminates failed with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-status-diagnostics") == 0)
            {
                const filesystem::AbsolutePath diagnosticsRoot = root.AddDirPath("Assets").AddDirPath("Diagnostics");
                Check(setupFiles.CreatePath(diagnosticsRoot), "diagnostic fixture directory creation");
                constexpr u8 byte = 0u;
                for (u32 index = 0u; index < 70u; ++index)
                {
                    char name[32]{};
                    std::snprintf(name, sizeof(name), "missing-%02u.png", index);
                    Check(nano::platform::WriteFileDurable(diagnosticsRoot.AddFilePath(name).AsStringView(), {&byte, 1u}),
                          "diagnostic fixture source creation");
                }
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::Success, "asset status with source diagnostics succeeds");
                Check(Contains(buffer.data, "\"diagnostics-truncated\":true") &&
                          Count(buffer.data, "\"scope\":\"source-diagnostic\"") == 64u,
                      "asset status bounds source diagnostics at 64 rows and reports truncation");
                Check(Count(buffer.data, "\"event\":\"completed\"") == 1u && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "diagnostic-heavy asset status still emits exactly one completed terminal");
            }
            else if (std::strcmp(argv[1], "asset-rescan") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "rescan", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset rescan completes exactly once");
            }
            else if (std::strcmp(argv[1], "asset-build") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "build", root.AsChar(), "Textures/red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-build\"") && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset build drives real texture compiler and completes once");
            }
            else if (std::strcmp(argv[1], "asset-reimport") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "reimport", root.AsChar(), "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-reimport\"") && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset reimport drives existing coordinator and completes once");
            }
            else if (std::strcmp(argv[1], "asset-reimport-failure") == 0)
            {
                constexpr u8 invalidPng = 0u;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                Check(nano::platform::DeleteFile(source.AsStringView()) &&
                          nano::platform::WriteFileDurable(source.AsStringView(), {&invalidPng, 1u}),
                      "reimport failure fixture corrupts source bytes without changing accepted metadata");
                const char* command[]{"nanovanguard", "asset", "reimport", root.AsChar(), "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset reimport compiler failure reaches one failed terminal with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-maintain-vddc") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "vddc", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Contains(buffer.data, "\"scope\":\"vddc-maintenance\",\"state\":\"complete\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain vddc reaches satisfied complete state and one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-loose") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "loose", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Contains(buffer.data, "\"scope\":\"loose-maintenance\",\"state\":\"complete\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain loose reaches satisfied complete state and one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-all") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "all", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          AppearsBefore(buffer.data,
                                       "\"scope\":\"vddc-maintenance\",\"state\":\"complete\"",
                                       "\"scope\":\"loose-maintenance\"") &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain all completes VDDC before starting loose and emits one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-cancel") == 0)
            {
                nano::RequestCommandCancellation();
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "vddc", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Cancelled &&
                          Count(buffer.data, "\"event\":\"cancelled\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"failed\""),
                      "asset maintain pre-cancel is deterministic and emits one cancelled terminal with exit 30");
            }
            else if (std::strcmp(argv[1], "asset-maintain-invalid") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "future", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::UsageError &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "invalid-maintenance-domain"),
                      "asset maintain semantic domain validation uses one failed terminal while preserving exit 2");
            }
            else if (std::strcmp(argv[1], "asset-move") == 0 || moveRestartPrepare)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("moved.png");
                assets::AssetMetadata before;
                assets::AssetMetadata after;
                Check(ReadAssetMetadata(source, before), "asset move reads original identity");
                const char* command[]{"nanovanguard", "asset", "move", root.AsChar(), "Textures/red.png", "Textures/moved.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-move\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(MetadataPath(source).AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(moved.AsStringView()) == nano::platform::PathKind::File &&
                          ReadAssetMetadata(moved, after) && before.id == after.id,
                      "asset move uses the transaction owner and preserves AssetId");
            }
            else if (std::strcmp(argv[1], "asset-move-conflict") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "move", root.AsChar(), "Textures/red.png", "Textures/red.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::OperationFailed &&
                          Contains(buffer.data, "\"operation-result\":\"target-conflict\"") &&
                          Contains(buffer.data, "\"reason\":\"target-conflict\"") &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset move preserves exact lower AssetOperationResult in status and failed terminal");
            }
            else if (std::strcmp(argv[1], "asset-duplicate") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath copy = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("copy.png");
                assets::AssetMetadata original;
                assets::AssetMetadata duplicated;
                Check(ReadAssetMetadata(source, original), "asset duplicate reads original identity");
                const char* command[]{"nanovanguard", "asset", "duplicate", root.AsChar(), "Textures/red.png", "Textures/copy.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-duplicate\"") &&
                          Contains(buffer.data, "\"new-asset-id\":") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::File &&
                          nano::platform::GetPathKind(copy.AsStringView()) == nano::platform::PathKind::File &&
                          ReadAssetMetadata(copy, duplicated) && original.id != duplicated.id,
                      "asset duplicate keeps the source and uses a fresh pipeline-generated identity");
            }
            else if (std::strcmp(argv[1], "asset-retarget") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "retarget", root.AsChar(), "Textures/red.png",
                                      "--output", "main", "--replacement-source", "Textures/blue.png",
                                      "--replacement-output", "main", "--format", "jsonl"};
                result = registry.Dispatch(13, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-retarget\"") &&
                          Contains(buffer.data, "\"operation-result\":\"success\""),
                      "asset retarget resolves both sides through accepted source/output state and completes once");
            }
            else if (std::strcmp(argv[1], "asset-retarget-invalid") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "retarget", root.AsChar(), "Textures/red.png",
                                      "--output", "main", "--replacement-source", "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(11, command, output);
                Check(result == nano::ExitCode::UsageError && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "replacement-mapping-count-mismatch"),
                      "asset retarget semantic mapping validation uses a failed terminal before mutation");
            }
            else if (std::strcmp(argv[1], "asset-delete-reject") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "reject", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-delete\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(MetadataPath(source).AsStringView()) == nano::platform::PathKind::Missing,
                      "asset delete reject delegates safe no-referencer deletion to the transaction owner");
            }
            else if (std::strcmp(argv[1], "asset-delete-replace") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath replacement = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("blue.png");
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "replace", "--output", "main",
                                      "--replacement-source", "Textures/blue.png", "--replacement-output", "main",
                                      "--format", "jsonl"};
                result = registry.Dispatch(15, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation-result\":\"success\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(replacement.AsStringView()) == nano::platform::PathKind::File,
                      "asset delete replace validates explicit accepted replacement mappings and deletes only the selected asset");
            }
            else if (std::strcmp(argv[1], "asset-delete-invalid-policy") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "guess", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::UsageError && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "invalid-dependents-policy"),
                      "asset delete semantic policy validation uses one failed terminal while preserving exit 2");
            }
            else if (std::strcmp(argv[1], "asset-p4-help") == 0)
            {
                const char* command[]{"nanovanguard", "help", "asset"};
                result = registry.Dispatch(3, command, output);
                Check(result == nano::ExitCode::Success && Contains(buffer.data, "maintain") && Contains(buffer.data, "move") &&
                          Contains(buffer.data, "duplicate") && Contains(buffer.data, "retarget") && Contains(buffer.data, "delete") &&
                          !Contains(buffer.data, "undo") && !Contains(buffer.data, "redo"),
                      "asset help exposes P3 plus exactly the P4 mutation/maintenance family without undo/redo");
            }
            else if (std::strcmp(argv[1], "asset-watch-cancel") == 0)
            {
                nano::RequestCommandCancellation();
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                const bool validCancellation = result == nano::ExitCode::Cancelled && Count(buffer.data, "\"event\":\"cancelled\"") == 1u &&
                                               !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"failed\"");
                if (!validCancellation)
                    std::fprintf(stderr, "[nanovanguardProjectAssetTests] watch-cancel exit=%d output=%s\n", static_cast<int>(result), buffer.data);
                Check(validCancellation, "asset watch owner-visible cancellation drains and emits one cancelled terminal");
            }
            else if (std::strcmp(argv[1], "asset-watch-active-cancel") == 0)
            {
                ActiveWatchCancellationCapture activeCapture;
                nano::Output activeOutput(&CaptureAndCancelActiveWatch, &activeCapture);
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, activeOutput);
                Check(activeCapture.requested, "asset watch active-cancel fixture requests cancellation only after automatic admission starts");
                Check(result == nano::ExitCode::Cancelled && Count(activeCapture.buffer.data, "\"event\":\"cancelled\"") == 1u &&
                          !Contains(activeCapture.buffer.data, "\"event\":\"completed\"") &&
                          !Contains(activeCapture.buffer.data, "\"event\":\"failed\""),
                      "active asset watch cancellation maps lower cancelled batch to one cancelled terminal with exit 30");
            }
            else if (std::strcmp(argv[1], "asset-watch-failure") == 0)
            {
                constexpr u8 invalidPng = 0u;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                Check(nano::platform::DeleteFile(source.AsStringView()) &&
                          nano::platform::WriteFileDurable(source.AsStringView(), {&invalidPng, 1u}),
                      "watch failure fixture corrupts source bytes without changing accepted metadata");
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::OperationFailed && Contains(buffer.data, "\"reason\":\"failed\"") &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u && !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset watch sticky automatic failure closes through the shutdown boundary and emits one failed terminal with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-invalid-source") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "../red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          Contains(buffer.data, "invalid-source-path"),
                      "asset locator rejects lexical parent escape with one failed terminal");
            }
            else if (std::strcmp(argv[1], "asset-output-key") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "build", root.AsChar(), "Textures/red.png", "--output", "missing", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          Contains(buffer.data, "output-key-not-found"),
                      "asset locator rejects missing exact output key with one failed terminal");
            }
            nano::ResetCommandCancellation();
            if (result != nano::ExitCode::Success && result != nano::ExitCode::Cancelled && result != nano::ExitCode::OperationFailed)
                std::fprintf(stderr, "[nanovanguardProjectAssetTests] command mode=%s exit=%d output=%s\n", argv[1], static_cast<int>(result), buffer.data);
        }
    }
    else if (successMode)
    {
        Check(setupFiles.CreatePath(root), "project root creation");
        Check(setupFiles.CreatePath(workspace.assets), "asset root creation");
        Check(setupFiles.CreatePath(workspace.derivedData), "derived-data root creation");
        Check(setupFiles.CreatePath(workspace.intermediate), "intermediate root creation");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(workspace, "linux-x64", &failure) == nano::ProjectAssetContextResult::InvalidArgument,
              "unsupported target fails closed before backend acquisition");
        Check(!context.IsOpen(), "unsupported target leaves context closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
              "unsupported target acquires no process facilities");
        const nano::ProjectAssetContextResult openResult = context.Open(workspace, "windows-x64", &failure);
        if (openResult != nano::ProjectAssetContextResult::Success)
            std::fprintf(stderr,
                         "[nanovanguardProjectAssetTests] open result=%u stage=%u sources=%u pipeline=%u failure=%u build=%u publication=%u operation=%u\n",
                         static_cast<u32>(openResult), static_cast<u32>(failure.stage), static_cast<u32>(failure.sources), static_cast<u32>(failure.pipeline),
                         static_cast<u32>(failure.pipelineFailure.result), static_cast<u32>(failure.pipelineFailure.build),
                         static_cast<u32>(failure.pipelineFailure.publication.result), static_cast<u32>(failure.pipelineFailure.operation.result));
        Check(openResult == nano::ProjectAssetContextResult::Success,
              "GPU-free project-asset context opens");
        Check(context.IsOpen(), "opened context reports live project pipeline");
        Check(context.GetPipeline() != nullptr && context.GetPipeline()->IsInitialized(),
              "opened context exposes initialized shared project pipeline");
        Check(context.GetSources() != nullptr, "opened context owns source database");

        const asset_streaming::ProjectAssetPipelineConfig* const config = context.GetConfig();
        Check(config != nullptr, "opened context exposes derived configuration");
        if (config != nullptr)
        {
            const filesystem::AbsolutePath derivedOwner = workspace.derivedData.AddDirPath("ProjectAssets");
            const filesystem::AbsolutePath intermediateOwner = workspace.intermediate.AddDirPath("ProjectAssets");
            constexpr char identity[] = "vanguard.editor.project-assets.texture.v1";
            Check(config->build.artifactCacheRoot == derivedOwner.AddDirPath("VDDC"),
                  "headless config VDDC matches editor identity");
            Check(config->build.dependencyIndexRoot == derivedOwner.AddDirPath("Index"),
                  "headless config index matches editor identity");
            Check(config->publication.looseRoot == intermediateOwner.AddDirPath("Loose"),
                  "headless config loose root matches editor identity");
            Check(config->operationJournalPath == intermediateOwner.AddFilePath("operations.vtxn"),
                  "headless config journal matches editor identity");
            Check(config->build.target == assets::TargetPlatform::WindowsD3D12 &&
                      config->publication.target == assets::TargetPlatform::WindowsD3D12,
                  "headless config target matches editor identity");
            Check(config->build.settingsFingerprint == crypto::Sha256(identity, sizeof(identity) - 1u),
                  "headless config fingerprint matches existing editor fingerprint");
            Check(config->maximumOperationHistoryBytes == config->maximumOperationFileBytes,
                  "headless config preserves editor operation-history byte bound");
            Check(!config->automaticRecook.enabled,
                  "headless config preserves editor automatic-recook default");
        }

        Check(context.Close() == nano::ProjectAssetContextResult::Success,
              "GPU-free project-asset context closes");
        Check(!context.IsOpen(), "closed context reports closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
              "clean close releases context-owned shutdown-capable facilities");
        Check(context.Close() == nano::ProjectAssetContextResult::Success,
              "cleanly closed context has idempotent Close");
    }
    else if (rollbackMode)
    {
        const filesystem::AbsolutePath missingRoot = scratch.AddDirPath("missing-project");
        const projects::ProjectWorkspace missingWorkspace = MakeWorkspace(missingRoot);
        if (DirectoryExists(setupFiles, missingRoot))
            Check(DeleteDirectoryTree(setupFiles, missingRoot), "previous missing-root fixture cleanup");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(missingWorkspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::SourceScanFailed,
              "source-scan failure rolls backend acquisition back");
        Check(failure.stage == nano::ProjectAssetContextStage::SourceScan &&
                  failure.sources != assets::SourceDatabaseResult::Success,
              "source-scan failure preserves exact stage/source evidence");
        Check(!context.IsOpen(), "failed source scan leaves context closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
              "failed source scan releases only acquired shutdown-capable facilities");
    }
    else if (inheritedMode)
    {
        Check(setupFiles.CreatePath(root), "project root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.assets), "asset root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.derivedData), "derived-data root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.intermediate), "intermediate root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(alternateRoot), "alternate root creation");
        const filesystem::AbsolutePath alternateCache = alternateRoot.AddDirPath("Cache");
        Check(setupFiles.CreatePath(alternateCache), "alternate cache creation");
        Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguardProjectAssetTests"),
              "inherited diagnostics initialization");
        Check(vanguard::io::Initialize(), "inherited I/O initialization");
        Check(filesystem::Initialize({alternateRoot, alternateRoot, alternateCache}),
              "inherited filesystem initialization");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(workspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::InvalidState,
              "incompatible inherited filesystem fails closed");
        Check(failure.stage == nano::ProjectAssetContextStage::Filesystem,
              "incompatible inherited filesystem reports filesystem stage");
        Check(!context.IsOpen(), "incompatible inherited filesystem leaves context closed");
        Check(diagnostics::IsInitialized() && vanguard::io::IsInitialized() && filesystem::IsInitialized(),
              "failed context does not release inherited diagnostics/I/O/filesystem owners");
        Check(!jobs::IsInitialized(),
              "failed inherited-filesystem check does not acquire later jobs owner");

        filesystem::Shutdown();
        vanguard::io::Shutdown();
        diagnostics::Shutdown();
    }

    if (!moveRestartPrepare && !p5RestartPrepare && !p5RestartVerify && DirectoryExists(setupFiles, root))
        Check(DeleteDirectoryTree(setupFiles, root), "project fixture cleanup");
    if (!moveRestartPrepare && !p5RestartPrepare && !p5RestartVerify)
        Check(!DirectoryExists(setupFiles, root), "completed mode leaves no project/cache residue in the approved scratch root");
    if (DirectoryExists(setupFiles, alternateRoot))
        Check(DeleteDirectoryTree(setupFiles, alternateRoot), "alternate fixture cleanup");

    std::fprintf(stderr, "[nanovanguardProjectAssetTests] finished with %u failed checks\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
