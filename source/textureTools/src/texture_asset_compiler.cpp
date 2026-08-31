#include <vanguard/texture_tools/texture_asset_compiler.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    using namespace vanguard;
    namespace assets = vanguard::assets;
    namespace textures = vanguard::textures;
    namespace tools = vanguard::texture_tools;

    constexpr u32 SettingsMagic = vanguard::serialization::MakeFourCC('V', 'T', 'C', 'B');
    constexpr u16 SettingsVersion = 1;
    constexpr u32 SettingsSize = 24;
    constexpr u8 KnownRouteFlags = static_cast<u8>(tools::TextureBuildRouteFlags::Streamable);
    constexpr u32 MaximumCompilerSubresources = 4095;
    constexpr u32 MaximumDimension = 131072;
    constexpr char CompilerName[] = "vanguard.texture.compiler";
    constexpr char CompilerToolPath[] = "tools/vanguard-texture-compiler";
    constexpr char ToolDomain[] = "vanguard.texture-compiler-tool.v1";

    struct ParsedSettings
    {
        tools::TextureBuildSourceMode mode = tools::TextureBuildSourceMode::Image2D;
        tools::ImportedColorSpace colorSpace = tools::ImportedColorSpace::Automatic;
        tools::TextureChannelMapping channels;
        tools::TextureBuildRouteFlags routeFlags = tools::TextureBuildRouteFlags::None;
        u8 ddsMipTailCount = 0;
        tools::TextureCookingProfileId profile = 0;
    };

    [[nodiscard]] bool IsIdentityMapping(const tools::TextureChannelMapping& mapping) noexcept
    {
        return mapping.red == tools::ImportedChannel::Red && mapping.green == tools::ImportedChannel::Green &&
               mapping.blue == tools::ImportedChannel::Blue && mapping.alpha == tools::ImportedChannel::Alpha;
    }

    [[nodiscard]] bool IsImageMode(const tools::TextureBuildSourceMode mode) noexcept
    {
        return mode == tools::TextureBuildSourceMode::Image2D || mode == tools::TextureBuildSourceMode::CubeCrossHorizontal ||
               mode == tools::TextureBuildSourceMode::CubeCrossVertical;
    }

    [[nodiscard]] bool ValidateSettings(const ParsedSettings& settings) noexcept
    {
        if (settings.mode > tools::TextureBuildSourceMode::PreservedDds || settings.colorSpace > tools::ImportedColorSpace::SRgb ||
            !settings.channels.IsValid() || (static_cast<u8>(settings.routeFlags) & ~KnownRouteFlags) != 0)
            return false;
        if (IsImageMode(settings.mode))
            return settings.profile != 0 && settings.routeFlags == tools::TextureBuildRouteFlags::None && settings.ddsMipTailCount == 0;
        const bool streamable = (static_cast<u8>(settings.routeFlags) & KnownRouteFlags) != 0;
        return settings.profile == 0 && IsIdentityMapping(settings.channels) &&
               ((streamable && settings.ddsMipTailCount != 0) || (!streamable && settings.ddsMipTailCount == 1));
    }

    [[nodiscard]] bool ParseSettings(const containers::ArraySpan<const u8> bytes, ParsedSettings& settings) noexcept
    {
        if (bytes.Size() != SettingsSize || bytes.Data() == nullptr)
            return false;
        filesystem::MemoryFileReader file(bytes.Data(), bytes.Size(), 0);
        vanguard::serialization::BinaryReader reader(file);
        u32 magic = 0;
        u16 version = 0;
        u16 reserved = 0;
        u8 mode = 0;
        u8 colorSpace = 0;
        u8 red = 0;
        u8 green = 0;
        u8 blue = 0;
        u8 alpha = 0;
        u8 flags = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(version) || !reader.ReadU16(reserved) || !reader.ReadU8(mode) ||
            !reader.ReadU8(colorSpace) || !reader.ReadU8(red) || !reader.ReadU8(green) || !reader.ReadU8(blue) || !reader.ReadU8(alpha) ||
            !reader.ReadU8(flags) || !reader.ReadU8(settings.ddsMipTailCount) || !reader.ReadU64(settings.profile) || reader.GetRemaining() != 0 ||
            magic != SettingsMagic || version != SettingsVersion || reserved != 0)
            return false;
        settings.mode = static_cast<tools::TextureBuildSourceMode>(mode);
        settings.colorSpace = static_cast<tools::ImportedColorSpace>(colorSpace);
        settings.channels = {static_cast<tools::ImportedChannel>(red), static_cast<tools::ImportedChannel>(green),
                             static_cast<tools::ImportedChannel>(blue), static_cast<tools::ImportedChannel>(alpha)};
        settings.routeFlags = static_cast<tools::TextureBuildRouteFlags>(flags);
        return ValidateSettings(settings);
    }

    [[nodiscard]] bool AddChecked(const u64 left, const u64 right, u64& result) noexcept
    {
        if (left > ~0ull - right)
            return false;
        result = left + right;
        return true;
    }

    [[nodiscard]] bool MultiplyChecked(const u64 left, const u64 right, u64& result) noexcept
    {
        if (left != 0 && right > ~0ull / left)
            return false;
        result = left * right;
        return true;
    }

    [[nodiscard]] bool AlignChecked(const u64 value, const u64 alignment, u64& result) noexcept
    {
        if (alignment == 0 || (alignment & (alignment - 1u)) != 0 || value > ~0ull - (alignment - 1u))
            return false;
        result = (value + alignment - 1u) & ~(alignment - 1u);
        return true;
    }

    [[nodiscard]] bool HashU8(crypto::Sha256Builder& builder, const u8 value) noexcept
    {
        return builder.Update(&value, sizeof(value));
    }

    [[nodiscard]] bool HashU16(crypto::Sha256Builder& builder, const u16 value) noexcept
    {
        const u8 bytes[2] = {static_cast<u8>(value), static_cast<u8>(value >> 8u)};
        return builder.Update(bytes, sizeof(bytes));
    }

    [[nodiscard]] bool HashU32(crypto::Sha256Builder& builder, const u32 value) noexcept
    {
        const u8 bytes[4] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        return builder.Update(bytes, sizeof(bytes));
    }

    [[nodiscard]] bool HashU64(crypto::Sha256Builder& builder, const u64 value) noexcept
    {
        return HashU32(builder, static_cast<u32>(value)) && HashU32(builder, static_cast<u32>(value >> 32u));
    }

    [[nodiscard]] crypto::Digest256 ToolFingerprint(const ParsedSettings& settings, const assets::TargetPlatform target,
                                                    const tools::TextureToolsConfigurationFingerprint& frozen) noexcept
    {
        crypto::Sha256Builder builder;
        u32 thresholdBits = 0;
        const tools::TextureCookingProfile* const profile = IsImageMode(settings.mode) ? tools::FindCookingProfile(settings.profile) : nullptr;
        if (!builder.Update(ToolDomain, sizeof(ToolDomain) - 1u) || !HashU32(builder, tools::TextureAssetCompilerVersion) ||
            !HashU8(builder, static_cast<u8>(target)) || !HashU8(builder, static_cast<u8>(settings.mode)) ||
            !builder.Update(frozen.importers.bytes, crypto::Digest256::ByteCount))
            return {};
        if (profile != nullptr)
        {
            std::memcpy(&thresholdBits, &profile->alphaCoverageThreshold, sizeof(thresholdBits));
            if (!builder.Update(frozen.profiles.bytes, crypto::Digest256::ByteCount) || !HashU64(builder, profile->id) ||
                !HashU32(builder, profile->version) || !HashU8(builder, static_cast<u8>(profile->targetFormat)) ||
                !HashU8(builder, static_cast<u8>(profile->targetColorSpace)) || !HashU16(builder, static_cast<u16>(profile->flags)) ||
                !HashU8(builder, profile->mipTailCount) || !HashU8(builder, profile->alphaCoverageChannel) ||
                !HashU32(builder, thresholdBits) || !HashU8(builder, profile->compressionQuality))
                return {};
        }
        else if (!HashU32(builder, 1) || !HashU8(builder, static_cast<u8>(settings.routeFlags)) || !HashU8(builder, settings.ddsMipTailCount))
            return {};
        crypto::Digest256 fingerprint;
        return builder.Finalize(fingerprint) ? fingerprint : crypto::Digest256{};
    }

    [[nodiscard]] bool RouteMatches(const ParsedSettings& settings, const tools::TextureSourceInspection& source) noexcept
    {
        return settings.mode == tools::TextureBuildSourceMode::PreservedDds ? source.kind == tools::TextureSourceKind::Dds
                                                                            : source.kind != tools::TextureSourceKind::Dds;
    }

    [[nodiscard]] bool OutputShape(const ParsedSettings& settings, const tools::TextureSourceInspection& source, u32& width, u32& height,
                                   u32& depth, u16& layers, u8& faces) noexcept
    {
        width = source.width;
        height = source.height;
        depth = source.depth;
        layers = source.arrayLayers;
        faces = source.faceCount;
        if (settings.mode == tools::TextureBuildSourceMode::CubeCrossHorizontal)
        {
            if (width % 4u != 0 || height % 3u != 0 || width / 4u != height / 3u)
                return false;
            width /= 4u;
            height /= 3u;
            faces = 6;
        }
        else if (settings.mode == tools::TextureBuildSourceMode::CubeCrossVertical)
        {
            if (width % 3u != 0 || height % 4u != 0 || width / 3u != height / 4u)
                return false;
            width /= 3u;
            height /= 4u;
            faces = 6;
        }
        return width != 0 && height != 0 && depth != 0 && layers != 0;
    }

    [[nodiscard]] bool Estimate(const assets::BuildRequest& request, const ParsedSettings& settings,
                                assets::BuildResourceEstimate& estimate) noexcept
    {
        tools::TextureSourceInspection source;
        if (tools::InspectTextureSource(request.source.content, source) != tools::TextureImportResult::Success || !RouteMatches(settings, source))
            return false;
        u32 width = 0, height = 0, depth = 0;
        u16 layers = 0;
        u8 faces = 0;
        if (!OutputShape(settings, source, width, height, depth, layers, faces) || width > MaximumDimension || height > MaximumDimension ||
            depth > MaximumDimension)
            return false;

        u8 mipCount = source.mipCount;
        textures::PixelFormat format = textures::PixelFormat::R8G8B8A8UNorm;
        if (IsImageMode(settings.mode))
        {
            const tools::TextureCookingProfile* const profile = tools::FindCookingProfile(settings.profile);
            if (profile == nullptr)
                return false;
            format = profile->targetFormat;
            mipCount = tools::HasFlag(profile->flags, tools::TextureCookingFlags::GenerateFullMipChain)
                           ? static_cast<u8>(textures::CalculateMipCount(width, height, depth))
                           : 1;
        }
        u64 imageCount = 0;
        u64 subresourceCount = 0;
        if (!MultiplyChecked(layers, faces, imageCount) || !MultiplyChecked(imageCount, mipCount, subresourceCount) ||
            subresourceCount == 0 || subresourceCount > MaximumCompilerSubresources)
            return false;

        u64 artifactBytes = 4096u;
        u64 metadataRecords = 0;
        if (!MultiplyChecked(subresourceCount, 128u, metadataRecords) || !AddChecked(artifactBytes, metadataRecords, artifactBytes))
            return false;
        if (settings.mode == tools::TextureBuildSourceMode::PreservedDds)
        {
            if (!AddChecked(artifactBytes, source.payloadBytes, artifactBytes))
                return false;
        }
        else
        {
            for (u8 mip = 0; mip < mipCount; ++mip)
            {
                const u32 mipWidth = textures::CalculateMipExtent(width, mip);
                const u32 mipHeight = textures::CalculateMipExtent(height, mip);
                const u32 mipDepth = textures::CalculateMipExtent(depth, mip);
                u64 bytes = textures::CalculateMinimumSlicePitch(format, mipWidth, mipHeight);
                if (!MultiplyChecked(bytes, mipDepth, bytes) || !MultiplyChecked(bytes, imageCount, bytes) ||
                    !AlignChecked(artifactBytes, 64u, artifactBytes) || !AddChecked(artifactBytes, bytes, artifactBytes))
                    return false;
            }
        }
        u64 baseTexels = 0;
        u64 baseFloatBytes = 0;
        if (!MultiplyChecked(width, height, baseTexels) || !MultiplyChecked(baseTexels, depth, baseTexels) ||
            !MultiplyChecked(baseTexels, imageCount, baseTexels) || !MultiplyChecked(baseTexels, 16u, baseFloatBytes))
            return false;
        u64 workingBytes = 0;
        if (settings.mode == tools::TextureBuildSourceMode::PreservedDds)
        {
            if (!MultiplyChecked(source.payloadBytes, 2u, workingBytes))
                return false;
        }
        else
        {
            u64 floatWorking = 0;
            u64 outputWorking = 0;
            if (!MultiplyChecked(baseFloatBytes, 3u, floatWorking) || !MultiplyChecked(artifactBytes, 2u, outputWorking) ||
                !AddChecked(source.decodedBytes, floatWorking, workingBytes) || !AddChecked(workingBytes, outputWorking, workingBytes))
                return false;
        }
        estimate.compilerTransientBytes = workingBytes;
        estimate.artifactBytes = artifactBytes;
        return estimate.IsValid();
    }

    [[nodiscard]] bool EmitArtifacts(const assets::CompileContext& context, assets::ArtifactWriter& artifacts,
                                     const containers::DynamicArray<u8>& bytes) noexcept
    {
        filesystem::MemoryFileReader reader(bytes, 0);
        textures::TextureFile texture;
        textures::ReadLimits limits;
        limits.maximumFileSize = artifacts.GetRemainingByteCount();
        limits.maximumTextureBytes = artifacts.GetRemainingByteCount();
        limits.maximumSubresources = artifacts.GetRemainingArtifactCount() > 0 ? artifacts.GetRemainingArtifactCount() - 1u : 0;
        if (texture.Open(reader, limits) != textures::Result::Success)
            return false;
        containers::DynamicArray<textures::StorageSegment> segments(memory::pools::Assets::GetInstance());
        if (textures::BuildStorageSegments(texture, bytes.Size(), segments, artifacts.GetRemainingArtifactCount()) != textures::Result::Success ||
            segments.Empty())
            return false;
        u64 expectedOffset = 0;
        for (u32 segmentIndex = 0; segmentIndex < segments.Size(); ++segmentIndex)
        {
            if (context.IsCancellationRequested())
                return false;
            const textures::StorageSegment& segment = segments[segmentIndex];
            if (segment.offset != expectedOffset || segment.byteSize == 0 || segment.byteSize > std::numeric_limits<u32>::max() ||
                segment.offset > bytes.Size() || segment.byteSize > bytes.Size() - segment.offset)
                return false;
            expectedOffset += segment.byteSize;
            assets::ArtifactFlags flags = assets::ArtifactFlags::Streamable;
            if ((static_cast<u8>(segment.flags) & static_cast<u8>(textures::StorageSegmentFlags::Metadata)) != 0)
                flags = assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident;
            else if ((static_cast<u8>(segment.flags) & static_cast<u8>(textures::StorageSegmentFlags::RequiredForMipTail)) != 0)
                flags = flags | assets::ArtifactFlags::MemoryResident;
            if (artifacts.Add(context.request.output, segmentIndex, flags, segment.alignmentLog2, bytes.TypedData() + segment.offset,
                              static_cast<usize>(segment.byteSize)) != assets::Result::Success)
                return false;
        }
        return expectedOffset == bytes.Size();
    }
} // namespace

namespace vanguard::texture_tools
{
    struct TextureAssetCompiler::Impl final
    {
        TextureToolsConfigurationFingerprint frozen;
        assets::BuildSystem* buildSystem = nullptr;
        assets::CompilerId compilerId = assets::InvalidCompilerId;
    };

    TextureBuildSettingsResult EncodeTextureBuildSettings(const TextureBuildDescription& description,
                                                           containers::DynamicArray<u8>& output) noexcept
    {
        output.Clear();
        const ParsedSettings settings{description.sourceMode, description.colorSpace, description.channels, description.routeFlags,
                                      description.ddsMipTailCount, description.profile};
        if (!ValidateSettings(settings) || (IsImageMode(settings.mode) && FindCookingProfile(settings.profile) == nullptr))
            return TextureBuildSettingsResult::InvalidArgument;
        output.Reserve(SettingsSize);
        if (output.Capacity() < SettingsSize)
            return TextureBuildSettingsResult::LimitExceeded;
        filesystem::MemoryFileWriter file(output);
        vanguard::serialization::BinaryWriter writer(file);
        const bool written = writer.WriteU32(SettingsMagic) && writer.WriteU16(SettingsVersion) && writer.WriteU16(0) &&
                             writer.WriteU8(static_cast<u8>(settings.mode)) && writer.WriteU8(static_cast<u8>(settings.colorSpace)) &&
                             writer.WriteU8(static_cast<u8>(settings.channels.red)) && writer.WriteU8(static_cast<u8>(settings.channels.green)) &&
                             writer.WriteU8(static_cast<u8>(settings.channels.blue)) && writer.WriteU8(static_cast<u8>(settings.channels.alpha)) &&
                             writer.WriteU8(static_cast<u8>(settings.routeFlags)) && writer.WriteU8(settings.ddsMipTailCount) &&
                             writer.WriteU64(settings.profile) && writer.Flush();
        if (!written || output.Size() != SettingsSize)
        {
            output.Clear();
            return TextureBuildSettingsResult::LimitExceeded;
        }
        return TextureBuildSettingsResult::Success;
    }

    namespace
    {
        [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            TextureAssetCompiler::Impl& implementation = *static_cast<TextureAssetCompiler::Impl*>(userData);
            ParsedSettings settings;
            if (!ParseSettings(request.settings, settings) || (IsImageMode(settings.mode) && FindCookingProfile(settings.profile) == nullptr))
                return false;
            const crypto::Digest256 fingerprint = ToolFingerprint(settings, request.target, implementation.frozen);
            const resources::ResourceReference tool(resources::ResourcePath::FromString(CompilerToolPath), TextureCompilerToolResourceType);
            return !fingerprint.IsEmpty() &&
                   dependencies.Add({tool, fingerprint, assets::DependencyRole::Tool, assets::DependencyRequirement::Required}) == assets::Result::Success;
        }

        [[nodiscard]] bool EstimateResources(const assets::BuildRequest& request, containers::ArraySpan<const assets::BuildDependency>,
                                             assets::BuildResourceEstimate& estimate, void*) noexcept
        {
            ParsedSettings settings;
            return ParseSettings(request.settings, settings) && Estimate(request, settings, estimate);
        }

        [[nodiscard]] bool CompileAsset(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void*) noexcept
        {
            if (context.IsCancellationRequested())
                return false;
            ParsedSettings settings;
            TextureSourceInspection inspection;
            if (!ParseSettings(context.request.settings, settings) ||
                InspectTextureSource(context.request.source.content, inspection) != TextureImportResult::Success || !RouteMatches(settings, inspection) ||
                artifacts.GetRemainingArtifactCount() < 2 || artifacts.GetRemainingByteCount() == 0)
                return false;

            const system::CancellationView cancellation{context.cancellation, context.cancellationUserData};
            TextureImportRequest importRequest;
            importRequest.encoded = context.request.source.content;
            importRequest.colorSpace = settings.colorSpace;
            importRequest.channels = settings.channels;
            importRequest.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.SizeInBytes());
            importRequest.limits.maximumDimension = MaximumDimension;
            importRequest.limits.maximumImages = MaximumCompilerSubresources;
            importRequest.limits.maximumDecodedBytes = artifacts.GetRemainingByteCount();
            importRequest.cancellation = cancellation;

            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            filesystem::MemoryFileWriter output(bytes);
            Result cookResult = Result::InvalidArgument;
            if (settings.mode == TextureBuildSourceMode::PreservedDds)
            {
                ImportedGpuTexture imported;
                if (ImportDdsTexture(importRequest, imported) != TextureImportResult::Success)
                    return false;
                GpuTextureCookSettings cook;
                cook.streamable = (static_cast<u8>(settings.routeFlags) & KnownRouteFlags) != 0;
                cook.mipTailCount = settings.ddsMipTailCount;
                cook.maximumOutputBytes = artifacts.GetRemainingByteCount();
                cook.cancellation = cancellation;
                cookResult = CookGpuTexture(imported.GetSource(), output, cook);
            }
            else
            {
                ImportedTexture imported;
                TextureImportReport report;
                if (ImportTexture(importRequest, imported, &report) != TextureImportResult::Success || inspection.kind == TextureSourceKind::Dds)
                    return false;
                ImportedTexture cube;
                SourceTexture source = imported.GetSource();
                if (settings.mode != TextureBuildSourceMode::Image2D)
                {
                    const CubeCrossLayout layout = settings.mode == TextureBuildSourceMode::CubeCrossHorizontal ? CubeCrossLayout::Horizontal
                                                                                                                : CubeCrossLayout::Vertical;
                    if (ExtractCubeCross(imported, layout, TextureUsage::Automatic, cube, importRequest.limits, cancellation) !=
                        TextureImportResult::Success)
                        return false;
                    source = cube.GetSource();
                }
                CookSettings cook;
                cook.profile = settings.profile;
                cook.maximumSubresources = artifacts.GetRemainingArtifactCount() - 1u;
                cook.maximumOutputBytes = artifacts.GetRemainingByteCount();
                cook.cancellation = cancellation;
                cookResult = CookTexture(source, output, cook);
            }
            if (cookResult != Result::Success || context.IsCancellationRequested())
            {
                VG_LOG_ERROR(diagnostics::Category::DataBuild, "Texture cook failed: %s", ToString(cookResult));
                return false;
            }
            return EmitArtifacts(context, artifacts, bytes) && !context.IsCancellationRequested();
        }
    } // namespace

    TextureAssetCompiler::~TextureAssetCompiler()
    {
        static_cast<void>(Shutdown());
    }

    bool TextureAssetCompiler::Initialize() noexcept
    {
        if (m_impl != nullptr || !texture_tools::IsInitialized())
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return false;
        if (!FreezeConfiguration(implementation->frozen))
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        implementation->compilerId = assets::HashCompilerName(CompilerName);
        m_impl = implementation;
        return true;
    }

    bool TextureAssetCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (m_impl->buildSystem != nullptr && Unregister() != assets::Result::Success)
            return false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureAssetCompiler::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    assets::CompilerDescriptor TextureAssetCompiler::GetDescriptor() noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->compilerId, CompilerName, TextureAssetCompilerVersion, TextureSourceResourceType, textures::TextureResourceType,
                Discover, CompileAsset, m_impl, EstimateResources};
    }

    assets::Result TextureAssetCompiler::Register(assets::BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem != nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = buildSystem.RegisterCompiler(GetDescriptor());
        if (result == assets::Result::Success)
            m_impl->buildSystem = &buildSystem;
        return result;
    }

    assets::Result TextureAssetCompiler::Unregister() noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem == nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = m_impl->buildSystem->UnregisterCompiler(m_impl->compilerId);
        if (result == assets::Result::Success)
            m_impl->buildSystem = nullptr;
        return result;
    }
} // namespace vanguard::texture_tools
