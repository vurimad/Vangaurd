#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/meshes/meshes.hpp>

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
            buffers = {{{20, meshes::BufferKind::Index, 2, 12},
                        {10, meshes::BufferKind::Vertex, 12, 48}}};
            pages = {{{20, 0, indexPage.data(), indexPage.size(), 4, meshes::PageFlags::RequiredForLowestLod},
                      {10, 24, vertexPage1.data(), vertexPage1.size(), 6,
                       meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload},
                      {10, 0, vertexPage0.data(), vertexPage0.size(), 4,
                       meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload}}};
            layouts = {{{1}}};
            streams = {{{1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12}}};
            materials = {{{100, 0xa11ce,
                           resources::ResourceReference(resources::ResourcePath::FromString("materials/test.vmat"),
                                                        serialization::MakeFourCC('V', 'M', 'A', 'T'))}}};
            lods = {{{1, 0.25f}, {0, 1.0f}}};

            meshes::Bounds submeshBounds;
            submeshBounds.minimum[0] = -1.0f;
            submeshBounds.minimum[1] = -1.0f;
            submeshBounds.minimum[2] = -1.0f;
            submeshBounds.maximum[0] = 1.0f;
            submeshBounds.maximum[1] = 1.0f;
            submeshBounds.maximum[2] = 1.0f;
            submeshBounds.sphereRadius = 1.75f;
            submeshes = {{{0x1000, 0xb0d1, 1, 100, 1, 20, meshes::IndexFormat::UInt16,
                           meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::CastsShadow, 0, 3, 3, 3,
                           submeshBounds},
                          {0x1000, 0xb0d1, 0, 100, 1, 20, meshes::IndexFormat::UInt16,
                           meshes::PrimitiveTopology::TriangleList,
                           meshes::SubmeshFlags::CastsShadow | meshes::SubmeshFlags::RayTracing, 0, 3, 0, 3,
                           submeshBounds}}};

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

    vanguard::u64 GeometryOffset(ByteArray& document)
    {
        vanguard::filesystem::MemoryFileReader reader(document, 0);
        serialization::BinaryReader binaryReader(reader);
        serialization::DocumentHeader header;
        if (serialization::ReadDocumentHeader(binaryReader, meshes::MeshMagic, {1, 0, 0}, {}, header) !=
            serialization::Result::Success)
        {
            return 0;
        }
        vanguard::containers::DynamicArray<serialization::SectionDescriptor> sectionTable(
            vanguard::memory::pools::Serialization::GetInstance());
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
    Check(WriteFixture(reordered.description, second) == meshes::Result::Success && EqualBytes(first, second),
          "canonical vmesh emission is independent of cooker record order");

    filesystem::MemoryFileReader reader(first, 0);
    meshes::MeshFile mesh;
    const meshes::Result openResult = mesh.Open(reader);
    if (openResult != meshes::Result::Success)
    {
        std::fprintf(stderr, "[meshesTests] vmesh open result: %s\n", meshes::ToString(openResult));
    }
    Check(openResult == meshes::Result::Success, "open vmesh metadata");
    Check(mesh.IsOpen() && mesh.Kind() == meshes::MeshKind::Static && mesh.Name() == fixture.description.name,
          "mesh identity round trip");
    Check(mesh.Buffers().Size() == 2 && mesh.Pages().Size() == 3 && mesh.VertexStreams().Size() == 1,
          "buffer and stream metadata round trip");
    Check(mesh.Lods().Size() == 2 && mesh.Lods()[0].minimumScreenCoverage == 1.0f &&
              mesh.Lods()[1].minimumScreenCoverage == 0.25f,
          "LODs canonicalized by level");
    Check(mesh.Submeshes().Size() == 2 && mesh.Submeshes()[0].firstIndex == 0 && mesh.Submeshes()[1].firstIndex == 3 &&
              mesh.Submeshes()[0].stableId == mesh.Submeshes()[1].stableId,
          "stable submesh identity survives canonical LOD ranges");

    std::array<vanguard::u8, 64> pageBytes{};
    filesystem::MemoryFileReader pageReader(first, 0);
    Check(mesh.ReadPage(pageReader, 0, pageBytes.data(), pageBytes.size()) == meshes::Result::Success,
          "range-read and validate first geometry page");
    Check(mesh.ReadPage(pageReader, 0, pageBytes.data(), 1) == meshes::Result::BufferTooSmall,
          "caller-owned page buffer capacity enforced");

    {
        ByteArray corrupt(first);
        const vanguard::u64 geometryOffset = GeometryOffset(corrupt);
        Check(geometryOffset != 0, "locate geometry section");
        if (geometryOffset != 0 && mesh.Pages().Size() != 0)
        {
            const vanguard::u64 corruptOffset = geometryOffset + mesh.Pages()[0].dataOffset;
            corrupt[static_cast<vanguard::u32>(corruptOffset)] ^= 1u;
        }
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        meshes::MeshFile corruptMesh;
        Check(corruptMesh.Open(corruptReader) == meshes::Result::Success,
              "metadata open does not force full geometry residency");
        filesystem::MemoryFileReader corruptPageReader(corrupt, 0);
        Check(corruptMesh.ReadPage(corruptPageReader, 0, pageBytes.data(), pageBytes.size()) ==
                  meshes::Result::IntegrityFailure,
              "streamed page corruption rejected at residency boundary");
    }
    {
        ByteArray corrupt(first);
        if (corrupt.Size() > 80)
        {
            corrupt[80] ^= 1u;
        }
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        meshes::MeshFile corruptMesh;
        Check(corrupt.Size() > 80 && corruptMesh.Open(corruptReader) == meshes::Result::IntegrityFailure,
              "metadata corruption rejected");
    }
    {
        Fixture invalid;
        invalid.pages[2].bufferOffset = 4;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidPage,
              "gapped buffer page coverage rejected");
    }
    {
        Fixture invalid;
        invalid.pages[1].flags = meshes::PageFlags::DirectGpuUpload;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidPage,
              "lowest LOD vertex range requires resident page coverage");
    }
    {
        Fixture invalid;
        invalid.streams[0].semantic = meshes::VertexSemantic::Normal;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::MissingPositionStream,
              "missing position stream rejected");
    }
    {
        Fixture invalid;
        invalid.buffers[0].id = invalid.buffers[1].id;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::DuplicateIdentifier,
              "duplicate stable cooker identifiers rejected");
    }
    {
        Fixture invalid;
        invalid.lods[0].minimumScreenCoverage = 0.5f;
        invalid.lods[1].minimumScreenCoverage = 0.5f;
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalid.description, output) == meshes::Result::InvalidLod,
              "non-decreasing LOD thresholds rejected");
    }
    {
        Fixture multipleLayouts;
        std::array<meshes::VertexLayoutBuildRecord, 2> layouts{{{1}, {2}}};
        std::array<meshes::VertexStreamBuildRecord, 2> streams{{
            multipleLayouts.streams[0],
            {2, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12}}};
        multipleLayouts.submeshes[0].vertexLayoutId = 2;
        multipleLayouts.description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
        multipleLayouts.description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
        ByteArray output(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(multipleLayouts.description, output) == meshes::Result::Success,
              "write adjacent position-only vertex layouts");
        filesystem::MemoryFileReader multipleLayoutReader(output, 0);
        meshes::MeshFile multipleLayoutMesh;
        Check(multipleLayoutMesh.Open(multipleLayoutReader) == meshes::Result::Success,
              "load duplicate semantics when they belong to different layouts");
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
        Check(WriteFixture(capacity.description, output) == meshes::Result::Success,
              "support production-scale stream metadata beyond the former placeholder cap");
        filesystem::MemoryFileReader capacityReader(output, 0);
        meshes::MeshFile capacityMesh;
        Check(capacityMesh.Open(capacityReader) == meshes::Result::Success && capacityMesh.VertexStreams().Size() == LayoutCount,
              "load expanded bounded stream metadata");
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
