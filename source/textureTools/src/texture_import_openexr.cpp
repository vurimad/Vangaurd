#include <vanguard/texture_tools/texture_import.hpp>
#include <vanguard/memory/memory.hpp>

#include <OpenEXRCore/openexr.h>

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
    using vanguard::u8;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::usize;

    struct ExrMemory final
    {
        const u8* data = nullptr;
        u64 size = 0;
    };

    struct ExrAllocationHeader final
    {
        u64 size = 0;
        u64 reserved = 0;
    };
    static_assert(sizeof(ExrAllocationHeader) == 16);

    thread_local u64 g_exrAllocationBudget = 0;
    thread_local u64 g_exrAllocatedBytes = 0;

    [[nodiscard]] void* AllocateExr(const size_t requested) noexcept
    {
        if (requested == 0 || requested > 0xffffffffull - sizeof(ExrAllocationHeader) ||
            g_exrAllocatedBytes > g_exrAllocationBudget ||
            requested > g_exrAllocationBudget - g_exrAllocatedBytes)
            return nullptr;
        const usize allocationSize = requested + sizeof(ExrAllocationHeader);
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Assets, allocationSize, 16);
        if (!block) return nullptr;
        auto* const header = static_cast<ExrAllocationHeader*>(block.address);
        header->size = allocationSize;
        g_exrAllocatedBytes += allocationSize;
        return reinterpret_cast<u8*>(block.address) + sizeof(ExrAllocationHeader);
    }

    void FreeExr(void* address) noexcept
    {
        if (address == nullptr) return;
        auto* const header = reinterpret_cast<ExrAllocationHeader*>(
            static_cast<u8*>(address) - sizeof(ExrAllocationHeader));
        const usize allocationSize = static_cast<usize>(header->size);
        vanguard::memory::MemoryBlock block{header, allocationSize, vanguard::memory::PoolId::Assets};
        vanguard::memory::Free(block);
        g_exrAllocatedBytes = allocationSize <= g_exrAllocatedBytes ? g_exrAllocatedBytes - allocationSize : 0;
    }

    void IgnoreExrError(exr_const_context_t, exr_result_t, const char*) noexcept {}

    [[nodiscard]] int64_t SizeExr(exr_const_context_t, void* userData) noexcept
    {
        const auto* const stream = static_cast<const ExrMemory*>(userData);
        return stream == nullptr || stream->size > 0x7fffffffffffffffull ? -1 : static_cast<int64_t>(stream->size);
    }

    [[nodiscard]] int64_t ReadExr(exr_const_context_t context, void* userData, void* destination, const u64 size,
                                  const u64 offset, exr_stream_error_func_ptr_t error) noexcept
    {
        const auto* const stream = static_cast<const ExrMemory*>(userData);
        if (stream == nullptr || destination == nullptr || offset > stream->size || size > stream->size - offset)
        {
            if (error != nullptr) error(context, EXR_ERR_READ_IO, "OpenEXR read exceeds encoded source bounds");
            return -1;
        }
        auto* const output = static_cast<u8*>(destination);
        for (u64 byte = 0; byte < size; ++byte) output[byte] = stream->data[offset + byte];
        return static_cast<int64_t>(size);
    }

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

    [[nodiscard]] bool EqualName(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr) return false;
        u32 index = 0;
        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index]) return false;
            ++index;
        }
        return left[index] == right[index];
    }

    [[nodiscard]] u32 NameLength(const char* name) noexcept
    {
        u32 length = 0;
        if (name != nullptr) while (name[length] != '\0') ++length;
        return length;
    }

    [[nodiscard]] int FindExactChannel(const exr_attr_chlist_t& channels, const char* name) noexcept
    {
        for (int index = 0; index < channels.num_channels; ++index)
            if (EqualName(channels.entries[index].name.str, name)) return index;
        return -1;
    }

    [[nodiscard]] int FindLayerChannel(const exr_attr_chlist_t& channels, const char* prefix,
                                       const u32 prefixLength, const char suffix) noexcept
    {
        for (int index = 0; index < channels.num_channels; ++index)
        {
            const char* const name = channels.entries[index].name.str;
            if (NameLength(name) != prefixLength + 2u || name[prefixLength] != '.' || name[prefixLength + 1u] != suffix)
                continue;
            bool samePrefix = true;
            for (u32 character = 0; character < prefixLength; ++character)
                if (name[character] != prefix[character]) samePrefix = false;
            if (samePrefix) return index;
        }
        return -1;
    }

    struct SelectedExrChannels final
    {
        int red = -1;
        int green = -1;
        int blue = -1;
        int alpha = -1;
        int luminance = -1;
    };

    [[nodiscard]] bool SelectChannels(const exr_attr_chlist_t& channels, SelectedExrChannels& selected) noexcept
    {
        selected.red = FindExactChannel(channels, "R");
        selected.green = FindExactChannel(channels, "G");
        selected.blue = FindExactChannel(channels, "B");
        selected.alpha = FindExactChannel(channels, "A");
        if (selected.red >= 0 && selected.green >= 0 && selected.blue >= 0) return true;
        selected = {};
        selected.red = selected.green = selected.blue = selected.alpha = selected.luminance = -1;
        selected.luminance = FindExactChannel(channels, "Y");
        selected.alpha = FindExactChannel(channels, "A");
        if (selected.luminance >= 0) return true;

        int completeLayerCount = 0;
        for (int index = 0; index < channels.num_channels; ++index)
        {
            const char* const name = channels.entries[index].name.str;
            const u32 length = NameLength(name);
            if (length < 3 || name[length - 2u] != '.' || name[length - 1u] != 'R') continue;
            const u32 prefixLength = length - 2u;
            const int green = FindLayerChannel(channels, name, prefixLength, 'G');
            const int blue = FindLayerChannel(channels, name, prefixLength, 'B');
            if (green < 0 || blue < 0) continue;
            ++completeLayerCount;
            selected.red = index;
            selected.green = green;
            selected.blue = blue;
            selected.alpha = FindLayerChannel(channels, name, prefixLength, 'A');
        }
        return completeLayerCount == 1;
    }

    [[nodiscard]] crypto::Digest256 Fingerprint(const tools::TextureImportRequest& request) noexcept
    {
        return request.sourceFingerprint.IsEmpty()
            ? crypto::Sha256(request.encoded.Data(), request.encoded.SizeInBytes()) : request.sourceFingerprint;
    }

    void WriteHalf(u8* destination, const u16 value) noexcept
    {
        destination[0] = static_cast<u8>(value);
        destination[1] = static_cast<u8>(value >> 8u);
    }

    void WriteFloat(u8* destination, const float value) noexcept
    {
        union { float value; u32 bits; } converted{value};
        destination[0] = static_cast<u8>(converted.bits);
        destination[1] = static_cast<u8>(converted.bits >> 8u);
        destination[2] = static_cast<u8>(converted.bits >> 16u);
        destination[3] = static_cast<u8>(converted.bits >> 24u);
    }

    [[nodiscard]] tools::TextureProbeResult ProbeOpenExr(const tools::TextureImportRequest& request, void*) noexcept
    {
        const bool signature = request.encoded.Count() >= 4 && request.encoded[0] == 0x76 &&
                               request.encoded[1] == 0x2f && request.encoded[2] == 0x31 && request.encoded[3] == 0x01;
        return signature ? tools::TextureProbeResult::Exact :
               (IsHint(request.typeHint, "exr") ? tools::TextureProbeResult::Possible : tools::TextureProbeResult::NoMatch);
    }

    [[nodiscard]] tools::TextureImportResult DecodeOpenExr(const tools::TextureImportRequest& request,
                                                            tools::ImportedTexture& output, void*) noexcept
    {
        if (ProbeOpenExr(request, nullptr) != tools::TextureProbeResult::Exact)
            return tools::TextureImportResult::DecodeFailure;
        const u64 previousBudget = g_exrAllocationBudget;
        const u64 previousAllocated = g_exrAllocatedBytes;
        g_exrAllocationBudget = request.limits.maximumDecodedBytes;
        g_exrAllocatedBytes = 0;

        ExrMemory stream{request.encoded.Data(), request.encoded.SizeInBytes()};
        exr_context_initializer_t initializer = EXR_DEFAULT_CONTEXT_INITIALIZER;
        initializer.error_handler_fn = &IgnoreExrError;
        initializer.alloc_fn = &AllocateExr;
        initializer.free_fn = &FreeExr;
        initializer.user_data = &stream;
        initializer.read_fn = &ReadExr;
        initializer.size_fn = &SizeExr;
        initializer.max_image_width = request.limits.maximumDimension > 0x7fffffffu
            ? 0x7fffffff : static_cast<int>(request.limits.maximumDimension);
        initializer.max_image_height = initializer.max_image_width;
        initializer.max_tile_width = initializer.max_image_width;
        initializer.max_tile_height = initializer.max_image_width;
        initializer.flags = EXR_CONTEXT_FLAG_STRICT_HEADER | EXR_CONTEXT_FLAG_DISABLE_CHUNK_RECONSTRUCTION |
                            EXR_CONTEXT_FLAG_SILENT_HEADER_PARSE;
        exr_context_t context = nullptr;
        exr_result_t exrResult = exr_start_read(&context, "Vanguard memory OpenEXR", &initializer);
        if (exrResult != EXR_ERR_SUCCESS)
        {
            g_exrAllocationBudget = previousBudget;
            g_exrAllocatedBytes = previousAllocated;
            return exrResult == EXR_ERR_OUT_OF_MEMORY ? tools::TextureImportResult::OutOfMemory :
                                                       tools::TextureImportResult::DecodeFailure;
        }

        tools::TextureImportResult result = tools::TextureImportResult::DecodeFailure;
        int partCount = 0;
        exr_storage_t storage = EXR_STORAGE_LAST_TYPE;
        exr_attr_box2i_t dataWindow{};
        const exr_attr_chlist_t* channels = nullptr;
        if (exr_get_count(context, &partCount) != EXR_ERR_SUCCESS || partCount != 1)
            result = partCount > 1 ? tools::TextureImportResult::MultipleImagesUnsupported :
                                   tools::TextureImportResult::DecodeFailure;
        else if (exr_get_storage(context, 0, &storage) != EXR_ERR_SUCCESS ||
                 exr_get_data_window(context, 0, &dataWindow) != EXR_ERR_SUCCESS ||
                 exr_get_channels(context, 0, &channels) != EXR_ERR_SUCCESS || channels == nullptr)
            result = tools::TextureImportResult::DecodeFailure;
        else if (storage == EXR_STORAGE_DEEP_SCANLINE || storage == EXR_STORAGE_DEEP_TILED)
            result = tools::TextureImportResult::UnsupportedFormat;
        else result = tools::TextureImportResult::Success;

        const int64_t width64 = static_cast<int64_t>(dataWindow.max.x) - dataWindow.min.x + 1;
        const int64_t height64 = static_cast<int64_t>(dataWindow.max.y) - dataWindow.min.y + 1;
        if (result == tools::TextureImportResult::Success &&
            (width64 <= 0 || height64 <= 0 || width64 > request.limits.maximumDimension ||
             height64 > request.limits.maximumDimension))
            result = width64 > request.limits.maximumDimension || height64 > request.limits.maximumDimension
                ? tools::TextureImportResult::LimitExceeded : tools::TextureImportResult::DecodeFailure;

        SelectedExrChannels selected;
        if (result == tools::TextureImportResult::Success && !SelectChannels(*channels, selected))
            result = tools::TextureImportResult::UnsupportedFormat;
        bool useFloat = false;
        if (result == tools::TextureImportResult::Success)
        {
            const int allSelected[5] = {selected.red, selected.green, selected.blue, selected.alpha, selected.luminance};
            for (const int index : allSelected)
            {
                if (index < 0) continue;
                const exr_attr_chlist_entry_t& channel = channels->entries[index];
                if (channel.x_sampling != 1 || channel.y_sampling != 1 ||
                    (channel.pixel_type != EXR_PIXEL_HALF && channel.pixel_type != EXR_PIXEL_FLOAT))
                {
                    result = tools::TextureImportResult::UnsupportedFormat;
                    break;
                }
                if (channel.pixel_type == EXR_PIXEL_FLOAT) useFloat = true;
            }
        }

        int32_t tileWidth = 0;
        int32_t tileHeight = 0;
        if (result == tools::TextureImportResult::Success && storage == EXR_STORAGE_TILED)
        {
            int32_t levelsX = 0;
            int32_t levelsY = 0;
            if (exr_get_tile_levels(context, 0, &levelsX, &levelsY) != EXR_ERR_SUCCESS ||
                exr_get_tile_sizes(context, 0, 0, 0, &tileWidth, &tileHeight) != EXR_ERR_SUCCESS ||
                tileWidth <= 0 || tileHeight <= 0)
                result = tools::TextureImportResult::DecodeFailure;
            else if (levelsX != 1 || levelsY != 1)
                result = tools::TextureImportResult::MultipleImagesUnsupported;
        }

        const u32 width = width64 > 0 ? static_cast<u32>(width64) : 0;
        const u32 height = height64 > 0 ? static_cast<u32>(height64) : 0;
        const tools::SourcePixelFormat format = useFloat ? tools::SourcePixelFormat::R32G32B32A32Float :
                                                          tools::SourcePixelFormat::R16G16B16A16Float;
        const u32 componentBytes = useFloat ? 4u : 2u;
        const u32 pixelBytes = componentBytes * 4u;
        const u64 rowPitch = static_cast<u64>(width) * pixelBytes;
        if (result == tools::TextureImportResult::Success &&
            (rowPitch > 0xffffffffull || rowPitch * height > request.limits.maximumDecodedBytes))
            result = tools::TextureImportResult::LimitExceeded;
        if (result == tools::TextureImportResult::Success)
            result = output.Initialize2D(format, textures::ColorSpace::Linear, width, height, static_cast<u32>(rowPitch),
                                         Fingerprint(request), tools::ResolveImportedProfile(request.usage, format),
                                         tools::importers::OpenExr, 1, request.limits);
        if (result == tools::TextureImportResult::Success)
        {
            const u64 pixelCount = static_cast<u64>(width) * height;
            for (u64 pixel = 0; pixel < pixelCount; ++pixel)
            {
                u8* const destination = output.MutableImageData() + static_cast<usize>(pixel) * pixelBytes;
                for (u32 channel = 0; channel < 3; ++channel)
                    if (useFloat) WriteFloat(destination + channel * componentBytes, 0.0f);
                    else WriteHalf(destination + channel * componentBytes, 0);
                if (useFloat) WriteFloat(destination + 3u * componentBytes, 1.0f);
                else WriteHalf(destination + 3u * componentBytes, 0x3c00u);
            }
        }

        const auto decodeChunk = [&](const exr_chunk_info_t& chunk, const u32 outputX, const u32 outputY) noexcept
        {
            exr_decode_pipeline_t decoder = EXR_DECODE_PIPELINE_INITIALIZER;
            exr_result_t decodeResult = exr_decoding_initialize(context, 0, &chunk, &decoder);
            if (decodeResult != EXR_ERR_SUCCESS) return decodeResult;
            for (int channel = 0; channel < decoder.channel_count; ++channel)
            {
                int destinationChannel = -1;
                if (selected.luminance >= 0 && EqualName(decoder.channels[channel].channel_name,
                                                         channels->entries[selected.luminance].name.str))
                    destinationChannel = 0;
                else if (selected.red >= 0 && EqualName(decoder.channels[channel].channel_name,
                                                        channels->entries[selected.red].name.str))
                    destinationChannel = 0;
                else if (selected.green >= 0 && EqualName(decoder.channels[channel].channel_name,
                                                          channels->entries[selected.green].name.str))
                    destinationChannel = 1;
                else if (selected.blue >= 0 && EqualName(decoder.channels[channel].channel_name,
                                                         channels->entries[selected.blue].name.str))
                    destinationChannel = 2;
                else if (selected.alpha >= 0 && EqualName(decoder.channels[channel].channel_name,
                                                          channels->entries[selected.alpha].name.str))
                    destinationChannel = 3;
                if (destinationChannel < 0) continue;
                decoder.channels[channel].decode_to_ptr = output.MutableImageData() +
                    (static_cast<usize>(outputY) * width + outputX) * pixelBytes +
                    static_cast<u32>(destinationChannel) * componentBytes;
                decoder.channels[channel].user_pixel_stride = static_cast<int32_t>(pixelBytes);
                decoder.channels[channel].user_line_stride = static_cast<int32_t>(rowPitch);
                decoder.channels[channel].user_bytes_per_element = static_cast<int16_t>(componentBytes);
                decoder.channels[channel].user_data_type = static_cast<uint16_t>(
                    useFloat ? EXR_PIXEL_FLOAT : EXR_PIXEL_HALF);
            }
            decodeResult = exr_decoding_choose_default_routines(context, 0, &decoder);
            if (decodeResult == EXR_ERR_SUCCESS) decodeResult = exr_decoding_run(context, 0, &decoder);
            const exr_result_t destroyResult = exr_decoding_destroy(context, &decoder);
            return decodeResult == EXR_ERR_SUCCESS ? destroyResult : decodeResult;
        };

        if (result == tools::TextureImportResult::Success && storage == EXR_STORAGE_SCANLINE)
        {
            int32_t linesPerChunk = 0;
            if (exr_get_scanlines_per_chunk(context, 0, &linesPerChunk) != EXR_ERR_SUCCESS || linesPerChunk <= 0)
                result = tools::TextureImportResult::DecodeFailure;
            for (int y = dataWindow.min.y; result == tools::TextureImportResult::Success && y <= dataWindow.max.y;
                 y += linesPerChunk)
            {
                exr_chunk_info_t chunk{};
                if (exr_read_scanline_chunk_info(context, 0, y, &chunk) != EXR_ERR_SUCCESS ||
                    decodeChunk(chunk, 0, static_cast<u32>(chunk.start_y - dataWindow.min.y)) != EXR_ERR_SUCCESS)
                    result = tools::TextureImportResult::DecodeFailure;
            }
        }
        else if (result == tools::TextureImportResult::Success && storage == EXR_STORAGE_TILED)
        {
            int32_t countX = 0;
            int32_t countY = 0;
            if (exr_get_tile_counts(context, 0, 0, 0, &countX, &countY) != EXR_ERR_SUCCESS || countX <= 0 || countY <= 0)
                result = tools::TextureImportResult::DecodeFailure;
            for (int32_t tileY = 0; result == tools::TextureImportResult::Success && tileY < countY; ++tileY)
            {
                for (int32_t tileX = 0; result == tools::TextureImportResult::Success && tileX < countX; ++tileX)
                {
                    exr_chunk_info_t chunk{};
                    if (exr_read_tile_chunk_info(context, 0, tileX, tileY, 0, 0, &chunk) != EXR_ERR_SUCCESS ||
                        decodeChunk(chunk, static_cast<u32>(tileX * tileWidth),
                                    static_cast<u32>(tileY * tileHeight)) != EXR_ERR_SUCCESS)
                        result = tools::TextureImportResult::DecodeFailure;
                }
            }
        }

        if (result == tools::TextureImportResult::Success && selected.luminance >= 0)
        {
            const u64 pixelCount = static_cast<u64>(width) * height;
            for (u64 pixel = 0; pixel < pixelCount; ++pixel)
            {
                u8* const destination = output.MutableImageData() + static_cast<usize>(pixel) * pixelBytes;
                for (u32 channel = 1; channel < 3; ++channel)
                    for (u32 byte = 0; byte < componentBytes; ++byte)
                        destination[channel * componentBytes + byte] = destination[byte];
            }
        }

        exr_finish(&context);
        if (result != tools::TextureImportResult::Success) output.Reset();
        g_exrAllocationBudget = previousBudget;
        g_exrAllocatedBytes = previousAllocated;
        return result;
    }

    const tools::TextureImporterDescriptor g_openExrImporter{
        tools::importers::OpenExr, "OpenEXR", 1, &ProbeOpenExr, &DecodeOpenExr, nullptr};
}

namespace vanguard::texture_tools
{
    const TextureImporterDescriptor& OpenExrImporter() noexcept { return g_openExrImporter; }
} // namespace vanguard::texture_tools
