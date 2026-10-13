#include <vanguard/assets/asset_index.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    constexpr u32 IndexMagic = vanguard::serialization::MakeFourCC('V', 'A', 'D', 'I');
    // The current index contract preserves Required/Optional/Soft exactly.
    // Development indexes with any other format version are disposable.
    constexpr u16 IndexMajorVersion = 2;
    constexpr u16 IndexMinorVersion = 1;
    constexpr u32 IndexHeaderSize = 128;
    constexpr char IndexFileName[] = "asset-dependencies.vadi";
    constexpr char TemporaryFileName[] = "asset-dependencies.vadi.tmp";

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

    class BufferWriter final
    {
    public:
        explicit BufferWriter(const u64 limit) noexcept : m_bytes(memory::pools::Assets::GetInstance()), m_limit(limit) {}

        [[nodiscard]] bool WriteU8(const u8 value) noexcept
        {
            return WriteBytes(&value, sizeof(value));
        }

        [[nodiscard]] bool WriteU16(const u16 value) noexcept
        {
            const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteU32(const u32 value) noexcept
        {
            const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteU64(const u64 value) noexcept
        {
            const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                                static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteBytes(const void* const data, const u32 size) noexcept
        {
            if ((size != 0 && data == nullptr) || static_cast<u64>(m_bytes.Size()) + size > m_limit || static_cast<u64>(m_bytes.Size()) + size > ~u32{0})
            {
                return false;
            }
            const u32 previous = m_bytes.Size();
            m_bytes.Resize(previous + size);
            if (m_bytes.Size() != previous + size)
            {
                return false;
            }
            const auto* source = static_cast<const u8*>(data);
            for (u32 index = 0; index < size; ++index)
            {
                m_bytes[previous + index] = source[index];
            }
            return true;
        }

        [[nodiscard]] bool PatchBytes(const u32 offset, const void* const data, const u32 size) noexcept
        {
            if ((size != 0 && data == nullptr) || offset > m_bytes.Size() || size > m_bytes.Size() - offset)
            {
                return false;
            }
            const auto* source = static_cast<const u8*>(data);
            for (u32 index = 0; index < size; ++index)
            {
                m_bytes[offset + index] = source[index];
            }
            return true;
        }

        [[nodiscard]] const containers::DynamicArray<u8>& Bytes() const noexcept
        {
            return m_bytes;
        }

    private:
        containers::DynamicArray<u8> m_bytes;
        u64 m_limit = 0;
    };

    class BufferReader final
    {
    public:
        BufferReader(const u8* const data, const u32 size) noexcept : m_data(data), m_size(size) {}

        [[nodiscard]] bool ReadU8(u8& value) noexcept
        {
            return ReadBytes(&value, sizeof(value));
        }

        [[nodiscard]] bool ReadU16(u16& value) noexcept
        {
            u8 bytes[2];
            if (!ReadBytes(bytes, sizeof(bytes)))
            {
                return false;
            }
            value = static_cast<u16>(bytes[0]) | static_cast<u16>(bytes[1] << 8u);
            return true;
        }

        [[nodiscard]] bool ReadU32(u32& value) noexcept
        {
            u8 bytes[4];
            if (!ReadBytes(bytes, sizeof(bytes)))
            {
                return false;
            }
            value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) | (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
            return true;
        }

        [[nodiscard]] bool ReadU64(u64& value) noexcept
        {
            u8 bytes[8];
            if (!ReadBytes(bytes, sizeof(bytes)))
            {
                return false;
            }
            value = static_cast<u64>(bytes[0]) | (static_cast<u64>(bytes[1]) << 8u) | (static_cast<u64>(bytes[2]) << 16u) |
                    (static_cast<u64>(bytes[3]) << 24u) | (static_cast<u64>(bytes[4]) << 32u) | (static_cast<u64>(bytes[5]) << 40u) |
                    (static_cast<u64>(bytes[6]) << 48u) | (static_cast<u64>(bytes[7]) << 56u);
            return true;
        }

        [[nodiscard]] bool ReadBytes(void* const data, const u32 size) noexcept
        {
            if ((size != 0 && data == nullptr) || m_position > m_size || size > m_size - m_position)
            {
                return false;
            }
            auto* destination = static_cast<u8*>(data);
            for (u32 index = 0; index < size; ++index)
            {
                destination[index] = m_data[m_position + index];
            }
            m_position += size;
            return true;
        }

        [[nodiscard]] bool Skip(const u32 size) noexcept
        {
            if (m_position > m_size || size > m_size - m_position)
            {
                return false;
            }
            m_position += size;
            return true;
        }

        [[nodiscard]] u32 Position() const noexcept
        {
            return m_position;
        }

        [[nodiscard]] u32 Size() const noexcept
        {
            return m_size;
        }

    private:
        const u8* m_data = nullptr;
        u32 m_size = 0;
        u32 m_position = 0;
    };

    [[nodiscard]] bool LessReference(const resources::ResourceReference left, const resources::ResourceReference right) noexcept
    {
        return left.GetPath().Id() < right.GetPath().Id() || (left.GetPath().Id() == right.GetPath().Id() && left.ExpectedType() < right.ExpectedType());
    }

    [[nodiscard]] bool ContainsReference(const containers::DynamicArray<resources::ResourceReference>& values,
                                         const resources::ResourceReference value) noexcept
    {
        for (const resources::ResourceReference existing : values)
        {
            if (existing == value)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool CopyRecord(const DependencyRecord& source, DependencyRecord& destination) noexcept
    {
        destination.source = source.source;
        destination.output = source.output;
        destination.target = source.target;
        destination.compiler = source.compiler;
        destination.compilerVersion = source.compilerVersion;
        destination.sourceInputFingerprint = source.sourceInputFingerprint;
        destination.buildFingerprint = source.buildFingerprint;
        destination.contentFingerprint = source.contentFingerprint;
        destination.dependencies = source.dependencies;
        destination.artifacts = source.artifacts;
        return destination.dependencies.Size() == source.dependencies.Size() && destination.artifacts.Size() == source.artifacts.Size();
    }
} // namespace

namespace vanguard::assets
{
    struct ReverseEdge
    {
        resources::ResourceReference input;
        resources::ResourceReference output;
    };

    struct DependencyIndex::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        explicit Impl(const DependencyIndexConfig& value) noexcept
            : config(value), root(filesystem::AbsolutePath::ParseDirPath(value.root)), path(root.AddFilePath(IndexFileName)),
              temporaryPath(root.AddFilePath(TemporaryFileName)), records(memory::pools::Assets::GetInstance()), lookup(memory::pools::Assets::GetInstance()),
              reverseEdges(memory::pools::Assets::GetInstance()), stagedRecords(memory::pools::Assets::GetInstance()),
              stagedLookup(memory::pools::Assets::GetInstance()), stagedRemovals(memory::pools::Assets::GetInstance()),
              stagedRemovalLookup(memory::pools::Assets::GetInstance()), stagedRetirementSuccessors(memory::pools::Assets::GetInstance()),
              retirementBatches(memory::pools::Assets::GetInstance()),
              retiredRecords(memory::pools::Assets::GetInstance()), retirementSuccessors(memory::pools::Assets::GetInstance())
        {
            config.root = nullptr;
        }

        ~Impl()
        {
            ClearRecordsLocked();
            ClearStagedLocked();
            ClearRetirementProtectionLocked();
        }

        DependencyIndexConfig config;
        filesystem::AbsolutePath root;
        filesystem::AbsolutePath path;
        filesystem::AbsolutePath temporaryPath;
        mutable concurrency::Mutex lock;
        containers::DynamicArray<DependencyRecord*> records;
        containers::HashMap<resources::ResourceId, u32> lookup;
        mutable containers::DynamicArray<ReverseEdge> reverseEdges;
        containers::DynamicArray<DependencyRecord*> stagedRecords;
        containers::HashMap<resources::ResourceId, u32> stagedLookup;
        containers::DynamicArray<resources::ResourceReference> stagedRemovals;
        containers::HashMap<resources::ResourceId, u32> stagedRemovalLookup;
        containers::DynamicArray<resources::ResourceReference> stagedRetirementSuccessors;
        containers::DynamicArray<DependencyRetirementProtectionBatch> retirementBatches;
        containers::DynamicArray<DependencyRecord> retiredRecords;
        containers::DynamicArray<DependencyPublicationWitness> retirementSuccessors;
        DependencyIndexStats stats;
        u64 generation = 0;
        u64 stagedPublications = 0;
        u64 stagedReplacements = 0;
        BuildSystem* persistentProtectionDomain = nullptr;
        BuildSystem* persistentProtectionDomainReservation = nullptr;
        u32 activePersistentPublications = 0u;
        bool changed = false;
        mutable bool reverseDirty = true;
        bool transactionActive = false;

        void ClearStagedLocked() noexcept
        {
            for (DependencyRecord* const record : stagedRecords)
            {
                if (record != nullptr)
                {
                    VANGUARD_DELETE(record);
                }
            }
            stagedRecords.Clear();
            stagedLookup.Clear();
            stagedRemovals.Clear();
            stagedRemovalLookup.Clear();
            stagedRetirementSuccessors.Clear();
            stagedPublications = 0;
            stagedReplacements = 0;
        }

        void ClearRecordsLocked() noexcept
        {
            for (DependencyRecord* const record : records)
            {
                VANGUARD_DELETE(record);
            }
            records.Clear();
            lookup.Clear();
            reverseEdges.Clear();
            stats.records = 0;
            stats.dependencies = 0;
            stats.artifacts = 0;
            reverseDirty = true;
        }

        void ClearRetirementProtectionLocked() noexcept
        {
            retirementBatches.Clear();
            retiredRecords.Clear();
            retirementSuccessors.Clear();
        }

        [[nodiscard]] bool RebuildLookupLocked() noexcept
        {
            lookup.Clear();
            for (u32 index = 0; index < records.Size(); ++index)
            {
                if (!lookup.Insert(records[index]->output.GetPath().Id(), index).IsSuccessful())
                {
                    lookup.Clear();
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] DependencyRecord* FindLocked(const resources::ResourceReference output) noexcept
        {
            u32 index = 0;
            if (!lookup.Find(output.GetPath().Id(), index) || index >= records.Size() || records[index]->output != output)
            {
                return nullptr;
            }
            return records[index];
        }

        [[nodiscard]] const DependencyRecord* FindLocked(const resources::ResourceReference output) const noexcept
        {
            u32 index = 0;
            if (!lookup.Find(output.GetPath().Id(), index) || index >= records.Size() || records[index]->output != output)
            {
                return nullptr;
            }
            return records[index];
        }

        void UpdateCountsLocked() noexcept
        {
            stats.records = records.Size();
            stats.dependencies = 0;
            stats.artifacts = 0;
            for (const DependencyRecord* const record : records)
            {
                stats.dependencies += record->dependencies.Size();
                stats.artifacts += record->artifacts.Size();
            }
        }

        [[nodiscard]] bool RebuildReverseLocked() const noexcept
        {
            if (!reverseDirty)
            {
                return true;
            }
            reverseEdges.Clear();
            for (const DependencyRecord* const record : records)
            {
                const u32 sourcePrevious = reverseEdges.Size();
                reverseEdges.PushBack({record->source, record->output});
                if (reverseEdges.Size() != sourcePrevious + 1u)
                {
                    reverseEdges.Clear();
                    return false;
                }
                for (const BuildDependency& dependency : record->dependencies)
                {
                    if (dependency.requirement == DependencyRequirement::Soft)
                    {
                        continue;
                    }
                    bool duplicate = dependency.identity == record->source;
                    for (const BuildDependency& previous : record->dependencies)
                    {
                        if (&previous == &dependency)
                        {
                            break;
                        }
                        duplicate = duplicate || previous.identity == dependency.identity;
                    }
                    if (duplicate)
                    {
                        continue;
                    }
                    const u32 previousSize = reverseEdges.Size();
                    reverseEdges.PushBack({dependency.identity, record->output});
                    if (reverseEdges.Size() != previousSize + 1u)
                    {
                        reverseEdges.Clear();
                        return false;
                    }
                }
            }
            for (u32 index = 1; index < reverseEdges.Size(); ++index)
            {
                ReverseEdge value = reverseEdges[index];
                u32 position = index;
                while (position != 0)
                {
                    const ReverseEdge& previous = reverseEdges[position - 1u];
                    const bool less =
                        LessReference(value.input, previous.input) || (value.input == previous.input && LessReference(value.output, previous.output));
                    if (!less)
                    {
                        break;
                    }
                    reverseEdges[position] = previous;
                    --position;
                }
                reverseEdges[position] = value;
            }
            reverseDirty = false;
            return true;
        }
    };

    DependencyRecord::DependencyRecord() noexcept : dependencies(memory::pools::Assets::GetInstance()), artifacts(memory::pools::Assets::GetInstance()) {}

    const char* ToString(const IndexResult result) noexcept
    {
        switch (result)
        {
        case IndexResult::Success:
            return "Success";
        case IndexResult::Recovered:
            return "Recovered";
        case IndexResult::NotFound:
            return "NotFound";
        case IndexResult::InvalidArgument:
            return "InvalidArgument";
        case IndexResult::InvalidState:
            return "InvalidState";
        case IndexResult::Incompatible:
            return "Incompatible";
        case IndexResult::Corrupt:
            return "Corrupt";
        case IndexResult::IoFailure:
            return "IoFailure";
        case IndexResult::OutOfMemory:
            return "OutOfMemory";
        case IndexResult::LimitExceeded:
            return "LimitExceeded";
        }
        return "Unknown";
    }

    BuildFingerprint ComputeSourceInputFingerprint(const BuildRequest& request) noexcept
    {
        constexpr char Domain[] = "vanguard.asset-source-input.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU64(hash, request.source.identity.GetPath().Id());
        HashU32(hash, request.source.identity.ExpectedType());
        HashU64(hash, request.output.GetPath().Id());
        HashU32(hash, request.output.ExpectedType());
        HashU8(hash, static_cast<u8>(request.target));
        const BuildFingerprint content = crypto::Sha256(request.source.content.Data(), request.source.content.SizeInBytes());
        const BuildFingerprint metadata = crypto::Sha256(request.source.metadata.Data(), request.source.metadata.SizeInBytes());
        const BuildFingerprint settings = crypto::Sha256(request.settings.Data(), request.settings.SizeInBytes());
        static_cast<void>(hash.Update(content.bytes, BuildFingerprint::ByteCount));
        static_cast<void>(hash.Update(metadata.bytes, BuildFingerprint::ByteCount));
        static_cast<void>(hash.Update(settings.bytes, BuildFingerprint::ByteCount));
        BuildFingerprint result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    namespace
    {
        [[nodiscard]] bool ReadFile(const filesystem::AbsolutePath& path, const u64 maximumBytes, containers::DynamicArray<u8>& bytes) noexcept
        {
            filesystem::Manager& manager = filesystem::GetManager();
            const u64 size = manager.GetFileSize(path);
            if (size < IndexHeaderSize || size > maximumBytes || size > ~u32{0})
            {
                return false;
            }
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
            {
                return false;
            }
            auto reader = manager.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
            {
                bytes.Clear();
                return false;
            }
            reader->Serialize(bytes.Data(), bytes.Size());
            const bool valid = !reader->HasErrors();
            reader.Reset();
            if (!valid)
            {
                bytes.Clear();
            }
            return valid;
        }

        [[nodiscard]] bool WriteDependency(BufferWriter& writer, const BuildDependency& dependency) noexcept
        {
            return writer.WriteU64(dependency.identity.GetPath().Id()) && writer.WriteU32(dependency.identity.ExpectedType()) &&
                   writer.WriteBytes(dependency.content.bytes, BuildFingerprint::ByteCount) && writer.WriteU8(static_cast<u8>(dependency.role)) &&
                   writer.WriteU8(static_cast<u8>(dependency.requirement)) && writer.WriteU16(0);
        }

        [[nodiscard]] bool WriteArtifact(BufferWriter& writer, const IndexedArtifact& artifact) noexcept
        {
            return writer.WriteU64(artifact.resource.GetPath().Id()) && writer.WriteU32(artifact.resource.ExpectedType()) && writer.WriteU32(artifact.segment) &&
                   writer.WriteU16(static_cast<u16>(artifact.flags)) && writer.WriteU8(artifact.alignmentLog2) && writer.WriteU8(0) &&
                   writer.WriteU64(artifact.byteCount) && writer.WriteU32(0);
        }

        [[nodiscard]] bool WriteRecord(BufferWriter& writer, const DependencyRecord& record) noexcept
        {
            return writer.WriteU64(record.source.GetPath().Id()) && writer.WriteU32(record.source.ExpectedType()) && writer.WriteU64(record.output.GetPath().Id()) &&
                   writer.WriteU32(record.output.ExpectedType()) && writer.WriteU8(static_cast<u8>(record.target)) && writer.WriteU8(0) && writer.WriteU16(0) &&
                   writer.WriteU64(record.compiler) && writer.WriteU32(record.compilerVersion) &&
                   writer.WriteBytes(record.sourceInputFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteBytes(record.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteBytes(record.contentFingerprint.bytes, BuildFingerprint::ByteCount) && writer.WriteU32(record.dependencies.Size()) &&
                   writer.WriteU32(record.artifacts.Size());
        }

        [[nodiscard]] bool WritePublicationWitness(BufferWriter& writer, const DependencyPublicationWitness& witness) noexcept
        {
            return writer.WriteU64(witness.output.GetPath().Id()) && writer.WriteU32(witness.output.ExpectedType()) &&
                   writer.WriteBytes(witness.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteBytes(witness.contentFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteU8(witness.predecessorPresent ? 1u : 0u) && writer.WriteU8(0u) && writer.WriteU16(0u);
        }

        [[nodiscard]] IndexResult ReadRecord(BufferReader& reader, const DependencyIndexConfig& config,
                                             DependencyRecord& record, u32& dependencyCount, u32& artifactCount) noexcept
        {
            dependencyCount = 0u;
            artifactCount = 0u;
            u64 sourcePath = 0u;
            u32 sourceType = 0u;
            u64 outputPath = 0u;
            u32 outputType = 0u;
            u8 target = 0u;
            u8 reserved8 = 0u;
            u16 reserved16 = 0u;
            const bool fixedRead = reader.ReadU64(sourcePath) && reader.ReadU32(sourceType) && reader.ReadU64(outputPath) &&
                                   reader.ReadU32(outputType) && reader.ReadU8(target) && reader.ReadU8(reserved8) &&
                                   reader.ReadU16(reserved16) && reader.ReadU64(record.compiler) && reader.ReadU32(record.compilerVersion) &&
                                   reader.ReadBytes(record.sourceInputFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                   reader.ReadBytes(record.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                   reader.ReadBytes(record.contentFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                   reader.ReadU32(dependencyCount) && reader.ReadU32(artifactCount);
            record.source = resources::ResourceReference(resources::ResourcePath::FromId(sourcePath), sourceType);
            record.output = resources::ResourceReference(resources::ResourcePath::FromId(outputPath), outputType);
            record.target = static_cast<TargetPlatform>(target);
            if (!fixedRead || reserved8 != 0u || reserved16 != 0u || !record.source.IsValid() || !record.source.IsTyped() ||
                !record.output.IsValid() || !record.output.IsTyped() || record.target >= TargetPlatform::Count ||
                record.compiler == InvalidCompilerId || record.compilerVersion == 0u ||
                dependencyCount > config.maximumDependenciesPerRecord || artifactCount > config.maximumArtifactsPerRecord)
                return IndexResult::Corrupt;

            record.dependencies.Resize(dependencyCount);
            record.artifacts.Resize(artifactCount);
            if (record.dependencies.Size() != dependencyCount || record.artifacts.Size() != artifactCount)
                return IndexResult::OutOfMemory;

            bool valid = true;
            for (BuildDependency& dependency : record.dependencies)
            {
                u64 path = 0u;
                u32 type = 0u;
                u8 role = 0u;
                u8 requirement = 0u;
                reserved16 = 0u;
                valid = valid && reader.ReadU64(path) && reader.ReadU32(type) &&
                        reader.ReadBytes(dependency.content.bytes, BuildFingerprint::ByteCount) && reader.ReadU8(role) &&
                        reader.ReadU8(requirement) && reader.ReadU16(reserved16);
                dependency.identity = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
                dependency.role = static_cast<DependencyRole>(role);
                dependency.requirement = static_cast<DependencyRequirement>(requirement);
                valid = valid && reserved16 == 0u && dependency.IsValid();
            }
            for (IndexedArtifact& artifact : record.artifacts)
            {
                u64 path = 0u;
                u32 type = 0u;
                u16 flags = 0u;
                reserved8 = 0u;
                u32 reserved32 = 0u;
                valid = valid && reader.ReadU64(path) && reader.ReadU32(type) && reader.ReadU32(artifact.segment) &&
                        reader.ReadU16(flags) && reader.ReadU8(artifact.alignmentLog2) && reader.ReadU8(reserved8) &&
                        reader.ReadU64(artifact.byteCount) && reader.ReadU32(reserved32);
                artifact.resource = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
                artifact.flags = static_cast<ArtifactFlags>(flags);
                valid = valid && artifact.resource.IsValid() && artifact.resource.IsTyped() && reserved8 == 0u &&
                        reserved32 == 0u && artifact.alignmentLog2 <= 63;
            }
            return valid ? IndexResult::Success : IndexResult::Corrupt;
        }

        [[nodiscard]] bool ReadPublicationWitness(BufferReader& reader, DependencyPublicationWitness& witness) noexcept
        {
            u64 outputPath = 0u;
            u32 outputType = 0u;
            u8 predecessorPresent = 0u;
            u8 reserved8 = 0u;
            u16 reserved16 = 0u;
            if (!reader.ReadU64(outputPath) || !reader.ReadU32(outputType) ||
                !reader.ReadBytes(witness.buildFingerprint.bytes, BuildFingerprint::ByteCount) ||
                !reader.ReadBytes(witness.contentFingerprint.bytes, BuildFingerprint::ByteCount) ||
                !reader.ReadU8(predecessorPresent) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) ||
                predecessorPresent > 1u || reserved8 != 0u || reserved16 != 0u)
                return false;
            witness.output = resources::ResourceReference(resources::ResourcePath::FromId(outputPath), outputType);
            witness.predecessorPresent = predecessorPresent != 0u;
            return witness.IsValid();
        }

        [[nodiscard]] IndexResult BuildSerializedIndex(const DependencyIndex::Impl& impl, BufferWriter& writer) noexcept
        {
            u64 dependencyCount = 0;
            u64 artifactCount = 0;
            containers::DynamicArray<u32> order(memory::pools::Assets::GetInstance());
            order.Resize(impl.records.Size());
            if (order.Size() != impl.records.Size())
            {
                return IndexResult::OutOfMemory;
            }
            for (u32 index = 0; index < order.Size(); ++index)
            {
                order[index] = index;
                dependencyCount += impl.records[index]->dependencies.Size();
                artifactCount += impl.records[index]->artifacts.Size();
            }
            for (const DependencyRecord& record : impl.retiredRecords)
            {
                dependencyCount += record.dependencies.Size();
                artifactCount += record.artifacts.Size();
            }
            if (dependencyCount > ~u32{0} || artifactCount > ~u32{0})
            {
                return IndexResult::LimitExceeded;
            }
            for (u32 index = 1; index < order.Size(); ++index)
            {
                const u32 value = order[index];
                u32 position = index;
                while (position != 0 && LessReference(impl.records[value]->output, impl.records[order[position - 1u]]->output))
                {
                    order[position] = order[position - 1u];
                    --position;
                }
                order[position] = value;
            }

            BuildFingerprint empty;
            bool written = writer.WriteU32(IndexMagic) && writer.WriteU16(IndexMajorVersion) && writer.WriteU16(IndexMinorVersion) &&
                           writer.WriteU32(IndexHeaderSize) && writer.WriteU32(impl.records.Size()) && writer.WriteU32(static_cast<u32>(dependencyCount)) &&
                           writer.WriteU32(static_cast<u32>(artifactCount)) && writer.WriteU32(0) && writer.WriteU64(0) &&
                           writer.WriteU64(impl.generation + 1u) && writer.WriteBytes(impl.config.settingsFingerprint.bytes, BuildFingerprint::ByteCount) &&
                           writer.WriteBytes(empty.bytes, BuildFingerprint::ByteCount) && writer.WriteU32(impl.retirementBatches.Size()) &&
                           writer.WriteU32(impl.retiredRecords.Size()) && writer.WriteU32(impl.retirementSuccessors.Size()) && writer.WriteU64(0u);
            for (const u32 index : order)
            {
                const DependencyRecord& record = *impl.records[index];
                written = written && WriteRecord(writer, record);
                for (const BuildDependency& dependency : record.dependencies)
                {
                    written = written && WriteDependency(writer, dependency);
                }
                for (const IndexedArtifact& artifact : record.artifacts)
                {
                    written = written && WriteArtifact(writer, artifact);
                }
            }
            u32 expectedRecordOffset = 0u;
            u32 expectedSuccessorOffset = 0u;
            for (const DependencyRetirementProtectionBatch& batch : impl.retirementBatches)
            {
                if (batch.successorCount == 0u || batch.recordOffset != expectedRecordOffset ||
                    batch.successorOffset != expectedSuccessorOffset || batch.recordOffset > impl.retiredRecords.Size() ||
                    batch.recordCount > impl.retiredRecords.Size() - batch.recordOffset ||
                    batch.successorOffset > impl.retirementSuccessors.Size() ||
                    batch.successorCount > impl.retirementSuccessors.Size() - batch.successorOffset)
                    return IndexResult::InvalidState;
                written = written && writer.WriteU32(batch.recordCount) && writer.WriteU32(batch.successorCount);
                for (u32 index = 0u; index < batch.recordCount; ++index)
                {
                    const DependencyRecord& record = impl.retiredRecords[batch.recordOffset + index];
                    written = written && WriteRecord(writer, record);
                    for (const BuildDependency& dependency : record.dependencies)
                        written = written && WriteDependency(writer, dependency);
                    for (const IndexedArtifact& artifact : record.artifacts)
                        written = written && WriteArtifact(writer, artifact);
                }
                for (u32 index = 0u; index < batch.successorCount; ++index)
                {
                    const DependencyPublicationWitness& witness = impl.retirementSuccessors[batch.successorOffset + index];
                    if (!witness.IsValid())
                        return IndexResult::InvalidState;
                    written = written && WritePublicationWitness(writer, witness);
                }
                expectedRecordOffset += batch.recordCount;
                expectedSuccessorOffset += batch.successorCount;
            }
            if (expectedRecordOffset != impl.retiredRecords.Size() || expectedSuccessorOffset != impl.retirementSuccessors.Size())
                return IndexResult::InvalidState;
            if (!written)
            {
                return IndexResult::LimitExceeded;
            }

            const containers::DynamicArray<u8>& bytes = writer.Bytes();
            const u64 fileSize = bytes.Size();
            const u8 sizeBytes[] = {static_cast<u8>(fileSize),        static_cast<u8>(fileSize >> 8u),  static_cast<u8>(fileSize >> 16u),
                                    static_cast<u8>(fileSize >> 24u), static_cast<u8>(fileSize >> 32u), static_cast<u8>(fileSize >> 40u),
                                    static_cast<u8>(fileSize >> 48u), static_cast<u8>(fileSize >> 56u)};
            const BuildFingerprint payload = crypto::Sha256(bytes.TypedData() + IndexHeaderSize, bytes.Size() - IndexHeaderSize);
            return writer.PatchBytes(28, sizeBytes, sizeof(sizeBytes)) && writer.PatchBytes(76, payload.bytes, BuildFingerprint::ByteCount)
                       ? IndexResult::Success
                       : IndexResult::InvalidState;
        }

        [[nodiscard]] IndexResult ParseIndex(DependencyIndex::Impl& impl, const containers::DynamicArray<u8>& bytes) noexcept
        {
            BufferReader reader(bytes.TypedData(), bytes.Size());
            u32 magic = 0;
            u16 major = 0;
            u16 minor = 0;
            u32 headerSize = 0;
            u32 recordCount = 0;
            u32 dependencyCount = 0;
            u32 artifactCount = 0;
            u32 reserved32 = 0;
            u64 fileSize = 0;
            u64 generation = 0;
            BuildFingerprint settings;
            BuildFingerprint payload;
            u32 retirementBatchCount = 0u;
            u32 retiredRecordCount = 0u;
            u32 retirementSuccessorCount = 0u;
            u64 reserved64 = 0u;
            if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) || !reader.ReadU32(recordCount) ||
                !reader.ReadU32(dependencyCount) || !reader.ReadU32(artifactCount) || !reader.ReadU32(reserved32) || !reader.ReadU64(fileSize) ||
                !reader.ReadU64(generation) || !reader.ReadBytes(settings.bytes, BuildFingerprint::ByteCount) ||
                !reader.ReadBytes(payload.bytes, BuildFingerprint::ByteCount) || !reader.ReadU32(retirementBatchCount) ||
                !reader.ReadU32(retiredRecordCount) || !reader.ReadU32(retirementSuccessorCount) || !reader.ReadU64(reserved64))
            {
                return IndexResult::Corrupt;
            }
            if (magic != IndexMagic || major != IndexMajorVersion || minor > IndexMinorVersion || settings != impl.config.settingsFingerprint)
            {
                return IndexResult::Incompatible;
            }
            const u64 totalRecordCount = static_cast<u64>(recordCount) + retiredRecordCount;
            if (headerSize != IndexHeaderSize || reserved32 != 0u || reserved64 != 0u || fileSize != bytes.Size() ||
                (minor == 0u && (retirementBatchCount != 0u || retiredRecordCount != 0u || retirementSuccessorCount != 0u)) ||
                recordCount > impl.config.maximumRecords || retiredRecordCount > impl.config.maximumRecords ||
                totalRecordCount > impl.config.maximumRecords || retirementBatchCount > impl.config.maximumRecords ||
                retirementSuccessorCount > impl.config.maximumRecords ||
                dependencyCount > totalRecordCount * impl.config.maximumDependenciesPerRecord ||
                artifactCount > totalRecordCount * impl.config.maximumArtifactsPerRecord ||
                crypto::Sha256(bytes.TypedData() + IndexHeaderSize, bytes.Size() - IndexHeaderSize) != payload)
            {
                return IndexResult::Corrupt;
            }

            u64 parsedDependencies = 0;
            u64 parsedArtifacts = 0;
            for (u32 recordIndex = 0; recordIndex < recordCount; ++recordIndex)
            {
                auto* const record = VANGUARD_NEW(DependencyRecord);
                if (record == nullptr)
                    return IndexResult::OutOfMemory;
                u32 recordDependencyCount = 0;
                u32 recordArtifactCount = 0;
                const IndexResult read = ReadRecord(reader, impl.config, *record, recordDependencyCount, recordArtifactCount);
                if (read != IndexResult::Success)
                {
                    VANGUARD_DELETE(record);
                    return read;
                }
                u32 duplicateIndex = 0;
                if (impl.lookup.Find(record->output.GetPath().Id(), duplicateIndex))
                {
                    VANGUARD_DELETE(record);
                    return IndexResult::Corrupt;
                }
                const u32 previous = impl.records.Size();
                impl.records.PushBack(record);
                if (impl.records.Size() != previous + 1u)
                {
                    VANGUARD_DELETE(record);
                    return IndexResult::OutOfMemory;
                }
                if (!impl.lookup.Insert(record->output.GetPath().Id(), previous).IsSuccessful())
                {
                    static_cast<void>(impl.records.RemoveAt(previous));
                    VANGUARD_DELETE(record);
                    return IndexResult::OutOfMemory;
                }
                parsedDependencies += recordDependencyCount;
                parsedArtifacts += recordArtifactCount;
            }

            impl.retirementBatches.Reserve(retirementBatchCount);
            impl.retiredRecords.Reserve(retiredRecordCount);
            impl.retirementSuccessors.Reserve(retirementSuccessorCount);
            if (impl.retirementBatches.Capacity() < retirementBatchCount || impl.retiredRecords.Capacity() < retiredRecordCount ||
                impl.retirementSuccessors.Capacity() < retirementSuccessorCount)
                return IndexResult::OutOfMemory;
            for (u32 batchIndex = 0u; batchIndex < retirementBatchCount; ++batchIndex)
            {
                u32 batchRecordCount = 0u;
                u32 batchSuccessorCount = 0u;
                if (!reader.ReadU32(batchRecordCount) || !reader.ReadU32(batchSuccessorCount) || batchSuccessorCount == 0u ||
                    batchRecordCount > retiredRecordCount - impl.retiredRecords.Size() ||
                    batchSuccessorCount > retirementSuccessorCount - impl.retirementSuccessors.Size())
                    return IndexResult::Corrupt;

                DependencyRetirementProtectionBatch batch;
                batch.recordOffset = impl.retiredRecords.Size();
                batch.recordCount = batchRecordCount;
                batch.successorOffset = impl.retirementSuccessors.Size();
                batch.successorCount = batchSuccessorCount;
                for (u32 recordIndex = 0u; recordIndex < batchRecordCount; ++recordIndex)
                {
                    DependencyRecord record;
                    u32 recordDependencyCount = 0u;
                    u32 recordArtifactCount = 0u;
                    const IndexResult read = ReadRecord(reader, impl.config, record, recordDependencyCount, recordArtifactCount);
                    if (read != IndexResult::Success)
                        return read;
                    const u32 before = impl.retiredRecords.Size();
                    impl.retiredRecords.PushBack(static_cast<DependencyRecord&&>(record));
                    if (impl.retiredRecords.Size() != before + 1u)
                        return IndexResult::OutOfMemory;
                    parsedDependencies += recordDependencyCount;
                    parsedArtifacts += recordArtifactCount;
                }
                for (u32 successorIndex = 0u; successorIndex < batchSuccessorCount; ++successorIndex)
                {
                    DependencyPublicationWitness witness;
                    if (!ReadPublicationWitness(reader, witness))
                        return IndexResult::Corrupt;
                    const u32 before = impl.retirementSuccessors.Size();
                    impl.retirementSuccessors.PushBack(witness);
                    if (impl.retirementSuccessors.Size() != before + 1u)
                        return IndexResult::OutOfMemory;
                }
                impl.retirementBatches.PushBack(batch);
                if (impl.retirementBatches.Size() != batchIndex + 1u)
                    return IndexResult::OutOfMemory;
            }
            if (reader.Position() != reader.Size() || parsedDependencies != dependencyCount || parsedArtifacts != artifactCount)
            {
                return IndexResult::Corrupt;
            }
            impl.generation = generation;
            impl.changed = false;
            impl.reverseDirty = true;
            impl.UpdateCountsLocked();
            return IndexResult::Success;
        }

        [[nodiscard]] IndexResult SaveIndexLocked(DependencyIndex::Impl& impl) noexcept
        {
            if (!impl.changed)
            {
                return IndexResult::Success;
            }
            BufferWriter writer(impl.config.maximumSerializedBytes);
            const IndexResult serialized = BuildSerializedIndex(impl, writer);
            if (serialized != IndexResult::Success)
            {
                return serialized;
            }
            filesystem::Manager& manager = filesystem::GetManager();
            auto file = manager.CreateFileWriter(impl.temporaryPath, filesystem::FOF_Buffered);
            if (!file)
            {
                ++impl.stats.ioFailures;
                return IndexResult::IoFailure;
            }
            const containers::DynamicArray<u8>& bytes = writer.Bytes();
            file->Serialize(const_cast<u8*>(bytes.TypedData()), bytes.Size());
            file->Flush();
            const bool written = !file->HasErrors();
            file.Reset();
            if (!written)
            {
                static_cast<void>(manager.DeleteFile(impl.temporaryPath));
                ++impl.stats.ioFailures;
                return IndexResult::IoFailure;
            }

            containers::DynamicArray<u8> validation(memory::pools::Assets::GetInstance());
            if (!ReadFile(impl.temporaryPath, impl.config.maximumSerializedBytes, validation) || validation.Size() != bytes.Size() ||
                crypto::Sha256(validation.TypedData(), validation.Size()) != crypto::Sha256(bytes.TypedData(), bytes.Size()))
            {
                static_cast<void>(manager.DeleteFile(impl.temporaryPath));
                ++impl.stats.ioFailures;
                return IndexResult::IoFailure;
            }
            if (!filesystem::ReplaceFile(impl.temporaryPath, impl.path))
            {
                static_cast<void>(manager.DeleteFile(impl.temporaryPath));
                ++impl.stats.ioFailures;
                return IndexResult::IoFailure;
            }
            ++impl.generation;
            ++impl.stats.saves;
            impl.changed = false;
            return IndexResult::Success;
        }
    } // namespace

    DependencyIndex::~DependencyIndex()
    {
        if (Shutdown() || m_impl == nullptr)
            return;

        // A destructor cannot report unsaved state. Preserve lifetime safety
        // by discarding only the in-memory index after cleanly detaching the
        // root domain. Concurrent destruction remains outside the API contract.
        BuildSystem* protectionDomain = nullptr;
        m_impl->lock.Acquire();
        if (m_impl->persistentProtectionDomainReservation == nullptr && m_impl->activePersistentPublications == 0u)
        {
            protectionDomain = m_impl->persistentProtectionDomain;
            m_impl->persistentProtectionDomainReservation = protectionDomain;
        }
        m_impl->lock.Release();
        if (protectionDomain != nullptr && !protectionDomain->UnregisterPersistentProtectionDomain(this))
            return;
        Impl* const implementation = m_impl;
        m_impl = nullptr;
        VANGUARD_DELETE(implementation);
    }

    IndexResult DependencyIndex::Initialize(const DependencyIndexConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!memory::IsInitialized() || !containers::IsInitialized() || !filesystem::IsInitialized() || config.root == nullptr || config.root[0] == '\0' ||
            config.maximumRecords == 0 || config.maximumDependenciesPerRecord == 0 || config.maximumArtifactsPerRecord == 0 ||
            config.maximumSerializedBytes < IndexHeaderSize || config.maximumSerializedBytes > ~u32{0} || !filesystem::AbsolutePath::IsValidPath(config.root) ||
            !filesystem::paths::IsAbsolutePath(config.root))
        {
            return IndexResult::InvalidArgument;
        }
        m_impl = VANGUARD_NEW(Impl)(config);
        if (m_impl == nullptr)
        {
            return IndexResult::OutOfMemory;
        }
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.CreatePath(m_impl->root))
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return IndexResult::IoFailure;
        }
        bool recovered = false;
        const bool primaryExists = manager.FileExist(m_impl->path);
        const bool temporaryExists = manager.FileExist(m_impl->temporaryPath);
        if (temporaryExists && (primaryExists || m_impl->config.allowDestructiveRecovery))
        {
            // With destructive recovery disabled, retain a temp-only image: no
            // committed primary exists to prove that discarding it is harmless.
            // When a primary exists we defer the no-destructive cleanup until
            // after that primary parses successfully below.
            if (!m_impl->config.allowDestructiveRecovery && primaryExists)
            {
                recovered = false;
            }
            else
            {
                recovered = manager.DeleteFile(m_impl->temporaryPath);
            }
            if (!recovered)
            {
                if (m_impl->config.allowDestructiveRecovery)
                {
                    ++m_impl->stats.ioFailures;
                    VANGUARD_DELETE(m_impl);
                    m_impl = nullptr;
                    return IndexResult::IoFailure;
                }
            }
        }
        if (!primaryExists)
        {
            if (temporaryExists && !m_impl->config.allowDestructiveRecovery)
            {
                VANGUARD_DELETE(m_impl);
                m_impl = nullptr;
                return IndexResult::InvalidState;
            }
            if (recovered)
            {
                ++m_impl->stats.recoveries;
                return IndexResult::Recovered;
            }
            return IndexResult::Success;
        }

        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        if (!ReadFile(m_impl->path, m_impl->config.maximumSerializedBytes, bytes))
        {
            ++m_impl->stats.ioFailures;
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return IndexResult::IoFailure;
        }
        const IndexResult loaded = ParseIndex(*m_impl, bytes);
        if (loaded == IndexResult::Success)
        {
            if (temporaryExists && !m_impl->config.allowDestructiveRecovery)
            {
                if (!manager.DeleteFile(m_impl->temporaryPath))
                {
                    ++m_impl->stats.ioFailures;
                    VANGUARD_DELETE(m_impl);
                    m_impl = nullptr;
                    return IndexResult::IoFailure;
                }
                recovered = true;
            }
            ++m_impl->stats.loads;
            return recovered ? IndexResult::Recovered : IndexResult::Success;
        }
        if (loaded != IndexResult::Corrupt && loaded != IndexResult::Incompatible)
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return loaded;
        }
        if (loaded == IndexResult::Corrupt)
        {
            ++m_impl->stats.corruptions;
        }
        else
        {
            ++m_impl->stats.incompatibleFiles;
        }
        if (!m_impl->config.allowDestructiveRecovery)
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return loaded;
        }
        m_impl->ClearRecordsLocked();
        m_impl->ClearRetirementProtectionLocked();
        if (!manager.DeleteFile(m_impl->path))
        {
            ++m_impl->stats.ioFailures;
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return IndexResult::IoFailure;
        }
        ++m_impl->stats.recoveries;
        m_impl->generation = 0;
        m_impl->changed = false;
        return IndexResult::Recovered;
    }

    bool DependencyIndex::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.Acquire();
        if (m_impl->changed || m_impl->transactionActive || m_impl->persistentProtectionDomainReservation != nullptr ||
            m_impl->activePersistentPublications != 0u)
        {
            m_impl->lock.Release();
            return false;
        }
        BuildSystem* const protectionDomain = m_impl->persistentProtectionDomain;
        m_impl->persistentProtectionDomainReservation = protectionDomain;
        m_impl->lock.Release();
        if (protectionDomain != nullptr && !protectionDomain->UnregisterPersistentProtectionDomain(this))
        {
            m_impl->lock.Acquire();
            if (m_impl->persistentProtectionDomainReservation == protectionDomain)
                m_impl->persistentProtectionDomainReservation = nullptr;
            m_impl->lock.Release();
            return false;
        }
        m_impl->lock.Acquire();
        m_impl->persistentProtectionDomain = nullptr;
        m_impl->persistentProtectionDomainReservation = nullptr;
        m_impl->lock.Release();
        Impl* const implementation = m_impl;
        m_impl = nullptr;
        VANGUARD_DELETE(implementation);
        return true;
    }

    bool DependencyIndex::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool DependencyIndex::BindPersistentProtectionDomain(BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || !buildSystem.IsInitialized())
            return false;

        m_impl->lock.Acquire();
        if (m_impl->persistentProtectionDomain == &buildSystem && m_impl->persistentProtectionDomainReservation == nullptr)
        {
            m_impl->lock.Release();
            return true;
        }
        if (m_impl->persistentProtectionDomain != nullptr || m_impl->persistentProtectionDomainReservation != nullptr ||
            m_impl->activePersistentPublications != 0u || m_impl->changed || m_impl->transactionActive || !m_impl->stagedRecords.Empty())
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->persistentProtectionDomainReservation = &buildSystem;
        m_impl->lock.Release();

        // Root-authority registration is deliberately outside the index lock.
        if (!buildSystem.RegisterPersistentProtectionDomain(this))
        {
            m_impl->lock.Acquire();
            if (m_impl->persistentProtectionDomainReservation == &buildSystem)
                m_impl->persistentProtectionDomainReservation = nullptr;
            m_impl->lock.Release();
            return false;
        }

        bool bound = false;
        m_impl->lock.Acquire();
        if (m_impl->persistentProtectionDomain == nullptr && m_impl->persistentProtectionDomainReservation == &buildSystem &&
            m_impl->activePersistentPublications == 0u && !m_impl->changed && !m_impl->transactionActive && m_impl->stagedRecords.Empty())
        {
            m_impl->persistentProtectionDomain = &buildSystem;
            bound = true;
        }
        if (m_impl->persistentProtectionDomainReservation == &buildSystem)
            m_impl->persistentProtectionDomainReservation = nullptr;
        m_impl->lock.Release();
        if (!bound)
            static_cast<void>(buildSystem.UnregisterPersistentProtectionDomain(this));
        return bound;
    }

    bool DependencyIndex::DetachPersistentProtectionDomain(BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr)
            return true;
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->persistentProtectionDomain == nullptr)
            return true;
        if (m_impl->persistentProtectionDomain != &buildSystem || m_impl->persistentProtectionDomainReservation != nullptr ||
            m_impl->activePersistentPublications != 0u)
            return false;
        m_impl->persistentProtectionDomain = nullptr;
        return true;
    }

    IndexResult DependencyIndex::Publish(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output) noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!request.IsValid() || !plan.IsPrepared() || output.buildFingerprint.IsEmpty() || output.contentFingerprint.IsEmpty() ||
            plan.GetSourceType() != request.source.identity.ExpectedType() || plan.GetOutputType() != request.output.ExpectedType() || output.artifacts.Empty() ||
            plan.GetDependencies().Count() > m_impl->config.maximumDependenciesPerRecord || output.artifacts.Size() > m_impl->config.maximumArtifactsPerRecord)
        {
            return IndexResult::InvalidArgument;
        }

        BuildSystem* protectionDomain = nullptr;
        m_impl->lock.Acquire();
        if (m_impl->persistentProtectionDomain == nullptr || m_impl->persistentProtectionDomainReservation != nullptr)
        {
            m_impl->lock.Release();
            return IndexResult::InvalidState;
        }
        protectionDomain = m_impl->persistentProtectionDomain;
        ++m_impl->activePersistentPublications;
        m_impl->lock.Release();

        struct PublicationScope final
        {
            Impl* impl = nullptr;
            ~PublicationScope()
            {
                if (impl == nullptr)
                    return;
                concurrency::ScopedLock guard(impl->lock);
                if (impl->activePersistentPublications != 0u)
                    --impl->activePersistentPublications;
            }
        } publicationScope{m_impl};

        if (!protectionDomain->OwnsPersistentPublication(this, output))
            return IndexResult::InvalidState;

        auto* const replacement = VANGUARD_NEW(DependencyRecord);
        if (replacement == nullptr)
        {
            return IndexResult::OutOfMemory;
        }
        replacement->source = request.source.identity;
        replacement->output = request.output;
        replacement->target = request.target;
        replacement->compiler = plan.Compiler();
        replacement->compilerVersion = plan.CompilerVersion();
        replacement->sourceInputFingerprint = ComputeSourceInputFingerprint(request);
        replacement->buildFingerprint = output.buildFingerprint;
        replacement->contentFingerprint = output.contentFingerprint;
        replacement->dependencies.Resize(plan.GetDependencies().Count());
        replacement->artifacts.Resize(output.artifacts.Size());
        if (replacement->dependencies.Size() != plan.GetDependencies().Count() || replacement->artifacts.Size() != output.artifacts.Size())
        {
            VANGUARD_DELETE(replacement);
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0; index < plan.GetDependencies().Count(); ++index)
        {
            replacement->dependencies[index] = plan.GetDependencies()[index];
        }
        for (u32 index = 0; index < output.artifacts.Size(); ++index)
        {
            const Artifact& artifact = output.artifacts[index];
            replacement->artifacts[index] = {artifact.resource, artifact.segment, artifact.flags, artifact.alignmentLog2, artifact.bytes.Size()};
        }

        const IndexResult published = [&]() noexcept
        {
            concurrency::ScopedLock guard(m_impl->lock);
            if (m_impl->persistentProtectionDomain != protectionDomain || m_impl->persistentProtectionDomainReservation != nullptr)
            {
                VANGUARD_DELETE(replacement);
                return IndexResult::InvalidState;
            }
            if (m_impl->transactionActive)
            {
                u32 committedPathIndex = 0;
                if (m_impl->lookup.Find(request.output.GetPath().Id(), committedPathIndex) &&
                    (committedPathIndex >= m_impl->records.Size() || m_impl->records[committedPathIndex]->output != request.output))
                {
                    VANGUARD_DELETE(replacement);
                    return IndexResult::InvalidArgument;
                }
                u32 retiredIndex = 0u;
                if (m_impl->stagedRemovalLookup.Find(request.output.GetPath().Id(), retiredIndex))
                {
                    VANGUARD_DELETE(replacement);
                    return IndexResult::InvalidState;
                }
                u32 stagedIndex = 0;
                if (m_impl->stagedLookup.Find(request.output.GetPath().Id(), stagedIndex))
                {
                    if (stagedIndex >= m_impl->stagedRecords.Size() || m_impl->stagedRecords[stagedIndex]->output != request.output)
                    {
                        VANGUARD_DELETE(replacement);
                        return IndexResult::InvalidArgument;
                    }
                    VANGUARD_DELETE(m_impl->stagedRecords[stagedIndex]);
                    m_impl->stagedRecords[stagedIndex] = replacement;
                    ++m_impl->stagedReplacements;
                }
                else
                {
                    const u32 previous = m_impl->stagedRecords.Size();
                    m_impl->stagedRecords.PushBack(replacement);
                    if (m_impl->stagedRecords.Size() != previous + 1u ||
                        !m_impl->stagedLookup.Insert(request.output.GetPath().Id(), previous).IsSuccessful())
                    {
                        if (m_impl->stagedRecords.Size() == previous + 1u)
                            static_cast<void>(m_impl->stagedRecords.RemoveAt(previous));
                        VANGUARD_DELETE(replacement);
                        return IndexResult::OutOfMemory;
                    }
                }
                ++m_impl->stagedPublications;
                return IndexResult::Success;
            }
            u32 pathIndex = 0;
            if (m_impl->lookup.Find(request.output.GetPath().Id(), pathIndex) &&
                (pathIndex >= m_impl->records.Size() || m_impl->records[pathIndex]->output != request.output))
            {
                VANGUARD_DELETE(replacement);
                return IndexResult::InvalidArgument;
            }
            DependencyRecord* const existing = m_impl->FindLocked(request.output);
            if (existing != nullptr)
            {
                u32 recordIndex = 0;
                static_cast<void>(m_impl->lookup.Find(request.output.GetPath().Id(), recordIndex));
                m_impl->records[recordIndex] = replacement;
                VANGUARD_DELETE(existing);
                ++m_impl->stats.replacements;
            }
            else
            {
                if (static_cast<u64>(m_impl->records.Size()) + m_impl->retiredRecords.Size() >= m_impl->config.maximumRecords)
                {
                    VANGUARD_DELETE(replacement);
                    return IndexResult::LimitExceeded;
                }
                const u32 previous = m_impl->records.Size();
                m_impl->records.PushBack(replacement);
                if (m_impl->records.Size() != previous + 1u)
                {
                    VANGUARD_DELETE(replacement);
                    return IndexResult::OutOfMemory;
                }
                if (!m_impl->lookup.Insert(request.output.GetPath().Id(), previous).IsSuccessful())
                {
                    static_cast<void>(m_impl->records.RemoveAt(previous));
                    VANGUARD_DELETE(replacement);
                    return IndexResult::OutOfMemory;
                }
            }
            ++m_impl->stats.publications;
            m_impl->changed = true;
            m_impl->reverseDirty = true;
            m_impl->UpdateCountsLocked();
            return IndexResult::Success;
        }();
        if (published == IndexResult::Success)
            protectionDomain->AdoptPersistentPublication(this, output);
        return published;
    }

    IndexResult DependencyIndex::Remove(const resources::ResourceReference output) noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!output.IsValid() || !output.IsTyped())
        {
            return IndexResult::InvalidArgument;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive)
        {
            return IndexResult::InvalidState;
        }
        u32 index = 0;
        if (!m_impl->lookup.Find(output.GetPath().Id(), index) || index >= m_impl->records.Size() || m_impl->records[index]->output != output)
        {
            return IndexResult::NotFound;
        }
        VANGUARD_DELETE(m_impl->records[index]);
        static_cast<void>(m_impl->records.RemoveAt(index));
        if (!m_impl->RebuildLookupLocked())
        {
            m_impl->changed = true;
            m_impl->reverseDirty = true;
            m_impl->UpdateCountsLocked();
            return IndexResult::OutOfMemory;
        }
        ++m_impl->stats.removals;
        m_impl->changed = true;
        m_impl->reverseDirty = true;
        m_impl->UpdateCountsLocked();
        return IndexResult::Success;
    }

    void DependencyIndex::Clear() noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive)
        {
            return;
        }
        m_impl->ClearRecordsLocked();
        m_impl->ClearRetirementProtectionLocked();
        m_impl->changed = true;
    }

    IndexResult DependencyIndex::Find(const resources::ResourceReference output, DependencyRecord& record) const noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!output.IsValid() || !output.IsTyped())
        {
            return IndexResult::InvalidArgument;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        const DependencyRecord* const found = m_impl->FindLocked(output);
        if (found == nullptr)
        {
            return IndexResult::NotFound;
        }
        return CopyRecord(*found, record) ? IndexResult::Success : IndexResult::OutOfMemory;
    }

    IndexResult DependencyIndex::GetDependencies(const resources::ResourceReference output,
                                                 containers::DynamicArray<resources::ResourceReference>& dependencies) const noexcept
    {
        dependencies.Clear();
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        const DependencyRecord* const record = m_impl->FindLocked(output);
        if (record == nullptr)
        {
            return IndexResult::NotFound;
        }
        dependencies.Resize(record->dependencies.Size());
        if (dependencies.Size() != record->dependencies.Size())
        {
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0; index < record->dependencies.Size(); ++index)
        {
            dependencies[index] = record->dependencies[index].identity;
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::GetDirectDependants(const resources::ResourceReference input,
                                                     containers::DynamicArray<resources::ResourceReference>& dependants) const noexcept
    {
        dependants.Clear();
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!input.IsValid() || !input.IsTyped())
        {
            return IndexResult::InvalidArgument;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->RebuildReverseLocked())
        {
            return IndexResult::OutOfMemory;
        }
        for (const ReverseEdge& edge : m_impl->reverseEdges)
        {
            if (edge.input == input && !ContainsReference(dependants, edge.output))
            {
                const u32 previous = dependants.Size();
                dependants.PushBack(edge.output);
                if (dependants.Size() != previous + 1u)
                {
                    dependants.Clear();
                    return IndexResult::OutOfMemory;
                }
            }
        }
        return dependants.Empty() ? IndexResult::NotFound : IndexResult::Success;
    }

    IndexResult DependencyIndex::CollectAffected(const resources::ResourceReference changed,
                                                 containers::DynamicArray<resources::ResourceReference>& affected) const noexcept
    {
        affected.Clear();
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!changed.IsValid() || !changed.IsTyped())
        {
            return IndexResult::InvalidArgument;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->RebuildReverseLocked())
        {
            return IndexResult::OutOfMemory;
        }
        containers::DynamicArray<resources::ResourceReference> queue(memory::pools::Assets::GetInstance());
        queue.PushBack(changed);
        if (queue.Size() != 1)
        {
            return IndexResult::OutOfMemory;
        }
        for (u32 cursor = 0; cursor < queue.Size(); ++cursor)
        {
            for (const ReverseEdge& edge : m_impl->reverseEdges)
            {
                if (edge.input != queue[cursor] || ContainsReference(affected, edge.output))
                {
                    continue;
                }
                const u32 affectedPrevious = affected.Size();
                const u32 queuePrevious = queue.Size();
                affected.PushBack(edge.output);
                queue.PushBack(edge.output);
                if (affected.Size() != affectedPrevious + 1u || queue.Size() != queuePrevious + 1u)
                {
                    affected.Clear();
                    return IndexResult::OutOfMemory;
                }
            }
        }
        return affected.Empty() ? IndexResult::NotFound : IndexResult::Success;
    }

    IndexResult DependencyIndex::MaterializeGeneratedDependencyContent(
        BuildPlan& plan, const containers::ArraySpan<const DependencyRecord> preferredRecords) const noexcept
    {
        if (m_impl == nullptr)
            return IndexResult::InvalidState;
        if (!plan.IsPrepared() || (!preferredRecords.Empty() && preferredRecords.Data() == nullptr))
            return IndexResult::InvalidArgument;

        bool missingRequired = false;
        for (const BuildDependency& dependency : plan.GetDependencies())
        {
            if (dependency.role != DependencyRole::Generated || dependency.requirement == DependencyRequirement::Soft)
                continue;

            BuildFingerprint content;
            bool foundPreferred = false;
            for (const DependencyRecord& record : preferredRecords)
            {
                if (record.output != dependency.identity)
                    continue;
                content = record.contentFingerprint;
                foundPreferred = true;
                break;
            }

            if (!foundPreferred)
            {
                DependencyRecord committed;
                const IndexResult found = Find(dependency.identity, committed);
                if (found == IndexResult::NotFound)
                {
                    if (dependency.requirement == DependencyRequirement::Required)
                        missingRequired = true;
                    continue;
                }
                if (found != IndexResult::Success)
                    return found;
                content = committed.contentFingerprint;
            }

            const Result applied = plan.SetGeneratedDependencyContent(dependency.identity, content);
            if (applied == Result::Success)
                continue;
            if (applied == Result::OutOfMemory)
                return IndexResult::OutOfMemory;
            if (applied == Result::LimitExceeded)
                return IndexResult::LimitExceeded;
            if (applied == Result::InvalidArgument)
                return IndexResult::InvalidArgument;
            return IndexResult::InvalidState;
        }
        return missingRequired ? IndexResult::NotFound : IndexResult::Success;
    }

    DirtyReason DependencyIndex::Evaluate(const BuildRequest& request, const BuildPlan& plan) const noexcept
    {
        if (m_impl == nullptr || !request.IsValid() || !plan.IsPrepared())
        {
            return DirtyReason::Missing;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        const DependencyRecord* const record = m_impl->FindLocked(request.output);
        if (record == nullptr)
        {
            return DirtyReason::Missing;
        }
        if (record->source != request.source.identity || record->target != request.target ||
            record->sourceInputFingerprint != ComputeSourceInputFingerprint(request))
        {
            return DirtyReason::SourceChanged;
        }
        if (record->compiler != plan.Compiler() || record->compilerVersion != plan.CompilerVersion() ||
            plan.GetSourceType() != request.source.identity.ExpectedType() || plan.GetOutputType() != request.output.ExpectedType())
        {
            return DirtyReason::CompilerChanged;
        }
        const containers::ArraySpan<const BuildDependency> dependencies = plan.GetDependencies();
        if (record->dependencies.Size() != dependencies.Count())
        {
            return DirtyReason::DependenciesChanged;
        }
        for (u32 index = 0; index < dependencies.Count(); ++index)
        {
            const BuildDependency& left = record->dependencies[index];
            const BuildDependency& right = dependencies[index];
            if (left.identity != right.identity || left.content != right.content || left.role != right.role || left.requirement != right.requirement)
            {
                return DirtyReason::DependenciesChanged;
            }
        }
        return DirtyReason::UpToDate;
    }

    IndexResult DependencyIndex::CaptureProtectionKeys(containers::DynamicArray<ArtifactSetKey>& keys,
                                                       const u32 maximumRecords, const u64 maximumBytes) const noexcept
    {
        keys.Clear();
        if (m_impl == nullptr || maximumRecords == 0u || maximumBytes == 0u)
            return IndexResult::InvalidArgument;

        concurrency::ScopedLock guard(m_impl->lock);
        const u64 committed = m_impl->records.Size();
        const u64 retired = m_impl->retiredRecords.Size();
        const u64 staged = m_impl->transactionActive ? m_impl->stagedRecords.Size() : 0u;
        const u64 count = committed + retired + staged;
        if (count > maximumRecords || count > maximumBytes / sizeof(ArtifactSetKey))
            return IndexResult::LimitExceeded;

        keys.Resize(static_cast<u32>(count));
        if (keys.Size() != count)
        {
            keys.Clear();
            return IndexResult::OutOfMemory;
        }
        u32 write = 0u;
        for (const DependencyRecord* const record : m_impl->records)
        {
            if (record == nullptr || record->buildFingerprint.IsEmpty() || record->contentFingerprint.IsEmpty())
            {
                keys.Clear();
                return IndexResult::InvalidState;
            }
            keys[write++] = {record->buildFingerprint, record->contentFingerprint};
        }
        for (const DependencyRecord& record : m_impl->retiredRecords)
        {
            if (record.buildFingerprint.IsEmpty() || record.contentFingerprint.IsEmpty())
            {
                keys.Clear();
                return IndexResult::InvalidState;
            }
            keys[write++] = {record.buildFingerprint, record.contentFingerprint};
        }
        if (m_impl->transactionActive)
        {
            for (const DependencyRecord* const record : m_impl->stagedRecords)
            {
                if (record == nullptr || record->buildFingerprint.IsEmpty() || record->contentFingerprint.IsEmpty())
                {
                    keys.Clear();
                    return IndexResult::InvalidState;
                }
                keys[write++] = {record->buildFingerprint, record->contentFingerprint};
            }
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::CaptureRetiredProtectionRecords(containers::DynamicArray<DependencyRecord>& records,
                                                                 const u32 maximumRecords, const u64 maximumBytes) const noexcept
    {
        records.Clear();
        if (m_impl == nullptr || maximumRecords == 0u || maximumBytes == 0u)
            return IndexResult::InvalidArgument;
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->retiredRecords.Size() > maximumRecords)
            return IndexResult::LimitExceeded;

        u64 retainedBytes = 0u;
        for (const DependencyRecord& record : m_impl->retiredRecords)
        {
            const u64 dependencyBytes = static_cast<u64>(record.dependencies.Size()) * sizeof(BuildDependency);
            const u64 artifactBytes = static_cast<u64>(record.artifacts.Size()) * sizeof(IndexedArtifact);
            const u64 recordBytes = sizeof(DependencyRecord) + dependencyBytes + artifactBytes;
            if (retainedBytes > maximumBytes || recordBytes > maximumBytes - retainedBytes)
                return IndexResult::LimitExceeded;
            retainedBytes += recordBytes;
        }

        records.Resize(m_impl->retiredRecords.Size());
        if (records.Size() != m_impl->retiredRecords.Size())
        {
            records.Clear();
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0u; index < m_impl->retiredRecords.Size(); ++index)
        {
            if (!CopyRecord(m_impl->retiredRecords[index], records[index]))
            {
                records.Clear();
                return IndexResult::OutOfMemory;
            }
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::CaptureRetirementProtectionSnapshot(
        containers::DynamicArray<DependencyRetirementProtectionBatch>& batches,
        containers::DynamicArray<DependencyRecord>& records,
        containers::DynamicArray<DependencyPublicationWitness>& successors,
        const u32 maximumRecords, const u64 maximumBytes) const noexcept
    {
        batches.Clear();
        records.Clear();
        successors.Clear();
        if (m_impl == nullptr || maximumRecords == 0u || maximumBytes == 0u)
            return IndexResult::InvalidArgument;

        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->retirementBatches.Size() > maximumRecords || m_impl->retiredRecords.Size() > maximumRecords ||
            m_impl->retirementSuccessors.Size() > maximumRecords)
            return IndexResult::LimitExceeded;

        u64 retainedBytes = static_cast<u64>(m_impl->retirementBatches.Size()) * sizeof(DependencyRetirementProtectionBatch) +
                            static_cast<u64>(m_impl->retirementSuccessors.Size()) * sizeof(DependencyPublicationWitness);
        if (retainedBytes > maximumBytes)
            return IndexResult::LimitExceeded;
        for (const DependencyRecord& record : m_impl->retiredRecords)
        {
            const u64 recordBytes = sizeof(DependencyRecord) +
                                    static_cast<u64>(record.dependencies.Size()) * sizeof(BuildDependency) +
                                    static_cast<u64>(record.artifacts.Size()) * sizeof(IndexedArtifact);
            if (recordBytes > maximumBytes - retainedBytes)
                return IndexResult::LimitExceeded;
            retainedBytes += recordBytes;
        }

        batches.Resize(m_impl->retirementBatches.Size());
        records.Resize(m_impl->retiredRecords.Size());
        successors.Resize(m_impl->retirementSuccessors.Size());
        if (batches.Size() != m_impl->retirementBatches.Size() || records.Size() != m_impl->retiredRecords.Size() ||
            successors.Size() != m_impl->retirementSuccessors.Size())
        {
            batches.Clear();
            records.Clear();
            successors.Clear();
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0u; index < batches.Size(); ++index)
            batches[index] = m_impl->retirementBatches[index];
        for (u32 index = 0u; index < successors.Size(); ++index)
            successors[index] = m_impl->retirementSuccessors[index];
        for (u32 index = 0u; index < records.Size(); ++index)
        {
            if (!CopyRecord(m_impl->retiredRecords[index], records[index]))
            {
                batches.Clear();
                records.Clear();
                successors.Clear();
                return IndexResult::OutOfMemory;
            }
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::ReleaseSatisfiedRetirementProtections(
        const containers::ArraySpan<const DependencyPublicationWitness> visible) noexcept
    {
        if (m_impl == nullptr || (!visible.Empty() && visible.Data() == nullptr))
            return IndexResult::InvalidArgument;

        containers::HashMap<resources::ResourceId, u32> visibleIndex(memory::pools::Assets::GetInstance());
        visibleIndex.Reserve(visible.Count());
        if (visibleIndex.Capacity() < visible.Count())
            return IndexResult::OutOfMemory;
        for (u32 index = 0u; index < visible.Count(); ++index)
        {
            if (!visible[index].IsValid())
                return IndexResult::InvalidArgument;
            u32 duplicate = 0u;
            if (visibleIndex.Find(visible[index].output.GetPath().Id(), duplicate))
                return IndexResult::InvalidArgument;
            if (!visibleIndex.Insert(visible[index].output.GetPath().Id(), index).IsSuccessful())
                return IndexResult::OutOfMemory;
        }

        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive || m_impl->changed || m_impl->persistentProtectionDomainReservation != nullptr ||
            m_impl->activePersistentPublications != 0u)
            return IndexResult::InvalidState;
        if (m_impl->retirementBatches.Empty() || visible.Empty())
            return IndexResult::Success;

        const auto batchSatisfied = [&](const DependencyRetirementProtectionBatch& batch) noexcept
        {
            if (batch.successorOffset > m_impl->retirementSuccessors.Size() ||
                batch.successorCount > m_impl->retirementSuccessors.Size() - batch.successorOffset)
                return false;
            for (u32 index = 0u; index < batch.successorCount; ++index)
            {
                const DependencyPublicationWitness& required = m_impl->retirementSuccessors[batch.successorOffset + index];
                u32 visibleSlot = 0u;
                if (!visibleIndex.Find(required.output.GetPath().Id(), visibleSlot) || visibleSlot >= visible.Count())
                    return false;
                const DependencyPublicationWitness& observed = visible[visibleSlot];
                if (observed.output != required.output || observed.buildFingerprint != required.buildFingerprint ||
                    observed.contentFingerprint != required.contentFingerprint)
                    return false;
            }
            return true;
        };

        u32 retainedBatchCount = 0u;
        u32 retainedRecordCount = 0u;
        u32 retainedSuccessorCount = 0u;
        for (const DependencyRetirementProtectionBatch& batch : m_impl->retirementBatches)
        {
            if (batchSatisfied(batch))
                continue;
            if (batch.recordOffset > m_impl->retiredRecords.Size() ||
                batch.recordCount > m_impl->retiredRecords.Size() - batch.recordOffset ||
                batch.successorOffset > m_impl->retirementSuccessors.Size() ||
                batch.successorCount > m_impl->retirementSuccessors.Size() - batch.successorOffset)
                return IndexResult::InvalidState;
            ++retainedBatchCount;
            retainedRecordCount += batch.recordCount;
            retainedSuccessorCount += batch.successorCount;
        }
        if (retainedBatchCount == m_impl->retirementBatches.Size())
            return IndexResult::Success;

        containers::DynamicArray<DependencyRetirementProtectionBatch> batches(memory::pools::Assets::GetInstance());
        containers::DynamicArray<DependencyRecord> retainedRecords(memory::pools::Assets::GetInstance());
        containers::DynamicArray<DependencyPublicationWitness> successors(memory::pools::Assets::GetInstance());
        batches.Reserve(retainedBatchCount);
        retainedRecords.Reserve(retainedRecordCount);
        successors.Reserve(retainedSuccessorCount);
        if (batches.Capacity() < retainedBatchCount || retainedRecords.Capacity() < retainedRecordCount ||
            successors.Capacity() < retainedSuccessorCount)
            return IndexResult::OutOfMemory;

        for (const DependencyRetirementProtectionBatch& batch : m_impl->retirementBatches)
        {
            if (batchSatisfied(batch))
                continue;
            DependencyRetirementProtectionBatch retained;
            retained.recordOffset = retainedRecords.Size();
            retained.recordCount = batch.recordCount;
            retained.successorOffset = successors.Size();
            retained.successorCount = batch.successorCount;
            for (u32 index = 0u; index < batch.recordCount; ++index)
            {
                DependencyRecord record;
                if (!CopyRecord(m_impl->retiredRecords[batch.recordOffset + index], record))
                    return IndexResult::OutOfMemory;
                retainedRecords.PushBack(static_cast<DependencyRecord&&>(record));
                if (retainedRecords.Size() != retained.recordOffset + index + 1u)
                    return IndexResult::OutOfMemory;
            }
            for (u32 index = 0u; index < batch.successorCount; ++index)
            {
                successors.PushBack(m_impl->retirementSuccessors[batch.successorOffset + index]);
                if (successors.Size() != retained.successorOffset + index + 1u)
                    return IndexResult::OutOfMemory;
            }
            const u32 before = batches.Size();
            batches.PushBack(retained);
            if (batches.Size() != before + 1u)
                return IndexResult::OutOfMemory;
        }

        containers::DynamicArray<DependencyRetirementProtectionBatch> oldBatches(memory::pools::Assets::GetInstance());
        containers::DynamicArray<DependencyRecord> oldRecords(memory::pools::Assets::GetInstance());
        containers::DynamicArray<DependencyPublicationWitness> oldSuccessors(memory::pools::Assets::GetInstance());
        oldBatches.Swap(m_impl->retirementBatches);
        oldRecords.Swap(m_impl->retiredRecords);
        oldSuccessors.Swap(m_impl->retirementSuccessors);
        batches.Swap(m_impl->retirementBatches);
        retainedRecords.Swap(m_impl->retiredRecords);
        successors.Swap(m_impl->retirementSuccessors);
        m_impl->changed = true;
        const IndexResult saved = SaveIndexLocked(*m_impl);
        if (saved != IndexResult::Success)
        {
            batches.Swap(m_impl->retirementBatches);
            retainedRecords.Swap(m_impl->retiredRecords);
            successors.Swap(m_impl->retirementSuccessors);
            oldBatches.Swap(m_impl->retirementBatches);
            oldRecords.Swap(m_impl->retiredRecords);
            oldSuccessors.Swap(m_impl->retirementSuccessors);
            m_impl->changed = false;
            return saved;
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::Save() noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive)
        {
            return IndexResult::InvalidState;
        }
        return SaveIndexLocked(*m_impl);
    }

    bool DependencyIndex::HasChanges() const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->changed || !m_impl->stagedRecords.Empty() || !m_impl->stagedRemovals.Empty();
    }

    IndexResult DependencyIndex::BeginTransaction() noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive || m_impl->changed)
        {
            return IndexResult::InvalidState;
        }
        m_impl->ClearStagedLocked();
        m_impl->transactionActive = true;
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::StageRemoval(const resources::ResourceReference output) noexcept
    {
        if (m_impl == nullptr)
            return IndexResult::InvalidState;
        if (!output.IsValid() || !output.IsTyped())
            return IndexResult::InvalidArgument;
        concurrency::ScopedLock guard(m_impl->lock);
        u32 stagedIndex = 0;
        if (!m_impl->transactionActive || m_impl->stagedLookup.Find(output.GetPath().Id(), stagedIndex))
            return IndexResult::InvalidState;
        const DependencyRecord* const committed = m_impl->FindLocked(output);
        if (committed == nullptr)
            return IndexResult::NotFound;
        u32 existingIndex = 0u;
        if (m_impl->stagedRemovalLookup.Find(output.GetPath().Id(), existingIndex))
            return existingIndex < m_impl->stagedRemovals.Size() && m_impl->stagedRemovals[existingIndex] == output
                       ? IndexResult::Success : IndexResult::InvalidArgument;
        const u32 previous = m_impl->stagedRemovals.Size();
        m_impl->stagedRemovals.PushBack(output);
        if (m_impl->stagedRemovals.Size() != previous + 1u)
            return IndexResult::OutOfMemory;
        if (!m_impl->stagedRemovalLookup.Insert(output.GetPath().Id(), previous).IsSuccessful())
        {
            static_cast<void>(m_impl->stagedRemovals.RemoveAt(previous));
            return IndexResult::OutOfMemory;
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::StageRetirementSuccessors(
        const containers::ArraySpan<const resources::ResourceReference> outputs) noexcept
    {
        if (m_impl == nullptr)
            return IndexResult::InvalidState;
        if (outputs.Empty() || outputs.Data() == nullptr || outputs.Count() > m_impl->config.maximumRecords)
            return IndexResult::InvalidArgument;
        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->transactionActive || !m_impl->stagedRetirementSuccessors.Empty())
            return IndexResult::InvalidState;

        m_impl->stagedRetirementSuccessors.Reserve(outputs.Count());
        if (m_impl->stagedRetirementSuccessors.Capacity() < outputs.Count())
            return IndexResult::OutOfMemory;
        for (u32 index = 0u; index < outputs.Count(); ++index)
        {
            const resources::ResourceReference output = outputs[index];
            if (!output.IsValid() || !output.IsTyped())
            {
                m_impl->stagedRetirementSuccessors.Clear();
                return IndexResult::InvalidArgument;
            }
            u32 stagedIndex = 0u;
            if (!m_impl->stagedLookup.Find(output.GetPath().Id(), stagedIndex) || stagedIndex >= m_impl->stagedRecords.Size() ||
                m_impl->stagedRecords[stagedIndex] == nullptr || m_impl->stagedRecords[stagedIndex]->output != output)
            {
                m_impl->stagedRetirementSuccessors.Clear();
                return IndexResult::InvalidState;
            }
            for (u32 previous = 0u; previous < index; ++previous)
            {
                if (outputs[previous] == output)
                {
                    m_impl->stagedRetirementSuccessors.Clear();
                    return IndexResult::InvalidArgument;
                }
            }
            m_impl->stagedRetirementSuccessors.PushBack(output);
            if (m_impl->stagedRetirementSuccessors.Size() != index + 1u)
            {
                m_impl->stagedRetirementSuccessors.Clear();
                return IndexResult::OutOfMemory;
            }
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::CaptureTransactionRecords(containers::DynamicArray<DependencyRecord>& records,
                                                           const u32 maximumRecords, const u64 maximumBytes) const noexcept
    {
        records.Clear();
        if (m_impl == nullptr || maximumRecords == 0u || maximumBytes == 0u)
        {
            return IndexResult::InvalidArgument;
        }

        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->transactionActive)
        {
            return IndexResult::InvalidState;
        }
        if (m_impl->stagedRecords.Size() > maximumRecords)
        {
            return IndexResult::LimitExceeded;
        }

        u64 retainedBytes = 0u;
        for (const DependencyRecord* const staged : m_impl->stagedRecords)
        {
            if (staged == nullptr)
            {
                records.Clear();
                return IndexResult::InvalidState;
            }
            const u64 dependencyBytes = static_cast<u64>(staged->dependencies.Size()) * sizeof(BuildDependency);
            const u64 artifactBytes = static_cast<u64>(staged->artifacts.Size()) * sizeof(IndexedArtifact);
            const u64 recordBytes = sizeof(DependencyRecord) + dependencyBytes + artifactBytes;
            if (retainedBytes > maximumBytes || recordBytes > maximumBytes - retainedBytes)
            {
                records.Clear();
                return IndexResult::LimitExceeded;
            }
            retainedBytes += recordBytes;
        }

        records.Resize(m_impl->stagedRecords.Size());
        if (records.Size() != m_impl->stagedRecords.Size())
        {
            records.Clear();
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0; index < m_impl->stagedRecords.Size(); ++index)
        {
            if (!CopyRecord(*m_impl->stagedRecords[index], records[index]))
            {
                records.Clear();
                return IndexResult::OutOfMemory;
            }
        }
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::CommitTransaction() noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->transactionActive)
        {
            return IndexResult::InvalidState;
        }
        if (m_impl->stagedRecords.Empty() && m_impl->stagedRemovals.Empty())
        {
            m_impl->transactionActive = false;
            ++m_impl->stats.transactionCommits;
            return IndexResult::Success;
        }

        // Ordinary recooks only publish/rewrite existing records. Preserve
        // their incremental O(staged records) commit behavior rather than
        // scanning every indexed output for the schema-retirement path.
        if (m_impl->stagedRemovals.Empty() && m_impl->stagedRetirementSuccessors.Empty())
        {
            struct AppliedRecord
            {
                u32 stageIndex = 0;
                u32 recordIndex = 0;
                DependencyRecord* previous = nullptr;
                bool inserted = false;
            };
            u32 newRecords = 0u;
            for (DependencyRecord* const staged : m_impl->stagedRecords)
            {
                u32 recordIndex = 0u;
                if (m_impl->lookup.Find(staged->output.GetPath().Id(), recordIndex))
                {
                    if (recordIndex >= m_impl->records.Size() || m_impl->records[recordIndex]->output != staged->output)
                        return IndexResult::InvalidArgument;
                }
                else
                    ++newRecords;
            }
            if (static_cast<u64>(m_impl->records.Size()) + m_impl->retiredRecords.Size() + newRecords >
                m_impl->config.maximumRecords)
                return IndexResult::LimitExceeded;

            containers::DynamicArray<AppliedRecord> applied(memory::pools::Assets::GetInstance());
            applied.Resize(m_impl->stagedRecords.Size());
            if (applied.Size() != m_impl->stagedRecords.Size())
                return IndexResult::OutOfMemory;
            u32 appliedCount = 0u;
            const auto undo = [&]() noexcept
            {
                while (appliedCount != 0u)
                {
                    const AppliedRecord& change = applied[--appliedCount];
                    DependencyRecord* const current = m_impl->records[change.recordIndex];
                    if (change.inserted)
                        static_cast<void>(m_impl->records.RemoveAt(change.recordIndex));
                    else
                        m_impl->records[change.recordIndex] = change.previous;
                    m_impl->stagedRecords[change.stageIndex] = current;
                }
                static_cast<void>(m_impl->RebuildLookupLocked());
                m_impl->changed = false;
                m_impl->reverseDirty = true;
                m_impl->UpdateCountsLocked();
            };

            for (u32 stageIndex = 0u; stageIndex < m_impl->stagedRecords.Size(); ++stageIndex)
            {
                DependencyRecord* const staged = m_impl->stagedRecords[stageIndex];
                u32 recordIndex = 0u;
                if (m_impl->lookup.Find(staged->output.GetPath().Id(), recordIndex))
                {
                    applied[appliedCount++] = {stageIndex, recordIndex, m_impl->records[recordIndex], false};
                    m_impl->records[recordIndex] = staged;
                    m_impl->stagedRecords[stageIndex] = nullptr;
                }
                else
                {
                    recordIndex = m_impl->records.Size();
                    m_impl->records.PushBack(staged);
                    if (m_impl->records.Size() != recordIndex + 1u ||
                        !m_impl->lookup.Insert(staged->output.GetPath().Id(), recordIndex).IsSuccessful())
                    {
                        if (m_impl->records.Size() == recordIndex + 1u)
                            static_cast<void>(m_impl->records.RemoveAt(recordIndex));
                        undo();
                        return IndexResult::OutOfMemory;
                    }
                    applied[appliedCount++] = {stageIndex, recordIndex, nullptr, true};
                    m_impl->stagedRecords[stageIndex] = nullptr;
                }
            }
            m_impl->changed = true;
            m_impl->reverseDirty = true;
            m_impl->UpdateCountsLocked();
            const IndexResult saved = SaveIndexLocked(*m_impl);
            if (saved != IndexResult::Success)
            {
                undo();
                return saved;
            }
            u64 committedReplacements = 0u;
            for (u32 i = 0u; i < appliedCount; ++i)
            {
                if (applied[i].previous != nullptr)
                {
                    ++committedReplacements;
                    VANGUARD_DELETE(applied[i].previous);
                }
            }
            const u64 publications = m_impl->stagedPublications;
            const u64 replacements = m_impl->stagedReplacements;
            m_impl->ClearStagedLocked();
            m_impl->transactionActive = false;
            m_impl->stats.publications += publications;
            m_impl->stats.replacements += replacements + committedReplacements;
            ++m_impl->stats.transactionCommits;
            return IndexResult::Success;
        }

        // Build a complete candidate pointer/index view without mutating the
        // committed view or the staged owner records. This makes removal,
        // replacement and insertion one save-or-rollback operation, including
        // allocation failures and failures of the durable file replacement.
        containers::DynamicArray<DependencyRecord*> candidate(memory::pools::Assets::GetInstance());
        containers::HashMap<resources::ResourceId, u32> candidateLookup(memory::pools::Assets::GetInstance());
        const u32 originalCount = m_impl->records.Size();
        const u32 stageCount = m_impl->stagedRecords.Size();
        const u32 removalCount = m_impl->stagedRemovals.Size();
        if (removalCount > originalCount || stageCount > m_impl->config.maximumRecords)
            return IndexResult::LimitExceeded;
        u32 newRecords = 0u;
        for (DependencyRecord* const staged : m_impl->stagedRecords)
        {
            if (staged == nullptr)
                return IndexResult::InvalidState;
            u32 index = 0u;
            if (m_impl->lookup.Find(staged->output.GetPath().Id(), index))
            {
                if (index >= originalCount || m_impl->records[index]->output != staged->output)
                    return IndexResult::InvalidArgument;
            }
            else
            {
                ++newRecords;
            }
        }
        const u64 finalCount = static_cast<u64>(originalCount) - removalCount + newRecords;
        const bool retirementProtectionRequested = !m_impl->stagedRetirementSuccessors.Empty();
        u32 replacementRetentionCount = 0u;
        u32 predecessorSuccessorCount = 0u;
        u32 newSuccessorCount = 0u;
        if (retirementProtectionRequested)
        {
            for (const resources::ResourceReference output : m_impl->stagedRetirementSuccessors)
            {
                u32 stagedIndex = 0u;
                if (!m_impl->stagedLookup.Find(output.GetPath().Id(), stagedIndex) || stagedIndex >= m_impl->stagedRecords.Size() ||
                    m_impl->stagedRecords[stagedIndex] == nullptr || m_impl->stagedRecords[stagedIndex]->output != output)
                    return IndexResult::InvalidState;
                u32 removalIndex = 0u;
                if (m_impl->stagedRemovalLookup.Find(output.GetPath().Id(), removalIndex))
                    continue;
                const DependencyRecord* const previous = m_impl->FindLocked(output);
                if (previous != nullptr)
                {
                    ++predecessorSuccessorCount;
                    if (previous->buildFingerprint != m_impl->stagedRecords[stagedIndex]->buildFingerprint ||
                        previous->contentFingerprint != m_impl->stagedRecords[stagedIndex]->contentFingerprint)
                        ++replacementRetentionCount;
                }
                else
                    ++newSuccessorCount;
            }
        }
        const u64 transactionRetentionCount = static_cast<u64>(removalCount) + replacementRetentionCount;
        const bool protectRetirements = retirementProtectionRequested &&
                                        (transactionRetentionCount != 0u ||
                                         (predecessorSuccessorCount != 0u && newSuccessorCount != 0u));
        const u64 retainedAfterCommit = static_cast<u64>(m_impl->retiredRecords.Size()) +
                                        (protectRetirements ? transactionRetentionCount : 0u);
        if (finalCount > m_impl->config.maximumRecords ||
            retainedAfterCommit > m_impl->config.maximumRecords ||
            finalCount + retainedAfterCommit > m_impl->config.maximumRecords ||
            (protectRetirements &&
             static_cast<u64>(m_impl->retirementSuccessors.Size()) + m_impl->stagedRetirementSuccessors.Size() >
                 m_impl->config.maximumRecords))
            return IndexResult::LimitExceeded;
        candidate.Reserve(static_cast<u32>(finalCount));
        if (candidate.Capacity() < finalCount)
            return IndexResult::OutOfMemory;
        const auto append = [&](DependencyRecord* const record) noexcept -> IndexResult
        {
            if (candidate.Size() >= m_impl->config.maximumRecords)
                return IndexResult::LimitExceeded;
            const u32 index = candidate.Size();
            candidate.PushBack(record);
            if (candidate.Size() != index + 1u)
                return IndexResult::OutOfMemory;
            return candidateLookup.Insert(record->output.GetPath().Id(), index).IsSuccessful()
                       ? IndexResult::Success : IndexResult::OutOfMemory;
        };
        for (DependencyRecord* const record : m_impl->records)
        {
            u32 retiredIndex = 0u;
            const bool retired = m_impl->stagedRemovalLookup.Find(record->output.GetPath().Id(), retiredIndex);
            u32 stageIndex = 0;
            if (retired || m_impl->stagedLookup.Find(record->output.GetPath().Id(), stageIndex))
                continue;
            const IndexResult result = append(record);
            if (result != IndexResult::Success)
                return result;
        }
        u64 committedReplacements = 0u;
        for (DependencyRecord* const staged : m_impl->stagedRecords)
        {
            if (staged == nullptr)
                return IndexResult::InvalidState;
            const DependencyRecord* const previous = m_impl->FindLocked(staged->output);
            if (previous != nullptr)
                ++committedReplacements;
            const IndexResult result = append(staged);
            if (result != IndexResult::Success)
                return result;
        }

        const u32 originalRetirementBatchCount = m_impl->retirementBatches.Size();
        const u32 originalRetiredRecordCount = m_impl->retiredRecords.Size();
        const u32 originalRetirementSuccessorCount = m_impl->retirementSuccessors.Size();
        const auto rollbackRetirementProtection = [&]() noexcept
        {
            m_impl->retirementBatches.Resize(originalRetirementBatchCount);
            m_impl->retiredRecords.Resize(originalRetiredRecordCount);
            m_impl->retirementSuccessors.Resize(originalRetirementSuccessorCount);
        };
        if (protectRetirements)
        {
            if (m_impl->stagedRetirementSuccessors.Empty() || m_impl->retirementBatches.Size() >= m_impl->config.maximumRecords)
                return IndexResult::InvalidState;
            m_impl->retirementBatches.Reserve(originalRetirementBatchCount + 1u);
            m_impl->retiredRecords.Reserve(originalRetiredRecordCount + static_cast<u32>(transactionRetentionCount));
            m_impl->retirementSuccessors.Reserve(originalRetirementSuccessorCount + m_impl->stagedRetirementSuccessors.Size());
            if (m_impl->retirementBatches.Capacity() < originalRetirementBatchCount + 1u ||
                m_impl->retiredRecords.Capacity() < originalRetiredRecordCount + transactionRetentionCount ||
                m_impl->retirementSuccessors.Capacity() < originalRetirementSuccessorCount + m_impl->stagedRetirementSuccessors.Size())
                return IndexResult::OutOfMemory;

            DependencyRetirementProtectionBatch protection;
            protection.recordOffset = originalRetiredRecordCount;
            protection.recordCount = static_cast<u32>(transactionRetentionCount);
            protection.successorOffset = originalRetirementSuccessorCount;
            protection.successorCount = m_impl->stagedRetirementSuccessors.Size();
            for (u32 removalIndex = 0u; removalIndex < m_impl->stagedRemovals.Size(); ++removalIndex)
            {
                const DependencyRecord* const record = m_impl->FindLocked(m_impl->stagedRemovals[removalIndex]);
                if (record == nullptr)
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                if (record->buildFingerprint.IsEmpty() || record->contentFingerprint.IsEmpty())
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                DependencyRecord retained;
                if (!CopyRecord(*record, retained))
                {
                    rollbackRetirementProtection();
                    return IndexResult::OutOfMemory;
                }
                m_impl->retiredRecords.PushBack(static_cast<DependencyRecord&&>(retained));
                if (m_impl->retiredRecords.Size() != protection.recordOffset + removalIndex + 1u)
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
            }
            for (const resources::ResourceReference output : m_impl->stagedRetirementSuccessors)
            {
                u32 stagedIndex = 0u;
                if (!m_impl->stagedLookup.Find(output.GetPath().Id(), stagedIndex) || stagedIndex >= m_impl->stagedRecords.Size())
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                u32 removalIndex = 0u;
                if (m_impl->stagedRemovalLookup.Find(output.GetPath().Id(), removalIndex))
                    continue;
                const DependencyRecord* const staged = m_impl->stagedRecords[stagedIndex];
                const DependencyRecord* const previous = m_impl->FindLocked(output);
                if (staged == nullptr || previous == nullptr ||
                    (previous->buildFingerprint == staged->buildFingerprint && previous->contentFingerprint == staged->contentFingerprint))
                    continue;
                if (previous->buildFingerprint.IsEmpty() || previous->contentFingerprint.IsEmpty())
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                DependencyRecord retained;
                if (!CopyRecord(*previous, retained))
                {
                    rollbackRetirementProtection();
                    return IndexResult::OutOfMemory;
                }
                const u32 before = m_impl->retiredRecords.Size();
                m_impl->retiredRecords.PushBack(static_cast<DependencyRecord&&>(retained));
                if (m_impl->retiredRecords.Size() != before + 1u)
                {
                    rollbackRetirementProtection();
                    return IndexResult::OutOfMemory;
                }
            }
            if (m_impl->retiredRecords.Size() != protection.recordOffset + protection.recordCount)
            {
                rollbackRetirementProtection();
                return IndexResult::InvalidState;
            }

            for (u32 successorIndex = 0u; successorIndex < m_impl->stagedRetirementSuccessors.Size(); ++successorIndex)
            {
                const resources::ResourceReference output = m_impl->stagedRetirementSuccessors[successorIndex];
                u32 stagedIndex = 0u;
                if (!m_impl->stagedLookup.Find(output.GetPath().Id(), stagedIndex) || stagedIndex >= m_impl->stagedRecords.Size())
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                const DependencyRecord* const staged = m_impl->stagedRecords[stagedIndex];
                if (staged == nullptr || staged->output != output || staged->buildFingerprint.IsEmpty() ||
                    staged->contentFingerprint.IsEmpty())
                {
                    rollbackRetirementProtection();
                    return IndexResult::InvalidState;
                }
                const bool predecessorPresent = m_impl->FindLocked(output) != nullptr;
                m_impl->retirementSuccessors.PushBack(
                    {staged->output, staged->buildFingerprint, staged->contentFingerprint, predecessorPresent});
                if (m_impl->retirementSuccessors.Size() != protection.successorOffset + successorIndex + 1u)
                {
                    rollbackRetirementProtection();
                    return IndexResult::OutOfMemory;
                }
            }
            m_impl->retirementBatches.PushBack(protection);
            if (m_impl->retirementBatches.Size() != originalRetirementBatchCount + 1u)
            {
                rollbackRetirementProtection();
                return IndexResult::OutOfMemory;
            }
        }

        // Candidate pointers are borrowed until the one durable Save succeeds.
        // The old container owns unchanged and superseded entries until then.
        containers::DynamicArray<DependencyRecord*> oldRecords(memory::pools::Assets::GetInstance());
        containers::HashMap<resources::ResourceId, u32> oldLookup(memory::pools::Assets::GetInstance());
        oldRecords.Swap(m_impl->records);
        oldLookup.Swap(m_impl->lookup);
        candidate.Swap(m_impl->records);
        candidateLookup.Swap(m_impl->lookup);
        m_impl->changed = true;
        m_impl->reverseDirty = true;
        m_impl->UpdateCountsLocked();
        const IndexResult saved = SaveIndexLocked(*m_impl);
        if (saved != IndexResult::Success)
        {
            candidate.Swap(m_impl->records);
            candidateLookup.Swap(m_impl->lookup);
            oldRecords.Swap(m_impl->records);
            oldLookup.Swap(m_impl->lookup);
            rollbackRetirementProtection();
            m_impl->changed = false;
            m_impl->reverseDirty = true;
            m_impl->UpdateCountsLocked();
            return saved;
        }

        // The candidate now owns staged publications. Delete only the old
        // entries that were replaced/retired; unchanged pointers moved intact.
        for (DependencyRecord* const old : oldRecords)
        {
            u32 stagedIndex = 0u;
            u32 retiredIndex = 0u;
            const bool obsolete = m_impl->stagedLookup.Find(old->output.GetPath().Id(), stagedIndex) ||
                                  m_impl->stagedRemovalLookup.Find(old->output.GetPath().Id(), retiredIndex);
            if (obsolete)
                VANGUARD_DELETE(old);
        }
        for (u32 index = 0; index < stageCount; ++index)
            m_impl->stagedRecords[index] = nullptr;
        const u64 publications = m_impl->stagedPublications;
        const u64 replacements = m_impl->stagedReplacements;
        m_impl->ClearStagedLocked();
        m_impl->transactionActive = false;
        m_impl->stats.publications += publications;
        m_impl->stats.replacements += replacements + committedReplacements;
        m_impl->stats.removals += removalCount;
        ++m_impl->stats.transactionCommits;
        return IndexResult::Success;
    }

    IndexResult DependencyIndex::RollbackTransaction() noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        if (!m_impl->transactionActive)
        {
            return IndexResult::InvalidState;
        }
        m_impl->ClearStagedLocked();
        m_impl->transactionActive = false;
        ++m_impl->stats.transactionRollbacks;
        return IndexResult::Success;
    }

    bool DependencyIndex::HasActiveTransaction() const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->transactionActive;
    }

    bool DependencyIndex::HasRetirementProtections() const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return !m_impl->retirementBatches.Empty();
    }

    DependencyIndexStats DependencyIndex::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->stats;
    }
} // namespace vanguard::assets
