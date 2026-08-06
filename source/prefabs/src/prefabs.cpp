#include <vanguard/prefabs/prefabs.hpp>

#include <algorithm>

namespace
{
    using namespace vanguard;
    namespace prefab = vanguard::prefabs;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 PrefabSection = serialization::MakeFourCC('P', 'R', 'O', 'T');
    constexpr u32 BodyWireVersion = 1;
    constexpr u32 BodyPrefixSize = 36;
    constexpr u16 KnownEntityFlags = static_cast<u16>(prefab::EntityFlags::Root) |
                                     static_cast<u16>(prefab::EntityFlags::DisabledByDefault) |
                                     static_cast<u16>(prefab::EntityFlags::EditorOnly);
    constexpr u16 KnownComponentFlags = static_cast<u16>(prefab::ComponentFlags::DisabledByDefault) |
                                        static_cast<u16>(prefab::ComponentFlags::EditorOnly);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : entities(memory::pools::World::GetInstance()), components(memory::pools::World::GetInstance()),
              dependencies(memory::pools::Resources::GetInstance()), componentData(memory::pools::World::GetInstance())
        {
        }

        u64 name = 0;
        crypto::Digest256 sourceFingerprint;
        containers::DynamicArray<prefab::EntityRecord> entities;
        containers::DynamicArray<prefab::ComponentRecord> components;
        containers::DynamicArray<prefab::DependencyRecord> dependencies;
        ByteArray componentData;
    };

    struct ComponentSource
    {
        const prefab::ComponentBuildRecord* source = nullptr;
    };

    [[nodiscard]] prefab::Result Convert(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success: return prefab::Result::Success;
        case serialization::Result::InvalidMagic: return prefab::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
        case serialization::Result::UnsupportedByteOrder:
        case serialization::Result::UnsupportedHeader: return prefab::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure: return prefab::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow: return prefab::Result::LimitExceeded;
        case serialization::Result::InvalidArgument: return prefab::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream: return prefab::Result::IoFailure;
        default: return prefab::Result::InvalidLayout;
        }
    }

    [[nodiscard]] prefab::Result Convert(const schemas::Result result) noexcept
    {
        switch (result)
        {
        case schemas::Result::Success: return prefab::Result::Success;
        case schemas::Result::SchemaNotFound: return prefab::Result::UnknownSchema;
        case schemas::Result::LimitExceeded: return prefab::Result::LimitExceeded;
        case schemas::Result::IoFailure: return prefab::Result::IoFailure;
        default: return prefab::Result::SchemaFailure;
        }
    }

    [[nodiscard]] prefab::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? prefab::Result::Success : Convert(writer.Status());
    }

    [[nodiscard]] prefab::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? prefab::Result::Success : Convert(reader.Status());
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool WriteReference(serialization::BinaryWriter& writer,
                                      const resources::ResourceReference reference) noexcept
    {
        return writer.WriteU64(reference.Path().Id()) && writer.WriteU32(reference.ExpectedType());
    }

    [[nodiscard]] bool ReadReference(serialization::BinaryReader& reader,
                                     resources::ResourceReference& reference) noexcept
    {
        u64 path = 0;
        u32 type = 0;
        if (!reader.ReadU64(path) || !reader.ReadU32(type)) return false;
        reference = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
        return true;
    }

    [[nodiscard]] bool EntityLess(const prefab::EntityRecord& left, const prefab::EntityRecord& right) noexcept
    {
        return left.stableId < right.stableId;
    }

    [[nodiscard]] bool ComponentSourceLess(const ComponentSource& left, const ComponentSource& right) noexcept
    {
        if (left.source->entityStableId != right.source->entityStableId)
            return left.source->entityStableId < right.source->entityStableId;
        return left.source->stableId < right.source->stableId;
    }

    [[nodiscard]] bool DependencyLess(const prefab::DependencyRecord& left,
                                      const prefab::DependencyRecord& right) noexcept
    {
        if (left.resource.Path() != right.resource.Path()) return left.resource.Path() < right.resource.Path();
        return left.resource.ExpectedType() < right.resource.ExpectedType();
    }

    [[nodiscard]] const prefab::EntityRecord* FindEntity(const containers::DynamicArray<prefab::EntityRecord>& entities,
                                                         const u64 stableId) noexcept
    {
        u32 first = 0;
        u32 count = entities.Size();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            if (entities[index].stableId < stableId)
            {
                first = index + 1u;
                count -= step + 1u;
            }
            else count = step;
        }
        return first < entities.Size() && entities[first].stableId == stableId ? &entities[first] : nullptr;
    }

    [[nodiscard]] bool AddDependency(containers::DynamicArray<prefab::DependencyRecord>& dependencies,
                                     const resources::ResourceReference reference,
                                     const resources::DependencyKind kind) noexcept
    {
        if (!reference.IsValid()) return kind != resources::DependencyKind::Required;
        if (!reference.IsTyped() || kind > resources::DependencyKind::Soft) return false;
        for (prefab::DependencyRecord& existing : dependencies)
        {
            if (existing.resource == reference)
            {
                if (kind < existing.kind) existing.kind = kind;
                return true;
            }
        }
        const u32 expected = dependencies.Size() + 1u;
        dependencies.PushBack({reference, kind});
        return dependencies.Size() == expected;
    }

    [[nodiscard]] bool DependencyVisitor(const resources::ResourceReference reference,
                                         const resources::DependencyKind kind, void* const userData) noexcept
    {
        auto& dependencies = *static_cast<containers::DynamicArray<prefab::DependencyRecord>*>(userData);
        return AddDependency(dependencies, reference, kind);
    }

    [[nodiscard]] bool AppendBytes(ByteArray& destination, const void* const source, const u32 size) noexcept
    {
        if (size > 0xffffffffu - destination.Size()) return false;
        const u32 offset = destination.Size();
        destination.Resize(offset + size);
        if (destination.Size() != offset + size) return false;
        const auto* bytes = static_cast<const u8*>(source);
        for (u32 index = 0; index < size; ++index) destination[offset + index] = bytes[index];
        return true;
    }

    [[nodiscard]] bool AlignData(ByteArray& data, const u32 alignment) noexcept
    {
        const u32 aligned = (data.Size() + alignment - 1u) & ~(alignment - 1u);
        if (aligned < data.Size()) return false;
        const u32 previous = data.Size();
        data.Resize(aligned);
        if (data.Size() != aligned) return false;
        for (u32 index = previous; index < aligned; ++index) data[index] = 0;
        return true;
    }

    [[nodiscard]] prefab::Result ValidateHierarchy(const containers::DynamicArray<prefab::EntityRecord>& entities) noexcept
    {
        containers::DynamicArray<u8> states(memory::pools::World::GetInstance());
        containers::DynamicArray<u32> path(memory::pools::World::GetInstance());
        states.Resize(entities.Size());
        if (states.Size() != entities.Size()) return prefab::Result::LimitExceeded;
        for (u32 index = 0; index < states.Size(); ++index) states[index] = 0;
        path.Reserve(entities.Size());
        for (u32 start = 0; start < entities.Size(); ++start)
        {
            if (states[start] == 2) continue;
            path.Clear();
            u32 cursor = start;
            for (;;)
            {
                if (states[cursor] == 2) break;
                if (states[cursor] == 1) return prefab::Result::HierarchyCycle;
                states[cursor] = 1;
                const u32 expectedPathSize = path.Size() + 1u;
                path.PushBack(cursor);
                if (path.Size() != expectedPathSize) return prefab::Result::LimitExceeded;
                const u64 parentStableId = entities[cursor].parentStableId;
                if (parentStableId == prefab::InvalidStableId) break;
                const prefab::EntityRecord* const parent = FindEntity(entities, parentStableId);
                if (parent == nullptr) return prefab::Result::MissingParent;
                cursor = static_cast<u32>(parent - entities.TypedData());
            }
            for (const u32 index : path) states[index] = 2;
        }
        return prefab::Result::Success;
    }

    [[nodiscard]] prefab::Result Canonicalize(const prefab::CookDescription& description,
                                              CanonicalData& output) noexcept
    {
        if (description.name == 0 || description.entities.Empty()) return prefab::Result::InvalidArgument;
        output.name = description.name;
        output.sourceFingerprint = description.sourceFingerprint;
        output.entities.Reserve(description.entities.Size());
        for (const prefab::EntityBuildRecord& source : description.entities)
        {
            if (source.stableId == prefab::InvalidStableId ||
                (static_cast<u16>(source.flags) & ~KnownEntityFlags) != 0) return prefab::Result::InvalidArgument;
            if (!description.includeEditorData &&
                (static_cast<u16>(source.flags) & static_cast<u16>(prefab::EntityFlags::EditorOnly)) != 0) continue;
            prefab::EntityFlags flags = source.flags;
            if (source.parentStableId == prefab::InvalidStableId)
                flags = flags | prefab::EntityFlags::Root;
            else
                flags = static_cast<prefab::EntityFlags>(static_cast<u16>(flags) &
                                                         ~static_cast<u16>(prefab::EntityFlags::Root));
            output.entities.PushBack({source.stableId, source.parentStableId, source.name, 0, 0, flags});
        }
        if (output.entities.Empty()) return prefab::Result::InvalidArgument;
        std::sort(output.entities.Begin(), output.entities.End(), EntityLess);
        for (u32 index = 1; index < output.entities.Size(); ++index)
            if (output.entities[index - 1u].stableId == output.entities[index].stableId)
                return prefab::Result::DuplicateIdentifier;
        prefab::Result result = ValidateHierarchy(output.entities);
        if (result != prefab::Result::Success) return result;

        containers::DynamicArray<ComponentSource> sources(memory::pools::World::GetInstance());
        containers::DynamicArray<u64> componentIds(memory::pools::World::GetInstance());
        sources.Reserve(description.components.Size());
        componentIds.Reserve(description.components.Size());
        for (const prefab::ComponentBuildRecord& source : description.components)
        {
            if (source.stableId == prefab::InvalidStableId || source.entityStableId == prefab::InvalidStableId ||
                source.schema == nullptr || source.object == nullptr || !static_cast<bool>(*source.schema) ||
                (static_cast<u16>(source.flags) & ~KnownComponentFlags) != 0) return prefab::Result::InvalidArgument;
            if (!description.includeEditorData &&
                (static_cast<u16>(source.flags) & static_cast<u16>(prefab::ComponentFlags::EditorOnly)) != 0) continue;
            if (FindEntity(output.entities, source.entityStableId) == nullptr) return prefab::Result::MissingParent;
            sources.PushBack({&source});
            componentIds.PushBack(source.stableId);
        }
        std::sort(componentIds.Begin(), componentIds.End());
        for (u32 index = 1; index < componentIds.Size(); ++index)
            if (componentIds[index - 1u] == componentIds[index]) return prefab::Result::DuplicateIdentifier;
        std::sort(sources.Begin(), sources.End(), ComponentSourceLess);

        schemas::WriteOptions writeOptions;
        writeOptions.includeEditorFields = description.includeEditorData;
        for (const ComponentSource& sorted : sources)
        {
            const prefab::ComponentBuildRecord& source = *sorted.source;
            ByteArray bytes(memory::pools::Serialization::GetInstance());
            filesystem::MemoryFileWriter memoryFile(bytes);
            serialization::BinaryWriter writer(memoryFile);
            const schemas::Result schemaResult = schemas::WriteObject(writer, *source.schema, source.object, writeOptions);
            if (schemaResult != schemas::Result::Success) return Convert(schemaResult);
            const schemas::Result dependencyResult =
                schemas::VisitDependencies(*source.schema, source.object, DependencyVisitor, &output.dependencies);
            if (dependencyResult != schemas::Result::Success) return Convert(dependencyResult);
            if (!AlignData(output.componentData, 8)) return prefab::Result::LimitExceeded;
            prefab::ComponentRecord record;
            record.stableId = source.stableId;
            record.entityStableId = source.entityStableId;
            record.schema = source.schema->id;
            record.schemaVersion = source.schema->currentVersion;
            record.flags = source.flags;
            record.dataOffset = output.componentData.Size();
            record.dataSize = bytes.Size();
            record.dataFingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
            if (!AppendBytes(output.componentData, bytes.Data(), bytes.Size())) return prefab::Result::LimitExceeded;
            output.components.PushBack(record);
        }

        u32 componentIndex = 0;
        for (prefab::EntityRecord& entity : output.entities)
        {
            while (componentIndex < output.components.Size() &&
                   output.components[componentIndex].entityStableId < entity.stableId) ++componentIndex;
            entity.firstComponent = componentIndex;
            while (componentIndex < output.components.Size() &&
                   output.components[componentIndex].entityStableId == entity.stableId)
            {
                ++entity.componentCount;
                ++componentIndex;
            }
        }
        if (componentIndex != output.components.Size()) return prefab::Result::InvalidLayout;

        for (const prefab::ExplicitDependency& dependency : description.explicitDependencies)
            if (!AddDependency(output.dependencies, dependency.resource, dependency.kind))
                return prefab::Result::InvalidArgument;
        std::sort(output.dependencies.Begin(), output.dependencies.End(), DependencyLess);
        return prefab::Result::Success;
    }

    [[nodiscard]] bool WriteEntity(serialization::BinaryWriter& writer, const prefab::EntityRecord& value) noexcept
    {
        return writer.WriteU64(value.stableId) && writer.WriteU64(value.parentStableId) && writer.WriteU64(value.name) &&
               writer.WriteU32(value.firstComponent) && writer.WriteU32(value.componentCount) &&
               writer.WriteU16(static_cast<u16>(value.flags)) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadEntity(serialization::BinaryReader& reader, prefab::EntityRecord& value) noexcept
    {
        u16 flags = 0;
        u16 reserved = 0;
        if (!reader.ReadU64(value.stableId) || !reader.ReadU64(value.parentStableId) || !reader.ReadU64(value.name) ||
            !reader.ReadU32(value.firstComponent) || !reader.ReadU32(value.componentCount) || !reader.ReadU16(flags) ||
            !reader.ReadU16(reserved)) return false;
        value.flags = static_cast<prefab::EntityFlags>(flags);
        return reserved == 0;
    }

    [[nodiscard]] bool WriteComponent(serialization::BinaryWriter& writer,
                                      const prefab::ComponentRecord& value) noexcept
    {
        return writer.WriteU64(value.stableId) && writer.WriteU64(value.entityStableId) && writer.WriteU64(value.schema) &&
               writer.WriteU16(value.schemaVersion) && writer.WriteU16(static_cast<u16>(value.flags)) &&
               writer.WriteU64(value.dataOffset) && writer.WriteU64(value.dataSize) && WriteDigest(writer, value.dataFingerprint);
    }

    [[nodiscard]] bool ReadComponent(serialization::BinaryReader& reader, prefab::ComponentRecord& value) noexcept
    {
        u16 flags = 0;
        if (!reader.ReadU64(value.stableId) || !reader.ReadU64(value.entityStableId) || !reader.ReadU64(value.schema) ||
            !reader.ReadU16(value.schemaVersion) || !reader.ReadU16(flags) || !reader.ReadU64(value.dataOffset) ||
            !reader.ReadU64(value.dataSize) || !ReadDigest(reader, value.dataFingerprint)) return false;
        value.flags = static_cast<prefab::ComponentFlags>(flags);
        return true;
    }

    [[nodiscard]] bool WriteDependency(serialization::BinaryWriter& writer,
                                       const prefab::DependencyRecord& value) noexcept
    {
        return WriteReference(writer, value.resource) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(0) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadDependency(serialization::BinaryReader& reader,
                                      prefab::DependencyRecord& value) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!ReadReference(reader, value.resource) || !reader.ReadU8(kind) || !reader.ReadU8(reserved8) ||
            !reader.ReadU16(reserved16)) return false;
        value.kind = static_cast<resources::DependencyKind>(kind);
        return reserved8 == 0 && reserved16 == 0;
    }

    [[nodiscard]] prefab::Result BuildBody(const CanonicalData& data, ByteArray& body,
                                           crypto::Digest256& fingerprint) noexcept
    {
        filesystem::MemoryFileWriter file(body);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU64(data.name) || !WriteDigest(writer, data.sourceFingerprint) ||
            !writer.WriteU32(data.entities.Size()) || !writer.WriteU32(data.components.Size()) ||
            !writer.WriteU32(data.dependencies.Size()) || !writer.WriteU64(data.componentData.Size())) return WriterResult(writer);
        for (const prefab::EntityRecord& entity : data.entities) if (!WriteEntity(writer, entity)) return WriterResult(writer);
        for (const prefab::ComponentRecord& component : data.components) if (!WriteComponent(writer, component)) return WriterResult(writer);
        for (const prefab::DependencyRecord& dependency : data.dependencies) if (!WriteDependency(writer, dependency)) return WriterResult(writer);
        if (!writer.WriteBytes(data.componentData.Data(), data.componentData.Size())) return WriterResult(writer);
        fingerprint = crypto::Sha256(body.Data(), body.Size());
        return prefab::Result::Success;
    }

    [[nodiscard]] prefab::Result WriteDocument(filesystem::IFile& file, const ByteArray& body,
                                               const crypto::Digest256& fingerprint) noexcept
    {
        ByteArray payload(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter payloadFile(payload);
        serialization::BinaryWriter payloadWriter(payloadFile);
        if (!payloadWriter.WriteU32(BodyWireVersion) || !WriteDigest(payloadWriter, fingerprint) ||
            !payloadWriter.WriteBytes(body.Data(), body.Size())) return WriterResult(payloadWriter);

        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = prefab::PrefabMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16)) return WriterResult(writer);
        serialization::SectionDescriptor section;
        section.id = PrefabSection;
        section.version = FileVersion;
        section.alignmentLog2 = 4;
        section.offset = writer.Position();
        section.storedSize = payload.Size();
        section.logicalSize = payload.Size();
        section.storedCrc64 = serialization::Crc64(payload.Data(), payload.Size());
        if (!writer.WriteBytes(payload.Data(), payload.Size()) || !writer.Align(16)) return WriterResult(writer);
        header.sectionTableOffset = writer.Position();
        const serialization::Result sectionResult = serialization::WriteSectionDescriptor(writer, section);
        if (sectionResult != serialization::Result::Success) return Convert(sectionResult);
        header.fileSize = writer.Position();
        if (!writer.Seek(0)) return WriterResult(writer);
        const serialization::Result headerResult = serialization::WriteDocumentHeader(writer, header);
        if (headerResult != serialization::Result::Success || !writer.Seek(header.fileSize) || !writer.Flush())
            return headerResult == serialization::Result::Success ? WriterResult(writer) : Convert(headerResult);
        return prefab::Result::Success;
    }

    template<typename Type>
    [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit) return false;
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] prefab::Result ValidateLoaded(const prefab::PrefabFile& file) noexcept
    {
        const auto entities = file.Entities();
        const auto components = file.Components();
        const auto dependencies = file.Dependencies();
        if (file.Name() == 0 || entities.Empty()) return prefab::Result::InvalidLayout;
        u32 expectedComponent = 0;
        for (u32 index = 0; index < entities.Size(); ++index)
        {
            const prefab::EntityRecord& entity = entities[index];
            const bool root = entity.parentStableId == prefab::InvalidStableId;
            const bool rootFlag = (static_cast<u16>(entity.flags) & static_cast<u16>(prefab::EntityFlags::Root)) != 0;
            if (entity.stableId == prefab::InvalidStableId || (index > 0 && entities[index - 1u].stableId >= entity.stableId) ||
                (static_cast<u16>(entity.flags) & ~KnownEntityFlags) != 0 || root != rootFlag ||
                entity.firstComponent != expectedComponent || entity.componentCount > components.Size() - expectedComponent)
                return prefab::Result::InvalidLayout;
            for (u32 local = 0; local < entity.componentCount; ++local)
                if (components[expectedComponent + local].entityStableId != entity.stableId)
                    return prefab::Result::InvalidLayout;
            expectedComponent += entity.componentCount;
        }
        if (expectedComponent != components.Size()) return prefab::Result::InvalidLayout;
        containers::DynamicArray<prefab::EntityRecord> hierarchy(memory::pools::World::GetInstance());
        hierarchy.Reserve(entities.Size());
        for (const prefab::EntityRecord& entity : entities) hierarchy.PushBack(entity);
        const prefab::Result hierarchyResult = ValidateHierarchy(hierarchy);
        if (hierarchyResult != prefab::Result::Success) return hierarchyResult;
        containers::DynamicArray<u64> componentIds(memory::pools::World::GetInstance());
        componentIds.Reserve(components.Size());
        u64 expectedDataOffset = 0;
        for (u32 index = 0; index < components.Size(); ++index)
        {
            const prefab::ComponentRecord& component = components[index];
            const auto bytes = file.ComponentData(component);
            expectedDataOffset = (expectedDataOffset + 7u) & ~7ull;
            if (component.stableId == prefab::InvalidStableId || component.schema == reflection::InvalidSchemaTypeId ||
                component.schemaVersion == 0 || (static_cast<u16>(component.flags) & ~KnownComponentFlags) != 0 ||
                component.dataSize == 0 || component.dataOffset != expectedDataOffset || bytes.Size() != component.dataSize)
                return prefab::Result::InvalidLayout;
            if (crypto::Sha256(bytes.Data(), bytes.Size()) != component.dataFingerprint)
                return prefab::Result::IntegrityFailure;
            if (index > 0 && components[index - 1u].entityStableId == component.entityStableId &&
                components[index - 1u].stableId >= component.stableId) return prefab::Result::InvalidLayout;
            componentIds.PushBack(component.stableId);
            if (componentIds.Size() != index + 1u) return prefab::Result::LimitExceeded;
            expectedDataOffset += component.dataSize;
        }
        std::sort(componentIds.Begin(), componentIds.End());
        for (u32 index = 1; index < componentIds.Size(); ++index)
            if (componentIds[index - 1u] == componentIds[index]) return prefab::Result::DuplicateIdentifier;
        for (u32 index = 0; index < dependencies.Size(); ++index)
        {
            const prefab::DependencyRecord& dependency = dependencies[index];
            if (!dependency.resource.IsValid() || !dependency.resource.IsTyped() ||
                dependency.kind > resources::DependencyKind::Soft ||
                (index > 0 && !DependencyLess(dependencies[index - 1u], dependency)))
                return prefab::Result::InvalidLayout;
        }
        return prefab::Result::Success;
    }
} // namespace

namespace vanguard::prefabs
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success: return "Success";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::InvalidState: return "InvalidState";
        case Result::InvalidMagic: return "InvalidMagic";
        case Result::UnsupportedVersion: return "UnsupportedVersion";
        case Result::InvalidLayout: return "InvalidLayout";
        case Result::IntegrityFailure: return "IntegrityFailure";
        case Result::LimitExceeded: return "LimitExceeded";
        case Result::DuplicateIdentifier: return "DuplicateIdentifier";
        case Result::MissingParent: return "MissingParent";
        case Result::HierarchyCycle: return "HierarchyCycle";
        case Result::UnknownSchema: return "UnknownSchema";
        case Result::SchemaFailure: return "SchemaFailure";
        case Result::IoFailure: return "IoFailure";
        }
        return "Unknown";
    }

    PrefabFile::PrefabFile() noexcept
        : m_entities(memory::pools::World::GetInstance()), m_components(memory::pools::World::GetInstance()),
          m_dependencies(memory::pools::Resources::GetInstance()), m_componentData(memory::pools::World::GetInstance())
    {
    }

    Result PrefabFile::Open(filesystem::IFile& file, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 1;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(reader, PrefabMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success) return Convert(headerResult);
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success) return Convert(sectionResult);
        if (sections.Size() != 1 || sections[0].id != PrefabSection || sections[0].version != FileVersion ||
            sections[0].codec != serialization::Codec::None || sections[0].storedSize != sections[0].logicalSize ||
            sections[0].storedSize > limits.maximumFileSize || sections[0].storedSize > 0xffffffffull)
            return Result::InvalidLayout;
        ByteArray payload(memory::pools::Serialization::GetInstance());
        payload.Resize(static_cast<u32>(sections[0].storedSize));
        if (payload.Size() != sections[0].storedSize) return Result::LimitExceeded;
        if (!reader.Seek(sections[0].offset) || !reader.ReadBytes(payload.Data(), payload.Size())) return ReaderResult(reader);
        if (serialization::Crc64(payload.Data(), payload.Size()) != sections[0].storedCrc64) return Result::IntegrityFailure;
        if (payload.Size() < BodyPrefixSize) return Result::InvalidLayout;
        filesystem::MemoryFileReader payloadFile(payload, 0);
        serialization::BinaryReader bodyReader(payloadFile);
        u32 bodyVersion = 0;
        if (!bodyReader.ReadU32(bodyVersion) || !ReadDigest(bodyReader, m_contentFingerprint)) return ReaderResult(bodyReader);
        if (bodyVersion != BodyWireVersion) return Result::UnsupportedVersion;
        if (crypto::Sha256(payload.TypedData() + BodyPrefixSize, payload.Size() - BodyPrefixSize) != m_contentFingerprint)
            return Result::IntegrityFailure;
        u32 entityCount = 0;
        u32 componentCount = 0;
        u32 dependencyCount = 0;
        u64 componentDataSize = 0;
        if (!bodyReader.ReadU64(m_name) || !ReadDigest(bodyReader, m_sourceFingerprint) ||
            !bodyReader.ReadU32(entityCount) || !bodyReader.ReadU32(componentCount) ||
            !bodyReader.ReadU32(dependencyCount) || !bodyReader.ReadU64(componentDataSize)) return ReaderResult(bodyReader);
        const u32 maximumComponentData = limits.maximumComponentDataBytes > 0xffffffffull
                                             ? 0xffffffffu
                                             : static_cast<u32>(limits.maximumComponentDataBytes);
        if (componentDataSize > limits.maximumComponentDataBytes || componentDataSize > 0xffffffffull ||
            !ResizeChecked(m_entities, entityCount, limits.maximumEntities) ||
            !ResizeChecked(m_components, componentCount, limits.maximumComponents) ||
            !ResizeChecked(m_dependencies, dependencyCount, limits.maximumDependencies) ||
            !ResizeChecked(m_componentData, static_cast<u32>(componentDataSize), maximumComponentData))
            return Result::LimitExceeded;
        for (EntityRecord& entity : m_entities) if (!ReadEntity(bodyReader, entity)) return Result::InvalidLayout;
        for (ComponentRecord& component : m_components) if (!ReadComponent(bodyReader, component)) return Result::InvalidLayout;
        for (DependencyRecord& dependency : m_dependencies) if (!ReadDependency(bodyReader, dependency)) return Result::InvalidLayout;
        if (!bodyReader.ReadBytes(m_componentData.Data(), m_componentData.Size())) return ReaderResult(bodyReader);
        if (bodyReader.Position() != bodyReader.Size()) return Result::InvalidLayout;
        m_open = true;
        const Result validation = ValidateLoaded(*this);
        if (validation != Result::Success) Close();
        return validation;
    }

    void PrefabFile::Close() noexcept
    {
        m_name = 0;
        m_sourceFingerprint = {};
        m_contentFingerprint = {};
        m_entities.Clear();
        m_components.Clear();
        m_dependencies.Clear();
        m_componentData.Clear();
        m_open = false;
    }

    bool PrefabFile::IsOpen() const noexcept { return m_open; }
    u64 PrefabFile::Name() const noexcept { return m_name; }
    const crypto::Digest256& PrefabFile::SourceFingerprint() const noexcept { return m_sourceFingerprint; }
    const crypto::Digest256& PrefabFile::ContentFingerprint() const noexcept { return m_contentFingerprint; }
    containers::ArraySpan<const EntityRecord> PrefabFile::Entities() const noexcept { return m_entities; }
    containers::ArraySpan<const ComponentRecord> PrefabFile::Components() const noexcept { return m_components; }
    containers::ArraySpan<const DependencyRecord> PrefabFile::Dependencies() const noexcept { return m_dependencies; }

    containers::ArraySpan<const u8> PrefabFile::ComponentData(const ComponentRecord& component) const noexcept
    {
        if (!m_open || component.dataOffset > m_componentData.Size() ||
            component.dataSize > m_componentData.Size() - component.dataOffset || component.dataSize > 0xffffffffull) return {};
        return {m_componentData.TypedData() + component.dataOffset, static_cast<u32>(component.dataSize)};
    }

    const EntityRecord* PrefabFile::FindEntity(const u64 stableId) const noexcept
    {
        return m_open ? ::FindEntity(m_entities, stableId) : nullptr;
    }

    const ComponentRecord* PrefabFile::FindComponent(const u64 stableId) const noexcept
    {
        if (!m_open || stableId == InvalidStableId) return nullptr;
        for (const ComponentRecord& component : m_components) if (component.stableId == stableId) return &component;
        return nullptr;
    }

    resources::ResourceTypeId PrefabResource::Type() const noexcept { return PrefabResourceType; }

    Result PrefabResource::Open(const void* const data, const usize size, const ReadLimits& limits) noexcept
    {
        if (data == nullptr || size == 0 || size > static_cast<usize>(~u32{0})) return Result::InvalidArgument;
        filesystem::MemoryFileReader reader(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        return m_file.Open(reader, limits);
    }

    const PrefabFile& PrefabResource::File() const noexcept { return m_file; }

    Result CookPrefab(const CookDescription& description, filesystem::IFile& output) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success) return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        crypto::Digest256 fingerprint;
        result = BuildBody(canonical, body, fingerprint);
        return result == Result::Success ? WriteDocument(output, body, fingerprint) : result;
    }

    Result CalculateContentFingerprint(const CookDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success) return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        return BuildBody(canonical, body, fingerprint);
    }
} // namespace vanguard::prefabs
