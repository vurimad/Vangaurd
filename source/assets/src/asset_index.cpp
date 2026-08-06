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
    constexpr u16 IndexMajorVersion = 1;
    constexpr u16 IndexMinorVersion = 0;
    constexpr u32 IndexHeaderSize = 128;
    constexpr char IndexFileName[] = "asset-dependencies.vadi";
    constexpr char TemporaryFileName[] = "asset-dependencies.vadi.tmp";

    void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        static_cast<void>(hash.Update(&value, sizeof(value)));
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
            const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                                static_cast<u8>(value >> 24u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteU64(const u64 value) noexcept
        {
            const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u),
                                static_cast<u8>(value >> 24u), static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                                static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteBytes(const void* const data, const u32 size) noexcept
        {
            if ((size != 0 && data == nullptr) || static_cast<u64>(m_bytes.Size()) + size > m_limit ||
                static_cast<u64>(m_bytes.Size()) + size > ~u32{0})
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
            value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) | (static_cast<u32>(bytes[2]) << 16u) |
                    (static_cast<u32>(bytes[3]) << 24u);
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
        return left.Path().Id() < right.Path().Id() ||
               (left.Path().Id() == right.Path().Id() && left.ExpectedType() < right.ExpectedType());
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
              temporaryPath(root.AddFilePath(TemporaryFileName)), records(memory::pools::Assets::GetInstance()),
              lookup(memory::pools::Assets::GetInstance()), reverseEdges(memory::pools::Assets::GetInstance()),
              stagedRecords(memory::pools::Assets::GetInstance()), stagedLookup(memory::pools::Assets::GetInstance())
        {
            config.root = nullptr;
        }

        ~Impl()
        {
            ClearRecordsLocked();
            ClearStagedLocked();
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
        DependencyIndexStats stats;
        u64 generation = 0;
        u64 stagedPublications = 0;
        u64 stagedReplacements = 0;
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

        [[nodiscard]] bool RebuildLookupLocked() noexcept
        {
            lookup.Clear();
            for (u32 index = 0; index < records.Size(); ++index)
            {
                if (!lookup.Insert(records[index]->output.Path().Id(), index).IsSuccessful())
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
            if (!lookup.Find(output.Path().Id(), index) || index >= records.Size() || records[index]->output != output)
            {
                return nullptr;
            }
            return records[index];
        }

        [[nodiscard]] const DependencyRecord* FindLocked(const resources::ResourceReference output) const noexcept
        {
            u32 index = 0;
            if (!lookup.Find(output.Path().Id(), index) || index >= records.Size() || records[index]->output != output)
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
                    const bool less = LessReference(value.input, previous.input) ||
                                      (value.input == previous.input && LessReference(value.output, previous.output));
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

    DependencyRecord::DependencyRecord() noexcept
        : dependencies(memory::pools::Assets::GetInstance()), artifacts(memory::pools::Assets::GetInstance())
    {
    }

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
        HashU64(hash, request.source.identity.Path().Id());
        HashU32(hash, request.source.identity.ExpectedType());
        HashU64(hash, request.output.Path().Id());
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
        [[nodiscard]] bool ReadFile(const filesystem::AbsolutePath& path, const u64 maximumBytes,
                                    containers::DynamicArray<u8>& bytes) noexcept
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
            return writer.WriteU64(dependency.identity.Path().Id()) && writer.WriteU32(dependency.identity.ExpectedType()) &&
                   writer.WriteBytes(dependency.content.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteU8(static_cast<u8>(dependency.role)) && writer.WriteU8(static_cast<u8>(dependency.requirement)) &&
                   writer.WriteU16(0);
        }

        [[nodiscard]] bool WriteArtifact(BufferWriter& writer, const IndexedArtifact& artifact) noexcept
        {
            return writer.WriteU64(artifact.resource.Path().Id()) && writer.WriteU32(artifact.resource.ExpectedType()) &&
                   writer.WriteU32(artifact.segment) && writer.WriteU16(static_cast<u16>(artifact.flags)) &&
                   writer.WriteU8(artifact.alignmentLog2) && writer.WriteU8(0) && writer.WriteU64(artifact.byteCount) && writer.WriteU32(0);
        }

        [[nodiscard]] bool WriteRecord(BufferWriter& writer, const DependencyRecord& record) noexcept
        {
            return writer.WriteU64(record.source.Path().Id()) && writer.WriteU32(record.source.ExpectedType()) &&
                   writer.WriteU64(record.output.Path().Id()) && writer.WriteU32(record.output.ExpectedType()) &&
                   writer.WriteU8(static_cast<u8>(record.target)) && writer.WriteU8(0) && writer.WriteU16(0) &&
                   writer.WriteU64(record.compiler) && writer.WriteU32(record.compilerVersion) &&
                   writer.WriteBytes(record.sourceInputFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteBytes(record.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteBytes(record.contentFingerprint.bytes, BuildFingerprint::ByteCount) &&
                   writer.WriteU32(record.dependencies.Size()) && writer.WriteU32(record.artifacts.Size());
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
                           writer.WriteU32(IndexHeaderSize) && writer.WriteU32(impl.records.Size()) &&
                           writer.WriteU32(static_cast<u32>(dependencyCount)) && writer.WriteU32(static_cast<u32>(artifactCount)) &&
                           writer.WriteU32(0) && writer.WriteU64(0) && writer.WriteU64(impl.generation + 1u) &&
                           writer.WriteBytes(impl.config.settingsFingerprint.bytes, BuildFingerprint::ByteCount) &&
                           writer.WriteBytes(empty.bytes, BuildFingerprint::ByteCount);
            const u8 reserved[IndexHeaderSize - 108]{};
            written = written && writer.WriteBytes(reserved, sizeof(reserved));
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
            if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) ||
                !reader.ReadU32(recordCount) || !reader.ReadU32(dependencyCount) || !reader.ReadU32(artifactCount) ||
                !reader.ReadU32(reserved32) || !reader.ReadU64(fileSize) || !reader.ReadU64(generation) ||
                !reader.ReadBytes(settings.bytes, BuildFingerprint::ByteCount) ||
                !reader.ReadBytes(payload.bytes, BuildFingerprint::ByteCount) || !reader.Skip(IndexHeaderSize - 108))
            {
                return IndexResult::Corrupt;
            }
            if (magic != IndexMagic || major != IndexMajorVersion || minor > IndexMinorVersion ||
                settings != impl.config.settingsFingerprint)
            {
                return IndexResult::Incompatible;
            }
            if (headerSize != IndexHeaderSize || reserved32 != 0 || fileSize != bytes.Size() || recordCount > impl.config.maximumRecords ||
                dependencyCount > static_cast<u64>(recordCount) * impl.config.maximumDependenciesPerRecord ||
                artifactCount > static_cast<u64>(recordCount) * impl.config.maximumArtifactsPerRecord ||
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
                {
                    return IndexResult::OutOfMemory;
                }
                u64 sourcePath = 0;
                u32 sourceType = 0;
                u64 outputPath = 0;
                u32 outputType = 0;
                u8 target = 0;
                u8 reserved8 = 0;
                u16 reserved16 = 0;
                u32 recordDependencyCount = 0;
                u32 recordArtifactCount = 0;
                const bool fixedRead = reader.ReadU64(sourcePath) && reader.ReadU32(sourceType) && reader.ReadU64(outputPath) &&
                                       reader.ReadU32(outputType) && reader.ReadU8(target) && reader.ReadU8(reserved8) &&
                                       reader.ReadU16(reserved16) && reader.ReadU64(record->compiler) &&
                                       reader.ReadU32(record->compilerVersion) &&
                                       reader.ReadBytes(record->sourceInputFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                       reader.ReadBytes(record->buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                       reader.ReadBytes(record->contentFingerprint.bytes, BuildFingerprint::ByteCount) &&
                                       reader.ReadU32(recordDependencyCount) && reader.ReadU32(recordArtifactCount);
                record->source = resources::ResourceReference(resources::ResourcePath::FromId(sourcePath), sourceType);
                record->output = resources::ResourceReference(resources::ResourcePath::FromId(outputPath), outputType);
                record->target = static_cast<TargetPlatform>(target);
                if (!fixedRead || reserved8 != 0 || reserved16 != 0 || !record->source.IsValid() || !record->source.IsTyped() ||
                    !record->output.IsValid() || !record->output.IsTyped() || record->target >= TargetPlatform::Count ||
                    record->compiler == InvalidCompilerId || record->compilerVersion == 0 ||
                    recordDependencyCount > impl.config.maximumDependenciesPerRecord ||
                    recordArtifactCount > impl.config.maximumArtifactsPerRecord)
                {
                    VANGUARD_DELETE(record);
                    return IndexResult::Corrupt;
                }
                u32 duplicateIndex = 0;
                if (impl.lookup.Find(record->output.Path().Id(), duplicateIndex))
                {
                    VANGUARD_DELETE(record);
                    return IndexResult::Corrupt;
                }

                record->dependencies.Resize(recordDependencyCount);
                record->artifacts.Resize(recordArtifactCount);
                if (record->dependencies.Size() != recordDependencyCount || record->artifacts.Size() != recordArtifactCount)
                {
                    VANGUARD_DELETE(record);
                    return IndexResult::OutOfMemory;
                }
                bool valid = true;
                for (BuildDependency& dependency : record->dependencies)
                {
                    u64 path = 0;
                    u32 type = 0;
                    u8 role = 0;
                    u8 requirement = 0;
                    valid = valid && reader.ReadU64(path) && reader.ReadU32(type) &&
                            reader.ReadBytes(dependency.content.bytes, BuildFingerprint::ByteCount) && reader.ReadU8(role) &&
                            reader.ReadU8(requirement) && reader.ReadU16(reserved16);
                    dependency.identity = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
                    dependency.role = static_cast<DependencyRole>(role);
                    dependency.requirement = static_cast<DependencyRequirement>(requirement);
                    valid = valid && reserved16 == 0 && dependency.IsValid();
                }
                for (IndexedArtifact& artifact : record->artifacts)
                {
                    u64 path = 0;
                    u32 type = 0;
                    u16 flags = 0;
                    valid = valid && reader.ReadU64(path) && reader.ReadU32(type) && reader.ReadU32(artifact.segment) &&
                            reader.ReadU16(flags) && reader.ReadU8(artifact.alignmentLog2) && reader.ReadU8(reserved8) &&
                            reader.ReadU64(artifact.byteCount) && reader.ReadU32(reserved32);
                    artifact.resource = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
                    artifact.flags = static_cast<ArtifactFlags>(flags);
                    valid = valid && artifact.resource.IsValid() && artifact.resource.IsTyped() && reserved8 == 0 && reserved32 == 0 &&
                            artifact.alignmentLog2 <= 63;
                }
                if (!valid)
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
                if (!impl.lookup.Insert(record->output.Path().Id(), previous).IsSuccessful())
                {
                    static_cast<void>(impl.records.RemoveAt(previous));
                    VANGUARD_DELETE(record);
                    return IndexResult::OutOfMemory;
                }
                parsedDependencies += recordDependencyCount;
                parsedArtifacts += recordArtifactCount;
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
            if (!manager.MoveFile(impl.temporaryPath, impl.path))
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
        static_cast<void>(Shutdown());
    }

    IndexResult DependencyIndex::Initialize(const DependencyIndexConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!memory::IsInitialized() || !containers::IsInitialized() || !filesystem::IsInitialized() || config.root == nullptr ||
            config.root[0] == '\0' || config.maximumRecords == 0 || config.maximumDependenciesPerRecord == 0 ||
            config.maximumArtifactsPerRecord == 0 || config.maximumSerializedBytes < IndexHeaderSize ||
            config.maximumSerializedBytes > ~u32{0} || !filesystem::AbsolutePath::IsValidPath(config.root) ||
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
        if (manager.FileExist(m_impl->temporaryPath))
        {
            recovered = manager.DeleteFile(m_impl->temporaryPath);
            if (!recovered)
            {
                ++m_impl->stats.ioFailures;
                VANGUARD_DELETE(m_impl);
                m_impl = nullptr;
                return IndexResult::IoFailure;
            }
        }
        if (!manager.FileExist(m_impl->path))
        {
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
        m_impl->ClearRecordsLocked();
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
        if (m_impl->changed || m_impl->transactionActive)
        {
            m_impl->lock.Release();
            return false;
        }
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

    IndexResult DependencyIndex::Publish(const BuildRequest& request, const BuildPlan& plan, const BuildOutput& output) noexcept
    {
        if (m_impl == nullptr)
        {
            return IndexResult::InvalidState;
        }
        if (!request.IsValid() || !plan.IsPrepared() || output.buildFingerprint.IsEmpty() || output.contentFingerprint.IsEmpty() ||
            plan.SourceType() != request.source.identity.ExpectedType() || plan.OutputType() != request.output.ExpectedType() ||
            output.artifacts.Empty() || plan.Dependencies().Count() > m_impl->config.maximumDependenciesPerRecord ||
            output.artifacts.Size() > m_impl->config.maximumArtifactsPerRecord)
        {
            return IndexResult::InvalidArgument;
        }

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
        replacement->dependencies.Resize(plan.Dependencies().Count());
        replacement->artifacts.Resize(output.artifacts.Size());
        if (replacement->dependencies.Size() != plan.Dependencies().Count() || replacement->artifacts.Size() != output.artifacts.Size())
        {
            VANGUARD_DELETE(replacement);
            return IndexResult::OutOfMemory;
        }
        for (u32 index = 0; index < plan.Dependencies().Count(); ++index)
        {
            replacement->dependencies[index] = plan.Dependencies()[index];
        }
        for (u32 index = 0; index < output.artifacts.Size(); ++index)
        {
            const Artifact& artifact = output.artifacts[index];
            replacement->artifacts[index] = {artifact.resource, artifact.segment, artifact.flags, artifact.alignmentLog2,
                                             artifact.bytes.Size()};
        }

        concurrency::ScopedLock guard(m_impl->lock);
        if (m_impl->transactionActive)
        {
            u32 committedPathIndex = 0;
            if (m_impl->lookup.Find(request.output.Path().Id(), committedPathIndex) &&
                (committedPathIndex >= m_impl->records.Size() || m_impl->records[committedPathIndex]->output != request.output))
            {
                VANGUARD_DELETE(replacement);
                return IndexResult::InvalidArgument;
            }
            u32 stagedIndex = 0;
            if (m_impl->stagedLookup.Find(request.output.Path().Id(), stagedIndex))
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
                    !m_impl->stagedLookup.Insert(request.output.Path().Id(), previous).IsSuccessful())
                {
                    if (m_impl->stagedRecords.Size() == previous + 1u)
                    {
                        static_cast<void>(m_impl->stagedRecords.RemoveAt(previous));
                    }
                    VANGUARD_DELETE(replacement);
                    return IndexResult::OutOfMemory;
                }
            }
            ++m_impl->stagedPublications;
            return IndexResult::Success;
        }
        u32 pathIndex = 0;
        if (m_impl->lookup.Find(request.output.Path().Id(), pathIndex) &&
            (pathIndex >= m_impl->records.Size() || m_impl->records[pathIndex]->output != request.output))
        {
            VANGUARD_DELETE(replacement);
            return IndexResult::InvalidArgument;
        }
        DependencyRecord* const existing = m_impl->FindLocked(request.output);
        if (existing != nullptr)
        {
            u32 recordIndex = 0;
            static_cast<void>(m_impl->lookup.Find(request.output.Path().Id(), recordIndex));
            m_impl->records[recordIndex] = replacement;
            VANGUARD_DELETE(existing);
            ++m_impl->stats.replacements;
        }
        else
        {
            if (m_impl->records.Size() >= m_impl->config.maximumRecords)
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
            if (!m_impl->lookup.Insert(request.output.Path().Id(), previous).IsSuccessful())
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
        if (!m_impl->lookup.Find(output.Path().Id(), index) || index >= m_impl->records.Size() || m_impl->records[index]->output != output)
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
            plan.SourceType() != request.source.identity.ExpectedType() || plan.OutputType() != request.output.ExpectedType())
        {
            return DirtyReason::CompilerChanged;
        }
        const containers::ArraySpan<const BuildDependency> dependencies = plan.Dependencies();
        if (record->dependencies.Size() != dependencies.Count())
        {
            return DirtyReason::DependenciesChanged;
        }
        for (u32 index = 0; index < dependencies.Count(); ++index)
        {
            const BuildDependency& left = record->dependencies[index];
            const BuildDependency& right = dependencies[index];
            if (left.identity != right.identity || left.content != right.content || left.role != right.role ||
                left.requirement != right.requirement)
            {
                return DirtyReason::DependenciesChanged;
            }
        }
        return DirtyReason::UpToDate;
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
        return m_impl->changed || !m_impl->stagedRecords.Empty();
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
        if (m_impl->stagedRecords.Empty())
        {
            m_impl->transactionActive = false;
            ++m_impl->stats.transactionCommits;
            return IndexResult::Success;
        }

        struct AppliedRecord
        {
            u32 stageIndex = 0;
            u32 recordIndex = 0;
            DependencyRecord* previous = nullptr;
            bool inserted = false;
        };

        u32 newRecords = 0;
        for (DependencyRecord* const staged : m_impl->stagedRecords)
        {
            u32 recordIndex = 0;
            if (m_impl->lookup.Find(staged->output.Path().Id(), recordIndex))
            {
                if (recordIndex >= m_impl->records.Size() || m_impl->records[recordIndex]->output != staged->output)
                {
                    return IndexResult::InvalidArgument;
                }
            }
            else
            {
                ++newRecords;
            }
        }
        if (newRecords > m_impl->config.maximumRecords - m_impl->records.Size())
        {
            return IndexResult::LimitExceeded;
        }

        containers::DynamicArray<AppliedRecord> applied(memory::pools::Assets::GetInstance());
        applied.Resize(m_impl->stagedRecords.Size());
        if (applied.Size() != m_impl->stagedRecords.Size())
        {
            return IndexResult::OutOfMemory;
        }
        u32 appliedCount = 0;

        const auto rollbackApplied = [&]() noexcept
        {
            while (appliedCount != 0)
            {
                --appliedCount;
                const AppliedRecord& change = applied[appliedCount];
                DependencyRecord* current = m_impl->records[change.recordIndex];
                if (change.inserted)
                {
                    static_cast<void>(m_impl->records.RemoveAt(change.recordIndex));
                }
                else
                {
                    m_impl->records[change.recordIndex] = change.previous;
                }
                m_impl->stagedRecords[change.stageIndex] = current;
            }
            static_cast<void>(m_impl->RebuildLookupLocked());
            m_impl->changed = false;
            m_impl->reverseDirty = true;
            m_impl->UpdateCountsLocked();
        };

        for (u32 stageIndex = 0; stageIndex < m_impl->stagedRecords.Size(); ++stageIndex)
        {
            DependencyRecord* const staged = m_impl->stagedRecords[stageIndex];
            u32 recordIndex = 0;
            if (m_impl->lookup.Find(staged->output.Path().Id(), recordIndex))
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
                    !m_impl->lookup.Insert(staged->output.Path().Id(), recordIndex).IsSuccessful())
                {
                    if (m_impl->records.Size() == recordIndex + 1u)
                    {
                        static_cast<void>(m_impl->records.RemoveAt(recordIndex));
                    }
                    rollbackApplied();
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
            rollbackApplied();
            return saved;
        }

        const u64 publications = m_impl->stagedPublications;
        const u64 replacements = m_impl->stagedReplacements;
        u64 committedReplacements = 0;
        for (u32 index = 0; index < appliedCount; ++index)
        {
            if (applied[index].previous != nullptr)
            {
                ++committedReplacements;
                VANGUARD_DELETE(applied[index].previous);
            }
        }
        m_impl->ClearStagedLocked();
        m_impl->transactionActive = false;
        m_impl->stats.publications += publications;
        m_impl->stats.replacements += replacements + committedReplacements;
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
