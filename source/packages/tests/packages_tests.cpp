#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/packages/packages.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[packagesTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    constexpr vanguard::packages::ResourceTypeId MeshType = vanguard::serialization::MakeFourCC('M', 'E', 'S', 'H');
    constexpr vanguard::packages::ResourceTypeId TextureType = vanguard::serialization::MakeFourCC('T', 'E', 'X', 'R');
    constexpr vanguard::packages::ResourceTypeId WorldType = vanguard::serialization::MakeFourCC('W', 'R', 'L', 'D');

    constexpr auto MakeCompressiblePayload()
    {
        std::array<vanguard::u8, 8192> bytes{};
        for (vanguard::usize index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<vanguard::u8>((index / 64u) & 7u);
        }
        return bytes;
    }

    constexpr auto g_compressible = MakeCompressiblePayload();
    constexpr std::array<vanguard::u8, 13> g_tail{0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x11, 0x22, 0x33, 0x44, 0x55};
    constexpr std::array<vanguard::u8, 11> g_texture{9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 42};
    constexpr std::array<vanguard::u8, 8> g_override{7, 7, 7, 7, 1, 2, 3, 4};

    vanguard::u32 LoadU32(const ByteArray& bytes, const vanguard::u32 offset)
    {
        return static_cast<vanguard::u32>(bytes[offset]) | (static_cast<vanguard::u32>(bytes[offset + 1]) << 8u) |
               (static_cast<vanguard::u32>(bytes[offset + 2]) << 16u) | (static_cast<vanguard::u32>(bytes[offset + 3]) << 24u);
    }

    vanguard::u64 LoadU64(const ByteArray& bytes, const vanguard::u32 offset)
    {
        vanguard::u64 value = 0;
        for (vanguard::u32 index = 0; index < 8; ++index)
        {
            value |= static_cast<vanguard::u64>(bytes[offset + index]) << (index * 8u);
        }
        return value;
    }

    void StoreU32(ByteArray& bytes, const vanguard::u32 offset, const vanguard::u32 value)
    {
        for (vanguard::u32 index = 0; index < 4; ++index)
        {
            bytes[offset + index] = static_cast<vanguard::u8>(value >> (index * 8u));
        }
    }

    void StoreU64(ByteArray& bytes, const vanguard::u32 offset, const vanguard::u64 value)
    {
        for (vanguard::u32 index = 0; index < 8; ++index)
        {
            bytes[offset + index] = static_cast<vanguard::u8>(value >> (index * 8u));
        }
    }

    bool BuildBasePackage(ByteArray& output)
    {
        namespace packages = vanguard::packages;
        namespace filesystem = vanguard::filesystem;

        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x1001;
        options.buildId = 0x20260729;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }

        const packages::ResourceId meshId = packages::HashResourcePath("meshes/hero.vmesh");
        const packages::ResourceId textureId = packages::HashResourcePath("textures/hero.vtex");
        const std::array<packages::BuildSegment, 2> meshSegments{
            {{g_compressible.data(), g_compressible.size(), packages::Codec::Lz4, 12, packages::SegmentFlags::Streamable},
             {g_tail.data(), g_tail.size(), packages::Codec::None, 12, packages::SegmentFlags::Inline}}};
        const std::array<packages::BuildSegment, 1> textureSegments{
            {{g_texture.data(), g_texture.size(), packages::Codec::None, 12, packages::SegmentFlags::MemoryResident}}};
        const std::array<packages::Dependency, 1> meshDependencies{
            {{textureId, TextureType, vanguard::resources::DependencyKind::Optional}}};

        packages::BuildResource mesh;
        mesh.path = "Meshes\\Hero.vmesh";
        mesh.type = MeshType;
        mesh.flags = packages::ResourceFlags::Streamable;
        mesh.segments = {meshSegments.data(), static_cast<vanguard::u32>(meshSegments.size())};
        mesh.dependencies = {meshDependencies.data(), static_cast<vanguard::u32>(meshDependencies.size())};

        packages::BuildResource texture;
        texture.path = "textures/hero.vtex";
        texture.type = TextureType;
        texture.flags = packages::ResourceFlags::Startup;
        texture.segments = {textureSegments.data(), static_cast<vanguard::u32>(textureSegments.size())};

        const packages::BuildResource* ordered[2]{&mesh, &texture};
        if (meshId > textureId)
        {
            std::swap(ordered[0], ordered[1]);
        }
        return writer.Add(*ordered[0]) == packages::Result::Success && writer.Add(*ordered[1]) == packages::Result::Success &&
               writer.Finalize() == packages::Result::Success;
    }

    bool BuildOverridePackage(ByteArray& output)
    {
        namespace packages = vanguard::packages;
        namespace filesystem = vanguard::filesystem;

        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x1002;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }

        const std::array<packages::BuildSegment, 1> segments{
            {{g_override.data(), g_override.size(), packages::Codec::None, 12, packages::SegmentFlags::MemoryResident}}};
        packages::BuildResource resource;
        resource.path = "textures/hero.vtex";
        resource.type = TextureType;
        resource.segments = {segments.data(), static_cast<vanguard::u32>(segments.size())};
        return writer.Add(resource) == packages::Result::Success && writer.Finalize() == packages::Result::Success;
    }

    bool BuildDeduplicatedPackage(ByteArray& output, vanguard::u32& deduplicatedSegments, vanguard::u64& storedBytes)
    {
        namespace packages = vanguard::packages;
        namespace filesystem = vanguard::filesystem;

        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x1003;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }

        const std::array<packages::BuildSegment, 1> segments{
            {{g_texture.data(), g_texture.size(), packages::Codec::None, 12, packages::SegmentFlags::MemoryResident}}};
        packages::BuildResource first;
        first.path = "textures/shared_a.vtex";
        first.type = TextureType;
        first.segments = {segments.data(), static_cast<vanguard::u32>(segments.size())};
        packages::BuildResource second;
        second.path = "textures/shared_b.vtex";
        second.type = TextureType;
        second.segments = {segments.data(), static_cast<vanguard::u32>(segments.size())};

        const packages::BuildResource* ordered[2]{&first, &second};
        if (packages::HashResourcePath(first.path) > packages::HashResourcePath(second.path))
        {
            std::swap(ordered[0], ordered[1]);
        }
        if (writer.Add(*ordered[0]) != packages::Result::Success || writer.Add(*ordered[1]) != packages::Result::Success ||
            writer.Finalize() != packages::Result::Success)
        {
            return false;
        }
        deduplicatedSegments = writer.DeduplicatedSegmentCount();
        storedBytes = writer.StoredPayloadBytes();
        return true;
    }

    bool BuildRootPackage(ByteArray& output)
    {
        namespace filesystem = vanguard::filesystem;
        namespace packages = vanguard::packages;

        std::array<packages::PackageSetEntry, 2> packageEntries{};
        packageEntries[0].packageNumber = 1;
        packageEntries[0].flags = packages::PackageSetEntryFlags::Required;
        packageEntries[0].packageId = 0x2001;
        packageEntries[0].buildId = 0x20260804;
        packageEntries[0].fileSize = 128ull * 1024ull * 1024ull;
        packageEntries[0].indexCrc64 = 0x1122334455667788ull;
        packageEntries[1].packageNumber = 2;
        packageEntries[1].flags = packages::PackageSetEntryFlags::Optional | packages::PackageSetEntryFlags::Override;
        packageEntries[1].mountPriority = 100;
        packageEntries[1].packageId = 0x2002;
        packageEntries[1].buildId = 0x20260804;
        packageEntries[1].fileSize = 64ull * 1024ull * 1024ull;
        packageEntries[1].indexCrc64 = 0x8877665544332211ull;
        for (vanguard::u32 byte = 0; byte < sizeof(packageEntries[0].contentDigest); ++byte)
        {
            packageEntries[0].contentDigest[byte] = static_cast<vanguard::u8>(byte + 1u);
            packageEntries[1].contentDigest[byte] = static_cast<vanguard::u8>(0xffu - byte);
        }

        packages::PackageSetBuild packageSet;
        packageSet.gameId = 0x56414e4755415244ull;
        packageSet.targetPlatformId = vanguard::serialization::MakeFourCC('W', 'I', 'N', '6');
        packageSet.startupWorld = packages::HashResourcePath("worlds/main.vworld");
        packageSet.startupWorldType = WorldType;
        packageSet.defaultInput = packages::HashResourcePath("input/default.vinput");
        packageSet.defaultInputType = TextureType;
        packageSet.packages = {packageEntries.data(), static_cast<vanguard::u32>(packageEntries.size())};

        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x2000;
        options.buildId = 0x20260804;
        options.dataAlignmentLog2 = 4;
        if (writer.Begin(file, options, packageSet) != packages::Result::Success)
        {
            return false;
        }

        const std::array<packages::BuildSegment, 1> segments{
            {{g_texture.data(), g_texture.size(), packages::Codec::None, 4, packages::SegmentFlags::MemoryResident}}};
        packages::BuildResource world;
        world.path = "worlds/main.vworld";
        world.type = WorldType;
        world.flags = packages::ResourceFlags::Startup;
        world.segments = {segments.data(), static_cast<vanguard::u32>(segments.size())};
        return writer.Add(world) == packages::Result::Success && writer.Finalize() == packages::Result::Success;
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;
    namespace packages = vanguard::packages;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "packagesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    ByteArray packageBytes(memory::pools::Resources::GetInstance());
    Check(BuildBasePackage(packageBytes), "build base package");
    Check(packageBytes.Size() > 8192, "package contains aligned data");

    filesystem::MemoryFileReader packageFile(packageBytes, 0);
    packages::PackageReader package;
    Check(package.Open(packageFile) == packages::Result::Success, "open package");
    Check(package.IsOpen(), "open state");
    Check(package.Header().resourceCount == 2, "resource count");
    Check(package.Header().segmentCount == 3, "segment count");
    Check(package.Header().dependencyCount == 1, "dependency count");
    Check(!package.HasPackageSet() && package.GetPackageSet() == nullptr, "ordinary VPAK has no package-set boot record");

    packages::PackageSet absentPackageSet;
    packageFile.Seek(0);
    Check(packages::ReadPackageSet(packageFile, absentPackageSet) == packages::Result::MissingPackageSet &&
              !absentPackageSet.IsValid(),
          "ordinary VPAK rejects package-set bootstrap discovery explicitly");

    char packageFileName[13]{};
    vanguard::usize packageFileNameSize = 0;
    Check(packages::FormatPackageFileName(0, packageFileName, sizeof(packageFileName), packageFileNameSize) ==
                  packages::Result::Success &&
              packageFileNameSize == 12 && std::memcmp(packageFileName, "DATA000.vpak", 13) == 0,
          "canonical root package filename");
    Check(packages::FormatPackageFileName(999, packageFileName, sizeof(packageFileName), packageFileNameSize) ==
                  packages::Result::Success &&
              std::memcmp(packageFileName, "DATA999.vpak", 13) == 0,
          "canonical maximum package filename");
    Check(packages::FormatPackageFileName(1000, packageFileName, sizeof(packageFileName), packageFileNameSize) ==
              packages::Result::InvalidArgument,
          "reject out-of-range package filename");

    ByteArray rootPackageBytes(memory::pools::Resources::GetInstance());
    ByteArray repeatedRootPackageBytes(memory::pools::Resources::GetInstance());
    Check(BuildRootPackage(rootPackageBytes) && BuildRootPackage(repeatedRootPackageBytes), "build DATA000 root package");
    Check(rootPackageBytes == repeatedRootPackageBytes, "deterministic DATA000 boot record and package bytes");

    filesystem::MemoryFileReader rootBootstrapFile(rootPackageBytes, 0);
    packages::PackageSet packageSet;
    Check(packages::ReadPackageSet(rootBootstrapFile, packageSet) == packages::Result::Success && packageSet.IsValid(),
          "read DATA000 boot record without mounting or opening the package index");
    Check(packageSet.gameId == 0x56414e4755415244ull && packageSet.buildId == 0x20260804 &&
              packageSet.targetPlatformId == vanguard::serialization::MakeFourCC('W', 'I', 'N', '6') &&
              packageSet.startupWorld == packages::HashResourcePath("worlds/main.vworld") &&
              packageSet.startupWorldType == WorldType &&
              packageSet.defaultInput == packages::HashResourcePath("input/default.vinput") &&
              packageSet.defaultInputType == TextureType && packageSet.packages.Size() == 2,
          "DATA000 game identity and startup resources");
    Check(packageSet.packages.Size() == 2 && packageSet.packages[0].packageNumber == 1 &&
              packageSet.packages[0].flags == packages::PackageSetEntryFlags::Required &&
              packageSet.packages[1].packageNumber == 2 && packageSet.packages[1].mountPriority == 100,
          "ordered package catalog and mount policy");

    filesystem::MemoryFileReader rootPackageFile(rootPackageBytes, 0);
    packages::PackageReader rootPackage;
    Check(rootPackage.Open(rootPackageFile) == packages::Result::Success && rootPackage.HasPackageSet() &&
              rootPackage.GetPackageSet() != nullptr && rootPackage.GetPackageSet()->IsValid(),
          "ordinary PackageReader validates and exposes DATA000 boot data");

    {
        packages::PackageSetReadLimits limits;
        limits.maximumPackages = 1;
        filesystem::MemoryFileReader limitedRootFile(rootPackageBytes, 0);
        packages::PackageSet limitedPackageSet;
        Check(packages::ReadPackageSet(limitedRootFile, limitedPackageSet, {}, limits) == packages::Result::LimitExceeded,
              "package-set catalog count limit");
    }

    {
        ByteArray corruptBootHeader(rootPackageBytes);
        corruptBootHeader[104] ^= 1u;
        filesystem::MemoryFileReader corruptBootFile(corruptBootHeader, 0);
        packages::PackageSet corruptPackageSet;
        Check(packages::ReadPackageSet(corruptBootFile, corruptPackageSet) == packages::Result::IntegrityFailure,
              "package-set header corruption");
    }

    {
        ByteArray unsupportedBootVersion(rootPackageBytes);
        unsupportedBootVersion[104] = 2;
        unsupportedBootVersion[105] = 0;
        StoreU32(unsupportedBootVersion, 96u + 88u,
                 vanguard::serialization::Crc32(unsupportedBootVersion.TypedData() + 96u, 88));
        filesystem::MemoryFileReader unsupportedBootFile(unsupportedBootVersion, 0);
        packages::PackageSet unsupportedPackageSet;
        Check(packages::ReadPackageSet(unsupportedBootFile, unsupportedPackageSet) == packages::Result::UnsupportedVersion,
              "package-set compatibility rejection");
    }

    {
        ByteArray corruptCatalog(rootPackageBytes);
        corruptCatalog[96u + 96u + 16u] ^= 1u;
        filesystem::MemoryFileReader corruptCatalogFile(corruptCatalog, 0);
        packages::PackageSet corruptPackageSet;
        Check(packages::ReadPackageSet(corruptCatalogFile, corruptPackageSet) == packages::Result::IntegrityFailure,
              "package-set catalog corruption");
    }

    {
        ByteArray malformedCatalog(rootPackageBytes);
        constexpr vanguard::u32 bootHeaderOffset = 96;
        constexpr vanguard::u32 entriesOffset = bootHeaderOffset + 96;
        StoreU32(malformedCatalog, entriesOffset + 80, 1);
        StoreU64(malformedCatalog, bootHeaderOffset + 76,
                 vanguard::serialization::Crc64(malformedCatalog.TypedData() + entriesOffset, 160));
        StoreU32(malformedCatalog, bootHeaderOffset + 88,
                 vanguard::serialization::Crc32(malformedCatalog.TypedData() + bootHeaderOffset, 88));
        filesystem::MemoryFileReader malformedCatalogFile(malformedCatalog, 0);
        packages::PackageSet malformedPackageSet;
        Check(packages::ReadPackageSet(malformedCatalogFile, malformedPackageSet) == packages::Result::InvalidLayout,
              "checksummed duplicate package number rejection");
    }

    {
        ByteArray mismatchedRootOffset(rootPackageBytes);
        StoreU64(mismatchedRootOffset, 80, 0);
        StoreU32(mismatchedRootOffset, 88, vanguard::serialization::Crc32(mismatchedRootOffset.TypedData(), 88));
        filesystem::MemoryFileReader mismatchedRootFile(mismatchedRootOffset, 0);
        packages::PackageSet mismatchedPackageSet;
        Check(packages::ReadPackageSet(mismatchedRootFile, mismatchedPackageSet) == packages::Result::InvalidLayout,
              "package-set flag and root offset consistency");
    }

    {
        ByteArray overlappingPayload(rootPackageBytes);
        const vanguard::u64 indexOffset64 = LoadU64(overlappingPayload, 24);
        const vanguard::u64 indexSize64 = LoadU64(overlappingPayload, 32);
        const vanguard::u32 resourceCount = LoadU32(overlappingPayload, 56);
        const vanguard::u32 segmentTable = static_cast<vanguard::u32>(indexOffset64) + 32u + resourceCount * 64u;
        StoreU64(overlappingPayload, segmentTable, 176u);
        StoreU64(overlappingPayload, 72,
                 vanguard::serialization::Crc64(overlappingPayload.TypedData() + indexOffset64,
                                                static_cast<vanguard::usize>(indexSize64)));
        StoreU32(overlappingPayload, 88, vanguard::serialization::Crc32(overlappingPayload.TypedData(), 88));
        filesystem::MemoryFileReader overlappingPayloadFile(overlappingPayload, 0);
        packages::PackageReader overlappingPackage;
        Check(overlappingPackage.Open(overlappingPayloadFile) == packages::Result::InvalidLayout,
              "resource payload cannot overlap DATA000 boot record");
    }

    const packages::Resource* mesh = package.Find("MESHES/hero.vmesh");
    const packages::Resource* texture = package.Find("textures\\hero.vtex");
    Check(mesh != nullptr, "normalized mesh lookup");
    Check(texture != nullptr, "normalized texture lookup");
    Check(package.Find("missing/resource") == nullptr, "missing lookup");
    if (mesh != nullptr)
    {
        Check(package.DebugPath(*mesh) == "meshes/hero.vmesh", "canonical debug path");
        const auto dependencies = package.Dependencies(*mesh);
        Check(dependencies.Count() == 1 && dependencies[0].id == texture->id && dependencies[0].type == TextureType &&
                  dependencies[0].kind == vanguard::resources::DependencyKind::Optional,
              "typed dependency range and requirement");

        std::array<vanguard::u8, g_compressible.size() + g_tail.size()> decoded{};
        std::array<vanguard::u8, g_compressible.size()> scratch{};
        packageFile.Seek(0);
        Check(package.ReadResource(packageFile, *mesh, decoded.data(), decoded.size(), scratch.data(), scratch.size()) ==
                  packages::Result::Success,
              "read segmented compressed resource");
        Check(std::memcmp(decoded.data(), g_compressible.data(), g_compressible.size()) == 0 &&
                  std::memcmp(decoded.data() + g_compressible.size(), g_tail.data(), g_tail.size()) == 0,
              "decoded resource contents");

        packages::ResourceFileReader logicalView;
        Check(logicalView.Open(package, *mesh, packageFile) == packages::Result::Success,
              "open lazy logical resource view");
        std::array<vanguard::u8, 8> range{};
        logicalView.Seek(static_cast<vanguard::i64>(g_compressible.size()));
        logicalView.Serialize(range.data(), 4);
        Check(logicalView.LastResult() == packages::Result::Success &&
                  logicalView.DecodedSegmentCount() == 1 &&
                  logicalView.StoredBytesRead() == package.Segments(*mesh)[1].storedSize &&
                  std::memcmp(range.data(), g_tail.data(), 4) == 0,
              "decode only the segment intersecting a logical range");
        logicalView.Seek(static_cast<vanguard::i64>(g_compressible.size() - 4));
        logicalView.Serialize(range.data(), range.size());
        Check(logicalView.LastResult() == packages::Result::Success &&
                  logicalView.DecodedSegmentCount() == 3 &&
                  std::memcmp(range.data(), g_compressible.data() + g_compressible.size() - 4, 4) == 0 &&
                  std::memcmp(range.data() + 4, g_tail.data(), 4) == 0,
              "read a logical range spanning independently decoded segments");

        const packages::Segment copiedSegment = package.Segments(*mesh)[0];
        Check(package.ReadSegment(packageFile, copiedSegment, decoded.data(), decoded.size(), scratch.data(), scratch.size()) ==
                  packages::Result::InvalidArgument,
              "foreign segment descriptor rejection");
    }

    ByteArray secondBuild(memory::pools::Resources::GetInstance());
    Check(BuildBasePackage(secondBuild), "repeat deterministic build");
    Check(secondBuild == packageBytes, "byte-identical deterministic build");

    ByteArray deduplicatedBytes(memory::pools::Resources::GetInstance());
    vanguard::u32 deduplicatedSegments = 0;
    vanguard::u64 storedPayloadBytes = 0;
    Check(BuildDeduplicatedPackage(deduplicatedBytes, deduplicatedSegments, storedPayloadBytes), "build deduplicated package");
    Check(deduplicatedSegments == 1 && storedPayloadBytes == g_texture.size(), "content-addressed payload deduplication telemetry");
    filesystem::MemoryFileReader deduplicatedFile(deduplicatedBytes, 0);
    packages::PackageReader deduplicatedPackage;
    Check(deduplicatedPackage.Open(deduplicatedFile) == packages::Result::Success, "open deduplicated package");
    if (deduplicatedPackage.Resources().Count() == 2)
    {
        const auto firstSegments = deduplicatedPackage.Segments(deduplicatedPackage.Resources()[0]);
        const auto secondSegments = deduplicatedPackage.Segments(deduplicatedPackage.Resources()[1]);
        Check(firstSegments.Count() == 1 && secondSegments.Count() == 1 && firstSegments[0].offset == secondSegments[0].offset,
              "exact shared physical segment range");
    }
    deduplicatedPackage.Close();

    {
        ByteArray falseAlias(deduplicatedBytes);
        const vanguard::u64 indexOffset64 = LoadU64(falseAlias, 24);
        const vanguard::u64 indexSize64 = LoadU64(falseAlias, 32);
        const vanguard::u32 resourceCount = LoadU32(falseAlias, 56);
        const vanguard::u32 secondSegment = static_cast<vanguard::u32>(indexOffset64) + 32u + resourceCount * 64u + 40u;
        StoreU64(falseAlias, secondSegment + 24u, LoadU64(falseAlias, secondSegment + 24u) ^ 1u);
        StoreU64(falseAlias, 72,
                 vanguard::serialization::Crc64(falseAlias.TypedData() + indexOffset64, static_cast<vanguard::usize>(indexSize64)));
        StoreU32(falseAlias, 88, vanguard::serialization::Crc32(falseAlias.TypedData(), 88));
        filesystem::MemoryFileReader falseAliasFile(falseAlias, 0);
        packages::PackageReader falseAliasPackage;
        Check(falseAliasPackage.Open(falseAliasFile) == packages::Result::InvalidLayout, "reject mismatched shared physical range");
    }

    ByteArray overrideBytes(memory::pools::Resources::GetInstance());
    Check(BuildOverridePackage(overrideBytes), "build override package");
    filesystem::MemoryFileReader overrideFile(overrideBytes, 0);
    packages::PackageReader overridePackage;
    Check(overridePackage.Open(overrideFile) == packages::Result::Success, "open override package");

    packages::MountTable mounts;
    Check(mounts.Mount(package, 0) == packages::Result::Success, "mount base");
    Check(mounts.Mount(overridePackage, 10) == packages::Result::Success, "mount override");
    const packages::ResolvedResource resolved = mounts.Resolve("textures/hero.vtex");
    Check(resolved && resolved.package == &overridePackage, "higher priority package wins");
    Check(mounts.Unmount(overridePackage), "unmount override");
    Check(mounts.Resolve("textures/hero.vtex").package == &package, "base visible after unmount");

    {
        packages::ReadLimits limits;
        limits.maximumResources = 1;
        filesystem::MemoryFileReader limitedFile(packageBytes, 0);
        packages::PackageReader limited;
        Check(limited.Open(limitedFile, limits) == packages::Result::LimitExceeded, "resource limit");
    }

    {
        ByteArray corrupt(packageBytes);
        corrupt[20] ^= 1u;
        filesystem::MemoryFileReader corruptFile(corrupt, 0);
        packages::PackageReader corruptReader;
        Check(corruptReader.Open(corruptFile) == packages::Result::IntegrityFailure, "header corruption");
    }

    {
        ByteArray corrupt(packageBytes);
        corrupt.Back() ^= 1u;
        filesystem::MemoryFileReader corruptFile(corrupt, 0);
        packages::PackageReader corruptReader;
        Check(corruptReader.Open(corruptFile) == packages::Result::IntegrityFailure, "index corruption");
    }

    {
        ByteArray malformed(packageBytes);
        const vanguard::u64 indexOffset64 = LoadU64(malformed, 24);
        const vanguard::u64 indexSize64 = LoadU64(malformed, 32);
        const vanguard::u32 resourceCount = LoadU32(malformed, 56);
        const vanguard::u32 indexOffset = static_cast<vanguard::u32>(indexOffset64);
        const vanguard::u32 segmentTable = indexOffset + 32u + resourceCount * 64u;
        StoreU64(malformed, segmentTable, 97);
        StoreU64(malformed, 72,
                 vanguard::serialization::Crc64(malformed.TypedData() + indexOffset, static_cast<vanguard::usize>(indexSize64)));
        StoreU32(malformed, 88, vanguard::serialization::Crc32(malformed.TypedData(), 88));

        filesystem::MemoryFileReader malformedFile(malformed, 0);
        packages::PackageReader malformedReader;
        Check(malformedReader.Open(malformedFile) == packages::Result::InvalidLayout, "checksummed malformed segment layout");
    }

    if (texture != nullptr)
    {
        ByteArray corrupt(packageBytes);
        const auto segments = package.Segments(*texture);
        corrupt[static_cast<vanguard::u32>(segments[0].offset)] ^= 1u;
        filesystem::MemoryFileReader corruptFile(corrupt, 0);
        std::array<vanguard::u8, g_texture.size()> output{};
        Check(package.ReadResource(corruptFile, *texture, output.data(), output.size()) == packages::Result::IntegrityFailure,
              "payload corruption");
    }

    {
        char canonical[64];
        vanguard::usize written = 0;
        Check(packages::CanonicalizeResourcePath("../escape", canonical, sizeof(canonical), written) == packages::Result::InvalidPath,
              "parent traversal rejection");
        Check(packages::HashResourcePath("C:/absolute") == packages::InvalidResourceId, "drive path rejection");
    }

    overridePackage.Close();
    package.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[packagesTests] Vanguard VPAK checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
