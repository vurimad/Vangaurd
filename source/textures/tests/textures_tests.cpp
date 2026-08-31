#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/textures/texture_resource.hpp>
#include <vanguard/textures/textures.hpp>

#include <array>
#include <cstring>
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
                const vanguard::u32 slicePitch = textures::CalculateMinimumSlicePitch(textures::PixelFormat::BC1UNorm, extent, extent);
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

    struct BudgetFixture
    {
        std::array<std::array<vanguard::u8, 2048>, 4> bytes{};
        std::array<textures::SubresourceBuildRecord, 4> subresources{};
        textures::BuildDescription description;

        BudgetFixture()
        {
            vanguard::u32 record = 0;
            for (vanguard::u8 mip = 0; mip < 2; ++mip)
            {
                const vanguard::u32 extent = textures::CalculateMipExtent(64, mip);
                const vanguard::u32 rowPitch = textures::CalculateMinimumRowPitch(textures::PixelFormat::BC1UNorm, extent);
                const vanguard::u32 slicePitch = textures::CalculateMinimumSlicePitch(textures::PixelFormat::BC1UNorm, extent, extent);
                for (vanguard::u16 layer = 0; layer < 2; ++layer)
                {
                    for (vanguard::u32 byte = 0; byte < slicePitch; ++byte)
                        bytes[record][byte] = static_cast<vanguard::u8>(record * 37u + byte);
                    subresources[record] = {mip, layer, 0, bytes[record].data(), slicePitch, rowPitch, slicePitch};
                    ++record;
                }
            }
            description.dimension = textures::TextureDimension::Texture2D;
            description.format = textures::PixelFormat::BC1UNorm;
            description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
            description.width = description.height = 64;
            description.arrayLayers = 2;
            description.mipCount = 2;
            description.mipTailFirstLevel = 0;
            description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        }
    };

    textures::Result WriteFixture(const textures::BuildDescription& description, ByteArray& output)
    {
        output.Clear();
        vanguard::filesystem::MemoryFileWriter writer(output);
        return textures::WriteTexture(writer, description);
    }

    bool WriteFile(const vanguard::filesystem::AbsolutePath& path, const ByteArray& bytes)
    {
        auto writer = vanguard::filesystem::GetManager().CreateFileWriter(path, vanguard::filesystem::FOF_Buffered);
        if (!writer)
            return false;
        writer->Serialize(const_cast<vanguard::u8*>(bytes.TypedData()), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace packages = vanguard::packages;
    namespace resources = vanguard::resources;
    namespace streaming = vanguard::streaming;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "texturesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    Fixture fixture;
    Fixture reordered;
    std::swap(reordered.subresources[0], reordered.subresources[7]);
    std::swap(reordered.subresources[1], reordered.subresources[5]);
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == textures::Result::Success, "write vtex");
    Check(WriteFixture(reordered.description, second) == textures::Result::Success && EqualBytes(first, second), "canonical vtex emission is independent of cooker record order");

    filesystem::MemoryFileReader reader(first, 0);
    textures::TextureFile texture;
    const textures::Result openResult = texture.Open(reader);
    if (openResult != textures::Result::Success)
    {
        std::fprintf(stderr, "[texturesTests] vtex open result: %s\n", textures::ToString(openResult));
    }
    Check(openResult == textures::Result::Success, "open vtex metadata");
    Check(texture.IsOpen() && texture.GetDimension() == textures::TextureDimension::Texture2D && texture.Format() == textures::PixelFormat::BC1UNorm &&
              texture.GetSpace() == textures::ColorSpace::SRgb,
          "texture identity and format round trip");
    Check(texture.GetWidth() == 8 && texture.GetHeight() == 8 && texture.GetArrayLayers() == 2 && texture.GetMipCount() == 4 && texture.GetMipTailFirstLevel() == 2 &&
              texture.GetSubresources().Size() == 8,
          "texture shape and complete subresource table round trip");
    Check(texture.FindSubresource(2, 1) == 5 && texture.GetSubresources().Size() > 5 && textures::HasFlag(texture.GetSubresources()[5].flags, textures::SubresourceFlags::MipTail),
          "mip-major array indexing and resident tail contract");

    std::array<vanguard::u8, 64> loaded{};
    filesystem::MemoryFileReader subresourceReader(first, 0);
    Check(texture.ReadSubresource(subresourceReader, 0, loaded.data(), loaded.size()) == textures::Result::Success, "range-read and validate one GPU subresource");
    Check(texture.ReadSubresource(subresourceReader, 0, loaded.data(), 1) == textures::Result::BufferTooSmall, "caller-owned subresource capacity enforced");

    containers::DynamicArray<textures::StorageSegment> segments(memory::pools::Rendering::GetInstance());
    Check(textures::BuildStorageSegments(texture, first.Size(), segments) == textures::Result::Success && segments.Size() == 9 &&
              segments[0].flags == textures::StorageSegmentFlags::Metadata &&
              static_cast<vanguard::u8>(segments[6].flags) == static_cast<vanguard::u8>(textures::StorageSegmentFlags::Streamable | textures::StorageSegmentFlags::RequiredForMipTail),
          "VPAK-ready metadata, streamed mip, and resident-tail segments");

    {
        ByteArray corrupt(first);
        if (texture.GetSubresources().Size() != 0)
        {
            const vanguard::u64 byte = texture.GetTextureDataOffset() + texture.GetSubresources()[0].dataOffset;
            corrupt[static_cast<vanguard::u32>(byte)] ^= 1u;
        }
        filesystem::MemoryFileReader corruptMetadataReader(corrupt, 0);
        textures::TextureFile corruptTexture;
        Check(corruptTexture.Open(corruptMetadataReader) == textures::Result::Success, "opening vtex metadata does not force texture payload residency");
        filesystem::MemoryFileReader corruptPayloadReader(corrupt, 0);
        Check(corruptTexture.ReadSubresource(corruptPayloadReader, 0, loaded.data(), loaded.size()) == textures::Result::IntegrityFailure,
              "streamed subresource corruption rejected at residency boundary");
    }
    {
        Fixture invalid;
        invalid.description.format = textures::PixelFormat::BC5UNorm;
        Check(WriteFixture(invalid.description, second) == textures::Result::InvalidFormat, "sRGB rejected for a non-color GPU format");
    }
    {
        Fixture invalid;
        invalid.subresources[0].rowPitch = 1;
        Check(WriteFixture(invalid.description, second) == textures::Result::InvalidSubresource, "undersized GPU row pitch rejected");
    }
    {
        Fixture invalid;
        invalid.description.dimension = textures::TextureDimension::Cube;
        Check(WriteFixture(invalid.description, second) == textures::Result::MissingSubresource, "incomplete cube face set rejected");
    }
    {
        Fixture invalid;
        invalid.subresources[7] = invalid.subresources[6];
        Check(WriteFixture(invalid.description, second) == textures::Result::DuplicateSubresource, "duplicate subresource identity rejected");
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
        Check(cubeTexture.Open(cubeReader) == textures::Result::Success && cubeTexture.FindSubresource(0, 0, 5) == 5 && cubeTexture.GetSubresources()[5].face == 5,
              "cube face ordering round trip");
    }
    {
        std::array<vanguard::u8, 16> volumeBytes{};
        const std::array<textures::SubresourceBuildRecord, 1> volumeSubresources{{{0, 0, 0, volumeBytes.data(), volumeBytes.size(), 4, 8}}};
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
        Check(volumeTexture.Open(volumeReader) == textures::Result::Success && volumeTexture.GetSubresources()[0].depth == 2 && volumeTexture.GetSubresources()[0].slicePitch == 8,
              "3D mip stores complete depth slices");
    }

    {
        const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_texture_residency_tests");
        const filesystem::AbsolutePath loosePath = directory.AddFilePath("texture.vtex");
        const filesystem::AbsolutePath budgetPath = directory.AddFilePath("budget.vtex");
        const filesystem::AbsolutePath corruptPath = directory.AddFilePath("corrupt.vtex");
        const filesystem::AbsolutePath packagePath = directory.AddFilePath("texture.vpak");
        filesystem::Manager& files = filesystem::GetManager();
        static_cast<void>(files.DeleteFile(loosePath));
        static_cast<void>(files.DeleteFile(budgetPath));
        static_cast<void>(files.DeleteFile(corruptPath));
        static_cast<void>(files.DeleteFile(packagePath));
        static_cast<void>(files.DeletePath(directory));
        BudgetFixture budgetFixture;
        ByteArray budgetBytes(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(budgetFixture.description, budgetBytes) == textures::Result::Success, "write staging-budget VTEX fixture");
        Check(files.CreatePath(directory) && WriteFile(loosePath, first) && WriteFile(budgetPath, budgetBytes), "publish loose VTEX fixtures");

        containers::DynamicArray<textures::StorageSegment> textureSegments(memory::pools::Rendering::GetInstance());
        containers::DynamicArray<packages::BuildSegment> packageSegments(memory::pools::Rendering::GetInstance());
        Check(textures::BuildStorageSegments(texture, first.Size(), textureSegments) == textures::Result::Success, "build VTEX package segmentation");
        packageSegments.Resize(textureSegments.Size());
        for (vanguard::u32 index = 0; index < textureSegments.Size(); ++index)
        {
            const textures::StorageSegment& segment = textureSegments[index];
            packages::SegmentFlags flags = packages::SegmentFlags::Streamable;
            if ((static_cast<vanguard::u8>(segment.flags) & static_cast<vanguard::u8>(textures::StorageSegmentFlags::Metadata)) != 0)
                flags = packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident;
            else if ((static_cast<vanguard::u8>(segment.flags) & static_cast<vanguard::u8>(textures::StorageSegmentFlags::RequiredForMipTail)) != 0)
                flags = flags | packages::SegmentFlags::MemoryResident;
            packageSegments[index] = {first.TypedData() + segment.offset, static_cast<vanguard::usize>(segment.byteSize), packages::Codec::None, segment.alignmentLog2, flags};
        }
        packages::BuildResource buildResource;
        buildResource.path = "textures/test.vtex";
        buildResource.type = textures::TextureResourceType;
        buildResource.flags = packages::ResourceFlags::Streamable;
        buildResource.segments = {packageSegments.TypedData(), packageSegments.Size()};
        ByteArray packageBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter packageMemoryWriter(packageBytes);
        packages::PackageWriter packageWriter;
        Check(packageWriter.Begin(packageMemoryWriter) == packages::Result::Success && packageWriter.Add(buildResource) == packages::Result::Success &&
                  packageWriter.Finalize() == packages::Result::Success && WriteFile(packagePath, packageBytes),
              "publish segmented VTEX package");

        textures::TextureSubresourceSource looseSource;
        textures::TextureSubresourceReadRequest looseRead;
        Check(looseSource.OpenLoose(loosePath) == textures::Result::Success && looseSource.ReadSubresourceAsync(texture, 5, looseRead) == textures::Result::Success &&
                  looseRead.TryWait(10000) && looseRead.GetResult() == textures::Result::Success && looseRead.GetBytes().Count() == texture.GetSubresources()[5].byteSize &&
                  std::memcmp(looseRead.GetBytes().Data(), fixture.bytes[5].data(), static_cast<size_t>(texture.GetSubresources()[5].byteSize)) == 0,
              "loose VTEX exact subresource read is verified");

        textures::TextureSubresourceSource packageSource;
        textures::TextureSubresourceReadRequest packageRead;
        const packages::ResourceId packagedId = packages::HashResourcePath("textures/test.vtex");
        Check(packageSource.OpenPackage(packagePath, packagedId) == textures::Result::Success && packageSource.ReadSubresourceAsync(texture, 6, packageRead) == textures::Result::Success &&
                  packageRead.TryWait(10000) && packageRead.GetResult() == textures::Result::Success && packageRead.GetStats().decodedSegments == 1 &&
                  packageRead.GetBytes().Count() == texture.GetSubresources()[6].byteSize &&
                  std::memcmp(packageRead.GetBytes().Data(), fixture.bytes[6].data(), static_cast<size_t>(texture.GetSubresources()[6].byteSize)) == 0,
              "packaged VTEX decodes and verifies only the requested subresource segment");

        ByteArray corrupt(first);
        const vanguard::u64 corruptByte = texture.GetTextureDataOffset() + texture.GetSubresources()[5].dataOffset;
        corrupt[static_cast<vanguard::u32>(corruptByte)] ^= 1u;
        Check(WriteFile(corruptPath, corrupt), "publish corrupted VTEX fixture");
        textures::TextureSubresourceSource corruptSource;
        textures::TextureSubresourceReadRequest corruptRead;
        Check(corruptSource.OpenLoose(corruptPath) == textures::Result::Success && corruptSource.ReadSubresourceAsync(texture, 5, corruptRead) == textures::Result::Success &&
                  corruptRead.TryWait(10000) && corruptRead.GetResult() == textures::Result::IntegrityFailure,
              "asynchronous VTEX subresource digest mismatch is rejected");

        resources::ResourceRegistry registry;
        resources::ResourcePipeline pipeline;
        streaming::ResourceStreamer streamer;
        textures::TextureResourceLoader loader;
        Check(registry.Initialize() && pipeline.Initialize(registry), "texture pipeline initialization");
        streaming::Config config;
        config.stagingBudgetBytes = 4096;
        config.maximumResourceBytes = 1024 * 1024;
        Check(streamer.Initialize(pipeline, config) && loader.Initialize(streamer, pipeline), "texture metadata loader registration");
        const resources::ResourceReference reference(resources::ResourcePath::FromString("textures/test.vtex"), textures::TextureResourceType);

        auto mountedFile = filesystem::RawFileReader::Create(packagePath);
        packages::PackageReader mountedPackage;
        Check(mountedFile && mountedPackage.Open(*mountedFile) == packages::Result::Success, "open VTEX package for streamer mount");
        mountedFile.Reset();
        Check(streamer.MountPackage(mountedPackage, packagePath, 0), "mount segmented VTEX package");
        resources::PipelineRequest packagePipelineRequest = streamer.Request(reference, resources::LoadPriority::High);
        Check(packagePipelineRequest.TryWait(10000) && packagePipelineRequest.HasLoaded(), "packaged VTEX metadata loads through TextureResourceLoader");
        resources::ResourceHandle packageHandle = packagePipelineRequest.Acquire();
        const auto* const packageObject = static_cast<const textures::TextureResourceObject*>(packageHandle.Get());
        Check(packageObject != nullptr && packageObject->IsOpen() && packageObject->GetMetadata().GetMipCount() == 4, "packaged TextureResourceObject retains its package generation");
        packageHandle.Reset();
        packagePipelineRequest.Reset();
        for (vanguard::u32 attempt = 0; attempt < 10000 && (pipeline.GetStats().activeOperations != 0 || pipeline.GetStats().activeJobs != 0 || pipeline.GetStats().activePreparations != 0 ||
                                                            streamer.GetStats().activeLoads != 0 || streamer.GetStats().activeReads != 0);
             ++attempt)
            vanguard::concurrency::SleepOnCurrentThread(1);
        Check(streamer.UnmountPackage(mountedPackage), "unmount VTEX package after loader path test");

        Check(streamer.RegisterLoose({reference, loosePath, {}, 0, 0}), "register loose VTEX generation");
        resources::PipelineRequest pipelineRequest = streamer.Request(reference, resources::LoadPriority::High);
        Check(pipelineRequest.TryWait(10000) && pipelineRequest.HasLoaded(), "VTEX metadata loads through ResourcePipeline");
        resources::ResourceHandle handle = pipelineRequest.Acquire();
        const auto* const object = static_cast<const textures::TextureResourceObject*>(handle.Get());
        Check(object != nullptr && object->IsOpen() && object->GetMetadata().GetMipCount() == 4, "metadata-only texture object retains its pinned source generation");
        Check(streamer.UnregisterLoose(reference.GetPath()), "catalog removal leaves the loaded texture generation pinned");

        textures::TextureMipAcquisition acquisition;
        textures::TextureMipReadWindowLimits windowLimits;
        windowLimits.maximumAdmissionBytes = 12;
        windowLimits.maximumSubresources = 4;
        Check(acquisition.OpenMipTail(handle, windowLimits) == textures::Result::Success, "open complete mip-tail acquisition");
        vanguard::u32 windows = 0;
        vanguard::u32 acquiredSubresources = 0;
        while (acquisition.GetState() != textures::TextureMipAcquisitionState::Complete)
        {
            Check(acquisition.BeginNextWindow() == textures::Result::Success, "issue bounded mip read window");
            acquisition.Wait();
            const containers::ArraySpan<const textures::TextureSubresourceView> window = acquisition.GetWindow();
            Check(acquisition.GetState() == textures::TextureMipAcquisitionState::Ready && window.Count() != 0, "bounded window completes with immutable subresource views");
            acquiredSubresources += window.Count();
            ++windows;
            Check(acquisition.ReleaseWindow() == textures::Result::Success, "explicit window release permits forward progress");
        }
        Check(acquiredSubresources == 4 && windows == 4, "mip range larger than its staging window completes incrementally");

        const resources::ResourceReference budgetReference(resources::ResourcePath::FromString("textures/budget.vtex"), textures::TextureResourceType);
        Check(streamer.RegisterLoose({budgetReference, budgetPath, {}, 0, 0}), "register staging-budget VTEX generation");
        resources::PipelineRequest budgetPipelineRequest = streamer.Request(budgetReference, resources::LoadPriority::High);
        Check(budgetPipelineRequest.TryWait(10000) && budgetPipelineRequest.HasLoaded(), "staging-budget VTEX metadata loads");
        resources::ResourceHandle budgetHandle = budgetPipelineRequest.Acquire();
        textures::TextureMipAcquisition budgetAcquisition;
        textures::TextureMipReadWindowLimits broadLimits;
        broadLimits.maximumAdmissionBytes = 8192;
        broadLimits.maximumSubresources = 4;
        Check(budgetAcquisition.Open(budgetHandle, 0, 2, broadLimits) == textures::Result::Success, "open mip interval larger than the streamer's complete staging budget");
        vanguard::u32 budgetWindows = 0;
        vanguard::u32 budgetSubresources = 0;
        while (budgetAcquisition.GetState() != textures::TextureMipAcquisitionState::Complete)
        {
            Check(budgetAcquisition.BeginNextWindow() == textures::Result::Success, "issue globally budgeted mip window");
            budgetAcquisition.Wait();
            const auto budgetWindow = budgetAcquisition.GetWindow();
            Check(budgetAcquisition.GetState() == textures::TextureMipAcquisitionState::Ready && budgetWindow.Count() != 0,
                  "globally budgeted mip window completes without circular admission wait");
            budgetSubresources += budgetWindow.Count();
            ++budgetWindows;
            Check(budgetAcquisition.ReleaseWindow() == textures::Result::Success, "release globally budgeted mip window");
        }
        Check(budgetSubresources == 4 && budgetWindows == 2, "5120-byte mip interval is split against the real 4096-byte global staging budget");

        textures::TextureMipAcquisition oversized;
        textures::TextureMipReadWindowLimits undersizedLimits;
        undersizedLimits.maximumAdmissionBytes = 1;
        Check(oversized.OpenMipTail(handle, undersizedLimits) == textures::Result::Success && oversized.BeginNextWindow() == textures::Result::LimitExceeded &&
                  oversized.GetState() == textures::TextureMipAcquisitionState::Failed,
              "one subresource larger than the configured window fails without queue deadlock");

        textures::TextureMipAcquisition cancelled;
        Check(cancelled.OpenMipTail(handle) == textures::Result::Success && cancelled.Cancel() && cancelled.GetState() == textures::TextureMipAcquisitionState::Cancelled &&
                  cancelled.GetResult() == textures::Result::Cancelled,
              "mip acquisition can be cancelled between read windows");

        textures::TextureMipAcquisition cancelledReading;
        Check(cancelledReading.OpenMipTail(handle) == textures::Result::Success && cancelledReading.BeginNextWindow() == textures::Result::Success && cancelledReading.Cancel() &&
                  cancelledReading.GetState() == textures::TextureMipAcquisitionState::Cancelled,
              "mip acquisition cancellation reports success while a window is reading");
        cancelledReading.Reset();

        textures::TextureMipAcquisition cancelledReady;
        Check(cancelledReady.Open(handle, 3, 1) == textures::Result::Success && cancelledReady.BeginNextWindow() == textures::Result::Success,
              "issue mip window for ready-state cancellation");
        cancelledReady.Wait();
        Check(cancelledReady.GetState() == textures::TextureMipAcquisitionState::Ready && cancelledReady.Cancel() &&
                  cancelledReady.GetState() == textures::TextureMipAcquisitionState::Cancelled,
              "mip acquisition cancellation reports success for an acquired window");

        acquisition.Reset();
        oversized.Reset();
        cancelled.Reset();
        cancelledReading.Reset();
        cancelledReady.Reset();
        budgetAcquisition.Reset();
        budgetHandle.Reset();
        budgetPipelineRequest.Reset();
        handle.Reset();
        pipelineRequest.Reset();
        for (vanguard::u32 attempt = 0; attempt < 10000 && (pipeline.GetStats().activeOperations != 0 || pipeline.GetStats().activeJobs != 0 || pipeline.GetStats().activePreparations != 0 ||
                                                            streamer.GetStats().activeLoads != 0 || streamer.GetStats().activeReads != 0);
             ++attempt)
            vanguard::concurrency::SleepOnCurrentThread(1);
        Check(streamer.UnregisterLoose(budgetReference.GetPath()), "unregister staging-budget VTEX generation");
        Check(loader.Shutdown() && streamer.Shutdown() && pipeline.Shutdown() && registry.Shutdown(), "texture streaming pipeline shutdown without retained state");

        packageRead.Reset();
        looseRead.Reset();
        corruptRead.Reset();
        packageSource.Close();
        looseSource.Close();
        corruptSource.Close();
        mountedPackage.Close();
        static_cast<void>(files.DeleteFile(loosePath));
        static_cast<void>(files.DeleteFile(budgetPath));
        static_cast<void>(files.DeleteFile(corruptPath));
        static_cast<void>(files.DeleteFile(packagePath));
        static_cast<void>(files.DeletePath(directory));
    }

    filesystem::Shutdown();
    static_cast<void>(jobs::Shutdown());
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::fprintf(stdout, "texturesTests: all tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
