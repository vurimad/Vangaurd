#include <vanguard/assets/package_set_assembly.hpp>

#include <vanguard/memory/pool.hpp>

#include <limits>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;
    namespace vgser = vanguard::serialization;

    constexpr u8 PlacementLittleEndian = 1;
    constexpr u8 PlacementEncoding = 1;

    [[nodiscard]] constexpr bool AddOverflow(const u64 left, const u64 right) noexcept
    {
        return right > std::numeric_limits<u64>::max() - left;
    }

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[]{static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                         static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    [[nodiscard]] u64 DigestId(crypto::Sha256Builder& hash) noexcept
    {
        crypto::Digest256 digest;
        if (!hash.Finalize(digest))
        {
            return 0;
        }
        u64 value = 0;
        for (u32 index = 0; index < 8; ++index)
        {
            value |= static_cast<u64>(digest.bytes[index]) << (index * 8u);
        }
        return value != 0 ? value : 1;
    }

    [[nodiscard]] u32 GetTargetPlatformId(const TargetPlatform target) noexcept
    {
        switch (target)
        {
        case TargetPlatform::WindowsD3D12:
            return vgser::MakeFourCC('W', 'D', '1', '2');
        case TargetPlatform::WindowsVulkan:
            return vgser::MakeFourCC('W', 'V', 'K', 'N');
        case TargetPlatform::LinuxVulkan:
            return vgser::MakeFourCC('L', 'V', 'K', 'N');
        case TargetPlatform::Count:
            break;
        }
        return 0;
    }

    [[nodiscard]] bool ReferenceLess(const resources::ResourceReference left, const resources::ResourceReference right) noexcept
    {
        if (left.GetPath() != right.GetPath())
        {
            return left.GetPath() < right.GetPath();
        }
        return left.ExpectedType() < right.ExpectedType();
    }

    [[nodiscard]] i32 FindResourceIndex(const PackageBuildPlan& source, const resources::ResourceReference resource) noexcept
    {
        for (u32 index = 0; index < source.resources.Size(); ++index)
        {
            if (source.resources[index].resource == resource)
            {
                return static_cast<i32>(index);
            }
        }
        return -1;
    }

    [[nodiscard]] u64 EstimateResourceBytes(const PlannedPackageResource& resource, const u8 packageAlignmentLog2) noexcept
    {
        if (packageAlignmentLog2 > 20)
        {
            return std::numeric_limits<u64>::max();
        }
        u64 bytes = packages::Resource::WireSize + packages::MaximumResourcePathBytes;
        for (const PlannedPackageSegment& segment : resource.segments)
        {
            if (segment.artifact.alignmentLog2 > 20)
            {
                return std::numeric_limits<u64>::max();
            }
            const u8 effectiveAlignmentLog2 = segment.artifact.alignmentLog2 > packageAlignmentLog2 ? segment.artifact.alignmentLog2 : packageAlignmentLog2;
            const u64 alignmentWaste = (u64{1} << effectiveAlignmentLog2) - 1u;
            if (AddOverflow(bytes, packages::Segment::WireSize) || AddOverflow(bytes + packages::Segment::WireSize, alignmentWaste) ||
                AddOverflow(bytes + packages::Segment::WireSize + alignmentWaste, segment.artifact.byteCount))
            {
                return std::numeric_limits<u64>::max();
            }
            bytes += packages::Segment::WireSize + alignmentWaste + segment.artifact.byteCount;
        }
        const u64 dependencyBytes = static_cast<u64>(resource.dependencies.Size()) * packages::Dependency::WireSize;
        return AddOverflow(bytes, dependencyBytes) ? std::numeric_limits<u64>::max() : bytes + dependencyBytes;
    }

    [[nodiscard]] PlannedDataPackage* FindDataPackage(PackageSetBuildPlan& plan, const u32 packageNumber) noexcept
    {
        for (PlannedDataPackage& package : plan.packages)
        {
            if (package.packageNumber == packageNumber)
            {
                return &package;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const PackagePlacement* FindPlacement(const PackagePlacementState& state, const resources::ResourceReference resource) noexcept
    {
        for (const PackagePlacement& placement : state.placements)
        {
            if (placement.resource == resource)
            {
                return &placement;
            }
        }
        return nullptr;
    }

    [[nodiscard]] PackagingResult AddDataPackage(PackageSetBuildPlan& plan, const u32 packageNumber, PlannedDataPackage*& package) noexcept
    {
        if (packageNumber > packages::MaximumPackageNumber || FindDataPackage(plan, packageNumber) != nullptr)
        {
            return PackagingResult::InvalidArgument;
        }
        const u32 previous = plan.packages.Size();
        plan.packages.PushBack(PlannedDataPackage{});
        if (plan.packages.Size() != previous + 1u)
        {
            return PackagingResult::OutOfMemory;
        }
        package = &plan.packages.Back();
        package->packageNumber = packageNumber;
        package->estimatedBytes = packages::PackageHeader::WireSize;
        return PackagingResult::Success;
    }

    void SortPackages(PackageSetBuildPlan& plan) noexcept
    {
        for (u32 index = 1; index < plan.packages.Size(); ++index)
        {
            u32 current = index;
            while (current != 0 && plan.packages[current].packageNumber < plan.packages[current - 1u].packageNumber)
            {
                PlannedDataPackage temporary(static_cast<PlannedDataPackage&&>(plan.packages[current]));
                plan.packages[current] = static_cast<PlannedDataPackage&&>(plan.packages[current - 1u]);
                plan.packages[current - 1u] = static_cast<PlannedDataPackage&&>(temporary);
                --current;
            }
        }
    }

    [[nodiscard]] u64 ComputePackageId(const u64 gameId, const u32 packageNumber) noexcept
    {
        crypto::Sha256Builder hash;
        HashU64(hash, gameId);
        HashU32(hash, vgser::MakeFourCC('V', 'P', 'I', 'D'));
        HashU32(hash, packageNumber);
        return DigestId(hash);
    }

    [[nodiscard]] u64 ComputePackageBuildId(const PackageBuildPlan& source, const PlannedDataPackage& package) noexcept
    {
        crypto::Sha256Builder hash;
        HashU64(hash, source.buildId);
        HashU32(hash, package.packageNumber);
        HashU32(hash, package.resourceIndices.Size());
        for (const u32 resourceIndex : package.resourceIndices)
        {
            const PlannedPackageResource& resource = source.resources[resourceIndex];
            HashU64(hash, resource.resource.GetPath().Id());
            HashU32(hash, resource.resource.ExpectedType());
            static_cast<void>(hash.Update(resource.origin.content.bytes, BuildFingerprint::ByteCount));
        }
        return DigestId(hash);
    }

    [[nodiscard]] u64 ComputePackageSetBuildId(const PackageSetBuildPlan& plan) noexcept
    {
        crypto::Sha256Builder hash;
        HashU64(hash, plan.gameId);
        HashU64(hash, plan.source->buildId);
        HashU32(hash, plan.targetPlatformId);
        HashU64(hash, plan.startupWorld.GetPath().Id());
        HashU32(hash, plan.startupWorld.ExpectedType());
        HashU64(hash, plan.defaultInput.GetPath().Id());
        HashU32(hash, plan.defaultInput.ExpectedType());
        HashU64(hash, plan.rendererBootstrap.GetPath().Id());
        HashU32(hash, plan.rendererBootstrap.ExpectedType());
        HashU32(hash, plan.packages.Size());
        for (const PlannedDataPackage& package : plan.packages)
        {
            HashU32(hash, package.packageNumber);
            HashU64(hash, package.packageId);
            HashU64(hash, package.buildId);
            HashU64(hash, package.estimatedBytes);
        }
        return DigestId(hash);
    }

    [[nodiscard]] PackagingResult MakePackagePlan(const PackageSetBuildPlan& setPlan, const PlannedDataPackage& dataPackage,
                                                  PackageBuildPlan& packagePlan) noexcept
    {
        packagePlan.Clear();
        packagePlan.packageId = dataPackage.packageId;
        packagePlan.buildId = dataPackage.packageNumber == 0 ? setPlan.buildId : dataPackage.buildId;
        packagePlan.target = setPlan.source->target;
        packagePlan.options = setPlan.source->options;
        packagePlan.options.packageId = packagePlan.packageId;
        packagePlan.options.buildId = packagePlan.buildId;
        for (const u32 resourceIndex : dataPackage.resourceIndices)
        {
            if (resourceIndex >= setPlan.source->resources.Size())
            {
                packagePlan.Clear();
                return PackagingResult::InvalidState;
            }
            const u32 previous = packagePlan.resources.Size();
            packagePlan.resources.PushBack(setPlan.source->resources[resourceIndex]);
            if (packagePlan.resources.Size() != previous + 1u)
            {
                packagePlan.Clear();
                return PackagingResult::OutOfMemory;
            }
        }
        return packagePlan.IsPrepared() ? PackagingResult::Success : PackagingResult::InvalidState;
    }

    struct StagedPackage
    {
        u32 packageNumber = 0;
        bool published = false;
        filesystem::AbsolutePath target;
        filesystem::AbsolutePath temporary;
        packages::PackageSetEntry catalogEntry;
    };

    [[nodiscard]] bool BuildPackagePaths(const filesystem::AbsolutePath& directory, const u32 packageNumber, filesystem::AbsolutePath& target,
                                         filesystem::AbsolutePath& temporary) noexcept
    {
        char fileName[17]{};
        usize fileNameSize = 0;
        if (packages::FormatPackageFileName(packageNumber, fileName, 13, fileNameSize) != packages::Result::Success)
        {
            return false;
        }
        target = directory.AddFilePath(fileName);
        fileName[12] = '.';
        fileName[13] = 't';
        fileName[14] = 'm';
        fileName[15] = 'p';
        fileName[16] = '\0';
        temporary = directory.AddFilePath(fileName);
        return !target.Empty() && !temporary.Empty();
    }

    [[nodiscard]] PackagingResult ValidateAndDigestPackage(const filesystem::AbsolutePath& path, const PlannedDataPackage& planned, const u64 expectedBuildId,
                                                           const PackageSetPublicationLimits& limits, packages::PackageSetEntry& entry) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        auto reader = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader)
        {
            return PackagingResult::IoFailure;
        }
        packages::PackageReader package;
        const packages::Result opened = package.Open(*reader, limits.package.validation);
        if (opened != packages::Result::Success || package.GetHeader().packageId != planned.packageId || package.GetHeader().buildId != expectedBuildId ||
            package.GetHeader().resourceCount != planned.resourceIndices.Size() || package.GetHeader().fileSize > planned.estimatedBytes ||
            package.GetHeader().fileSize > limits.package.validation.maximumFileSize || package.HasPackageSet() != (planned.packageNumber == 0))
        {
            return PackagingResult::ValidationFailed;
        }

        entry.packageNumber = planned.packageNumber;
        entry.flags = packages::PackageSetEntryFlags::Required;
        entry.mountPriority = 0;
        entry.packageId = package.GetHeader().packageId;
        entry.buildId = package.GetHeader().buildId;
        entry.fileSize = package.GetHeader().fileSize;
        entry.indexCrc64 = package.GetHeader().indexCrc64;
        package.Close();

        containers::DynamicArray<u8> buffer(memory::pools::Assets::GetInstance());
        buffer.Resize(limits.hashBufferBytes);
        if (buffer.Size() != limits.hashBufferBytes || limits.hashBufferBytes == 0)
        {
            return PackagingResult::OutOfMemory;
        }
        crypto::Sha256Builder hash;
        reader->Seek(0);
        u64 remaining = entry.fileSize;
        while (remaining != 0)
        {
            const usize batch = remaining > buffer.Size() ? buffer.Size() : static_cast<usize>(remaining);
            reader->Serialize(buffer.Data(), batch);
            if (reader->HasErrors() || !hash.Update(buffer.Data(), batch))
            {
                return PackagingResult::IoFailure;
            }
            remaining -= batch;
        }
        crypto::Digest256 digest;
        if (!hash.Finalize(digest))
        {
            return PackagingResult::InvalidState;
        }
        for (u32 byte = 0; byte < sizeof(entry.contentDigest); ++byte)
        {
            entry.contentDigest[byte] = digest.bytes[byte];
        }
        return PackagingResult::Success;
    }

    void CleanupStagedPackages(containers::DynamicArray<StagedPackage>& staged, const bool removeTargets) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        for (StagedPackage& package : staged)
        {
            if (!package.temporary.Empty() && manager.FileExist(package.temporary))
            {
                static_cast<void>(manager.DeleteFile(package.temporary));
            }
            if (removeTargets && package.published && !package.target.Empty() && manager.FileExist(package.target))
            {
                static_cast<void>(manager.DeleteFile(package.target));
            }
        }
    }
} // namespace

namespace vanguard::assets
{
    PackagePlacementState::PackagePlacementState() noexcept : placements(memory::pools::Assets::GetInstance()) {}

    void PackagePlacementState::Clear() noexcept
    {
        version = CurrentVersion;
        gameId = 0;
        target = TargetPlatform::WindowsD3D12;
        placements.Clear();
    }

    bool PackagePlacementState::IsValid() const noexcept
    {
        if (version != CurrentVersion || gameId == 0 || target >= TargetPlatform::Count || placements.Empty())
        {
            return false;
        }
        for (u32 index = 0; index < placements.Size(); ++index)
        {
            const PackagePlacement& placement = placements[index];
            if (!placement.resource.IsValid() || !placement.resource.IsTyped() || placement.packageNumber > packages::MaximumPackageNumber ||
                (index != 0 && !ReferenceLess(placements[index - 1u].resource, placement.resource)))
            {
                return false;
            }
        }
        return true;
    }

    PackagingResult WritePackagePlacementState(filesystem::IFile& file, const PackagePlacementState& state, const PackagePlacementLimits& limits) noexcept
    {
        if (!file.IsWriter() || file.GetOffset() != 0 || file.GetSize() != 0 || !state.IsValid() || state.placements.Size() > limits.maximumResources)
        {
            return PackagingResult::InvalidArgument;
        }
        const u64 payloadSize = static_cast<u64>(state.placements.Size()) * PackagePlacementState::EntryWireSize;
        if (payloadSize > limits.maximumBytes || PackagePlacementState::HeaderWireSize > limits.maximumBytes - payloadSize)
        {
            return PackagingResult::LimitExceeded;
        }

        containers::DynamicArray<u8> payload(memory::pools::Assets::GetInstance());
        payload.Reserve(static_cast<u32>(payloadSize));
        filesystem::MemoryFileWriter payloadFile(payload);
        serialization::BinaryWriter payloadWriter(payloadFile);
        for (const PackagePlacement& placement : state.placements)
        {
            if (!payloadWriter.WriteU64(placement.resource.GetPath().Id()) || !payloadWriter.WriteU32(placement.resource.ExpectedType()) ||
                !payloadWriter.WriteU16(static_cast<u16>(placement.packageNumber)) || !payloadWriter.WriteU16(0))
            {
                return PackagingResult::IoFailure;
            }
        }

        u8 header[PackagePlacementState::HeaderWireSize]{};
        filesystem::MemoryFileWriterExternalBuffer headerFile(header, sizeof(header));
        serialization::BinaryWriter headerWriter(headerFile);
        const bool encoded = headerWriter.WriteU32(PackagePlacementMagic) && headerWriter.WriteU8(PlacementLittleEndian) &&
                             headerWriter.WriteU8(PlacementEncoding) && headerWriter.WriteU16(PackagePlacementState::HeaderWireSize) &&
                             headerWriter.WriteU16(state.version.major) && headerWriter.WriteU16(state.version.minor) && headerWriter.WriteU32(0) &&
                             headerWriter.WriteU64(state.gameId) && headerWriter.WriteU8(static_cast<u8>(state.target)) && headerWriter.WriteU8(0) &&
                             headerWriter.WriteU16(0) && headerWriter.WriteU32(state.placements.Size()) &&
                             headerWriter.WriteU16(PackagePlacementState::EntryWireSize) && headerWriter.WriteU16(0) && headerWriter.WriteU64(payloadSize) &&
                             headerWriter.WriteU64(serialization::Crc64(payload.Data(), payload.Size())) &&
                             headerWriter.WriteU32(serialization::Crc32(header, 52)) && headerWriter.WriteU64(0);
        serialization::BinaryWriter writer(file);
        return encoded && headerWriter.Position() == sizeof(header) && writer.WriteBytes(header, sizeof(header)) &&
                       writer.WriteBytes(payload.Data(), payload.Size()) && writer.Flush()
                   ? PackagingResult::Success
                   : PackagingResult::IoFailure;
    }

    PackagingResult ReadPackagePlacementState(filesystem::IFile& file, PackagePlacementState& state, const PackagePlacementLimits& limits) noexcept
    {
        state.Clear();
        if (!file.IsReader() || file.GetSize() < PackagePlacementState::HeaderWireSize || file.GetSize() > limits.maximumBytes)
        {
            return PackagingResult::InvalidArgument;
        }
        u8 header[PackagePlacementState::HeaderWireSize]{};
        serialization::BinaryReader source(file);
        if (!source.Seek(0) || !source.ReadBytes(header, sizeof(header)))
        {
            return PackagingResult::IoFailure;
        }
        const u32 expectedHeaderCrc =
            static_cast<u32>(header[52]) | (static_cast<u32>(header[53]) << 8u) | (static_cast<u32>(header[54]) << 16u) | (static_cast<u32>(header[55]) << 24u);
        if (serialization::Crc32(header, 52) != expectedHeaderCrc)
        {
            return PackagingResult::CorruptManifest;
        }

        filesystem::MemoryFileReader headerFile(header, sizeof(header), 0);
        serialization::BinaryReader reader(headerFile);
        u32 magic = 0;
        u8 byteOrder = 0;
        u8 encoding = 0;
        u16 headerSize = 0;
        u32 flags = 0;
        u8 target = 0;
        u8 reserved8 = 0;
        u16 reserved16A = 0;
        u32 count = 0;
        u16 entrySize = 0;
        u16 reserved16B = 0;
        u64 payloadSize = 0;
        u64 payloadCrc64 = 0;
        u32 ignoredHeaderCrc = 0;
        u64 reserved64 = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU8(byteOrder) || !reader.ReadU8(encoding) || !reader.ReadU16(headerSize) ||
            !reader.ReadU16(state.version.major) || !reader.ReadU16(state.version.minor) || !reader.ReadU32(flags) || !reader.ReadU64(state.gameId) ||
            !reader.ReadU8(target) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16A) || !reader.ReadU32(count) || !reader.ReadU16(entrySize) ||
            !reader.ReadU16(reserved16B) || !reader.ReadU64(payloadSize) || !reader.ReadU64(payloadCrc64) || !reader.ReadU32(ignoredHeaderCrc) ||
            !reader.ReadU64(reserved64))
        {
            state.Clear();
            return PackagingResult::CorruptManifest;
        }
        if (magic != PackagePlacementMagic)
        {
            state.Clear();
            return PackagingResult::InvalidMagic;
        }
        if (state.version != PackagePlacementState::CurrentVersion)
        {
            state.Clear();
            return PackagingResult::UnsupportedVersion;
        }
        state.target = static_cast<TargetPlatform>(target);
        if (byteOrder != PlacementLittleEndian || encoding != PlacementEncoding || headerSize != PackagePlacementState::HeaderWireSize || flags != 0 ||
            reserved8 != 0 || reserved16A != 0 || reserved16B != 0 || reserved64 != 0 || state.gameId == 0 || state.target >= TargetPlatform::Count ||
            entrySize != PackagePlacementState::EntryWireSize || count == 0)
        {
            state.Clear();
            return PackagingResult::CorruptManifest;
        }
        if (count > limits.maximumResources || payloadSize > limits.maximumBytes)
        {
            state.Clear();
            return PackagingResult::LimitExceeded;
        }
        if (payloadSize != static_cast<u64>(count) * PackagePlacementState::EntryWireSize ||
            PackagePlacementState::HeaderWireSize + payloadSize != file.GetSize() || payloadSize > std::numeric_limits<u32>::max())
        {
            state.Clear();
            return PackagingResult::CorruptManifest;
        }

        containers::DynamicArray<u8> payload(memory::pools::Assets::GetInstance());
        payload.Resize(static_cast<u32>(payloadSize));
        if (!source.ReadBytes(payload.Data(), payload.Size()) || serialization::Crc64(payload.Data(), payload.Size()) != payloadCrc64)
        {
            state.Clear();
            return PackagingResult::CorruptManifest;
        }
        filesystem::MemoryFileReader payloadFile(payload, 0);
        serialization::BinaryReader payloadReader(payloadFile);
        state.placements.Resize(count);
        for (PackagePlacement& placement : state.placements)
        {
            resources::ResourceId id = resources::InvalidResourceId;
            resources::ResourceTypeId type = resources::InvalidResourceTypeId;
            u16 packageNumber = 0;
            u16 reserved = 0;
            if (!payloadReader.ReadU64(id) || !payloadReader.ReadU32(type) || !payloadReader.ReadU16(packageNumber) || !payloadReader.ReadU16(reserved) ||
                reserved != 0)
            {
                state.Clear();
                return PackagingResult::CorruptManifest;
            }
            placement.resource = resources::ResourceReference(resources::ResourcePath::FromId(id), type);
            placement.packageNumber = packageNumber;
        }
        if (!state.IsValid())
        {
            state.Clear();
            return PackagingResult::CorruptManifest;
        }
        return PackagingResult::Success;
    }

    PlannedDataPackage::PlannedDataPackage() noexcept : resourceIndices(memory::pools::Assets::GetInstance()) {}

    PackageSetBuildPlan::PackageSetBuildPlan() noexcept : packages(memory::pools::Assets::GetInstance()) {}

    void PackageSetBuildPlan::Clear() noexcept
    {
        source = nullptr;
        gameId = 0;
        buildId = 0;
        targetPlatformId = 0;
        startupWorld = {};
        defaultInput = {};
        rendererBootstrap = {};
        maximumPackageBytes = 0;
        packages.Clear();
    }

    bool PackageSetBuildPlan::IsPrepared() const noexcept
    {
        if (source == nullptr || !source->IsPrepared() || gameId == 0 || buildId == 0 || targetPlatformId == 0 || !startupWorld.IsValid() ||
            !startupWorld.IsTyped() || !defaultInput.IsValid() || !defaultInput.IsTyped() || maximumPackageBytes == 0 || packages.Empty() ||
            packages[0].packageNumber != 0)
        {
            return false;
        }
        for (u32 index = 0; index < packages.Size(); ++index)
        {
            const PlannedDataPackage& package = packages[index];
            if (package.packageId == 0 || package.buildId == 0 || package.resourceIndices.Empty() || package.estimatedBytes > maximumPackageBytes ||
                (index != 0 && packages[index - 1u].packageNumber >= package.packageNumber))
            {
                return false;
            }
        }
        return true;
    }

    const PlannedDataPackage* PackageSetBuildPlan::FindPackage(const u32 packageNumber) const noexcept
    {
        for (const PlannedDataPackage& package : packages)
        {
            if (package.packageNumber == packageNumber)
            {
                return &package;
            }
        }
        return nullptr;
    }

    PackagingResult PackageSetPlanner::Prepare(const PackageBuildPlan& source, const PackageSetPlanOptions& options,
                                               const PackagePlacementState* const previousPlacement, PackageSetBuildPlan& plan,
                                               PackagePlacementState& nextPlacement) const noexcept
    {
        plan.Clear();
        nextPlacement.Clear();
        if (!source.IsPrepared() || options.gameId == 0 || !options.startupWorld.IsValid() || !options.startupWorld.IsTyped() ||
            !options.defaultInput.IsValid() || !options.defaultInput.IsTyped() || options.targetPackageBytes == 0 ||
            options.maximumPackageBytes < options.targetPackageBytes || options.maximumBootstrapBytes == 0 ||
            options.maximumBootstrapBytes > options.maximumPackageBytes || options.maximumPackages == 0 ||
            options.maximumPackages > packages::MaximumPackageNumber + 1u)
        {
            return PackagingResult::InvalidArgument;
        }
        if (previousPlacement != nullptr && !options.rebalance &&
            (!previousPlacement->IsValid() || previousPlacement->gameId != options.gameId || previousPlacement->target != source.target))
        {
            return PackagingResult::InvalidArgument;
        }

        const i32 startupIndex = FindResourceIndex(source, options.startupWorld);
        const i32 inputIndex = FindResourceIndex(source, options.defaultInput);
        const i32 rendererIndex = options.rendererBootstrap.IsValid() ? FindResourceIndex(source, options.rendererBootstrap) : -1;
        if (options.rendererBootstrap.IsValid() && (!options.rendererBootstrap.IsTyped() || rendererIndex < 0))
            return PackagingResult::MissingResource;
        if (startupIndex < 0 || inputIndex < 0)
        {
            return PackagingResult::MissingResource;
        }
        containers::DynamicArray<u8> bootstrap(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> pending(memory::pools::Assets::GetInstance());
        bootstrap.Resize(source.resources.Size());
        if (bootstrap.Size() != source.resources.Size())
        {
            return PackagingResult::OutOfMemory;
        }
        pending.PushBack(static_cast<u32>(startupIndex));
        if (pending.Size() != 1)
        {
            return PackagingResult::OutOfMemory;
        }
        bootstrap[static_cast<u32>(startupIndex)] = 1;
        if (inputIndex != startupIndex)
        {
            pending.PushBack(static_cast<u32>(inputIndex));
            if (pending.Size() != 2)
            {
                return PackagingResult::OutOfMemory;
            }
            bootstrap[static_cast<u32>(inputIndex)] = 1;
        }
        if (rendererIndex >= 0 && bootstrap[static_cast<u32>(rendererIndex)] == 0)
        {
            bootstrap[static_cast<u32>(rendererIndex)] = 1;
            const u32 previousCount = pending.Size();
            pending.PushBack(static_cast<u32>(rendererIndex));
            if (pending.Size() != previousCount + 1u)
                return PackagingResult::OutOfMemory;
        }
        for (u32 pendingIndex = 0; pendingIndex < pending.Size(); ++pendingIndex)
        {
            const PlannedPackageResource& resource = source.resources[pending[pendingIndex]];
            for (const PlannedPackageDependency& dependency : resource.dependencies)
            {
                if (dependency.kind != resources::DependencyKind::Required)
                {
                    continue;
                }
                const i32 dependencyIndex = FindResourceIndex(source, dependency.resource);
                if (dependencyIndex < 0)
                {
                    return PackagingResult::MissingResource;
                }
                if (bootstrap[static_cast<u32>(dependencyIndex)] == 0)
                {
                    bootstrap[static_cast<u32>(dependencyIndex)] = 1;
                    const u32 previousPending = pending.Size();
                    pending.PushBack(static_cast<u32>(dependencyIndex));
                    if (pending.Size() != previousPending + 1u)
                    {
                        return PackagingResult::OutOfMemory;
                    }
                }
            }
        }

        plan.source = &source;
        plan.gameId = options.gameId;
        plan.targetPlatformId = GetTargetPlatformId(source.target);
        plan.startupWorld = options.startupWorld;
        plan.defaultInput = options.defaultInput;
        plan.rendererBootstrap = options.rendererBootstrap;
        plan.maximumPackageBytes = options.maximumPackageBytes;
        PlannedDataPackage* addedPackage = nullptr;
        PackagingResult added = AddDataPackage(plan, 0, addedPackage);
        if (added != PackagingResult::Success)
        {
            plan.Clear();
            return added;
        }

        for (u32 resourceIndex = 0; resourceIndex < source.resources.Size(); ++resourceIndex)
        {
            const PlannedPackageResource& resource = source.resources[resourceIndex];
            const u64 estimatedBytes = EstimateResourceBytes(resource, source.options.dataAlignmentLog2);
            if (estimatedBytes == std::numeric_limits<u64>::max() || estimatedBytes > options.maximumPackageBytes)
            {
                plan.Clear();
                return PackagingResult::LimitExceeded;
            }
            if (bootstrap[resourceIndex] != 0)
            {
                PlannedDataPackage& rootPackage = plan.packages[0];
                if (rootPackage.estimatedBytes > options.maximumBootstrapBytes - estimatedBytes)
                {
                    plan.Clear();
                    return PackagingResult::LimitExceeded;
                }
                const u32 previousResources = rootPackage.resourceIndices.Size();
                rootPackage.resourceIndices.PushBack(resourceIndex);
                if (rootPackage.resourceIndices.Size() != previousResources + 1u)
                {
                    plan.Clear();
                    return PackagingResult::OutOfMemory;
                }
                rootPackage.estimatedBytes += estimatedBytes;
                continue;
            }

            PlannedDataPackage* destination = nullptr;
            const PackagePlacement* previous =
                previousPlacement != nullptr && !options.rebalance ? FindPlacement(*previousPlacement, resource.resource) : nullptr;
            if (previous != nullptr)
            {
                if (previous->packageNumber == 0 || previous->packageNumber >= options.maximumPackages)
                {
                    plan.Clear();
                    return PackagingResult::RebalanceRequired;
                }
                destination = FindDataPackage(plan, previous->packageNumber);
                if (destination == nullptr)
                {
                    added = AddDataPackage(plan, previous->packageNumber, destination);
                    if (added != PackagingResult::Success)
                    {
                        plan.Clear();
                        return added;
                    }
                }
                if (destination->estimatedBytes > options.maximumPackageBytes - estimatedBytes)
                {
                    plan.Clear();
                    return PackagingResult::RebalanceRequired;
                }
            }
            else
            {
                for (PlannedDataPackage& candidate : plan.packages)
                {
                    if (candidate.packageNumber != 0 && estimatedBytes <= options.targetPackageBytes &&
                        candidate.estimatedBytes <= options.targetPackageBytes - estimatedBytes)
                    {
                        destination = &candidate;
                        break;
                    }
                }
                if (destination == nullptr)
                {
                    u32 packageNumber = 1;
                    while (packageNumber < options.maximumPackages && FindDataPackage(plan, packageNumber) != nullptr)
                    {
                        ++packageNumber;
                    }
                    if (packageNumber >= options.maximumPackages)
                    {
                        plan.Clear();
                        return PackagingResult::LimitExceeded;
                    }
                    added = AddDataPackage(plan, packageNumber, destination);
                    if (added != PackagingResult::Success)
                    {
                        plan.Clear();
                        return added;
                    }
                }
            }
            const u32 previousResources = destination->resourceIndices.Size();
            destination->resourceIndices.PushBack(resourceIndex);
            if (destination->resourceIndices.Size() != previousResources + 1u)
            {
                plan.Clear();
                return PackagingResult::OutOfMemory;
            }
            destination->estimatedBytes += estimatedBytes;
        }

        SortPackages(plan);
        PlannedDataPackage& rootPackage = plan.packages[0];
        const u64 packageSetBytes = packages::PackageSet::HeaderWireSize + packages::PackageSet::BootstrapReferenceWireSize +
            static_cast<u64>(plan.packages.Size() - 1u) * packages::PackageSetEntry::WireSize;
        if (packageSetBytes > options.maximumBootstrapBytes || packageSetBytes > options.maximumPackageBytes ||
            rootPackage.estimatedBytes > options.maximumBootstrapBytes - packageSetBytes ||
            rootPackage.estimatedBytes > options.maximumPackageBytes - packageSetBytes)
        {
            plan.Clear();
            return PackagingResult::LimitExceeded;
        }
        rootPackage.estimatedBytes += packageSetBytes;
        for (PlannedDataPackage& package : plan.packages)
        {
            package.packageId = ComputePackageId(plan.gameId, package.packageNumber);
            package.buildId = ComputePackageBuildId(source, package);
        }
        plan.buildId = ComputePackageSetBuildId(plan);
        plan.packages[0].buildId = plan.buildId;

        nextPlacement.gameId = options.gameId;
        nextPlacement.target = source.target;
        for (u32 resourceIndex = 0; resourceIndex < source.resources.Size(); ++resourceIndex)
        {
            u32 assignedPackage = packages::MaximumPackageNumber + 1u;
            for (const PlannedDataPackage& package : plan.packages)
            {
                for (const u32 assignedIndex : package.resourceIndices)
                {
                    if (assignedIndex == resourceIndex)
                    {
                        assignedPackage = package.packageNumber;
                        break;
                    }
                }
                if (assignedPackage <= packages::MaximumPackageNumber)
                {
                    break;
                }
            }
            if (assignedPackage > packages::MaximumPackageNumber)
            {
                plan.Clear();
                nextPlacement.Clear();
                return PackagingResult::InvalidState;
            }
            const u32 previousPlacements = nextPlacement.placements.Size();
            nextPlacement.placements.PushBack({source.resources[resourceIndex].resource, assignedPackage});
            if (nextPlacement.placements.Size() != previousPlacements + 1u)
            {
                plan.Clear();
                nextPlacement.Clear();
                return PackagingResult::OutOfMemory;
            }
        }
        if (!plan.IsPrepared() || !nextPlacement.IsValid())
        {
            plan.Clear();
            nextPlacement.Clear();
            return PackagingResult::InvalidState;
        }
        return PackagingResult::Success;
    }

    PackagingResult PackageSetAssembler::Publish(const PackageSetBuildPlan& plan, const filesystem::AbsolutePath& outputDirectory,
                                                 const PackageAssemblyCallbacks& callbacks, const PackageSetPublicationLimits& limits) const noexcept
    {
        if (!plan.IsPrepared() || outputDirectory.Empty() || !outputDirectory.IsDirectoryPath() || callbacks.resolvePath == nullptr ||
            callbacks.readArtifact == nullptr || limits.hashBufferBytes == 0)
        {
            return PackagingResult::InvalidArgument;
        }

        filesystem::Manager& manager = filesystem::GetManager();
        containers::DynamicArray<StagedPackage> staged(memory::pools::Assets::GetInstance());
        staged.Resize(plan.packages.Size());
        if (staged.Size() != plan.packages.Size())
        {
            return PackagingResult::OutOfMemory;
        }
        for (u32 index = 0; index < plan.packages.Size(); ++index)
        {
            StagedPackage& stage = staged[index];
            stage.packageNumber = plan.packages[index].packageNumber;
            if (!BuildPackagePaths(outputDirectory, stage.packageNumber, stage.target, stage.temporary) || manager.FileExist(stage.target))
            {
                CleanupStagedPackages(staged, false);
                return PackagingResult::PublicationFailed;
            }
            if (manager.FileExist(stage.temporary) && !manager.DeleteFile(stage.temporary))
            {
                CleanupStagedPackages(staged, false);
                return PackagingResult::PublicationFailed;
            }
        }

        PackageAssembler assembler;
        for (u32 index = 1; index < plan.packages.Size(); ++index)
        {
            const PlannedDataPackage& planned = plan.packages[index];
            StagedPackage& stage = staged[index];
            PackageBuildPlan packagePlan;
            PackagingResult result = MakePackagePlan(plan, planned, packagePlan);
            if (result != PackagingResult::Success)
            {
                CleanupStagedPackages(staged, false);
                return result;
            }
            auto writer = manager.CreateFileWriter(stage.temporary, filesystem::FOF_Buffered);
            if (!writer)
            {
                CleanupStagedPackages(staged, false);
                return PackagingResult::IoFailure;
            }
            result = assembler.Assemble(packagePlan, *writer, callbacks, limits.package);
            writer.Reset();
            if (result == PackagingResult::Success)
            {
                result = ValidateAndDigestPackage(stage.temporary, planned, planned.buildId, limits, stage.catalogEntry);
            }
            if (result != PackagingResult::Success)
            {
                CleanupStagedPackages(staged, false);
                return result;
            }
        }

        containers::DynamicArray<packages::PackageSetEntry> catalog(memory::pools::Assets::GetInstance());
        catalog.Resize(plan.packages.Size() - 1u);
        if (catalog.Size() != plan.packages.Size() - 1u)
        {
            CleanupStagedPackages(staged, false);
            return PackagingResult::OutOfMemory;
        }
        for (u32 index = 1; index < plan.packages.Size(); ++index)
        {
            catalog[index - 1u] = staged[index].catalogEntry;
        }
        packages::PackageSetBuild packageSet;
        packageSet.gameId = plan.gameId;
        packageSet.targetPlatformId = plan.targetPlatformId;
        packageSet.startupWorld = plan.startupWorld.GetPath().Id();
        packageSet.startupWorldType = plan.startupWorld.ExpectedType();
        packageSet.defaultInput = plan.defaultInput.GetPath().Id();
        packageSet.defaultInputType = plan.defaultInput.ExpectedType();
        packageSet.rendererBootstrap = plan.rendererBootstrap.GetPath().Id();
        packageSet.rendererBootstrapType = plan.rendererBootstrap.ExpectedType();
        packageSet.packages = catalog;

        const PlannedDataPackage& rootPlanned = plan.packages[0];
        PackageBuildPlan rootPlan;
        PackagingResult result = MakePackagePlan(plan, rootPlanned, rootPlan);
        if (result == PackagingResult::Success)
        {
            auto rootWriter = manager.CreateFileWriter(staged[0].temporary, filesystem::FOF_Buffered);
            if (!rootWriter)
            {
                result = PackagingResult::IoFailure;
            }
            else
            {
                result = assembler.Assemble(rootPlan, *rootWriter, callbacks, packageSet, limits.package);
                rootWriter.Reset();
            }
        }
        if (result == PackagingResult::Success)
        {
            result = ValidateAndDigestPackage(staged[0].temporary, rootPlanned, plan.buildId, limits, staged[0].catalogEntry);
        }
        if (result != PackagingResult::Success)
        {
            CleanupStagedPackages(staged, false);
            return result;
        }

        for (u32 index = 1; index < staged.Size(); ++index)
        {
            if (!manager.MoveFile(staged[index].temporary, staged[index].target))
            {
                CleanupStagedPackages(staged, true);
                return PackagingResult::PublicationFailed;
            }
            staged[index].published = true;
        }
        if (!manager.MoveFile(staged[0].temporary, staged[0].target))
        {
            CleanupStagedPackages(staged, true);
            return PackagingResult::PublicationFailed;
        }
        staged[0].published = true;
        return PackagingResult::Success;
    }
} // namespace vanguard::assets
