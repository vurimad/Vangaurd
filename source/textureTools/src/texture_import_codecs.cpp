#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <csetjmp>

extern "C"
{
#include <jpeglib.h>
#include <png.h>
}

namespace vanguard::texture_tools
{
    [[nodiscard]] TextureCookingProfileId ResolveImportedProfile(TextureUsage usage, SourcePixelFormat format) noexcept;
}

namespace
{
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace serialization = vanguard::serialization;
    namespace textures = vanguard::textures;
    namespace tools = vanguard::texture_tools;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::u8;
    using vanguard::usize;

    [[nodiscard]] bool IsHint(const containers::StringView hint, const char* expected) noexcept
    {
        u32 length = 0;
        while (expected[length] != '\0')
            ++length;
        if (hint.Length() != length)
            return false;
        for (u32 index = 0; index < length; ++index)
        {
            const char value = hint[index] >= 'A' && hint[index] <= 'Z' ? static_cast<char>(hint[index] - 'A' + 'a') : hint[index];
            if (value != expected[index])
                return false;
        }
        return true;
    }

    [[nodiscard]] crypto::Digest256 Fingerprint(const tools::TextureImportRequest& request) noexcept
    {
        return request.sourceFingerprint.IsEmpty() ? crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes()) : request.sourceFingerprint;
    }

    [[nodiscard]] textures::ColorSpace ResolveColorSpace(const tools::TextureImportRequest& request, const textures::ColorSpace automatic) noexcept
    {
        return request.colorSpace == tools::ImportedColorSpace::Linear ? textures::ColorSpace::Linear
               : request.colorSpace == tools::ImportedColorSpace::SRgb ? textures::ColorSpace::SRgb
                                                                       : automatic;
    }

    struct PngReader final
    {
        const u8* bytes = nullptr;
        usize size = 0;
        usize position = 0;
    };

    void ReadPng(png_structp png, png_bytep destination, const png_size_t byteCount)
    {
        auto* const reader = static_cast<PngReader*>(png_get_io_ptr(png));
        if (reader == nullptr || byteCount > reader->size - reader->position)
            png_error(png, "truncated PNG input");
        for (png_size_t index = 0; index < byteCount; ++index)
            destination[index] = reader->bytes[reader->position + index];
        reader->position += byteCount;
    }

    void PngFailure(png_structp png, png_const_charp)
    {
        png_longjmp(png, 1);
    }
    void PngWarning(png_structp, png_const_charp) {}

    [[nodiscard]] tools::TextureProbeResult ProbePng(const tools::TextureImportRequest& request, void*) noexcept
    {
        return request.encoded.Count() >= 8 && png_sig_cmp(request.encoded.Data(), 0, 8) == 0
                   ? tools::TextureProbeResult::Exact
                   : (IsHint(request.typeHint, "png") ? tools::TextureProbeResult::Possible : tools::TextureProbeResult::NoMatch);
    }

    [[nodiscard]] u32 ReadBigEndianU32(const u8* bytes) noexcept
    {
        return static_cast<u32>(bytes[0]) << 24u | static_cast<u32>(bytes[1]) << 16u | static_cast<u32>(bytes[2]) << 8u | static_cast<u32>(bytes[3]);
    }

    [[nodiscard]] bool ValidatePngStructure(const tools::TextureImportRequest& request) noexcept
    {
        const u8* const bytes = request.encoded.Data();
        const usize size = request.encoded.SizeInBytes();
        usize offset = 8;
        bool foundHeader = false;
        bool foundImageData = false;
        while (offset <= size && size - offset >= 12)
        {
            const u32 length = ReadBigEndianU32(bytes + offset);
            if (static_cast<u64>(length) + 12u > size - offset)
                return false;
            const u8* const type = bytes + offset + 4u;
            const bool isHeader = type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R';
            const bool isImageData = type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T';
            const bool isEnd = type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D';
            if ((!foundHeader && (!isHeader || length != 13)) || (foundHeader && isHeader))
                return false;
            const u32 storedCrc = ReadBigEndianU32(bytes + offset + 8u + length);
            if (serialization::Crc32(type, static_cast<usize>(length) + 4u) != storedCrc)
                return false;
            foundHeader = true;
            foundImageData = foundImageData || isImageData;
            offset += static_cast<usize>(length) + 12u;
            if (isEnd)
                return length == 0 && foundImageData && offset == size;
        }
        return false;
    }

    [[nodiscard]] tools::TextureImportResult DecodePng(const tools::TextureImportRequest& request, tools::ImportedTexture& output, void*) noexcept
    {
        if (request.encoded.Count() < 8 || png_sig_cmp(request.encoded.Data(), 0, 8) != 0 || !ValidatePngStructure(request))
            return tools::TextureImportResult::DecodeFailure;

        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, PngFailure, PngWarning);
        if (png == nullptr)
            return tools::TextureImportResult::OutOfMemory;
        png_infop info = png_create_info_struct(png);
        if (info == nullptr)
        {
            png_destroy_read_struct(&png, nullptr, nullptr);
            return tools::TextureImportResult::OutOfMemory;
        }

        PngReader reader{request.encoded.Data(), request.encoded.SizeInBytes(), 0};
        tools::TextureImportResult result = tools::TextureImportResult::DecodeFailure;
        if (setjmp(png_jmpbuf(png)) == 0)
        {
            png_set_read_fn(png, &reader, ReadPng);
            png_set_user_limits(png, request.limits.maximumDimension, request.limits.maximumDimension);
            png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
            png_read_info(png, info);

            const png_uint_32 width = png_get_image_width(png, info);
            const png_uint_32 height = png_get_image_height(png, info);
            const int originalBitDepth = png_get_bit_depth(png, info);
            const int originalColorType = png_get_color_type(png, info);
            if (width == 0 || height == 0 || width > request.limits.maximumDimension || height > request.limits.maximumDimension ||
                (originalBitDepth != 16 && originalBitDepth > 8))
            {
                result = tools::TextureImportResult::LimitExceeded;
            }
            else
            {
                const bool preserve16Bit = originalBitDepth == 16;
                if (originalColorType == PNG_COLOR_TYPE_PALETTE)
                    png_set_palette_to_rgb(png);
                if (originalColorType == PNG_COLOR_TYPE_GRAY && originalBitDepth < 8)
                    png_set_expand_gray_1_2_4_to_8(png);
                if (png_get_valid(png, info, PNG_INFO_tRNS))
                    png_set_tRNS_to_alpha(png);
                if (originalColorType == PNG_COLOR_TYPE_GRAY || originalColorType == PNG_COLOR_TYPE_GRAY_ALPHA)
                    png_set_gray_to_rgb(png);
                if ((originalColorType & PNG_COLOR_MASK_ALPHA) == 0 && !png_get_valid(png, info, PNG_INFO_tRNS))
                    png_set_add_alpha(png, preserve16Bit ? 0xffffu : 0xffu, PNG_FILLER_AFTER);
#if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || defined(__x86_64__) || defined(__aarch64__)
                if (preserve16Bit)
                    png_set_swap(png);
#endif
                static_cast<void>(png_set_interlace_handling(png));
                png_read_update_info(png, info);

                const png_size_t rowPitch = png_get_rowbytes(png, info);
                const u32 bytesPerPixel = preserve16Bit ? 8u : 4u;
                const u64 decodedBytes = static_cast<u64>(rowPitch) * height;
                if (png_get_channels(png, info) != 4 || rowPitch != static_cast<u64>(width) * bytesPerPixel || rowPitch > 0xffffffffull ||
                    decodedBytes > request.limits.maximumDecodedBytes)
                {
                    result = decodedBytes > request.limits.maximumDecodedBytes ? tools::TextureImportResult::LimitExceeded
                                                                               : tools::TextureImportResult::DecodeFailure;
                }
                else
                {
                    const tools::SourcePixelFormat format =
                        preserve16Bit ? tools::SourcePixelFormat::R16G16B16A16UNorm : tools::SourcePixelFormat::R8G8B8A8UNorm;
                    const textures::ColorSpace automaticColor =
                        png_get_valid(png, info, PNG_INFO_sRGB) ? textures::ColorSpace::SRgb : textures::ColorSpace::SRgb;
                    result =
                        output.Initialize2D(format, ResolveColorSpace(request, automaticColor), width, height, static_cast<u32>(rowPitch), Fingerprint(request),
                                            tools::ResolveImportedProfile(request.usage, format), tools::importers::Png, 1, request.limits);
                    if (result == tools::TextureImportResult::Success)
                    {
                        for (u32 row = 0; row < height; ++row)
                            png_read_row(png, output.GetMutableImageData() + static_cast<usize>(row) * rowPitch, nullptr);
                        png_read_end(png, info);
                    }
                }
            }
        }
        png_destroy_read_struct(&png, &info, nullptr);
        if (result != tools::TextureImportResult::Success)
            output.Reset();
        return result;
    }

    struct JpegError final
    {
        jpeg_error_mgr base;
        jmp_buf jump;
    };

    void JpegFailure(j_common_ptr context)
    {
        auto* const error = reinterpret_cast<JpegError*>(context->err);
        longjmp(error->jump, 1);
    }

    [[nodiscard]] tools::TextureProbeResult ProbeJpeg(const tools::TextureImportRequest& request, void*) noexcept
    {
        const bool signature = request.encoded.Count() >= 3 && request.encoded[0] == 0xff && request.encoded[1] == 0xd8 && request.encoded[2] == 0xff;
        return signature ? tools::TextureProbeResult::Exact
                         : (IsHint(request.typeHint, "jpg") || IsHint(request.typeHint, "jpeg") ? tools::TextureProbeResult::Possible
                                                                                                : tools::TextureProbeResult::NoMatch);
    }

    [[nodiscard]] tools::TextureImportResult DecodeJpeg(const tools::TextureImportRequest& request, tools::ImportedTexture& output, void*) noexcept
    {
        if (request.encoded.Count() < 3 || request.encoded[0] != 0xff || request.encoded[1] != 0xd8 || request.encoded[2] != 0xff)
            return tools::TextureImportResult::DecodeFailure;

        jpeg_decompress_struct decoder{};
        JpegError error{};
        decoder.err = jpeg_std_error(&error.base);
        error.base.error_exit = JpegFailure;
        if (setjmp(error.jump) != 0)
        {
            jpeg_destroy_decompress(&decoder);
            output.Reset();
            return tools::TextureImportResult::DecodeFailure;
        }

        jpeg_create_decompress(&decoder);
        jpeg_mem_src(&decoder, request.encoded.Data(), request.encoded.SizeInBytes());
        if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK || decoder.image_width == 0 || decoder.image_height == 0)
        {
            jpeg_destroy_decompress(&decoder);
            return tools::TextureImportResult::DecodeFailure;
        }
        if (decoder.image_width > request.limits.maximumDimension || decoder.image_height > request.limits.maximumDimension)
        {
            jpeg_destroy_decompress(&decoder);
            return tools::TextureImportResult::LimitExceeded;
        }

        decoder.out_color_space = JCS_EXT_RGBA;
        decoder.dct_method = JDCT_ISLOW;
        decoder.do_fancy_upsampling = TRUE;
        decoder.do_block_smoothing = TRUE;
        if (!jpeg_start_decompress(&decoder) || decoder.output_components != 4)
        {
            jpeg_destroy_decompress(&decoder);
            return tools::TextureImportResult::DecodeFailure;
        }

        const u64 rowPitch = static_cast<u64>(decoder.output_width) * 4u;
        const u64 decodedBytes = rowPitch * decoder.output_height;
        if (rowPitch > 0xffffffffull || decodedBytes > request.limits.maximumDecodedBytes)
        {
            jpeg_destroy_decompress(&decoder);
            return tools::TextureImportResult::LimitExceeded;
        }
        tools::TextureImportResult result = output.Initialize2D(tools::SourcePixelFormat::R8G8B8A8UNorm, ResolveColorSpace(request, textures::ColorSpace::SRgb),
                                                                decoder.output_width, decoder.output_height, static_cast<u32>(rowPitch), Fingerprint(request),
                                                                tools::ResolveImportedProfile(request.usage, tools::SourcePixelFormat::R8G8B8A8UNorm),
                                                                tools::importers::Jpeg, 1, request.limits);
        while (result == tools::TextureImportResult::Success && decoder.output_scanline < decoder.output_height)
        {
            JSAMPROW row = output.GetMutableImageData() + static_cast<usize>(decoder.output_scanline) * rowPitch;
            if (jpeg_read_scanlines(&decoder, &row, 1) != 1)
                result = tools::TextureImportResult::DecodeFailure;
        }
        if (result == tools::TextureImportResult::Success && !jpeg_finish_decompress(&decoder))
            result = tools::TextureImportResult::DecodeFailure;
        jpeg_destroy_decompress(&decoder);
        if (result != tools::TextureImportResult::Success)
            output.Reset();
        return result;
    }

    const tools::TextureImporterDescriptor g_pngImporter{tools::importers::Png, "Portable Network Graphics", 1, &ProbePng, &DecodePng, nullptr};
    const tools::TextureImporterDescriptor g_jpegImporter{tools::importers::Jpeg, "JPEG", 1, &ProbeJpeg, &DecodeJpeg, nullptr};
} // namespace

namespace vanguard::texture_tools
{
    const TextureImporterDescriptor& PngImporter() noexcept
    {
        return g_pngImporter;
    }
    const TextureImporterDescriptor& JpegImporter() noexcept
    {
        return g_jpegImporter;
    }
} // namespace vanguard::texture_tools
