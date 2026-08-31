#include <vanguard/assets/asset_index.hpp>
#include <vanguard/assets/package_planner.hpp>
#include <vanguard/assets/package_set_assembly.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/game_input/mapping_resource.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/world/cells.hpp>
#include <vanguard/world/worlds.hpp>

namespace
{
    namespace assets = vanguard::assets;
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace gameInput = vanguard::game_input;
    namespace memory = vanguard::memory;
    namespace packages = vanguard::packages;
    namespace prefabs = vanguard::prefabs;
    namespace resources = vanguard::resources;
    namespace serialization = vanguard::serialization;
    namespace world = vanguard::world;

    constexpr vanguard::u64 GameId = 0x56414e4755415244ull;
    constexpr vanguard::u64 WorldId = 0x444556574f524c44ull;
    constexpr vanguard::u64 CellId = 0x44455643454c4c01ull;
    constexpr resources::ResourceTypeId InputSourceType = serialization::MakeFourCC('I', 'N', 'S', 'R');
    constexpr resources::ResourceTypeId PrefabSourceType = serialization::MakeFourCC('P', 'F', 'S', 'R');
    constexpr resources::ResourceTypeId CellSourceType = serialization::MakeFourCC('C', 'E', 'S', 'R');
    constexpr resources::ResourceTypeId WorldSourceType = serialization::MakeFourCC('W', 'L', 'S', 'R');

    struct StoredArtifact final
    {
        StoredArtifact() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        resources::ResourceReference resource;
        const char* path = nullptr;
        containers::DynamicArray<vanguard::u8> bytes;
    };

    struct BootstrapContent final
    {
        BootstrapContent() noexcept
        {
            input.resource = Reference("input/default.vinput", gameInput::MappingResourceType);
            input.path = "input/default.vinput";
            prefab.resource = Reference("prefabs/bootstrap.vprefab", prefabs::PrefabResourceType);
            prefab.path = "prefabs/bootstrap.vprefab";
            cell.resource = Reference("world/bootstrap.vcell", world::CellResourceType);
            cell.path = "world/bootstrap.vcell";
            startupWorld.resource = Reference("world/bootstrap.vworld", world::WorldResourceType);
            startupWorld.path = "world/bootstrap.vworld";
        }

        [[nodiscard]] static resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
        {
            return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
        }

        [[nodiscard]] StoredArtifact* Find(const resources::ResourceReference resource) noexcept
        {
            for (StoredArtifact* const artifact : {&input, &prefab, &cell, &startupWorld})
                if (artifact->resource == resource)
                    return artifact;
            return nullptr;
        }

        [[nodiscard]] const StoredArtifact* Find(const resources::ResourceReference resource) const noexcept
        {
            for (const StoredArtifact* const artifact : {&input, &prefab, &cell, &startupWorld})
                if (artifact->resource == resource)
                    return artifact;
            return nullptr;
        }

        StoredArtifact input;
        StoredArtifact prefab;
        StoredArtifact cell;
        StoredArtifact startupWorld;
    };

    [[nodiscard]] bool DiscoverDependencies(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* const userData) noexcept
    {
        const auto& content = *static_cast<const BootstrapContent*>(userData);
        if (request.output == content.startupWorld.resource)
            return dependencies.Add({content.cell.resource, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success;
        if (request.output == content.cell.resource)
            return dependencies.Add({content.prefab.resource, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success;
        return request.output == content.input.resource || request.output == content.prefab.resource;
    }

    void SetBounds(world::Bounds& bounds, const vanguard::f32 extent) noexcept
    {
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = -extent;
            bounds.maximum[axis] = extent;
        }
    }

    void SetBounds(world::WorldBounds& bounds, const vanguard::f64 extent) noexcept
    {
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = -extent;
            bounds.maximum[axis] = extent;
        }
    }

    [[nodiscard]] bool CompileResource(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* const userData) noexcept
    {
        const auto& content = *static_cast<const BootstrapContent*>(userData);
        containers::DynamicArray<vanguard::u8> bytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter output(bytes);
        bool cooked = false;

        if (context.request.output == content.input.resource)
        {
            const gameInput::MappingBuildDescription description;
            cooked = gameInput::CookMapping(description, output) == gameInput::MappingResult::Success;
        }
        else if (context.request.output == content.prefab.resource)
        {
            const prefabs::EntityBuildRecord entity{1, prefabs::InvalidStableId, 0x626f6f7473747261ull, prefabs::EntityFlags::Root};
            prefabs::CookDescription description;
            description.name = 0x626f6f7473747261ull;
            description.entities = {&entity, 1};
            description.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count());
            cooked = prefabs::CookPrefab(description, output) == prefabs::Result::Success;
        }
        else if (context.request.output == content.cell.resource)
        {
            world::PlacementBuildRecord placement;
            placement.entityId = 1;
            placement.name = 0x626f6f7473747261ull;
            placement.prefab = content.prefab.resource;
            SetBounds(placement.bounds, 1.0f);
            placement.streamingDistance = 1000.0f;
            placement.visibilityDistance = 1000.0f;
            placement.flags = world::PlacementFlags::Persistent;

            world::CellBuildDescription description;
            description.cellId = CellId;
            description.worldId = WorldId;
            description.category = world::CellCategory::AlwaysLoaded;
            SetBounds(description.bounds, 100.0f);
            description.placements = {&placement, 1};
            description.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count());
            cooked = world::CookCell(description, output) == world::Result::Success;
        }
        else if (context.request.output == content.startupWorld.resource)
        {
            world::WorldCellBuildRecord cell;
            cell.cellId = CellId;
            cell.name = 0x626f6f7473747261ull;
            cell.cell = content.cell.resource;
            cell.category = world::CellCategory::AlwaysLoaded;
            cell.streamingPriority = world::StreamingPriority::Critical;
            SetBounds(cell.bounds, 100.0);
            cell.activationDistance = 1000.0f;
            cell.retentionDistance = 1200.0f;
            cell.flags = world::WorldCellFlags::AlwaysLoaded;

            world::WorldBuildDescription description;
            description.worldId = WorldId;
            SetBounds(description.bounds, 100.0);
            description.cells = {&cell, 1};
            description.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count());
            cooked = world::CookWorld(description, output) == world::Result::Success;
        }

        return cooked && artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                                       bytes.Size()) == assets::Result::Success;
    }

    [[nodiscard]] assets::CompilerDescriptor Compiler(const char* const name, const resources::ResourceTypeId sourceType,
                                                      const resources::ResourceTypeId outputType, BootstrapContent& content) noexcept
    {
        return {assets::HashCompilerName(name), name, 1, sourceType, outputType, &DiscoverDependencies, &CompileResource, &content};
    }

    [[nodiscard]] bool StoreBuildOutput(BootstrapContent& content, const assets::BuildOutput& output) noexcept
    {
        for (const assets::Artifact& artifact : output.artifacts)
        {
            StoredArtifact* const destination = content.Find(artifact.resource);
            if (destination == nullptr || artifact.segment != 0)
                return false;
            destination->bytes = artifact.bytes;
        }
        return true;
    }

    [[nodiscard]] bool BuildResource(assets::BuildSystem& builds, assets::DependencyIndex& index, BootstrapContent& content, const char* const sourcePath,
                                     const resources::ResourceTypeId sourceType, StoredArtifact& artifact,
                                     const StoredArtifact* const generatedDependency = nullptr) noexcept
    {
        constexpr char SourceContent[] = "Vanguard deterministic development bootstrap source 1";
        const assets::BuildRequest request{
            {BootstrapContent::Reference(sourcePath, sourceType), {reinterpret_cast<const vanguard::u8*>(SourceContent), sizeof(SourceContent) - 1u}, {}},
            artifact.resource,
            assets::TargetPlatform::WindowsD3D12,
            {}};
        assets::BuildPlan plan;
        if (builds.Prepare(request, plan) != assets::Result::Success)
            return false;
        if (generatedDependency != nullptr)
        {
            assets::DependencyRecord dependencyRecord;
            if (index.Find(generatedDependency->resource, dependencyRecord) != assets::IndexResult::Success ||
                plan.SetGeneratedDependencyContent(generatedDependency->resource, dependencyRecord.contentFingerprint) != assets::Result::Success)
                return false;
        }
        assets::BuildOutput output;
        return builds.Execute(request, plan, output) == assets::Result::Success && StoreBuildOutput(content, output) &&
               index.Publish(request, plan, output) == assets::IndexResult::Success;
    }

    [[nodiscard]] bool ResolvePath(const resources::ResourceReference resource, char* const destination, const vanguard::usize capacity,
                                   vanguard::usize& written, void* const userData) noexcept
    {
        const StoredArtifact* const artifact = static_cast<const BootstrapContent*>(userData)->Find(resource);
        if (artifact == nullptr || artifact->path == nullptr)
            return false;
        written = 0;
        while (artifact->path[written] != '\0')
        {
            if (written == capacity)
                return false;
            destination[written] = artifact->path[written];
            ++written;
        }
        return true;
    }

    [[nodiscard]] bool ReadArtifact(const assets::IndexedArtifact& artifact, containers::DynamicArray<vanguard::u8>& bytes, void* const userData) noexcept
    {
        const StoredArtifact* const stored = static_cast<const BootstrapContent*>(userData)->Find(artifact.resource);
        if (stored == nullptr || artifact.segment != 0 || artifact.byteCount != stored->bytes.Size())
            return false;
        bytes = stored->bytes;
        return true;
    }

    [[nodiscard]] bool ValidateImage(filesystem::Manager& files, const filesystem::AbsolutePath& path, const BootstrapContent& content) noexcept
    {
        auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
        packages::PackageReader reader;
        if (!file || reader.Open(*file) != packages::Result::Success || !reader.HasPackageSet())
            return false;
        const packages::PackageSet* const packageSet = reader.GetPackageSet();
        return packageSet != nullptr && packageSet->gameId == GameId && packageSet->startupWorld == content.startupWorld.resource.GetPath().Id() &&
               packageSet->startupWorldType == content.startupWorld.resource.ExpectedType() && packageSet->defaultInput == content.input.resource.GetPath().Id() &&
               packageSet->defaultInputType == content.input.resource.ExpectedType();
    }

    [[nodiscard]] bool BuildBootstrapImage(const char* const runtimeRootArgument) noexcept
    {
        if (runtimeRootArgument == nullptr || runtimeRootArgument[0] == '\0')
            return false;
        const filesystem::AbsolutePath outputDirectory = filesystem::AbsolutePath::CreateDirPath(runtimeRootArgument);
        const filesystem::AbsolutePath cacheDirectory = filesystem::paths::GetUserCacheDirectory().AddDirPath("bootstrapImage");
        if (!filesystem::Initialize({outputDirectory, outputDirectory, cacheDirectory}))
            return false;
        filesystem::Manager& files = filesystem::GetManager();
        const filesystem::AbsolutePath packagePath = outputDirectory.AddFilePath("DATA000.vpak");
        if (files.FileExist(packagePath))
        {
            VG_LOG_ERROR(diagnostics::Category::Resources, "bootstrap image already exists at %s; committed DATA images are never overwritten implicitly",
                         packagePath.AsChar());
            filesystem::Shutdown();
            return false;
        }
        if (!files.CreatePath(cacheDirectory))
        {
            filesystem::Shutdown();
            return false;
        }

        BootstrapContent content;
        assets::Config buildConfig;
        const filesystem::AbsolutePath ddcDirectory = cacheDirectory.AddDirPath("ddc");
        static_cast<void>(files.CreatePath(ddcDirectory));
        buildConfig.persistentCacheRoot = ddcDirectory.AsChar();
        assets::BuildSystem builds;
        assets::DependencyIndex index;
        assets::DependencyIndexConfig indexConfig;
        indexConfig.root = cacheDirectory.AsChar();
        indexConfig.settingsFingerprint = crypto::Sha256("bootstrapImage.settings.1", 25);

        const assets::CompilerDescriptor compilers[]{Compiler("bootstrap.input", InputSourceType, gameInput::MappingResourceType, content),
                                                     Compiler("bootstrap.prefab", PrefabSourceType, prefabs::PrefabResourceType, content),
                                                     Compiler("bootstrap.cell", CellSourceType, world::CellResourceType, content),
                                                     Compiler("bootstrap.world", WorldSourceType, world::WorldResourceType, content)};
        bool succeeded = builds.Initialize(buildConfig) && assets::IsSuccess(index.Initialize(indexConfig));
        for (const assets::CompilerDescriptor& compiler : compilers)
            succeeded = succeeded && builds.RegisterCompiler(compiler) == assets::Result::Success;
        succeeded = succeeded && BuildResource(builds, index, content, "source/input/default.bootstrap", InputSourceType, content.input) &&
                    BuildResource(builds, index, content, "source/prefabs/bootstrap.bootstrap", PrefabSourceType, content.prefab) &&
                    BuildResource(builds, index, content, "source/world/bootstrap_cell.bootstrap", CellSourceType, content.cell, &content.prefab) &&
                    BuildResource(builds, index, content, "source/world/bootstrap_world.bootstrap", WorldSourceType, content.startupWorld, &content.cell) &&
                    index.Save() == assets::IndexResult::Success;

        assets::PackageManifest manifest;
        manifest.packageId = GameId;
        succeeded = succeeded && manifest.AddRoot({content.startupWorld.resource, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success &&
                    manifest.AddRoot({content.input.resource, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success;
        assets::PackageBuildPlan packagePlan;
        assets::PackagePlanner packagePlanner;
        succeeded = succeeded && packagePlanner.Prepare(manifest, index, packagePlan) == assets::PackagingResult::Success;

        assets::PackageSetPlanOptions setOptions;
        setOptions.gameId = GameId;
        setOptions.startupWorld = content.startupWorld.resource;
        setOptions.defaultInput = content.input.resource;
        assets::PackageSetBuildPlan setPlan;
        assets::PackagePlacementState placement;
        assets::PackageSetPlanner setPlanner;
        succeeded = succeeded && setPlanner.Prepare(packagePlan, setOptions, nullptr, setPlan, placement) == assets::PackagingResult::Success;
        const assets::PackageAssemblyCallbacks callbacks{&ResolvePath, &ReadArtifact, &content};
        assets::PackageSetAssembler assembler;
        succeeded = succeeded && assembler.Publish(setPlan, outputDirectory, callbacks) == assets::PackagingResult::Success &&
                    ValidateImage(files, packagePath, content);

        for (const assets::CompilerDescriptor& compiler : compilers)
            if (builds.IsInitialized())
                static_cast<void>(builds.UnregisterCompiler(compiler.id));
        if (index.IsInitialized())
            static_cast<void>(index.Shutdown());
        if (builds.IsInitialized())
            static_cast<void>(builds.Shutdown());
        filesystem::Shutdown();
        return succeeded;
    }
} // namespace

int main(const int argumentCount, const char* const* const arguments)
{
    if (!memory::Initialize())
        return 1;
    const bool diagnosticsInitialized = diagnostics::Initialize(diagnostics::Mode::Synchronous, "bootstrapImage");
    const bool containersInitialized = diagnosticsInitialized && containers::Initialize();
    const bool ioInitialized = containersInitialized && vanguard::io::Initialize();
    const bool succeeded = ioInitialized && argumentCount == 2 && BuildBootstrapImage(arguments[1]);
    if (succeeded)
        VG_LOG_INFO(diagnostics::Category::Resources, "published and validated DATA000.vpak beside the runtime executable");
    else if (diagnosticsInitialized)
        VG_LOG_ERROR(diagnostics::Category::Resources, "development bootstrap image generation failed; usage: bootstrapImage <runtime-image-directory>");
    if (ioInitialized)
        vanguard::io::Shutdown();
    if (diagnosticsInitialized)
        diagnostics::Shutdown();
    return succeeded ? 0 : 1;
}
