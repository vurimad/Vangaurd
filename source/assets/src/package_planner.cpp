#include <vanguard/assets/package_planner.hpp>

#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    constexpr u8 ManifestLittleEndian = 1;
    constexpr u8 ManifestEncoding = 1;
    constexpr u32 KnownManifestFlags = static_cast<u32>(PackageManifestFlags::IncludeGeneratedDependencies) |
                                       static_cast<u32>(PackageManifestFlags::IncludeOptionalDependencies) |
                                       static_cast<u32>(PackageManifestFlags::IncludeEditorArtifacts) | static_cast<u32>(PackageManifestFlags::HasDebugPaths);
    constexpr u8 KnownRootFlags = static_cast<u8>(PackageRootFlags::Startup) | static_cast<u8>(PackageRootFlags::Optional);

    struct PlannedResourceIdentity
    {
        resources::ResourceId path = resources::InvalidResourceId;
        resources::ResourceTypeId type = resources::InvalidResourceTypeId;

        [[nodiscard]] u32 CalcHash() const noexcept
        {
            u64 value = path;
            value ^= static_cast<u64>(type) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
            value ^= value >> 33u;
            value *= 0xff51afd7ed558ccdull;
            value ^= value >> 33u;
            return static_cast<u32>(value ^ (value >> 32u));
        }

        [[nodiscard]] friend constexpr bool operator==(const PlannedResourceIdentity&, const PlannedResourceIdentity&) noexcept = default;
    };

    [[nodiscard]] PlannedResourceIdentity Identity(const resources::ResourceReference resource) noexcept
    {
        return {resource.GetPath().Id(), resource.ExpectedType()};
    }

    [[nodiscard]] bool RootLess(const PackageRoot& left, const PackageRoot& right) noexcept
    {
        if (left.resource.GetPath() != right.resource.GetPath())
        {
            return left.resource.GetPath() < right.resource.GetPath();
        }
        return left.resource.ExpectedType() < right.resource.ExpectedType();
    }

    [[nodiscard]] bool ReferenceLess(const resources::ResourceReference left, const resources::ResourceReference right) noexcept
    {
        if (left.GetPath() != right.GetPath())
        {
            return left.GetPath() < right.GetPath();
        }
        return left.ExpectedType() < right.ExpectedType();
    }

    template <typename T, typename Less> void InsertionSort(containers::DynamicArray<T>& values, const Less& less) noexcept
    {
        for (u32 index = 1; index < values.Size(); ++index)
        {
            u32 current = index;
            while (current != 0 && less(values[current], values[current - 1]))
            {
                T temporary(static_cast<T&&>(values[current]));
                values[current] = static_cast<T&&>(values[current - 1]);
                values[current - 1] = static_cast<T&&>(temporary);
                --current;
            }
        }
    }

    [[nodiscard]] packages::ResourceFlags RootResourceFlags(const PackageManifest& manifest, const resources::ResourceReference resource) noexcept
    {
        packages::ResourceFlags flags = packages::ResourceFlags::None;
        for (const PackageRoot& root : manifest.roots)
        {
            if (root.resource != resource)
            {
                continue;
            }
            if (HasFlag(root.flags, PackageRootFlags::Startup))
            {
                flags = flags | packages::ResourceFlags::Startup;
            }
            if (HasFlag(root.flags, PackageRootFlags::Optional))
            {
                flags = flags | packages::ResourceFlags::Optional;
            }
        }
        return flags;
    }

    [[nodiscard]] PlannedPackageResource* FindPlanned(PackageBuildPlan& plan, const resources::ResourceReference resource) noexcept
    {
        for (PlannedPackageResource& existing : plan.resources)
        {
            if (existing.resource == resource)
            {
                return &existing;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool AddDependency(PlannedPackageResource& resource, const PlannedPackageDependency& dependency, const PackagePlanLimits& limits,
                                     u32& dependencyCount) noexcept
    {
        for (const PlannedPackageDependency& existing : resource.dependencies)
        {
            if (existing.resource == dependency.resource)
            {
                return existing.kind == dependency.kind;
            }
        }
        if (dependencyCount >= limits.maximumDependencies)
        {
            return false;
        }
        const u32 previous = resource.dependencies.Size();
        resource.dependencies.PushBack(dependency);
        if (resource.dependencies.Size() != previous + 1u)
        {
            return false;
        }
        ++dependencyCount;
        return true;
    }

    void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        static_cast<void>(hash.Update(&value, sizeof(value)));
    }

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                            static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    [[nodiscard]] u64 ComputeBuildId(const PackageBuildPlan& plan) noexcept
    {
        crypto::Sha256Builder hash;
        HashU64(hash, plan.packageId);
        HashU8(hash, static_cast<u8>(plan.target));
        HashU32(hash, plan.resources.Size());
        for (const PlannedPackageResource& resource : plan.resources)
        {
            HashU64(hash, resource.resource.GetPath().Id());
            HashU32(hash, resource.resource.ExpectedType());
            HashU32(hash, static_cast<u32>(resource.flags));
            static_cast<void>(hash.Update(resource.origin.build.bytes, BuildFingerprint::ByteCount));
            static_cast<void>(hash.Update(resource.origin.content.bytes, BuildFingerprint::ByteCount));
            HashU32(hash, resource.segments.Size());
            for (const PlannedPackageSegment& segment : resource.segments)
            {
                HashU32(hash, segment.artifact.segment);
                HashU32(hash, static_cast<u32>(segment.artifact.flags));
                HashU8(hash, segment.artifact.alignmentLog2);
                HashU64(hash, segment.artifact.byteCount);
                HashU8(hash, static_cast<u8>(segment.codec));
                HashU8(hash, static_cast<u8>(segment.flags));
            }
            HashU32(hash, resource.dependencies.Size());
            for (const PlannedPackageDependency& dependency : resource.dependencies)
            {
                HashU64(hash, dependency.resource.GetPath().Id());
                HashU32(hash, dependency.resource.ExpectedType());
                HashU8(hash, static_cast<u8>(dependency.kind));
            }
        }
        crypto::Digest256 digest;
        if (!hash.Finalize(digest))
        {
            return 0;
        }
        return static_cast<u64>(digest.bytes[0]) | (static_cast<u64>(digest.bytes[1]) << 8u) | (static_cast<u64>(digest.bytes[2]) << 16u) |
               (static_cast<u64>(digest.bytes[3]) << 24u) | (static_cast<u64>(digest.bytes[4]) << 32u) | (static_cast<u64>(digest.bytes[5]) << 40u) |
               (static_cast<u64>(digest.bytes[6]) << 48u) | (static_cast<u64>(digest.bytes[7]) << 56u);
    }
} // namespace

namespace vanguard::assets
{
    const char* ToString(const PackagingResult result) noexcept
    {
        switch (result)
        {
        case PackagingResult::Success:
            return "Success";
        case PackagingResult::InvalidArgument:
            return "InvalidArgument";
        case PackagingResult::InvalidState:
            return "InvalidState";
        case PackagingResult::InvalidMagic:
            return "InvalidMagic";
        case PackagingResult::UnsupportedVersion:
            return "UnsupportedVersion";
        case PackagingResult::CorruptManifest:
            return "CorruptManifest";
        case PackagingResult::DuplicateRoot:
            return "DuplicateRoot";
        case PackagingResult::MissingResource:
            return "MissingResource";
        case PackagingResult::TargetMismatch:
            return "TargetMismatch";
        case PackagingResult::MissingArtifact:
            return "MissingArtifact";
        case PackagingResult::DuplicateArtifact:
            return "DuplicateArtifact";
        case PackagingResult::ArtifactReadFailed:
            return "ArtifactReadFailed";
        case PackagingResult::InvalidArtifactData:
            return "InvalidArtifactData";
        case PackagingResult::PackageWriteFailed:
            return "PackageWriteFailed";
        case PackagingResult::ValidationFailed:
            return "ValidationFailed";
        case PackagingResult::PublicationFailed:
            return "PublicationFailed";
        case PackagingResult::RebalanceRequired:
            return "RebalanceRequired";
        case PackagingResult::IndexFailure:
            return "IndexFailure";
        case PackagingResult::OutOfMemory:
            return "OutOfMemory";
        case PackagingResult::LimitExceeded:
            return "LimitExceeded";
        case PackagingResult::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    PackageManifest::PackageManifest() noexcept : roots(memory::pools::Assets::GetInstance()) {}

    PackagingResult PackageManifest::AddRoot(const PackageRoot& root, const u32 maximumRoots) noexcept
    {
        if (!root.resource.IsValid() || !root.resource.IsTyped() || (static_cast<u8>(root.flags) & ~KnownRootFlags) != 0)
        {
            return PackagingResult::InvalidArgument;
        }
        for (const PackageRoot& existing : roots)
        {
            if (existing.resource == root.resource)
            {
                return PackagingResult::DuplicateRoot;
            }
            if (existing.resource.GetPath() == root.resource.GetPath())
            {
                return PackagingResult::InvalidArgument;
            }
        }
        if (roots.Size() >= maximumRoots)
        {
            return PackagingResult::LimitExceeded;
        }
        const u32 previous = roots.Size();
        roots.PushBack(root);
        if (roots.Size() != previous + 1u)
        {
            return PackagingResult::OutOfMemory;
        }
        InsertionSort(roots, RootLess);
        return PackagingResult::Success;
    }

    bool PackageManifest::IsValid(const PackageManifestLimits& limits) const noexcept
    {
        if (version != CurrentVersion || packageId == 0 || target >= TargetPlatform::Count || static_cast<u8>(codec) > static_cast<u8>(packages::Codec::Lz4) ||
            dataAlignmentLog2 > 20 || roots.Empty() || roots.Size() > limits.maximumRoots || (static_cast<u32>(flags) & ~KnownManifestFlags) != 0)
        {
            return false;
        }
        for (u32 index = 0; index < roots.Size(); ++index)
        {
            const PackageRoot& root = roots[index];
            if (!root.resource.IsValid() || !root.resource.IsTyped() || (static_cast<u8>(root.flags) & ~KnownRootFlags) != 0 ||
                (index != 0 && !RootLess(roots[index - 1], root)))
            {
                return false;
            }
        }
        const u64 payloadSize = static_cast<u64>(roots.Size()) * RootWireSize;
        return payloadSize <= limits.maximumBytes && HeaderWireSize <= limits.maximumBytes - payloadSize;
    }

    PackagingResult WritePackageManifest(filesystem::IFile& file, const PackageManifest& manifest, const PackageManifestLimits& limits) noexcept
    {
        if (!file.IsWriter() || file.GetOffset() != 0 || file.GetSize() != 0 || !manifest.IsValid(limits))
        {
            return PackagingResult::InvalidArgument;
        }
        const u64 payloadSize = static_cast<u64>(manifest.roots.Size()) * PackageManifest::RootWireSize;
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Reserve(static_cast<u32>(PackageManifest::HeaderWireSize + payloadSize));
        filesystem::MemoryFileWriter memoryFile(bytes);
        serialization::BinaryWriter writer(memoryFile);
        const bool headerWritten = writer.WriteU32(PackageManifestMagic) && writer.WriteU8(ManifestLittleEndian) && writer.WriteU8(ManifestEncoding) &&
                                   writer.WriteU16(PackageManifest::HeaderWireSize) && writer.WriteU16(manifest.version.major) &&
                                   writer.WriteU16(manifest.version.minor) && writer.WriteU32(static_cast<u32>(manifest.flags)) &&
                                   writer.WriteU8(static_cast<u8>(manifest.target)) && writer.WriteU8(static_cast<u8>(manifest.codec)) &&
                                   writer.WriteU8(manifest.dataAlignmentLog2) && writer.WriteU8(0) && writer.WriteU64(manifest.packageId) &&
                                   writer.WriteU32(manifest.roots.Size()) && writer.WriteU16(PackageManifest::RootWireSize) && writer.WriteU16(0) &&
                                   writer.WriteU64(payloadSize) && writer.WriteU64(0) && writer.WriteU32(0) && writer.WriteU64(0);
        if (!headerWritten || writer.Position() != PackageManifest::HeaderWireSize)
        {
            return PackagingResult::OutOfMemory;
        }
        for (const PackageRoot& root : manifest.roots)
        {
            if (!writer.WriteU64(root.resource.GetPath().Id()) || !writer.WriteU32(root.resource.ExpectedType()) || !writer.WriteU8(static_cast<u8>(root.flags)) ||
                !writer.WriteU8(0) || !writer.WriteU16(0))
            {
                return PackagingResult::OutOfMemory;
            }
        }
        const u64 payloadCrc = serialization::Crc64(bytes.TypedData() + PackageManifest::HeaderWireSize, static_cast<usize>(payloadSize));
        if (!writer.Seek(44) || !writer.WriteU64(payloadCrc))
        {
            return PackagingResult::IoFailure;
        }
        const u32 headerCrc = serialization::Crc32(bytes.TypedData(), 52);
        if (!writer.Seek(52) || !writer.WriteU32(headerCrc))
        {
            return PackagingResult::IoFailure;
        }
        serialization::BinaryWriter output(file);
        return output.WriteBytes(bytes.TypedData(), bytes.Size()) && output.Flush() ? PackagingResult::Success : PackagingResult::IoFailure;
    }

    PackagingResult ReadPackageManifest(filesystem::IFile& file, PackageManifest& manifest, const PackageManifestLimits& limits) noexcept
    {
        if (!file.IsReader() || file.GetOffset() != 0 || file.GetSize() < PackageManifest::HeaderWireSize || file.GetSize() > limits.maximumBytes ||
            file.GetSize() > static_cast<u64>(0xffffffffu))
        {
            return PackagingResult::InvalidArgument;
        }
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Resize(static_cast<u32>(file.GetSize()));
        if (bytes.Size() != file.GetSize())
        {
            return PackagingResult::OutOfMemory;
        }
        file.Serialize(bytes.TypedData(), bytes.Size());
        if (file.GetOffset() != file.GetSize())
        {
            return PackagingResult::IoFailure;
        }
        filesystem::MemoryFileReader memoryFile(bytes, 0);
        serialization::BinaryReader reader(memoryFile);
        u32 magic = 0;
        u8 byteOrder = 0;
        u8 encoding = 0;
        u16 headerSize = 0;
        PackageManifest decoded;
        u32 flags = 0;
        u8 target = 0;
        u8 codec = 0;
        u8 alignment = 0;
        u8 reserved8 = 0;
        u32 rootCount = 0;
        u16 rootSize = 0;
        u16 reserved16 = 0;
        u64 payloadSize = 0;
        u64 payloadCrc = 0;
        u32 headerCrc = 0;
        u64 reserved64 = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU8(byteOrder) || !reader.ReadU8(encoding) || !reader.ReadU16(headerSize) ||
            !reader.ReadU16(decoded.version.major) || !reader.ReadU16(decoded.version.minor) || !reader.ReadU32(flags) || !reader.ReadU8(target) ||
            !reader.ReadU8(codec) || !reader.ReadU8(alignment) || !reader.ReadU8(reserved8) || !reader.ReadU64(decoded.packageId) ||
            !reader.ReadU32(rootCount) || !reader.ReadU16(rootSize) || !reader.ReadU16(reserved16) || !reader.ReadU64(payloadSize) ||
            !reader.ReadU64(payloadCrc) || !reader.ReadU32(headerCrc) || !reader.ReadU64(reserved64))
        {
            return PackagingResult::IoFailure;
        }
        if (magic != PackageManifestMagic)
        {
            return PackagingResult::InvalidMagic;
        }
        if (decoded.version != PackageManifest::CurrentVersion)
        {
            return PackagingResult::UnsupportedVersion;
        }
        const u64 expectedPayload = static_cast<u64>(rootCount) * PackageManifest::RootWireSize;
        if (byteOrder != ManifestLittleEndian || encoding != ManifestEncoding || headerSize != PackageManifest::HeaderWireSize ||
            rootSize != PackageManifest::RootWireSize || rootCount == 0 || rootCount > limits.maximumRoots || payloadSize != expectedPayload ||
            PackageManifest::HeaderWireSize + payloadSize != bytes.Size() || reserved8 != 0 || reserved16 != 0 || reserved64 != 0 ||
            headerCrc != serialization::Crc32(bytes.TypedData(), 52) ||
            payloadCrc != serialization::Crc64(bytes.TypedData() + PackageManifest::HeaderWireSize, static_cast<usize>(payloadSize)))
        {
            return PackagingResult::CorruptManifest;
        }
        decoded.flags = static_cast<PackageManifestFlags>(flags);
        decoded.target = static_cast<TargetPlatform>(target);
        decoded.codec = static_cast<packages::Codec>(codec);
        decoded.dataAlignmentLog2 = alignment;
        PackageRoot previousRoot;
        bool hasPreviousRoot = false;
        for (u32 index = 0; index < rootCount; ++index)
        {
            u64 id = 0;
            u32 type = 0;
            u8 rootFlags = 0;
            u8 rootReserved8 = 0;
            u16 rootReserved16 = 0;
            if (!reader.ReadU64(id) || !reader.ReadU32(type) || !reader.ReadU8(rootFlags) || !reader.ReadU8(rootReserved8) || !reader.ReadU16(rootReserved16) ||
                rootReserved8 != 0 || rootReserved16 != 0)
            {
                return PackagingResult::CorruptManifest;
            }
            const PackageRoot root{resources::ResourceReference(resources::ResourcePath::FromId(id), type), static_cast<PackageRootFlags>(rootFlags)};
            if (hasPreviousRoot && !RootLess(previousRoot, root))
            {
                return PackagingResult::CorruptManifest;
            }
            const PackagingResult added = decoded.AddRoot(root, limits.maximumRoots);
            if (added != PackagingResult::Success)
            {
                return PackagingResult::CorruptManifest;
            }
            previousRoot = root;
            hasPreviousRoot = true;
        }
        if (!decoded.IsValid(limits))
        {
            return PackagingResult::CorruptManifest;
        }
        manifest = static_cast<PackageManifest&&>(decoded);
        return PackagingResult::Success;
    }

    PlannedPackageResource::PlannedPackageResource() noexcept
        : segments(memory::pools::Assets::GetInstance()), dependencies(memory::pools::Assets::GetInstance())
    {
    }

    PackageBuildPlan::PackageBuildPlan() noexcept : resources(memory::pools::Assets::GetInstance()) {}

    void PackageBuildPlan::Clear() noexcept
    {
        packageId = 0;
        buildId = 0;
        target = TargetPlatform::WindowsD3D12;
        options = {};
        resources.Clear();
    }

    bool PackageBuildPlan::IsPrepared() const noexcept
    {
        if (packageId == 0 || buildId == 0 || resources.Empty() || options.packageId != packageId || options.buildId != buildId)
        {
            return false;
        }
        for (const PlannedPackageResource& resource : resources)
        {
            if (!resource.resource.IsValid() || !resource.resource.IsTyped() || !resource.origin.IsValid() || resource.origin.content.IsEmpty() ||
                resource.segments.Empty())
            {
                return false;
            }
        }
        return true;
    }

    PackagingResult PackagePlanner::Prepare(const PackageManifest& manifest, const DependencyIndex& index, PackageBuildPlan& plan,
                                            const PackagePlanLimits& limits) const noexcept
    {
        plan.Clear();
        if (!manifest.IsValid() || !index.IsInitialized() || limits.maximumResources == 0 || limits.maximumSegments == 0)
        {
            return PackagingResult::InvalidArgument;
        }

        struct PendingResource
        {
            resources::ResourceReference resource;
            bool optional = false;
        };

        containers::DynamicArray<PendingResource> pending(memory::pools::Assets::GetInstance());
        containers::HashMap<resources::ResourceId, u8> queued(memory::pools::Assets::GetInstance());
        containers::HashMap<PlannedResourceIdentity, u8> artifactAudiences(memory::pools::Assets::GetInstance());
        for (const PackageRoot& root : manifest.roots)
        {
            if (!queued.Insert(root.resource.GetPath().Id(), 1).IsSuccessful())
            {
                return PackagingResult::OutOfMemory;
            }
            const u32 previous = pending.Size();
            pending.PushBack({root.resource, HasFlag(root.flags, PackageRootFlags::Optional)});
            if (pending.Size() != previous + 1u)
            {
                return PackagingResult::OutOfMemory;
            }
        }

        u32 segmentCount = 0;
        u32 dependencyCount = 0;
        for (u32 pendingIndex = 0; pendingIndex < pending.Size(); ++pendingIndex)
        {
            const PendingResource current = pending[pendingIndex];

            DependencyRecord record;
            const IndexResult found = index.Find(current.resource, record);
            if (found == IndexResult::NotFound && current.optional)
            {
                continue;
            }
            if (found == IndexResult::NotFound)
            {
                plan.Clear();
                return PackagingResult::MissingResource;
            }
            if (found != IndexResult::Success)
            {
                plan.Clear();
                return PackagingResult::IndexFailure;
            }
            if (record.output != current.resource)
            {
                plan.Clear();
                return PackagingResult::IndexFailure;
            }
            if (record.target != manifest.target)
            {
                plan.Clear();
                return PackagingResult::TargetMismatch;
            }

            containers::DynamicArray<PlannedPackageDependency> runtimeDependencies(memory::pools::Assets::GetInstance());
            for (const BuildDependency& dependency : record.dependencies)
            {
                if (dependency.role != DependencyRole::Generated)
                {
                    continue;
                }
                const resources::DependencyKind kind =
                    dependency.requirement == DependencyRequirement::Required
                        ? resources::DependencyKind::Required
                        : (dependency.requirement == DependencyRequirement::Optional ? resources::DependencyKind::Optional
                                                                                     : resources::DependencyKind::Soft);
                const u32 previousRuntimeDependencies = runtimeDependencies.Size();
                runtimeDependencies.PushBack({dependency.identity, kind});
                if (runtimeDependencies.Size() != previousRuntimeDependencies + 1u)
                {
                    plan.Clear();
                    return PackagingResult::OutOfMemory;
                }
                const bool includeDependency =
                    HasFlag(manifest.flags, PackageManifestFlags::IncludeGeneratedDependencies) &&
                    (dependency.requirement == DependencyRequirement::Required ||
                     (dependency.requirement == DependencyRequirement::Optional &&
                      HasFlag(manifest.flags, PackageManifestFlags::IncludeOptionalDependencies)));
                u8 alreadyQueued = 0;
                if (includeDependency && !queued.Find(dependency.identity.GetPath().Id(), alreadyQueued))
                {
                    if (pending.Size() >= limits.maximumResources || !queued.Insert(dependency.identity.GetPath().Id(), 1).IsSuccessful())
                    {
                        plan.Clear();
                        return pending.Size() >= limits.maximumResources ? PackagingResult::LimitExceeded : PackagingResult::OutOfMemory;
                    }
                    const u32 previousPending = pending.Size();
                    pending.PushBack({dependency.identity, dependency.requirement == DependencyRequirement::Optional});
                    if (pending.Size() != previousPending + 1u)
                    {
                        plan.Clear();
                        return PackagingResult::OutOfMemory;
                    }
                }
            }

            u32 selectedArtifacts = 0;
            for (const IndexedArtifact& artifact : record.artifacts)
            {
                const u8 audience = HasFlag(artifact.flags, ArtifactFlags::EditorOnly) ? 2u : 1u;
                u8* const knownAudience = artifactAudiences.FindPtr(Identity(artifact.resource));
                if (knownAudience == nullptr)
                {
                    if (!artifactAudiences.Insert(Identity(artifact.resource), audience).IsSuccessful())
                    {
                        plan.Clear();
                        return PackagingResult::OutOfMemory;
                    }
                }
                else if ((*knownAudience | audience) == 3u)
                {
                    plan.Clear();
                    return PackagingResult::InvalidArtifactData;
                }

                if (HasFlag(artifact.flags, ArtifactFlags::EditorOnly) && !HasFlag(manifest.flags, PackageManifestFlags::IncludeEditorArtifacts))
                {
                    continue;
                }
                const ArtifactSetKey origin{record.buildFingerprint, record.contentFingerprint};
                PlannedPackageResource* resource = FindPlanned(plan, artifact.resource);
                if (resource == nullptr)
                {
                    if (plan.resources.Size() >= limits.maximumResources)
                    {
                        plan.Clear();
                        return PackagingResult::LimitExceeded;
                    }
                    PlannedPackageResource created;
                    created.resource = artifact.resource;
                    created.flags = RootResourceFlags(manifest, record.output);
                    created.origin = origin;
                    const u32 previousResources = plan.resources.Size();
                    plan.resources.PushBack(static_cast<PlannedPackageResource&&>(created));
                    if (plan.resources.Size() != previousResources + 1u)
                    {
                        plan.Clear();
                        return PackagingResult::OutOfMemory;
                    }
                    resource = &plan.resources.Back();
                }
                else if (resource->origin != origin)
                {
                    plan.Clear();
                    return PackagingResult::InvalidArtifactData;
                }
                for (const PlannedPackageSegment& existing : resource->segments)
                {
                    if (existing.artifact.segment == artifact.segment)
                    {
                        plan.Clear();
                        return PackagingResult::DuplicateArtifact;
                    }
                }
                if (segmentCount >= limits.maximumSegments)
                {
                    plan.Clear();
                    return PackagingResult::LimitExceeded;
                }

                packages::SegmentFlags segmentFlags = packages::SegmentFlags::None;
                if (HasFlag(artifact.flags, ArtifactFlags::Streamable))
                {
                    segmentFlags = segmentFlags | packages::SegmentFlags::Streamable;
                    resource->flags = resource->flags | packages::ResourceFlags::Streamable;
                }
                else
                {
                    segmentFlags = segmentFlags | packages::SegmentFlags::Inline;
                }
                if (HasFlag(artifact.flags, ArtifactFlags::MemoryResident))
                {
                    segmentFlags = segmentFlags | packages::SegmentFlags::MemoryResident;
                }
                const u32 previousSegments = resource->segments.Size();
                resource->segments.PushBack({artifact, manifest.codec, segmentFlags});
                if (resource->segments.Size() != previousSegments + 1u)
                {
                    plan.Clear();
                    return PackagingResult::OutOfMemory;
                }
                ++segmentCount;
                ++selectedArtifacts;

                for (const PlannedPackageDependency& dependency : runtimeDependencies)
                {
                    if (!AddDependency(*resource, dependency, limits, dependencyCount))
                    {
                        plan.Clear();
                        return dependencyCount >= limits.maximumDependencies ? PackagingResult::LimitExceeded : PackagingResult::OutOfMemory;
                    }
                }
            }
            if (selectedArtifacts == 0 && !current.optional)
            {
                plan.Clear();
                return PackagingResult::MissingArtifact;
            }
        }

        if (plan.resources.Empty())
        {
            return PackagingResult::MissingArtifact;
        }
        for (PlannedPackageResource& resource : plan.resources)
        {
            bool allEditorOnly = true;
            for (const PlannedPackageSegment& segment : resource.segments)
            {
                allEditorOnly = allEditorOnly && HasFlag(segment.artifact.flags, ArtifactFlags::EditorOnly);
            }
            if (allEditorOnly)
            {
                resource.flags = resource.flags | packages::ResourceFlags::EditorOnly;
            }
            InsertionSort(resource.segments,
                          [](const PlannedPackageSegment& left, const PlannedPackageSegment& right) { return left.artifact.segment < right.artifact.segment; });
            InsertionSort(resource.dependencies, [](const PlannedPackageDependency& left, const PlannedPackageDependency& right)
                          { return ReferenceLess(left.resource, right.resource); });
        }
        InsertionSort(plan.resources,
                      [](const PlannedPackageResource& left, const PlannedPackageResource& right) { return ReferenceLess(left.resource, right.resource); });

        plan.packageId = manifest.packageId;
        plan.target = manifest.target;
        plan.options.flags = packages::PackageFlags::Deterministic;
        if (HasFlag(manifest.flags, PackageManifestFlags::HasDebugPaths))
        {
            plan.options.flags = plan.options.flags | packages::PackageFlags::HasDebugPaths;
        }
        plan.options.packageId = manifest.packageId;
        plan.options.dataAlignmentLog2 = manifest.dataAlignmentLog2;
        plan.buildId = ComputeBuildId(plan);
        plan.options.buildId = plan.buildId;
        if (plan.buildId == 0)
        {
            plan.Clear();
            return PackagingResult::InvalidState;
        }
        return PackagingResult::Success;
    }

    PackagingResult PackageAssembler::Assemble(const PackageBuildPlan& plan, filesystem::IFile& output, const PackageAssemblyCallbacks& callbacks,
                                               const PackageAssemblyLimits& limits) const noexcept
    {
        return AssembleInternal(plan, output, callbacks, nullptr, limits);
    }

    PackagingResult PackageAssembler::Assemble(const PackageBuildPlan& plan, filesystem::IFile& output, const PackageAssemblyCallbacks& callbacks,
                                               const packages::PackageSetBuild& packageSet, const PackageAssemblyLimits& limits) const noexcept
    {
        return AssembleInternal(plan, output, callbacks, &packageSet, limits);
    }

    PackagingResult PackageAssembler::AssembleInternal(const PackageBuildPlan& plan, filesystem::IFile& output, const PackageAssemblyCallbacks& callbacks,
                                                       const packages::PackageSetBuild* const packageSet, const PackageAssemblyLimits& limits) const noexcept
    {
        if (!plan.IsPrepared() || !output.IsWriter() || output.GetOffset() != 0 || output.GetSize() != 0 || callbacks.resolvePath == nullptr ||
            callbacks.readArtifact == nullptr || limits.maximumResourceBytes == 0)
        {
            return PackagingResult::InvalidArgument;
        }

        class Payload final
        {
        public:
            Payload() noexcept : bytes(memory::pools::Assets::GetInstance()) {}
            containers::DynamicArray<u8> bytes;
        };

        packages::PackageWriter writer;
        const packages::Result beginResult = packageSet != nullptr ? writer.Begin(output, plan.options, *packageSet) : writer.Begin(output, plan.options);
        if (beginResult != packages::Result::Success)
        {
            return PackagingResult::PackageWriteFailed;
        }

        for (const PlannedPackageResource& resource : plan.resources)
        {
            char path[packages::MaximumResourcePathBytes];
            usize pathSize = 0;
            if (!callbacks.resolvePath(resource.resource, path, sizeof(path), pathSize, callbacks.resolvePathUserData) || pathSize == 0 ||
                pathSize > sizeof(path))
            {
                writer.Reset();
                return PackagingResult::InvalidArgument;
            }

            containers::DynamicArray<Payload> payloads(memory::pools::Assets::GetInstance());
            containers::DynamicArray<packages::BuildSegment> segments(memory::pools::Assets::GetInstance());
            containers::DynamicArray<packages::Dependency> dependencies(memory::pools::Assets::GetInstance());
            payloads.Resize(resource.segments.Size());
            segments.Resize(resource.segments.Size());
            dependencies.Resize(resource.dependencies.Size());
            if (payloads.Size() != resource.segments.Size() || segments.Size() != resource.segments.Size() ||
                dependencies.Size() != resource.dependencies.Size())
            {
                writer.Reset();
                return PackagingResult::OutOfMemory;
            }

            u64 resourceBytes = 0;
            for (u32 index = 0; index < resource.segments.Size(); ++index)
            {
                const PlannedPackageSegment& planned = resource.segments[index];
                Payload& payload = payloads[index];
                if (!callbacks.readArtifact(resource.origin, planned.artifact, payload.bytes, callbacks.readArtifactUserData))
                {
                    writer.Reset();
                    return PackagingResult::ArtifactReadFailed;
                }
                if (payload.bytes.Size() != planned.artifact.byteCount || payload.bytes.Size() > limits.maximumResourceBytes ||
                    resourceBytes > limits.maximumResourceBytes - payload.bytes.Size())
                {
                    writer.Reset();
                    return PackagingResult::InvalidArtifactData;
                }
                resourceBytes += payload.bytes.Size();
                segments[index] = {payload.bytes.TypedData(), payload.bytes.Size(), planned.codec, planned.artifact.alignmentLog2, planned.flags};
            }

            for (u32 index = 0; index < resource.dependencies.Size(); ++index)
            {
                const PlannedPackageDependency& planned = resource.dependencies[index];
                dependencies[index] = {planned.resource.GetPath().Id(), planned.resource.ExpectedType(), planned.kind};
            }

            const packages::BuildResource build{{path, pathSize}, resource.resource.ExpectedType(), resource.flags, segments, dependencies};
            if (packages::HashResourcePath(build.path) != resource.resource.GetPath().Id() || writer.Add(build) != packages::Result::Success)
            {
                writer.Reset();
                return PackagingResult::PackageWriteFailed;
            }
        }
        return writer.Finalize() == packages::Result::Success ? PackagingResult::Success : PackagingResult::PackageWriteFailed;
    }

    PackagingResult PackageAssembler::Publish(const PackageBuildPlan& plan, const filesystem::AbsolutePath& target, const filesystem::AbsolutePath& temporary,
                                              const PackageAssemblyCallbacks& callbacks, const PackageAssemblyLimits& limits) const noexcept
    {
        if (!plan.IsPrepared() || target.Empty() || temporary.Empty() || !target.IsFilePath() || !temporary.IsFilePath() || target == temporary)
        {
            return PackagingResult::InvalidArgument;
        }
        filesystem::Manager& manager = filesystem::GetManager();
        if (manager.FileExist(temporary) && !manager.DeleteFile(temporary))
        {
            return PackagingResult::PublicationFailed;
        }

        auto writer = manager.CreateFileWriter(temporary, filesystem::FOF_Buffered);
        if (!writer)
        {
            return PackagingResult::IoFailure;
        }
        const PackagingResult assembled = Assemble(plan, *writer, callbacks, limits);
        writer.Reset();
        if (assembled != PackagingResult::Success)
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return assembled;
        }

        auto reader = manager.CreateFileReader(temporary, filesystem::FOF_Buffered);
        if (!reader)
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return PackagingResult::IoFailure;
        }
        packages::PackageReader package;
        const packages::Result opened = package.Open(*reader, limits.validation);
        const bool valid = opened == packages::Result::Success && package.GetHeader().packageId == plan.packageId && package.GetHeader().buildId == plan.buildId &&
                           package.GetHeader().resourceCount == plan.resources.Size();
        package.Close();
        reader.Reset();
        if (!valid)
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return PackagingResult::ValidationFailed;
        }
        if (!filesystem::ReplaceFile(temporary, target))
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return PackagingResult::PublicationFailed;
        }
        return PackagingResult::Success;
    }
} // namespace vanguard::assets
