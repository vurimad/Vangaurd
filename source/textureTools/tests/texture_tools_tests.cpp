#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>

#include "texture_import_fixtures.hpp"

#define assert(condition) static_cast<void>(condition)
#include <bc7decomp.h>
#include <cmp_core.h>
#include <rgbcx.h>
#undef assert

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    namespace textures = vanguard::textures;
    namespace tt = vanguard::texture_tools;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;
    int g_failures = 0;

    void Check(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[textureToolsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    bool Equal(const ByteArray& left, const ByteArray& right)
    {
        if (left.Size() != right.Size())
            return false;
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
        {
            if (left[index] != right[index])
                return false;
        }
        return true;
    }

    struct CancellationCounter
    {
        vanguard::u32 checks = 0;
        vanguard::u32 cancelAt = 0;
    };

    bool CancelAfterChecks(void* const userData) noexcept
    {
        CancellationCounter& counter = *static_cast<CancellationCounter*>(userData);
        return counter.checks++ >= counter.cancelAt;
    }

    vanguard::u16 ReadU16(const vanguard::u8* bytes) noexcept
    {
        return static_cast<vanguard::u16>(bytes[0]) | static_cast<vanguard::u16>(bytes[1] << 8u);
    }

    float ReadFloat(const vanguard::u8* bytes) noexcept
    {
        float value = 0.0f;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    template <std::size_t Size> void WriteU32(std::array<vanguard::u8, Size>& bytes, const vanguard::u32 offset, const vanguard::u32 value) noexcept
    {
        bytes[offset] = static_cast<vanguard::u8>(value);
        bytes[offset + 1] = static_cast<vanguard::u8>(value >> 8u);
        bytes[offset + 2] = static_cast<vanguard::u8>(value >> 16u);
        bytes[offset + 3] = static_cast<vanguard::u8>(value >> 24u);
    }

    std::array<vanguard::u8, 184> MakeBc1Dds() noexcept
    {
        std::array<vanguard::u8, 184> bytes{};
        WriteU32(bytes, 0, 0x20534444u); // "DDS "
        WriteU32(bytes, 4, 124);
        WriteU32(bytes, 8, 0x000a1007u); // Caps, dimensions, pixel format, linear size, and mip count.
        WriteU32(bytes, 12, 8);
        WriteU32(bytes, 16, 8);
        WriteU32(bytes, 20, 32);
        WriteU32(bytes, 28, 4);
        WriteU32(bytes, 76, 32);
        WriteU32(bytes, 80, 4);
        WriteU32(bytes, 84, 0x31545844u); // DXT1
        WriteU32(bytes, 108, 0x00401008u);
        for (vanguard::u32 byte = 128; byte < static_cast<vanguard::u32>(bytes.size()); ++byte)
            bytes[byte] = static_cast<vanguard::u8>(byte * 37u + 11u);
        return bytes;
    }

    std::array<vanguard::u8, 244> MakeBc7CubeDds() noexcept
    {
        std::array<vanguard::u8, 244> bytes{};
        WriteU32(bytes, 0, 0x20534444u); // "DDS "
        WriteU32(bytes, 4, 124);
        WriteU32(bytes, 8, 0x00081007u);
        WriteU32(bytes, 12, 4);
        WriteU32(bytes, 16, 4);
        WriteU32(bytes, 20, 16);
        WriteU32(bytes, 28, 1);
        WriteU32(bytes, 76, 32);
        WriteU32(bytes, 80, 4);
        WriteU32(bytes, 84, 0x30315844u); // DX10
        WriteU32(bytes, 108, 0x00001008u);
        WriteU32(bytes, 128, 99); // DXGI_FORMAT_BC7_UNORM_SRGB
        WriteU32(bytes, 132, 3);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        WriteU32(bytes, 136, 4);  // D3D11_RESOURCE_MISC_TEXTURECUBE
        WriteU32(bytes, 140, 1);  // One cube, not six faces.
        WriteU32(bytes, 144, 1);  // Straight alpha.
        for (vanguard::u32 byte = 148; byte < static_cast<vanguard::u32>(bytes.size()); ++byte)
            bytes[byte] = static_cast<vanguard::u8>(byte * 19u + 7u);
        return bytes;
    }

    tt::TextureProbeResult ProbeRawTexture(const tt::TextureImportRequest& request, void*) noexcept
    {
        return request.encoded.Count() >= 8 && request.encoded[0] == 'V' && request.encoded[1] == 'R' && request.encoded[2] == 'A' && request.encoded[3] == 'W'
                   ? tt::TextureProbeResult::Exact
                   : tt::TextureProbeResult::NoMatch;
    }

    tt::TextureImportResult DecodeRawTexture(const tt::TextureImportRequest& request, tt::ImportedTexture& output, void*) noexcept
    {
        const vanguard::u32 width = request.encoded[4];
        const vanguard::u32 height = request.encoded[5];
        const vanguard::u32 byteCount = width * height * 4u;
        if (width == 0 || height == 0 || request.encoded.Count() != byteCount + 8u)
            return tt::TextureImportResult::DecodeFailure;
        const vanguard::crypto::Digest256 fingerprint = vanguard::crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes());
        const tt::TextureImportResult initializeResult =
            output.Initialize2D(tt::SourcePixelFormat::R8G8B8A8UNorm, textures::ColorSpace::Linear, width, height, width * 4u, fingerprint, tt::profiles::Data,
                                0x7672617700000001ull, 1, request.limits);
        if (initializeResult != tt::TextureImportResult::Success)
            return initializeResult;
        for (vanguard::u32 index = 0; index < byteCount; ++index)
            output.GetMutableImageData()[index] = request.encoded[index + 8u];
        return tt::TextureImportResult::Success;
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
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "textureToolsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");
    Check(tt::Initialize(), "texture tools initialization");
    const tt::TextureCookingProfile bc2Profile{
        0x6263320000000001ull, 1, textures::PixelFormat::BC2UNorm, textures::ColorSpace::SRgb, tt::TextureCookingFlags::None, 1, 3, 0.5f, 0};
    Check(tt::RegisterCookingProfile(bc2Profile) == tt::ProfileRegistrationResult::Success, "register project BC2 profile before cooking");
    const tt::TextureCookingProfile oddMipProfile{
        0x6f64646d69700001ull, 1, textures::PixelFormat::R8UNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::GenerateFullMipChain, 2, 0, 0.5f, 0};
    Check(tt::RegisterCookingProfile(oddMipProfile) == tt::ProfileRegistrationResult::Success, "register uncompressed odd-extent conformance profile");
    const tt::TextureCookingProfile invalidSrgbHdr{
        0x696e76616c696401ull, 1, textures::PixelFormat::BC6HUFloat, textures::ColorSpace::SRgb, tt::TextureCookingFlags::None, 1, 0, 0.5f, 0};
    Check(tt::RegisterCookingProfile(invalidSrgbHdr) == tt::ProfileRegistrationResult::InvalidArgument,
          "reject sRGB on a format without sRGB sampling support");
    tt::TextureCookingProfile nonFiniteProfile = bc2Profile;
    nonFiniteProfile.id = 0x6e616e0000000001ull;
    nonFiniteProfile.alphaCoverageThreshold = std::numeric_limits<float>::quiet_NaN();
    Check(tt::RegisterCookingProfile(nonFiniteProfile) == tt::ProfileRegistrationResult::InvalidArgument,
          "reject non-finite profile fields before freezing tool identity");
    const std::array<tt::TextureCookingProfile, 7> formatProfiles{
        {{0x666d744243310001ull, 1, textures::PixelFormat::BC1UNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 10},
         {0x666d744243330001ull, 1, textures::PixelFormat::BC3UNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 10},
         {0x666d744243345301ull, 1, textures::PixelFormat::BC4SNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 10},
         {0x666d744243355301ull, 1, textures::PixelFormat::BC5SNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 10},
         {0x666d744243365301ull, 1, textures::PixelFormat::BC6HSFloat, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 0},
         {0x666d745247380001ull, 1, textures::PixelFormat::R8G8UNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 0},
         {0x666d745231360001ull, 1, textures::PixelFormat::R16G16B16A16Float, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 0}}};
    for (const tt::TextureCookingProfile& formatProfile : formatProfiles)
        Check(tt::RegisterCookingProfile(formatProfile) == tt::ProfileRegistrationResult::Success, "register format-conformance profile");

    const tt::TextureImporterDescriptor rawImporter{0x7672617700000001ull, "Vanguard raw importer", 1, &ProbeRawTexture, &DecodeRawTexture, nullptr};
    Check(tt::RegisterTextureImporter(rawImporter) == tt::TextureImporterRegistrationResult::Success, "register project texture source importer");
    Check(tt::RegisterTextureImporter(rawImporter) == tt::TextureImporterRegistrationResult::DuplicateIdentifier, "reject duplicate texture importer identity");
    tt::TextureToolsConfigurationFingerprint frozenTextureTools;
    Check(tt::FreezeConfiguration(frozenTextureTools) && !frozenTextureTools.profiles.IsEmpty() && !frozenTextureTools.importers.IsEmpty(),
          "explicitly freeze profile and importer identity before parallel-capable tool use");
    tt::TextureToolsConfigurationFingerprint repeatedFrozenTextureTools;
    Check(tt::FreezeConfiguration(repeatedFrozenTextureTools) && repeatedFrozenTextureTools.profiles == frozenTextureTools.profiles &&
              repeatedFrozenTextureTools.importers == frozenTextureTools.importers,
          "texture-tools freeze is idempotent and deterministic");
    const std::array<vanguard::u8, 24> rawEncoded{{'V', 'R', 'A', 'W', 2, 2, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    tt::ImportedTexture rawImported;
    tt::TextureImportReport rawImportReport;
    const tt::TextureImportRequest rawImportRequest{
        {rawEncoded.data(), static_cast<vanguard::u32>(rawEncoded.size())}, "vraw", tt::TextureUsage::Data, tt::ImportedColorSpace::Linear};
    Check(tt::ImportTexture(rawImportRequest, rawImported, &rawImportReport) == tt::TextureImportResult::Success && rawImported.IsValid() &&
              rawImportReport.importer == rawImporter.id && rawImported.GetSource().images[0].data == rawImported.GetMutableImageData() &&
              rawImported.GetMutableImageData()[15] == 16,
          "registered importer produces owned neutral source data");
    tt::TextureImportRequest swizzleRequest = rawImportRequest;
    swizzleRequest.channels = {tt::ImportedChannel::Blue, tt::ImportedChannel::Green, tt::ImportedChannel::Red, tt::ImportedChannel::One};
    tt::ImportedTexture swizzledImported;
    Check(tt::ImportTexture(swizzleRequest, swizzledImported) == tt::TextureImportResult::Success && swizzledImported.GetMutableImageData()[0] == 3 &&
              swizzledImported.GetMutableImageData()[1] == 2 && swizzledImported.GetMutableImageData()[2] == 1 && swizzledImported.GetMutableImageData()[3] == 255,
          "explicit channel mapping is applied after source decoding");
    Check(tt::RegisterTextureImporter({0x6c61746500000001ull, "late", 1, &ProbeRawTexture, &DecodeRawTexture, nullptr}) ==
              tt::TextureImporterRegistrationResult::RegistrySealed,
          "texture importer registry seals on first import");

    const std::array<vanguard::u8, 70> redPng{{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
                                               0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
                                               0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
                                               0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82}};
    tt::ImportedTexture pngImported;
    tt::TextureImportReport pngImportReport;
    const tt::TextureImportRequest pngRequest{
        {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, "png", tt::TextureUsage::Ui, tt::ImportedColorSpace::SRgb};
    Check(tt::ImportTexture(pngRequest, pngImported, &pngImportReport) == tt::TextureImportResult::Success && pngImportReport.importer == tt::importers::Png &&
              pngImportReport.width == 1 && pngImportReport.height == 1 && pngImported.GetRecommendedProfile() == tt::profiles::Ui &&
              pngImported.GetMutableImageData()[0] == 255 && pngImported.GetMutableImageData()[1] == 0,
          "libpng imports PNG bytes into an owned RGBA source");
    ByteArray importedTextureBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter importedTextureWriter(importedTextureBytes);
    Check(tt::CookTexture(pngImported.GetSource(), importedTextureWriter, {pngImported.GetRecommendedProfile()}) == tt::Result::Success,
          "imported source feeds directly into vtex cooking");

    tt::TextureSourceInspection pngInspection;
    Check(tt::InspectTextureSource({redPng.data(), static_cast<vanguard::u32>(redPng.size())}, pngInspection) == tt::TextureImportResult::Success &&
              pngInspection.kind == tt::TextureSourceKind::Png && pngInspection.width == 1 && pngInspection.height == 1 &&
              pngInspection.decodedBytes == 16,
          "cheap source inspection reads PNG shape without decoding pixels");

    ByteArray textureBuildSettings(memory::pools::Assets::GetInstance());
    tt::TextureBuildDescription textureBuildDescription;
    textureBuildDescription.sourceMode = tt::TextureBuildSourceMode::Image2D;
    textureBuildDescription.colorSpace = tt::ImportedColorSpace::SRgb;
    textureBuildDescription.profile = tt::profiles::Ui;
    Check(tt::EncodeTextureBuildSettings(textureBuildDescription, textureBuildSettings) == tt::TextureBuildSettingsResult::Success &&
              textureBuildSettings.Size() == 24 && textureBuildSettings[0] == 'V' && textureBuildSettings[1] == 'T' &&
              textureBuildSettings[2] == 'C' && textureBuildSettings[3] == 'B',
          "texture build settings use the canonical fixed-size V1 encoding");

    vanguard::assets::BuildSystem textureBuildSystem;
    tt::TextureAssetCompiler textureCompiler;
    Check(textureBuildSystem.Initialize(), "initialize generic build system for texture compiler");
    Check(textureCompiler.Initialize() && textureCompiler.Register(textureBuildSystem) == vanguard::assets::Result::Success,
          "freeze texture tools and register the single VTSR-to-VTEX compiler");
    const vanguard::resources::ResourceReference textureSource(
        vanguard::resources::ResourcePath::FromString("tests/textures/red.vtsr"), tt::TextureSourceResourceType);
    const vanguard::resources::ResourceReference textureOutput(
        vanguard::resources::ResourcePath::FromString("tests/textures/red.vtex"), textures::TextureResourceType);
    const vanguard::assets::BuildRequest textureBuildRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, textureBuildSettings};
    vanguard::assets::BuildPlan textureBuildPlan;
    Check(textureBuildSystem.Prepare(textureBuildRequest, textureBuildPlan) == vanguard::assets::Result::Success &&
              textureBuildPlan.HasResourceEstimate() && textureBuildPlan.GetResourceEstimate().artifactBytes != 0,
          "texture preparation discovers only tool identity and a bounded source-header estimate");
    vanguard::assets::BuildOutput textureBuildOutput;
    Check(textureBuildSystem.Execute(textureBuildRequest, textureBuildPlan, textureBuildOutput) == vanguard::assets::Result::Success &&
              textureBuildOutput.artifacts.Size() == 2 && textureBuildOutput.artifacts[0].segment == 0 &&
              vanguard::assets::HasFlag(textureBuildOutput.artifacts[0].flags, vanguard::assets::ArtifactFlags::Primary) &&
              vanguard::assets::HasFlag(textureBuildOutput.artifacts[0].flags, vanguard::assets::ArtifactFlags::MemoryResident) &&
              vanguard::assets::HasFlag(textureBuildOutput.artifacts[1].flags, vanguard::assets::ArtifactFlags::Streamable) &&
              vanguard::assets::HasFlag(textureBuildOutput.artifacts[1].flags, vanguard::assets::ArtifactFlags::MemoryResident),
          "texture compiler emits metadata plus the required VTEX mip-tail segment");

    ByteArray reconstructedTexture(memory::pools::Assets::GetInstance());
    reconstructedTexture.Reserve(importedTextureBytes.Size());
    bool denseArtifactSet = reconstructedTexture.Capacity() >= importedTextureBytes.Size();
    for (vanguard::u32 segment = 0; denseArtifactSet && segment < textureBuildOutput.artifacts.Size(); ++segment)
    {
        const vanguard::assets::Artifact& artifact = textureBuildOutput.artifacts[segment];
        denseArtifactSet = artifact.resource == textureOutput && artifact.segment == segment;
        for (vanguard::u32 byte = 0; denseArtifactSet && byte < artifact.bytes.Size(); ++byte)
            reconstructedTexture.PushBack(artifact.bytes[byte]);
    }
    filesystem::MemoryFileReader reconstructedReader(reconstructedTexture, 0);
    textures::TextureFile reconstructedFile;
    Check(denseArtifactSet && Equal(reconstructedTexture, importedTextureBytes) &&
              reconstructedFile.Open(reconstructedReader) == textures::Result::Success,
          "ordered compiler artifacts reconstruct the direct-cooked VTEX byte for byte and reopen successfully");

    vanguard::assets::BuildOutput cachedTextureBuild;
    Check(textureBuildSystem.Execute(textureBuildRequest, textureBuildPlan, cachedTextureBuild) == vanguard::assets::Result::Success &&
              cachedTextureBuild.disposition == vanguard::assets::BuildDisposition::CacheHit &&
              cachedTextureBuild.buildFingerprint == textureBuildOutput.buildFingerprint &&
              cachedTextureBuild.contentFingerprint == textureBuildOutput.contentFingerprint,
          "an identical texture build deterministically hits the generic derived-data cache");

    const auto RejectNonCanonicalSettings = [&](const vanguard::u32 offset, const vanguard::u8 value, const char* const message) {
        ByteArray corrupted(memory::pools::Assets::GetInstance());
        corrupted.Reserve(textureBuildSettings.Size());
        for (vanguard::u32 byte = 0; byte < textureBuildSettings.Size(); ++byte)
            corrupted.PushBack(textureBuildSettings[byte]);
        corrupted[offset] = value;
        const vanguard::assets::BuildRequest request{
            {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
            vanguard::assets::TargetPlatform::WindowsD3D12, corrupted};
        vanguard::assets::BuildPlan plan;
        Check(textureBuildSystem.Prepare(request, plan) == vanguard::assets::Result::DependencyDiscoveryFailed, message);
    };
    RejectNonCanonicalSettings(0, 0, "reject texture settings with a noncanonical magic value");
    RejectNonCanonicalSettings(4, 2, "reject texture settings with an unsupported version");
    RejectNonCanonicalSettings(6, 1, "reject texture settings with nonzero reserved bytes");
    RejectNonCanonicalSettings(8, 0xff, "reject texture settings with an unknown source mode");
    RejectNonCanonicalSettings(10, 0xff, "reject texture settings with an invalid channel selector");
    RejectNonCanonicalSettings(14, 1, "reject streamable flags on a decoded-image route");
    RejectNonCanonicalSettings(15, 1, "reject a DDS mip-tail field on a decoded-image route");
    ByteArray extendedSettings(memory::pools::Assets::GetInstance());
    extendedSettings.Reserve(textureBuildSettings.Size() + 1u);
    for (vanguard::u32 byte = 0; byte < textureBuildSettings.Size(); ++byte)
        extendedSettings.PushBack(textureBuildSettings[byte]);
    extendedSettings.PushBack(0);
    const vanguard::assets::BuildRequest extendedSettingsRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, extendedSettings};
    vanguard::assets::BuildPlan extendedSettingsPlan;
    Check(textureBuildSystem.Prepare(extendedSettingsRequest, extendedSettingsPlan) == vanguard::assets::Result::DependencyDiscoveryFailed,
          "reject trailing bytes after the canonical texture settings record");

    const vanguard::assets::BuildRequest vulkanTextureRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsVulkan, textureBuildSettings};
    vanguard::assets::BuildPlan vulkanTexturePlan;
    vanguard::assets::BuildOutput vulkanTextureOutput;
    Check(textureBuildSystem.Prepare(vulkanTextureRequest, vulkanTexturePlan) == vanguard::assets::Result::Success &&
              textureBuildSystem.Execute(vulkanTextureRequest, vulkanTexturePlan, vulkanTextureOutput) == vanguard::assets::Result::Success &&
              vulkanTextureOutput.disposition == vanguard::assets::BuildDisposition::Built &&
              !(vulkanTextureOutput.buildFingerprint == textureBuildOutput.buildFingerprint),
          "target-dependent tool identity invalidates the texture build cache");

    tt::TextureBuildDescription colorBuildDescription = textureBuildDescription;
    colorBuildDescription.profile = tt::profiles::Color;
    ByteArray colorBuildSettings(memory::pools::Assets::GetInstance());
    Check(tt::EncodeTextureBuildSettings(colorBuildDescription, colorBuildSettings) == tt::TextureBuildSettingsResult::Success,
          "encode alternate-profile settings for cache invalidation proof");
    const vanguard::assets::BuildRequest colorTextureRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, colorBuildSettings};
    vanguard::assets::BuildPlan colorTexturePlan;
    vanguard::assets::BuildOutput colorTextureOutput;
    Check(textureBuildSystem.Prepare(colorTextureRequest, colorTexturePlan) == vanguard::assets::Result::Success &&
              textureBuildSystem.Execute(colorTextureRequest, colorTexturePlan, colorTextureOutput) == vanguard::assets::Result::Success &&
              colorTextureOutput.disposition == vanguard::assets::BuildDisposition::Built &&
              !(colorTextureOutput.buildFingerprint == textureBuildOutput.buildFingerprint),
          "profile selection and its frozen tool identity invalidate the texture build cache");

    const vanguard::assets::BuildRequest cancelledTextureRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::LinuxVulkan, textureBuildSettings};
    vanguard::assets::BuildPlan cancelledTexturePlan;
    vanguard::assets::BuildOutput cancelledTextureOutput;
    CancellationCounter cancellationCounter{0, 2};
    Check(textureBuildSystem.Prepare(cancelledTextureRequest, cancelledTexturePlan) == vanguard::assets::Result::Success &&
              textureBuildSystem.Execute(cancelledTextureRequest, cancelledTexturePlan, cancelledTextureOutput, CancelAfterChecks,
                                         &cancellationCounter) == vanguard::assets::Result::Cancelled &&
              cancelledTextureOutput.artifacts.Empty() && cancelledTextureOutput.contentFingerprint.IsEmpty(),
          "cancellation during texture cooking exposes no partial artifact set and stores no cache entry");
    vanguard::assets::BuildOutput retriedTextureOutput;
    Check(textureBuildSystem.Execute(cancelledTextureRequest, cancelledTexturePlan, retriedTextureOutput) == vanguard::assets::Result::Success &&
              retriedTextureOutput.disposition == vanguard::assets::BuildDisposition::Built,
          "a cancelled texture build can be retried and rebuilt cleanly");

    std::array<vanguard::u8, 70> oversizedPng = redPng;
    oversizedPng[16] = 0;
    oversizedPng[17] = 2;
    oversizedPng[18] = 0;
    oversizedPng[19] = 1;
    const vanguard::assets::BuildRequest oversizedTextureRequest{
        {textureSource, {oversizedPng.data(), static_cast<vanguard::u32>(oversizedPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, textureBuildSettings};
    vanguard::assets::BuildPlan oversizedTexturePlan;
    Check(textureBuildSystem.Prepare(oversizedTextureRequest, oversizedTexturePlan) == vanguard::assets::Result::ResourceEstimationFailed,
          "oversized source dimensions are rejected by bounded planning before decoder allocation");

    ByteArray mismatchedSettings(memory::pools::Assets::GetInstance());
    tt::TextureBuildDescription mismatchedDescription;
    mismatchedDescription.sourceMode = tt::TextureBuildSourceMode::PreservedDds;
    mismatchedDescription.routeFlags = tt::TextureBuildRouteFlags::Streamable;
    mismatchedDescription.ddsMipTailCount = 4;
    mismatchedDescription.profile = 0;
    Check(tt::EncodeTextureBuildSettings(mismatchedDescription, mismatchedSettings) == tt::TextureBuildSettingsResult::Success,
          "encode canonical preserved-DDS recipe");
    const vanguard::assets::BuildRequest mismatchedRequest{
        {textureSource, {redPng.data(), static_cast<vanguard::u32>(redPng.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, mismatchedSettings};
    vanguard::assets::BuildPlan mismatchedPlan;
    Check(textureBuildSystem.Prepare(mismatchedRequest, mismatchedPlan) == vanguard::assets::Result::ResourceEstimationFailed,
          "authoritative source mode rejects PNG bytes presented as preserved DDS before cooking");

    const std::array<vanguard::u8, 184> compilerDds = MakeBc1Dds();
    tt::TextureBuildDescription ddsBuildDescription;
    ddsBuildDescription.sourceMode = tt::TextureBuildSourceMode::PreservedDds;
    ddsBuildDescription.routeFlags = tt::TextureBuildRouteFlags::Streamable;
    ddsBuildDescription.ddsMipTailCount = 2;
    ddsBuildDescription.profile = 0;
    ByteArray ddsBuildSettings(memory::pools::Assets::GetInstance());
    Check(tt::EncodeTextureBuildSettings(ddsBuildDescription, ddsBuildSettings) == tt::TextureBuildSettingsResult::Success,
          "encode strict preserved-DDS compiler settings");
    const vanguard::assets::BuildRequest ddsBuildRequest{
        {textureSource, {compilerDds.data(), static_cast<vanguard::u32>(compilerDds.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, ddsBuildSettings};
    vanguard::assets::BuildOutput ddsBuildOutput;
    Check(textureBuildSystem.Build(ddsBuildRequest, ddsBuildOutput) == vanguard::assets::Result::Success && ddsBuildOutput.artifacts.Size() == 5 &&
              vanguard::assets::HasFlag(ddsBuildOutput.artifacts[0].flags, vanguard::assets::ArtifactFlags::Primary) &&
              !vanguard::assets::HasFlag(ddsBuildOutput.artifacts[1].flags, vanguard::assets::ArtifactFlags::MemoryResident) &&
              vanguard::assets::HasFlag(ddsBuildOutput.artifacts[3].flags, vanguard::assets::ArtifactFlags::MemoryResident) &&
              vanguard::assets::HasFlag(ddsBuildOutput.artifacts[4].flags, vanguard::assets::ArtifactFlags::MemoryResident),
          "preserved DDS compiles without filtering into independent high mips and a resident tail");
    ByteArray reconstructedDdsTexture(memory::pools::Assets::GetInstance());
    bool denseDdsArtifactSet = true;
    for (vanguard::u32 segment = 0; denseDdsArtifactSet && segment < ddsBuildOutput.artifacts.Size(); ++segment)
    {
        const vanguard::assets::Artifact& artifact = ddsBuildOutput.artifacts[segment];
        denseDdsArtifactSet = artifact.resource == textureOutput && artifact.segment == segment;
        for (vanguard::u32 byte = 0; denseDdsArtifactSet && byte < artifact.bytes.Size(); ++byte)
            reconstructedDdsTexture.PushBack(artifact.bytes[byte]);
    }
    filesystem::MemoryFileReader reconstructedDdsReader(reconstructedDdsTexture, 0);
    textures::TextureFile reconstructedDdsFile;
    Check(denseDdsArtifactSet && reconstructedDdsFile.Open(reconstructedDdsReader) == textures::Result::Success &&
              reconstructedDdsFile.Format() == textures::PixelFormat::BC1UNorm && reconstructedDdsFile.GetMipCount() == 4,
          "preserved-DDS artifact boundaries reconstruct one valid complete VTEX");
    const vanguard::assets::BuildRequest ddsAsImageRequest{
        {textureSource, {compilerDds.data(), static_cast<vanguard::u32>(compilerDds.size())}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, textureBuildSettings};
    vanguard::assets::BuildPlan ddsAsImagePlan;
    Check(textureBuildSystem.Prepare(ddsAsImageRequest, ddsAsImagePlan) == vanguard::assets::Result::ResourceEstimationFailed,
          "authoritative image route rejects DDS source bytes before cooking");

    tt::ImportedTexture rejectedPng;
    Check(tt::ImportTexture({{redPng.data(), 48}, "png"}, rejectedPng) == tt::TextureImportResult::DecodeFailure && !rejectedPng.IsValid(),
          "truncated PNG input is rejected without partial output");

    ByteArray jpegEncoded(memory::pools::Assets::GetInstance());
    const filesystem::AbsolutePath jpegFixture =
        root.AddDirPath("source").AddDirPath("textureTools").AddDirPath("tests").AddDirPath("data").AddFilePath("libjpeg_rgb.jpg");
    Check(filesystem::LoadFileToBuffer(jpegFixture, jpegEncoded), "load JPEG conformance fixture through Vanguard filesystem");
    tt::ImportedTexture jpegImported;
    tt::TextureImportReport jpegReport;
    const tt::TextureImportRequest jpegRequest{
        {jpegEncoded.TypedData(), jpegEncoded.Size()}, "jpg", tt::TextureUsage::Color, tt::ImportedColorSpace::Automatic};
    Check(tt::ImportTexture(jpegRequest, jpegImported, &jpegReport) == tt::TextureImportResult::Success && jpegReport.importer == tt::importers::Jpeg &&
              jpegReport.width != 0 && jpegReport.height != 0 && jpegReport.decodedFormat == tt::SourcePixelFormat::R8G8B8A8UNorm &&
              jpegImported.GetImageData()[3] == 255,
          "libjpeg-turbo imports JPEG into deterministic RGBA8 source data");
    const vanguard::assets::BuildRequest changedSourceRequest{
        {textureSource, {jpegEncoded.TypedData(), jpegEncoded.Size()}, {}}, textureOutput,
        vanguard::assets::TargetPlatform::WindowsD3D12, textureBuildSettings};
    vanguard::assets::BuildPlan changedSourcePlan;
    vanguard::assets::BuildOutput changedSourceOutput;
    Check(textureBuildSystem.Prepare(changedSourceRequest, changedSourcePlan) == vanguard::assets::Result::Success &&
              textureBuildSystem.Execute(changedSourceRequest, changedSourcePlan, changedSourceOutput) == vanguard::assets::Result::Success &&
              changedSourceOutput.disposition == vanguard::assets::BuildDisposition::Built &&
              !(changedSourceOutput.buildFingerprint == textureBuildOutput.buildFingerprint),
          "changed encoded source bytes invalidate the texture build cache");
    Check(textureCompiler.Shutdown() && textureBuildSystem.Shutdown(), "shutdown texture compiler and generic build system cleanly");
    tt::TextureImportRequest constrainedJpegRequest = jpegRequest;
    constrainedJpegRequest.limits.maximumDimension = 1;
    tt::ImportedTexture constrainedJpeg;
    Check(tt::ImportTexture(constrainedJpegRequest, constrainedJpeg) == tt::TextureImportResult::LimitExceeded && !constrainedJpeg.IsValid(),
          "JPEG dimensions are budgeted before decoded storage allocation");

    tt::tests::EncodedFixture tiffFixture;
    Check(tt::tests::MakeTiledTiff16(tiffFixture), "create compressed tiled 16-bit TIFF conformance fixture");
    const tt::TextureImportRequest tiffRequest{
        {tiffFixture.bytes.data(), static_cast<vanguard::u32>(tiffFixture.size)}, "tiff", tt::TextureUsage::Data, tt::ImportedColorSpace::Automatic};
    tt::ImportedTexture tiffImported;
    tt::TextureImportReport tiffReport;
    Check(tt::ImportTexture(tiffRequest, tiffImported, &tiffReport) == tt::TextureImportResult::Success && tiffReport.importer == tt::importers::Tiff &&
              tiffReport.width == 16 && tiffReport.height == 16 && tiffReport.decodedFormat == tt::SourcePixelFormat::R16G16B16A16UNorm &&
              ReadU16(tiffImported.GetImageData()) == 15000 && ReadU16(tiffImported.GetImageData() + 2) == 15 && ReadU16(tiffImported.GetImageData() + 4) == 65535 &&
              ReadU16(tiffImported.GetImageData() + 6) == 65535,
          "libtiff preserves 16-bit channels, decodes LZW tiles, and normalizes top-right orientation");
    tt::TextureImportRequest constrainedTiffRequest = tiffRequest;
    constrainedTiffRequest.limits.maximumDimension = 8;
    tt::ImportedTexture constrainedTiff;
    Check(tt::ImportTexture(constrainedTiffRequest, constrainedTiff) == tt::TextureImportResult::LimitExceeded && !constrainedTiff.IsValid(),
          "TIFF dimensions are rejected before decoded image allocation");

    tt::tests::EncodedFixture floatTiffFixture;
    Check(tt::tests::MakeScanlineTiffFloat(floatTiffFixture), "create Deflate-compressed float TIFF fixture");
    tt::ImportedTexture floatTiffImported;
    const tt::TextureImportRequest floatTiffRequest{
        {floatTiffFixture.bytes.data(), static_cast<vanguard::u32>(floatTiffFixture.size)}, "tif", tt::TextureUsage::Hdr, tt::ImportedColorSpace::Automatic};
    Check(tt::ImportTexture(floatTiffRequest, floatTiffImported) == tt::TextureImportResult::Success &&
              floatTiffImported.GetSource().format == tt::SourcePixelFormat::R32G32B32A32Float && ReadFloat(floatTiffImported.GetImageData()) == -2.0f &&
              ReadFloat(floatTiffImported.GetImageData() + 4) == 0.5f && ReadFloat(floatTiffImported.GetImageData() + 8) == 8.0f &&
              ReadFloat(floatTiffImported.GetImageData() + 12) == 1.0f,
          "libtiff preserves negative and above-one HDR float samples through Deflate decoding");

    tt::tests::EncodedFixture exrFixture;
    Check(tt::tests::MakeScanlineOpenExrFloat(exrFixture), "create ZIP-compressed float OpenEXR conformance fixture");
    const tt::TextureImportRequest exrRequest{
        {exrFixture.bytes.data(), static_cast<vanguard::u32>(exrFixture.size)}, "exr", tt::TextureUsage::Hdr, tt::ImportedColorSpace::Automatic};
    tt::ImportedTexture exrImported;
    tt::TextureImportReport exrReport;
    Check(tt::ImportTexture(exrRequest, exrImported, &exrReport) == tt::TextureImportResult::Success && exrReport.importer == tt::importers::OpenExr &&
              exrReport.width == 2 && exrReport.height == 2 && exrReport.decodedFormat == tt::SourcePixelFormat::R32G32B32A32Float &&
              exrImported.GetSource().colorSpace == textures::ColorSpace::Linear && ReadFloat(exrImported.GetImageData()) == 0.25f &&
              ReadFloat(exrImported.GetImageData() + 4) == 0.5f && ReadFloat(exrImported.GetImageData() + 16) == 2.0f &&
              ReadFloat(exrImported.GetImageData() + 60) == 0.25f,
          "OpenEXR preserves signed HDR float values and channel identity through ZIP scanline decoding");
    tt::ImportedTexture truncatedExr;
    Check(tt::ImportTexture({{exrFixture.bytes.data(), static_cast<vanguard::u32>(exrFixture.size / 2u)}, "exr"}, truncatedExr) ==
                  tt::TextureImportResult::DecodeFailure &&
              !truncatedExr.IsValid(),
          "truncated OpenEXR is rejected without exposing partial output");

    const std::array<vanguard::u8, 184> bc1Dds = MakeBc1Dds();
    const tt::TextureImportRequest ddsRequest{
        {bc1Dds.data(), static_cast<vanguard::u32>(bc1Dds.size())}, "dds", tt::TextureUsage::Color, tt::ImportedColorSpace::SRgb};
    tt::ImportedGpuTexture importedDds;
    tt::GpuTextureImportReport ddsReport;
    Check(tt::ImportDdsTexture(ddsRequest, importedDds, &ddsReport) == tt::TextureImportResult::Success && importedDds.IsValid() &&
              importedDds.Importer() == tt::importers::Dds && ddsReport.format == textures::PixelFormat::BC1UNorm &&
              ddsReport.colorSpace == textures::ColorSpace::SRgb && ddsReport.width == 8 && ddsReport.height == 8 && ddsReport.mipCount == 4 &&
              ddsReport.subresourceCount == 4 && ddsReport.payloadBytes == 56,
          "DDS imports a complete BC1 mip chain as byte-exact GPU subresources");
    const tt::GpuTextureSource ddsSource = importedDds.GetSource();
    Check(ddsSource.subresources.Count() == 4 && ddsSource.subresources[0].rowPitch == 16 && ddsSource.subresources[0].slicePitch == 32 &&
              ddsSource.subresources[1].rowPitch == 8 && ddsSource.subresources[3].byteSize == 8 &&
              static_cast<const vanguard::u8*>(ddsSource.subresources[0].data)[0] == bc1Dds[128] &&
              static_cast<const vanguard::u8*>(ddsSource.subresources[3].data)[7] == bc1Dds[183],
          "DDS subresource boundaries and payload bytes are preserved");
    ByteArray ddsVtexBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter ddsVtexWriter(ddsVtexBytes);
    Check(tt::CookGpuTexture(ddsSource, ddsVtexWriter) == tt::Result::Success, "precompressed DDS payload emits directly into vtex without recompression");
    filesystem::MemoryFileReader ddsVtexReader(ddsVtexBytes, 0);
    textures::TextureFile ddsVtex;
    ByteArray ddsMip(memory::pools::Assets::GetInstance());
    ddsMip.Resize(32);
    Check(ddsVtex.Open(ddsVtexReader) == textures::Result::Success && ddsVtex.Format() == textures::PixelFormat::BC1UNorm &&
              ddsVtex.GetSpace() == textures::ColorSpace::SRgb && ddsVtex.GetMipCount() == 4 && ddsVtex.GetSubresources().Count() == 4 &&
              ddsVtex.ReadSubresource(ddsVtexReader, ddsVtex.FindSubresource(0), ddsMip.TypedData(), ddsMip.Size()) == textures::Result::Success,
          "reopen the DDS-derived vtex and read its highest-resolution mip");
    bool ddsMipMatches = ddsMip.Size() == 32;
    for (vanguard::u32 byte = 0; ddsMipMatches && byte < ddsMip.Size(); ++byte)
        ddsMipMatches = ddsMip[byte] == bc1Dds[128 + byte];
    Check(ddsMipMatches, "vtex preserves imported DDS blocks byte for byte");

    tt::ImportedGpuTexture rejectedDds;
    Check(tt::ImportDdsTexture({{bc1Dds.data(), static_cast<vanguard::u32>(bc1Dds.size() - 1)}, "dds"}, rejectedDds) ==
                  tt::TextureImportResult::DecodeFailure &&
              !rejectedDds.IsValid(),
          "reject a truncated DDS payload without partial output");
    std::array<vanguard::u8, 184> invalidMipDds = bc1Dds;
    WriteU32(invalidMipDds, 28, 5);
    Check(tt::ImportDdsTexture({{invalidMipDds.data(), static_cast<vanguard::u32>(invalidMipDds.size())}, "dds"}, rejectedDds) ==
              tt::TextureImportResult::LimitExceeded,
          "reject a DDS mip declaration beyond the texture's complete chain");
    std::array<vanguard::u8, 184> premultipliedDds = bc1Dds;
    WriteU32(premultipliedDds, 84, 0x32545844u); // DXT2
    Check(tt::ImportDdsTexture({{premultipliedDds.data(), static_cast<vanguard::u32>(premultipliedDds.size())}, "dds"}, rejectedDds) ==
              tt::TextureImportResult::UnsupportedFormat,
          "reject premultiplied DDS data whose alpha mode cannot be represented by vtex");
    tt::TextureImportRequest swizzledDdsRequest = ddsRequest;
    swizzledDdsRequest.channels = {tt::ImportedChannel::Blue, tt::ImportedChannel::Green, tt::ImportedChannel::Red, tt::ImportedChannel::Alpha};
    Check(tt::ImportDdsTexture(swizzledDdsRequest, rejectedDds) == tt::TextureImportResult::InvalidArgument,
          "reject channel remapping of byte-exact GPU blocks");

    const std::array<vanguard::u8, 244> bc7CubeDds = MakeBc7CubeDds();
    tt::ImportedGpuTexture importedBc7Cube;
    tt::GpuTextureImportReport bc7CubeReport;
    Check(tt::ImportDdsTexture({{bc7CubeDds.data(), static_cast<vanguard::u32>(bc7CubeDds.size())}, "dds"}, importedBc7Cube, &bc7CubeReport) ==
                  tt::TextureImportResult::Success &&
              importedBc7Cube.IsValid() && bc7CubeReport.dimension == textures::TextureDimension::Cube &&
              bc7CubeReport.format == textures::PixelFormat::BC7UNorm && bc7CubeReport.colorSpace == textures::ColorSpace::SRgb &&
              bc7CubeReport.arrayLayers == 1 && bc7CubeReport.subresourceCount == 6 && importedBc7Cube.GetSource().subresources[5].face == 5,
          "DX10 DDS preserves BC7 sRGB cube metadata and canonical six-face payloads");
    std::array<vanguard::u8, 244> nonSquareCubeDds = bc7CubeDds;
    WriteU32(nonSquareCubeDds, 12, 2);
    Check(tt::ImportDdsTexture({{nonSquareCubeDds.data(), static_cast<vanguard::u32>(nonSquareCubeDds.size())}, "dds"}, rejectedDds) ==
              tt::TextureImportResult::DecodeFailure,
          "reject a non-square DX10 cube before exposing subresources");

    const std::array<vanguard::u8, 8> rgba16Pixel{{0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0xff, 0xff}};
    const tt::SourceImage rgba16Image{rgba16Pixel.data(), rgba16Pixel.size(), 8, 8};
    const tt::SourceTexture rgba16Source{
        textures::TextureDimension::Texture2D, tt::SourcePixelFormat::R16G16B16A16UNorm, textures::ColorSpace::Linear, 1, 1, 1, 1, {}, {&rgba16Image, 1}};
    ByteArray rgba16Bytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter rgba16Writer(rgba16Bytes);
    Check(tt::CookTexture(rgba16Source, rgba16Writer, {tt::profiles::Ui}) == tt::Result::Success, "cook precision-preserving RGBA16 UNorm importer output");

    std::array<tt::ImportedTexture, 6> importedCubeFaces;
    for (vanguard::u32 face = 0; face < 6; ++face)
    {
        std::array<vanguard::u8, 24> encodedFace = rawEncoded;
        for (vanguard::usize byte = 8; byte < encodedFace.size(); ++byte)
            encodedFace[byte] = static_cast<vanguard::u8>(face * 31u + byte);
        const tt::TextureImportRequest faceRequest{
            {encodedFace.data(), static_cast<vanguard::u32>(encodedFace.size())}, "vraw", tt::TextureUsage::Data, tt::ImportedColorSpace::Linear};
        Check(tt::ImportTexture(faceRequest, importedCubeFaces[face]) == tt::TextureImportResult::Success, "import individual cube source face");
    }
    tt::ImportedTexture assembledCube;
    Check(tt::AssembleCubeFaces({importedCubeFaces.data(), static_cast<vanguard::u32>(importedCubeFaces.size())}, tt::TextureUsage::Data, assembledCube) ==
                  tt::TextureImportResult::Success &&
              assembledCube.GetSource().dimension == textures::TextureDimension::Cube && assembledCube.GetSource().images.Count() == 6 &&
              assembledCube.GetImageData(5)[0] != assembledCube.GetImageData(0)[0],
          "assemble six imported images into canonical cube face order");
    ByteArray assembledCubeBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter assembledCubeWriter(assembledCubeBytes);
    Check(tt::CookTexture(assembledCube.GetSource(), assembledCubeWriter, {assembledCube.GetRecommendedProfile()}) == tt::Result::Success,
          "assembled imported cube feeds seamless vtex cooking");

    std::array<vanguard::u8, 8 + 4 * 3 * 4> horizontalCrossBytes{};
    horizontalCrossBytes[0] = 'V';
    horizontalCrossBytes[1] = 'R';
    horizontalCrossBytes[2] = 'A';
    horizontalCrossBytes[3] = 'W';
    horizontalCrossBytes[4] = 4;
    horizontalCrossBytes[5] = 3;
    for (vanguard::u32 pixel = 0; pixel < 12; ++pixel)
    {
        horizontalCrossBytes[8 + pixel * 4] = static_cast<vanguard::u8>(pixel);
        horizontalCrossBytes[8 + pixel * 4 + 3] = 255;
    }
    tt::ImportedTexture horizontalCross;
    const tt::TextureImportRequest horizontalCrossRequest{
        {horizontalCrossBytes.data(), static_cast<vanguard::u32>(horizontalCrossBytes.size())}, "vraw", tt::TextureUsage::Data, tt::ImportedColorSpace::Linear};
    Check(tt::ImportTexture(horizontalCrossRequest, horizontalCross) == tt::TextureImportResult::Success, "import horizontal cube-cross source");
    tt::ImportedTexture extractedCrossCube;
    Check(tt::ExtractCubeCross(horizontalCross, tt::CubeCrossLayout::Horizontal, tt::TextureUsage::Data, extractedCrossCube) ==
                  tt::TextureImportResult::Success &&
              extractedCrossCube.GetImageData(0)[0] == 6 && extractedCrossCube.GetImageData(1)[0] == 4 && extractedCrossCube.GetImageData(2)[0] == 1 &&
              extractedCrossCube.GetImageData(3)[0] == 9 && extractedCrossCube.GetImageData(4)[0] == 5 && extractedCrossCube.GetImageData(5)[0] == 7,
          "horizontal cube cross maps cells to canonical face order");

    std::array<vanguard::u8, 8 * 8 * 4> pixels{};
    for (vanguard::u32 y = 0; y < 8; ++y)
    {
        for (vanguard::u32 x = 0; x < 8; ++x)
        {
            const vanguard::u32 offset = (y * 8 + x) * 4;
            pixels[offset + 0] = static_cast<vanguard::u8>(x * 31);
            pixels[offset + 1] = static_cast<vanguard::u8>(y * 31);
            pixels[offset + 2] = static_cast<vanguard::u8>((x + y) * 15);
            pixels[offset + 3] = (x + y) % 3 == 0 ? 0 : 255;
        }
    }
    const tt::SourceImage image{pixels.data(), pixels.size(), 8 * 4, 8 * 8 * 4};
    const tt::SourceTexture source{textures::TextureDimension::Texture2D,
                                   tt::SourcePixelFormat::R8G8B8A8UNorm,
                                   textures::ColorSpace::SRgb,
                                   8,
                                   8,
                                   1,
                                   1,
                                   vanguard::crypto::Sha256(pixels.data(), pixels.size()),
                                   {&image, 1}};
    ByteArray first(memory::pools::Assets::GetInstance());
    ByteArray second(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter firstWriter(first);
    filesystem::MemoryFileWriter secondWriter(second);
    tt::CookReport report;
    tt::CookSettings serialSettings;
    serialSettings.profile = tt::profiles::ColorAlpha;
    serialSettings.execution = tt::CookSettings::Execution::Serial;
    Check(tt::CookTexture(source, firstWriter, serialSettings, &report) == tt::Result::Success && !report.usedJobs, "cook BC7 color texture");
    ByteArray unavailableJobsBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter unavailableJobsWriter(unavailableJobsBytes);
    tt::CookSettings unavailableJobsSettings;
    unavailableJobsSettings.profile = tt::profiles::ColorAlpha;
    unavailableJobsSettings.execution = tt::CookSettings::Execution::Jobs;
    Check(tt::CookTexture(source, unavailableJobsWriter, unavailableJobsSettings) == tt::Result::InvalidState,
          "explicit Jobs cooking rejects an unavailable scheduler");
    vanguard::jobs::Config jobsConfig = vanguard::jobs::ToolConfig();
    jobsConfig.maxWorkers = 4;
    Check(vanguard::jobs::Initialize(jobsConfig), "Jobs initialization for parallel texture cooking");
    const vanguard::jobs::SchedulerStats schedulerBefore = vanguard::jobs::GetSchedulerStats();
    tt::CookSettings jobsSettings;
    jobsSettings.profile = tt::profiles::ColorAlpha;
    jobsSettings.execution = tt::CookSettings::Execution::Jobs;
    jobsSettings.maximumBlocksPerJobBatch = 1;
    tt::CookReport parallelReport;
    Check(tt::CookTexture(source, secondWriter, jobsSettings, &parallelReport) == tt::Result::Success && Equal(first, second),
          "parallel cooking is byte-identical to serial cooking");
    const vanguard::jobs::SchedulerStats schedulerAfter = vanguard::jobs::GetSchedulerStats();
    Check(parallelReport.usedJobs && parallelReport.encodedBlockCount == 7 && schedulerAfter.submittedJobs > schedulerBefore.submittedJobs,
          "BC blocks were dispatched through Vanguard Jobs");
    Check(report.targetFormat == textures::PixelFormat::BC7UNorm && report.mipCount == 4 && report.subresourceCount == 4, "color profile report");

    constexpr vanguard::u32 StressWidth = 257;
    constexpr vanguard::u32 StressHeight = 129;
    ByteArray stressPixels(memory::pools::Assets::GetInstance());
    stressPixels.Resize(StressWidth * StressHeight);
    for (vanguard::u32 index = 0; index < stressPixels.Size(); ++index)
        stressPixels[index] = static_cast<vanguard::u8>((index * 37u + index / StressWidth * 11u) & 255u);
    const tt::SourceImage stressImage{stressPixels.TypedData(), stressPixels.Size(), StressWidth, StressWidth * StressHeight};
    const tt::SourceTexture stressSource{textures::TextureDimension::Texture2D,
                                         tt::SourcePixelFormat::R8UNorm,
                                         textures::ColorSpace::Linear,
                                         StressWidth,
                                         StressHeight,
                                         1,
                                         1,
                                         {},
                                         {&stressImage, 1}};
    tt::CookSettings stressSerialSettings;
    stressSerialSettings.profile = tt::profiles::Data;
    stressSerialSettings.execution = tt::CookSettings::Execution::Serial;
    tt::CookSettings stressJobsSettings = stressSerialSettings;
    stressJobsSettings.execution = tt::CookSettings::Execution::Jobs;
    stressJobsSettings.maximumBlocksPerJobBatch = 32;
    ByteArray stressSerialBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter stressSerialWriter(stressSerialBytes);
    Check(tt::CookTexture(stressSource, stressSerialWriter, stressSerialSettings) == tt::Result::Success, "serial odd-size stress cook");
    for (vanguard::u32 iteration = 0; iteration < 4; ++iteration)
    {
        ByteArray stressJobsBytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter stressJobsWriter(stressJobsBytes);
        Check(tt::CookTexture(stressSource, stressJobsWriter, stressJobsSettings) == tt::Result::Success && Equal(stressSerialBytes, stressJobsBytes),
              "repeated Jobs stress cook remains deterministic");
    }
    Check(vanguard::jobs::GetOutstandingJobCount() == 0, "texture stress leaves no outstanding Jobs work");

    filesystem::MemoryFileReader reader(first, 0);
    textures::TextureFile texture;
    Check(texture.Open(reader) == textures::Result::Success, "open cooked vtex");
    Check(texture.Format() == textures::PixelFormat::BC7UNorm && texture.GetSpace() == textures::ColorSpace::SRgb, "runtime format and color space");
    Check(texture.GetMipCount() == 4 && texture.GetSubresources().Size() == 4 && texture.GetMipTailFirstLevel() == 0, "complete mip chain and tail");
    std::array<vanguard::u8, 64> bc7Mip{};
    const vanguard::u32 bc7MipIndex = texture.FindSubresource(0);
    std::array<bc7decomp::color_rgba, 16> decodedBc7{};
    Check(bc7MipIndex != textures::InvalidSubresourceIndex &&
              texture.ReadSubresource(reader, bc7MipIndex, bc7Mip.data(), bc7Mip.size()) == textures::Result::Success &&
              bc7decomp::unpack_bc7(bc7Mip.data(), decodedBc7.data()),
          "BC7 output passes independent block decoding");
    for (vanguard::u8 mip = 0; mip < 4; ++mip)
    {
        const vanguard::u32 index = texture.FindSubresource(mip);
        Check(index != textures::InvalidSubresourceIndex, "find cooked mip");
        if (index != textures::InvalidSubresourceIndex)
        {
            const textures::SubresourceRecord& record = texture.GetSubresources()[index];
            Check(record.rowPitch == textures::CalculateMinimumRowPitch(textures::PixelFormat::BC7UNorm, record.width), "direct-upload BC7 row pitch");
        }
    }

    std::array<vanguard::u8, 4 * 4 * 4> normals{};
    for (vanguard::u32 index = 0; index < 16; ++index)
    {
        normals[index * 4 + 0] = 128;
        normals[index * 4 + 1] = 128;
        normals[index * 4 + 2] = 255;
        normals[index * 4 + 3] = 255;
    }
    const tt::SourceImage normalImage{normals.data(), normals.size(), 16, 64};
    const tt::SourceTexture normalSource{
        textures::TextureDimension::Texture2D, tt::SourcePixelFormat::R8G8B8A8UNorm, textures::ColorSpace::Linear, 4, 4, 1, 1, {}, {&normalImage, 1}};
    ByteArray normalBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter normalWriter(normalBytes);
    Check(tt::CookTexture(normalSource, normalWriter, {tt::profiles::Normal}) == tt::Result::Success, "cook renormalized BC5 normal texture");
    filesystem::MemoryFileReader normalReader(normalBytes, 0);
    textures::TextureFile normalTexture;
    Check(normalTexture.Open(normalReader) == textures::Result::Success && normalTexture.Format() == textures::PixelFormat::BC5UNorm &&
              normalTexture.GetMipCount() == 3,
          "normal vtex contract");
    std::array<vanguard::u8, 16> compressedNormal{};
    std::array<vanguard::u8, 64> decodedNormal{};
    const vanguard::u32 normalMipIndex = normalTexture.FindSubresource(0);
    if (normalMipIndex != textures::InvalidSubresourceIndex)
    {
        Check(normalTexture.ReadSubresource(normalReader, normalMipIndex, compressedNormal.data(), compressedNormal.size()) == textures::Result::Success,
              "read BC5 normal block");
        rgbcx::unpack_bc5(compressedNormal.data(), decodedNormal.data(), 0, 1, 4);
        Check(decodedNormal[0] >= 120 && decodedNormal[0] <= 136 && decodedNormal[1] >= 120 && decodedNormal[1] <= 136,
              "BC5 normal block decodes near the source direction");
    }

    std::array<vanguard::f32, 4 * 4 * 4> hdrPixels{};
    for (vanguard::u32 index = 0; index < 16; ++index)
    {
        hdrPixels[index * 4 + 0] = static_cast<vanguard::f32>(index) * 0.5f;
        hdrPixels[index * 4 + 1] = 2.0f;
        hdrPixels[index * 4 + 2] = 0.25f;
        hdrPixels[index * 4 + 3] = 1.0f;
    }
    const tt::SourceImage hdrImage{hdrPixels.data(), sizeof(hdrPixels), 4 * 4 * sizeof(vanguard::f32), sizeof(hdrPixels)};
    const tt::SourceTexture hdrSource{
        textures::TextureDimension::Texture2D, tt::SourcePixelFormat::R32G32B32A32Float, textures::ColorSpace::Linear, 4, 4, 1, 1, {}, {&hdrImage, 1}};
    ByteArray hdrBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter hdrWriter(hdrBytes);
    Check(tt::CookTexture(hdrSource, hdrWriter, {tt::profiles::Hdr}) == tt::Result::Success, "cook BC6H HDR texture");
    filesystem::MemoryFileReader hdrReader(hdrBytes, 0);
    textures::TextureFile hdrTexture;
    Check(hdrTexture.Open(hdrReader) == textures::Result::Success && hdrTexture.Format() == textures::PixelFormat::BC6HUFloat, "BC6H HDR vtex contract");
    std::array<vanguard::u8, 16> compressedHdr{};
    std::array<vanguard::u16, 48> decodedHdr{};
    const vanguard::u32 hdrMipIndex = hdrTexture.FindSubresource(0);
    Check(hdrMipIndex != textures::InvalidSubresourceIndex &&
              hdrTexture.ReadSubresource(hdrReader, hdrMipIndex, compressedHdr.data(), compressedHdr.size()) == textures::Result::Success &&
              DecompressBlockBC6(compressedHdr.data(), decodedHdr.data(), nullptr) == 0,
          "BC6H output passes independent block decoding");

    for (vanguard::u32 profileIndex = 0; profileIndex < formatProfiles.size(); ++profileIndex)
    {
        const tt::TextureCookingProfile& formatProfile = formatProfiles[profileIndex];
        const tt::SourceTexture& formatSource = formatProfile.targetFormat == textures::PixelFormat::BC6HSFloat ? hdrSource : source;
        ByteArray formatBytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter formatWriter(formatBytes);
        Check(tt::CookTexture(formatSource, formatWriter, {formatProfile.id}) == tt::Result::Success, "cook registered runtime format");
        filesystem::MemoryFileReader formatReader(formatBytes, 0);
        textures::TextureFile formatTexture;
        Check(formatTexture.Open(formatReader) == textures::Result::Success && formatTexture.Format() == formatProfile.targetFormat,
              "registered runtime format survives vtex emission");
    }

    ByteArray bc2Bytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter bc2Writer(bc2Bytes);
    Check(tt::CookTexture(source, bc2Writer, {bc2Profile.id}) == tt::Result::Success, "cook project BC2 texture");
    filesystem::MemoryFileReader bc2Reader(bc2Bytes, 0);
    textures::TextureFile bc2Texture;
    std::array<vanguard::u8, 64> compressedBc2{};
    std::array<vanguard::u8, 64> decodedBc2{};
    Check(bc2Texture.Open(bc2Reader) == textures::Result::Success &&
              bc2Texture.ReadSubresource(bc2Reader, bc2Texture.FindSubresource(0), compressedBc2.data(), compressedBc2.size()) == textures::Result::Success &&
              DecompressBlockBC2(compressedBc2.data(), decodedBc2.data(), nullptr) == 0,
          "BC2 output passes independent block decoding");

    const std::array<vanguard::u8, 3> oddPixels{0, 0, 255};
    const tt::SourceImage oddImage{oddPixels.data(), oddPixels.size(), 3, 3};
    const tt::SourceTexture oddSource{
        textures::TextureDimension::Texture1D, tt::SourcePixelFormat::R8UNorm, textures::ColorSpace::Linear, 3, 1, 1, 1, {}, {&oddImage, 1}};
    ByteArray oddBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter oddWriter(oddBytes);
    tt::CookReport oddReport;
    Check(tt::CookTexture(oddSource, oddWriter, {oddMipProfile.id}, &oddReport) == tt::Result::Success && oddReport.generatedMipTexelCount == 1,
          "cook complete odd-extent mip footprint");
    filesystem::MemoryFileReader oddReader(oddBytes, 0);
    textures::TextureFile oddTexture;
    Check(oddTexture.Open(oddReader) == textures::Result::Success, "open odd-extent vtex");
    const vanguard::u32 oddMipIndex = oddTexture.FindSubresource(1);
    vanguard::u8 oddMipValue = 0;
    Check(oddMipIndex != textures::InvalidSubresourceIndex &&
              oddTexture.ReadSubresource(oddReader, oddMipIndex, &oddMipValue, 1) == textures::Result::Success && oddMipValue == 85,
          "3-to-1 mip includes every source texel");

    std::array<vanguard::u8, 27> volumePixels{};
    for (vanguard::u8 index = 0; index < volumePixels.size(); ++index)
        volumePixels[index] = index;
    const tt::SourceImage volumeImage{volumePixels.data(), volumePixels.size(), 3, 9};
    const tt::SourceTexture volumeSource{
        textures::TextureDimension::Texture3D, tt::SourcePixelFormat::R8UNorm, textures::ColorSpace::Linear, 3, 3, 3, 1, {}, {&volumeImage, 1}};
    ByteArray volumeBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter volumeWriter(volumeBytes);
    Check(tt::CookTexture(volumeSource, volumeWriter, {oddMipProfile.id}) == tt::Result::Success, "cook odd-extent 3D mip chain");
    filesystem::MemoryFileReader volumeReader(volumeBytes, 0);
    textures::TextureFile volumeTexture;
    vanguard::u8 volumeMipValue = 0;
    Check(volumeTexture.Open(volumeReader) == textures::Result::Success &&
              volumeTexture.ReadSubresource(volumeReader, volumeTexture.FindSubresource(1), &volumeMipValue, 1) == textures::Result::Success &&
              volumeMipValue == 13,
          "3D mip includes every source voxel");

    std::array<std::array<vanguard::u8, 16>, 6> cubePixels{};
    std::array<tt::SourceImage, 6> cubeImages{};
    for (vanguard::u32 face = 0; face < cubeImages.size(); ++face)
    {
        for (vanguard::u32 pixel = 0; pixel < 4; ++pixel)
        {
            cubePixels[face][pixel * 4 + 0] = static_cast<vanguard::u8>(face * 31);
            cubePixels[face][pixel * 4 + 1] = static_cast<vanguard::u8>(pixel * 63);
            cubePixels[face][pixel * 4 + 2] = 127;
            cubePixels[face][pixel * 4 + 3] = 255;
        }
        cubeImages[face] = {cubePixels[face].data(), cubePixels[face].size(), 8, 16};
    }
    const tt::SourceTexture cubeSource{
        textures::TextureDimension::Cube, tt::SourcePixelFormat::R8G8B8A8UNorm, textures::ColorSpace::SRgb, 2, 2, 1, 1, {}, {cubeImages.data(), 6}};
    ByteArray cubeBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter cubeWriter(cubeBytes);
    Check(tt::CookTexture(cubeSource, cubeWriter, {tt::profiles::Ui}) == tt::Result::Success, "cook six-face cube source");
    filesystem::MemoryFileReader cubeReader(cubeBytes, 0);
    textures::TextureFile cubeTexture;
    Check(cubeTexture.Open(cubeReader) == textures::Result::Success && cubeTexture.GetSubresources().Size() == 6 &&
              cubeTexture.FindSubresource(0, 0, 5) != textures::InvalidSubresourceIndex,
          "cube face ordering survives vtex emission");
    ByteArray compressedCubeMipBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter compressedCubeMipWriter(compressedCubeMipBytes);
    Check(tt::CookTexture(cubeSource, compressedCubeMipWriter, {tt::profiles::Color}) == tt::Result::Success,
          "cook compressed cube mip chain with direction-space filtering");

    constexpr vanguard::u32 CubeExtent = 8;
    std::array<std::array<vanguard::u8, CubeExtent * CubeExtent>, 6> seamlessCubePixels{};
    std::array<tt::SourceImage, 6> seamlessCubeImages{};
    for (vanguard::u32 face = 0; face < 6; ++face)
    {
        for (vanguard::u32 y = 0; y < CubeExtent; ++y)
            for (vanguard::u32 x = 0; x < CubeExtent; ++x)
                seamlessCubePixels[face][y * CubeExtent + x] = static_cast<vanguard::u8>((face * 29u + x * 11u + y * 17u) & 255u);
        seamlessCubeImages[face] = {seamlessCubePixels[face].data(), seamlessCubePixels[face].size(), CubeExtent, CubeExtent * CubeExtent};
    }
    const tt::SourceTexture seamlessCubeSource{
        textures::TextureDimension::Cube, tt::SourcePixelFormat::R8UNorm, textures::ColorSpace::Linear, CubeExtent, CubeExtent, 1, 1, {},
        {seamlessCubeImages.data(), 6}};
    ByteArray seamlessSerialBytes(memory::pools::Assets::GetInstance());
    ByteArray seamlessJobsBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter seamlessSerialWriter(seamlessSerialBytes);
    filesystem::MemoryFileWriter seamlessJobsWriter(seamlessJobsBytes);
    tt::CookSettings seamlessSerialSettings;
    seamlessSerialSettings.profile = oddMipProfile.id;
    seamlessSerialSettings.execution = tt::CookSettings::Execution::Serial;
    tt::CookSettings seamlessJobsSettings = seamlessSerialSettings;
    seamlessJobsSettings.execution = tt::CookSettings::Execution::Jobs;
    seamlessJobsSettings.maximumBlocksPerJobBatch = 1;
    tt::CookReport seamlessReport;
    const tt::Result seamlessSerialResult = tt::CookTexture(seamlessCubeSource, seamlessSerialWriter, seamlessSerialSettings, &seamlessReport);
    if (seamlessSerialResult != tt::Result::Success)
        std::fprintf(stderr, "[textureToolsTests] seamless cube result: %s\n", tt::ToString(seamlessSerialResult));
    Check(seamlessSerialResult == tt::Result::Success, "cook seamless uncompressed cube mip chain");
    Check(tt::CookTexture(seamlessCubeSource, seamlessJobsWriter, seamlessJobsSettings) == tt::Result::Success && Equal(seamlessSerialBytes, seamlessJobsBytes),
          "cube filtering is byte-identical between serial and Jobs execution");
    filesystem::MemoryFileReader seamlessReader(seamlessSerialBytes, 0);
    textures::TextureFile seamlessTexture;
    Check(seamlessTexture.Open(seamlessReader) == textures::Result::Success && seamlessTexture.GetMipCount() == 4 && seamlessTexture.GetSubresources().Size() == 24 &&
              seamlessReport.generatedMipTexelCount == 126,
          "seamless cube vtex contains every face and generated mip");

    enum CubeEdge : vanguard::u8
    {
        Left,
        Right,
        Top,
        Bottom
    };
    struct EdgePair
    {
        vanguard::u8 firstFace;
        CubeEdge firstEdge;
        vanguard::u8 secondFace;
        CubeEdge secondEdge;
        bool reversed;
    };
    const std::array<EdgePair, 12> edgePairs{{{0, Left, 4, Right, false},
                                              {0, Right, 5, Left, false},
                                              {0, Top, 2, Right, true},
                                              {0, Bottom, 3, Right, false},
                                              {1, Left, 5, Right, false},
                                              {1, Right, 4, Left, false},
                                              {1, Top, 2, Left, false},
                                              {1, Bottom, 3, Left, true},
                                              {2, Top, 5, Top, true},
                                              {2, Bottom, 4, Top, false},
                                              {3, Top, 4, Bottom, false},
                                              {3, Bottom, 5, Bottom, true}}};
    constexpr vanguard::u32 CheckedMip = 1;
    constexpr vanguard::u32 CheckedExtent = CubeExtent >> CheckedMip;
    std::array<std::array<vanguard::u8, CheckedExtent * CheckedExtent>, 6> checkedFaces{};
    for (vanguard::u8 face = 0; face < 6; ++face)
    {
        const vanguard::u32 index = seamlessTexture.FindSubresource(CheckedMip, 0, face);
        Check(index != textures::InvalidSubresourceIndex &&
                  seamlessTexture.ReadSubresource(seamlessReader, index, checkedFaces[face].data(), checkedFaces[face].size()) == textures::Result::Success,
              "read seamless cube face mip");
    }
    const auto edgeTexel = [&checkedFaces](const vanguard::u8 face, const CubeEdge edge, const vanguard::u32 coordinate)
    {
        const vanguard::u32 x = edge == Left ? 0u : (edge == Right ? CheckedExtent - 1u : coordinate);
        const vanguard::u32 y = edge == Top ? 0u : (edge == Bottom ? CheckedExtent - 1u : coordinate);
        return checkedFaces[face][y * CheckedExtent + x];
    };
    for (const EdgePair& pair : edgePairs)
    {
        for (vanguard::u32 coordinate = 0; coordinate < CheckedExtent; ++coordinate)
        {
            const vanguard::u32 secondCoordinate = pair.reversed ? CheckedExtent - 1u - coordinate : coordinate;
            Check(edgeTexel(pair.firstFace, pair.firstEdge, coordinate) == edgeTexel(pair.secondFace, pair.secondEdge, secondCoordinate),
                  "all cube mip edges agree across face orientation");
        }
    }
    std::array<vanguard::u8, 6> finalFaceValues{};
    for (vanguard::u8 face = 0; face < 6; ++face)
    {
        const vanguard::u32 index = seamlessTexture.FindSubresource(3, 0, face);
        Check(index != textures::InvalidSubresourceIndex &&
                  seamlessTexture.ReadSubresource(seamlessReader, index, &finalFaceValues[face], 1) == textures::Result::Success,
              "read final cube mip face");
        if (face != 0)
            Check(finalFaceValues[face] == finalFaceValues[0], "one-texel cube mip is globally continuous");
    }

    std::array<std::array<vanguard::u8, CubeExtent * CubeExtent>, 6> secondLayerPixels{};
    std::array<tt::SourceImage, 12> cubeArrayImages{};
    for (vanguard::u32 face = 0; face < 6; ++face)
    {
        cubeArrayImages[face] = seamlessCubeImages[face];
        for (vanguard::u32 pixel = 0; pixel < CubeExtent * CubeExtent; ++pixel)
            secondLayerPixels[face][pixel] = static_cast<vanguard::u8>(255u - seamlessCubePixels[face][pixel]);
        cubeArrayImages[6u + face] = {secondLayerPixels[face].data(), secondLayerPixels[face].size(), CubeExtent, CubeExtent * CubeExtent};
    }
    const tt::SourceTexture cubeArraySource{
        textures::TextureDimension::Cube, tt::SourcePixelFormat::R8UNorm, textures::ColorSpace::Linear, CubeExtent, CubeExtent, 1, 2, {},
        {cubeArrayImages.data(), 12}};
    ByteArray cubeArrayBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter cubeArrayWriter(cubeArrayBytes);
    Check(tt::CookTexture(cubeArraySource, cubeArrayWriter, seamlessSerialSettings) == tt::Result::Success,
          "cook two independently filtered cube-array layers");
    filesystem::MemoryFileReader cubeArrayReader(cubeArrayBytes, 0);
    textures::TextureFile cubeArrayTexture;
    vanguard::u8 firstLayerFinal = 0;
    vanguard::u8 secondLayerFinal = 0;
    Check(cubeArrayTexture.Open(cubeArrayReader) == textures::Result::Success && cubeArrayTexture.GetArrayLayers() == 2 &&
              cubeArrayTexture.GetSubresources().Size() == 48 &&
              cubeArrayTexture.ReadSubresource(cubeArrayReader, cubeArrayTexture.FindSubresource(3, 0, 0), &firstLayerFinal, 1) == textures::Result::Success &&
              cubeArrayTexture.ReadSubresource(cubeArrayReader, cubeArrayTexture.FindSubresource(3, 1, 0), &secondLayerFinal, 1) == textures::Result::Success &&
              firstLayerFinal != secondLayerFinal,
          "cube-array layers retain independent mip signals");

    const std::array<vanguard::u8, 16> nonFinitePixel{0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f};
    const tt::SourceImage nonFiniteImage{nonFinitePixel.data(), nonFinitePixel.size(), 16, 16};
    const tt::SourceTexture nonFiniteSource{
        textures::TextureDimension::Texture2D, tt::SourcePixelFormat::R32G32B32A32Float, textures::ColorSpace::Linear, 1, 1, 1, 1, {}, {&nonFiniteImage, 1}};
    ByteArray rejectedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter rejectedWriter(rejectedBytes);
    Check(tt::CookTexture(nonFiniteSource, rejectedWriter, {tt::profiles::Hdr}) == tt::Result::InvalidSourceData, "reject non-finite source components");

    const tt::TextureCookingProfile custom{
        0x7465737400000001ull, 1, textures::PixelFormat::R8UNorm, textures::ColorSpace::Linear, tt::TextureCookingFlags::None, 1, 0, 0.5f, 0};
    Check(tt::RegisterCookingProfile(custom) == tt::ProfileRegistrationResult::RegistrySealed, "profile registry seals after cooking begins");

    filesystem::Shutdown();
    Check(vanguard::jobs::Shutdown(), "Jobs shutdown after texture cooking");
    io::Shutdown();
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
