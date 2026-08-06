#include <vanguard/assets/assets.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    constexpr u16 KnownArtifactFlags = static_cast<u16>(ArtifactFlags::Primary) | static_cast<u16>(ArtifactFlags::Streamable) |
                                       static_cast<u16>(ArtifactFlags::MemoryResident) | static_cast<u16>(ArtifactFlags::EditorOnly);

    void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        static_cast<void>(hash.Update(&value, sizeof(value)));
    }

    void HashU16(crypto::Sha256Builder& hash, const u16 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                            static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u),
                            static_cast<u8>(value >> 24u), static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                            static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashDigest(crypto::Sha256Builder& hash, const BuildFingerprint& digest) noexcept
    {
        static_cast<void>(hash.Update(digest.bytes, BuildFingerprint::ByteCount));
    }

    [[nodiscard]] bool DependencyLess(const BuildDependency& left, const BuildDependency& right) noexcept
    {
        if (left.role != right.role)
        {
            return left.role < right.role;
        }
        if (left.identity.Path() != right.identity.Path())
        {
            return left.identity.Path() < right.identity.Path();
        }
        if (left.identity.ExpectedType() != right.identity.ExpectedType())
        {
            return left.identity.ExpectedType() < right.identity.ExpectedType();
        }
        return left.requirement < right.requirement;
    }

    void SortDependencies(containers::DynamicArray<BuildDependency>& dependencies) noexcept
    {
        for (u32 index = 1; index < dependencies.Size(); ++index)
        {
            const BuildDependency value = dependencies[index];
            u32 insertion = index;
            while (insertion != 0 && DependencyLess(value, dependencies[insertion - 1u]))
            {
                dependencies[insertion] = dependencies[insertion - 1u];
                --insertion;
            }
            dependencies[insertion] = value;
        }
    }

    [[nodiscard]] bool CopyArtifacts(const containers::ArraySpan<const Artifact> source,
                                     containers::DynamicArray<Artifact>& destination) noexcept
    {
        destination.Clear();
        destination.Reserve(source.Count());
        if (destination.Capacity() < source.Count())
        {
            return false;
        }
        for (const Artifact& artifact : source)
        {
            const u32 previous = destination.Size();
            destination.PushBack(artifact);
            if (destination.Size() != previous + 1u)
            {
                destination.Clear();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] u64 ArtifactByteCount(const containers::ArraySpan<const Artifact> artifacts) noexcept
    {
        u64 bytes = 0;
        for (const Artifact& artifact : artifacts)
        {
            bytes += artifact.bytes.Size();
        }
        return bytes;
    }

    [[nodiscard]] BuildFingerprint ComputeBuildFingerprint(const BuildRequest& request, const CompilerDescriptor& compiler,
                                                           const containers::ArraySpan<const BuildDependency> dependencies) noexcept
    {
        constexpr char Domain[] = "vanguard.asset-build.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU64(hash, request.source.identity.Path().Id());
        HashU32(hash, request.source.identity.ExpectedType());
        HashU64(hash, request.output.Path().Id());
        HashU32(hash, request.output.ExpectedType());
        HashU64(hash, compiler.id);
        HashU32(hash, compiler.version);
        HashU8(hash, static_cast<u8>(request.target));
        HashDigest(hash, crypto::Sha256(request.source.content.Data(), request.source.content.SizeInBytes()));
        HashDigest(hash, crypto::Sha256(request.source.metadata.Data(), request.source.metadata.SizeInBytes()));
        HashDigest(hash, crypto::Sha256(request.settings.Data(), request.settings.SizeInBytes()));
        HashU32(hash, dependencies.Count());
        for (const BuildDependency& dependency : dependencies)
        {
            HashU8(hash, static_cast<u8>(dependency.role));
            HashU8(hash, static_cast<u8>(dependency.requirement));
            HashU64(hash, dependency.identity.Path().Id());
            HashU32(hash, dependency.identity.ExpectedType());
            HashDigest(hash, dependency.content);
        }
        BuildFingerprint result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    [[nodiscard]] BuildFingerprint ComputeArtifactFingerprint(const containers::ArraySpan<const Artifact> artifacts) noexcept
    {
        constexpr char Domain[] = "vanguard.artifact-set.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU32(hash, artifacts.Count());
        for (const Artifact& artifact : artifacts)
        {
            HashU64(hash, artifact.resource.Path().Id());
            HashU32(hash, artifact.resource.ExpectedType());
            HashU32(hash, artifact.segment);
            HashU16(hash, static_cast<u16>(artifact.flags));
            HashU8(hash, artifact.alignmentLog2);
            HashU64(hash, artifact.bytes.Size());
            static_cast<void>(hash.Update(artifact.bytes.Data(), artifact.bytes.Size()));
        }
        BuildFingerprint result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    constexpr u32 PersistentCacheMagic = vanguard::serialization::MakeFourCC('V', 'D', 'D', 'C');
    constexpr u16 PersistentCacheMajorVersion = 1;
    constexpr u16 PersistentCacheMinorVersion = 0;
    constexpr u32 PersistentCacheHeaderSize = 96;
    constexpr u32 PersistentArtifactDescriptorSize = 32;

    enum class PersistentLookupResult : u8
    {
        Hit,
        Miss,
        Corrupt,
        IoFailure,
        OutOfMemory
    };

    struct PersistentArtifactDescriptor
    {
        u64 path = 0;
        u32 type = 0;
        u32 segment = 0;
        u16 flags = 0;
        u8 alignmentLog2 = 0;
        u64 size = 0;
    };

    [[nodiscard]] char HexDigit(const u8 value) noexcept
    {
        return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
    }

    void EncodeDigest(const BuildFingerprint& fingerprint, char* const destination) noexcept
    {
        for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
        {
            destination[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
            destination[index * 2u + 1u] = HexDigit(fingerprint.bytes[index] & 0x0fu);
        }
    }

    [[nodiscard]] filesystem::AbsolutePath PersistentRecordDirectory(const filesystem::AbsolutePath& root,
                                                                     const BuildFingerprint& fingerprint) noexcept
    {
        char first[3] = {HexDigit(fingerprint.bytes[0] >> 4u), HexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {HexDigit(fingerprint.bytes[1] >> 4u), HexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        return root.AddDirPath(first).AddDirPath(second);
    }

    [[nodiscard]] filesystem::AbsolutePath PersistentRecordPath(const filesystem::AbsolutePath& root,
                                                                const BuildFingerprint& fingerprint) noexcept
    {
        char fileName[70];
        EncodeDigest(fingerprint, fileName);
        fileName[64] = '.';
        fileName[65] = 'v';
        fileName[66] = 'd';
        fileName[67] = 'd';
        fileName[68] = 'c';
        fileName[69] = '\0';
        return PersistentRecordDirectory(root, fingerprint).AddFilePath(fileName);
    }

    [[nodiscard]] bool WritePersistentRecord(const filesystem::AbsolutePath& path, const BuildOutput& output) noexcept
    {
        u64 payloadBytes = 0;
        for (const Artifact& artifact : output.artifacts)
        {
            if (artifact.bytes.Size() > ~u64{0} - payloadBytes)
            {
                return false;
            }
            payloadBytes += artifact.bytes.Size();
        }
        const u64 descriptorBytes = static_cast<u64>(output.artifacts.Size()) * PersistentArtifactDescriptorSize;
        if (payloadBytes > ~u64{0} - PersistentCacheHeaderSize - descriptorBytes)
        {
            return false;
        }
        const u64 fileSize = PersistentCacheHeaderSize + descriptorBytes + payloadBytes;

        auto file = filesystem::GetManager().CreateFileWriter(path, filesystem::FOF_Buffered);
        if (!file)
        {
            return false;
        }
        vanguard::serialization::BinaryWriter writer(*file);
        bool written = writer.WriteU32(PersistentCacheMagic) && writer.WriteU16(PersistentCacheMajorVersion) &&
                       writer.WriteU16(PersistentCacheMinorVersion) && writer.WriteU32(PersistentCacheHeaderSize) &&
                       writer.WriteU32(output.artifacts.Size()) && writer.WriteU64(fileSize) &&
                       writer.WriteBytes(output.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                       writer.WriteBytes(output.contentFingerprint.bytes, BuildFingerprint::ByteCount) && writer.WriteU64(payloadBytes);

        for (const Artifact& artifact : output.artifacts)
        {
            written = written && writer.WriteU64(artifact.resource.Path().Id()) && writer.WriteU32(artifact.resource.ExpectedType()) &&
                      writer.WriteU32(artifact.segment) && writer.WriteU16(static_cast<u16>(artifact.flags)) &&
                      writer.WriteU8(artifact.alignmentLog2) && writer.WriteU8(0) && writer.WriteU64(artifact.bytes.Size()) &&
                      writer.WriteU32(0);
        }
        for (const Artifact& artifact : output.artifacts)
        {
            written = written && writer.WriteBytes(artifact.bytes.Data(), artifact.bytes.Size());
        }
        written = written && writer.Position() == fileSize && writer.Flush();
        file.Reset();
        return written;
    }

    [[nodiscard]] PersistentLookupResult ReadPersistentRecord(const filesystem::AbsolutePath& path, const BuildFingerprint& expectedBuild,
                                                              const Config& config, BuildOutput& output) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.FileExist(path))
        {
            return PersistentLookupResult::Miss;
        }
        auto file = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!file)
        {
            return PersistentLookupResult::IoFailure;
        }
        vanguard::serialization::BinaryReader reader(*file);
        u32 magic = 0;
        u16 major = 0;
        u16 minor = 0;
        u32 headerSize = 0;
        u32 artifactCount = 0;
        u64 fileSize = 0;
        BuildFingerprint build;
        BuildFingerprint content;
        u64 payloadBytes = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) ||
            !reader.ReadU32(artifactCount) || !reader.ReadU64(fileSize) || !reader.ReadBytes(build.bytes, BuildFingerprint::ByteCount) ||
            !reader.ReadBytes(content.bytes, BuildFingerprint::ByteCount) || !reader.ReadU64(payloadBytes))
        {
            return PersistentLookupResult::Corrupt;
        }
        const u64 descriptorBytes = static_cast<u64>(artifactCount) * PersistentArtifactDescriptorSize;
        if (magic != PersistentCacheMagic || major != PersistentCacheMajorVersion || minor > PersistentCacheMinorVersion ||
            headerSize != PersistentCacheHeaderSize || build != expectedBuild || artifactCount == 0 ||
            artifactCount > config.maximumArtifactsPerBuild || payloadBytes > config.maximumArtifactBytesPerBuild ||
            fileSize != reader.Size() || fileSize < PersistentCacheHeaderSize || descriptorBytes > fileSize - PersistentCacheHeaderSize ||
            payloadBytes != fileSize - PersistentCacheHeaderSize - descriptorBytes)
        {
            return PersistentLookupResult::Corrupt;
        }

        containers::DynamicArray<PersistentArtifactDescriptor> descriptors(memory::pools::Assets::GetInstance());
        descriptors.Resize(artifactCount);
        if (descriptors.Size() != artifactCount)
        {
            return PersistentLookupResult::OutOfMemory;
        }
        u64 describedPayloadBytes = 0;
        bool hasPrimary = false;
        for (u32 index = 0; index < artifactCount; ++index)
        {
            PersistentArtifactDescriptor& descriptor = descriptors[index];
            u8 reserved8 = 0;
            u32 reserved32 = 0;
            if (!reader.ReadU64(descriptor.path) || !reader.ReadU32(descriptor.type) || !reader.ReadU32(descriptor.segment) ||
                !reader.ReadU16(descriptor.flags) || !reader.ReadU8(descriptor.alignmentLog2) || !reader.ReadU8(reserved8) ||
                !reader.ReadU64(descriptor.size) || !reader.ReadU32(reserved32) || descriptor.path == resources::InvalidResourceId ||
                descriptor.type == resources::InvalidResourceTypeId || descriptor.size == 0 || descriptor.alignmentLog2 > 20 ||
                reserved8 != 0 || reserved32 != 0 || (descriptor.flags & ~KnownArtifactFlags) != 0 ||
                descriptor.size > payloadBytes - describedPayloadBytes)
            {
                return PersistentLookupResult::Corrupt;
            }
            describedPayloadBytes += descriptor.size;
            hasPrimary = hasPrimary || (descriptor.flags & static_cast<u16>(ArtifactFlags::Primary)) != 0;
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (descriptors[previous].path == descriptor.path && descriptors[previous].type == descriptor.type &&
                    descriptors[previous].segment == descriptor.segment)
                {
                    return PersistentLookupResult::Corrupt;
                }
            }
        }
        if (!hasPrimary || describedPayloadBytes != payloadBytes)
        {
            return PersistentLookupResult::Corrupt;
        }

        output.Reset();
        output.artifacts.Resize(artifactCount);
        if (output.artifacts.Size() != artifactCount)
        {
            return PersistentLookupResult::OutOfMemory;
        }
        for (u32 index = 0; index < artifactCount; ++index)
        {
            const PersistentArtifactDescriptor& descriptor = descriptors[index];
            Artifact& artifact = output.artifacts[index];
            artifact.resource = resources::ResourceReference(resources::ResourcePath::FromId(descriptor.path), descriptor.type);
            artifact.segment = descriptor.segment;
            artifact.flags = static_cast<ArtifactFlags>(descriptor.flags);
            artifact.alignmentLog2 = descriptor.alignmentLog2;
            artifact.bytes.Resize(static_cast<u32>(descriptor.size));
            if (artifact.bytes.Size() != descriptor.size || !reader.ReadBytes(artifact.bytes.Data(), artifact.bytes.Size()))
            {
                output.Reset();
                return artifact.bytes.Size() != descriptor.size ? PersistentLookupResult::OutOfMemory : PersistentLookupResult::Corrupt;
            }
        }
        if (reader.Position() != fileSize || ComputeArtifactFingerprint({output.artifacts.TypedData(), output.artifacts.Size()}) != content)
        {
            output.Reset();
            return PersistentLookupResult::Corrupt;
        }
        output.disposition = BuildDisposition::CacheHit;
        output.buildFingerprint = build;
        output.contentFingerprint = content;
        return PersistentLookupResult::Hit;
    }
} // namespace

namespace vanguard::assets
{
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
        case Result::CompilerNotFound:
            return "CompilerNotFound";
        case Result::CompilerAlreadyRegistered:
            return "CompilerAlreadyRegistered";
        case Result::CompilerBusy:
            return "CompilerBusy";
        case Result::DependencyDiscoveryFailed:
            return "DependencyDiscoveryFailed";
        case Result::DuplicateDependency:
            return "DuplicateDependency";
        case Result::CompileFailed:
            return "CompileFailed";
        case Result::InvalidArtifact:
            return "InvalidArtifact";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::OutOfMemory:
            return "OutOfMemory";
        case Result::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    BuildPlan::BuildPlan() noexcept : m_dependencies(memory::pools::Assets::GetInstance()) {}

    void BuildPlan::Reset() noexcept
    {
        m_compiler = InvalidCompilerId;
        m_compilerVersion = 0;
        m_sourceType = resources::InvalidResourceTypeId;
        m_outputType = resources::InvalidResourceTypeId;
        m_dependencies.Clear();
    }

    bool BuildPlan::IsPrepared() const noexcept
    {
        return m_compiler != InvalidCompilerId && m_compilerVersion != 0 && m_sourceType != resources::InvalidResourceTypeId &&
               m_outputType != resources::InvalidResourceTypeId;
    }

    CompilerId BuildPlan::Compiler() const noexcept
    {
        return m_compiler;
    }

    u32 BuildPlan::CompilerVersion() const noexcept
    {
        return m_compilerVersion;
    }

    resources::ResourceTypeId BuildPlan::SourceType() const noexcept
    {
        return m_sourceType;
    }

    resources::ResourceTypeId BuildPlan::OutputType() const noexcept
    {
        return m_outputType;
    }

    containers::ArraySpan<const BuildDependency> BuildPlan::Dependencies() const noexcept
    {
        return {m_dependencies.TypedData(), m_dependencies.Size()};
    }

    Result BuildPlan::SetGeneratedDependencyContent(const resources::ResourceReference dependency, const BuildFingerprint& content) noexcept
    {
        if (!dependency.IsValid() || !dependency.IsTyped() || content.IsEmpty())
        {
            return Result::InvalidArgument;
        }
        for (BuildDependency& existing : m_dependencies)
        {
            if (existing.role == DependencyRole::Generated && existing.identity == dependency)
            {
                existing.content = content;
                return Result::Success;
            }
        }
        return Result::InvalidArgument;
    }

    DependencyCollector::DependencyCollector() noexcept : m_dependencies(memory::pools::Assets::GetInstance()) {}

    Result DependencyCollector::Add(const BuildDependency& dependency) noexcept
    {
        if (m_status != Result::Success)
        {
            return m_status;
        }
        if (!dependency.IsValid())
        {
            m_status = Result::InvalidArgument;
            return m_status;
        }
        for (const BuildDependency& existing : m_dependencies)
        {
            if (existing.role == dependency.role && existing.identity == dependency.identity)
            {
                m_status = Result::DuplicateDependency;
                return m_status;
            }
        }
        const u32 previous = m_dependencies.Size();
        m_dependencies.PushBack(dependency);
        if (m_dependencies.Size() != previous + 1u)
        {
            m_status = Result::OutOfMemory;
        }
        return m_status;
    }

    u32 DependencyCollector::Count() const noexcept
    {
        return m_dependencies.Size();
    }

    containers::ArraySpan<const BuildDependency> DependencyCollector::Dependencies() const noexcept
    {
        return {m_dependencies.TypedData(), m_dependencies.Size()};
    }

    Result DependencyCollector::Status() const noexcept
    {
        return m_status;
    }

    Artifact::Artifact() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

    ArtifactWriter::ArtifactWriter(const resources::ResourceReference primaryOutput, const u32 maximumArtifacts,
                                   const u64 maximumBytes) noexcept
        : m_primaryOutput(primaryOutput), m_artifacts(memory::pools::Assets::GetInstance()), m_maximumArtifacts(maximumArtifacts),
          m_maximumBytes(maximumBytes)
    {
    }

    Result ArtifactWriter::Add(const resources::ResourceReference resource, const u32 segment, const ArtifactFlags flags,
                               const u8 alignmentLog2, const void* const data, const usize size) noexcept
    {
        if (m_status != Result::Success)
        {
            return m_status;
        }
        const bool primary = HasFlag(flags, ArtifactFlags::Primary);
        if (!resource.IsValid() || !resource.IsTyped() || size == 0 || data == nullptr || size > static_cast<usize>(~u32{0}) ||
            alignmentLog2 > 20 || (static_cast<u16>(flags) & ~KnownArtifactFlags) != 0 || (primary && resource != m_primaryOutput))
        {
            m_status = Result::InvalidArtifact;
            return m_status;
        }
        if (m_artifacts.Size() >= m_maximumArtifacts || size > m_maximumBytes - m_bytes)
        {
            m_status = Result::LimitExceeded;
            return m_status;
        }
        for (const Artifact& existing : m_artifacts)
        {
            if (existing.resource == resource && existing.segment == segment)
            {
                m_status = Result::InvalidArtifact;
                return m_status;
            }
        }

        const u32 previous = m_artifacts.Size();
        m_artifacts.Resize(previous + 1u);
        if (m_artifacts.Size() != previous + 1u)
        {
            m_status = Result::OutOfMemory;
            return m_status;
        }
        Artifact& artifact = m_artifacts.Back();
        artifact.resource = resource;
        artifact.segment = segment;
        artifact.flags = flags;
        artifact.alignmentLog2 = alignmentLog2;
        artifact.bytes.Resize(static_cast<u32>(size));
        if (artifact.bytes.Size() != size)
        {
            static_cast<void>(m_artifacts.RemoveAt(previous));
            m_status = Result::OutOfMemory;
            return m_status;
        }
        const auto* const source = static_cast<const u8*>(data);
        for (u32 index = 0; index < artifact.bytes.Size(); ++index)
        {
            artifact.bytes[index] = source[index];
        }
        m_bytes += size;
        m_hasPrimary |= primary;
        return Result::Success;
    }

    u32 ArtifactWriter::Count() const noexcept
    {
        return m_artifacts.Size();
    }

    u64 ArtifactWriter::ByteCount() const noexcept
    {
        return m_bytes;
    }

    containers::ArraySpan<const Artifact> ArtifactWriter::Artifacts() const noexcept
    {
        return {m_artifacts.TypedData(), m_artifacts.Size()};
    }

    Result ArtifactWriter::Status() const noexcept
    {
        return m_status;
    }

    bool ArtifactWriter::HasPrimaryOutput() const noexcept
    {
        return m_hasPrimary;
    }

    CompilerId HashCompilerName(const containers::StringView name) noexcept
    {
        if (name.Empty())
        {
            return InvalidCompilerId;
        }
        constexpr u64 OffsetBasis = 14695981039346656037ull;
        constexpr u64 Prime = 1099511628211ull;
        u64 hash = OffsetBasis;
        bool previousDot = false;
        for (const char byte : name)
        {
            const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';
            if (!valid || (byte == '.' && previousDot))
            {
                return InvalidCompilerId;
            }
            hash ^= static_cast<u8>(byte);
            hash *= Prime;
            previousDot = byte == '.';
        }
        return previousDot || hash == InvalidCompilerId ? InvalidCompilerId : hash;
    }

    BuildOutput::BuildOutput() noexcept : artifacts(memory::pools::Assets::GetInstance()) {}

    void BuildOutput::Reset() noexcept
    {
        disposition = BuildDisposition::Built;
        buildFingerprint = {};
        contentFingerprint = {};
        artifacts.Clear();
    }

    struct BuildSystem::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        struct CompilerRecord
        {
            CompilerDescriptor descriptor;
            u32 activeBuilds = 0;
        };

        struct CacheEntry
        {
            CacheEntry() noexcept : artifacts(memory::pools::Assets::GetInstance()) {}

            BuildFingerprint key;
            BuildFingerprint content;
            containers::DynamicArray<Artifact> artifacts;
            u64 bytes = 0;
            u64 sequence = 0;
        };

        explicit Impl(const Config& value) noexcept
            : config(value), compilers(memory::pools::Assets::GetInstance()), cache(memory::pools::Assets::GetInstance())
        {
        }

        Config config;
        filesystem::AbsolutePath persistentRoot;
        mutable concurrency::RWSpinLock lock;
        containers::DynamicArray<CompilerRecord> compilers;
        containers::DynamicArray<CacheEntry> cache;
        Stats stats;
        u64 nextSequence = 1;

        [[nodiscard]] bool InitializePersistentCache() noexcept
        {
            if (config.persistentCacheRoot == nullptr)
            {
                return true;
            }
            persistentRoot = filesystem::AbsolutePath::ParseDirPath(config.persistentCacheRoot);
            config.persistentCacheRoot = nullptr;
            filesystem::Manager& manager = filesystem::GetManager();
            if (persistentRoot.Empty() || !manager.CreatePath(persistentRoot))
            {
                return false;
            }

            containers::DynamicArray<filesystem::AbsolutePath> staleFiles(memory::pools::Assets::GetInstance());
            manager.FindFiles(persistentRoot, containers::String("*.tmp"), staleFiles, true);
            u64 recovered = 0;
            for (const filesystem::AbsolutePath& stale : staleFiles)
            {
                if (manager.DeleteFile(stale))
                {
                    ++recovered;
                }
            }
            stats.persistentCacheRecoveries += recovered;
            return true;
        }

        [[nodiscard]] bool LookupPersistentCache(const BuildFingerprint& key, BuildOutput& output) noexcept
        {
            if (persistentRoot.Empty())
            {
                return false;
            }
            const BuildFingerprint requestedKey = key;
            const filesystem::AbsolutePath path = PersistentRecordPath(persistentRoot, requestedKey);
            const PersistentLookupResult result = ReadPersistentRecord(path, requestedKey, config, output);
            if (result != PersistentLookupResult::Hit)
            {
                output.Reset();
                output.buildFingerprint = requestedKey;
            }

            lock.Acquire();
            switch (result)
            {
            case PersistentLookupResult::Hit:
                ++stats.persistentCacheHits;
                break;
            case PersistentLookupResult::Miss:
                ++stats.persistentCacheMisses;
                break;
            case PersistentLookupResult::Corrupt:
                ++stats.persistentCacheCorruptions;
                break;
            case PersistentLookupResult::IoFailure:
            case PersistentLookupResult::OutOfMemory:
                ++stats.persistentCacheIoFailures;
                break;
            }
            lock.Release();

            if (result == PersistentLookupResult::Corrupt)
            {
                static_cast<void>(filesystem::GetManager().DeleteFile(path));
            }
            return result == PersistentLookupResult::Hit;
        }

        void StorePersistentCache(const BuildOutput& output) noexcept
        {
            if (persistentRoot.Empty())
            {
                return;
            }
            filesystem::Manager& manager = filesystem::GetManager();
            const filesystem::AbsolutePath directory = PersistentRecordDirectory(persistentRoot, output.buildFingerprint);
            const filesystem::AbsolutePath target = PersistentRecordPath(persistentRoot, output.buildFingerprint);
            if (!manager.CreatePath(directory))
            {
                RecordPersistentStore(false);
                return;
            }

            if (manager.FileExist(target))
            {
                BuildOutput existing;
                const PersistentLookupResult existingResult = ReadPersistentRecord(target, output.buildFingerprint, config, existing);
                if (existingResult == PersistentLookupResult::Hit && existing.contentFingerprint == output.contentFingerprint)
                {
                    return;
                }
                if (!manager.DeleteFile(target))
                {
                    RecordPersistentStore(false);
                    return;
                }
            }

            const filesystem::AbsolutePath generated = manager.GenerateTemporaryFilePath();
            const filesystem::AbsolutePath temporary = directory.AddFilePath(filesystem::paths::GetFileName(generated));
            if (!WritePersistentRecord(temporary, output))
            {
                static_cast<void>(manager.DeleteFile(temporary));
                RecordPersistentStore(false);
                return;
            }

            BuildOutput validation;
            const PersistentLookupResult validationResult = ReadPersistentRecord(temporary, output.buildFingerprint, config, validation);
            if (validationResult != PersistentLookupResult::Hit || validation.contentFingerprint != output.contentFingerprint)
            {
                static_cast<void>(manager.DeleteFile(temporary));
                RecordPersistentStore(false);
                return;
            }

            const bool moved = filesystem::SystemIO::MoveFile(temporary.AsChar(), target.AsChar());
            bool published = moved;
            if (!moved && manager.FileExist(target))
            {
                BuildOutput raced;
                published = ReadPersistentRecord(target, output.buildFingerprint, config, raced) == PersistentLookupResult::Hit &&
                            raced.contentFingerprint == output.contentFingerprint;
            }
            if (!moved)
            {
                static_cast<void>(manager.DeleteFile(temporary));
            }
            RecordPersistentStore(published);
        }

        void RecordPersistentStore(const bool success) noexcept
        {
            lock.Acquire();
            if (success)
            {
                ++stats.persistentCacheStores;
            }
            else
            {
                ++stats.persistentCacheIoFailures;
            }
            lock.Release();
        }

        [[nodiscard]] Result AcquireCompiler(const BuildRequest& request, CompilerDescriptor& descriptor) noexcept
        {
            lock.Acquire();
            for (CompilerRecord& compiler : compilers)
            {
                if (compiler.descriptor.sourceType == request.source.identity.ExpectedType() &&
                    compiler.descriptor.outputType == request.output.ExpectedType())
                {
                    ++compiler.activeBuilds;
                    ++stats.activeBuilds;
                    descriptor = compiler.descriptor;
                    lock.Release();
                    return Result::Success;
                }
            }
            lock.Release();
            return Result::CompilerNotFound;
        }

        void ReleaseCompiler(const CompilerId id) noexcept
        {
            lock.Acquire();
            for (CompilerRecord& compiler : compilers)
            {
                if (compiler.descriptor.id == id)
                {
                    if (compiler.activeBuilds != 0)
                    {
                        --compiler.activeBuilds;
                    }
                    break;
                }
            }
            if (stats.activeBuilds != 0)
            {
                --stats.activeBuilds;
            }
            lock.Release();
        }

        [[nodiscard]] bool LookupCache(const BuildFingerprint& key, BuildOutput& output) noexcept
        {
            lock.AcquireShared();
            for (const CacheEntry& entry : cache)
            {
                if (entry.key == key)
                {
                    output.buildFingerprint = entry.key;
                    output.contentFingerprint = entry.content;
                    output.disposition = BuildDisposition::CacheHit;
                    const bool copied = CopyArtifacts({entry.artifacts.TypedData(), entry.artifacts.Size()}, output.artifacts);
                    lock.ReleaseShared();
                    return copied;
                }
            }
            lock.ReleaseShared();
            return false;
        }

        void StoreCache(const BuildOutput& output) noexcept
        {
            const u64 bytes = ArtifactByteCount({output.artifacts.TypedData(), output.artifacts.Size()});
            if (config.maximumCacheEntries == 0 || bytes > config.maximumCacheBytes)
            {
                return;
            }

            lock.Acquire();
            for (CacheEntry& entry : cache)
            {
                if (entry.key == output.buildFingerprint)
                {
                    stats.cacheBytes -= entry.bytes;
                    entry.content = output.contentFingerprint;
                    entry.bytes = bytes;
                    static_cast<void>(CopyArtifacts({output.artifacts.TypedData(), output.artifacts.Size()}, entry.artifacts));
                    stats.cacheBytes += entry.bytes;
                    lock.Release();
                    return;
                }
            }

            while (!cache.Empty() && (cache.Size() >= config.maximumCacheEntries || bytes > config.maximumCacheBytes - stats.cacheBytes))
            {
                u32 oldest = 0;
                for (u32 index = 1; index < cache.Size(); ++index)
                {
                    if (cache[index].sequence < cache[oldest].sequence)
                    {
                        oldest = index;
                    }
                }
                stats.cacheBytes -= cache[oldest].bytes;
                static_cast<void>(cache.RemoveAt(oldest));
                ++stats.cacheEvictions;
            }

            const u32 previous = cache.Size();
            cache.Resize(previous + 1u);
            if (cache.Size() == previous + 1u)
            {
                CacheEntry& entry = cache.Back();
                entry.key = output.buildFingerprint;
                entry.content = output.contentFingerprint;
                entry.bytes = bytes;
                entry.sequence = nextSequence++;
                if (CopyArtifacts({output.artifacts.TypedData(), output.artifacts.Size()}, entry.artifacts))
                {
                    stats.cacheBytes += bytes;
                    ++stats.cacheStores;
                }
                else
                {
                    static_cast<void>(cache.RemoveAt(previous));
                }
            }
            stats.cacheEntries = cache.Size();
            lock.Release();
        }

        void RecordResult(const bool cacheHit, const bool success) noexcept
        {
            lock.Acquire();
            ++stats.buildRequests;
            if (success)
            {
                if (cacheHit)
                {
                    ++stats.cacheHits;
                }
                else
                {
                    ++stats.cacheMisses;
                    ++stats.localBuilds;
                }
            }
            else
            {
                ++stats.failedBuilds;
            }
            stats.cacheEntries = cache.Size();
            lock.Release();
        }
    };

    namespace
    {
        class CompilerLease final
        {
        public:
            CompilerLease(BuildSystem::Impl& implementation, const CompilerId compiler) noexcept
                : m_implementation(&implementation), m_compiler(compiler)
            {
            }

            ~CompilerLease()
            {
                m_implementation->ReleaseCompiler(m_compiler);
            }

            CompilerLease(const CompilerLease&) = delete;
            CompilerLease& operator=(const CompilerLease&) = delete;

        private:
            BuildSystem::Impl* m_implementation;
            CompilerId m_compiler;
        };
    } // namespace

    BuildSystem::~BuildSystem()
    {
        static_cast<void>(Shutdown());
    }

    bool BuildSystem::Initialize(const Config& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!memory::IsInitialized() || config.maximumCompilers == 0 || config.maximumDependenciesPerBuild == 0 ||
            config.maximumArtifactsPerBuild == 0 || config.maximumArtifactBytesPerBuild == 0 ||
            (config.maximumCacheEntries != 0 && config.maximumCacheBytes == 0) ||
            (config.persistentCacheRoot != nullptr && (!filesystem::IsInitialized() || config.persistentCacheRoot[0] == '\0' ||
                                                       !filesystem::AbsolutePath::IsValidPath(config.persistentCacheRoot) ||
                                                       !filesystem::paths::IsAbsolutePath(config.persistentCacheRoot))))
        {
            return false;
        }
        m_impl = VANGUARD_NEW(Impl)(config);
        if (m_impl == nullptr)
        {
            return false;
        }
        if (!m_impl->InitializePersistentCache())
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return false;
        }
        return true;
    }

    bool BuildSystem::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.AcquireShared();
        const bool busy = m_impl->stats.activeBuilds != 0;
        m_impl->lock.ReleaseShared();
        if (busy)
        {
            return false;
        }
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool BuildSystem::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    Result BuildSystem::RegisterCompiler(const CompilerDescriptor& compiler) noexcept
    {
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!compiler.IsValid() || HashCompilerName(compiler.name) != compiler.id)
        {
            return Result::InvalidArgument;
        }

        m_impl->lock.Acquire();
        if (m_impl->compilers.Size() >= m_impl->config.maximumCompilers)
        {
            m_impl->lock.Release();
            return Result::LimitExceeded;
        }
        for (const Impl::CompilerRecord& existing : m_impl->compilers)
        {
            if (existing.descriptor.id == compiler.id ||
                (existing.descriptor.sourceType == compiler.sourceType && existing.descriptor.outputType == compiler.outputType))
            {
                m_impl->lock.Release();
                return Result::CompilerAlreadyRegistered;
            }
        }
        const u32 previous = m_impl->compilers.Size();
        m_impl->compilers.PushBack({compiler, 0});
        if (m_impl->compilers.Size() != previous + 1u)
        {
            m_impl->lock.Release();
            return Result::OutOfMemory;
        }
        m_impl->stats.registeredCompilers = m_impl->compilers.Size();
        m_impl->lock.Release();
        return Result::Success;
    }

    Result BuildSystem::UnregisterCompiler(const CompilerId compiler) noexcept
    {
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        m_impl->lock.Acquire();
        for (u32 index = 0; index < m_impl->compilers.Size(); ++index)
        {
            const Impl::CompilerRecord& record = m_impl->compilers[index];
            if (record.descriptor.id == compiler)
            {
                if (record.activeBuilds != 0)
                {
                    m_impl->lock.Release();
                    return Result::CompilerBusy;
                }
                static_cast<void>(m_impl->compilers.RemoveAt(index));
                m_impl->stats.registeredCompilers = m_impl->compilers.Size();
                m_impl->lock.Release();
                return Result::Success;
            }
        }
        m_impl->lock.Release();
        return Result::CompilerNotFound;
    }

    Result BuildSystem::Build(const BuildRequest& request, BuildOutput& output) noexcept
    {
        output.Reset();
        BuildPlan plan;
        const Result prepared = Prepare(request, plan);
        if (prepared != Result::Success)
        {
            if (m_impl != nullptr)
            {
                m_impl->RecordResult(false, false);
            }
            return prepared;
        }
        return Execute(request, plan, output);
    }

    Result BuildSystem::Prepare(const BuildRequest& request, BuildPlan& plan) noexcept
    {
        plan.Reset();
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!request.IsValid())
        {
            return Result::InvalidArgument;
        }

        CompilerDescriptor compiler;
        Result result = m_impl->AcquireCompiler(request, compiler);
        if (result != Result::Success)
        {
            return result;
        }
        CompilerLease lease(*m_impl, compiler.id);

        DependencyCollector collector;
        if (!compiler.discoverDependencies(request, collector, compiler.userData))
        {
            return Result::DependencyDiscoveryFailed;
        }
        if (collector.Status() != Result::Success)
        {
            return collector.Status();
        }
        if (collector.Count() > m_impl->config.maximumDependenciesPerBuild)
        {
            return Result::LimitExceeded;
        }

        plan.m_dependencies.Reserve(collector.Count());
        if (plan.m_dependencies.Capacity() < collector.Count())
        {
            return Result::OutOfMemory;
        }
        for (const BuildDependency& dependency : collector.Dependencies())
        {
            plan.m_dependencies.PushBack(dependency);
        }
        SortDependencies(plan.m_dependencies);
        plan.m_compiler = compiler.id;
        plan.m_compilerVersion = compiler.version;
        plan.m_sourceType = compiler.sourceType;
        plan.m_outputType = compiler.outputType;
        return Result::Success;
    }

    Result BuildSystem::Execute(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output,
                                const IsCancellationRequestedFunction cancellation, void* const cancellationUserData) noexcept
    {
        output.Reset();
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!request.IsValid() || !plan.IsPrepared() || plan.m_sourceType != request.source.identity.ExpectedType() ||
            plan.m_outputType != request.output.ExpectedType())
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidArgument;
        }
        for (const BuildDependency& dependency : plan.m_dependencies)
        {
            if (!dependency.IsValid() || (dependency.role == DependencyRole::Generated &&
                                          dependency.requirement == DependencyRequirement::Required && dependency.content.IsEmpty()))
            {
                m_impl->RecordResult(false, false);
                return Result::InvalidState;
            }
        }

        CompilerDescriptor compiler;
        Result result = m_impl->AcquireCompiler(request, compiler);
        if (result != Result::Success)
        {
            m_impl->RecordResult(false, false);
            return result;
        }
        CompilerLease lease(*m_impl, compiler.id);
        if (compiler.id != plan.m_compiler || compiler.version != plan.m_compilerVersion)
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidState;
        }
        if (cancellation != nullptr && cancellation(cancellationUserData))
        {
            m_impl->RecordResult(false, false);
            return Result::Cancelled;
        }

        const containers::ArraySpan<const BuildDependency> sortedDependencies{plan.m_dependencies.TypedData(), plan.m_dependencies.Size()};
        output.buildFingerprint = ComputeBuildFingerprint(request, compiler, sortedDependencies);

        if (m_impl->LookupCache(output.buildFingerprint, output))
        {
            m_impl->RecordResult(true, true);
            return Result::Success;
        }
        if (m_impl->LookupPersistentCache(output.buildFingerprint, output))
        {
            m_impl->StoreCache(output);
            m_impl->RecordResult(true, true);
            return Result::Success;
        }

        ArtifactWriter writer(request.output, m_impl->config.maximumArtifactsPerBuild, m_impl->config.maximumArtifactBytesPerBuild);
        const CompileContext context{request, sortedDependencies, output.buildFingerprint, cancellation, cancellationUserData};
        if (!compiler.compile(context, writer, compiler.userData))
        {
            m_impl->RecordResult(false, false);
            return cancellation != nullptr && cancellation(cancellationUserData) ? Result::Cancelled : Result::CompileFailed;
        }
        if (cancellation != nullptr && cancellation(cancellationUserData))
        {
            m_impl->RecordResult(false, false);
            return Result::Cancelled;
        }
        if (writer.Status() != Result::Success)
        {
            m_impl->RecordResult(false, false);
            return writer.Status();
        }
        if (writer.Count() == 0 || !writer.HasPrimaryOutput())
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidArtifact;
        }
        if (!CopyArtifacts(writer.Artifacts(), output.artifacts))
        {
            m_impl->RecordResult(false, false);
            return Result::OutOfMemory;
        }
        output.disposition = BuildDisposition::Built;
        output.contentFingerprint = ComputeArtifactFingerprint({output.artifacts.TypedData(), output.artifacts.Size()});
        m_impl->StorePersistentCache(output);
        m_impl->StoreCache(output);
        m_impl->RecordResult(false, true);
        return Result::Success;
    }

    void BuildSystem::ClearCache() noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        m_impl->lock.Acquire();
        m_impl->cache.Clear();
        m_impl->stats.cacheEntries = 0;
        m_impl->stats.cacheBytes = 0;
        m_impl->lock.Release();
    }

    Stats BuildSystem::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        m_impl->lock.AcquireShared();
        const Stats stats = m_impl->stats;
        m_impl->lock.ReleaseShared();
        return stats;
    }
} // namespace vanguard::assets
