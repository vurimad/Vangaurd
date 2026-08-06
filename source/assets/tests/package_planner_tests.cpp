#include <vanguard/assets/package_planner.hpp>
#include <vanguard/assets/package_set_assembly.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <limits>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x50535243u;
    constexpr resources::ResourceTypeId RuntimeType = 0x5052544du;

    struct Fixture
    {
        resources::ResourceReference worldSource;
        resources::ResourceReference materialSource;
        resources::ResourceReference decalSource;
        resources::ResourceReference world;
        resources::ResourceReference material;
        resources::ResourceReference decal;
        u8 worldByte = 1;
        u8 materialByte = 2;
        u8 decalByte = 3;
        bool wrongArtifactSize = false;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[packagePlannerTests] FAILED: %s", message);
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
        if (request.source.identity != fixture.worldSource)
        {
            return request.source.identity == fixture.materialSource || request.source.identity == fixture.decalSource;
        }
        const assets::BuildFingerprint sourceFingerprint = crypto::Sha256("world-source-sidecar", 20);
        return collector.Add({fixture.material, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success &&
               collector.Add({fixture.decal, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Optional}) ==
                   assets::Result::Success &&
               collector.Add({Reference("source/package/world.sidecar", SourceType), sourceFingerprint, assets::DependencyRole::Source,
                              assets::DependencyRequirement::Required}) == assets::Result::Success;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void*) noexcept
    {
        const u8 runtime[] = {context.request.source.content[0], static_cast<u8>(context.dependencies.Count())};
        if (writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, runtime,
                       sizeof(runtime)) != assets::Result::Success)
        {
            return false;
        }
        if (context.request.source.identity.Path() == resources::ResourcePath::FromString("source/package/world.asset"))
        {
            const u8 editor[] = {0xee, 0xdd};
            return writer.Add(context.request.output, 1, assets::ArtifactFlags::EditorOnly | assets::ArtifactFlags::Streamable, 8, editor,
                              sizeof(editor)) == assets::Result::Success;
        }
        return true;
    }

    [[nodiscard]] assets::BuildRequest MakeRequest(const resources::ResourceReference source, const resources::ResourceReference output,
                                                   const u8& byte) noexcept
    {
        return {{source, {&byte, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};
    }

    [[nodiscard]] bool BuildAndPublish(assets::BuildSystem& system, assets::DependencyIndex& index, const assets::BuildRequest& request,
                                       const resources::ResourceReference required = {},
                                       const assets::BuildFingerprint& requiredContent = {},
                                       const resources::ResourceReference optional = {},
                                       const assets::BuildFingerprint& optionalContent = {}) noexcept
    {
        assets::BuildPlan plan;
        if (system.Prepare(request, plan) != assets::Result::Success)
        {
            return false;
        }
        if (required.IsValid() && plan.SetGeneratedDependencyContent(required, requiredContent) != assets::Result::Success)
        {
            return false;
        }
        if (optional.IsValid() && plan.SetGeneratedDependencyContent(optional, optionalContent) != assets::Result::Success)
        {
            return false;
        }
        assets::BuildOutput output;
        return system.Execute(request, plan, output) == assets::Result::Success &&
               index.Publish(request, plan, output) == assets::IndexResult::Success;
    }

    [[nodiscard]] bool ResolvePath(const resources::ResourceReference resource, char* const destination, const usize capacity,
                                   usize& written, void* const userData) noexcept
    {
        const auto& fixture = *static_cast<Fixture*>(userData);
        const char* path = nullptr;
        if (resource == fixture.world)
        {
            path = "cooked/package/world.vworld";
        }
        else if (resource == fixture.material)
        {
            path = "cooked/package/material.vmat";
        }
        else if (resource == fixture.decal)
        {
            path = "cooked/package/decal.vtex";
        }
        if (path == nullptr)
        {
            return false;
        }
        written = 0;
        while (path[written] != '\0')
        {
            if (written == capacity)
            {
                return false;
            }
            destination[written] = path[written];
            ++written;
        }
        return true;
    }

    [[nodiscard]] bool ReadArtifact(const assets::IndexedArtifact& artifact, containers::DynamicArray<u8>& bytes,
                                    void* const userData) noexcept
    {
        auto& fixture = *static_cast<Fixture*>(userData);
        bytes.Clear();
        if (artifact.resource == fixture.world && artifact.segment == 1)
        {
            bytes.PushBack(0xee);
            bytes.PushBack(0xdd);
        }
        else if (artifact.resource == fixture.world)
        {
            bytes.PushBack(fixture.worldByte);
            bytes.PushBack(3);
        }
        else if (artifact.resource == fixture.material)
        {
            bytes.PushBack(fixture.materialByte);
            bytes.PushBack(0);
        }
        else if (artifact.resource == fixture.decal)
        {
            bytes.PushBack(fixture.decalByte);
            bytes.PushBack(0);
        }
        else
        {
            return false;
        }
        if (fixture.wrongArtifactSize)
        {
            bytes.PushBack(0xff);
        }
        return true;
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

    void DeletePackageSetFiles(filesystem::Manager& manager, const filesystem::AbsolutePath& root) noexcept
    {
        for (const char* const name : {"DATA000.vpak", "DATA000.vpak.tmp", "DATA001.vpak", "DATA001.vpak.tmp"})
        {
            const filesystem::AbsolutePath path = root.AddFilePath(name);
            if (manager.FileExist(path))
            {
                static_cast<void>(manager.DeleteFile(path));
            }
        }
    }

    [[nodiscard]] u32 AssignedPackage(const assets::PackagePlacementState& state,
                                      const resources::ResourceReference resource) noexcept
    {
        for (const assets::PackagePlacement& placement : state.placements)
        {
            if (placement.resource == resource)
            {
                return placement.packageNumber;
            }
        }
        return packages::MaximumPackageNumber + 1u;
    }

    [[nodiscard]] bool VerifyPackageDigest(filesystem::Manager& manager, const filesystem::AbsolutePath& path,
                                           const packages::PackageSetEntry& entry) noexcept
    {
        auto reader = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader || reader->GetSize() != entry.fileSize || entry.fileSize > std::numeric_limits<u32>::max())
        {
            return false;
        }
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Resize(static_cast<u32>(entry.fileSize));
        reader->Serialize(bytes.Data(), bytes.Size());
        if (reader->HasErrors())
        {
            return false;
        }
        const crypto::Digest256 digest = crypto::Sha256(bytes.Data(), bytes.Size());
        for (u32 byte = 0; byte < crypto::Digest256::ByteCount; ++byte)
        {
            if (digest.bytes[byte] != entry.contentDigest[byte])
            {
                return false;
            }
        }
        return true;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "packagePlannerTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath root = working.AddDirPath("vanguard_package_planner_tests");
    Check(filesystem::Initialize({working, working, root}), "filesystem initialization");
    filesystem::Manager& manager = filesystem::GetManager();
    DeleteIndexFiles(manager, root);
    Check(manager.CreatePath(root), "planner test root");

    Fixture fixture;
    fixture.worldSource = Reference("source/package/world.asset", SourceType);
    fixture.materialSource = Reference("source/package/material.asset", SourceType);
    fixture.decalSource = Reference("source/package/decal.asset", SourceType);
    fixture.world = Reference("cooked/package/world.vworld", RuntimeType);
    fixture.material = Reference("cooked/package/material.vmat", RuntimeType);
    fixture.decal = Reference("cooked/package/decal.vtex", RuntimeType);

    assets::BuildSystem buildSystem;
    Check(buildSystem.Initialize(), "build-system initialization");
    const assets::CompilerDescriptor compiler{assets::HashCompilerName("assets.package_planner_test"),
                                              "assets.package_planner_test",
                                              1,
                                              SourceType,
                                              RuntimeType,
                                              &Discover,
                                              &Compile,
                                              &fixture};
    Check(buildSystem.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");

    assets::DependencyIndexConfig indexConfig;
    indexConfig.root = root.AsChar();
    indexConfig.settingsFingerprint = crypto::Sha256("package-planner-settings", 24);
    assets::DependencyIndex index;
    Check(index.Initialize(indexConfig) == assets::IndexResult::Success, "index initialization");

    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.materialSource, fixture.material, fixture.materialByte)),
          "material publication");
    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.decalSource, fixture.decal, fixture.decalByte)), "decal publication");
    assets::DependencyRecord materialRecord;
    assets::DependencyRecord decalRecord;
    Check(index.Find(fixture.material, materialRecord) == assets::IndexResult::Success, "material lookup");
    Check(index.Find(fixture.decal, decalRecord) == assets::IndexResult::Success, "decal lookup");
    Check(BuildAndPublish(buildSystem, index, MakeRequest(fixture.worldSource, fixture.world, fixture.worldByte), fixture.material,
                          materialRecord.contentFingerprint, fixture.decal, decalRecord.contentFingerprint),
          "world publication");
    Check(index.Save() == assets::IndexResult::Success, "index save");

    assets::PackageManifest manifest;
    manifest.packageId = 0x1122334455667788ull;
    Check(manifest.AddRoot({fixture.world, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success, "manifest root");
    Check(manifest.AddRoot({fixture.world, assets::PackageRootFlags::None}) == assets::PackagingResult::DuplicateRoot,
          "duplicate manifest root");

    containers::DynamicArray<u8> manifestBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter manifestWriter(manifestBytes);
    Check(assets::WritePackageManifest(manifestWriter, manifest) == assets::PackagingResult::Success, "manifest serialization");
    assets::PackageManifest decoded;
    filesystem::MemoryFileReader manifestReader(manifestBytes, 0);
    Check(assets::ReadPackageManifest(manifestReader, decoded) == assets::PackagingResult::Success &&
              decoded.packageId == manifest.packageId && decoded.roots.Size() == 1 && decoded.roots[0].resource == fixture.world,
          "manifest round trip");

    containers::DynamicArray<u8> corruptManifest(manifestBytes);
    corruptManifest.Back() ^= 1u;
    filesystem::MemoryFileReader corruptReader(corruptManifest, 0);
    assets::PackageManifest rejected;
    Check(assets::ReadPackageManifest(corruptReader, rejected) == assets::PackagingResult::CorruptManifest, "manifest integrity rejection");

    assets::PackagePlanner planner;
    assets::PackageBuildPlan runtimePlan;
    Check(planner.Prepare(decoded, index, runtimePlan) == assets::PackagingResult::Success, "runtime plan");
    Check(runtimePlan.IsPrepared() && runtimePlan.resources.Size() == 2 && runtimePlan.options.packageId == manifest.packageId &&
              runtimePlan.buildId != 0,
          "runtime closure and deterministic identity");
    if (runtimePlan.resources.Size() == 2)
    {
        const assets::PlannedPackageResource* worldPlan =
            runtimePlan.resources[0].resource == fixture.world ? &runtimePlan.resources[0] : &runtimePlan.resources[1];
        Check(worldPlan->resource == fixture.world && worldPlan->segments.Size() == 1 && worldPlan->dependencies.Size() == 2 &&
                  static_cast<u32>(worldPlan->flags) == static_cast<u32>(packages::ResourceFlags::Startup),
              "runtime artifact filtering and dependency metadata");
    }

    const assets::PackageAssemblyCallbacks assemblyCallbacks{&ResolvePath, &ReadArtifact, &fixture};
    assets::PackageAssembler assembler;
    containers::DynamicArray<u8> packageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packageWriter(packageBytes);
    Check(assembler.Assemble(runtimePlan, packageWriter, assemblyCallbacks) == assets::PackagingResult::Success,
          "assemble planned package");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == packages::Result::Success && packageReader.Header().packageId == runtimePlan.packageId &&
              packageReader.Header().buildId == runtimePlan.buildId && packageReader.Header().resourceCount == 2,
          "assembled package validation");
    const packages::Resource* packagedWorld = packageReader.Find("cooked/package/world.vworld");
    Check(packagedWorld != nullptr && packageReader.Segments(*packagedWorld).Count() == 1 &&
              packageReader.Dependencies(*packagedWorld).Count() == 2,
          "assembled world metadata");
    packageReader.Close();

    const filesystem::AbsolutePath packagePath = root.AddFilePath("planned.vpak");
    const filesystem::AbsolutePath temporaryPackagePath = root.AddFilePath("planned.vpak.tmp");
    fixture.wrongArtifactSize = true;
    containers::DynamicArray<u8> rejectedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter rejectedWriter(rejectedBytes);
    Check(assembler.Assemble(runtimePlan, rejectedWriter, assemblyCallbacks) == assets::PackagingResult::InvalidArtifactData,
          "artifact size contract");
    Check(assembler.Publish(runtimePlan, packagePath, temporaryPackagePath, assemblyCallbacks) ==
                  assets::PackagingResult::InvalidArtifactData &&
              !manager.FileExist(packagePath) && !manager.FileExist(temporaryPackagePath),
          "failed publication removes partial output");
    fixture.wrongArtifactSize = false;

    auto abandoned = manager.CreateFileWriter(temporaryPackagePath, filesystem::FOF_Buffered);
    const u8 abandonedBytes[] = {1, 2, 3, 4};
    if (abandoned)
    {
        abandoned->Serialize(const_cast<u8*>(abandonedBytes), sizeof(abandonedBytes));
        abandoned.Reset();
    }
    Check(manager.FileExist(temporaryPackagePath), "abandoned temporary fixture");
    Check(assembler.Publish(runtimePlan, packagePath, temporaryPackagePath, assemblyCallbacks) == assets::PackagingResult::Success &&
              manager.FileExist(packagePath) && !manager.FileExist(temporaryPackagePath),
          "abandoned-temporary recovery and validated atomic publication");

    assets::PackageBuildPlan repeatedPlan;
    Check(planner.Prepare(decoded, index, repeatedPlan) == assets::PackagingResult::Success && repeatedPlan.buildId == runtimePlan.buildId,
          "deterministic repeated planning");

    decoded.flags = decoded.flags | assets::PackageManifestFlags::IncludeOptionalDependencies |
                    assets::PackageManifestFlags::IncludeEditorArtifacts | assets::PackageManifestFlags::HasDebugPaths;
    assets::PackageBuildPlan editorPlan;
    Check(planner.Prepare(decoded, index, editorPlan) == assets::PackagingResult::Success && editorPlan.resources.Size() == 3 &&
              editorPlan.buildId != runtimePlan.buildId,
          "optional closure and editor artifact policy");
    if (editorPlan.resources.Size() == 3)
    {
        const assets::PlannedPackageResource* worldPlan = nullptr;
        for (const assets::PlannedPackageResource& resource : editorPlan.resources)
        {
            if (resource.resource == fixture.world)
            {
                worldPlan = &resource;
            }
        }
        Check(worldPlan != nullptr && worldPlan->segments.Size() == 2, "editor plan retains editor segment");
    }

    assets::PackageSetPlanOptions setOptions;
    setOptions.gameId = 0x56414e4755415244ull;
    setOptions.startupWorld = fixture.world;
    setOptions.defaultInput = fixture.material;
    setOptions.targetPackageBytes = 4096;
    setOptions.maximumPackageBytes = 16384;
    setOptions.maximumBootstrapBytes = 16384;
    setOptions.maximumPackages = 4;
    assets::PackageSetPlanner setPlanner;
    assets::PackageSetBuildPlan setPlan;
    assets::PackagePlacementState placement;
    Check(setPlanner.Prepare(editorPlan, setOptions, nullptr, setPlan, placement) == assets::PackagingResult::Success &&
              setPlan.IsPrepared() && setPlan.packages.Size() == 2 && setPlan.packages[0].packageNumber == 0 &&
              setPlan.packages[1].packageNumber == 1 && AssignedPackage(placement, fixture.world) == 0 &&
              AssignedPackage(placement, fixture.material) == 0 && AssignedPackage(placement, fixture.decal) == 1,
          "stable DATA000 bootstrap closure and numbered bulk placement");

    containers::DynamicArray<u8> placementBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter placementWriter(placementBytes);
    Check(assets::WritePackagePlacementState(placementWriter, placement) == assets::PackagingResult::Success,
          "package placement serialization");
    assets::PackagePlacementState decodedPlacement;
    filesystem::MemoryFileReader placementReader(placementBytes, 0);
    Check(assets::ReadPackagePlacementState(placementReader, decodedPlacement) == assets::PackagingResult::Success &&
              AssignedPackage(decodedPlacement, fixture.decal) == 1,
          "package placement round trip");
    assets::PackageSetBuildPlan stableSetPlan;
    assets::PackagePlacementState stablePlacement;
    Check(setPlanner.Prepare(editorPlan, setOptions, &decodedPlacement, stableSetPlan, stablePlacement) ==
                  assets::PackagingResult::Success &&
              stableSetPlan.buildId == setPlan.buildId && AssignedPackage(stablePlacement, fixture.decal) == 1,
          "persisted package ownership remains stable");

    containers::DynamicArray<u8> corruptPlacementBytes(placementBytes);
    Check(!corruptPlacementBytes.Empty(), "serialized placement has bytes");
    if (!corruptPlacementBytes.Empty())
    {
        corruptPlacementBytes.Back() ^= 1u;
        filesystem::MemoryFileReader corruptPlacementReader(corruptPlacementBytes, 0);
        assets::PackagePlacementState corruptPlacement;
        Check(assets::ReadPackagePlacementState(corruptPlacementReader, corruptPlacement) == assets::PackagingResult::CorruptManifest,
              "package placement integrity rejection");
    }

    const filesystem::AbsolutePath packageSetRoot = root.AddDirPath("package_set_output");
    Check(manager.CreatePath(packageSetRoot), "package-set output directory");
    DeletePackageSetFiles(manager, packageSetRoot);
    assets::PackageSetAssembler setAssembler;
    const assets::PackagingResult setPublication = setAssembler.Publish(setPlan, packageSetRoot, assemblyCallbacks);
    Check(setPublication == assets::PackagingResult::Success,
          "deterministic multi-package publication");
    const filesystem::AbsolutePath data000 = packageSetRoot.AddFilePath("DATA000.vpak");
    const filesystem::AbsolutePath data001 = packageSetRoot.AddFilePath("DATA001.vpak");
    auto rootReaderFile = manager.CreateFileReader(data000, filesystem::FOF_Buffered);
    auto bulkReaderFile = manager.CreateFileReader(data001, filesystem::FOF_Buffered);
    packages::PackageReader rootReader;
    packages::PackageReader bulkReader;
    const bool rootOpened = rootReaderFile && rootReader.Open(*rootReaderFile) == packages::Result::Success;
    const bool bulkOpened = bulkReaderFile && bulkReader.Open(*bulkReaderFile) == packages::Result::Success;
    const packages::PackageSet* packageSet = rootOpened ? rootReader.GetPackageSet() : nullptr;
    Check(rootOpened && bulkOpened && rootReader.HasPackageSet() && !bulkReader.HasPackageSet() && packageSet != nullptr &&
              packageSet->packages.Size() == 1 && packageSet->packages[0].packageNumber == 1 &&
              packageSet->packages[0].packageId == bulkReader.Header().packageId &&
              packageSet->packages[0].buildId == bulkReader.Header().buildId &&
              packageSet->packages[0].fileSize == bulkReader.Header().fileSize &&
              packageSet->packages[0].indexCrc64 == bulkReader.Header().indexCrc64 &&
              VerifyPackageDigest(manager, data001, packageSet->packages[0]),
          "DATA000 catalog authenticates DATA001 metadata and content");
    rootReader.Close();
    bulkReader.Close();
    rootReaderFile.Reset();
    bulkReaderFile.Reset();
    Check(setAssembler.Publish(setPlan, packageSetRoot, assemblyCallbacks) == assets::PackagingResult::PublicationFailed &&
              manager.FileExist(data000) && manager.FileExist(data001),
          "package-set publication refuses to overwrite a committed image");

    DeletePackageSetFiles(manager, packageSetRoot);
    fixture.wrongArtifactSize = true;
    Check(setAssembler.Publish(setPlan, packageSetRoot, assemblyCallbacks) == assets::PackagingResult::InvalidArtifactData &&
              !manager.FileExist(data000) && !manager.FileExist(data001) &&
              !manager.FileExist(packageSetRoot.AddFilePath("DATA000.vpak.tmp")) &&
              !manager.FileExist(packageSetRoot.AddFilePath("DATA001.vpak.tmp")),
          "failed package-set publication leaves no commit marker or partial packages");
    fixture.wrongArtifactSize = false;
    DeletePackageSetFiles(manager, packageSetRoot);

    assets::PackageManifest missing;
    missing.packageId = 7;
    Check(missing.AddRoot({Reference("cooked/package/missing.vmesh", RuntimeType), {}}) == assets::PackagingResult::Success,
          "missing manifest construction");
    assets::PackageBuildPlan missingPlan;
    Check(planner.Prepare(missing, index, missingPlan) == assets::PackagingResult::MissingResource, "missing required root");

    assets::PackageManifest wrongTarget(manifest);
    wrongTarget.target = assets::TargetPlatform::LinuxVulkan;
    assets::PackageBuildPlan wrongTargetPlan;
    Check(planner.Prepare(wrongTarget, index, wrongTargetPlan) == assets::PackagingResult::TargetMismatch, "target mismatch");

    Check(index.Shutdown(), "index shutdown");
    Check(buildSystem.UnregisterCompiler(compiler.id) == assets::Result::Success, "compiler unregistration");
    Check(buildSystem.Shutdown(), "build-system shutdown");
    if (manager.FileExist(packagePath))
    {
        static_cast<void>(manager.DeleteFile(packagePath));
    }
    if (manager.FileExist(temporaryPackagePath))
    {
        static_cast<void>(manager.DeleteFile(temporaryPackagePath));
    }
    DeleteIndexFiles(manager, root);
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
