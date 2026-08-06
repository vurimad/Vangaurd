#include "texture_import_fixtures.hpp"

#include <OpenEXRCore/openexr.h>
#include <tiffio.h>

#include <cstdio>
#include <cstring>

namespace
{
    using Fixture = vanguard::texture_tools::tests::EncodedFixture;

    struct FixtureStream final
    {
        Fixture* fixture = nullptr;
        std::size_t position = 0;
    };

    tmsize_t ReadTiff(thandle_t handle, void* destination, const tmsize_t requested) noexcept
    {
        auto* const stream = static_cast<FixtureStream*>(handle);
        if (requested < 0 || stream->position > stream->fixture->size) return -1;
        const std::size_t available = stream->fixture->size - stream->position;
        const std::size_t count = static_cast<std::size_t>(requested) < available
            ? static_cast<std::size_t>(requested) : available;
        std::memcpy(destination, stream->fixture->bytes.data() + stream->position, count);
        stream->position += count;
        return static_cast<tmsize_t>(count);
    }

    tmsize_t WriteTiff(thandle_t handle, void* source, const tmsize_t requested) noexcept
    {
        auto* const stream = static_cast<FixtureStream*>(handle);
        if (requested < 0 || static_cast<std::size_t>(requested) > stream->fixture->bytes.size() - stream->position)
            return -1;
        std::memcpy(stream->fixture->bytes.data() + stream->position, source, static_cast<std::size_t>(requested));
        stream->position += static_cast<std::size_t>(requested);
        if (stream->position > stream->fixture->size) stream->fixture->size = stream->position;
        return requested;
    }

    toff_t SeekTiff(thandle_t handle, const toff_t offset, const int origin) noexcept
    {
        auto* const stream = static_cast<FixtureStream*>(handle);
        std::size_t base = 0;
        if (origin == SEEK_CUR) base = stream->position;
        else if (origin == SEEK_END) base = stream->fixture->size;
        else if (origin != SEEK_SET) return static_cast<toff_t>(-1);
        if (offset > stream->fixture->bytes.size() - base) return static_cast<toff_t>(-1);
        stream->position = base + static_cast<std::size_t>(offset);
        return static_cast<toff_t>(stream->position);
    }

    int CloseTiff(thandle_t) noexcept { return 0; }
    toff_t SizeTiff(thandle_t handle) noexcept
    {
        return static_cast<toff_t>(static_cast<FixtureStream*>(handle)->fixture->size);
    }
    int MapTiff(thandle_t, void**, toff_t*) noexcept { return 0; }
    void UnmapTiff(thandle_t, void*, toff_t) noexcept {}

    int64_t WriteExr(exr_const_context_t context, void* userData, const void* source, const uint64_t requested,
                     const uint64_t offset, exr_stream_error_func_ptr_t error) noexcept
    {
        auto* const fixture = static_cast<Fixture*>(userData);
        if (offset > fixture->bytes.size() || requested > fixture->bytes.size() - offset)
        {
            if (error != nullptr) error(context, EXR_ERR_WRITE_IO, "OpenEXR test fixture exceeds fixed buffer");
            return -1;
        }
        std::memcpy(fixture->bytes.data() + offset, source, static_cast<std::size_t>(requested));
        const std::size_t end = static_cast<std::size_t>(offset + requested);
        if (end > fixture->size) fixture->size = end;
        return static_cast<int64_t>(requested);
    }

    void IgnoreExrError(exr_const_context_t, exr_result_t, const char*) noexcept {}

    bool Same(const char* left, const char* right) noexcept
    {
        return std::strcmp(left, right) == 0;
    }
}

namespace vanguard::texture_tools::tests
{
    bool MakeTiledTiff16(EncodedFixture& output) noexcept
    {
        output = {};
        FixtureStream stream{&output, 0};
        TIFF* const tiff = TIFFClientOpen("Vanguard TIFF fixture", "w", &stream, &ReadTiff, &WriteTiff, &SeekTiff,
                                          &CloseTiff, &SizeTiff, &MapTiff, &UnmapTiff);
        if (tiff == nullptr) return false;
        bool success = TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 16u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 16u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPRIGHT) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_TILEWIDTH, 16u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_TILELENGTH, 16u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL) == 1;
        std::array<std::uint16_t, 16u * 16u * 3u> pixels{};
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 16; ++x)
            {
                const std::size_t pixel = (static_cast<std::size_t>(y) * 16u + x) * 3u;
                pixels[pixel] = static_cast<std::uint16_t>(x * 1000u + y);
                pixels[pixel + 1u] = static_cast<std::uint16_t>(y * 2000u + x);
                pixels[pixel + 2u] = 65535u;
            }
        if (success) success = TIFFWriteEncodedTile(tiff, 0, pixels.data(), static_cast<tmsize_t>(sizeof(pixels))) >= 0;
        TIFFClose(tiff);
        return success;
    }

    bool MakeScanlineTiffFloat(EncodedFixture& output) noexcept
    {
        output = {};
        FixtureStream stream{&output, 0};
        TIFF* const tiff = TIFFClientOpen("Vanguard float TIFF fixture", "w", &stream, &ReadTiff, &WriteTiff,
                                          &SeekTiff, &CloseTiff, &SizeTiff, &MapTiff, &UnmapTiff);
        if (tiff == nullptr) return false;
        bool success = TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 2u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 1u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 32) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1u) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE) == 1 &&
                       TIFFSetField(tiff, TIFFTAG_PREDICTOR, PREDICTOR_FLOATINGPOINT) == 1;
        const float pixels[6] = {-2.0f, 0.5f, 8.0f, 16.0f, -4.0f, 1.0f};
        if (success) success = TIFFWriteScanline(tiff, const_cast<float*>(pixels), 0, 0) >= 0;
        TIFFClose(tiff);
        return success;
    }

    bool MakeScanlineOpenExrFloat(EncodedFixture& output) noexcept
    {
        output = {};
        exr_context_initializer_t initializer = EXR_DEFAULT_CONTEXT_INITIALIZER;
        initializer.user_data = &output;
        initializer.write_fn = &WriteExr;
        initializer.error_handler_fn = &IgnoreExrError;
        exr_context_t context = nullptr;
        int part = -1;
        exr_result_t result = exr_start_write(&context, "Vanguard OpenEXR fixture", EXR_WRITE_FILE_DIRECTLY,
                                               &initializer);
        if (result == EXR_ERR_SUCCESS) result = exr_add_part(context, "beauty", EXR_STORAGE_SCANLINE, &part);
        if (result == EXR_ERR_SUCCESS)
            result = exr_initialize_required_attr_simple(context, part, 2, 2, EXR_COMPRESSION_ZIP);
        const char* const names[4] = {"R", "G", "B", "A"};
        for (int channel = 0; result == EXR_ERR_SUCCESS && channel < 4; ++channel)
            result = exr_add_channel(context, part, names[channel], EXR_PIXEL_FLOAT,
                                     EXR_PERCEPTUALLY_LOGARITHMIC, 1, 1);
        if (result == EXR_ERR_SUCCESS) result = exr_write_header(context);
        exr_chunk_info_t chunk{};
        if (result == EXR_ERR_SUCCESS) result = exr_write_scanline_chunk_info(context, part, 0, &chunk);
        exr_encode_pipeline_t encoder = EXR_ENCODE_PIPELINE_INITIALIZER;
        if (result == EXR_ERR_SUCCESS) result = exr_encoding_initialize(context, part, &chunk, &encoder);
        const float pixels[16] = {0.25f, 0.5f, 1.0f, 1.0f, 2.0f, 4.0f, 8.0f, 0.5f,
                                  -1.0f, 0.0f, 1.0f, 1.0f, 16.0f, 32.0f, 64.0f, 0.25f};
        for (int channel = 0; result == EXR_ERR_SUCCESS && channel < encoder.channel_count; ++channel)
        {
            int sourceChannel = -1;
            for (int candidate = 0; candidate < 4; ++candidate)
                if (Same(encoder.channels[channel].channel_name, names[candidate])) sourceChannel = candidate;
            if (sourceChannel < 0) result = EXR_ERR_INVALID_ARGUMENT;
            else
            {
                encoder.channels[channel].encode_from_ptr = reinterpret_cast<const uint8_t*>(pixels + sourceChannel);
                encoder.channels[channel].user_pixel_stride = 16;
                encoder.channels[channel].user_line_stride = 32;
                encoder.channels[channel].user_bytes_per_element = 4;
                encoder.channels[channel].user_data_type = static_cast<uint16_t>(EXR_PIXEL_FLOAT);
            }
        }
        if (result == EXR_ERR_SUCCESS) result = exr_encoding_choose_default_routines(context, part, &encoder);
        if (result == EXR_ERR_SUCCESS) result = exr_encoding_run(context, part, &encoder);
        const exr_result_t destroyResult = exr_encoding_destroy(context, &encoder);
        if (result == EXR_ERR_SUCCESS) result = destroyResult;
        const exr_result_t finishResult = exr_finish(&context);
        if (result == EXR_ERR_SUCCESS) result = finishResult;
        return result == EXR_ERR_SUCCESS;
    }
}
