#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/memory/memory.hpp>

#define IMATH_HALF_NO_LOOKUP_TABLE
#include <half.h>
#include <tiffio.h>

#include <cstdio>

namespace vanguard::texture_tools
{
    [[nodiscard]] TextureCookingProfileId ResolveImportedProfile(TextureUsage usage, SourcePixelFormat format) noexcept;
}

namespace
{
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace textures = vanguard::textures;
    namespace tools = vanguard::texture_tools;
    using vanguard::f32;
    using vanguard::u8;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::usize;

    struct TiffMemory final
    {
        const u8* data = nullptr;
        u64 size = 0;
        u64 position = 0;
    };

    [[nodiscard]] tmsize_t ReadTiff(thandle_t handle, void* destination, const tmsize_t requested) noexcept
    {
        auto* const stream = static_cast<TiffMemory*>(handle);
        if (stream == nullptr || destination == nullptr || requested < 0 || stream->position > stream->size) return -1;
        const u64 available = stream->size - stream->position;
        const u64 count = static_cast<u64>(requested) < available ? static_cast<u64>(requested) : available;
        auto* const output = static_cast<u8*>(destination);
        for (u64 byte = 0; byte < count; ++byte) output[byte] = stream->data[stream->position + byte];
        stream->position += count;
        return static_cast<tmsize_t>(count);
    }

    [[nodiscard]] tmsize_t WriteTiff(thandle_t, void*, tmsize_t) noexcept { return 0; }

    [[nodiscard]] toff_t SeekTiff(thandle_t handle, const toff_t offset, const int origin) noexcept
    {
        auto* const stream = static_cast<TiffMemory*>(handle);
        if (stream == nullptr) return static_cast<toff_t>(-1);
        u64 base = 0;
        if (origin == SEEK_CUR) base = stream->position;
        else if (origin == SEEK_END) base = stream->size;
        else if (origin != SEEK_SET) return static_cast<toff_t>(-1);
        if (offset > ~u64{0} - base || base + offset > stream->size) return static_cast<toff_t>(-1);
        stream->position = base + offset;
        return stream->position;
    }

    [[nodiscard]] int CloseTiff(thandle_t) noexcept { return 0; }
    [[nodiscard]] toff_t SizeTiff(thandle_t handle) noexcept
    {
        const auto* const stream = static_cast<const TiffMemory*>(handle);
        return stream == nullptr ? 0 : stream->size;
    }
    [[nodiscard]] int MapTiff(thandle_t handle, void** base, toff_t* size) noexcept
    {
        const auto* const stream = static_cast<const TiffMemory*>(handle);
        if (stream == nullptr || base == nullptr || size == nullptr) return 0;
        *base = const_cast<u8*>(stream->data);
        *size = stream->size;
        return 1;
    }
    void UnmapTiff(thandle_t, void*, toff_t) noexcept {}
    [[nodiscard]] int IgnoreTiffMessage(TIFF*, void*, const char*, const char*, va_list) noexcept { return 1; }

    [[nodiscard]] bool IsHint(const containers::StringView hint, const char* expected) noexcept
    {
        u32 length = 0;
        while (expected[length] != '\0') ++length;
        if (hint.Length() != length) return false;
        for (u32 index = 0; index < length; ++index)
        {
            const char value = hint[index] >= 'A' && hint[index] <= 'Z'
                ? static_cast<char>(hint[index] - 'A' + 'a') : hint[index];
            if (value != expected[index]) return false;
        }
        return true;
    }

    [[nodiscard]] crypto::Digest256 Fingerprint(const tools::TextureImportRequest& request) noexcept
    {
        return request.sourceFingerprint.IsEmpty()
            ? crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes()) : request.sourceFingerprint;
    }

    [[nodiscard]] textures::ColorSpace ResolveColorSpace(const tools::TextureImportRequest& request,
                                                         const textures::ColorSpace automatic) noexcept
    {
        return request.colorSpace == tools::ImportedColorSpace::Linear ? textures::ColorSpace::Linear :
               request.colorSpace == tools::ImportedColorSpace::SRgb ? textures::ColorSpace::SRgb : automatic;
    }

    [[nodiscard]] u16 ReadU16(const u8* bytes) noexcept
    {
        return static_cast<u16>(bytes[0]) | static_cast<u16>(static_cast<u16>(bytes[1]) << 8u);
    }

    [[nodiscard]] f32 ReadF32(const u8* bytes) noexcept
    {
        union { u32 bits; f32 value; } converted{};
        converted.bits = static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8u |
                         static_cast<u32>(bytes[2]) << 16u | static_cast<u32>(bytes[3]) << 24u;
        return converted.value;
    }

    void WriteU16(u8* bytes, const u16 value) noexcept
    {
        bytes[0] = static_cast<u8>(value);
        bytes[1] = static_cast<u8>(value >> 8u);
    }

    void WriteF32(u8* bytes, const f32 value) noexcept
    {
        union { f32 value; u32 bits; } converted{value};
        bytes[0] = static_cast<u8>(converted.bits);
        bytes[1] = static_cast<u8>(converted.bits >> 8u);
        bytes[2] = static_cast<u8>(converted.bits >> 16u);
        bytes[3] = static_cast<u8>(converted.bits >> 24u);
    }

    [[nodiscard]] f32 ReadNormalized(const u8* sample, const u16 bits, const u16 sampleFormat) noexcept
    {
        if (sampleFormat == SAMPLEFORMAT_IEEEFP)
            return bits == 16 ? imath_half_to_float(ReadU16(sample)) : ReadF32(sample);
        return bits == 8 ? static_cast<f32>(sample[0]) / 255.0f : static_cast<f32>(ReadU16(sample)) / 65535.0f;
    }

    void WriteNormalized(u8* destination, const tools::SourcePixelFormat format, const u32 channel, f32 value) noexcept
    {
        if (format == tools::SourcePixelFormat::R8UNorm || format == tools::SourcePixelFormat::R8G8B8A8UNorm)
        {
            if (value < 0.0f) value = 0.0f;
            destination[channel] = static_cast<u8>(value >= 1.0f ? 255u : static_cast<u32>(value * 255.0f + 0.5f));
        }
        else if (format == tools::SourcePixelFormat::R16G16B16A16UNorm)
        {
            if (value < 0.0f) value = 0.0f;
            WriteU16(destination + channel * 2u,
                     static_cast<u16>(value >= 1.0f ? 65535u : static_cast<u32>(value * 65535.0f + 0.5f)));
        }
        else if (format == tools::SourcePixelFormat::R16G16B16A16Float)
        {
            WriteU16(destination + channel * 2u, imath_float_to_half(value));
        }
        else WriteF32(destination + channel * 4u, value);
    }

    void TransformCoordinate(const u16 orientation, const u32 width, const u32 height, const u32 x, const u32 y,
                             u32& outputX, u32& outputY) noexcept
    {
        switch (orientation)
        {
        case ORIENTATION_TOPRIGHT: outputX = width - 1u - x; outputY = y; break;
        case ORIENTATION_BOTRIGHT: outputX = width - 1u - x; outputY = height - 1u - y; break;
        case ORIENTATION_BOTLEFT: outputX = x; outputY = height - 1u - y; break;
        case ORIENTATION_LEFTTOP: outputX = y; outputY = x; break;
        case ORIENTATION_RIGHTTOP: outputX = height - 1u - y; outputY = x; break;
        case ORIENTATION_RIGHTBOT: outputX = height - 1u - y; outputY = width - 1u - x; break;
        case ORIENTATION_LEFTBOT: outputX = y; outputY = width - 1u - x; break;
        default: outputX = x; outputY = y; break;
        }
    }

    void StorePixel(u8* output, const u32 outputWidth, const u32 outputBytesPerPixel,
                    const tools::SourcePixelFormat outputFormat, const u16 photometric, const u16 orientation,
                    const u32 sourceWidth, const u32 sourceHeight, const u32 x, const u32 y,
                    const u8* const samples[4], const u16 sampleCount, const u16 bits,
                    const u16 sampleFormat, const bool associatedAlpha) noexcept
    {
        u32 outputX = 0;
        u32 outputY = 0;
        TransformCoordinate(orientation, sourceWidth, sourceHeight, x, y, outputX, outputY);
        u8* const destination = output + (static_cast<usize>(outputY) * outputWidth + outputX) * outputBytesPerPixel;
        f32 rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (photometric == PHOTOMETRIC_RGB)
        {
            rgba[0] = ReadNormalized(samples[0], bits, sampleFormat);
            rgba[1] = ReadNormalized(samples[1], bits, sampleFormat);
            rgba[2] = ReadNormalized(samples[2], bits, sampleFormat);
            if (sampleCount == 4) rgba[3] = ReadNormalized(samples[3], bits, sampleFormat);
        }
        else
        {
            f32 gray = ReadNormalized(samples[0], bits, sampleFormat);
            if (photometric == PHOTOMETRIC_MINISWHITE) gray = 1.0f - gray;
            rgba[0] = rgba[1] = rgba[2] = gray;
            if (sampleCount == 2) rgba[3] = ReadNormalized(samples[1], bits, sampleFormat);
        }
        if (associatedAlpha && rgba[3] > 0.0f)
        {
            rgba[0] /= rgba[3]; rgba[1] /= rgba[3]; rgba[2] /= rgba[3];
        }
        if (outputFormat == tools::SourcePixelFormat::R8UNorm)
        {
            WriteNormalized(destination, outputFormat, 0, rgba[0]);
            return;
        }
        for (u32 channel = 0; channel < 4; ++channel)
            WriteNormalized(destination, outputFormat, channel, rgba[channel]);
    }

    [[nodiscard]] tools::TextureProbeResult ProbeTiff(const tools::TextureImportRequest& request, void*) noexcept
    {
        const bool little = request.encoded.Count() >= 4 && request.encoded[0] == 'I' && request.encoded[1] == 'I' &&
                            request.encoded[2] == 42 && request.encoded[3] == 0;
        const bool big = request.encoded.Count() >= 4 && request.encoded[0] == 'M' && request.encoded[1] == 'M' &&
                         request.encoded[2] == 0 && request.encoded[3] == 42;
        const bool littleBig = request.encoded.Count() >= 4 && request.encoded[0] == 'I' && request.encoded[1] == 'I' &&
                               request.encoded[2] == 43 && request.encoded[3] == 0;
        const bool bigBig = request.encoded.Count() >= 4 && request.encoded[0] == 'M' && request.encoded[1] == 'M' &&
                            request.encoded[2] == 0 && request.encoded[3] == 43;
        return little || big || littleBig || bigBig ? tools::TextureProbeResult::Exact :
               (IsHint(request.typeHint, "tif") || IsHint(request.typeHint, "tiff")
                    ? tools::TextureProbeResult::Possible : tools::TextureProbeResult::NoMatch);
    }

    [[nodiscard]] tools::TextureImportResult DecodeTiff(const tools::TextureImportRequest& request,
                                                         tools::ImportedTexture& output, void*) noexcept
    {
        if (ProbeTiff(request, nullptr) != tools::TextureProbeResult::Exact)
            return tools::TextureImportResult::DecodeFailure;
        TiffMemory stream{request.encoded.Data(), request.encoded.SizeInBytes(), 0};
        TIFFOpenOptions* const options = TIFFOpenOptionsAlloc();
        if (options == nullptr) return tools::TextureImportResult::OutOfMemory;
        const u64 allocationLimit = request.limits.maximumDecodedBytes < 0x7fffffffffffffffull
            ? request.limits.maximumDecodedBytes : 0x7fffffffffffffffull;
        TIFFOpenOptionsSetMaxSingleMemAlloc(options, static_cast<tmsize_t>(allocationLimit));
        TIFFOpenOptionsSetMaxCumulatedMemAlloc(options, static_cast<tmsize_t>(allocationLimit));
        TIFFOpenOptionsSetWarnAboutUnknownTags(options, 0);
        TIFFOpenOptionsSetErrorHandlerExtR(options, &IgnoreTiffMessage, nullptr);
        TIFFOpenOptionsSetWarningHandlerExtR(options, &IgnoreTiffMessage, nullptr);
        TIFF* const tiff = TIFFClientOpenExt("Vanguard memory TIFF", "rm", &stream, &ReadTiff, &WriteTiff,
                                             &SeekTiff, &CloseTiff, &SizeTiff, &MapTiff, &UnmapTiff, options);
        TIFFOpenOptionsFree(options);
        if (tiff == nullptr) return tools::TextureImportResult::DecodeFailure;

        tools::TextureImportResult result = tools::TextureImportResult::DecodeFailure;
        const tdir_t directoryCount = TIFFNumberOfDirectories(tiff);
        if (directoryCount != 1 || !TIFFSetDirectory(tiff, 0))
        {
            result = directoryCount > 1 ? tools::TextureImportResult::MultipleImagesUnsupported :
                                          tools::TextureImportResult::DecodeFailure;
            TIFFClose(tiff);
            return result;
        }

        u32 width = 0;
        u32 height = 0;
        u16 samplesPerPixel = 1;
        u16 bitsPerSample = 1;
        u16 sampleFormat = SAMPLEFORMAT_UINT;
        u16 photometric = 0xffffu;
        u16 planar = PLANARCONFIG_CONTIG;
        u16 orientation = ORIENTATION_TOPLEFT;
        if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) ||
            !TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric))
        {
            TIFFClose(tiff);
            return tools::TextureImportResult::DecodeFailure;
        }
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
        TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar);
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation);
        const bool grayscale = photometric == PHOTOMETRIC_MINISBLACK || photometric == PHOTOMETRIC_MINISWHITE;
        const bool rgb = photometric == PHOTOMETRIC_RGB;
        if (width == 0 || height == 0 || (orientation < ORIENTATION_TOPLEFT || orientation > ORIENTATION_LEFTBOT) ||
            (planar != PLANARCONFIG_CONTIG && planar != PLANARCONFIG_SEPARATE) ||
            (!grayscale && !rgb) || (grayscale && samplesPerPixel != 1 && samplesPerPixel != 2) ||
            (rgb && samplesPerPixel != 3 && samplesPerPixel != 4) ||
            (sampleFormat != SAMPLEFORMAT_UINT && sampleFormat != SAMPLEFORMAT_IEEEFP) ||
            (sampleFormat == SAMPLEFORMAT_UINT && bitsPerSample != 8 && bitsPerSample != 16) ||
            (sampleFormat == SAMPLEFORMAT_IEEEFP && bitsPerSample != 16 && bitsPerSample != 32))
        {
            TIFFClose(tiff);
            return tools::TextureImportResult::UnsupportedFormat;
        }

        bool associatedAlpha = false;
        if ((grayscale && samplesPerPixel == 2) || (rgb && samplesPerPixel == 4))
        {
            u16 extraCount = 0;
            u16* extraTypes = nullptr;
            if (TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes) && extraCount != 0)
            {
                if (extraCount != 1 || (extraTypes[0] != EXTRASAMPLE_ASSOCALPHA &&
                    extraTypes[0] != EXTRASAMPLE_UNASSALPHA && extraTypes[0] != EXTRASAMPLE_UNSPECIFIED))
                {
                    TIFFClose(tiff);
                    return tools::TextureImportResult::UnsupportedFormat;
                }
                associatedAlpha = extraTypes[0] == EXTRASAMPLE_ASSOCALPHA;
            }
        }

        const bool transposed = orientation >= ORIENTATION_LEFTTOP;
        const u32 outputWidth = transposed ? height : width;
        const u32 outputHeight = transposed ? width : height;
        if (outputWidth > request.limits.maximumDimension || outputHeight > request.limits.maximumDimension)
        {
            TIFFClose(tiff);
            return tools::TextureImportResult::LimitExceeded;
        }
        tools::SourcePixelFormat outputFormat = tools::SourcePixelFormat::R8G8B8A8UNorm;
        if (sampleFormat == SAMPLEFORMAT_IEEEFP)
            outputFormat = bitsPerSample == 16 ? tools::SourcePixelFormat::R16G16B16A16Float :
                                                tools::SourcePixelFormat::R32G32B32A32Float;
        else if (bitsPerSample == 16) outputFormat = tools::SourcePixelFormat::R16G16B16A16UNorm;
        else if (grayscale && samplesPerPixel == 1) outputFormat = tools::SourcePixelFormat::R8UNorm;
        const u32 outputBytesPerPixel = outputFormat == tools::SourcePixelFormat::R8UNorm ? 1u :
                                        outputFormat == tools::SourcePixelFormat::R8G8B8A8UNorm ? 4u :
                                        outputFormat == tools::SourcePixelFormat::R32G32B32A32Float ? 16u : 8u;
        const u64 rowPitch = static_cast<u64>(outputWidth) * outputBytesPerPixel;
        if (rowPitch > 0xffffffffull || rowPitch * outputHeight > request.limits.maximumDecodedBytes)
        {
            TIFFClose(tiff);
            return tools::TextureImportResult::LimitExceeded;
        }
        const textures::ColorSpace automaticColor = sampleFormat == SAMPLEFORMAT_IEEEFP
            ? textures::ColorSpace::Linear : textures::ColorSpace::SRgb;
        result = output.Initialize2D(outputFormat, ResolveColorSpace(request, automaticColor), outputWidth, outputHeight,
                                     static_cast<u32>(rowPitch), Fingerprint(request),
                                     tools::ResolveImportedProfile(request.usage, outputFormat), tools::importers::Tiff,
                                     1, request.limits);
        if (result != tools::TextureImportResult::Success)
        {
            TIFFClose(tiff);
            return result;
        }

        const u32 inputBytesPerSample = bitsPerSample / 8u;
        containers::DynamicArray<u8> scratch(vanguard::memory::pools::Assets::GetInstance());
        if (TIFFIsTiled(tiff))
        {
            u32 tileWidth = 0;
            u32 tileHeight = 0;
            if (!TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tileWidth) || !TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tileHeight) ||
                tileWidth == 0 || tileHeight == 0)
                result = tools::TextureImportResult::DecodeFailure;
            const u64 tileBytes = TIFFTileSize64(tiff);
            const u64 scratchBytes = tileBytes * (planar == PLANARCONFIG_SEPARATE ? samplesPerPixel : 1u);
            if (result == tools::TextureImportResult::Success &&
                (tileBytes == 0 || scratchBytes > 0xffffffffull || scratchBytes > request.limits.maximumDecodedBytes))
                result = tools::TextureImportResult::LimitExceeded;
            if (result == tools::TextureImportResult::Success) scratch.Resize(static_cast<u32>(scratchBytes));
            if (result == tools::TextureImportResult::Success && scratch.Size() != scratchBytes)
                result = tools::TextureImportResult::OutOfMemory;
            const u64 tileRowBytes = TIFFTileRowSize64(tiff);
            for (u32 tileY = 0; result == tools::TextureImportResult::Success && tileY < height; tileY += tileHeight)
            {
                for (u32 tileX = 0; result == tools::TextureImportResult::Success && tileX < width; tileX += tileWidth)
                {
                    const u16 planes = planar == PLANARCONFIG_SEPARATE ? samplesPerPixel : 1u;
                    for (u16 plane = 0; result == tools::TextureImportResult::Success && plane < planes; ++plane)
                    {
                        const u32 tile = TIFFComputeTile(tiff, tileX, tileY, 0, plane);
                        if (TIFFReadEncodedTile(tiff, tile, scratch.TypedData() + static_cast<u64>(plane) * tileBytes,
                                                static_cast<tmsize_t>(tileBytes)) < 0)
                            result = tools::TextureImportResult::DecodeFailure;
                    }
                    for (u32 localY = 0; result == tools::TextureImportResult::Success &&
                         localY < tileHeight && tileY + localY < height; ++localY)
                    {
                        for (u32 localX = 0; localX < tileWidth && tileX + localX < width; ++localX)
                        {
                            const u8* samples[4]{};
                            for (u16 sample = 0; sample < samplesPerPixel; ++sample)
                            {
                                samples[sample] = planar == PLANARCONFIG_CONTIG
                                    ? scratch.TypedData() + static_cast<u64>(localY) * tileRowBytes +
                                          static_cast<u64>(localX * samplesPerPixel + sample) * inputBytesPerSample
                                    : scratch.TypedData() + static_cast<u64>(sample) * tileBytes +
                                          static_cast<u64>(localY) * tileRowBytes +
                                          static_cast<u64>(localX) * inputBytesPerSample;
                            }
                            StorePixel(output.MutableImageData(), outputWidth, outputBytesPerPixel, outputFormat,
                                       photometric, orientation, width, height, tileX + localX, tileY + localY,
                                       samples, samplesPerPixel, bitsPerSample, sampleFormat, associatedAlpha);
                        }
                    }
                }
            }
        }
        else
        {
            const u64 scanlineBytes = TIFFScanlineSize64(tiff);
            const u64 scratchBytes = scanlineBytes * (planar == PLANARCONFIG_SEPARATE ? samplesPerPixel : 1u);
            if (scanlineBytes == 0 || scratchBytes > 0xffffffffull || scratchBytes > request.limits.maximumDecodedBytes)
                result = tools::TextureImportResult::LimitExceeded;
            else scratch.Resize(static_cast<u32>(scratchBytes));
            if (result == tools::TextureImportResult::Success && scratch.Size() != scratchBytes)
                result = tools::TextureImportResult::OutOfMemory;
            for (u32 y = 0; result == tools::TextureImportResult::Success && y < height; ++y)
            {
                const u16 planes = planar == PLANARCONFIG_SEPARATE ? samplesPerPixel : 1u;
                for (u16 plane = 0; result == tools::TextureImportResult::Success && plane < planes; ++plane)
                    if (TIFFReadScanline(tiff, scratch.TypedData() + static_cast<u64>(plane) * scanlineBytes, y, plane) < 0)
                        result = tools::TextureImportResult::DecodeFailure;
                for (u32 x = 0; result == tools::TextureImportResult::Success && x < width; ++x)
                {
                    const u8* samples[4]{};
                    for (u16 sample = 0; sample < samplesPerPixel; ++sample)
                        samples[sample] = planar == PLANARCONFIG_CONTIG
                            ? scratch.TypedData() + static_cast<u64>(x * samplesPerPixel + sample) * inputBytesPerSample
                            : scratch.TypedData() + static_cast<u64>(sample) * scanlineBytes +
                                  static_cast<u64>(x) * inputBytesPerSample;
                    StorePixel(output.MutableImageData(), outputWidth, outputBytesPerPixel, outputFormat, photometric,
                               orientation, width, height, x, y, samples, samplesPerPixel, bitsPerSample,
                               sampleFormat, associatedAlpha);
                }
            }
        }
        TIFFClose(tiff);
        if (result != tools::TextureImportResult::Success) output.Reset();
        return result;
    }

    const tools::TextureImporterDescriptor g_tiffImporter{
        tools::importers::Tiff, "Tagged Image File Format", 1, &ProbeTiff, &DecodeTiff, nullptr};
}

namespace vanguard::texture_tools
{
    const TextureImporterDescriptor& TiffImporter() noexcept { return g_tiffImporter; }
} // namespace vanguard::texture_tools
