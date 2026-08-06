#include <vanguard/world/cells.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace vanguard;
    namespace cell = vanguard::world;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 CellSection = serialization::MakeFourCC('C', 'E', 'L', 'L');
    constexpr u32 BodyWireVersion = 1;
    constexpr u32 BodyPrefixSize = 36;
    constexpr u16 KnownGroupFlags = static_cast<u16>(cell::ActivationGroupFlags::DefaultActive) |
                                    static_cast<u16>(cell::ActivationGroupFlags::EditorOnly);
    constexpr u32 KnownPlacementFlags = static_cast<u32>(cell::PlacementFlags::Persistent) |
                                        static_cast<u32>(cell::PlacementFlags::InitiallyDisabled) |
                                        static_cast<u32>(cell::PlacementFlags::EditorOnly) |
                                        static_cast<u32>(cell::PlacementFlags::NoCollision) |
                                        static_cast<u32>(cell::PlacementFlags::Interior) |
                                        static_cast<u32>(cell::PlacementFlags::Mission) |
                                        static_cast<u32>(cell::PlacementFlags::Decoration) |
                                        static_cast<u32>(cell::PlacementFlags::TwoDimensionalStreaming) |
                                        static_cast<u32>(cell::PlacementFlags::AllowDistanceBoosting) |
                                        static_cast<u32>(cell::PlacementFlags::PersistTransform);
    constexpr u16 KnownOverrideFlags = static_cast<u16>(cell::OverrideFlags::EditorOnly);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : groups(memory::pools::World::GetInstance()), placements(memory::pools::World::GetInstance()),
              lookup(memory::pools::World::GetInstance()), overrides(memory::pools::World::GetInstance()),
              references(memory::pools::World::GetInstance()), dependencies(memory::pools::Resources::GetInstance()),
              overrideData(memory::pools::World::GetInstance())
        {
        }

        u64 cellId = 0;
        u64 worldId = 0;
        i32 gridCoordinate[3]{};
        u8 hierarchyLevel = 0;
        cell::CellCategory category = cell::CellCategory::Generic;
        f64 origin[3]{};
        cell::Bounds bounds;
        crypto::Digest256 sourceFingerprint;
        containers::DynamicArray<cell::ActivationGroupRecord> groups;
        containers::DynamicArray<cell::PlacementRecord> placements;
        containers::DynamicArray<cell::EntityLookupRecord> lookup;
        containers::DynamicArray<cell::ComponentOverrideRecord> overrides;
        containers::DynamicArray<cell::EntityReferenceRecord> references;
        containers::DynamicArray<cell::DependencyRecord> dependencies;
        ByteArray overrideData;
    };

    struct OverrideSource
    {
        const cell::ComponentOverrideBuildRecord* source = nullptr;
        u32 placementIndex = 0;
    };

    struct ReferenceSource
    {
        cell::EntityReferenceBuildRecord source;
        u32 placementIndex = 0;
    };

    [[nodiscard]] cell::Result Convert(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success: return cell::Result::Success;
        case serialization::Result::InvalidMagic: return cell::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
        case serialization::Result::UnsupportedByteOrder:
        case serialization::Result::UnsupportedHeader: return cell::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure: return cell::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow: return cell::Result::LimitExceeded;
        case serialization::Result::InvalidArgument: return cell::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream: return cell::Result::IoFailure;
        default: return cell::Result::InvalidLayout;
        }
    }

    [[nodiscard]] cell::Result Convert(const schemas::Result result) noexcept
    {
        switch (result)
        {
        case schemas::Result::Success: return cell::Result::Success;
        case schemas::Result::SchemaNotFound: return cell::Result::UnknownSchema;
        case schemas::Result::LimitExceeded: return cell::Result::LimitExceeded;
        case schemas::Result::IoFailure: return cell::Result::IoFailure;
        default: return cell::Result::SchemaFailure;
        }
    }

    [[nodiscard]] cell::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? cell::Result::Success : Convert(writer.Status());
    }

    [[nodiscard]] cell::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? cell::Result::Success : Convert(reader.Status());
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool WriteReference(serialization::BinaryWriter& writer, resources::ResourceReference value) noexcept
    {
        return writer.WriteU64(value.Path().Id()) && writer.WriteU32(value.ExpectedType());
    }

    [[nodiscard]] bool ReadReference(serialization::BinaryReader& reader, resources::ResourceReference& value) noexcept
    {
        u64 path = 0;
        u32 type = 0;
        if (!reader.ReadU64(path) || !reader.ReadU32(type)) return false;
        value = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
        return true;
    }

    [[nodiscard]] bool WriteBounds(serialization::BinaryWriter& writer, const cell::Bounds& value) noexcept
    {
        for (const f32 coordinate : value.minimum) if (!writer.WriteF32(coordinate)) return false;
        for (const f32 coordinate : value.maximum) if (!writer.WriteF32(coordinate)) return false;
        return true;
    }

    [[nodiscard]] bool ReadBounds(serialization::BinaryReader& reader, cell::Bounds& value) noexcept
    {
        for (f32& coordinate : value.minimum) if (!reader.ReadF32(coordinate)) return false;
        for (f32& coordinate : value.maximum) if (!reader.ReadF32(coordinate)) return false;
        return true;
    }

    [[nodiscard]] bool WriteTransform(serialization::BinaryWriter& writer, const cell::PlacementTransform& value) noexcept
    {
        for (const f32 coordinate : value.translation) if (!writer.WriteF32(coordinate)) return false;
        for (const f32 coordinate : value.rotation) if (!writer.WriteF32(coordinate)) return false;
        for (const f32 coordinate : value.scale) if (!writer.WriteF32(coordinate)) return false;
        return true;
    }

    [[nodiscard]] bool ReadTransform(serialization::BinaryReader& reader, cell::PlacementTransform& value) noexcept
    {
        for (f32& coordinate : value.translation) if (!reader.ReadF32(coordinate)) return false;
        for (f32& coordinate : value.rotation) if (!reader.ReadF32(coordinate)) return false;
        for (f32& coordinate : value.scale) if (!reader.ReadF32(coordinate)) return false;
        return true;
    }

    [[nodiscard]] bool IsFiniteBounds(const cell::Bounds& bounds) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                bounds.minimum[axis] > bounds.maximum[axis]) return false;
        return true;
    }

    [[nodiscard]] bool IsValidTransform(const cell::PlacementTransform& transform) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
            if (!std::isfinite(transform.translation[axis]) || !std::isfinite(transform.scale[axis]) ||
                transform.scale[axis] == 0.0f) return false;
        f32 lengthSquared = 0.0f;
        for (const f32 component : transform.rotation)
        {
            if (!std::isfinite(component)) return false;
            lengthSquared += component * component;
        }
        return lengthSquared >= 0.999f && lengthSquared <= 1.001f;
    }

    [[nodiscard]] bool GroupLess(const cell::ActivationGroupRecord& left, const cell::ActivationGroupRecord& right) noexcept
    {
        return left.stableId < right.stableId;
    }

    [[nodiscard]] bool PlacementLess(const cell::PlacementRecord& left, const cell::PlacementRecord& right) noexcept
    {
        if (left.activationGroup != right.activationGroup) return left.activationGroup < right.activationGroup;
        return left.entityId < right.entityId;
    }

    [[nodiscard]] bool LookupLess(const cell::EntityLookupRecord& left, const cell::EntityLookupRecord& right) noexcept
    {
        return left.entityId < right.entityId;
    }

    [[nodiscard]] bool OverrideSourceLess(const OverrideSource& left, const OverrideSource& right) noexcept
    {
        if (left.placementIndex != right.placementIndex) return left.placementIndex < right.placementIndex;
        return left.source->componentStableId < right.source->componentStableId;
    }

    [[nodiscard]] bool ReferenceSourceLess(const ReferenceSource& left, const ReferenceSource& right) noexcept
    {
        if (left.placementIndex != right.placementIndex) return left.placementIndex < right.placementIndex;
        if (left.source.slot != right.source.slot) return left.source.slot < right.source.slot;
        return left.source.targetEntityId < right.source.targetEntityId;
    }

    [[nodiscard]] bool DependencyLess(const cell::DependencyRecord& left, const cell::DependencyRecord& right) noexcept
    {
        if (left.resource.Path() != right.resource.Path()) return left.resource.Path() < right.resource.Path();
        return left.resource.ExpectedType() < right.resource.ExpectedType();
    }

    [[nodiscard]] const cell::DependencyRecord* FindDependency(
        const containers::ArraySpan<const cell::DependencyRecord> dependencies,
        const resources::ResourceReference resource) noexcept
    {
        u32 first = 0;
        u32 count = dependencies.Size();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            const cell::DependencyRecord& candidate = dependencies[index];
            if (candidate.resource.Path() < resource.Path() ||
                (candidate.resource.Path() == resource.Path() && candidate.resource.ExpectedType() < resource.ExpectedType()))
            {
                first = index + 1u;
                count -= step + 1u;
            }
            else count = step;
        }
        return first < dependencies.Size() && dependencies[first].resource == resource ? &dependencies[first] : nullptr;
    }

    [[nodiscard]] const cell::ActivationGroupRecord* FindGroup(const containers::DynamicArray<cell::ActivationGroupRecord>& groups,
                                                              const u64 stableId) noexcept
    {
        u32 first = 0;
        u32 count = groups.Size();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            if (groups[index].stableId < stableId) { first = index + 1u; count -= step + 1u; }
            else count = step;
        }
        return first < groups.Size() && groups[first].stableId == stableId ? &groups[first] : nullptr;
    }

    [[nodiscard]] const cell::EntityLookupRecord* FindLookup(const containers::DynamicArray<cell::EntityLookupRecord>& lookup,
                                                             const u64 entityId) noexcept
    {
        u32 first = 0;
        u32 count = lookup.Size();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            if (lookup[index].entityId < entityId) { first = index + 1u; count -= step + 1u; }
            else count = step;
        }
        return first < lookup.Size() && lookup[first].entityId == entityId ? &lookup[first] : nullptr;
    }

    [[nodiscard]] bool AddDependency(containers::DynamicArray<cell::DependencyRecord>& dependencies,
                                     resources::ResourceReference reference, resources::DependencyKind kind) noexcept
    {
        if (!reference.IsValid()) return kind != resources::DependencyKind::Required;
        if (!reference.IsTyped() || kind > resources::DependencyKind::Soft) return false;
        for (cell::DependencyRecord& existing : dependencies)
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

    [[nodiscard]] bool DependencyVisitor(resources::ResourceReference reference, resources::DependencyKind kind,
                                         void* userData) noexcept
    {
        return AddDependency(*static_cast<containers::DynamicArray<cell::DependencyRecord>*>(userData), reference, kind);
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

    [[nodiscard]] bool AppendBytes(ByteArray& destination, const void* source, const u32 size) noexcept
    {
        if (size > 0xffffffffu - destination.Size()) return false;
        const u32 offset = destination.Size();
        destination.Resize(offset + size);
        if (destination.Size() != offset + size) return false;
        const auto* bytes = static_cast<const u8*>(source);
        for (u32 index = 0; index < size; ++index) destination[offset + index] = bytes[index];
        return true;
    }

    [[nodiscard]] cell::Result ValidateHierarchy(const containers::DynamicArray<cell::PlacementRecord>& placements,
                                                 const containers::DynamicArray<cell::EntityLookupRecord>& lookup) noexcept
    {
        containers::DynamicArray<u8> states(memory::pools::World::GetInstance());
        containers::DynamicArray<u32> path(memory::pools::World::GetInstance());
        states.Resize(placements.Size());
        if (states.Size() != placements.Size()) return cell::Result::LimitExceeded;
        for (u32 index = 0; index < states.Size(); ++index) states[index] = 0;
        path.Reserve(placements.Size());
        for (u32 start = 0; start < placements.Size(); ++start)
        {
            if (states[start] == 2) continue;
            path.Clear();
            u32 cursor = start;
            for (;;)
            {
                if (states[cursor] == 2) break;
                if (states[cursor] == 1) return cell::Result::HierarchyCycle;
                states[cursor] = 1;
                const u32 expected = path.Size() + 1u;
                path.PushBack(cursor);
                if (path.Size() != expected) return cell::Result::LimitExceeded;
                const u64 parentId = placements[cursor].parentEntityId;
                if (parentId == cell::InvalidEntityId) break;
                const cell::EntityLookupRecord* parentLookup = FindLookup(lookup, parentId);
                if (parentLookup == nullptr) return cell::Result::MissingParent;
                const cell::PlacementRecord& parent = placements[parentLookup->placementIndex];
                if (parent.activationGroup != cell::AlwaysActiveGroup &&
                    parent.activationGroup != placements[cursor].activationGroup) return cell::Result::InvalidLayout;
                cursor = parentLookup->placementIndex;
            }
            for (const u32 index : path) states[index] = 2;
        }
        return cell::Result::Success;
    }

    [[nodiscard]] cell::Result Canonicalize(const cell::CellBuildDescription& description, CanonicalData& output) noexcept
    {
        if (description.cellId == 0 || description.worldId == 0 || description.placements.Empty() ||
            description.category > cell::CellCategory::Generic || !IsFiniteBounds(description.bounds)) return cell::Result::InvalidArgument;
        for (const f64 coordinate : description.origin) if (!std::isfinite(coordinate)) return cell::Result::InvalidArgument;
        output.cellId = description.cellId;
        output.worldId = description.worldId;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            output.gridCoordinate[axis] = description.gridCoordinate[axis];
            output.origin[axis] = description.origin[axis];
        }
        output.hierarchyLevel = description.hierarchyLevel;
        output.category = description.category;
        output.bounds = description.bounds;
        output.sourceFingerprint = description.sourceFingerprint;

        output.groups.PushBack({cell::AlwaysActiveGroup, 0, 0, 0, cell::ActivationGroupFlags::DefaultActive});
        for (const cell::ActivationGroupBuildRecord& source : description.activationGroups)
        {
            if (source.stableId == cell::AlwaysActiveGroup || (static_cast<u16>(source.flags) & ~KnownGroupFlags) != 0)
                return cell::Result::InvalidArgument;
            if (!description.includeEditorData && (static_cast<u16>(source.flags) &
                static_cast<u16>(cell::ActivationGroupFlags::EditorOnly)) != 0) continue;
            output.groups.PushBack({source.stableId, source.name, 0, 0, source.flags});
        }
        std::sort(output.groups.Begin(), output.groups.End(), GroupLess);
        for (u32 index = 1; index < output.groups.Size(); ++index)
            if (output.groups[index - 1u].stableId == output.groups[index].stableId) return cell::Result::DuplicateIdentifier;

        output.placements.Reserve(description.placements.Size());
        for (const cell::PlacementBuildRecord& source : description.placements)
        {
            if (source.entityId == cell::InvalidEntityId || !source.prefab.IsValid() ||
                source.prefab.ExpectedType() != prefabs::PrefabResourceType || !IsValidTransform(source.transform) ||
                !IsFiniteBounds(source.bounds) || !std::isfinite(source.streamingDistance) || source.streamingDistance < 0.0f ||
                !std::isfinite(source.visibilityDistance) || source.visibilityDistance < 0.0f ||
                (static_cast<u32>(source.flags) & ~KnownPlacementFlags) != 0) return cell::Result::InvalidArgument;
            for (const f32 coordinate : source.streamingReferencePoint)
                if (!std::isfinite(coordinate)) return cell::Result::InvalidArgument;
            if (!description.includeEditorData && (static_cast<u32>(source.flags) &
                static_cast<u32>(cell::PlacementFlags::EditorOnly)) != 0) continue;
            if (FindGroup(output.groups, source.activationGroup) == nullptr) return cell::Result::UnknownActivationGroup;
            cell::PlacementRecord record;
            record.entityId = source.entityId;
            record.parentEntityId = source.parentEntityId;
            record.name = source.name;
            record.activationGroup = source.activationGroup;
            record.prefab = source.prefab;
            record.transform = source.transform;
            record.bounds = source.bounds;
            for (u32 axis = 0; axis < 3; ++axis) record.streamingReferencePoint[axis] = source.streamingReferencePoint[axis];
            record.streamingDistance = source.streamingDistance;
            record.visibilityDistance = source.visibilityDistance;
            record.layerMask = source.layerMask;
            record.flags = source.flags;
            record.timeOfDayVisibilityMask = source.timeOfDayVisibilityMask;
            record.streamingPriority = source.streamingPriority;
            output.placements.PushBack(record);
            if (!AddDependency(output.dependencies, source.prefab, resources::DependencyKind::Required))
                return cell::Result::LimitExceeded;
        }
        if (output.placements.Empty()) return cell::Result::InvalidArgument;
        std::sort(output.placements.Begin(), output.placements.End(), PlacementLess);
        output.lookup.Reserve(output.placements.Size());
        for (u32 index = 0; index < output.placements.Size(); ++index) output.lookup.PushBack({output.placements[index].entityId, index});
        std::sort(output.lookup.Begin(), output.lookup.End(), LookupLess);
        for (u32 index = 1; index < output.lookup.Size(); ++index)
            if (output.lookup[index - 1u].entityId == output.lookup[index].entityId) return cell::Result::DuplicateIdentifier;
        cell::Result result = ValidateHierarchy(output.placements, output.lookup);
        if (result != cell::Result::Success) return result;

        u32 placementIndex = 0;
        for (cell::ActivationGroupRecord& group : output.groups)
        {
            group.firstPlacement = placementIndex;
            while (placementIndex < output.placements.Size() && output.placements[placementIndex].activationGroup == group.stableId)
            {
                ++group.placementCount;
                ++placementIndex;
            }
        }
        if (placementIndex != output.placements.Size()) return cell::Result::InvalidLayout;

        containers::DynamicArray<OverrideSource> overrideSources(memory::pools::World::GetInstance());
        for (const cell::ComponentOverrideBuildRecord& source : description.overrides)
        {
            if (source.entityId == cell::InvalidEntityId || source.componentStableId == prefabs::InvalidStableId ||
                source.mode > cell::OverrideMode::Remove || (static_cast<u16>(source.flags) & ~KnownOverrideFlags) != 0)
                return cell::Result::InvalidArgument;
            if (!description.includeEditorData && (static_cast<u16>(source.flags) &
                static_cast<u16>(cell::OverrideFlags::EditorOnly)) != 0) continue;
            const cell::EntityLookupRecord* lookup = FindLookup(output.lookup, source.entityId);
            if (lookup == nullptr) return cell::Result::InvalidReference;
            if (source.mode == cell::OverrideMode::Remove)
            {
                if (source.schema != nullptr || source.object != nullptr) return cell::Result::InvalidArgument;
            }
            else if (source.schema == nullptr || source.object == nullptr || !static_cast<bool>(*source.schema))
                return cell::Result::InvalidArgument;
            overrideSources.PushBack({&source, lookup->placementIndex});
        }
        std::sort(overrideSources.Begin(), overrideSources.End(), OverrideSourceLess);
        schemas::WriteOptions writeOptions;
        writeOptions.includeEditorFields = description.includeEditorData;
        for (u32 index = 0; index < overrideSources.Size(); ++index)
        {
            const OverrideSource& sorted = overrideSources[index];
            const cell::ComponentOverrideBuildRecord& source = *sorted.source;
            if (index > 0 && overrideSources[index - 1u].placementIndex == sorted.placementIndex &&
                overrideSources[index - 1u].source->componentStableId == source.componentStableId)
                return cell::Result::DuplicateIdentifier;
            cell::ComponentOverrideRecord record;
            record.entityId = source.entityId;
            record.componentStableId = source.componentStableId;
            record.flags = source.flags;
            record.mode = source.mode;
            if (source.mode != cell::OverrideMode::Remove)
            {
                ByteArray bytes(memory::pools::Serialization::GetInstance());
                filesystem::MemoryFileWriter memoryFile(bytes);
                serialization::BinaryWriter writer(memoryFile);
                const schemas::Result schemaResult = schemas::WriteObject(writer, *source.schema, source.object, writeOptions);
                if (schemaResult != schemas::Result::Success) return Convert(schemaResult);
                const schemas::Result dependencyResult =
                    schemas::VisitDependencies(*source.schema, source.object, DependencyVisitor, &output.dependencies);
                if (dependencyResult != schemas::Result::Success) return Convert(dependencyResult);
                if (!AlignData(output.overrideData, 8)) return cell::Result::LimitExceeded;
                record.schema = source.schema->id;
                record.schemaVersion = source.schema->currentVersion;
                record.dataOffset = output.overrideData.Size();
                record.dataSize = bytes.Size();
                record.dataFingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
                if (!AppendBytes(output.overrideData, bytes.Data(), bytes.Size())) return cell::Result::LimitExceeded;
            }
            output.overrides.PushBack(record);
        }
        u32 overrideIndex = 0;
        for (cell::PlacementRecord& placement : output.placements)
        {
            placement.firstOverride = overrideIndex;
            while (overrideIndex < output.overrides.Size() && output.overrides[overrideIndex].entityId == placement.entityId)
            {
                ++placement.overrideCount;
                ++overrideIndex;
            }
        }
        if (overrideIndex != output.overrides.Size()) return cell::Result::InvalidLayout;

        containers::DynamicArray<ReferenceSource> referenceSources(memory::pools::World::GetInstance());
        for (const cell::EntityReferenceBuildRecord& source : description.entityReferences)
        {
            if (source.sourceEntityId == cell::InvalidEntityId || source.targetEntityId == cell::InvalidEntityId ||
                source.slot == 0 || source.kind > cell::EntityReferenceKind::OptionalWorld) return cell::Result::InvalidArgument;
            const cell::EntityLookupRecord* sourceLookup = FindLookup(output.lookup, source.sourceEntityId);
            if (sourceLookup == nullptr) return cell::Result::InvalidReference;
            if ((source.kind == cell::EntityReferenceKind::RequiredLocal ||
                 source.kind == cell::EntityReferenceKind::OptionalLocal) && FindLookup(output.lookup, source.targetEntityId) == nullptr)
                return cell::Result::InvalidReference;
            referenceSources.PushBack({source, sourceLookup->placementIndex});
        }
        std::sort(referenceSources.Begin(), referenceSources.End(), ReferenceSourceLess);
        for (u32 index = 0; index < referenceSources.Size(); ++index)
        {
            if (index > 0 && referenceSources[index - 1u].placementIndex == referenceSources[index].placementIndex &&
                referenceSources[index - 1u].source.slot == referenceSources[index].source.slot)
                return cell::Result::DuplicateIdentifier;
            const auto& source = referenceSources[index].source;
            output.references.PushBack({source.sourceEntityId, source.slot, source.targetEntityId, source.kind});
        }
        u32 referenceIndex = 0;
        for (cell::PlacementRecord& placement : output.placements)
        {
            placement.firstReference = referenceIndex;
            while (referenceIndex < output.references.Size() &&
                   output.references[referenceIndex].sourceEntityId == placement.entityId)
            {
                ++placement.referenceCount;
                ++referenceIndex;
            }
        }
        if (referenceIndex != output.references.Size()) return cell::Result::InvalidLayout;

        for (const cell::ExplicitDependency& dependency : description.explicitDependencies)
            if (!AddDependency(output.dependencies, dependency.resource, dependency.kind)) return cell::Result::InvalidArgument;
        std::sort(output.dependencies.Begin(), output.dependencies.End(), DependencyLess);
        return cell::Result::Success;
    }

    [[nodiscard]] bool WriteGroup(serialization::BinaryWriter& writer, const cell::ActivationGroupRecord& value) noexcept
    {
        return writer.WriteU64(value.stableId) && writer.WriteU64(value.name) && writer.WriteU32(value.firstPlacement) &&
               writer.WriteU32(value.placementCount) && writer.WriteU16(static_cast<u16>(value.flags)) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadGroup(serialization::BinaryReader& reader, cell::ActivationGroupRecord& value) noexcept
    {
        u16 flags = 0;
        u16 reserved = 0;
        if (!reader.ReadU64(value.stableId) || !reader.ReadU64(value.name) || !reader.ReadU32(value.firstPlacement) ||
            !reader.ReadU32(value.placementCount) || !reader.ReadU16(flags) || !reader.ReadU16(reserved)) return false;
        value.flags = static_cast<cell::ActivationGroupFlags>(flags);
        return reserved == 0;
    }

    [[nodiscard]] bool WritePlacement(serialization::BinaryWriter& writer, const cell::PlacementRecord& value) noexcept
    {
        if (!writer.WriteU64(value.entityId) || !writer.WriteU64(value.parentEntityId) || !writer.WriteU64(value.name) ||
            !writer.WriteU64(value.activationGroup) || !WriteReference(writer, value.prefab) || !WriteTransform(writer, value.transform) ||
            !WriteBounds(writer, value.bounds)) return false;
        for (const f32 coordinate : value.streamingReferencePoint) if (!writer.WriteF32(coordinate)) return false;
        return writer.WriteF32(value.streamingDistance) && writer.WriteF32(value.visibilityDistance) && writer.WriteU64(value.layerMask) &&
               writer.WriteU32(value.firstOverride) && writer.WriteU32(value.overrideCount) && writer.WriteU32(value.firstReference) &&
               writer.WriteU32(value.referenceCount) && writer.WriteU32(static_cast<u32>(value.flags)) &&
               writer.WriteU16(value.timeOfDayVisibilityMask) && writer.WriteU8(value.streamingPriority) && writer.WriteU8(0);
    }

    [[nodiscard]] bool ReadPlacement(serialization::BinaryReader& reader, cell::PlacementRecord& value) noexcept
    {
        u32 flags = 0;
        u8 reserved = 0;
        if (!reader.ReadU64(value.entityId) || !reader.ReadU64(value.parentEntityId) || !reader.ReadU64(value.name) ||
            !reader.ReadU64(value.activationGroup) || !ReadReference(reader, value.prefab) || !ReadTransform(reader, value.transform) ||
            !ReadBounds(reader, value.bounds)) return false;
        for (f32& coordinate : value.streamingReferencePoint) if (!reader.ReadF32(coordinate)) return false;
        if (!reader.ReadF32(value.streamingDistance) || !reader.ReadF32(value.visibilityDistance) || !reader.ReadU64(value.layerMask) ||
            !reader.ReadU32(value.firstOverride) || !reader.ReadU32(value.overrideCount) || !reader.ReadU32(value.firstReference) ||
            !reader.ReadU32(value.referenceCount) || !reader.ReadU32(flags) || !reader.ReadU16(value.timeOfDayVisibilityMask) ||
            !reader.ReadU8(value.streamingPriority) || !reader.ReadU8(reserved)) return false;
        value.flags = static_cast<cell::PlacementFlags>(flags);
        return reserved == 0;
    }

    [[nodiscard]] bool WriteLookup(serialization::BinaryWriter& writer, const cell::EntityLookupRecord& value) noexcept
    {
        return writer.WriteU64(value.entityId) && writer.WriteU32(value.placementIndex) && writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadLookup(serialization::BinaryReader& reader, cell::EntityLookupRecord& value) noexcept
    {
        u32 reserved = 0;
        return reader.ReadU64(value.entityId) && reader.ReadU32(value.placementIndex) && reader.ReadU32(reserved) && reserved == 0;
    }

    [[nodiscard]] bool WriteOverride(serialization::BinaryWriter& writer, const cell::ComponentOverrideRecord& value) noexcept
    {
        return writer.WriteU64(value.entityId) && writer.WriteU64(value.componentStableId) && writer.WriteU64(value.schema) &&
               writer.WriteU16(value.schemaVersion) && writer.WriteU16(static_cast<u16>(value.flags)) &&
               writer.WriteU8(static_cast<u8>(value.mode)) && writer.WriteU8(0) && writer.WriteU16(0) &&
               writer.WriteU64(value.dataOffset) && writer.WriteU64(value.dataSize) && WriteDigest(writer, value.dataFingerprint);
    }

    [[nodiscard]] bool ReadOverride(serialization::BinaryReader& reader, cell::ComponentOverrideRecord& value) noexcept
    {
        u16 flags = 0;
        u8 mode = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU64(value.entityId) || !reader.ReadU64(value.componentStableId) || !reader.ReadU64(value.schema) ||
            !reader.ReadU16(value.schemaVersion) || !reader.ReadU16(flags) || !reader.ReadU8(mode) ||
            !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) || !reader.ReadU64(value.dataOffset) ||
            !reader.ReadU64(value.dataSize) || !ReadDigest(reader, value.dataFingerprint)) return false;
        value.flags = static_cast<cell::OverrideFlags>(flags);
        value.mode = static_cast<cell::OverrideMode>(mode);
        return reserved8 == 0 && reserved16 == 0;
    }

    [[nodiscard]] bool WriteEntityReference(serialization::BinaryWriter& writer, const cell::EntityReferenceRecord& value) noexcept
    {
        return writer.WriteU64(value.sourceEntityId) && writer.WriteU64(value.slot) && writer.WriteU64(value.targetEntityId) &&
               writer.WriteU8(static_cast<u8>(value.kind)) && writer.WriteU8(0) && writer.WriteU16(0) && writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadEntityReference(serialization::BinaryReader& reader, cell::EntityReferenceRecord& value) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU64(value.sourceEntityId) || !reader.ReadU64(value.slot) || !reader.ReadU64(value.targetEntityId) ||
            !reader.ReadU8(kind) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) || !reader.ReadU32(reserved32)) return false;
        value.kind = static_cast<cell::EntityReferenceKind>(kind);
        return reserved8 == 0 && reserved16 == 0 && reserved32 == 0;
    }

    [[nodiscard]] bool WriteDependency(serialization::BinaryWriter& writer, const cell::DependencyRecord& value) noexcept
    {
        return WriteReference(writer, value.resource) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(0) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadDependency(serialization::BinaryReader& reader, cell::DependencyRecord& value) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!ReadReference(reader, value.resource) || !reader.ReadU8(kind) || !reader.ReadU8(reserved8) ||
            !reader.ReadU16(reserved16)) return false;
        value.kind = static_cast<resources::DependencyKind>(kind);
        return reserved8 == 0 && reserved16 == 0;
    }

    [[nodiscard]] cell::Result BuildBody(const CanonicalData& data, ByteArray& body, crypto::Digest256& fingerprint) noexcept
    {
        filesystem::MemoryFileWriter file(body);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU64(data.cellId) || !writer.WriteU64(data.worldId)) return WriterResult(writer);
        for (const i32 coordinate : data.gridCoordinate) if (!writer.WriteI32(coordinate)) return WriterResult(writer);
        if (!writer.WriteU8(data.hierarchyLevel) || !writer.WriteU8(static_cast<u8>(data.category)) || !writer.WriteU16(0))
            return WriterResult(writer);
        for (const f64 coordinate : data.origin) if (!writer.WriteF64(coordinate)) return WriterResult(writer);
        if (!WriteBounds(writer, data.bounds) || !WriteDigest(writer, data.sourceFingerprint) ||
            !writer.WriteU32(data.groups.Size()) || !writer.WriteU32(data.placements.Size()) ||
            !writer.WriteU32(data.lookup.Size()) || !writer.WriteU32(data.overrides.Size()) ||
            !writer.WriteU32(data.references.Size()) || !writer.WriteU32(data.dependencies.Size()) ||
            !writer.WriteU64(data.overrideData.Size())) return WriterResult(writer);
        for (const cell::ActivationGroupRecord& value : data.groups) if (!WriteGroup(writer, value)) return WriterResult(writer);
        for (const cell::PlacementRecord& value : data.placements) if (!WritePlacement(writer, value)) return WriterResult(writer);
        for (const cell::EntityLookupRecord& value : data.lookup) if (!WriteLookup(writer, value)) return WriterResult(writer);
        for (const cell::ComponentOverrideRecord& value : data.overrides) if (!WriteOverride(writer, value)) return WriterResult(writer);
        for (const cell::EntityReferenceRecord& value : data.references) if (!WriteEntityReference(writer, value)) return WriterResult(writer);
        for (const cell::DependencyRecord& value : data.dependencies) if (!WriteDependency(writer, value)) return WriterResult(writer);
        if (!writer.WriteBytes(data.overrideData.Data(), data.overrideData.Size())) return WriterResult(writer);
        fingerprint = crypto::Sha256(body.Data(), body.Size());
        return cell::Result::Success;
    }

    [[nodiscard]] cell::Result WriteDocument(filesystem::IFile& file, const ByteArray& body,
                                             const crypto::Digest256& fingerprint) noexcept
    {
        ByteArray payload(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter payloadFile(payload);
        serialization::BinaryWriter payloadWriter(payloadFile);
        if (!payloadWriter.WriteU32(BodyWireVersion) || !WriteDigest(payloadWriter, fingerprint) ||
            !payloadWriter.WriteBytes(body.Data(), body.Size())) return WriterResult(payloadWriter);
        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = cell::CellMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16)) return WriterResult(writer);
        serialization::SectionDescriptor section;
        section.id = CellSection;
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
        return cell::Result::Success;
    }

    template<typename Type>
    [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit) return false;
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] cell::Result ValidateLoaded(const cell::CellFile& file,
                                              const containers::DynamicArray<cell::EntityLookupRecord>& lookup,
                                              const u64 overrideDataSize) noexcept
    {
        const auto groups = file.ActivationGroups();
        const auto placements = file.Placements();
        const auto overrides = file.Overrides();
        const auto references = file.EntityReferences();
        const auto dependencies = file.Dependencies();
        if (file.CellId() == 0 || file.WorldId() == 0 || groups.Empty() || placements.Empty() ||
            groups[0].stableId != cell::AlwaysActiveGroup || file.Category() > cell::CellCategory::Generic ||
            !IsFiniteBounds(file.CellBounds())) return cell::Result::InvalidLayout;
        for (u32 axis = 0; axis < 3; ++axis) if (!std::isfinite(file.Origin()[axis])) return cell::Result::InvalidLayout;
        u32 expectedPlacement = 0;
        for (u32 index = 0; index < groups.Size(); ++index)
        {
            const cell::ActivationGroupRecord& group = groups[index];
            if ((index > 0 && groups[index - 1u].stableId >= group.stableId) ||
                (static_cast<u16>(group.flags) & ~KnownGroupFlags) != 0 || group.firstPlacement != expectedPlacement ||
                group.placementCount > placements.Size() - expectedPlacement) return cell::Result::InvalidLayout;
            for (u32 local = 0; local < group.placementCount; ++local)
            {
                const cell::PlacementRecord& placement = placements[expectedPlacement + local];
                if (placement.activationGroup != group.stableId ||
                    (local > 0 && placements[expectedPlacement + local - 1u].entityId >= placement.entityId))
                    return cell::Result::InvalidLayout;
            }
            expectedPlacement += group.placementCount;
        }
        if (expectedPlacement != placements.Size() || lookup.Size() != placements.Size()) return cell::Result::InvalidLayout;
        for (u32 index = 0; index < lookup.Size(); ++index)
            if (lookup[index].entityId == cell::InvalidEntityId || lookup[index].placementIndex >= placements.Size() ||
                placements[lookup[index].placementIndex].entityId != lookup[index].entityId ||
                (index > 0 && lookup[index - 1u].entityId >= lookup[index].entityId)) return cell::Result::InvalidLayout;
        containers::DynamicArray<cell::PlacementRecord> hierarchyPlacements(memory::pools::World::GetInstance());
        hierarchyPlacements.Reserve(placements.Size());
        for (const cell::PlacementRecord& placement : placements) hierarchyPlacements.PushBack(placement);
        if (hierarchyPlacements.Size() != placements.Size()) return cell::Result::LimitExceeded;
        const cell::Result hierarchyResult = ValidateHierarchy(hierarchyPlacements, lookup);
        if (hierarchyResult != cell::Result::Success) return hierarchyResult;
        u32 expectedOverride = 0;
        u32 expectedReference = 0;
        for (const cell::PlacementRecord& placement : placements)
        {
            if (!placement.prefab.IsValid() || placement.prefab.ExpectedType() != prefabs::PrefabResourceType ||
                !IsValidTransform(placement.transform) || !IsFiniteBounds(placement.bounds) ||
                !std::isfinite(placement.streamingDistance) || placement.streamingDistance < 0.0f ||
                !std::isfinite(placement.visibilityDistance) || placement.visibilityDistance < 0.0f ||
                (static_cast<u32>(placement.flags) & ~KnownPlacementFlags) != 0 ||
                placement.firstOverride != expectedOverride || placement.overrideCount > overrides.Size() - expectedOverride ||
                placement.firstReference != expectedReference || placement.referenceCount > references.Size() - expectedReference)
                return cell::Result::InvalidLayout;
            for (const f32 coordinate : placement.streamingReferencePoint) if (!std::isfinite(coordinate)) return cell::Result::InvalidLayout;
            for (u32 local = 0; local < placement.overrideCount; ++local)
                if (overrides[expectedOverride + local].entityId != placement.entityId) return cell::Result::InvalidLayout;
            for (u32 local = 0; local < placement.referenceCount; ++local)
                if (references[expectedReference + local].sourceEntityId != placement.entityId) return cell::Result::InvalidLayout;
            expectedOverride += placement.overrideCount;
            expectedReference += placement.referenceCount;
        }
        if (expectedOverride != overrides.Size() || expectedReference != references.Size()) return cell::Result::InvalidLayout;
        u64 expectedDataOffset = 0;
        for (u32 index = 0; index < overrides.Size(); ++index)
        {
            const cell::ComponentOverrideRecord& record = overrides[index];
            if (record.entityId == cell::InvalidEntityId || record.componentStableId == prefabs::InvalidStableId ||
                record.mode > cell::OverrideMode::Remove || (static_cast<u16>(record.flags) & ~KnownOverrideFlags) != 0 ||
                FindLookup(lookup, record.entityId) == nullptr) return cell::Result::InvalidLayout;
            if (record.mode == cell::OverrideMode::Remove)
            {
                if (record.schema != reflection::InvalidSchemaTypeId || record.schemaVersion != 0 ||
                    record.dataOffset != 0 || record.dataSize != 0 || !record.dataFingerprint.IsEmpty()) return cell::Result::InvalidLayout;
            }
            else
            {
                expectedDataOffset = (expectedDataOffset + 7u) & ~7ull;
                const auto bytes = file.OverrideData(record);
                if (record.schema == reflection::InvalidSchemaTypeId || record.schemaVersion == 0 || record.dataSize == 0 ||
                    record.dataOffset != expectedDataOffset || bytes.Size() != record.dataSize) return cell::Result::InvalidLayout;
                if (crypto::Sha256(bytes.Data(), bytes.Size()) != record.dataFingerprint) return cell::Result::IntegrityFailure;
                expectedDataOffset += record.dataSize;
            }
            if (index > 0 && overrides[index - 1u].entityId == record.entityId &&
                overrides[index - 1u].componentStableId >= record.componentStableId) return cell::Result::InvalidLayout;
        }
        if (expectedDataOffset != overrideDataSize) return cell::Result::InvalidLayout;
        for (u32 index = 0; index < references.Size(); ++index)
        {
            const cell::EntityReferenceRecord& reference = references[index];
            if (reference.sourceEntityId == cell::InvalidEntityId || reference.targetEntityId == cell::InvalidEntityId ||
                reference.slot == 0 || reference.kind > cell::EntityReferenceKind::OptionalWorld ||
                FindLookup(lookup, reference.sourceEntityId) == nullptr ||
                ((reference.kind == cell::EntityReferenceKind::RequiredLocal ||
                  reference.kind == cell::EntityReferenceKind::OptionalLocal) &&
                 FindLookup(lookup, reference.targetEntityId) == nullptr)) return cell::Result::InvalidLayout;
            if (index > 0 && references[index - 1u].sourceEntityId == reference.sourceEntityId &&
                references[index - 1u].slot >= reference.slot) return cell::Result::InvalidLayout;
        }
        for (u32 index = 0; index < dependencies.Size(); ++index)
            if (!dependencies[index].resource.IsValid() || !dependencies[index].resource.IsTyped() ||
                dependencies[index].kind > resources::DependencyKind::Soft ||
                (index > 0 && !DependencyLess(dependencies[index - 1u], dependencies[index]))) return cell::Result::InvalidLayout;
        for (const cell::PlacementRecord& placement : placements)
        {
            const cell::DependencyRecord* dependency = FindDependency(dependencies, placement.prefab);
            if (dependency == nullptr || dependency->kind != resources::DependencyKind::Required)
                return cell::Result::InvalidLayout;
        }
        return cell::Result::Success;
    }
} // namespace

namespace vanguard::world
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
        case Result::UnknownActivationGroup: return "UnknownActivationGroup";
        case Result::InvalidReference: return "InvalidReference";
        case Result::UnknownSchema: return "UnknownSchema";
        case Result::SchemaFailure: return "SchemaFailure";
        case Result::IoFailure: return "IoFailure";
        }
        return "Unknown";
    }

    CellFile::CellFile() noexcept
        : m_activationGroups(memory::pools::World::GetInstance()), m_placements(memory::pools::World::GetInstance()),
          m_entityLookup(memory::pools::World::GetInstance()), m_overrides(memory::pools::World::GetInstance()),
          m_entityReferences(memory::pools::World::GetInstance()), m_dependencies(memory::pools::Resources::GetInstance()),
          m_overrideData(memory::pools::World::GetInstance())
    {
    }

    Result CellFile::Open(filesystem::IFile& file, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 1;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(reader, CellMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success) return Convert(headerResult);
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success) return Convert(sectionResult);
        if (sections.Size() != 1 || sections[0].id != CellSection || sections[0].version != FileVersion ||
            sections[0].codec != serialization::Codec::None || sections[0].storedSize != sections[0].logicalSize ||
            sections[0].storedSize > limits.maximumFileSize || sections[0].storedSize > 0xffffffffull) return Result::InvalidLayout;
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
        u8 category = 0;
        u16 reserved = 0;
        if (!bodyReader.ReadU64(m_cellId) || !bodyReader.ReadU64(m_worldId)) return ReaderResult(bodyReader);
        for (i32& coordinate : m_gridCoordinate) if (!bodyReader.ReadI32(coordinate)) return ReaderResult(bodyReader);
        if (!bodyReader.ReadU8(m_hierarchyLevel) || !bodyReader.ReadU8(category) || !bodyReader.ReadU16(reserved) || reserved != 0)
            return Result::InvalidLayout;
        m_category = static_cast<CellCategory>(category);
        for (f64& coordinate : m_origin) if (!bodyReader.ReadF64(coordinate)) return ReaderResult(bodyReader);
        u32 groupCount = 0;
        u32 placementCount = 0;
        u32 lookupCount = 0;
        u32 overrideCount = 0;
        u32 referenceCount = 0;
        u32 dependencyCount = 0;
        u64 overrideDataSize = 0;
        if (!ReadBounds(bodyReader, m_bounds) || !ReadDigest(bodyReader, m_sourceFingerprint) ||
            !bodyReader.ReadU32(groupCount) || !bodyReader.ReadU32(placementCount) || !bodyReader.ReadU32(lookupCount) ||
            !bodyReader.ReadU32(overrideCount) || !bodyReader.ReadU32(referenceCount) ||
            !bodyReader.ReadU32(dependencyCount) || !bodyReader.ReadU64(overrideDataSize)) return ReaderResult(bodyReader);
        const u32 maximumOverrideData = limits.maximumOverrideDataBytes > 0xffffffffull
                                            ? 0xffffffffu : static_cast<u32>(limits.maximumOverrideDataBytes);
        if (overrideDataSize > limits.maximumOverrideDataBytes || overrideDataSize > 0xffffffffull ||
            !ResizeChecked(m_activationGroups, groupCount, limits.maximumActivationGroups) ||
            !ResizeChecked(m_placements, placementCount, limits.maximumPlacements) ||
            !ResizeChecked(m_entityLookup, lookupCount, limits.maximumPlacements) ||
            !ResizeChecked(m_overrides, overrideCount, limits.maximumOverrides) ||
            !ResizeChecked(m_entityReferences, referenceCount, limits.maximumEntityReferences) ||
            !ResizeChecked(m_dependencies, dependencyCount, limits.maximumDependencies) ||
            !ResizeChecked(m_overrideData, static_cast<u32>(overrideDataSize), maximumOverrideData)) return Result::LimitExceeded;
        for (ActivationGroupRecord& value : m_activationGroups) if (!ReadGroup(bodyReader, value)) return Result::InvalidLayout;
        for (PlacementRecord& value : m_placements) if (!ReadPlacement(bodyReader, value)) return Result::InvalidLayout;
        for (EntityLookupRecord& value : m_entityLookup) if (!ReadLookup(bodyReader, value)) return Result::InvalidLayout;
        for (ComponentOverrideRecord& value : m_overrides) if (!ReadOverride(bodyReader, value)) return Result::InvalidLayout;
        for (EntityReferenceRecord& value : m_entityReferences) if (!ReadEntityReference(bodyReader, value)) return Result::InvalidLayout;
        for (DependencyRecord& value : m_dependencies) if (!ReadDependency(bodyReader, value)) return Result::InvalidLayout;
        if (!bodyReader.ReadBytes(m_overrideData.Data(), m_overrideData.Size())) return ReaderResult(bodyReader);
        if (bodyReader.Position() != bodyReader.Size()) return Result::InvalidLayout;
        m_open = true;
        const Result validation = ValidateLoaded(*this, m_entityLookup, m_overrideData.Size());
        if (validation != Result::Success) Close();
        return validation;
    }

    void CellFile::Close() noexcept
    {
        m_cellId = 0;
        m_worldId = 0;
        for (u32 axis = 0; axis < 3; ++axis) { m_gridCoordinate[axis] = 0; m_origin[axis] = 0.0; }
        m_hierarchyLevel = 0;
        m_category = CellCategory::Generic;
        m_bounds = {};
        m_sourceFingerprint = {};
        m_contentFingerprint = {};
        m_activationGroups.Clear();
        m_placements.Clear();
        m_entityLookup.Clear();
        m_overrides.Clear();
        m_entityReferences.Clear();
        m_dependencies.Clear();
        m_overrideData.Clear();
        m_open = false;
    }

    bool CellFile::IsOpen() const noexcept { return m_open; }
    u64 CellFile::CellId() const noexcept { return m_cellId; }
    u64 CellFile::WorldId() const noexcept { return m_worldId; }
    const i32* CellFile::GridCoordinate() const noexcept { return m_gridCoordinate; }
    u8 CellFile::HierarchyLevel() const noexcept { return m_hierarchyLevel; }
    CellCategory CellFile::Category() const noexcept { return m_category; }
    const f64* CellFile::Origin() const noexcept { return m_origin; }
    const Bounds& CellFile::CellBounds() const noexcept { return m_bounds; }
    const crypto::Digest256& CellFile::SourceFingerprint() const noexcept { return m_sourceFingerprint; }
    const crypto::Digest256& CellFile::ContentFingerprint() const noexcept { return m_contentFingerprint; }
    containers::ArraySpan<const ActivationGroupRecord> CellFile::ActivationGroups() const noexcept { return m_activationGroups; }
    containers::ArraySpan<const PlacementRecord> CellFile::Placements() const noexcept { return m_placements; }
    containers::ArraySpan<const ComponentOverrideRecord> CellFile::Overrides() const noexcept { return m_overrides; }
    containers::ArraySpan<const EntityReferenceRecord> CellFile::EntityReferences() const noexcept { return m_entityReferences; }
    containers::ArraySpan<const DependencyRecord> CellFile::Dependencies() const noexcept { return m_dependencies; }

    containers::ArraySpan<const PlacementRecord> CellFile::PlacementsInGroup(
        const ActivationGroupRecord& group) const noexcept
    {
        if (!m_open || group.firstPlacement > m_placements.Size() ||
            group.placementCount > m_placements.Size() - group.firstPlacement) return {};
        return {m_placements.TypedData() + group.firstPlacement, group.placementCount};
    }

    containers::ArraySpan<const ComponentOverrideRecord> CellFile::OverridesFor(
        const PlacementRecord& placement) const noexcept
    {
        if (!m_open || placement.firstOverride > m_overrides.Size() ||
            placement.overrideCount > m_overrides.Size() - placement.firstOverride) return {};
        return {m_overrides.TypedData() + placement.firstOverride, placement.overrideCount};
    }

    containers::ArraySpan<const EntityReferenceRecord> CellFile::ReferencesFor(
        const PlacementRecord& placement) const noexcept
    {
        if (!m_open || placement.firstReference > m_entityReferences.Size() ||
            placement.referenceCount > m_entityReferences.Size() - placement.firstReference) return {};
        return {m_entityReferences.TypedData() + placement.firstReference, placement.referenceCount};
    }

    containers::ArraySpan<const u8> CellFile::OverrideData(const ComponentOverrideRecord& record) const noexcept
    {
        if (!m_open || record.dataOffset > m_overrideData.Size() || record.dataSize > m_overrideData.Size() - record.dataOffset ||
            record.dataSize > 0xffffffffull) return {};
        return {m_overrideData.TypedData() + record.dataOffset, static_cast<u32>(record.dataSize)};
    }

    const PlacementRecord* CellFile::FindPlacement(const u64 entityId) const noexcept
    {
        if (!m_open) return nullptr;
        const EntityLookupRecord* lookup = FindLookup(m_entityLookup, entityId);
        return lookup == nullptr ? nullptr : &m_placements[lookup->placementIndex];
    }

    const ActivationGroupRecord* CellFile::FindActivationGroup(const u64 stableId) const noexcept
    {
        return m_open ? FindGroup(m_activationGroups, stableId) : nullptr;
    }

    CellResource::CellResource() noexcept : m_prefabs(memory::pools::Resources::GetInstance()) {}

    resources::ResourceTypeId CellResource::Type() const noexcept { return CellResourceType; }

    Result CellResource::Open(const void* const data, const usize size, const ReadLimits& limits) noexcept
    {
        m_prefabs.Clear();
        if (data == nullptr || size == 0 || size > static_cast<usize>(~u32{0})) return Result::InvalidArgument;
        filesystem::MemoryFileReader reader(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        return m_file.Open(reader, limits);
    }

    bool CellResource::BindDependencies(const resources::LoadContext& context) noexcept
    {
        if (!m_file.IsOpen() || !m_prefabs.Empty()) return false;
        for (u32 index = 0; index < context.DependencyCount(); ++index)
        {
            const resources::ResourceReference reference = context.DependencyReference(index);
            if (reference.ExpectedType() != prefabs::PrefabResourceType) continue;
            const resources::ResourceHandle& dependency = context.Dependency(index);
            if (!dependency.IsValid() || dependency.Type() != prefabs::PrefabResourceType) return false;
            const u32 expectedSize = m_prefabs.Size() + 1u;
            m_prefabs.PushBack(dependency);
            if (m_prefabs.Size() != expectedSize) return false;
        }

        for (const PlacementRecord& placement : m_file.Placements())
            if (ResolvePrefab(placement.prefab) == nullptr) return false;
        return true;
    }

    const prefabs::PrefabFile* CellResource::ResolvePrefab(const resources::ResourceReference reference) const noexcept
    {
        if (!reference.IsValid() || reference.ExpectedType() != prefabs::PrefabResourceType) return nullptr;
        for (const resources::ResourceHandle& handle : m_prefabs)
            if (handle.Path() == reference.Path() && handle.Type() == prefabs::PrefabResourceType)
                return &static_cast<const prefabs::PrefabResource*>(handle.Get())->File();
        return nullptr;
    }

    const CellFile& CellResource::File() const noexcept { return m_file; }

    Result CookCell(const CellBuildDescription& description, filesystem::IFile& output) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success) return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        crypto::Digest256 fingerprint;
        result = BuildBody(canonical, body, fingerprint);
        return result == Result::Success ? WriteDocument(output, body, fingerprint) : result;
    }

    Result CalculateContentFingerprint(const CellBuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success) return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        return BuildBody(canonical, body, fingerprint);
    }
} // namespace vanguard::world
