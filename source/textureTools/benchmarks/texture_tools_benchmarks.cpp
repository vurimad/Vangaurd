#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>

#include <chrono>
#include <cstdio>

namespace
{
    namespace containers = vanguard::containers;
    namespace textures = vanguard::textures;
    namespace tt = vanguard::texture_tools;
    using ByteArray = containers::DynamicArray<vanguard::u8>;

    constexpr vanguard::u32 Extent = 512;
    constexpr vanguard::u32 Iterations = 3;

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

    double Run(const tt::SourceTexture& source, const tt::CookSettings& settings, ByteArray& output)
    {
        const auto begin = std::chrono::steady_clock::now();
        for (vanguard::u32 iteration = 0; iteration < Iterations; ++iteration)
        {
            output.Clear();
            vanguard::filesystem::MemoryFileWriter writer(output);
            if (tt::CookTexture(source, writer, settings) != tt::Result::Success)
                return -1.0;
        }
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(end - begin).count();
    }
} // namespace

int main()
{
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;

    if (!memory::Initialize() || !diagnostics::Initialize(diagnostics::Mode::Synchronous, "textureToolsBenchmarks") || !containers::Initialize() ||
        !io::Initialize())
    {
        return 1;
    }
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    tt::TextureToolsConfigurationFingerprint frozenTextureTools;
    if (!filesystem::Initialize({root, root, root}) || !tt::Initialize() || !tt::FreezeConfiguration(frozenTextureTools))
        return 2;
    jobs::Config jobsConfig = jobs::ToolConfig();
    jobsConfig.maxWorkers = 0;
    if (!jobs::Initialize(jobsConfig))
        return 3;

    ByteArray pixels(memory::pools::Assets::GetInstance());
    pixels.Resize(Extent * Extent * 4u);
    for (vanguard::u32 y = 0; y < Extent; ++y)
    {
        for (vanguard::u32 x = 0; x < Extent; ++x)
        {
            const vanguard::u32 index = (y * Extent + x) * 4u;
            pixels[index + 0] = static_cast<vanguard::u8>((x * 17u + y * 3u) & 255u);
            pixels[index + 1] = static_cast<vanguard::u8>((x * 5u + y * 13u) & 255u);
            pixels[index + 2] = static_cast<vanguard::u8>(((x ^ y) * 11u) & 255u);
            pixels[index + 3] = static_cast<vanguard::u8>((x + y) & 255u);
        }
    }
    const tt::SourceImage image{pixels.TypedData(), pixels.Size(), Extent * 4u, Extent * Extent * 4u};
    const tt::SourceTexture source{textures::TextureDimension::Texture2D,
                                   tt::SourcePixelFormat::R8G8B8A8UNorm,
                                   textures::ColorSpace::SRgb,
                                   Extent,
                                   Extent,
                                   1,
                                   1,
                                   vanguard::crypto::Sha256(pixels.TypedData(), pixels.Size()),
                                   {&image, 1}};
    tt::CookSettings serial;
    serial.profile = tt::profiles::ColorAlpha;
    serial.execution = tt::CookSettings::Execution::Serial;
    tt::CookSettings parallel = serial;
    parallel.execution = tt::CookSettings::Execution::Jobs;
    parallel.maximumBlocksPerJobBatch = 256;
    ByteArray serialOutput(memory::pools::Assets::GetInstance());
    ByteArray parallelOutput(memory::pools::Assets::GetInstance());
    const double serialSeconds = Run(source, serial, serialOutput);
    const double parallelSeconds = Run(source, parallel, parallelOutput);
    if (serialSeconds <= 0.0 || parallelSeconds <= 0.0 || !Equal(serialOutput, parallelOutput))
        return 4;
    const double megaPixels = static_cast<double>(Extent) * Extent * Iterations / 1000000.0;
    std::printf("textureTools BC7 %ux%u x %u: serial %.2f MPix/s, Jobs %.2f MPix/s, speedup %.2fx, workers %u\n", Extent, Extent, Iterations,
                megaPixels / serialSeconds, megaPixels / parallelSeconds, serialSeconds / parallelSeconds, jobs::GetWorkerCount());

    if (!jobs::Shutdown())
        return 5;
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    return 0;
}
