#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/streaming/resource_source.hpp>
#include <vanguard/streaming/streaming.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/pool.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vanguard;

    using ByteArray = containers::DynamicArray<u8>;

    constexpr resources::ResourceTypeId BlobType = vanguard::serialization::MakeFourCC('B', 'L', 'O', 'B');

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[streamingTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    constexpr auto MakeCompressiblePayload()
    {
        std::array<u8, 8192> bytes{};
        for (usize index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<u8>((index / 64u) & 15u);
        }
        return bytes;
    }

    auto MakeBudgetPayload()
    {
        std::array<u8, 128 * 1024> bytes{};
        for (usize index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<u8>(index * 31u);
        }
        return bytes;
    }

    constexpr auto g_compressible = MakeCompressiblePayload();
    const auto g_budgetPayload = MakeBudgetPayload();
    constexpr std::array<u8, 13> g_tail{0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x11, 0x22, 0x33, 0x44, 0x55};
    constexpr std::array<u8, 11> g_dependency{9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 42};
    constexpr std::array<u8, 17> g_looseOverride{0x56, 0x41, 0x4e, 0x47, 0x55, 0x41, 0x52, 0x44, 0x2d, 0x4c, 0x4f, 0x4f, 0x53, 0x45, 1, 2, 3};
    constexpr std::array<u8, 4096> g_corruptPayload{};

    [[nodiscard]] resources::ResourceReference MakeReference(const char* const path) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), BlobType);
    }

    void WaitForDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer);

    class BlobResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        BlobResource(const resources::ResourceId identity, const u64 contentCrc64, const usize byteCount, const u32 dependencyCount) noexcept
            : m_identity(identity), m_contentCrc64(contentCrc64), m_byteCount(byteCount), m_dependencyCount(dependencyCount)
        {
        }

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return BlobType;
        }

        resources::ResourceId m_identity;
        u64 m_contentCrc64;
        usize m_byteCount;
        u32 m_dependencyCount;
    };

    struct DecoderState
    {
        concurrency::Atomic<u32> decoded{0};
        concurrency::Atomic<u32> destroyed{0};
    };

    resources::ResourceObject* DecodeBlob(const resources::ResourceReference reference, const void* const data, const usize size, const resources::LoadContext& context,
                                          resources::Failure& failure, void* const userData) noexcept
    {
        auto& state = *static_cast<DecoderState*>(userData);
        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
        {
            if (!context.GetDependency(index))
            {
                failure = resources::Failure::DependencyFailure;
                return nullptr;
            }
        }
        static_cast<void>(state.decoded.Increment());
        return VANGUARD_NEW(BlobResource)(reference.GetPath().Id(), vanguard::serialization::Crc64(data, size), size, context.GetDependencyCount());
    }

    void DestroyBlob(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        auto& state = *static_cast<DecoderState*>(userData);
        static_cast<void>(state.destroyed.Increment());
        VANGUARD_DELETE(static_cast<BlobResource*>(resource));
    }

    bool SaveBytes(const filesystem::AbsolutePath& path, ByteArray& bytes)
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
        {
            return false;
        }
        if (!bytes.Empty())
        {
            writer->Serialize(bytes.TypedData(), bytes.Size());
        }
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }

    bool SaveBytes(const filesystem::AbsolutePath& path, const void* const data, const usize size)
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
        {
            return false;
        }
        if (size != 0)
        {
            writer->Serialize(const_cast<void*>(data), size);
        }
        writer->Flush();
        return writer->GetSize() == size;
    }

    void RunResourceSourceTest(const filesystem::AbsolutePath& packagePath, const filesystem::AbsolutePath& loosePath)
    {
        streaming::ResourceSource loose;
        Check(loose.OpenLoose(loosePath) == streaming::ResourceSourceResult::Success, "open generic loose resource source");
        Check(loose.GetLogicalSize() == g_looseOverride.size(), "generic loose source exposes logical size");
        streaming::ResourceSourceReader looseReader;
        Check(looseReader.Open(loose) == streaming::ResourceSourceResult::Success, "open generic loose source reader");
        std::array<u8, 5> looseRange{};
        looseReader.Seek(4);
        looseReader.Serialize(looseRange.data(), looseRange.size());
        Check(std::memcmp(looseRange.data(), g_looseOverride.data() + 4, looseRange.size()) == 0, "generic loose reader reads exact range");
        Check(looseReader.GetStats().storedBytesRead == looseRange.size(), "generic loose reader accounts physical bytes");

        streaming::ResourceReadPlan loosePlan;
        Check(loose.PlanRead(3, 7, loosePlan) == streaming::ResourceSourceResult::Success && loosePlan.logicalBytes == 7 && loosePlan.storedBytes == 7 && loosePlan.decodedBytes == 7 &&
                  loosePlan.touchedSegments == 1,
              "generic loose source plans exact asynchronous range cost");
        std::array<u8, 7> asyncLooseRange{};
        streaming::ResourceReadRequest looseRequest;
        Check(loose.ReadAsync({3, asyncLooseRange.size(), asyncLooseRange.data(), asyncLooseRange.size()}, looseRequest) == streaming::ResourceSourceResult::Success,
              "submit generic asynchronous loose range");
        loose.Close();
        Check(looseRequest.TryWait(10000) && looseRequest.GetResult() == streaming::ResourceSourceResult::Success &&
                  std::memcmp(asyncLooseRange.data(), g_looseOverride.data() + 3, asyncLooseRange.size()) == 0,
              "asynchronous loose range pins source generation through completion");
        Check(looseRequest.GetStats().storedBytesRead == asyncLooseRange.size() && looseRequest.GetStats().decodedBytesProduced == asyncLooseRange.size(),
              "asynchronous loose range reports stored and produced bytes");

        streaming::ResourceSource package;
        const resources::ResourceId root = packages::HashResourcePath("stream/root.vblob");
        Check(package.OpenPackage(packagePath, root, BlobType) == streaming::ResourceSourceResult::Success, "open generic packaged resource source");
        Check(package.GetLogicalSize() == g_compressible.size() + g_tail.size(), "generic packaged source exposes logical size");

        streaming::ResourceSourceReader first;
        streaming::ResourceSourceReader second;
        Check(first.Open(package) == streaming::ResourceSourceResult::Success && second.Open(package) == streaming::ResourceSourceResult::Success,
              "generic packaged source permits independent readers");
        std::array<u8, 32> firstRange{};
        std::array<u8, 13> secondRange{};
        first.Seek(96);
        first.Serialize(firstRange.data(), firstRange.size());
        second.Seek(static_cast<i64>(g_compressible.size()));
        second.Serialize(secondRange.data(), secondRange.size());
        Check(std::memcmp(firstRange.data(), g_compressible.data() + 96, firstRange.size()) == 0, "first packaged reader decodes selected range");
        Check(std::memcmp(secondRange.data(), g_tail.data(), secondRange.size()) == 0, "second packaged reader reads another segment independently");
        Check(first.GetStats().decodedSegments == 1 && second.GetStats().decodedSegments == 1, "packaged readers account their own decoded segments");

        streaming::ResourceReadPlan packagePlan;
        constexpr u64 crossingOffset = g_compressible.size() - 4u;
        Check(package.PlanRead(crossingOffset, 8, packagePlan) == streaming::ResourceSourceResult::Success && packagePlan.logicalBytes == 8 && packagePlan.touchedSegments == 2 &&
                  packagePlan.decodedBytes == g_compressible.size() + g_tail.size(),
              "packaged range plan accounts complete authenticated segments");
        std::array<u8, 8> asyncPackageRange{};
        streaming::ResourceReadRequest packageRequest;
        Check(package.ReadAsync({crossingOffset, asyncPackageRange.size(), asyncPackageRange.data(), asyncPackageRange.size()}, packageRequest) == streaming::ResourceSourceResult::Success,
              "submit generic asynchronous packaged range");
        package.Close();
        std::array<u8, 8> expectedCrossing{};
        std::memcpy(expectedCrossing.data(), g_compressible.data() + crossingOffset, 4);
        std::memcpy(expectedCrossing.data() + 4, g_tail.data(), 4);
        Check(packageRequest.TryWait(10000) && packageRequest.GetResult() == streaming::ResourceSourceResult::Success &&
                  std::memcmp(asyncPackageRange.data(), expectedCrossing.data(), expectedCrossing.size()) == 0,
              "asynchronous packaged range decodes exact logical bytes after source owner closes");
        Check(packageRequest.GetStats().storedBytesRead == packagePlan.storedBytes && packageRequest.GetStats().decodedSegments == 2,
              "asynchronous packaged range reports physical and decode work");
    }

    void RunRangeQueueTest(streaming::ResourceStreamer& streamer)
    {
        Check(streaming::ClassifyFailure(streaming::ResourceSourceResult::IoFailure) == streaming::ResourceFailureClass::Transient &&
                  streaming::ClassifyFailure(streaming::ResourceSourceResult::Cancelled) == streaming::ResourceFailureClass::Cancelled &&
                  streaming::ClassifyFailure(streaming::ResourceSourceResult::IntegrityFailure) == streaming::ResourceFailureClass::Permanent,
              "streaming failures distinguish retryable I/O from terminal failures");
        streaming::ResourceSource source;
        containers::DynamicArray<streaming::DependencyDescriptor> dependencies(memory::pools::Streaming::GetInstance());
        Check(streamer.OpenSource(MakeReference("stream/root.vblob"), source, dependencies) == resources::Failure::None,
              "open governed source for queued range reads");
        streaming::ResourceRangeReadQueue queue;
        Check(queue.Open(source) == streaming::ResourceSourceResult::Success, "open generic coalesced range queue");

        std::array<streaming::CoalescedResourceReadRequest, 7> reads;
        for (u32 index = 0; index < 6; ++index)
            Check(queue.Read(index * 512u, 4096u, reads[index]) == streaming::ResourceSourceResult::Success, "queue bounded range read");
        Check(queue.Read(5u * 512u, 4096u, reads[6]) == streaming::ResourceSourceResult::Success && reads[5].IsSameOperation(reads[6]),
              "equal generation ranges coalesce into one operation");

        for (u32 index = 0; index < 5; ++index)
            Check(reads[index].TryWait(10000) && reads[index].GetResult() == streaming::ResourceSourceResult::Success, "admitted range finishes");
        Check(!reads[5].HasFinished(), "over-budget range waits in FIFO admission queue");
        reads[0].Reset();
        Check(reads[5].TryWait(10000) && reads[5].GetResult() == streaming::ResourceSourceResult::Success,
              "queued range starts after earlier shared bytes are released");
        for (auto& read : reads)
            read.Reset();
    }

    bool BuildBasePackage(ByteArray& output)
    {
        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x53545245414d0001ull;
        options.buildId = 0x20260729;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }

        const std::array<packages::BuildSegment, 2> rootSegments{{{g_compressible.data(), g_compressible.size(), packages::Codec::Lz4, 12, packages::SegmentFlags::Streamable},
                                                                  {g_tail.data(), g_tail.size(), packages::Codec::None, 12, packages::SegmentFlags::Inline}}};
        const std::array<packages::BuildSegment, 1> dependencySegments{{{g_dependency.data(), g_dependency.size(), packages::Codec::None, 12, packages::SegmentFlags::MemoryResident}}};
        const resources::ResourceId dependencyId = packages::HashResourcePath("stream/dependency.vblob");
        const std::array<packages::Dependency, 1> dependencies{{{dependencyId, BlobType, resources::DependencyKind::Required}}};

        packages::BuildResource root;
        root.path = "stream/root.vblob";
        root.type = BlobType;
        root.flags = packages::ResourceFlags::Streamable;
        root.segments = {rootSegments.data(), static_cast<u32>(rootSegments.size())};
        root.dependencies = {dependencies.data(), static_cast<u32>(dependencies.size())};

        packages::BuildResource dependency;
        dependency.path = "stream/dependency.vblob";
        dependency.type = BlobType;
        dependency.flags = packages::ResourceFlags::Startup;
        dependency.segments = {dependencySegments.data(), static_cast<u32>(dependencySegments.size())};

        std::array<packages::BuildResource*, 2> ordered{&root, &dependency};
        std::sort(ordered.begin(), ordered.end(), [](const packages::BuildResource* const left, const packages::BuildResource* const right)
                  { return packages::HashResourcePath(left->path) < packages::HashResourcePath(right->path); });
        return writer.Add(*ordered[0]) == packages::Result::Success && writer.Add(*ordered[1]) == packages::Result::Success && writer.Finalize() == packages::Result::Success;
    }

    bool BuildCorruptPackage(ByteArray& output, packages::PackageReader& metadata)
    {
        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = 0x53545245414d0002ull;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }
        const std::array<packages::BuildSegment, 1> segments{{{g_corruptPayload.data(), g_corruptPayload.size(), packages::Codec::None, 12, packages::SegmentFlags::Streamable}}};
        packages::BuildResource resource;
        resource.path = "stream/corrupt.vblob";
        resource.type = BlobType;
        resource.segments = {segments.data(), static_cast<u32>(segments.size())};
        if (writer.Add(resource) != packages::Result::Success || writer.Finalize() != packages::Result::Success)
        {
            return false;
        }

        filesystem::MemoryFileReader reader(output, 0);
        if (metadata.Open(reader) != packages::Result::Success)
        {
            return false;
        }
        const packages::Resource* const stored = metadata.Find("stream/corrupt.vblob");
        if (stored == nullptr)
        {
            return false;
        }
        const auto storedSegments = metadata.GetSegments(*stored);
        if (storedSegments.Count() != 1)
        {
            return false;
        }
        output[static_cast<u32>(storedSegments[0].offset)] ^= 0x5au;
        return true;
    }

    bool BuildSinglePackage(ByteArray& output, const u64 packageId, const u64 buildId, const char* const resourcePath, const void* const payload, const usize payloadSize)
    {
        output.Clear();
        filesystem::MemoryFileWriter file(output);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = packageId;
        options.buildId = buildId;
        if (writer.Begin(file, options) != packages::Result::Success)
        {
            return false;
        }
        const packages::BuildSegment segment{payload, payloadSize, packages::Codec::None, 12, packages::SegmentFlags::Streamable};
        packages::BuildResource resource;
        resource.path = resourcePath;
        resource.type = BlobType;
        resource.flags = packages::ResourceFlags::Streamable;
        resource.segments = {&segment, 1};
        return writer.Add(resource) == packages::Result::Success && writer.Finalize() == packages::Result::Success;
    }

    bool MakeCatalogEntry(ByteArray& bytes, const u32 packageNumber, const packages::PackageSetEntryFlags flags, const i32 priority, packages::PackageSetEntry& entry)
    {
        filesystem::MemoryFileReader file(bytes, 0);
        packages::PackageReader reader;
        if (reader.Open(file) != packages::Result::Success)
        {
            return false;
        }
        entry.packageNumber = packageNumber;
        entry.flags = flags;
        entry.mountPriority = priority;
        entry.packageId = reader.GetHeader().packageId;
        entry.buildId = reader.GetHeader().buildId;
        entry.fileSize = reader.GetHeader().fileSize;
        entry.indexCrc64 = reader.GetHeader().indexCrc64;
        const crypto::Digest256 digest = crypto::Sha256(bytes.Data(), bytes.Size());
        for (u32 byte = 0; byte < crypto::Digest256::ByteCount; ++byte)
        {
            entry.contentDigest[byte] = digest.bytes[byte];
        }
        reader.Close();
        return true;
    }

    bool BuildRuntimePackageSet(ByteArray& rootBytes, ByteArray& bulkBytes, ByteArray& optionalBytes)
    {
        constexpr u64 RootPackageId = 0x4441544130303001ull;
        constexpr u64 PackageSetBuildId = 0x202608040001ull;
        if (!BuildSinglePackage(bulkBytes, 0x4441544130303101ull, 0x202608040101ull, "stream/set.vblob", g_dependency.data(), g_dependency.size()) ||
            !BuildSinglePackage(optionalBytes, 0x4441544130303201ull, 0x202608040201ull, "stream/optional.vblob", g_tail.data(), g_tail.size()))
        {
            return false;
        }

        std::array<packages::PackageSetEntry, 2> entries{};
        if (!MakeCatalogEntry(bulkBytes, 1, packages::PackageSetEntryFlags::Required | packages::PackageSetEntryFlags::Override, 10, entries[0]) ||
            !MakeCatalogEntry(optionalBytes, 2, packages::PackageSetEntryFlags::Optional, 20, entries[1]))
        {
            return false;
        }

        rootBytes.Clear();
        filesystem::MemoryFileWriter rootFile(rootBytes);
        packages::PackageWriter writer;
        packages::BuildOptions options;
        options.packageId = RootPackageId;
        options.buildId = PackageSetBuildId;
        packages::PackageSetBuild packageSet;
        packageSet.gameId = 0x56414e4755415244ull;
        packageSet.targetPlatformId = vanguard::serialization::MakeFourCC('W', 'D', '1', '2');
        packageSet.startupWorld = packages::HashResourcePath("world/startup.vworld");
        packageSet.startupWorldType = BlobType;
        packageSet.defaultInput = packages::HashResourcePath("input/default.vinput");
        packageSet.defaultInputType = BlobType;
        packageSet.packages = {entries.data(), static_cast<u32>(entries.size())};
        if (writer.Begin(rootFile, options, packageSet) != packages::Result::Success)
        {
            return false;
        }
        const packages::BuildSegment startupSegment{g_tail.data(), g_tail.size(), packages::Codec::None, 12, packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment shadowedSegment{g_tail.data(), g_tail.size(), packages::Codec::None, 12, packages::SegmentFlags::Streamable};
        packages::BuildResource startup;
        startup.path = "world/startup.vworld";
        startup.type = BlobType;
        startup.flags = packages::ResourceFlags::Startup;
        startup.segments = {&startupSegment, 1};
        packages::BuildResource shadowed;
        shadowed.path = "stream/set.vblob";
        shadowed.type = BlobType;
        shadowed.flags = packages::ResourceFlags::Streamable;
        shadowed.segments = {&shadowedSegment, 1};
        std::array<packages::BuildResource*, 2> ordered{&startup, &shadowed};
        std::sort(ordered.begin(), ordered.end(), [](const packages::BuildResource* const left, const packages::BuildResource* const right)
                  { return packages::HashResourcePath(left->path) < packages::HashResourcePath(right->path); });
        return writer.Add(*ordered[0]) == packages::Result::Success && writer.Add(*ordered[1]) == packages::Result::Success && writer.Finalize() == packages::Result::Success;
    }

    void RunPackageSetMountTest(const filesystem::AbsolutePath& directory, filesystem::Manager& files, resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer)
    {
        const filesystem::AbsolutePath data000 = directory.AddFilePath("DATA000.vpak");
        const filesystem::AbsolutePath data001 = directory.AddFilePath("DATA001.vpak");
        const filesystem::AbsolutePath data002 = directory.AddFilePath("DATA002.vpak");
        static_cast<void>(files.DeleteFile(data000));
        static_cast<void>(files.DeleteFile(data001));
        static_cast<void>(files.DeleteFile(data002));

        ByteArray rootBytes(memory::pools::Resources::GetInstance());
        ByteArray bulkBytes(memory::pools::Resources::GetInstance());
        ByteArray optionalBytes(memory::pools::Resources::GetInstance());
        Check(BuildRuntimePackageSet(rootBytes, bulkBytes, optionalBytes), "build DATA000 package-set fixture");
        Check(SaveBytes(data000, rootBytes) && SaveBytes(data001, bulkBytes), "publish required package-set fixture files");

        const u32 baselineMounts = streamer.GetStats().mountedPackages;
        streaming::PackageSetMountConfig config;
        config.expectedGameId = 0x56414e4755415244ull;
        config.expectedBuildId = 0x202608040001ull;
        config.expectedTargetPlatformId = vanguard::serialization::MakeFourCC('W', 'D', '1', '2');
        streaming::PackageSetMount mount;
        Check(mount.Mount(streamer, directory, config) == streaming::PackageSetMountResult::Success && mount.IsMounted() && mount.GetGameId() == config.expectedGameId &&
                  mount.BuildId() == config.expectedBuildId && mount.GetTargetPlatformId() == config.expectedTargetPlatformId && mount.PackageCount() == 2 &&
                  mount.FindPackage(0) != nullptr && mount.FindPackage(1) != nullptr && mount.FindPackage(2) == nullptr && mount.StartupWorld() == MakeReference("world/startup.vworld") &&
                  mount.GetDefaultInput() == MakeReference("input/default.vinput") && streamer.GetStats().mountedPackages == baselineMounts + 2,
              "DATA000 mounts root and required catalog packages while tolerating absent optional data");

        resources::PipelineRequest request = streamer.Request(MakeReference("stream/set.vblob"), resources::LoadPriority::High);
        Check(request.TryWait(10000) && request.HasLoaded(), "package-set resource resolves through ResourceStreamer");
        resources::ResourceHandle handle = request.Acquire();
        const auto* const resource = static_cast<const BlobResource*>(handle.Get());
        Check(resource != nullptr && resource->m_contentCrc64 == vanguard::serialization::Crc64(g_dependency.data(), g_dependency.size()),
              "higher-priority override package reaches the registered decoder");
        handle.Reset();
        request.Reset();
        WaitForDrain(pipeline, streamer);
        Check(mount.Unmount() == streaming::PackageSetMountResult::Success && !mount.IsMounted() && streamer.GetStats().mountedPackages == baselineMounts,
              "package-set unmount removes the complete set atomically");

        Check(SaveBytes(data002, optionalBytes), "install optional package fixture");
        streaming::PackageSetMountConfig requiredOnly = config;
        requiredOnly.mountOptionalPackages = false;
        Check(mount.Mount(streamer, directory, requiredOnly) == streaming::PackageSetMountResult::Success && mount.PackageCount() == 2 && mount.FindPackage(2) == nullptr,
              "optional package mounting can be disabled explicitly");
        Check(mount.Unmount() == streaming::PackageSetMountResult::Success, "required-only package set unmount");
        Check(mount.Mount(streamer, directory, config) == streaming::PackageSetMountResult::Success && mount.PackageCount() == 3 && mount.FindPackage(2) != nullptr,
              "installed optional package joins the same atomic mount transaction");
        Check(mount.Unmount() == streaming::PackageSetMountResult::Success, "optional package set unmount");
        static_cast<void>(files.DeleteFile(data002));

        streaming::PackageSetMountConfig wrongGame = config;
        wrongGame.expectedGameId ^= 1u;
        Check(mount.Mount(streamer, directory, wrongGame) == streaming::PackageSetMountResult::GameMismatch && streamer.GetStats().mountedPackages == baselineMounts,
              "game identity mismatch leaves no partial mounts");

        static_cast<void>(files.DeleteFile(data001));
        Check(mount.Mount(streamer, directory, config) == streaming::PackageSetMountResult::MissingRequiredPackage && streamer.GetStats().mountedPackages == baselineMounts,
              "missing required package leaves no partial mounts");
        Check(SaveBytes(data001, bulkBytes), "restore required package");

        ByteArray oversizedBulk(bulkBytes);
        oversizedBulk.PushBack(0xff);
        Check(SaveBytes(data001, oversizedBulk), "write size-mismatched required package");
        Check(mount.Mount(streamer, directory, config) == streaming::PackageSetMountResult::PackageMetadataMismatch && streamer.GetStats().mountedPackages == baselineMounts,
              "catalog size mismatch leaves no partial mounts");

        ByteArray digestMismatch(bulkBytes);
        filesystem::MemoryFileReader bulkFile(digestMismatch, 0);
        packages::PackageReader bulkReader;
        Check(bulkReader.Open(bulkFile) == packages::Result::Success, "open digest-corruption fixture metadata");
        const packages::Resource* const stored = bulkReader.Find("stream/set.vblob");
        if (stored != nullptr)
        {
            const auto segments = bulkReader.GetSegments(*stored);
            if (!segments.Empty())
            {
                digestMismatch[static_cast<u32>(segments[0].offset)] ^= 0x5a;
            }
        }
        bulkReader.Close();
        Check(SaveBytes(data001, digestMismatch), "write digest-mismatched required package");
        streaming::PackageSetMountConfig wholeFile = config;
        wholeFile.verification = packages::CatalogVerification::WholeFileDigest;
        Check(mount.Mount(streamer, directory, wholeFile) == streaming::PackageSetMountResult::PackageDigestMismatch && streamer.GetStats().mountedPackages == baselineMounts,
              "whole-file verification rejects altered payload before mounting");

        static_cast<void>(files.DeleteFile(data000));
        Check(mount.Mount(streamer, directory, config) == streaming::PackageSetMountResult::MissingRootPackage && streamer.GetStats().mountedPackages == baselineMounts,
              "missing DATA000 is reported without directory discovery or partial mounts");
        static_cast<void>(files.DeleteFile(data001));
        static_cast<void>(files.DeleteFile(data002));
    }

    constexpr resources::ResourceTypeId ShaderType = vanguard::serialization::MakeFourCC('V', 'S', 'H', 'D');
    constexpr resources::ResourceTypeId MaterialType = vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T');
    constexpr resources::ResourceTypeId TextureType = vanguard::serialization::MakeFourCC('V', 'T', 'E', 'X');

    struct ShaderParameter
    {
        const char* name;
        reflection::SchemaTypeId type;
        reflection::ValueKind kind;
        u32 size;
        u32 alignment;
        reflection::FieldFlags flags;
    };

    [[nodiscard]] constexpr u32 AlignValue(const u32 value, const u32 alignment) noexcept
    {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    struct ShaderMaterialLayout
    {
        explicit ShaderMaterialLayout() noexcept : fields(memory::pools::Resources::GetInstance()) {}

        [[nodiscard]] bool Build(const char* const interfaceName, const ShaderParameter* const parameters, const u32 parameterCount) noexcept
        {
            fields.Resize(parameterCount + 1u);
            if (fields.Size() != parameterCount + 1u)
            {
                return false;
            }

            u32 offset = 0;
            fields[0] = reflection::MakeField("shader", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference, offset, sizeof(resources::ResourceReference),
                                              alignof(resources::ResourceReference), 1, 0, reflection::FieldFlags::Required);
            offset += sizeof(resources::ResourceReference);
            u32 objectAlignment = alignof(resources::ResourceReference);

            for (u32 index = 0; index < parameterCount; ++index)
            {
                const ShaderParameter& parameter = parameters[index];
                if (parameter.name == nullptr || parameter.name[0] == '\0' || parameter.size == 0 || parameter.alignment == 0)
                {
                    return false;
                }
                offset = AlignValue(offset, parameter.alignment);
                fields[index + 1u] = reflection::MakeField(parameter.name, parameter.type, parameter.kind, offset, parameter.size, parameter.alignment, 1, 0, parameter.flags);
                offset += parameter.size;
                if (parameter.alignment > objectAlignment)
                {
                    objectAlignment = parameter.alignment;
                }
            }

            schema = {reflection::HashSchemaName(interfaceName), interfaceName, AlignValue(offset, objectAlignment), objectAlignment, 1, 1, fields.TypedData(), fields.Size()};
            return schema.id != reflection::InvalidSchemaTypeId;
        }

        [[nodiscard]] const reflection::SchemaField* Find(const char* const name) const noexcept
        {
            const reflection::SchemaFieldId id = reflection::HashFieldName(name);
            for (const reflection::SchemaField& field : fields)
            {
                if (field.id == id)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        containers::DynamicArray<reflection::SchemaField> fields;
        reflection::Schema schema;
    };

    class ShaderResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        explicit ShaderResource(const ShaderMaterialLayout& layout) noexcept : m_layout(&layout) {}

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return ShaderType;
        }

        const ShaderMaterialLayout* m_layout;
    };

    class TextureResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return TextureType;
        }
    };

    class DynamicMaterialResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        explicit DynamicMaterialResource(const reflection::Schema& schema) noexcept
            : m_schema(&schema), m_storage(memory::Allocate(memory::PoolId::Resources, schema.size, schema.alignment)), m_dependencies(memory::pools::Resources::GetInstance())
        {
            auto* const bytes = static_cast<u8*>(m_storage.address);
            for (usize index = 0; index < m_storage.size; ++index)
            {
                bytes[index] = 0;
            }
        }

        ~DynamicMaterialResource() override
        {
            memory::Free(m_storage);
        }

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return MaterialType;
        }

        const reflection::Schema* m_schema;
        memory::MemoryBlock m_storage;
        containers::DynamicArray<resources::ResourceHandle> m_dependencies;
    };

    resources::ResourceObject* DecodeShader(resources::ResourceReference, const void*, usize, const resources::LoadContext&, resources::Failure& failure, void* const userData) noexcept
    {
        failure = resources::Failure::None;
        return VANGUARD_NEW(ShaderResource)(*static_cast<ShaderMaterialLayout*>(userData));
    }

    resources::ResourceObject* DecodeTexture(resources::ResourceReference, const void*, usize, const resources::LoadContext&, resources::Failure& failure, void*) noexcept
    {
        failure = resources::Failure::None;
        return VANGUARD_NEW(TextureResource)();
    }

    void DestroyShader(resources::ResourceObject* const resource, void*) noexcept
    {
        VANGUARD_DELETE(static_cast<ShaderResource*>(resource));
    }

    void DestroyTexture(resources::ResourceObject* const resource, void*) noexcept
    {
        VANGUARD_DELETE(static_cast<TextureResource*>(resource));
    }

    const reflection::Schema* ResolveMaterialSchema(const resources::LoadContext& context, void*) noexcept
    {
        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
        {
            const resources::ResourceObject* const dependency = context.GetDependency(index).Get();
            if (dependency != nullptr && dependency->GetType() == ShaderType)
            {
                return &static_cast<const ShaderResource*>(dependency)->m_layout->schema;
            }
        }
        return nullptr;
    }

    resources::ResourceObject* CreateDynamicMaterial(const reflection::Schema& schema, const resources::LoadContext&, void*) noexcept
    {
        auto* const resource = VANGUARD_NEW(DynamicMaterialResource)(schema);
        if (!resource->m_storage)
        {
            VANGUARD_DELETE(resource);
            return nullptr;
        }
        return resource;
    }

    void* DynamicMaterialObject(resources::ResourceObject& resource, void*) noexcept
    {
        return static_cast<DynamicMaterialResource&>(resource).m_storage.address;
    }

    bool BindDynamicMaterial(resources::ResourceObject& resource, const resources::LoadContext& context, void*) noexcept
    {
        auto& material = static_cast<DynamicMaterialResource&>(resource);
        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
        {
            const resources::ResourceHandle& dependency = context.GetDependency(index);
            if (dependency)
            {
                material.m_dependencies.PushBack(dependency);
            }
        }
        return true;
    }

    void DestroyDynamicMaterial(resources::ResourceObject* const resource, void*) noexcept
    {
        VANGUARD_DELETE(static_cast<DynamicMaterialResource*>(resource));
    }

    struct MaterialDependencies
    {
        MaterialDependencies() noexcept : values(memory::pools::Resources::GetInstance()) {}

        containers::DynamicArray<streaming::DependencyDescriptor> values;
    };

    bool CollectMaterialDependency(const resources::ResourceReference reference, const resources::DependencyKind kind, void* const userData) noexcept
    {
        static_cast<MaterialDependencies*>(userData)->values.PushBack({reference, kind});
        return true;
    }

    void WaitForDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer)
    {
        for (u32 attempt = 0; attempt < 10000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = pipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeJobs == 0 && pipelineStats.activeOperations == 0 && pipelineStats.activePreparations == 0 && streamingStats.activeLoads == 0 &&
                streamingStats.activeReads == 0)
            {
                return;
            }
            concurrency::SleepOnCurrentThread(1);
        }
        Check(false, "resource and streaming work drains");
    }

    void RunShaderDerivedMaterialTest(const filesystem::AbsolutePath& directory, filesystem::Manager& files, resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer)
    {
        const ShaderParameter pbrParameters[] = {{"roughness", reflection::builtin::F32, reflection::ValueKind::F32, sizeof(f32), alignof(f32), reflection::FieldFlags::Required},
                                                 {"metallic", reflection::builtin::F32, reflection::ValueKind::F32, sizeof(f32), alignof(f32), reflection::FieldFlags::None},
                                                 {"albedo", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference, sizeof(resources::ResourceReference),
                                                  alignof(resources::ResourceReference), reflection::FieldFlags::OptionalDependency}};
        const ShaderParameter emissiveParameters[] = {{"emissive_intensity", reflection::builtin::F32, reflection::ValueKind::F32, sizeof(f32), alignof(f32), reflection::FieldFlags::None}};

        ShaderMaterialLayout pbrLayout;
        ShaderMaterialLayout emissiveLayout;
        Check(pbrLayout.Build("vanguard.shader_interface.test_pbr", pbrParameters, 3) && emissiveLayout.Build("vanguard.shader_interface.test_emissive", emissiveParameters, 1),
              "shader parameter declarations generate material schemas");
        Check(pbrLayout.schema.fieldCount == 4 && pbrLayout.Find("roughness") != nullptr && pbrLayout.Find("albedo") != nullptr && pbrLayout.Find("emissive_intensity") == nullptr &&
                  emissiveLayout.schema.fieldCount == 2 && emissiveLayout.Find("emissive_intensity") != nullptr && pbrLayout.schema.id != emissiveLayout.schema.id &&
                  pbrLayout.schema.size != emissiveLayout.schema.size,
              "material layouts differ with shader interfaces");
        Check(reflection::RegisterSchema(pbrLayout.schema), "shader-derived schema registration");

        const resources::ResourceReference shader(resources::ResourcePath::FromString("shaders/test_pbr.vshader"), ShaderType);
        const resources::ResourceReference albedo(resources::ResourcePath::FromString("textures/test_albedo.vtex"), TextureType);
        const resources::ResourceReference materialReference(resources::ResourcePath::FromString("materials/test_stone.vmat"), MaterialType);

        memory::MemoryBlock source = memory::Allocate(memory::PoolId::Resources, pbrLayout.schema.size, pbrLayout.schema.alignment);
        Check(static_cast<bool>(source), "dynamic material source storage allocation");
        auto* const sourceBytes = static_cast<u8*>(source.address);
        for (usize index = 0; index < source.size; ++index)
        {
            sourceBytes[index] = 0;
        }
        const reflection::SchemaField* const shaderField = pbrLayout.Find("shader");
        const reflection::SchemaField* const roughnessField = pbrLayout.Find("roughness");
        const reflection::SchemaField* const metallicField = pbrLayout.Find("metallic");
        const reflection::SchemaField* const albedoField = pbrLayout.Find("albedo");
        Check(shaderField != nullptr && roughnessField != nullptr && metallicField != nullptr && albedoField != nullptr, "generated PBR fields are discoverable");
        *reinterpret_cast<resources::ResourceReference*>(sourceBytes + shaderField->offset) = shader;
        *reinterpret_cast<f32*>(sourceBytes + roughnessField->offset) = 0.72f;
        *reinterpret_cast<f32*>(sourceBytes + metallicField->offset) = 0.14f;
        *reinterpret_cast<resources::ResourceReference*>(sourceBytes + albedoField->offset) = albedo;

        ByteArray materialBytes(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter materialFile(materialBytes);
        vanguard::serialization::BinaryWriter materialWriter(materialFile);
        Check(schemas::WriteObject(materialWriter, pbrLayout.schema, source.address) == schemas::Result::Success && materialWriter.Flush(), "shader-derived material serialization");

        MaterialDependencies dependencies;
        Check(schemas::VisitDependencies(pbrLayout.schema, source.address, &CollectMaterialDependency, &dependencies) == schemas::Result::Success && dependencies.values.Size() == 2,
              "generated schema drives material dependency extraction");
        memory::Free(source);

        const filesystem::AbsolutePath shaderPath = directory.AddFilePath("test_pbr.vshader");
        const filesystem::AbsolutePath texturePath = directory.AddFilePath("test_albedo.vtex");
        const filesystem::AbsolutePath materialPath = directory.AddFilePath("test_stone.vmat");
        const u8 leafBytes[] = {0x56, 0x47};
        Check(SaveBytes(shaderPath, leafBytes, 2) && SaveBytes(texturePath, leafBytes, 2) && SaveBytes(materialPath, materialBytes), "shader-derived material fixtures");

        streaming::SchemaDecoderDescriptor materialDecoder;
        materialDecoder.type = MaterialType;
        materialDecoder.name = "shader-derived material";
        materialDecoder.resolveSchema = &ResolveMaterialSchema;
        materialDecoder.create = &CreateDynamicMaterial;
        materialDecoder.object = &DynamicMaterialObject;
        materialDecoder.bindDependencies = &BindDynamicMaterial;
        materialDecoder.destroy = &DestroyDynamicMaterial;
        Check(streamer.RegisterSchemaDecoder(materialDecoder) && streamer.RegisterDecoder({ShaderType, "test shader interface", &DecodeShader, &DestroyShader, &pbrLayout}) &&
                  streamer.RegisterDecoder({TextureType, "test texture", &DecodeTexture, &DestroyTexture, nullptr}),
              "dynamic material and dependency decoders");

        Check(streamer.RegisterLoose({shader, shaderPath, {}, 0, 0}) && streamer.RegisterLoose({albedo, texturePath, {}, 0, 0}) &&
                  streamer.RegisterLoose({materialReference,
                                          materialPath,
                                          {dependencies.values.TypedData(), dependencies.values.Size()},
                                          vanguard::serialization::Crc64(materialBytes.Data(), materialBytes.Size()),
                                          0}),
              "dynamic material source registration");

        resources::PipelineRequest request = streamer.Request(materialReference, resources::LoadPriority::High);
        Check(request.TryWait(10000) && request.HasLoaded(), "material schema resolves after shader dependency fan-in");
        resources::ResourceHandle handle = request.Acquire();
        const auto* const material = static_cast<const DynamicMaterialResource*>(handle.Get());
        const auto* const loadedBytes = material != nullptr ? static_cast<const u8*>(material->m_storage.address) : nullptr;
        Check(material != nullptr && material->m_schema == &pbrLayout.schema && material->m_dependencies.Size() == 2 &&
                  *reinterpret_cast<const f32*>(loadedBytes + roughnessField->offset) == 0.72f && *reinterpret_cast<const f32*>(loadedBytes + metallicField->offset) == 0.14f,
              "published material storage follows loaded shader layout");

        handle.Reset();
        request.Reset();
        WaitForDrain(pipeline, streamer);
        Check(streamer.UnregisterDecoder(MaterialType) && streamer.UnregisterDecoder(ShaderType) && streamer.UnregisterDecoder(TextureType), "dynamic material decoders unregister");
        Check(reflection::UnregisterSchema(pbrLayout.schema.id), "shader-derived schema unregister");
        static_cast<void>(files.DeleteFile(shaderPath));
        static_cast<void>(files.DeleteFile(texturePath));
        static_cast<void>(files.DeleteFile(materialPath));
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "streamingTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");
    Check(reflection::Initialize(), "reflection initialization");

    const filesystem::AbsolutePath workingDirectory = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath testDirectory = workingDirectory.AddDirPath("vanguard_streaming_conformance");
    const filesystem::AbsolutePath packagePath = testDirectory.AddFilePath("base.vpak");
    const filesystem::AbsolutePath corruptPackagePath = testDirectory.AddFilePath("corrupt.vpak");
    const filesystem::AbsolutePath loosePath = testDirectory.AddFilePath("root.loose");
    const filesystem::AbsolutePath budgetPath = testDirectory.AddFilePath("budget.loose");
    const filesystem::AbsolutePath cancelPath = testDirectory.AddFilePath("cancel.loose");

    Check(filesystem::Initialize({workingDirectory, workingDirectory, testDirectory}), "filesystem initialization");
    filesystem::Manager& files = filesystem::GetManager();
    static_cast<void>(files.DeleteFile(packagePath));
    static_cast<void>(files.DeleteFile(corruptPackagePath));
    static_cast<void>(files.DeleteFile(loosePath));
    static_cast<void>(files.DeleteFile(budgetPath));
    static_cast<void>(files.DeleteFile(cancelPath));
    static_cast<void>(files.DeletePath(testDirectory));
    Check(files.CreatePath(testDirectory), "test directory creation");

    ByteArray baseBytes(memory::pools::Resources::GetInstance());
    ByteArray corruptBytes(memory::pools::Resources::GetInstance());
    packages::PackageReader corruptPackage;
    Check(BuildBasePackage(baseBytes), "build segmented VPAK");
    Check(BuildCorruptPackage(corruptBytes, corruptPackage), "build corrupt-payload VPAK metadata");
    Check(SaveBytes(packagePath, baseBytes), "write segmented VPAK");
    Check(SaveBytes(corruptPackagePath, corruptBytes), "write corrupted VPAK payload");
    Check(SaveBytes(loosePath, g_looseOverride.data(), g_looseOverride.size()), "write loose override");
    Check(SaveBytes(budgetPath, g_budgetPayload.data(), g_budgetPayload.size()), "write over-budget loose resource");
    Check(SaveBytes(cancelPath, g_budgetPayload.data(), 48 * 1024), "write cancellable loose resource");

    RunResourceSourceTest(packagePath, loosePath);

    auto baseFile = filesystem::RawFileReader::Create(packagePath);
    packages::PackageReader basePackage;
    Check(baseFile && basePackage.Open(*baseFile) == packages::Result::Success, "open segmented VPAK metadata");
    baseFile.Reset();

    resources::ResourceRegistry registry;
    resources::ResourcePipeline pipeline;
    streaming::ResourceStreamer streamer;
    Check(registry.Initialize(), "resource registry initialization");
    Check(pipeline.Initialize(registry), "resource pipeline initialization");
    streaming::Config streamingConfig;
    streamingConfig.stagingBudgetBytes = 64 * 1024;
    streamingConfig.maximumResourceBytes = 256 * 1024;
    Check(streamer.Initialize(pipeline, streamingConfig), "resource streamer initialization");

    RunShaderDerivedMaterialTest(testDirectory, files, pipeline, streamer);

    DecoderState decoderState;
    Check(streamer.RegisterDecoder({BlobType, "streaming blob", &DecodeBlob, &DestroyBlob, &decoderState}), "streaming decoder registration");
    RunPackageSetMountTest(testDirectory, files, pipeline, streamer);
    Check(streamer.MountPackage(basePackage, packagePath, 0), "base VPAK mount");
    Check(streamer.MountPackage(corruptPackage, corruptPackagePath, 0), "corrupt test VPAK mount");

    const resources::ResourceReference root = MakeReference("stream/root.vblob");
    const resources::ResourceReference dependency = MakeReference("stream/dependency.vblob");
    const std::array<streaming::DependencyDescriptor, 1> looseDependencies{{{dependency, resources::DependencyKind::Required}}};
    Check(streamer.RegisterLoose({root, loosePath, containers::ArraySpan<const streaming::DependencyDescriptor>(looseDependencies.data(), static_cast<u32>(looseDependencies.size())),
                                  vanguard::serialization::Crc64(g_looseOverride.data(), g_looseOverride.size()), 100}),
          "loose override registration");

    resources::PipelineRequest looseRequest = streamer.Request(root, resources::LoadPriority::High);
    Check(looseRequest.TryWait(10000) && looseRequest.HasLoaded(), "loose source loads asynchronously");
    resources::ResourceHandle looseHandle = looseRequest.Acquire();
    const auto* const looseResource = static_cast<const BlobResource*>(looseHandle.Get());
    Check(looseResource != nullptr && looseResource->m_contentCrc64 == vanguard::serialization::Crc64(g_looseOverride.data(), g_looseOverride.size()) &&
              looseResource->m_dependencyCount == 1,
          "loose source precedence and package dependency fan-in");
    looseHandle.Reset();
    looseRequest.Reset();
    WaitForDrain(pipeline, streamer);
    Check(streamer.UnregisterLoose(root.GetPath()), "loose override removal");

    RunRangeQueueTest(streamer);

    resources::PipelineRequest packageRequest = streamer.Request(root, resources::LoadPriority::Critical);
    Check(packageRequest.TryWait(10000) && packageRequest.HasLoaded(), "segmented VPAK loads asynchronously");
    resources::ResourceHandle packageHandle = packageRequest.Acquire();
    const auto* const packageResource = static_cast<const BlobResource*>(packageHandle.Get());
    u64 expectedPackageCrc = vanguard::serialization::Crc64(g_compressible.data(), g_compressible.size());
    std::array<u8, g_compressible.size() + g_tail.size()> expectedPackageBytes{};
    std::memcpy(expectedPackageBytes.data(), g_compressible.data(), g_compressible.size());
    std::memcpy(expectedPackageBytes.data() + g_compressible.size(), g_tail.data(), g_tail.size());
    expectedPackageCrc = vanguard::serialization::Crc64(expectedPackageBytes.data(), expectedPackageBytes.size());
    Check(packageResource != nullptr && packageResource->m_contentCrc64 == expectedPackageCrc && packageResource->m_byteCount == expectedPackageBytes.size() &&
              packageResource->m_dependencyCount == 1,
          "mixed LZ4/raw VPAK segments decode and validate");
    packageHandle.Reset();
    packageRequest.Reset();

    resources::PipelineRequest corruptRequest = streamer.Request(MakeReference("stream/corrupt.vblob"));
    Check(corruptRequest.TryWait(10000) && corruptRequest.HasFailed() && corruptRequest.GetError() == resources::Failure::IntegrityFailure, "stored-segment corruption is rejected");
    corruptRequest.Reset();

    Check(streamer.RegisterLoose({MakeReference("stream/budget.vblob"), budgetPath, {}, vanguard::serialization::Crc64(g_budgetPayload.data(), g_budgetPayload.size()), 0}),
          "over-budget source registration");
    resources::PipelineRequest budgetRequest = streamer.Request(MakeReference("stream/budget.vblob"));
    Check(budgetRequest.TryWait(10000) && budgetRequest.HasFailed() && budgetRequest.GetError() == resources::Failure::OutOfMemory, "staging budget rejects oversized load");
    budgetRequest.Reset();

    Check(streamer.RegisterLoose({MakeReference("stream/cancel.vblob"), cancelPath, {}, 0, 0}), "cancellable source registration");
    resources::PipelineRequest cancelRequest = streamer.Request(MakeReference("stream/cancel.vblob"), resources::LoadPriority::Background);
    Check(cancelRequest.Cancel(), "explicit cancellation accepted");
    Check(cancelRequest.GetStatus() == resources::State::Cancelled || (cancelRequest.TryWait(10000) && cancelRequest.GetError() == resources::Failure::Cancelled),
          "cancelled request reaches terminal cancellation");
    cancelRequest.Reset();

    WaitForDrain(pipeline, streamer);
    const streaming::Stats stats = streamer.GetStats();
    Check(stats.registeredDecoders == 1 && stats.mountedPackages == 2 && stats.activeLoads == 0 && stats.activeReads == 0 && stats.stagingBytesInUse == 0 && stats.bytesRead != 0 &&
              stats.completedLoads >= 3 && stats.integrityFailures >= 1 && stats.budgetRejections >= 1,
          "streaming telemetry reports work, failures, and no leaks");
    Check(decoderState.decoded.GetValue() == decoderState.destroyed.GetValue(), "decoded resource objects are released after request drain");

    Check(streamer.UnmountPackage(corruptPackage), "corrupt package unmount");
    Check(streamer.UnmountPackage(basePackage), "base package unmount");
    Check(streamer.UnregisterDecoder(BlobType), "streaming decoder unregistration");
    Check(streamer.Shutdown(), "resource streamer shutdown");
    Check(pipeline.Shutdown(), "resource pipeline shutdown");
    Check(registry.Shutdown(), "resource registry shutdown");
    Check(jobs::Shutdown(), "jobs shutdown");

    corruptPackage.Close();
    basePackage.Close();
    static_cast<void>(files.DeleteFile(packagePath));
    static_cast<void>(files.DeleteFile(corruptPackagePath));
    static_cast<void>(files.DeleteFile(loosePath));
    static_cast<void>(files.DeleteFile(budgetPath));
    static_cast<void>(files.DeleteFile(cancelPath));
    static_cast<void>(files.DeletePath(testDirectory));
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[streamingTests] Vanguard asynchronous resource storage "
                  "checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
