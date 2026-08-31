#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <bc7enc.h>
#include <cmp_core.h>
#include <rgbcx.h>

#include <cmath>
#include <cstring>

namespace
{
    namespace containers = vanguard::containers;
    namespace concurrency = vanguard::concurrency;
    namespace crypto = vanguard::crypto;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace textures = vanguard::textures;
    namespace tools = vanguard::texture_tools;
    using vanguard::f32;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::u8;
    using vanguard::usize;

    constexpr u32 MaximumProfiles = 32;
    tools::TextureCookingProfile g_profiles[MaximumProfiles]{};
    u32 g_profileCount = 0;
    bool g_initialized = false;
    bool g_registrySealed = false;
    crypto::Digest256 g_profileRegistryFingerprint;

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

    struct Float4
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 1.0f;
    };

    struct Float3
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
    };

    struct Image
    {
        Image() noexcept : pixels(memory::pools::Assets::GetInstance()) {}

        u32 width = 0;
        u32 height = 0;
        u32 depth = 0;
        containers::DynamicArray<Float4> pixels;
    };

    struct EncodedImage
    {
        EncodedImage() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        containers::DynamicArray<u8> bytes;
        u32 rowPitch = 0;
        u32 slicePitch = 0;
    };

    enum class DecodeResult : u8
    {
        Success,
        InvalidLayout,
        InvalidData,
        OutOfMemory
    };

    [[nodiscard]] f32 Clamp01(const f32 value) noexcept
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    [[nodiscard]] u8 ToByte(const f32 value) noexcept
    {
        return static_cast<u8>(Clamp01(value) * 255.0f + 0.5f);
    }

    [[nodiscard]] f32 SrgbToLinear(const f32 value) noexcept
    {
        return value <= 0.04045f ? value / 12.92f : ::powf((value + 0.055f) / 1.055f, 2.4f);
    }

    [[nodiscard]] f32 LinearToSrgb(const f32 value) noexcept
    {
        const f32 clamped = Clamp01(value);
        return clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * ::powf(clamped, 1.0f / 2.4f) - 0.055f;
    }

    [[nodiscard]] f32 HalfToFloat(const u16 value) noexcept
    {
        const u32 sign = static_cast<u32>(value & 0x8000u) << 16u;
        vanguard::i32 exponent = (value >> 10u) & 0x1fu;
        u32 mantissa = value & 0x3ffu;
        u32 bits = 0;
        if (exponent == 0)
        {
            if (mantissa != 0)
            {
                exponent = 1;
                while ((mantissa & 0x400u) == 0)
                {
                    mantissa <<= 1u;
                    --exponent;
                }
                mantissa &= 0x3ffu;
                bits = sign | (static_cast<u32>(exponent + 112) << 23u) | (mantissa << 13u);
            }
            else
            {
                bits = sign;
            }
        }
        else if (exponent == 31)
        {
            bits = sign | 0x7f800000u | (mantissa << 13u);
        }
        else
        {
            bits = sign | (static_cast<u32>(exponent + 112) << 23u) | (mantissa << 13u);
        }
        union
        {
            u32 integer;
            f32 floating;
        } conversion{bits};
        return conversion.floating;
    }

    [[nodiscard]] u16 ReadU16(const u8* data) noexcept
    {
        return static_cast<u16>(data[0]) | static_cast<u16>(static_cast<u16>(data[1]) << 8u);
    }

    [[nodiscard]] f32 ReadF32(const u8* data) noexcept
    {
        union
        {
            u32 integer;
            f32 floating;
        } conversion{static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8u) | (static_cast<u32>(data[2]) << 16u) | (static_cast<u32>(data[3]) << 24u)};
        return conversion.floating;
    }

    [[nodiscard]] bool IsFinite(const f32 value) noexcept
    {
        union
        {
            f32 floating;
            u32 integer;
        } conversion{value};
        return (conversion.integer & 0x7f800000u) != 0x7f800000u;
    }

    [[nodiscard]] u16 FloatToHalf(const f32 value) noexcept
    {
        union
        {
            f32 floating;
            u32 integer;
        } conversion{value};
        const u32 sign = (conversion.integer >> 16u) & 0x8000u;
        vanguard::i32 exponent = static_cast<vanguard::i32>((conversion.integer >> 23u) & 0xffu) - 127 + 15;
        u32 mantissa = conversion.integer & 0x7fffffu;
        if (exponent <= 0)
        {
            if (exponent < -10)
                return static_cast<u16>(sign);
            mantissa = (mantissa | 0x800000u) >> static_cast<u32>(1 - exponent);
            return static_cast<u16>(sign | ((mantissa + 0xfffu + ((mantissa >> 13u) & 1u)) >> 13u));
        }
        if (exponent >= 31)
        {
            const bool notANumber = ((conversion.integer >> 23u) & 0xffu) == 0xffu && mantissa != 0;
            return static_cast<u16>(sign | (notANumber ? 0x7e00u : 0x7c00u));
        }
        mantissa += 0xfffu + ((mantissa >> 13u) & 1u);
        if ((mantissa & 0x800000u) != 0)
        {
            mantissa = 0;
            ++exponent;
            if (exponent >= 31)
                return static_cast<u16>(sign | 0x7c00u);
        }
        return static_cast<u16>(sign | (static_cast<u32>(exponent) << 10u) | (mantissa >> 13u));
    }

    [[nodiscard]] u32 SourceBytesPerPixel(const tools::SourcePixelFormat format) noexcept
    {
        switch (format)
        {
        case tools::SourcePixelFormat::R8UNorm:
            return 1;
        case tools::SourcePixelFormat::R8G8UNorm:
            return 2;
        case tools::SourcePixelFormat::R8G8B8A8UNorm:
            return 4;
        case tools::SourcePixelFormat::R16G16B16A16UNorm:
            return 8;
        case tools::SourcePixelFormat::R16G16B16A16Float:
            return 8;
        case tools::SourcePixelFormat::R32G32B32A32Float:
            return 16;
        }
        return 0;
    }

    [[nodiscard]] Float4 ReadSourcePixel(const u8* data, const tools::SourcePixelFormat format) noexcept
    {
        switch (format)
        {
        case tools::SourcePixelFormat::R8UNorm:
            return {data[0] / 255.0f, 0.0f, 0.0f, 1.0f};
        case tools::SourcePixelFormat::R8G8UNorm:
            return {data[0] / 255.0f, data[1] / 255.0f, 0.0f, 1.0f};
        case tools::SourcePixelFormat::R8G8B8A8UNorm:
            return {data[0] / 255.0f, data[1] / 255.0f, data[2] / 255.0f, data[3] / 255.0f};
        case tools::SourcePixelFormat::R16G16B16A16UNorm:
            return {ReadU16(data + 0) / 65535.0f, ReadU16(data + 2) / 65535.0f, ReadU16(data + 4) / 65535.0f, ReadU16(data + 6) / 65535.0f};
        case tools::SourcePixelFormat::R16G16B16A16Float:
        {
            return {HalfToFloat(ReadU16(data + 0)), HalfToFloat(ReadU16(data + 2)), HalfToFloat(ReadU16(data + 4)), HalfToFloat(ReadU16(data + 6))};
        }
        case tools::SourcePixelFormat::R32G32B32A32Float:
        {
            return {ReadF32(data + 0), ReadF32(data + 4), ReadF32(data + 8), ReadF32(data + 12)};
        }
        }
        return {};
    }

    [[nodiscard]] DecodeResult DecodeSourceImage(const tools::SourceTexture& source, const tools::SourceImage& input, const bool decodeSrgb,
                                                 Image& output) noexcept
    {
        const u32 pixelSize = SourceBytesPerPixel(source.format);
        const u64 minimumRow = static_cast<u64>(source.width) * pixelSize;
        const u64 minimumSlice = static_cast<u64>(input.rowPitch) * source.height;
        const u64 required = static_cast<u64>(input.slicePitch) * (source.depth - 1u) + minimumSlice;
        if (pixelSize == 0 || input.data == nullptr || input.rowPitch < minimumRow || input.slicePitch < minimumSlice || input.byteSize < required ||
            static_cast<u64>(source.width) * source.height * source.depth > 0xffffffffull)
        {
            return DecodeResult::InvalidLayout;
        }
        output.width = source.width;
        output.height = source.height;
        output.depth = source.depth;
        output.pixels.Resize(source.width * source.height * source.depth);
        if (output.pixels.Size() != source.width * source.height * source.depth)
        {
            return DecodeResult::OutOfMemory;
        }
        const auto* bytes = static_cast<const u8*>(input.data);
        for (u32 z = 0; z < source.depth; ++z)
        {
            for (u32 y = 0; y < source.height; ++y)
            {
                const u8* row = bytes + static_cast<usize>(z) * input.slicePitch + static_cast<usize>(y) * input.rowPitch;
                for (u32 x = 0; x < source.width; ++x)
                {
                    Float4 pixel = ReadSourcePixel(row + static_cast<usize>(x) * pixelSize, source.format);
                    if (!IsFinite(pixel.x) || !IsFinite(pixel.y) || !IsFinite(pixel.z) || !IsFinite(pixel.w))
                    {
                        return DecodeResult::InvalidData;
                    }
                    if (decodeSrgb)
                    {
                        pixel.x = SrgbToLinear(pixel.x);
                        pixel.y = SrgbToLinear(pixel.y);
                        pixel.z = SrgbToLinear(pixel.z);
                    }
                    output.pixels[(z * source.height + y) * source.width + x] = pixel;
                }
            }
        }
        return DecodeResult::Success;
    }

    void NormalizeNormals(Image& image) noexcept
    {
        for (Float4& pixel : image.pixels)
        {
            f32 x = pixel.x * 2.0f - 1.0f;
            f32 y = pixel.y * 2.0f - 1.0f;
            f32 z = pixel.z * 2.0f - 1.0f;
            const f32 lengthSquared = x * x + y * y + z * z;
            if (lengthSquared > 0.0000001f)
            {
                const f32 inverseLength = 1.0f / ::sqrtf(lengthSquared);
                x *= inverseLength;
                y *= inverseLength;
                z *= inverseLength;
            }
            else
            {
                x = 0.0f;
                y = 0.0f;
                z = 1.0f;
            }
            pixel.x = x * 0.5f + 0.5f;
            pixel.y = y * 0.5f + 0.5f;
            pixel.z = z * 0.5f + 0.5f;
        }
    }

    [[nodiscard]] bool Downsample(const Image& source, Image& destination) noexcept
    {
        destination.width = source.width > 1 ? source.width / 2u : 1u;
        destination.height = source.height > 1 ? source.height / 2u : 1u;
        destination.depth = source.depth > 1 ? source.depth / 2u : 1u;
        destination.pixels.Resize(destination.width * destination.height * destination.depth);
        if (destination.pixels.Size() != destination.width * destination.height * destination.depth)
        {
            return false;
        }
        for (u32 z = 0; z < destination.depth; ++z)
        {
            const u64 startZ = static_cast<u64>(z) * source.depth;
            const u64 endZ = static_cast<u64>(z + 1u) * source.depth;
            const u32 firstZ = static_cast<u32>(startZ / destination.depth);
            const u32 lastZ = static_cast<u32>((endZ + destination.depth - 1u) / destination.depth);
            for (u32 y = 0; y < destination.height; ++y)
            {
                const u64 startY = static_cast<u64>(y) * source.height;
                const u64 endY = static_cast<u64>(y + 1u) * source.height;
                const u32 firstY = static_cast<u32>(startY / destination.height);
                const u32 lastY = static_cast<u32>((endY + destination.height - 1u) / destination.height);
                for (u32 x = 0; x < destination.width; ++x)
                {
                    const u64 startX = static_cast<u64>(x) * source.width;
                    const u64 endX = static_cast<u64>(x + 1u) * source.width;
                    const u32 firstX = static_cast<u32>(startX / destination.width);
                    const u32 lastX = static_cast<u32>((endX + destination.width - 1u) / destination.width);
                    Float4 result{0.0f, 0.0f, 0.0f, 0.0f};
                    f32 totalWeight = 0.0f;
                    for (u32 sourceZ = firstZ; sourceZ < lastZ; ++sourceZ)
                    {
                        const u64 sourceZStart = static_cast<u64>(sourceZ) * destination.depth;
                        const u64 sourceZEnd = static_cast<u64>(sourceZ + 1u) * destination.depth;
                        const u64 overlapZ = (endZ < sourceZEnd ? endZ : sourceZEnd) - (startZ > sourceZStart ? startZ : sourceZStart);
                        for (u32 sourceY = firstY; sourceY < lastY; ++sourceY)
                        {
                            const u64 sourceYStart = static_cast<u64>(sourceY) * destination.height;
                            const u64 sourceYEnd = static_cast<u64>(sourceY + 1u) * destination.height;
                            const u64 overlapY = (endY < sourceYEnd ? endY : sourceYEnd) - (startY > sourceYStart ? startY : sourceYStart);
                            for (u32 sourceX = firstX; sourceX < lastX; ++sourceX)
                            {
                                const u64 sourceXStart = static_cast<u64>(sourceX) * destination.width;
                                const u64 sourceXEnd = static_cast<u64>(sourceX + 1u) * destination.width;
                                const u64 overlapX = (endX < sourceXEnd ? endX : sourceXEnd) - (startX > sourceXStart ? startX : sourceXStart);
                                const f32 weight = static_cast<f32>(overlapX * overlapY * overlapZ);
                                const Float4& pixel = source.pixels[(sourceZ * source.height + sourceY) * source.width + sourceX];
                                result.x += pixel.x * weight;
                                result.y += pixel.y * weight;
                                result.z += pixel.z * weight;
                                result.w += pixel.w * weight;
                                totalWeight += weight;
                            }
                        }
                    }
                    const f32 inverseWeight = 1.0f / totalWeight;
                    result.x *= inverseWeight;
                    result.y *= inverseWeight;
                    result.z *= inverseWeight;
                    result.w *= inverseWeight;
                    destination.pixels[(z * destination.height + y) * destination.width + x] = result;
                }
            }
        }
        return true;
    }

    [[nodiscard]] Float3 Normalize(const Float3 value) noexcept
    {
        const f32 inverseLength = 1.0f / ::sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
        return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
    }

    [[nodiscard]] Float3 Cross(const Float3 left, const Float3 right) noexcept
    {
        return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z, left.x * right.y - left.y * right.x};
    }

    /// Direct3D/NVRHI cube convention: +X, -X, +Y, -Y, +Z, -Z, with image Y growing downward.
    /// Coordinates use texel-corner space; a texel center is therefore (x + 0.5, y + 0.5).
    [[nodiscard]] Float3 CubeDirection(const u32 face, const f32 x, const f32 y, const u32 extent) noexcept
    {
        const f32 u = x * (2.0f / static_cast<f32>(extent)) - 1.0f;
        const f32 v = y * (2.0f / static_cast<f32>(extent)) - 1.0f;
        switch (face)
        {
        case 0:
            return Normalize({1.0f, -v, -u});
        case 1:
            return Normalize({-1.0f, -v, u});
        case 2:
            return Normalize({u, 1.0f, v});
        case 3:
            return Normalize({u, -1.0f, -v});
        case 4:
            return Normalize({u, -v, 1.0f});
        default:
            return Normalize({-u, -v, -1.0f});
        }
    }

    struct CubeProjection
    {
        u32 face = 0;
        f32 u = 0.0f;
        f32 v = 0.0f;
    };

    [[nodiscard]] CubeProjection ProjectCubeDirection(const Float3 direction) noexcept
    {
        const f32 absoluteX = ::fabsf(direction.x);
        const f32 absoluteY = ::fabsf(direction.y);
        const f32 absoluteZ = ::fabsf(direction.z);
        if (absoluteX >= absoluteY && absoluteX >= absoluteZ)
        {
            return direction.x >= 0.0f ? CubeProjection{0, -direction.z / absoluteX, -direction.y / absoluteX}
                                       : CubeProjection{1, direction.z / absoluteX, -direction.y / absoluteX};
        }
        if (absoluteY >= absoluteZ)
        {
            return direction.y >= 0.0f ? CubeProjection{2, direction.x / absoluteY, direction.z / absoluteY}
                                       : CubeProjection{3, direction.x / absoluteY, -direction.z / absoluteY};
        }
        return direction.z >= 0.0f ? CubeProjection{4, direction.x / absoluteZ, -direction.y / absoluteZ}
                                   : CubeProjection{5, -direction.x / absoluteZ, -direction.y / absoluteZ};
    }

    [[nodiscard]] u32 ClampTexel(const vanguard::i32 coordinate, const u32 extent) noexcept
    {
        return coordinate < 0 ? 0u : (static_cast<u32>(coordinate) >= extent ? extent - 1u : static_cast<u32>(coordinate));
    }

    [[nodiscard]] Float4 CubeTap(const Image faces[6], const u32 primaryFace, const vanguard::i32 x, const vanguard::i32 y, const u32 extent) noexcept
    {
        if (x >= 0 && y >= 0 && static_cast<u32>(x) < extent && static_cast<u32>(y) < extent)
            return faces[primaryFace].pixels[static_cast<u32>(y) * extent + static_cast<u32>(x)];

        const CubeProjection projection = ProjectCubeDirection(CubeDirection(primaryFace, static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, extent));
        const vanguard::i32 projectedX = static_cast<vanguard::i32>(::floorf((projection.u + 1.0f) * 0.5f * extent));
        const vanguard::i32 projectedY = static_cast<vanguard::i32>(::floorf((projection.v + 1.0f) * 0.5f * extent));
        return faces[projection.face].pixels[ClampTexel(projectedY, extent) * extent + ClampTexel(projectedX, extent)];
    }

    [[nodiscard]] Float4 SampleCube(const Image faces[6], const Float3 direction, const u32 extent) noexcept
    {
        const CubeProjection projection = ProjectCubeDirection(direction);
        const f32 x = (projection.u + 1.0f) * 0.5f * extent - 0.5f;
        const f32 y = (projection.v + 1.0f) * 0.5f * extent - 0.5f;
        const vanguard::i32 firstX = static_cast<vanguard::i32>(::floorf(x));
        const vanguard::i32 firstY = static_cast<vanguard::i32>(::floorf(y));
        const f32 fractionX = x - static_cast<f32>(firstX);
        const f32 fractionY = y - static_cast<f32>(firstY);
        const Float4 taps[4] = {CubeTap(faces, projection.face, firstX, firstY, extent), CubeTap(faces, projection.face, firstX + 1, firstY, extent),
                                CubeTap(faces, projection.face, firstX, firstY + 1, extent), CubeTap(faces, projection.face, firstX + 1, firstY + 1, extent)};
        const f32 weights[4] = {(1.0f - fractionX) * (1.0f - fractionY), fractionX * (1.0f - fractionY), (1.0f - fractionX) * fractionY, fractionX * fractionY};
        Float4 result{0.0f, 0.0f, 0.0f, 0.0f};
        for (u32 tap = 0; tap < 4; ++tap)
        {
            result.x += taps[tap].x * weights[tap];
            result.y += taps[tap].y * weights[tap];
            result.z += taps[tap].z * weights[tap];
            result.w += taps[tap].w * weights[tap];
        }
        return result;
    }

    /// Angular-domain quadrature gives every destination texel a comparable spherical footprint, independent of which
    /// cube face contains a tap. Cross-face bilinear taps are remapped instead of clamped.
    [[nodiscard]] Float4 FilterCubeDirection(const Image faces[6], const Float3 center, const u32 sourceExtent, const u32 destinationExtent) noexcept
    {
        const Float3 reference = ::fabsf(center.x) <= ::fabsf(center.y) && ::fabsf(center.x) <= ::fabsf(center.z)
                                     ? Float3{1.0f, 0.0f, 0.0f}
                                     : (::fabsf(center.y) <= ::fabsf(center.z) ? Float3{0.0f, 1.0f, 0.0f} : Float3{0.0f, 0.0f, 1.0f});
        const Float3 tangent = Normalize(Cross(reference, center));
        const Float3 bitangent = Cross(center, tangent);
        constexpr f32 Offsets[4] = {-0.75f, -0.25f, 0.25f, 0.75f};
        constexpr f32 Weights[4] = {0.25f, 0.75f, 0.75f, 0.25f};
        const f32 angularTexelRadius = 1.0f / static_cast<f32>(destinationExtent);
        Float4 result{0.0f, 0.0f, 0.0f, 0.0f};
        f32 totalWeight = 0.0f;
        for (u32 sampleY = 0; sampleY < 4; ++sampleY)
        {
            for (u32 sampleX = 0; sampleX < 4; ++sampleX)
            {
                const Float3 direction =
                    Normalize({center.x + tangent.x * Offsets[sampleX] * angularTexelRadius + bitangent.x * Offsets[sampleY] * angularTexelRadius,
                               center.y + tangent.y * Offsets[sampleX] * angularTexelRadius + bitangent.y * Offsets[sampleY] * angularTexelRadius,
                               center.z + tangent.z * Offsets[sampleX] * angularTexelRadius + bitangent.z * Offsets[sampleY] * angularTexelRadius});
                const Float4 sample = SampleCube(faces, direction, sourceExtent);
                const f32 weight = Weights[sampleX] * Weights[sampleY];
                result.x += sample.x * weight;
                result.y += sample.y * weight;
                result.z += sample.z * weight;
                result.w += sample.w * weight;
                totalWeight += weight;
            }
        }
        const f32 inverseWeight = 1.0f / totalWeight;
        return {result.x * inverseWeight, result.y * inverseWeight, result.z * inverseWeight, result.w * inverseWeight};
    }

    void FixCubeEdges(Image faces[6]) noexcept
    {
        const u32 extent = faces[0].width;
        if (extent == 1)
        {
            Float4 average{0.0f, 0.0f, 0.0f, 0.0f};
            for (u32 face = 0; face < 6; ++face)
            {
                average.x += faces[face].pixels[0].x;
                average.y += faces[face].pixels[0].y;
                average.z += faces[face].pixels[0].z;
                average.w += faces[face].pixels[0].w;
            }
            average = {average.x / 6.0f, average.y / 6.0f, average.z / 6.0f, average.w / 6.0f};
            for (u32 face = 0; face < 6; ++face)
                faces[face].pixels[0] = average;
            return;
        }

        constexpr f32 Outside = 0.001f;
        for (u32 face = 0; face < 6; ++face)
        {
            for (u32 edge = 0; edge < 4; ++edge)
            {
                for (u32 coordinate = 1; coordinate + 1 < extent; ++coordinate)
                {
                    const u32 x = edge == 0 ? 0u : (edge == 1 ? extent - 1u : coordinate);
                    const u32 y = edge == 2 ? 0u : (edge == 3 ? extent - 1u : coordinate);
                    const f32 outsideX = edge == 0 ? -Outside : (edge == 1 ? static_cast<f32>(extent) + Outside : static_cast<f32>(coordinate) + 0.5f);
                    const f32 outsideY = edge == 2 ? -Outside : (edge == 3 ? static_cast<f32>(extent) + Outside : static_cast<f32>(coordinate) + 0.5f);
                    const CubeProjection neighbor = ProjectCubeDirection(CubeDirection(face, outsideX, outsideY, extent));
                    const u32 neighborX = ClampTexel(static_cast<vanguard::i32>(::floorf((neighbor.u + 1.0f) * 0.5f * extent)), extent);
                    const u32 neighborY = ClampTexel(static_cast<vanguard::i32>(::floorf((neighbor.v + 1.0f) * 0.5f * extent)), extent);
                    const u32 address = face * extent * extent + y * extent + x;
                    const u32 neighborAddress = neighbor.face * extent * extent + neighborY * extent + neighborX;
                    if (address >= neighborAddress)
                        continue;
                    Float4& first = faces[face].pixels[y * extent + x];
                    Float4& second = faces[neighbor.face].pixels[neighborY * extent + neighborX];
                    const Float4 average{(first.x + second.x) * 0.5f, (first.y + second.y) * 0.5f, (first.z + second.z) * 0.5f, (first.w + second.w) * 0.5f};
                    first = average;
                    second = average;
                }
            }
        }

        // Each geometric cube corner is represented by exactly three face-corner texels.
        for (vanguard::i32 signX = -1; signX <= 1; signX += 2)
        {
            for (vanguard::i32 signY = -1; signY <= 1; signY += 2)
            {
                for (vanguard::i32 signZ = -1; signZ <= 1; signZ += 2)
                {
                    Float4 sum{0.0f, 0.0f, 0.0f, 0.0f};
                    u32 matchedFaces[3]{};
                    u32 matchedPixels[3]{};
                    u32 matchCount = 0;
                    for (u32 face = 0; face < 6; ++face)
                    {
                        for (u32 corner = 0; corner < 4; ++corner)
                        {
                            const u32 x = (corner & 1u) != 0 ? extent - 1u : 0u;
                            const u32 y = (corner & 2u) != 0 ? extent - 1u : 0u;
                            const Float3 direction = CubeDirection(face, (corner & 1u) != 0 ? static_cast<f32>(extent) : 0.0f,
                                                                   (corner & 2u) != 0 ? static_cast<f32>(extent) : 0.0f, extent);
                            if ((direction.x > 0.0f ? 1 : -1) != signX || (direction.y > 0.0f ? 1 : -1) != signY || (direction.z > 0.0f ? 1 : -1) != signZ)
                                continue;
                            if (matchCount < 3)
                            {
                                const Float4& pixel = faces[face].pixels[y * extent + x];
                                matchedFaces[matchCount] = face;
                                matchedPixels[matchCount] = y * extent + x;
                                sum.x += pixel.x;
                                sum.y += pixel.y;
                                sum.z += pixel.z;
                                sum.w += pixel.w;
                                ++matchCount;
                            }
                        }
                    }
                    if (matchCount == 3)
                    {
                        const Float4 average{sum.x / 3.0f, sum.y / 3.0f, sum.z / 3.0f, sum.w / 3.0f};
                        for (u32 match = 0; match < 3; ++match)
                            faces[matchedFaces[match]].pixels[matchedPixels[match]] = average;
                    }
                }
            }
        }
    }

    [[nodiscard]] bool DownsampleCube(const Image source[6], Image destination[6], const bool useJobs, const u32 maximumBatchSize, bool& jobFailure,
                                      bool& usedJobs) noexcept
    {
        const u32 sourceExtent = source[0].width;
        const u32 destinationExtent = sourceExtent > 1 ? sourceExtent / 2u : 1u;
        for (u32 face = 0; face < 6; ++face)
        {
            destination[face].width = destinationExtent;
            destination[face].height = destinationExtent;
            destination[face].depth = 1;
            destination[face].pixels.Resize(destinationExtent * destinationExtent);
            if (destination[face].pixels.Size() != destinationExtent * destinationExtent)
                return false;
        }
        const u32 workItemCount = 6u * destinationExtent * destinationExtent;
        const auto filter = [&source, &destination, sourceExtent, destinationExtent](const u32 workIndex) noexcept
        {
            const u32 pixelsPerFace = destinationExtent * destinationExtent;
            const u32 face = workIndex / pixelsPerFace;
            const u32 pixel = workIndex % pixelsPerFace;
            const u32 x = pixel % destinationExtent;
            const u32 y = pixel / destinationExtent;
            const Float3 direction = CubeDirection(face, static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, destinationExtent);
            destination[face].pixels[pixel] = FilterCubeDirection(source, direction, sourceExtent, destinationExtent);
        };
        if (useJobs && workItemCount > 1)
        {
            jobs::Builder builder({jobs::Priority::CriticalPath, jobs::Affinity::AnyWorker});
            jobs::JobName name{"Vanguard.TextureTools.FilterCubeMip"};
            jobs::ParallelTask task = jobs::ParallelTask::Create([&filter](const u32 workIndex, const jobs::JobContext&) noexcept { filter(workIndex); });
            if (!builder.IsValid() || !task || !builder.DispatchParallel(name, workItemCount, static_cast<jobs::ParallelTask&&>(task), {}, maximumBatchSize))
            {
                jobFailure = true;
                return false;
            }
            usedJobs = true;
            jobs::Counter counter = builder.ExtractCounter();
            if (!counter.IsValid() || !counter.Wait())
            {
                jobFailure = true;
                return false;
            }
        }
        else
        {
            for (u32 workIndex = 0; workIndex < workItemCount; ++workIndex)
                filter(workIndex);
        }
        FixCubeEdges(destination);
        return true;
    }

    [[nodiscard]] f32 AlphaCoverage(const Image& image, const u8 channel, const f32 threshold) noexcept
    {
        u32 covered = 0;
        for (const Float4& pixel : image.pixels)
        {
            const f32 values[4] = {pixel.x, pixel.y, pixel.z, pixel.w};
            covered += values[channel] >= threshold ? 1u : 0u;
        }
        return image.pixels.Size() != 0 ? static_cast<f32>(covered) / static_cast<f32>(image.pixels.Size()) : 0.0f;
    }

    [[nodiscard]] f32 PreserveCoverage(Image& image, const u8 channel, const f32 threshold, const f32 targetCoverage) noexcept
    {
        f32 lower = 0.0f;
        f32 upper = 8.0f;
        for (u32 iteration = 0; iteration < 12; ++iteration)
        {
            const f32 scale = (lower + upper) * 0.5f;
            u32 covered = 0;
            for (const Float4& pixel : image.pixels)
            {
                const f32 values[4] = {pixel.x, pixel.y, pixel.z, pixel.w};
                covered += Clamp01(values[channel] * scale) >= threshold ? 1u : 0u;
            }
            const f32 coverage = static_cast<f32>(covered) / static_cast<f32>(image.pixels.Size());
            if (coverage < targetCoverage)
                lower = scale;
            else
                upper = scale;
        }
        const f32 scale = (lower + upper) * 0.5f;
        for (Float4& pixel : image.pixels)
        {
            f32* values[4] = {&pixel.x, &pixel.y, &pixel.z, &pixel.w};
            *values[channel] = Clamp01(*values[channel] * scale);
        }
        const f32 achievedCoverage = AlphaCoverage(image, channel, threshold);
        return achievedCoverage > targetCoverage ? achievedCoverage - targetCoverage : targetCoverage - achievedCoverage;
    }

    [[nodiscard]] f32 CubeAlphaCoverage(const Image faces[6], const u8 channel, const f32 threshold) noexcept
    {
        u64 covered = 0;
        u64 texels = 0;
        for (u32 face = 0; face < 6; ++face)
        {
            for (const Float4& pixel : faces[face].pixels)
            {
                const f32 values[4] = {pixel.x, pixel.y, pixel.z, pixel.w};
                covered += values[channel] >= threshold ? 1u : 0u;
            }
            texels += faces[face].pixels.Size();
        }
        return texels != 0 ? static_cast<f32>(covered) / static_cast<f32>(texels) : 0.0f;
    }

    [[nodiscard]] f32 PreserveCubeCoverage(Image faces[6], const u8 channel, const f32 threshold, const f32 targetCoverage) noexcept
    {
        f32 lower = 0.0f;
        f32 upper = 8.0f;
        for (u32 iteration = 0; iteration < 12; ++iteration)
        {
            const f32 scale = (lower + upper) * 0.5f;
            u64 covered = 0;
            u64 texels = 0;
            for (u32 face = 0; face < 6; ++face)
            {
                for (const Float4& pixel : faces[face].pixels)
                {
                    const f32 values[4] = {pixel.x, pixel.y, pixel.z, pixel.w};
                    covered += Clamp01(values[channel] * scale) >= threshold ? 1u : 0u;
                }
                texels += faces[face].pixels.Size();
            }
            const f32 coverage = static_cast<f32>(covered) / static_cast<f32>(texels);
            if (coverage < targetCoverage)
                lower = scale;
            else
                upper = scale;
        }
        const f32 scale = (lower + upper) * 0.5f;
        for (u32 face = 0; face < 6; ++face)
        {
            for (Float4& pixel : faces[face].pixels)
            {
                f32* values[4] = {&pixel.x, &pixel.y, &pixel.z, &pixel.w};
                *values[channel] = Clamp01(*values[channel] * scale);
            }
        }
        const f32 achievedCoverage = CubeAlphaCoverage(faces, channel, threshold);
        return achievedCoverage > targetCoverage ? achievedCoverage - targetCoverage : targetCoverage - achievedCoverage;
    }

    void GatherBlock(const Image& image, const u32 blockX, const u32 blockY, const u32 z, const bool encodeSrgb, u8 block[64]) noexcept
    {
        for (u32 y = 0; y < 4; ++y)
        {
            const u32 sourceY = blockY * 4u + y < image.height ? blockY * 4u + y : image.height - 1u;
            for (u32 x = 0; x < 4; ++x)
            {
                const u32 sourceX = blockX * 4u + x < image.width ? blockX * 4u + x : image.width - 1u;
                Float4 pixel = image.pixels[(z * image.height + sourceY) * image.width + sourceX];
                if (encodeSrgb)
                {
                    pixel.x = LinearToSrgb(pixel.x);
                    pixel.y = LinearToSrgb(pixel.y);
                    pixel.z = LinearToSrgb(pixel.z);
                }
                const u32 destination = (y * 4u + x) * 4u;
                block[destination + 0] = ToByte(pixel.x);
                block[destination + 1] = ToByte(pixel.y);
                block[destination + 2] = ToByte(pixel.z);
                block[destination + 3] = ToByte(pixel.w);
            }
        }
    }

    void GatherHalfBlock(const Image& image, const u32 blockX, const u32 blockY, const u32 z, u16 block[64]) noexcept
    {
        for (u32 y = 0; y < 4; ++y)
        {
            const u32 sourceY = blockY * 4u + y < image.height ? blockY * 4u + y : image.height - 1u;
            for (u32 x = 0; x < 4; ++x)
            {
                const u32 sourceX = blockX * 4u + x < image.width ? blockX * 4u + x : image.width - 1u;
                const Float4& pixel = image.pixels[(z * image.height + sourceY) * image.width + sourceX];
                const u32 destination = (y * 4u + x) * 4u;
                block[destination + 0] = FloatToHalf(pixel.x);
                block[destination + 1] = FloatToHalf(pixel.y);
                block[destination + 2] = FloatToHalf(pixel.z);
                block[destination + 3] = FloatToHalf(pixel.w);
            }
        }
    }

    void GatherSignedBlock(const Image& image, const u32 blockX, const u32 blockY, const u32 z, char channelX[16], char channelY[16]) noexcept
    {
        for (u32 y = 0; y < 4; ++y)
        {
            const u32 sourceY = blockY * 4u + y < image.height ? blockY * 4u + y : image.height - 1u;
            for (u32 x = 0; x < 4; ++x)
            {
                const u32 sourceX = blockX * 4u + x < image.width ? blockX * 4u + x : image.width - 1u;
                const Float4& pixel = image.pixels[(z * image.height + sourceY) * image.width + sourceX];
                const f32 signedX = pixel.x < -1.0f ? -1.0f : (pixel.x > 1.0f ? 1.0f : pixel.x);
                const f32 signedY = pixel.y < -1.0f ? -1.0f : (pixel.y > 1.0f ? 1.0f : pixel.y);
                channelX[y * 4u + x] = static_cast<char>(signedX * 127.0f);
                channelY[y * 4u + x] = static_cast<char>(signedY * 127.0f);
            }
        }
    }

    [[nodiscard]] bool EncodeBlock(const Image& image, const tools::TextureCookingProfile& profile, const textures::FormatInfo& format, const u32 blockColumns,
                                   const u32 blockRows, const u32 workIndex, const bool encodeSrgb, const bc7enc_compress_block_params& bc7Parameters,
                                   void* bc6Options, EncodedImage& output) noexcept
    {
        const u32 blocksPerSlice = blockColumns * blockRows;
        const u32 z = workIndex / blocksPerSlice;
        const u32 sliceBlock = workIndex % blocksPerSlice;
        const u32 blockY = sliceBlock / blockColumns;
        const u32 blockX = sliceBlock % blockColumns;
        u8* destination =
            output.bytes.TypedData() + static_cast<usize>(z) * output.slicePitch + static_cast<usize>(blockY) * output.rowPitch + blockX * format.bytesPerBlock;
        if (profile.targetFormat == textures::PixelFormat::BC6HUFloat || profile.targetFormat == textures::PixelFormat::BC6HSFloat)
        {
            u16 sourceBlock[64]{};
            GatherHalfBlock(image, blockX, blockY, z, sourceBlock);
            return CompressBlockBC6(sourceBlock, 16, destination, bc6Options) == 0;
        }
        if (profile.targetFormat == textures::PixelFormat::BC4SNorm || profile.targetFormat == textures::PixelFormat::BC5SNorm)
        {
            char channelX[16]{};
            char channelY[16]{};
            GatherSignedBlock(image, blockX, blockY, z, channelX, channelY);
            return profile.targetFormat == textures::PixelFormat::BC4SNorm ? CompressBlockBC4S(channelX, 4, destination, nullptr) == 0
                                                                           : CompressBlockBC5S(channelX, 4, channelY, 4, destination, nullptr) == 0;
        }
        u8 sourceBlock[64]{};
        GatherBlock(image, blockX, blockY, z, encodeSrgb, sourceBlock);
        switch (profile.targetFormat)
        {
        case textures::PixelFormat::BC1UNorm:
            rgbcx::encode_bc1(profile.compressionQuality > rgbcx::MAX_LEVEL ? rgbcx::MAX_LEVEL : profile.compressionQuality, destination, sourceBlock, true,
                              false);
            return true;
        case textures::PixelFormat::BC2UNorm:
            return CompressBlockBC2(sourceBlock, 16, destination, nullptr) == 0;
        case textures::PixelFormat::BC3UNorm:
            rgbcx::encode_bc3(profile.compressionQuality > rgbcx::MAX_LEVEL ? rgbcx::MAX_LEVEL : profile.compressionQuality, destination, sourceBlock);
            return true;
        case textures::PixelFormat::BC4UNorm:
            rgbcx::encode_bc4(destination, sourceBlock, 4);
            return true;
        case textures::PixelFormat::BC5UNorm:
            rgbcx::encode_bc5(destination, sourceBlock, 0, 1, 4);
            return true;
        case textures::PixelFormat::BC7UNorm:
            bc7enc_compress_block(destination, sourceBlock, &bc7Parameters);
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool EncodeImage(const Image& image, const tools::TextureCookingProfile& profile, EncodedImage& output, const bool useJobs,
                                   const u32 maximumBatchSize, bool& jobFailure, u64& encodedBlockCount, bool& usedJobs) noexcept
    {
        const textures::FormatInfo format = textures::GetFormatInfo(profile.targetFormat);
        output.rowPitch = textures::CalculateMinimumRowPitch(profile.targetFormat, image.width);
        output.slicePitch = textures::CalculateMinimumSlicePitch(profile.targetFormat, image.width, image.height);
        const u64 byteCount = static_cast<u64>(output.slicePitch) * image.depth;
        if (format.bytesPerBlock == 0 || byteCount > 0xffffffffull)
        {
            return false;
        }
        output.bytes.Resize(static_cast<u32>(byteCount));
        if (output.bytes.Size() != byteCount)
        {
            return false;
        }
        const bool encodeSrgb = profile.targetColorSpace == textures::ColorSpace::SRgb;
        if (format.blockCompressed)
        {
            const u32 blockColumns = (image.width + 3u) / 4u;
            const u32 blockRows = (image.height + 3u) / 4u;
            const u32 workItemCount = blockColumns * blockRows * image.depth;
            encodedBlockCount += workItemCount;
            void* bc6Options = nullptr;
            if (profile.targetFormat == textures::PixelFormat::BC6HSFloat && (CreateOptionsBC6(&bc6Options) != 0 || SetSignedBC6(bc6Options, true) != 0))
            {
                if (bc6Options != nullptr)
                    DestroyOptionsBC6(bc6Options);
                return false;
            }
            bc7enc_compress_block_params bc7Parameters;
            bc7enc_compress_block_params_init(&bc7Parameters);
            bc7Parameters.m_uber_level = profile.compressionQuality > BC7ENC_MAX_UBER_LEVEL ? BC7ENC_MAX_UBER_LEVEL : profile.compressionQuality;
            const bool canUseJobs = useJobs && workItemCount > 1;
            if (canUseJobs)
            {
                concurrency::Atomic<u32> failures{0};
                jobs::Builder builder({jobs::Priority::CriticalPath, jobs::Affinity::AnyWorker});
                jobs::JobName name{"Vanguard.TextureTools.EncodeBlocks"};
                jobs::ParallelTask task = jobs::ParallelTask::Create(
                    [&image, &profile, &format, blockColumns, blockRows, encodeSrgb, &bc7Parameters, bc6Options, &output,
                     &failures](const u32 workIndex, const jobs::JobContext&) noexcept
                    {
                        if (!EncodeBlock(image, profile, format, blockColumns, blockRows, workIndex, encodeSrgb, bc7Parameters, bc6Options, output))
                        {
                            failures.SetValue(1);
                        }
                    });
                if (!builder.IsValid() || !task ||
                    !builder.DispatchParallel(name, workItemCount, static_cast<jobs::ParallelTask&&>(task), {}, maximumBatchSize))
                {
                    jobFailure = true;
                    if (bc6Options != nullptr)
                        DestroyOptionsBC6(bc6Options);
                    return false;
                }
                usedJobs = true;
                jobs::Counter counter = builder.ExtractCounter();
                if (!counter.IsValid() || !counter.Wait())
                {
                    jobFailure = true;
                    if (bc6Options != nullptr)
                        DestroyOptionsBC6(bc6Options);
                    return false;
                }
                if (failures.GetValue() != 0)
                {
                    if (bc6Options != nullptr)
                        DestroyOptionsBC6(bc6Options);
                    return false;
                }
            }
            else
            {
                for (u32 workIndex = 0; workIndex < workItemCount; ++workIndex)
                {
                    if (!EncodeBlock(image, profile, format, blockColumns, blockRows, workIndex, encodeSrgb, bc7Parameters, bc6Options, output))
                    {
                        if (bc6Options != nullptr)
                            DestroyOptionsBC6(bc6Options);
                        return false;
                    }
                }
            }
            if (bc6Options != nullptr)
                DestroyOptionsBC6(bc6Options);
            return true;
        }
        for (u32 z = 0; z < image.depth; ++z)
        {
            for (u32 y = 0; y < image.height; ++y)
            {
                u8* destination = output.bytes.TypedData() + static_cast<usize>(z) * output.slicePitch + static_cast<usize>(y) * output.rowPitch;
                for (u32 x = 0; x < image.width; ++x)
                {
                    Float4 pixel = image.pixels[(z * image.height + y) * image.width + x];
                    if (encodeSrgb)
                    {
                        pixel.x = LinearToSrgb(pixel.x);
                        pixel.y = LinearToSrgb(pixel.y);
                        pixel.z = LinearToSrgb(pixel.z);
                    }
                    switch (profile.targetFormat)
                    {
                    case textures::PixelFormat::R8UNorm:
                        destination[x] = ToByte(pixel.x);
                        break;
                    case textures::PixelFormat::R8G8UNorm:
                        destination[x * 2u + 0u] = ToByte(pixel.x);
                        destination[x * 2u + 1u] = ToByte(pixel.y);
                        break;
                    case textures::PixelFormat::R8G8B8A8UNorm:
                        destination[x * 4u + 0u] = ToByte(pixel.x);
                        destination[x * 4u + 1u] = ToByte(pixel.y);
                        destination[x * 4u + 2u] = ToByte(pixel.z);
                        destination[x * 4u + 3u] = ToByte(pixel.w);
                        break;
                    case textures::PixelFormat::R16G16B16A16Float:
                    {
                        auto* half = reinterpret_cast<u16*>(destination + x * 8u);
                        half[0] = FloatToHalf(pixel.x);
                        half[1] = FloatToHalf(pixel.y);
                        half[2] = FloatToHalf(pixel.z);
                        half[3] = FloatToHalf(pixel.w);
                        break;
                    }
                    default:
                        return false;
                    }
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool IsSupportedTarget(const textures::PixelFormat format) noexcept
    {
        return format == textures::PixelFormat::R8UNorm || format == textures::PixelFormat::R8G8UNorm || format == textures::PixelFormat::R8G8B8A8UNorm ||
               format == textures::PixelFormat::R16G16B16A16Float || format == textures::PixelFormat::BC1UNorm || format == textures::PixelFormat::BC3UNorm ||
               format == textures::PixelFormat::BC2UNorm || format == textures::PixelFormat::BC6HUFloat || format == textures::PixelFormat::BC6HSFloat ||
               format == textures::PixelFormat::BC4SNorm || format == textures::PixelFormat::BC5SNorm || format == textures::PixelFormat::BC4UNorm ||
               format == textures::PixelFormat::BC5UNorm || format == textures::PixelFormat::BC7UNorm;
    }

    [[nodiscard]] tools::ProfileRegistrationResult RegisterInternal(const tools::TextureCookingProfile& profile) noexcept
    {
        if (g_registrySealed)
            return tools::ProfileRegistrationResult::RegistrySealed;
        const u16 knownFlags = static_cast<u16>(tools::TextureCookingFlags::GenerateFullMipChain) | static_cast<u16>(tools::TextureCookingFlags::Streamable) |
                               static_cast<u16>(tools::TextureCookingFlags::RenormalizeNormals) |
                               static_cast<u16>(tools::TextureCookingFlags::PreserveAlphaCoverage);
        const textures::FormatInfo formatInfo = textures::GetFormatInfo(profile.targetFormat);
        if (profile.id == 0 || profile.version == 0 || !IsSupportedTarget(profile.targetFormat) || profile.mipTailCount == 0 ||
            profile.targetColorSpace > textures::ColorSpace::SRgb || (profile.targetColorSpace == textures::ColorSpace::SRgb && !formatInfo.supportsSRgb) ||
            (static_cast<u16>(profile.flags) & ~knownFlags) != 0 ||
            (tools::HasFlag(profile.flags, tools::TextureCookingFlags::RenormalizeNormals) && profile.targetColorSpace != textures::ColorSpace::Linear) ||
            profile.alphaCoverageChannel > 3 || !std::isfinite(profile.alphaCoverageThreshold) || profile.alphaCoverageThreshold < 0.0f ||
            profile.alphaCoverageThreshold > 1.0f ||
            profile.compressionQuality > 18)
        {
            return tools::ProfileRegistrationResult::InvalidArgument;
        }
        for (u32 index = 0; index < g_profileCount; ++index)
        {
            if (g_profiles[index].id == profile.id)
                return tools::ProfileRegistrationResult::DuplicateIdentifier;
        }
        if (g_profileCount == MaximumProfiles)
            return tools::ProfileRegistrationResult::CapacityExceeded;
        g_profiles[g_profileCount++] = profile;
        return tools::ProfileRegistrationResult::Success;
    }
} // namespace

namespace vanguard::texture_tools
{
    [[nodiscard]] bool RegisterBuiltInTextureImporters() noexcept;
    [[nodiscard]] bool FreezeTextureImporterRegistry(crypto::Digest256& fingerprint) noexcept;

    [[nodiscard]] bool FreezeCookingProfileRegistry(crypto::Digest256& fingerprint) noexcept
    {
        if (!g_initialized)
            return false;
        if (!g_registrySealed)
        {
            u32 order[MaximumProfiles]{};
            for (u32 index = 0; index < g_profileCount; ++index)
            {
                order[index] = index;
                for (u32 cursor = index; cursor != 0 && g_profiles[order[cursor]].id < g_profiles[order[cursor - 1u]].id; --cursor)
                {
                    const u32 temporary = order[cursor];
                    order[cursor] = order[cursor - 1u];
                    order[cursor - 1u] = temporary;
                }
            }
            constexpr char domain[] = "vanguard.texture-profile-registry.v1";
            crypto::Sha256Builder builder;
            if (!builder.Update(domain, sizeof(domain) - 1u) || !HashU32(builder, g_profileCount))
                return false;
            for (u32 position = 0; position < g_profileCount; ++position)
            {
                const TextureCookingProfile& profile = g_profiles[order[position]];
                u32 thresholdBits = 0;
                std::memcpy(&thresholdBits, &profile.alphaCoverageThreshold, sizeof(thresholdBits));
                if (!HashU64(builder, profile.id) || !HashU32(builder, profile.version) || !HashU8(builder, static_cast<u8>(profile.targetFormat)) ||
                    !HashU8(builder, static_cast<u8>(profile.targetColorSpace)) || !HashU16(builder, static_cast<u16>(profile.flags)) ||
                    !HashU8(builder, profile.mipTailCount) || !HashU8(builder, profile.alphaCoverageChannel) || !HashU32(builder, thresholdBits) ||
                    !HashU8(builder, profile.compressionQuality))
                    return false;
            }
            if (!builder.Finalize(g_profileRegistryFingerprint))
                return false;
            g_registrySealed = true;
        }
        fingerprint = g_profileRegistryFingerprint;
        return !fingerprint.IsEmpty();
    }

    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::UnknownCookingProfile:
            return "UnknownCookingProfile";
        case Result::UnsupportedSourceFormat:
            return "UnsupportedSourceFormat";
        case Result::UnsupportedTargetFormat:
            return "UnsupportedTargetFormat";
        case Result::InvalidSourceLayout:
            return "InvalidSourceLayout";
        case Result::InvalidSourceData:
            return "InvalidSourceData";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::OutOfMemory:
            return "OutOfMemory";
        case Result::Cancelled:
            return "Cancelled";
        case Result::CodecFailure:
            return "CodecFailure";
        case Result::TextureWriteFailure:
            return "TextureWriteFailure";
        }
        return "Unknown";
    }

    bool Initialize() noexcept
    {
        if (g_initialized)
            return true;
        rgbcx::init();
        bc7enc_compress_block_init();
        if (!RegisterBuiltInTextureImporters())
            return false;
        const TextureCookingFlags runtimeMips = TextureCookingFlags::GenerateFullMipChain | TextureCookingFlags::Streamable;
        if (RegisterInternal({profiles::Color, 2, textures::PixelFormat::BC7UNorm, textures::ColorSpace::SRgb, runtimeMips, 4, 3, 0.5f, 2}) !=
                ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::ColorAlpha, 2, textures::PixelFormat::BC7UNorm, textures::ColorSpace::SRgb,
                              runtimeMips | TextureCookingFlags::PreserveAlphaCoverage, 4, 3, 0.5f, 2}) != ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::Normal, 2, textures::PixelFormat::BC5UNorm, textures::ColorSpace::Linear,
                              runtimeMips | TextureCookingFlags::RenormalizeNormals, 4, 3, 0.5f, 10}) != ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::Masks, 2, textures::PixelFormat::BC7UNorm, textures::ColorSpace::Linear, runtimeMips, 4, 3, 0.5f, 1}) !=
                ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::Data, 2, textures::PixelFormat::BC4UNorm, textures::ColorSpace::Linear, runtimeMips, 4, 0, 0.5f, 10}) !=
                ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::Ui, 1, textures::PixelFormat::R8G8B8A8UNorm, textures::ColorSpace::SRgb, TextureCookingFlags::None, 1, 3, 0.5f, 0}) !=
                ProfileRegistrationResult::Success ||
            RegisterInternal({profiles::Hdr, 2, textures::PixelFormat::BC6HUFloat, textures::ColorSpace::Linear, runtimeMips, 4, 3, 0.5f, 0}) !=
                ProfileRegistrationResult::Success)
        {
            return false;
        }
        g_initialized = true;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_initialized;
    }

    bool FreezeConfiguration(TextureToolsConfigurationFingerprint& fingerprint) noexcept
    {
        TextureToolsConfigurationFingerprint frozen;
        if (!FreezeCookingProfileRegistry(frozen.profiles) || !FreezeTextureImporterRegistry(frozen.importers))
            return false;
        fingerprint = frozen;
        return true;
    }

    ProfileRegistrationResult RegisterCookingProfile(const TextureCookingProfile& profile) noexcept
    {
        if (!g_initialized)
            return ProfileRegistrationResult::InvalidArgument;
        return RegisterInternal(profile);
    }

    const TextureCookingProfile* FindCookingProfile(const TextureCookingProfileId id) noexcept
    {
        for (u32 index = 0; index < g_profileCount; ++index)
        {
            if (g_profiles[index].id == id)
                return &g_profiles[index];
        }
        return nullptr;
    }

    Result CookTexture(const SourceTexture& source, filesystem::IFile& output, const CookSettings& settings, CookReport* report) noexcept
    {
        if (!g_initialized)
            return Result::InvalidState;
        if (settings.cancellation.IsCancellationRequested())
            return Result::Cancelled;
        if (settings.execution > CookSettings::Execution::Jobs || settings.maximumDimension == 0 || settings.maximumSubresources == 0 ||
            settings.maximumOutputBytes == 0 || (settings.execution == CookSettings::Execution::Jobs && !jobs::IsInitialized()))
        {
            return Result::InvalidState;
        }
        const bool useJobs =
            settings.execution == CookSettings::Execution::Jobs || (settings.execution == CookSettings::Execution::Automatic && jobs::IsInitialized());
        if (!g_registrySealed)
            return Result::InvalidState;
        const TextureCookingProfile* profile = FindCookingProfile(settings.profile);
        if (profile == nullptr)
            return Result::UnknownCookingProfile;
        if (source.dimension > textures::TextureDimension::Cube || source.format > SourcePixelFormat::R32G32B32A32Float ||
            source.colorSpace > textures::ColorSpace::SRgb)
        {
            return Result::InvalidArgument;
        }
        const u32 faceCount = source.dimension == textures::TextureDimension::Cube ? 6u : 1u;
        const u32 expectedImages = source.dimension == textures::TextureDimension::Texture3D ? 1u : static_cast<u32>(source.arrayLayers) * faceCount;
        if (source.width == 0 || source.height == 0 || source.depth == 0 || source.arrayLayers == 0 || source.images.Size() != expectedImages ||
            source.width > settings.maximumDimension || source.height > settings.maximumDimension || source.depth > settings.maximumDimension ||
            (source.dimension == textures::TextureDimension::Texture1D && (source.height != 1 || source.depth != 1)) ||
            (source.dimension == textures::TextureDimension::Texture2D && source.depth != 1) ||
            (source.dimension == textures::TextureDimension::Texture3D && source.arrayLayers != 1) ||
            (source.dimension == textures::TextureDimension::Cube && (source.width != source.height || source.depth != 1)))
        {
            return Result::InvalidArgument;
        }
        const u8 mipCount = HasFlag(profile->flags, TextureCookingFlags::GenerateFullMipChain)
                                ? static_cast<u8>(textures::CalculateMipCount(source.width, source.height, source.depth))
                                : 1u;
        const u64 subresourceCount64 = static_cast<u64>(expectedImages) * mipCount;
        if (subresourceCount64 > settings.maximumSubresources || subresourceCount64 > 0xffffffffull)
            return Result::LimitExceeded;
        const u32 subresourceCount = static_cast<u32>(subresourceCount64);
        containers::DynamicArray<EncodedImage> encoded(memory::pools::Assets::GetInstance());
        containers::DynamicArray<textures::SubresourceBuildRecord> records(memory::pools::Assets::GetInstance());
        encoded.Resize(subresourceCount);
        records.Resize(subresourceCount);
        if (encoded.Size() != subresourceCount || records.Size() != subresourceCount)
            return Result::OutOfMemory;
        u64 sourceBytes = 0;
        u64 cookedBytes = 0;
        u64 encodedBlockCount = 0;
        u64 generatedMipTexelCount = 0;
        f32 maximumAlphaCoverageError = 0.0f;
        bool usedJobs = false;
        bool jobFailure = false;
        if (source.dimension == textures::TextureDimension::Cube)
        {
            for (u32 layer = 0; layer < source.arrayLayers; ++layer)
            {
                if (settings.cancellation.IsCancellationRequested())
                    return Result::Cancelled;
                Image current[6];
                for (u32 face = 0; face < 6; ++face)
                {
                    if (settings.cancellation.IsCancellationRequested())
                        return Result::Cancelled;
                    const u32 imageIndex = layer * 6u + face;
                    if (sourceBytes > ~0ull - source.images[imageIndex].byteSize)
                        return Result::LimitExceeded;
                    sourceBytes += source.images[imageIndex].byteSize;
                    const DecodeResult decodeResult =
                        DecodeSourceImage(source, source.images[imageIndex], source.colorSpace == textures::ColorSpace::SRgb, current[face]);
                    if (decodeResult == DecodeResult::InvalidLayout)
                        return Result::InvalidSourceLayout;
                    if (decodeResult == DecodeResult::InvalidData)
                        return Result::InvalidSourceData;
                    if (decodeResult == DecodeResult::OutOfMemory)
                        return Result::OutOfMemory;
                }
                const f32 baseCoverage = HasFlag(profile->flags, TextureCookingFlags::PreserveAlphaCoverage)
                                             ? CubeAlphaCoverage(current, profile->alphaCoverageChannel, profile->alphaCoverageThreshold)
                                             : 0.0f;
                for (u8 mip = 0; mip < mipCount; ++mip)
                {
                    if (settings.cancellation.IsCancellationRequested())
                        return Result::Cancelled;
                    if (mip != 0)
                    {
                        Image next[6];
                        if (!DownsampleCube(current, next, useJobs, settings.maximumBlocksPerJobBatch, jobFailure, usedJobs))
                            return jobFailure ? Result::InvalidState : Result::OutOfMemory;
                        for (u32 face = 0; face < 6; ++face)
                        {
                            current[face] = static_cast<Image&&>(next[face]);
                            generatedMipTexelCount += current[face].pixels.Size();
                        }
                    }
                    if (HasFlag(profile->flags, TextureCookingFlags::RenormalizeNormals))
                        for (u32 face = 0; face < 6; ++face)
                            NormalizeNormals(current[face]);
                    if (mip != 0 && HasFlag(profile->flags, TextureCookingFlags::PreserveAlphaCoverage))
                    {
                        const f32 coverageError = PreserveCubeCoverage(current, profile->alphaCoverageChannel, profile->alphaCoverageThreshold, baseCoverage);
                        if (coverageError > maximumAlphaCoverageError)
                            maximumAlphaCoverageError = coverageError;
                    }
                    for (u32 face = 0; face < 6; ++face)
                    {
                        const u32 imageIndex = layer * 6u + face;
                        const u32 recordIndex = imageIndex * mipCount + mip;
                        if (!EncodeImage(current[face], *profile, encoded[recordIndex], useJobs, settings.maximumBlocksPerJobBatch, jobFailure,
                                         encodedBlockCount, usedJobs))
                            return jobFailure ? Result::InvalidState : Result::CodecFailure;
                        cookedBytes += encoded[recordIndex].bytes.Size();
                        if (cookedBytes > settings.maximumOutputBytes)
                            return Result::LimitExceeded;
                        records[recordIndex] = {mip,
                                                static_cast<u16>(layer),
                                                static_cast<u8>(face),
                                                encoded[recordIndex].bytes.TypedData(),
                                                encoded[recordIndex].bytes.Size(),
                                                encoded[recordIndex].rowPitch,
                                                encoded[recordIndex].slicePitch};
                    }
                }
            }
        }
        else
            for (u32 imageIndex = 0; imageIndex < expectedImages; ++imageIndex)
            {
                if (settings.cancellation.IsCancellationRequested())
                    return Result::Cancelled;
                if (sourceBytes > ~0ull - source.images[imageIndex].byteSize)
                    return Result::LimitExceeded;
                sourceBytes += source.images[imageIndex].byteSize;
                Image current;
                const DecodeResult decodeResult =
                    DecodeSourceImage(source, source.images[imageIndex], source.colorSpace == textures::ColorSpace::SRgb, current);
                if (decodeResult == DecodeResult::InvalidLayout)
                    return Result::InvalidSourceLayout;
                if (decodeResult == DecodeResult::InvalidData)
                    return Result::InvalidSourceData;
                if (decodeResult == DecodeResult::OutOfMemory)
                    return Result::OutOfMemory;
                const f32 baseCoverage = HasFlag(profile->flags, TextureCookingFlags::PreserveAlphaCoverage)
                                             ? AlphaCoverage(current, profile->alphaCoverageChannel, profile->alphaCoverageThreshold)
                                             : 0.0f;
                for (u8 mip = 0; mip < mipCount; ++mip)
                {
                    if (settings.cancellation.IsCancellationRequested())
                        return Result::Cancelled;
                    if (mip != 0)
                    {
                        Image next;
                        if (!Downsample(current, next))
                            return Result::OutOfMemory;
                        current = static_cast<Image&&>(next);
                        generatedMipTexelCount += current.pixels.Size();
                    }
                    if (HasFlag(profile->flags, TextureCookingFlags::RenormalizeNormals))
                        NormalizeNormals(current);
                    if (mip != 0 && HasFlag(profile->flags, TextureCookingFlags::PreserveAlphaCoverage))
                    {
                        const f32 coverageError = PreserveCoverage(current, profile->alphaCoverageChannel, profile->alphaCoverageThreshold, baseCoverage);
                        if (coverageError > maximumAlphaCoverageError)
                            maximumAlphaCoverageError = coverageError;
                    }
                    const u32 recordIndex = imageIndex * mipCount + mip;
                    if (!EncodeImage(current, *profile, encoded[recordIndex], useJobs, settings.maximumBlocksPerJobBatch, jobFailure, encodedBlockCount,
                                     usedJobs))
                    {
                        return jobFailure ? Result::InvalidState : Result::CodecFailure;
                    }
                    cookedBytes += encoded[recordIndex].bytes.Size();
                    if (cookedBytes > settings.maximumOutputBytes)
                        return Result::LimitExceeded;
                    records[recordIndex] = {mip,
                                            source.dimension == textures::TextureDimension::Texture3D ? 0u : static_cast<u16>(imageIndex / faceCount),
                                            source.dimension == textures::TextureDimension::Cube ? static_cast<u8>(imageIndex % faceCount) : 0u,
                                            encoded[recordIndex].bytes.TypedData(),
                                            encoded[recordIndex].bytes.Size(),
                                            encoded[recordIndex].rowPitch,
                                            encoded[recordIndex].slicePitch};
                }
            }
        const u8 mipTailFirstLevel = HasFlag(profile->flags, TextureCookingFlags::Streamable) && mipCount > profile->mipTailCount
                                         ? static_cast<u8>(mipCount - profile->mipTailCount)
                                         : 0u;
        textures::TextureFlags textureFlags = textures::TextureFlags::DirectGpuUpload;
        if (HasFlag(profile->flags, TextureCookingFlags::Streamable))
            textureFlags = textureFlags | textures::TextureFlags::Streamable;
        textures::BuildDescription description{source.dimension,
                                               profile->targetFormat,
                                               profile->targetColorSpace,
                                               textureFlags,
                                               source.width,
                                               source.height,
                                               source.depth,
                                               source.arrayLayers,
                                               mipCount,
                                               mipTailFirstLevel,
                                               source.sourceFingerprint,
                                               {records.TypedData(), records.Size()}};
        if (settings.cancellation.IsCancellationRequested())
            return Result::Cancelled;
        const textures::Result writeResult = textures::WriteTexture(output, description);
        if (writeResult != textures::Result::Success)
        {
            VG_LOG_ERROR(diagnostics::Category::DataBuild, "vtex publication failed: %s", textures::ToString(writeResult));
            return Result::TextureWriteFailure;
        }
        if (report != nullptr)
        {
            *report = {profile->id,       profile->version,       profile->targetFormat,     mipCount, subresourceCount, sourceBytes, cookedBytes,
                       encodedBlockCount, generatedMipTexelCount, maximumAlphaCoverageError, usedJobs};
        }
        return Result::Success;
    }
} // namespace vanguard::texture_tools
