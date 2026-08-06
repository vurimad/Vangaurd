#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/textures/textures.hpp>

#include <array>
#include <cstdio>
#include <utility>

namespace
{
    namespace textures = vanguard::textures;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[texturesTests] FAILED: %s\n", message);
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
        std::array<std::array<vanguard::u8, 32>, 8> bytes{};
        std::array<textures::SubresourceBuildRecord, 8> subresources{};
        textures::BuildDescription description;

        Fixture()
        {
            for (vanguard::u32 index = 0; index < bytes.size(); ++index)
            {
                for (vanguard::u32 byte = 0; byte < bytes[index].size(); ++byte)
                {
                    bytes[index][byte] = static_cast<vanguard::u8>(index * 31 + byte);
                }
            }
            vanguard::u32 record = 0;
            for (vanguard::u8 mip = 0; mip < 4; ++mip)
            {
                const vanguard::u32 extent = textures::CalculateMipExtent(8, mip);
                const vanguard::u32 rowPitch = textures::CalculateMinimumRowPitch(textures::PixelFormat::BC1UNorm, extent);
                const vanguard::u32 slicePitch =
                    textures::CalculateMinimumSlicePitch(textures::PixelFormat::BC1UNorm, extent, extent);
                for (vanguard::u16 layer = 0; layer < 2; ++layer)
                {
                    subresources[record] = {mip, layer, 0, bytes[record].data(), slicePitch, rowPitch, slicePitch};
                    ++record;
                }
            }
            description.dimension = textures::TextureDimension::Texture2D;
            description.format = textures::PixelFormat::BC1UNorm;
            description.colorSpace = textures::ColorSpace::SRgb;
            description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
            description.width = 8;
            description.height = 8;
            description.depth = 1;
            description.arrayLayers = 2;
            description.mipCount = 4;
            description.mipTailFirstLevel = 2;
            description.sourceFingerprint = vanguard::crypto::Sha256("texture-source-and-profile", 26);
            description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        }
    };

    textures::Result WriteFixture(const textures::BuildDescription& description, ByteArray& output)
    {
        output.Clear();
        vanguard::filesystem::MemoryFileWriter writer(output);
        return textures::WriteTexture(writer, description);
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
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "texturesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    Fixture fixture;
    Fixture reordered;
    std::swap(reordered.subresources[0], reordered.subresources[7]);
    std::swap(reordered.subresources[1], reordered.subresources[5]);
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == textures::Result::Success, "write vtex");
    Check(WriteFixture(reordered.description, second) == textures::Result::Success && EqualBytes(first, second),
          "canonical vtex emission is independent of cooker record order");

    filesystem::MemoryFileReader reader(first, 0);
    textures::TextureFile texture;
    const textures::Result openResult = texture.Open(reader);
    if (openResult != textures::Result::Success)
    {
        std::fprintf(stderr, "[texturesTests] vtex open result: %s\n", textures::ToString(openResult));
    }
    Check(openResult == textures::Result::Success, "open vtex metadata");
    Check(texture.IsOpen() && texture.Dimension() == textures::TextureDimension::Texture2D &&
              texture.Format() == textures::PixelFormat::BC1UNorm && texture.Space() == textures::ColorSpace::SRgb,
          "texture identity and format round trip");
    Check(texture.Width() == 8 && texture.Height() == 8 && texture.ArrayLayers() == 2 && texture.MipCount() == 4 &&
              texture.MipTailFirstLevel() == 2 && texture.Subresources().Size() == 8,
          "texture shape and complete subresource table round trip");
    Check(texture.FindSubresource(2, 1) == 5 && texture.Subresources().Size() > 5 &&
              textures::HasFlag(texture.Subresources()[5].flags, textures::SubresourceFlags::MipTail),
          "mip-major array indexing and resident tail contract");

    std::array<vanguard::u8, 64> loaded{};
    filesystem::MemoryFileReader subresourceReader(first, 0);
    Check(texture.ReadSubresource(subresourceReader, 0, loaded.data(), loaded.size()) == textures::Result::Success,
          "range-read and validate one GPU subresource");
    Check(texture.ReadSubresource(subresourceReader, 0, loaded.data(), 1) == textures::Result::BufferTooSmall,
          "caller-owned subresource capacity enforced");

    containers::DynamicArray<textures::StorageSegment> segments(memory::pools::Rendering::GetInstance());
    Check(textures::BuildStorageSegments(texture, first.Size(), segments) == textures::Result::Success && segments.Size() == 9 &&
              segments[0].flags == textures::StorageSegmentFlags::Metadata &&
              static_cast<vanguard::u8>(segments[6].flags) ==
                  static_cast<vanguard::u8>(textures::StorageSegmentFlags::Streamable |
                                            textures::StorageSegmentFlags::RequiredForMipTail),
          "VPAK-ready metadata, streamed mip, and resident-tail segments");

    {
        ByteArray corrupt(first);
        if (texture.Subresources().Size() != 0)
        {
            const vanguard::u64 byte = texture.TextureDataOffset() + texture.Subresources()[0].dataOffset;
            corrupt[static_cast<vanguard::u32>(byte)] ^= 1u;
        }
        filesystem::MemoryFileReader corruptMetadataReader(corrupt, 0);
        textures::TextureFile corruptTexture;
        Check(corruptTexture.Open(corruptMetadataReader) == textures::Result::Success,
              "opening vtex metadata does not force texture payload residency");
        filesystem::MemoryFileReader corruptPayloadReader(corrupt, 0);
        Check(corruptTexture.ReadSubresource(corruptPayloadReader, 0, loaded.data(), loaded.size()) ==
                  textures::Result::IntegrityFailure,
              "streamed subresource corruption rejected at residency boundary");
    }
    {
        Fixture invalid;
        invalid.description.format = textures::PixelFormat::BC5UNorm;
        Check(WriteFixture(invalid.description, second) == textures::Result::InvalidFormat,
              "sRGB rejected for a non-color GPU format");
    }
    {
        Fixture invalid;
        invalid.subresources[0].rowPitch = 1;
        Check(WriteFixture(invalid.description, second) == textures::Result::InvalidSubresource,
              "undersized GPU row pitch rejected");
    }
    {
        Fixture invalid;
        invalid.description.dimension = textures::TextureDimension::Cube;
        Check(WriteFixture(invalid.description, second) == textures::Result::MissingSubresource,
              "incomplete cube face set rejected");
    }
    {
        Fixture invalid;
        invalid.subresources[7] = invalid.subresources[6];
        Check(WriteFixture(invalid.description, second) == textures::Result::DuplicateSubresource,
              "duplicate subresource identity rejected");
    }
    {
        std::array<std::array<vanguard::u8, 8>, 6> faceBytes{};
        std::array<textures::SubresourceBuildRecord, 6> faces{};
        for (vanguard::u8 face = 0; face < 6; ++face)
        {
            faceBytes[face][0] = face;
            faces[face] = {0, 0, face, faceBytes[face].data(), faceBytes[face].size(), 8, 8};
        }
        textures::BuildDescription cube;
        cube.dimension = textures::TextureDimension::Cube;
        cube.format = textures::PixelFormat::BC1UNorm;
        cube.width = cube.height = 4;
        cube.subresources = {faces.data(), static_cast<vanguard::u32>(faces.size())};
        Check(WriteFixture(cube, second) == textures::Result::Success, "write complete cube face set");
        filesystem::MemoryFileReader cubeReader(second, 0);
        textures::TextureFile cubeTexture;
        Check(cubeTexture.Open(cubeReader) == textures::Result::Success &&
                  cubeTexture.FindSubresource(0, 0, 5) == 5 && cubeTexture.Subresources()[5].face == 5,
              "cube face ordering round trip");
    }
    {
        std::array<vanguard::u8, 16> volumeBytes{};
        const std::array<textures::SubresourceBuildRecord, 1> volumeSubresources{{
            {0, 0, 0, volumeBytes.data(), volumeBytes.size(), 4, 8}}};
        textures::BuildDescription volume;
        volume.dimension = textures::TextureDimension::Texture3D;
        volume.format = textures::PixelFormat::R8UNorm;
        volume.width = 4;
        volume.height = 2;
        volume.depth = 2;
        volume.subresources = {volumeSubresources.data(), static_cast<vanguard::u32>(volumeSubresources.size())};
        Check(WriteFixture(volume, second) == textures::Result::Success, "write 3D texture subresource");
        filesystem::MemoryFileReader volumeReader(second, 0);
        textures::TextureFile volumeTexture;
        Check(volumeTexture.Open(volumeReader) == textures::Result::Success &&
                  volumeTexture.Subresources()[0].depth == 2 && volumeTexture.Subresources()[0].slicePitch == 8,
              "3D mip stores complete depth slices");
    }

    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::fprintf(stdout, "texturesTests: all tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
