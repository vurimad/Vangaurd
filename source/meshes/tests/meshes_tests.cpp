#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/meshes/mesh_page_source.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/packages/packages.hpp>

#include <array>
#include <cstdio>
#include <utility>

namespace
{
    namespace meshes = vanguard::meshes;
    namespace resources = vanguard::resources;
    namespace serialization = vanguard::serialization;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    inline constexpr resources::ResourceTypeId TestMaterialType = serialization::MakeFourCC('V', 'M', 'A', 'T');

    class TestMaterialResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return TestMaterialType;
        }
    };

    void BeginTestMaterialLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        if (!registry.BeginLoading(request))
            return;
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(vanguard::memory::PoolId::Resources, sizeof(TestMaterialResource), alignof(TestMaterialResource));
        if (!block)
        {
            static_cast<void>(registry.Fail(request, resources::Failure::OutOfMemory));
            return;
        }
        auto* const material = ::new (block.address) TestMaterialResource();
        if (!registry.Publish(request, material))
        {
            material->~TestMaterialResource();
            vanguard::memory::Free(block);
        }
    }

    void DestroyTestMaterial(resources::ResourceObject* const object, void*) noexcept
    {
        if (object == nullptr)
            return;
        static_cast<TestMaterialResource*>(object)->~TestMaterialResource();
        vanguard::memory::MemoryBlock block{object, sizeof(TestMaterialResource), vanguard::memory::PoolId::Resources};
        vanguard::memory::Free(block);
    }

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[meshesTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    bool EqualBytes(const ByteArray& left, const ByteArray& right)
    {
        if (left.Size() != right.Size())
        {
            return false;
        }
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
        {
            if (left[index] != right[index])
            {
                return false;
            }
        }
        return true;
    }

    struct Fixture
    {
        std::array<vanguard::u8, 24> vertexPage0{};
        std::array<vanguard::u8, 24> vertexPage1{};
        std::array<vanguard::u8, 12> indexPage{};
        std::array<meshes::BufferBuildRecord, 2> buffers;
        std::array<meshes::PageBuildRecord, 3> pages;
        std::array<meshes::VertexLayoutBuildRecord, 1> layouts;
        std::array<meshes::VertexStreamBuildRecord, 1> streams;
        std::array<meshes::MaterialSlotBuildRecord, 1> materials;
        std::array<meshes::LodBuildRecord, 2> lods;
        std::array<meshes::SubmeshBuildRecord, 2> submeshes;
        meshes::BuildDescription description;

        Fixture()
        {
            for (vanguard::u32 index = 0; index < vertexPage0.size(); ++index)
            {
                vertexPage0[index] = static_cast<vanguard::u8>(index + 1);
                vertexPage1[index] = static_cast<vanguard::u8>(index + 31);
            }
            for (vanguard::u32 index = 0; index < indexPage.size(); ++index)
            {
                indexPage[index] = static_cast<vanguard::u8>(index + 61);
            }
            buffers = {{{20, meshes::BufferKind::Index, 2, 12}, {10, meshes::BufferKind::Vertex, 12, 48}}};
            pages = {{{20, 0, indexPage.data(), indexPage.size(), 4, meshes::PageFlags::RequiredForLowestLod},
                      {10, 24, vertexPage1.data(), vertexPage1.size(), 6, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload},
                      {10, 0, vertexPage0.data(), vertexPage0.size(), 4, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload}}};
            layouts = {{{1}}};
            streams = {{{1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12}}};
            materials = {{{100, 0xa11ce, resources::ResourceReference(resources::ResourcePath::FromString("materials/test.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T'))}}};
            lods = {{{1, 0.25f}, {0, 1.0f}}};

            meshes::Bounds submeshBounds;
            submeshBounds.minimum[0] = -1.0f;
            submeshBounds.minimum[1] = -1.0f;
            submeshBounds.minimum[2] = -1.0f;
            submeshBounds.maximum[0] = 1.0f;
            submeshBounds.maximum[1] = 1.0f;
            submeshBounds.maximum[2] = 1.0f;
            submeshBounds.sphereRadius = 1.75f;
            submeshes = {{{0x1000, 0xb0d1, 1, 100, 1, 20, meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::CastsShadow, 0, 3, 3, 3, submeshBounds},
                          {0x1000, 0xb0d1, 0, 100, 1, 20, meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList,
                           meshes::SubmeshFlags::CastsShadow | meshes::SubmeshFlags::RayTracing, 0, 3, 0, 3, submeshBounds}}};

            description.kind = meshes::MeshKind::Static;
            description.name = 0x6d657368;
            description.bounds = submeshBounds;
            description.positionQuantization.scale[0] = 2.0f;
            description.positionQuantization.scale[1] = 2.0f;
            description.positionQuantization.scale[2] = 2.0f;
            description.sourceFingerprint = vanguard::crypto::Sha256("source-mesh-and-settings", 24);
            description.buffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
            description.pages = {pages.data(), static_cast<vanguard::u32>(pages.size())};
            description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
            description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
            description.materialSlots = {materials.data(), static_cast<vanguard::u32>(materials.size())};
            description.lods = {lods.data(), static_cast<vanguard::u32>(lods.size())};
            description.submeshes = {submeshes.data(), static_cast<vanguard::u32>(submeshes.size())};
        }
    };

    meshes::Result WriteFixture(const meshes::BuildDescription& description, ByteArray& output)
    {
        output.Clear();
        vanguard::filesystem::MemoryFileWriter file(output);
        return meshes::WriteMesh(file, description);
    }

    bool WriteFile(const vanguard::filesystem::AbsolutePath& path, const ByteArray& bytes)
    {
        auto writer = vanguard::filesystem::GetManager().CreateFileWriter(path, vanguard::filesystem::FOF_Buffered);
        if (!writer)
        {
            return false;
        }
        writer->Serialize(const_cast<vanguard::u8*>(bytes.TypedData()), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }

    vanguard::u64 GetGeometryOffset(ByteArray& document)
    {
        vanguard::filesystem::MemoryFileReader reader(document, 0);
        serialization::BinaryReader binaryReader(reader);
        serialization::DocumentHeader header;
        if (serialization::ReadDocumentHeader(binaryReader, meshes::MeshMagic, {1, 0, 0}, {}, header) != serialization::Result::Success)
        {
            return 0;
        }
        vanguard::containers::DynamicArray<serialization::SectionDescriptor> sectionTable(vanguard::memory::pools::Serialization::GetInstance());
        if (serialization::ReadSectionTable(binaryReader, header, {}, sectionTable) != serialization::Result::Success)
        {
            return 0;
        }
        for (const serialization::SectionDescriptor& section : sectionTable)
        {
            if (section.id == serialization::MakeFourCC('G', 'E', 'O', 'M'))
            {
                return section.offset;
            }
        }
        return 0;
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "meshesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    Fixture fixture;
    Fixture reordered;
    std::swap(reordered.buffers[0], reordered.buffers[1]);
    std::swap(reordered.pages[0], reordered.pages[2]);
    std::swap(reordered.lods[0], reordered.lods[1]);
    std::swap(reordered.submeshes[0], reordered.submeshes[1]);
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == meshes::Result::Success, "write vmesh");
    Check(WriteFixture(reordered.description, second) == meshes::Result::Success && EqualBytes(first, second), "canonical vmesh emission is independent of cooker record order");

    filesystem::MemoryFileReader reader(first, 0);
    meshes::MeshFile mesh;
    const meshes::Result openResult = mesh.Open(reader);
    if (openResult != meshes::Result::Success)
    {
        std::fprintf(stderr, "[meshesTests] vmesh open result: %s\n", meshes::ToString(openResult));
    }
    Check(openResult == meshes::Result::Success, "open vmesh metadata");
    Check(mesh.IsOpen() && mesh.GetKind() == meshes::MeshKind::Static && mesh.GetName() == fixture.description.name, "mesh identity round trip");
    Check(mesh.GetBuffers().Size() == 2 && mesh.GetPages().Size() == 3 && mesh.GetVertexStreams().Size() == 1, "buffer and stream metadata round trip");
    Check(mesh.GetLods().Size() == 2 && mesh.GetLods()[0].minimumScreenCoverage == 1.0f && mesh.GetLods()[1].minimumScreenCoverage == 0.25f, "LODs canonicalized by level");
    Check(mesh.GetSubmeshes().Size() == 2 && mesh.GetSubmeshes()[0].firstIndex == 0 && mesh.GetSubmeshes()[1].firstIndex == 3 &&
              mesh.GetSubmeshes()[0].stableId == mesh.GetSubmeshes()[1].stableId,
          "stable submesh identity survives canonical LOD ranges");

    {
        const filesystem::AbsolutePath testDirectory = root.AddDirPath("vanguard_mesh_page_source_tests");
        const filesystem::AbsolutePath loosePath = testDirectory.AddFilePath("mesh.vmesh");
        const filesystem::AbsolutePath packagePath = testDirectory.AddFilePath("mesh.vpak");
        filesystem::Manager& fileManager = filesystem::GetManager();
        static_cast<void>(fileManager.DeleteFile(loosePath));
        static_cast<void>(fileManager.DeleteFile(packagePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
        Check(fileManager.CreatePath(testDirectory), "create mesh page source test directory");
        Check(WriteFile(loosePath, first), "publish loose vmesh source");

        containers::DynamicArray<meshes::StorageSegment> storageSegments(memory::pools::Rendering::GetInstance());
        Check(meshes::BuildStorageSegments(mesh, first.Size(), storageSegments) == meshes::Result::Success, "build vmesh package storage segments");
        containers::DynamicArray<vanguard::packages::BuildSegment> buildSegments(memory::pools::Rendering::GetInstance());
        buildSegments.Resize(storageSegments.Size());
        for (vanguard::u32 index = 0; index < storageSegments.Size(); ++index)
        {
            const meshes::StorageSegment& segment = storageSegments[index];
            vanguard::packages::SegmentFlags flags = vanguard::packages::SegmentFlags::Streamable;
            if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::Metadata))
            {
                flags = vanguard::packages::SegmentFlags::Inline | vanguard::packages::SegmentFlags::MemoryResident;
            }
            else if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::RequiredForLowestLod))
            {
                flags = flags | vanguard::packages::SegmentFlags::MemoryResident;
            }
            buildSegments[index] = {first.TypedData() + segment.offset, static_cast<vanguard::usize>(segment.byteSize), vanguard::packages::Codec::None, segment.alignmentLog2, flags};
        }
        vanguard::packages::BuildResource buildResource;
        buildResource.path = "meshes/test.vmesh";
        buildResource.type = meshes::MeshResourceType;
        buildResource.flags = vanguard::packages::ResourceFlags::Streamable;
        buildResource.segments = {buildSegments.TypedData(), buildSegments.Size()};
        ByteArray packageBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter packageMemoryWriter(packageBytes);
        vanguard::packages::PackageWriter packageWriter;
        Check(packageWriter.Begin(packageMemoryWriter) == vanguard::packages::Result::Success && packageWriter.Add(buildResource) == vanguard::packages::Result::Success &&
                  packageWriter.Finalize() == vanguard::packages::Result::Success && WriteFile(packagePath, packageBytes),
              "publish segmented VPAK mesh source");

        meshes::MeshPageSource looseSource;
        meshes::MeshFile looseMesh;
        Check(looseSource.OpenLoose(loosePath) == meshes::Result::Success && looseSource.GetLogicalSize() == first.Size() && looseSource.ReadMetadata(looseMesh) == meshes::Result::Success &&
                  looseMesh.GetContentFingerprint() == mesh.GetContentFingerprint(),
              "owned loose mesh source opens metadata without geometry staging");
        ByteArray loosePage(memory::pools::Rendering::GetInstance());
        Check(looseSource.ReadPage(looseMesh, 0, loosePage) == meshes::Result::Success, "owned loose mesh source reads one validated page");
        meshes::MeshPageReadRequest asyncLoosePage;
        Check(looseSource.ReadPageAsync(looseMesh, 0, asyncLoosePage) == meshes::Result::Success && asyncLoosePage.TryWait(10000) && asyncLoosePage.GetResult() == meshes::Result::Success &&
                  asyncLoosePage.GetBytes().Count() == loosePage.Size() && std::memcmp(asyncLoosePage.GetBytes().Data(), loosePage.TypedData(), loosePage.Size()) == 0,
              "owned loose mesh source asynchronously reads and validates one page");

        meshes::MeshPageSource packageSource;
        meshes::MeshFile packageMesh;
        meshes::MeshPageReadStats metadataStats;
        const vanguard::packages::ResourceId packagedId = vanguard::packages::HashResourcePath("meshes/test.vmesh");
        Check(packageSource.OpenPackage(packagePath, packagedId) == meshes::Result::Success && packageSource.ReadMetadata(packageMesh, &metadataStats) == meshes::Result::Success &&
                  packageMesh.GetContentFingerprint() == mesh.GetContentFingerprint() && metadataStats.decodedSegments == 1 && metadataStats.storedBytesRead < packageSource.GetLogicalSize(),
              "owned VPAK mesh source decodes metadata segment only");
        meshes::MeshPageReadStats pageStats;
        ByteArray packagePage(memory::pools::Rendering::GetInstance());
        Check(packageSource.ReadPage(packageMesh, 0, packagePage, &pageStats) == meshes::Result::Success && EqualBytes(loosePage, packagePage) && pageStats.decodedSegments == 1 &&
                  pageStats.storedBytesRead < packageSource.GetLogicalSize(),
              "owned VPAK mesh source decodes only the requested page segment");
        meshes::MeshPageReadRequest asyncPackagePage;
        Check(packageSource.ReadPageAsync(packageMesh, 0, asyncPackagePage) == meshes::Result::Success && asyncPackagePage.TryWait(10000) &&
                  asyncPackagePage.GetResult() == meshes::Result::Success && asyncPackagePage.GetBytes().Count() == packagePage.Size() &&
                  std::memcmp(asyncPackagePage.GetBytes().Data(), packagePage.TypedData(), packagePage.Size()) == 0 && asyncPackagePage.GetStats().decodedSegments == 1,
              "owned VPAK mesh source asynchronously decodes and validates only one page segment");

        meshes::MeshPageSource movedSource(std::move(packageSource));
        ByteArray movedPage(memory::pools::Rendering::GetInstance());
        Check(!packageSource.IsOpen() && movedSource.ReadPage(packageMesh, 1, movedPage) == meshes::Result::Success, "mesh page source retains owned package metadata across moves");

        resources::ResourceRegistry dependencyRegistry;
        Check(dependencyRegistry.Initialize() && dependencyRegistry.RegisterLoader({TestMaterialType, "mesh test material", BeginTestMaterialLoad, DestroyTestMaterial, nullptr}),
              "initialize mesh dependency registry");
        resources::ResourceRequest materialRequest = dependencyRegistry.Request(fixture.materials[0].material);
        resources::ResourceHandle material = materialRequest.Acquire();
        Check(material.IsValid(), "resolve required mesh material dependency");

        meshes::MeshPageSource resourceSource;
        meshes::MeshResourceObject resource;
        Check(resourceSource.OpenPackage(packagePath, packagedId) == meshes::Result::Success && resource.Open(std::move(resourceSource), {&material, 1}) == meshes::Result::Success &&
                  resource.IsOpen() && resource.GetType() == meshes::MeshResourceType && resource.GetDependencies().Count() == 1,
              "publish metadata-only mesh resource with retained exact dependencies");
        containers::DynamicArray<vanguard::u32> fallbackPages(memory::pools::Rendering::GetInstance());
        Check(resource.BuildLodUploadSet(static_cast<vanguard::u16>(resource.GetMetadata().GetLods().Size() - 1u), fallbackPages) == meshes::Result::Success && fallbackPages.Size() == 3,
              "derive complete fallback LOD install set from immutable metadata");

        meshes::MeshPageSource missingDependencySource;
        meshes::MeshResourceObject missingDependencyResource;
        Check(missingDependencySource.OpenPackage(packagePath, packagedId) == meshes::Result::Success &&
                  missingDependencyResource.Open(std::move(missingDependencySource), {}) == meshes::Result::DependencyMismatch && !missingDependencyResource.IsOpen(),
              "reject mesh publication when META dependencies are not retained");
        resource.Close();
        material.Reset();
        materialRequest.Reset();
        Check(dependencyRegistry.Evict(fixture.materials[0].material.GetPath()) && dependencyRegistry.UnregisterLoader(TestMaterialType) && dependencyRegistry.Shutdown(),
              "release mesh dependency ownership cleanly");

        looseMesh.Close();
        packageMesh.Close();
        looseSource.Close();
        movedSource.Close();
        static_cast<void>(fileManager.DeleteFile(loosePath));
        static_cast<void>(fileManager.DeleteFile(packagePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
    }

    std::array<vanguard::u8, 64> pageBytes{};
    filesystem::MemoryFileReader pageReader(first, 0);
    Check(mesh.ReadPage(pageReader, 0, pageBytes.data(), pageBytes.size()) == meshes::Result::Success, "range-read and validate first geometry page");
    Check(mesh.ReadPage(pageReader, 0, pageBytes.data(), 1) == meshes::Result::BufferTooSmall, "caller-owned page buffer capacity enforced");

    {
        ByteArray corrupt(first);
        const vanguard::u64 geometryOffset = GetGeometryOffset(corrupt);
        Check(geometryOffset != 0, "locate geometry section");
        if (geometryOffset != 0 && mesh.GetPages().Size() != 0)
        {
            const vanguard::u64 corruptOffset = geometryOffset + mesh.GetPages()[0].dataOffset;
            corrupt[static_cast<vanguard::u32>(corruptOffset)] ^= 1u;
        }
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        meshes::MeshFile corruptMesh;
        Check(corruptMesh.Open(corruptReader) == meshes::Result::Success, "metadata open does not force full geometry residency");
        filesystem::MemoryFileReader corruptPageReader(corrupt, 0);
        Check(corruptMesh.ReadPage(corruptPageReader, 0, pageBytes.data(), pageBytes.size()) == meshes::Result::IntegrityFailure, "streamed page corruption rejected at residency boundary");
    }
    {
        ByteArray corrupt(first);
        if (corrupt.Size() > 80)
        {
            corrupt[80] ^= 1u;
        }
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        meshes::MeshFile corruptMesh;
        Check(corrupt.Size() > 80 && corruptMesh.Open(corruptReader) == meshes::Result::IntegrityFailure, "metadata corruption rejected");
    }
    {
        Fixture invalid;
        invalid.pages[2].bufferOffset = 4;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidPage, "gapped buffer page coverage rejected");
    }
    {
        Fixture invalid;
        invalid.pages[1].flags = meshes::PageFlags::DirectGpuUpload;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidPage, "lowest LOD vertex range requires resident page coverage");
    }
    {
        Fixture invalid;
        invalid.streams[0].semantic = meshes::VertexSemantic::Normal;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::MissingPositionStream, "missing position stream rejected");
    }
    {
        Fixture invalid;
        invalid.buffers[0].id = invalid.buffers[1].id;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::DuplicateIdentifier, "duplicate stable cooker identifiers rejected");
    }
    {
        Fixture invalid;
        invalid.lods[0].minimumScreenCoverage = 0.5f;
        invalid.lods[1].minimumScreenCoverage = 0.5f;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidLod, "non-decreasing LOD thresholds rejected");
    }
    {
        Fixture multipleLayouts;
        std::array<meshes::VertexLayoutBuildRecord, 2> layouts{{{1}, {2}}};
        std::array<meshes::VertexStreamBuildRecord, 2> streams{{multipleLayouts.streams[0], {2, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12}}};
        multipleLayouts.submeshes[0].vertexLayoutId = 2;
        multipleLayouts.description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
        multipleLayouts.description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(multipleLayouts.description, output) == meshes::Result::Success, "write adjacent position-only vertex layouts");
        filesystem::MemoryFileReader multipleLayoutReader(output, 0);
        meshes::MeshFile multipleLayoutMesh;
        Check(multipleLayoutMesh.Open(multipleLayoutReader) == meshes::Result::Success, "load duplicate semantics when they belong to different layouts");
    }
    {
        Fixture capacity;
        constexpr vanguard::u32 LayoutCount = 65;
        std::array<meshes::VertexLayoutBuildRecord, LayoutCount> layouts{};
        std::array<meshes::VertexStreamBuildRecord, LayoutCount> streams{};
        std::array<meshes::SubmeshBuildRecord, LayoutCount> submeshes{};
        const std::array<meshes::LodBuildRecord, 1> lods{{{0, 1.0f}}};
        for (vanguard::u32 index = 0; index < LayoutCount; ++index)
        {
            layouts[index].id = index + 1;
            streams[index] = {index + 1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12};
            submeshes[index] = capacity.submeshes[1];
            submeshes[index].stableId = index + 1;
            submeshes[index].vertexLayoutId = index + 1;
        }
        capacity.description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
        capacity.description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
        capacity.description.lods = {lods.data(), static_cast<vanguard::u32>(lods.size())};
        capacity.description.submeshes = {submeshes.data(), static_cast<vanguard::u32>(submeshes.size())};
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(capacity.description, output) == meshes::Result::Success, "support production-scale stream metadata beyond the former placeholder cap");
        filesystem::MemoryFileReader capacityReader(output, 0);
        meshes::MeshFile capacityMesh;
        Check(capacityMesh.Open(capacityReader) == meshes::Result::Success && capacityMesh.GetVertexStreams().Size() == LayoutCount, "load expanded bounded stream metadata");
    }

    mesh.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[meshesTests] Vanguard vmesh format and streaming-page checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
