#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/rendering/renderer_feature_programs.hpp>
#include <vanguard/schemas/schemas.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/world/worlds.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace filesystem = vanguard::filesystem;
    namespace packages = vanguard::packages;
    namespace prefabs = vanguard::prefabs;
    namespace reflection = vanguard::reflection;
    namespace rendering = vanguard::rendering;
    namespace schemas = vanguard::schemas;
    namespace serialization = vanguard::serialization;
    namespace world = vanguard::world;

    using ByteArray = containers::DynamicArray<vanguard::u8>;

    constexpr char PrefabPath[] = "prefabs/bootstrap.vprefab";
    constexpr char WorldPath[] = "world/bootstrap.vworld";
    constexpr char FixtureFingerprintTag[] = "editor-scene-output-camera-only-package-v1";
    constexpr vanguard::u32 StandardBootstrapResourceCount = 47;

    struct CameraData final
    {
        vanguard::f32 verticalFieldOfViewRadians = 1.0471975512f;
        vanguard::f32 nearPlane = 0.1f;
        vanguard::f32 farPlane = 1000.0f;
        vanguard::u64 layerMask = ~vanguard::u64{0};
        vanguard::u32 visibilityMask = ~vanguard::u32{0};
    };

    constexpr reflection::SchemaTypeId CameraComponentType = reflection::HashSchemaName("vanguard.camera_component");
    const reflection::SchemaField CameraFields[]{
        reflection::MakeField("vertical_field_of_view_radians", reflection::builtin::F32, reflection::ValueKind::F32,
                              static_cast<vanguard::u32>(offsetof(CameraData, verticalFieldOfViewRadians)), sizeof(vanguard::f32),
                              alignof(vanguard::f32)),
        reflection::MakeField("near_plane", reflection::builtin::F32, reflection::ValueKind::F32,
                              static_cast<vanguard::u32>(offsetof(CameraData, nearPlane)), sizeof(vanguard::f32), alignof(vanguard::f32)),
        reflection::MakeField("far_plane", reflection::builtin::F32, reflection::ValueKind::F32,
                              static_cast<vanguard::u32>(offsetof(CameraData, farPlane)), sizeof(vanguard::f32), alignof(vanguard::f32)),
        reflection::MakeField("layer_mask", reflection::builtin::U64, reflection::ValueKind::U64,
                              static_cast<vanguard::u32>(offsetof(CameraData, layerMask)), sizeof(vanguard::u64), alignof(vanguard::u64)),
        reflection::MakeField("visibility_mask", reflection::builtin::U32, reflection::ValueKind::U32,
                              static_cast<vanguard::u32>(offsetof(CameraData, visibilityMask)), sizeof(vanguard::u32), alignof(vanguard::u32))};
    const reflection::Schema CameraSchema{
        CameraComponentType, "vanguard.camera_component", sizeof(CameraData), alignof(CameraData), 1, 1, CameraFields, 5};

    [[nodiscard]] bool Fail(const char* const stage) noexcept
    {
        std::fprintf(stderr, "editorSceneOutputPackageFixture: %s failed\n", stage);
        return false;
    }

    [[nodiscard]] bool CopyPath(const char* const source, char* const destination, const vanguard::usize capacity) noexcept
    {
        const vanguard::usize length = std::strlen(source);
        if (length + 1u > capacity)
            return false;
        std::memcpy(destination, source, length + 1u);
        return true;
    }

    [[nodiscard]] bool ResolveBootstrapPath(const packages::ResourceId id, char* const destination,
                                            const vanguard::usize capacity) noexcept
    {
        constexpr const char* FixedPaths[]{
            "input/default.vinput",
            PrefabPath,
            "meshes/bootstrap_quad.vmesh",
            "materials/bootstrap_surface.vmat",
            "materials/bootstrap_surface_depth.vshader",
            "materials/bootstrap_surface_opaque.vshader",
            "materials/bootstrap_surface_depth.vppl",
            "materials/bootstrap_surface_opaque.vppl",
            "world/bootstrap.vcell",
            WorldPath,
            "engine/rendering/default.vrcat"};
        for (const char* const path : FixedPaths)
        {
            if (packages::HashResourcePath(path) == id)
                return CopyPath(path, destination, capacity);
        }

        char candidate[256]{};
        for (const rendering::RendererFeatureProgram& feature : rendering::RendererFeaturePrograms)
        {
            const int shaderLength = std::snprintf(candidate, sizeof(candidate), "engine/rendering/%s.vshader", feature.name);
            if (shaderLength <= 0 || static_cast<vanguard::usize>(shaderLength) >= sizeof(candidate))
                return false;
            if (packages::HashResourcePath(candidate) == id)
                return CopyPath(candidate, destination, capacity);

            const int pipelineLength = std::snprintf(candidate, sizeof(candidate), "engine/rendering/%s.vppl", feature.name);
            if (pipelineLength <= 0 || static_cast<vanguard::usize>(pipelineLength) >= sizeof(candidate))
                return false;
            if (packages::HashResourcePath(candidate) == id)
                return CopyPath(candidate, destination, capacity);
        }
        return false;
    }

    [[nodiscard]] bool ReadResourceBytes(const packages::PackageReader& package, filesystem::IFile& file,
                                         const packages::Resource& resource, ByteArray& bytes) noexcept
    {
        if (resource.logicalSize > std::numeric_limits<vanguard::u32>::max())
            return false;
        vanguard::u64 maximumStoredSize = 0;
        for (const packages::Segment& segment : package.GetSegments(resource))
        {
            if (segment.storedSize > maximumStoredSize)
                maximumStoredSize = segment.storedSize;
        }
        if (maximumStoredSize > std::numeric_limits<vanguard::u32>::max())
            return false;

        bytes.Resize(static_cast<vanguard::u32>(resource.logicalSize));
        ByteArray scratch(vanguard::memory::pools::Tools::GetInstance());
        scratch.Resize(static_cast<vanguard::u32>(maximumStoredSize));
        return package.ReadResource(file, resource, bytes.Data(), bytes.Size(), scratch.Data(), scratch.Size()) == packages::Result::Success;
    }

    [[nodiscard]] bool ReadCamera(const prefabs::PrefabFile& prefab, const prefabs::ComponentRecord& component,
                                  CameraData& camera) noexcept
    {
        if (component.schema != CameraComponentType || component.schemaVersion != CameraSchema.currentVersion)
            return false;
        const containers::ArraySpan<const vanguard::u8> componentData = prefab.GetComponentData(component);
        if (componentData.Empty() || componentData.Count() > std::numeric_limits<vanguard::u32>::max())
            return false;
        ByteArray bytes(vanguard::memory::pools::Tools::GetInstance());
        bytes.Resize(componentData.Count());
        std::memcpy(bytes.Data(), componentData.Data(), componentData.Count());
        filesystem::MemoryFileReader file(bytes, 0);
        serialization::BinaryReader reader(file);
        schemas::ReadInfo readInfo;
        return schemas::ReadObject(reader, CameraSchema, &camera, {}, &readInfo) == schemas::Result::Success &&
               readInfo.sourceSchemaVersion == component.schemaVersion;
    }

    [[nodiscard]] bool EqualCamera(const CameraData& left, const CameraData& right) noexcept
    {
        return left.verticalFieldOfViewRadians == right.verticalFieldOfViewRadians && left.nearPlane == right.nearPlane &&
               left.farPlane == right.farPlane && left.layerMask == right.layerMask && left.visibilityMask == right.visibilityMask;
    }

    [[nodiscard]] bool CookCameraOnlyPrefab(const prefabs::PrefabFile& source, const prefabs::EntityRecord& entity,
                                            const prefabs::ComponentRecord& component, const CameraData& camera,
                                            ByteArray& cooked) noexcept
    {
        prefabs::EntityBuildRecord outputEntity;
        outputEntity.stableId = entity.stableId;
        outputEntity.parentStableId = entity.parentStableId;
        outputEntity.name = entity.name;
        outputEntity.flags = entity.flags;

        prefabs::ComponentBuildRecord outputCamera;
        outputCamera.stableId = component.stableId;
        outputCamera.entityStableId = component.entityStableId;
        outputCamera.schema = &CameraSchema;
        outputCamera.object = &camera;
        outputCamera.flags = component.flags;

        crypto::Sha256Builder fingerprintBuilder;
        crypto::Digest256 sourceFingerprint;
        const crypto::Digest256& originalFingerprint = source.GetSourceFingerprint();
        if (!fingerprintBuilder.Update(originalFingerprint.bytes, crypto::Digest256::ByteCount) ||
            !fingerprintBuilder.Update(FixtureFingerprintTag, sizeof(FixtureFingerprintTag) - 1u) ||
            !fingerprintBuilder.Finalize(sourceFingerprint))
        {
            return false;
        }

        prefabs::CookDescription description;
        description.name = source.GetName();
        description.entities = {&outputEntity, 1};
        description.components = {&outputCamera, 1};
        description.sourceFingerprint = sourceFingerprint;
        filesystem::MemoryFileWriter writer(cooked);
        return prefabs::CookPrefab(description, writer) == prefabs::Result::Success;
    }

    [[nodiscard]] bool DependenciesEqual(const containers::ArraySpan<const packages::Dependency> left,
                                         const containers::ArraySpan<const packages::Dependency> right) noexcept
    {
        if (left.Count() != right.Count())
            return false;
        for (vanguard::u32 index = 0; index < left.Count(); ++index)
        {
            if (left[index].id != right[index].id || left[index].type != right[index].type || left[index].kind != right[index].kind)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool PackageSetBootstrapEqual(const packages::PackageSet& left, const packages::PackageSet& right) noexcept
    {
        return left.gameId == right.gameId && left.targetPlatformId == right.targetPlatformId &&
               left.startupWorld == right.startupWorld && left.startupWorldType == right.startupWorldType &&
               left.defaultInput == right.defaultInput && left.defaultInputType == right.defaultInputType &&
               left.rendererBootstrap == right.rendererBootstrap && left.rendererBootstrapType == right.rendererBootstrapType &&
               left.packages.Empty() && right.packages.Empty();
    }

    [[nodiscard]] bool ValidateOutput(filesystem::Manager& files, const filesystem::AbsolutePath& outputPath,
                                      const packages::PackageReader& sourcePackage, const world::WorldCameraLocator& sourceLocator,
                                      const prefabs::EntityRecord& sourceEntity, const prefabs::ComponentRecord& sourceCamera,
                                      const CameraData& sourceCameraData, const vanguard::u64 expectedBuildId) noexcept
    {
        auto file = files.CreateFileReader(outputPath, filesystem::FOF_Buffered);
        packages::PackageReader output;
        if (!file || output.Open(*file) != packages::Result::Success || !output.HasPackageSet() || output.GetPackageSet() == nullptr ||
            output.GetHeader().packageId != sourcePackage.GetHeader().packageId || output.GetHeader().buildId != expectedBuildId ||
            output.GetResources().Count() != sourcePackage.GetResources().Count() ||
            !PackageSetBootstrapEqual(*sourcePackage.GetPackageSet(), *output.GetPackageSet()))
        {
            return false;
        }

        const auto sourceResources = sourcePackage.GetResources();
        const auto outputResources = output.GetResources();
        for (vanguard::u32 index = 0; index < sourceResources.Count(); ++index)
        {
            const packages::Resource& source = sourceResources[index];
            const packages::Resource& derived = outputResources[index];
            if (source.id != derived.id || source.type != derived.type || source.flags != derived.flags ||
                !DependenciesEqual(sourcePackage.GetDependencies(source), output.GetDependencies(derived)))
            {
                return false;
            }
            const bool replacedPrefab = source.id == packages::HashResourcePath(PrefabPath);
            const auto sourceSegments = sourcePackage.GetSegments(source);
            const auto outputSegments = output.GetSegments(derived);
            if (sourceSegments.Count() != outputSegments.Count())
                return false;
            for (vanguard::u32 segmentIndex = 0; segmentIndex < sourceSegments.Count(); ++segmentIndex)
            {
                if (sourceSegments[segmentIndex].alignmentLog2 != outputSegments[segmentIndex].alignmentLog2 ||
                    sourceSegments[segmentIndex].flags != outputSegments[segmentIndex].flags ||
                    (!replacedPrefab && (sourceSegments[segmentIndex].logicalSize != outputSegments[segmentIndex].logicalSize ||
                                         sourceSegments[segmentIndex].codec != outputSegments[segmentIndex].codec)))
                {
                    return false;
                }
            }
            if (!replacedPrefab && source.contentCrc64 != derived.contentCrc64)
                return false;
        }

        const packages::Resource* const worldResource = output.Find(packages::HashResourcePath(WorldPath));
        const packages::Resource* const prefabResource = output.Find(packages::HashResourcePath(PrefabPath));
        if (worldResource == nullptr || prefabResource == nullptr)
            return false;

        ByteArray worldBytes(vanguard::memory::pools::Tools::GetInstance());
        ByteArray prefabBytes(vanguard::memory::pools::Tools::GetInstance());
        if (!ReadResourceBytes(output, *file, *worldResource, worldBytes) || !ReadResourceBytes(output, *file, *prefabResource, prefabBytes))
            return false;

        filesystem::MemoryFileReader worldFile(worldBytes, 0);
        world::WorldFile derivedWorld;
        if (derivedWorld.Open(worldFile) != world::Result::Success || !derivedWorld.HasStartupCamera())
            return false;
        const world::WorldCameraLocator& locator = derivedWorld.GetStartupCamera();
        if (locator.cellId != sourceLocator.cellId || locator.entityId != sourceLocator.entityId ||
            locator.componentStableId != sourceLocator.componentStableId)
        {
            return false;
        }

        filesystem::MemoryFileReader prefabFile(prefabBytes, 0);
        prefabs::PrefabFile derivedPrefab;
        if (derivedPrefab.Open(prefabFile) != prefabs::Result::Success || derivedPrefab.GetEntities().Count() != 1 ||
            derivedPrefab.GetComponents().Count() != 1)
        {
            return false;
        }
        const prefabs::EntityRecord& entity = derivedPrefab.GetEntities()[0];
        const prefabs::ComponentRecord& component = derivedPrefab.GetComponents()[0];
        if (entity.stableId != sourceEntity.stableId || entity.parentStableId != sourceEntity.parentStableId ||
            entity.name != sourceEntity.name || entity.flags != sourceEntity.flags || component.stableId != sourceCamera.stableId ||
            component.entityStableId != sourceCamera.entityStableId || component.flags != sourceCamera.flags ||
            component.schema != CameraComponentType)
        {
            return false;
        }
        CameraData derivedCamera;
        return ReadCamera(derivedPrefab, component, derivedCamera) && EqualCamera(sourceCameraData, derivedCamera);
    }

    [[nodiscard]] bool BuildFixture(const char* const baseDirectoryArgument, const char* const outputDirectoryArgument) noexcept
    {
        if (baseDirectoryArgument == nullptr || baseDirectoryArgument[0] == '\0' || outputDirectoryArgument == nullptr ||
            outputDirectoryArgument[0] == '\0')
        {
            return false;
        }
        const filesystem::AbsolutePath baseDirectory = filesystem::AbsolutePath::CreateDirPath(baseDirectoryArgument);
        const filesystem::AbsolutePath outputDirectory = filesystem::AbsolutePath::CreateDirPath(outputDirectoryArgument);
        if (baseDirectory.Empty() || outputDirectory.Empty() || baseDirectory == outputDirectory ||
            !filesystem::Initialize({baseDirectory, baseDirectory, baseDirectory}))
        {
            return false;
        }

        bool succeeded = false;
        filesystem::Manager& files = filesystem::GetManager();
        const filesystem::AbsolutePath sourcePath = baseDirectory.AddFilePath("DATA000.vpak");
        const filesystem::AbsolutePath outputPath = outputDirectory.AddFilePath("DATA000.vpak");
        const filesystem::AbsolutePath temporaryOutputPath = outputDirectory.AddFilePath("DATA000.vpak.tmp");
        do
        {
            if (!files.FileExist(sourcePath) || files.FileExist(outputPath) || !files.CreatePath(outputDirectory) ||
                (files.FileExist(temporaryOutputPath) && !files.DeleteFile(temporaryOutputPath)))
                break;
            auto sourceFile = files.CreateFileReader(sourcePath, filesystem::FOF_Buffered);
            if (!sourceFile)
                break;

            packages::PackageReader sourcePackage;
            if (sourcePackage.Open(*sourceFile) != packages::Result::Success || !sourcePackage.HasPackageSet() ||
                sourcePackage.GetPackageSet() == nullptr || sourcePackage.GetPackageSet()->version != serialization::Version{1, 2} ||
                !sourcePackage.GetPackageSet()->packages.Empty() ||
                sourcePackage.GetResources().Count() != StandardBootstrapResourceCount)
            {
                break;
            }

            for (const packages::Resource& resource : sourcePackage.GetResources())
            {
                char path[256]{};
                if (!ResolveBootstrapPath(resource.id, path, sizeof(path)))
                {
                    std::fprintf(stderr, "editorSceneOutputPackageFixture: unknown bootstrap resource id=%llu\n",
                                 static_cast<unsigned long long>(resource.id));
                    break;
                }
            }
            bool allPathsResolved = true;
            for (const packages::Resource& resource : sourcePackage.GetResources())
            {
                char path[256]{};
                if (!ResolveBootstrapPath(resource.id, path, sizeof(path)))
                {
                    allPathsResolved = false;
                    break;
                }
            }
            if (!allPathsResolved)
                break;

            const packages::Resource* const sourceWorldResource = sourcePackage.Find(packages::HashResourcePath(WorldPath));
            const packages::Resource* const sourcePrefabResource = sourcePackage.Find(packages::HashResourcePath(PrefabPath));
            if (sourceWorldResource == nullptr || sourcePrefabResource == nullptr || sourcePackage.GetSegments(*sourcePrefabResource).Count() != 1)
                break;

            ByteArray worldBytes(vanguard::memory::pools::Tools::GetInstance());
            ByteArray sourcePrefabBytes(vanguard::memory::pools::Tools::GetInstance());
            if (!ReadResourceBytes(sourcePackage, *sourceFile, *sourceWorldResource, worldBytes) ||
                !ReadResourceBytes(sourcePackage, *sourceFile, *sourcePrefabResource, sourcePrefabBytes))
            {
                break;
            }

            filesystem::MemoryFileReader worldFile(worldBytes, 0);
            world::WorldFile sourceWorld;
            if (sourceWorld.Open(worldFile) != world::Result::Success || !sourceWorld.HasStartupCamera())
                break;
            const world::WorldCameraLocator locator = sourceWorld.GetStartupCamera();

            filesystem::MemoryFileReader prefabFile(sourcePrefabBytes, 0);
            prefabs::PrefabFile sourcePrefab;
            if (sourcePrefab.Open(prefabFile) != prefabs::Result::Success)
                break;
            const prefabs::EntityRecord* const sourceEntity = sourcePrefab.FindEntity(locator.entityId);
            const prefabs::ComponentRecord* const sourceCamera = sourcePrefab.FindComponent(locator.componentStableId);
            if (sourceEntity == nullptr || sourceCamera == nullptr || sourceCamera->entityStableId != sourceEntity->stableId ||
                sourceEntity->parentStableId != prefabs::InvalidStableId || sourceCamera->schema != CameraComponentType)
            {
                break;
            }

            CameraData camera;
            if (!ReadCamera(sourcePrefab, *sourceCamera, camera))
                break;
            ByteArray cameraOnlyPrefab(vanguard::memory::pools::Tools::GetInstance());
            if (!CookCameraOnlyPrefab(sourcePrefab, *sourceEntity, *sourceCamera, camera, cameraOnlyPrefab))
                break;

            vanguard::u64 fixtureBuildId = serialization::Crc64(&sourcePackage.GetHeader().buildId, sizeof(sourcePackage.GetHeader().buildId));
            fixtureBuildId = serialization::Crc64(cameraOnlyPrefab.Data(), cameraOnlyPrefab.Size(), fixtureBuildId);
            if (fixtureBuildId == 0 || fixtureBuildId == sourcePackage.GetHeader().buildId)
                fixtureBuildId ^= 0x45445343454e4555ull;

            packages::BuildOptions options;
            options.flags = static_cast<packages::PackageFlags>(
                static_cast<vanguard::u32>(sourcePackage.GetHeader().flags) & ~static_cast<vanguard::u32>(packages::PackageFlags::HasPackageSet));
            options.packageId = sourcePackage.GetHeader().packageId;
            options.buildId = fixtureBuildId;
            options.dataAlignmentLog2 = 12;

            const packages::PackageSet& sourceSet = *sourcePackage.GetPackageSet();
            packages::PackageSetBuild packageSet;
            packageSet.gameId = sourceSet.gameId;
            packageSet.targetPlatformId = sourceSet.targetPlatformId;
            packageSet.startupWorld = sourceSet.startupWorld;
            packageSet.startupWorldType = sourceSet.startupWorldType;
            packageSet.defaultInput = sourceSet.defaultInput;
            packageSet.defaultInputType = sourceSet.defaultInputType;
            packageSet.rendererBootstrap = sourceSet.rendererBootstrap;
            packageSet.rendererBootstrapType = sourceSet.rendererBootstrapType;

            auto outputFile = files.CreateFileWriter(temporaryOutputPath, filesystem::FOF_Buffered);
            packages::PackageWriter writer;
            if (!outputFile || writer.Begin(*outputFile, options, packageSet) != packages::Result::Success)
                break;

            bool addedAll = true;
            for (const packages::Resource& resource : sourcePackage.GetResources())
            {
                char path[256]{};
                if (!ResolveBootstrapPath(resource.id, path, sizeof(path)))
                {
                    addedAll = false;
                    break;
                }
                const auto sourceSegments = sourcePackage.GetSegments(resource);
                containers::DynamicArray<packages::BuildSegment> buildSegments(vanguard::memory::pools::Tools::GetInstance());
                ByteArray resourceBytes(vanguard::memory::pools::Tools::GetInstance());
                const bool replacePrefab = resource.id == packages::HashResourcePath(PrefabPath);
                if (replacePrefab)
                {
                    const packages::Segment& sourceSegment = sourceSegments[0];
                    buildSegments.PushBack({cameraOnlyPrefab.Data(), cameraOnlyPrefab.Size(), sourceSegment.codec,
                                            sourceSegment.alignmentLog2, sourceSegment.flags});
                }
                else
                {
                    if (!ReadResourceBytes(sourcePackage, *sourceFile, resource, resourceBytes))
                    {
                        addedAll = false;
                        break;
                    }
                    vanguard::usize logicalOffset = 0;
                    for (const packages::Segment& segment : sourceSegments)
                    {
                        buildSegments.PushBack({resourceBytes.TypedData() + logicalOffset, static_cast<vanguard::usize>(segment.logicalSize),
                                                segment.codec, segment.alignmentLog2, segment.flags});
                        logicalOffset += static_cast<vanguard::usize>(segment.logicalSize);
                    }
                }

                packages::BuildResource build;
                build.path = path;
                build.type = resource.type;
                build.flags = resource.flags;
                build.segments = buildSegments;
                build.dependencies = sourcePackage.GetDependencies(resource);
                if (writer.Add(build) != packages::Result::Success)
                {
                    addedAll = false;
                    break;
                }
            }
            if (!addedAll || writer.Finalize() != packages::Result::Success)
                break;
            outputFile.Reset();

            if (!ValidateOutput(files, temporaryOutputPath, sourcePackage, locator, *sourceEntity, *sourceCamera, camera, fixtureBuildId) ||
                !files.MoveFile(temporaryOutputPath, outputPath))
                break;

            std::printf("editorSceneOutputPackageFixture: PASS resources=%u sourceBuild=%llu fixtureBuild=%llu\n",
                        sourcePackage.GetResources().Count(), static_cast<unsigned long long>(sourcePackage.GetHeader().buildId),
                        static_cast<unsigned long long>(fixtureBuildId));
            succeeded = true;
        } while (false);

        if (files.FileExist(temporaryOutputPath))
            static_cast<void>(files.DeleteFile(temporaryOutputPath));
        filesystem::Shutdown();
        return succeeded;
    }
} // namespace

int main(const int argumentCount, const char* const* const arguments)
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace memory = vanguard::memory;

    if (!memory::Initialize())
        return 1;
    const bool diagnosticsInitialized = diagnostics::Initialize(diagnostics::Mode::Synchronous, "editorSceneOutputPackageFixture");
    const bool containersInitialized = diagnosticsInitialized && containers::Initialize();
    const bool ioInitialized = containersInitialized && vanguard::io::Initialize();
    const bool succeeded = ioInitialized && argumentCount == 3 && BuildFixture(arguments[1], arguments[2]);
    if (!succeeded && diagnosticsInitialized)
        static_cast<void>(Fail("fixture generation"));
    if (ioInitialized)
        vanguard::io::Shutdown();
    if (diagnosticsInitialized)
        diagnostics::Shutdown();
    return succeeded ? 0 : 1;
}
