#include <vanguard/world/worlds.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace vanguard;
    namespace world = vanguard::world;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 WorldSection = serialization::MakeFourCC('W', 'R', 'L', 'D');
    constexpr u32 BodyWireVersion = 1;
    constexpr u32 BodyPrefixSize = 36;
    constexpr u32 KnownCellFlags = static_cast<u32>(world::WorldCellFlags::AlwaysLoaded) | static_cast<u32>(world::WorldCellFlags::Interior) |
                                   static_cast<u32>(world::WorldCellFlags::Mission) | static_cast<u32>(world::WorldCellFlags::TwoDimensionalStreaming) |
                                   static_cast<u32>(world::WorldCellFlags::EditorOnly) | static_cast<u32>(world::WorldCellFlags::AllowDistanceBoosting);
    constexpr u32 KnownProxyFlags = static_cast<u32>(world::DistantProxyFlags::ProxyOnly) | static_cast<u32>(world::DistantProxyFlags::Building) |
                                    static_cast<u32>(world::DistantProxyFlags::Terrain) | static_cast<u32>(world::DistantProxyFlags::Road) |
                                    static_cast<u32>(world::DistantProxyFlags::Water) | static_cast<u32>(world::DistantProxyFlags::Decoration) |
                                    static_cast<u32>(world::DistantProxyFlags::Mission) | static_cast<u32>(world::DistantProxyFlags::TwoDimensionalStreaming) |
                                    static_cast<u32>(world::DistantProxyFlags::KeepResident) | static_cast<u32>(world::DistantProxyFlags::EditorOnly) |
                                    static_cast<u32>(world::DistantProxyFlags::AllowDistanceBoosting);
    constexpr u8 KnownChildFlags = static_cast<u8>(world::ProxyChildFlags::RequiredForReplacement);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : cells(memory::pools::World::GetInstance()), proxies(memory::pools::World::GetInstance()), children(memory::pools::World::GetInstance()),
              dependencies(memory::pools::Resources::GetInstance())
        {
        }

        u64 worldId = 0;
        f64 origin[3]{};
        world::WorldBounds bounds;
        crypto::Digest256 sourceFingerprint;
        containers::DynamicArray<world::WorldCellRecord> cells;
        containers::DynamicArray<world::DistantProxyRecord> proxies;
        containers::DynamicArray<world::ProxyChildRecord> children;
        containers::DynamicArray<world::DependencyRecord> dependencies;
    };

    [[nodiscard]] world::Result Convert(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return world::Result::Success;
        case serialization::Result::InvalidMagic:
            return world::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
        case serialization::Result::UnsupportedByteOrder:
        case serialization::Result::UnsupportedHeader:
            return world::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return world::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return world::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return world::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return world::Result::IoFailure;
        default:
            return world::Result::InvalidLayout;
        }
    }

    [[nodiscard]] world::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.IsGood() ? world::Result::Success : Convert(writer.GetStatus());
    }

    [[nodiscard]] world::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.IsGood() ? world::Result::Success : Convert(reader.GetStatus());
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& value) noexcept
    {
        return writer.WriteBytes(value.bytes, sizeof(value.bytes));
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& value) noexcept
    {
        return reader.ReadBytes(value.bytes, sizeof(value.bytes));
    }

    [[nodiscard]] bool WriteReference(serialization::BinaryWriter& writer, const resources::ResourceReference value) noexcept
    {
        return writer.WriteU64(value.GetPath().Id()) && writer.WriteU32(value.ExpectedType());
    }

    [[nodiscard]] bool ReadReference(serialization::BinaryReader& reader, resources::ResourceReference& value) noexcept
    {
        u64 path = 0;
        u32 type = 0;
        if (!reader.ReadU64(path) || !reader.ReadU32(type))
            return false;
        value = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
        return true;
    }

    [[nodiscard]] bool WriteBounds(serialization::BinaryWriter& writer, const world::WorldBounds& value) noexcept
    {
        for (const f64 coordinate : value.minimum)
            if (!writer.WriteF64(coordinate))
                return false;
        for (const f64 coordinate : value.maximum)
            if (!writer.WriteF64(coordinate))
                return false;
        return true;
    }

    [[nodiscard]] bool ReadBounds(serialization::BinaryReader& reader, world::WorldBounds& value) noexcept
    {
        for (f64& coordinate : value.minimum)
            if (!reader.ReadF64(coordinate))
                return false;
        for (f64& coordinate : value.maximum)
            if (!reader.ReadF64(coordinate))
                return false;
        return true;
    }

    [[nodiscard]] bool IsFiniteBounds(const world::WorldBounds& bounds) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) || bounds.minimum[axis] > bounds.maximum[axis])
                return false;
        return true;
    }

    [[nodiscard]] bool Contains(const world::WorldBounds& outer, const world::WorldBounds& inner) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
            if (inner.minimum[axis] < outer.minimum[axis] || inner.maximum[axis] > outer.maximum[axis])
                return false;
        return true;
    }

    [[nodiscard]] bool CellLess(const world::WorldCellRecord& left, const world::WorldCellRecord& right) noexcept
    {
        return left.cellId < right.cellId;
    }

    [[nodiscard]] bool ProxyLess(const world::DistantProxyRecord& left, const world::DistantProxyRecord& right) noexcept
    {
        return left.proxyId < right.proxyId;
    }

    [[nodiscard]] bool ChildLess(const world::ProxyChildRecord& left, const world::ProxyChildRecord& right) noexcept
    {
        if (left.proxyId != right.proxyId)
            return left.proxyId < right.proxyId;
        if (left.kind != right.kind)
            return left.kind < right.kind;
        return left.childId < right.childId;
    }

    [[nodiscard]] bool DependencyLess(const world::DependencyRecord& left, const world::DependencyRecord& right) noexcept
    {
        if (left.resource.GetPath() != right.resource.GetPath())
            return left.resource.GetPath() < right.resource.GetPath();
        return left.resource.ExpectedType() < right.resource.ExpectedType();
    }

    template <typename Record, typename Id> [[nodiscard]] const Record* FindById(const containers::ArraySpan<const Record> records, const Id id) noexcept
    {
        u32 first = 0;
        u32 count = records.Size();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            const u64 candidate = [&]() noexcept
            {
                if constexpr (requires { records[index].cellId; })
                    return records[index].cellId;
                else
                    return records[index].proxyId;
            }();
            if (candidate < id)
            {
                first = index + 1u;
                count -= step + 1u;
            }
            else
                count = step;
        }
        if (first >= records.Size())
            return nullptr;
        if constexpr (requires { records[first].cellId; })
            return records[first].cellId == id ? &records[first] : nullptr;
        else
            return records[first].proxyId == id ? &records[first] : nullptr;
    }

    [[nodiscard]] bool AddDependency(containers::DynamicArray<world::DependencyRecord>& dependencies, const resources::ResourceReference reference) noexcept
    {
        if (!reference.IsValid() || !reference.IsTyped())
            return false;
        for (const world::DependencyRecord& existing : dependencies)
            if (existing.resource == reference)
                return true;
        const u32 expected = dependencies.Size() + 1u;
        dependencies.PushBack({reference, resources::DependencyKind::Soft});
        return dependencies.Size() == expected;
    }

    [[nodiscard]] world::Result ValidateCellHierarchy(const containers::DynamicArray<world::WorldCellRecord>& cells,
                                                      const world::WorldBounds& worldBounds) noexcept
    {
        const containers::ArraySpan<const world::WorldCellRecord> view = cells;
        for (const world::WorldCellRecord& cell : cells)
        {
            if (!Contains(worldBounds, cell.bounds))
                return world::Result::InvalidLayout;
            if (cell.parentCellId == world::InvalidCellId)
                continue;
            const world::WorldCellRecord* parent = FindById(view, cell.parentCellId);
            if (parent == nullptr)
                return world::Result::MissingParent;
            if (parent->hierarchyLevel <= cell.hierarchyLevel || !Contains(parent->bounds, cell.bounds))
                return world::Result::InvalidLayout;
        }
        return world::Result::Success;
    }

    [[nodiscard]] world::Result ValidateProxyHierarchy(const containers::DynamicArray<world::DistantProxyRecord>& proxies,
                                                       const world::WorldBounds& worldBounds) noexcept
    {
        const containers::ArraySpan<const world::DistantProxyRecord> view = proxies;
        containers::DynamicArray<u8> states(memory::pools::World::GetInstance());
        states.Resize(proxies.Size());
        if (states.Size() != proxies.Size())
            return world::Result::LimitExceeded;
        for (u32 index = 0; index < states.Size(); ++index)
            states[index] = 0;
        for (u32 start = 0; start < proxies.Size(); ++start)
        {
            if (!Contains(worldBounds, proxies[start].bounds))
                return world::Result::InvalidLayout;
            u32 cursor = start;
            while (states[cursor] != 2)
            {
                if (states[cursor] == 1)
                    return world::Result::HierarchyCycle;
                states[cursor] = 1;
                const u64 parentId = proxies[cursor].parentProxyId;
                if (parentId == world::InvalidProxyId)
                    break;
                const world::DistantProxyRecord* parent = FindById(view, parentId);
                if (parent == nullptr)
                    return world::Result::MissingParent;
                if (!Contains(parent->bounds, proxies[cursor].bounds) ||
                    parent->secondaryReferencePointDistance < proxies[cursor].secondaryReferencePointDistance)
                    return world::Result::InvalidLayout;
                cursor = static_cast<u32>(parent - proxies.TypedData());
            }
            cursor = start;
            while (states[cursor] == 1)
            {
                states[cursor] = 2;
                const u64 parentId = proxies[cursor].parentProxyId;
                if (parentId == world::InvalidProxyId)
                    break;
                const world::DistantProxyRecord* parent = FindById(view, parentId);
                if (parent == nullptr)
                    break;
                cursor = static_cast<u32>(parent - proxies.TypedData());
            }
        }
        return world::Result::Success;
    }

    [[nodiscard]] world::Result Canonicalize(const world::WorldBuildDescription& description, CanonicalData& output) noexcept
    {
        if (description.worldId == 0 || description.cells.Empty() || !IsFiniteBounds(description.bounds))
            return world::Result::InvalidArgument;
        for (const f64 coordinate : description.origin)
            if (!std::isfinite(coordinate))
                return world::Result::InvalidArgument;
        output.worldId = description.worldId;
        output.bounds = description.bounds;
        output.sourceFingerprint = description.sourceFingerprint;
        for (u32 axis = 0; axis < 3; ++axis)
            output.origin[axis] = description.origin[axis];

        output.cells.Reserve(description.cells.Size());
        for (const world::WorldCellBuildRecord& source : description.cells)
        {
            if (source.cellId == world::InvalidCellId || !source.cell.IsValid() || source.cell.ExpectedType() != world::CellResourceType ||
                source.category > world::CellCategory::Generic || !IsFiniteBounds(source.bounds) || !std::isfinite(source.activationDistance) ||
                source.activationDistance < 0.0f || !std::isfinite(source.retentionDistance) || source.retentionDistance < source.activationDistance ||
                (static_cast<u32>(source.flags) & ~KnownCellFlags) != 0)
                return world::Result::InvalidArgument;
            bool validCoordinates = true;
            for (u32 axis = 0; axis < 3; ++axis)
                validCoordinates &= std::isfinite(source.origin[axis]) && std::isfinite(source.streamingReferencePoint[axis]);
            if (!validCoordinates)
                return world::Result::InvalidArgument;
            if (!description.includeEditorData && (static_cast<u32>(source.flags) & static_cast<u32>(world::WorldCellFlags::EditorOnly)) != 0)
                continue;
            world::WorldCellRecord record;
            record.cellId = source.cellId;
            record.parentCellId = source.parentCellId;
            record.name = source.name;
            record.cell = source.cell;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                record.gridCoordinate[axis] = source.gridCoordinate[axis];
                record.origin[axis] = source.origin[axis];
                record.streamingReferencePoint[axis] = source.streamingReferencePoint[axis];
            }
            record.hierarchyLevel = source.hierarchyLevel;
            record.category = source.category;
            record.streamingPriority = source.streamingPriority;
            record.bounds = source.bounds;
            record.activationDistance = source.activationDistance;
            record.retentionDistance = source.retentionDistance;
            record.flags = source.flags;
            output.cells.PushBack(record);
            if (!AddDependency(output.dependencies, source.cell))
                return world::Result::LimitExceeded;
        }
        if (output.cells.Empty())
            return world::Result::InvalidArgument;
        std::sort(output.cells.Begin(), output.cells.End(), CellLess);
        for (u32 index = 1; index < output.cells.Size(); ++index)
            if (output.cells[index - 1u].cellId == output.cells[index].cellId)
                return world::Result::DuplicateIdentifier;
        world::Result result = ValidateCellHierarchy(output.cells, output.bounds);
        if (result != world::Result::Success)
            return result;

        output.proxies.Reserve(description.distantProxies.Size());
        for (const world::DistantProxyBuildRecord& source : description.distantProxies)
        {
            if (source.proxyId == world::InvalidProxyId || source.ownerCellId == world::InvalidCellId || !source.mesh.IsValid() ||
                source.mesh.ExpectedType() != meshes::MeshResourceType || !IsFiniteBounds(source.bounds) || !std::isfinite(source.streamingDistance) ||
                !std::isfinite(source.secondaryReferencePointDistance) || !std::isfinite(source.nearHideDistance) || source.nearHideDistance < 0.0f ||
                source.secondaryReferencePointDistance < 0.0f || source.streamingDistance < source.nearHideDistance ||
                (static_cast<u32>(source.flags) & ~KnownProxyFlags) != 0)
                return world::Result::InvalidArgument;
            for (u32 axis = 0; axis < 3; ++axis)
                if (!std::isfinite(source.streamingReferencePoint[axis]) || !std::isfinite(source.preboostStreamingReferencePoint[axis]) ||
                    !std::isfinite(source.secondaryReferencePoint[axis]))
                    return world::Result::InvalidArgument;
            if (!description.includeEditorData && (static_cast<u32>(source.flags) & static_cast<u32>(world::DistantProxyFlags::EditorOnly)) != 0)
                continue;
            const world::WorldCellRecord* ownerCell = FindById(containers::ArraySpan<const world::WorldCellRecord>(output.cells), source.ownerCellId);
            if (ownerCell == nullptr)
                return world::Result::InvalidReference;
            if (!Contains(ownerCell->bounds, source.bounds))
                return world::Result::InvalidLayout;
            world::DistantProxyRecord record;
            record.proxyId = source.proxyId;
            record.parentProxyId = source.parentProxyId;
            record.ownerCellId = source.ownerCellId;
            record.name = source.name;
            record.mesh = source.mesh;
            record.bounds = source.bounds;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                record.streamingReferencePoint[axis] = source.streamingReferencePoint[axis];
                record.preboostStreamingReferencePoint[axis] = source.preboostStreamingReferencePoint[axis];
                record.secondaryReferencePoint[axis] = source.secondaryReferencePoint[axis];
            }
            record.streamingDistance = source.streamingDistance;
            record.secondaryReferencePointDistance = source.secondaryReferencePointDistance;
            record.nearHideDistance = source.nearHideDistance;
            record.streamingPriority = source.streamingPriority;
            record.flags = source.flags;
            output.proxies.PushBack(record);
            if (!AddDependency(output.dependencies, source.mesh))
                return world::Result::LimitExceeded;
        }
        std::sort(output.proxies.Begin(), output.proxies.End(), ProxyLess);
        for (u32 index = 1; index < output.proxies.Size(); ++index)
            if (output.proxies[index - 1u].proxyId == output.proxies[index].proxyId)
                return world::Result::DuplicateIdentifier;
        result = ValidateProxyHierarchy(output.proxies, output.bounds);
        if (result != world::Result::Success)
            return result;

        const auto cells = containers::ArraySpan<const world::WorldCellRecord>(output.cells);
        const auto proxies = containers::ArraySpan<const world::DistantProxyRecord>(output.proxies);
        output.children.Reserve(description.proxyChildren.Size());
        for (const world::ProxyChildBuildRecord& source : description.proxyChildren)
        {
            if (source.proxyId == world::InvalidProxyId || source.childId == 0 || source.kind > world::ProxyChildKind::Proxy ||
                (static_cast<u8>(source.flags) & ~KnownChildFlags) != 0 || FindById(proxies, source.proxyId) == nullptr)
                return world::Result::InvalidReference;
            if (source.kind == world::ProxyChildKind::Cell)
            {
                if (FindById(cells, source.childId) == nullptr)
                    return world::Result::InvalidReference;
            }
            else
            {
                const world::DistantProxyRecord* child = FindById(proxies, source.childId);
                if (child == nullptr || child->parentProxyId != source.proxyId)
                    return world::Result::InvalidReference;
            }
            output.children.PushBack({source.proxyId, source.childId, source.kind, source.flags});
        }
        std::sort(output.children.Begin(), output.children.End(), ChildLess);
        for (u32 index = 1; index < output.children.Size(); ++index)
            if (output.children[index - 1u].proxyId == output.children[index].proxyId && output.children[index - 1u].kind == output.children[index].kind &&
                output.children[index - 1u].childId == output.children[index].childId)
                return world::Result::DuplicateIdentifier;
        u32 childIndex = 0;
        for (world::DistantProxyRecord& proxy : output.proxies)
        {
            proxy.firstChild = childIndex;
            bool hasRequiredChild = false;
            while (childIndex < output.children.Size() && output.children[childIndex].proxyId == proxy.proxyId)
            {
                hasRequiredChild |= (static_cast<u8>(output.children[childIndex].flags) & static_cast<u8>(world::ProxyChildFlags::RequiredForReplacement)) != 0;
                ++proxy.childCount;
                ++childIndex;
            }
            const bool proxyOnly = (static_cast<u32>(proxy.flags) & static_cast<u32>(world::DistantProxyFlags::ProxyOnly)) != 0;
            if (!proxyOnly && !hasRequiredChild)
                return world::Result::InvalidLayout;
        }
        if (childIndex != output.children.Size())
            return world::Result::InvalidLayout;
        std::sort(output.dependencies.Begin(), output.dependencies.End(), DependencyLess);
        return world::Result::Success;
    }

    [[nodiscard]] bool WriteCell(serialization::BinaryWriter& writer, const world::WorldCellRecord& value) noexcept
    {
        if (!writer.WriteU64(value.cellId) || !writer.WriteU64(value.parentCellId) || !writer.WriteU64(value.name) || !WriteReference(writer, value.cell))
            return false;
        for (const i32 coordinate : value.gridCoordinate)
            if (!writer.WriteI32(coordinate))
                return false;
        if (!writer.WriteU8(value.hierarchyLevel) || !writer.WriteU8(static_cast<u8>(value.category)) ||
            !writer.WriteU8(static_cast<u8>(value.streamingPriority)) || !writer.WriteU8(0))
            return false;
        for (const f64 coordinate : value.origin)
            if (!writer.WriteF64(coordinate))
                return false;
        if (!WriteBounds(writer, value.bounds))
            return false;
        for (const f64 coordinate : value.streamingReferencePoint)
            if (!writer.WriteF64(coordinate))
                return false;
        return writer.WriteF32(value.activationDistance) && writer.WriteF32(value.retentionDistance) && writer.WriteU32(static_cast<u32>(value.flags)) &&
               writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadCell(serialization::BinaryReader& reader, world::WorldCellRecord& value) noexcept
    {
        u8 category = 0;
        u8 reserved8 = 0;
        u32 flags = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU64(value.cellId) || !reader.ReadU64(value.parentCellId) || !reader.ReadU64(value.name) || !ReadReference(reader, value.cell))
            return false;
        for (i32& coordinate : value.gridCoordinate)
            if (!reader.ReadI32(coordinate))
                return false;
        u8 streamingPriority = 0;
        if (!reader.ReadU8(value.hierarchyLevel) || !reader.ReadU8(category) || !reader.ReadU8(streamingPriority) || !reader.ReadU8(reserved8))
            return false;
        value.category = static_cast<world::CellCategory>(category);
        value.streamingPriority = static_cast<world::StreamingPriority>(streamingPriority);
        for (f64& coordinate : value.origin)
            if (!reader.ReadF64(coordinate))
                return false;
        if (!ReadBounds(reader, value.bounds))
            return false;
        for (f64& coordinate : value.streamingReferencePoint)
            if (!reader.ReadF64(coordinate))
                return false;
        if (!reader.ReadF32(value.activationDistance) || !reader.ReadF32(value.retentionDistance) || !reader.ReadU32(flags) || !reader.ReadU32(reserved32))
            return false;
        value.flags = static_cast<world::WorldCellFlags>(flags);
        return reserved8 == 0 && reserved32 == 0;
    }

    [[nodiscard]] bool WriteProxy(serialization::BinaryWriter& writer, const world::DistantProxyRecord& value) noexcept
    {
        if (!writer.WriteU64(value.proxyId) || !writer.WriteU64(value.parentProxyId) || !writer.WriteU64(value.ownerCellId) || !writer.WriteU64(value.name) ||
            !WriteReference(writer, value.mesh) || !WriteBounds(writer, value.bounds))
            return false;
        for (const f64 coordinate : value.streamingReferencePoint)
            if (!writer.WriteF64(coordinate))
                return false;
        for (const f64 coordinate : value.preboostStreamingReferencePoint)
            if (!writer.WriteF64(coordinate))
                return false;
        for (const f64 coordinate : value.secondaryReferencePoint)
            if (!writer.WriteF64(coordinate))
                return false;
        return writer.WriteF32(value.streamingDistance) && writer.WriteF32(value.secondaryReferencePointDistance) && writer.WriteF32(value.nearHideDistance) &&
               writer.WriteF32(0.0f) && writer.WriteU32(value.firstChild) && writer.WriteU32(value.childCount) &&
               writer.WriteU32(static_cast<u32>(value.flags)) && writer.WriteU8(static_cast<u8>(value.streamingPriority)) && writer.WriteU8(0) &&
               writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadProxy(serialization::BinaryReader& reader, world::DistantProxyRecord& value) noexcept
    {
        u32 flags = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU64(value.proxyId) || !reader.ReadU64(value.parentProxyId) || !reader.ReadU64(value.ownerCellId) || !reader.ReadU64(value.name) ||
            !ReadReference(reader, value.mesh) || !ReadBounds(reader, value.bounds))
            return false;
        for (f64& coordinate : value.streamingReferencePoint)
            if (!reader.ReadF64(coordinate))
                return false;
        for (f64& coordinate : value.preboostStreamingReferencePoint)
            if (!reader.ReadF64(coordinate))
                return false;
        for (f64& coordinate : value.secondaryReferencePoint)
            if (!reader.ReadF64(coordinate))
                return false;
        u8 streamingPriority = 0;
        f32 reservedDistance = 0.0f;
        if (!reader.ReadF32(value.streamingDistance) || !reader.ReadF32(value.secondaryReferencePointDistance) || !reader.ReadF32(value.nearHideDistance) ||
            !reader.ReadF32(reservedDistance) || !reader.ReadU32(value.firstChild) || !reader.ReadU32(value.childCount) || !reader.ReadU32(flags) ||
            !reader.ReadU8(streamingPriority) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16))
            return false;
        value.flags = static_cast<world::DistantProxyFlags>(flags);
        value.streamingPriority = static_cast<world::StreamingPriority>(streamingPriority);
        return reservedDistance == 0.0f && reserved8 == 0 && reserved16 == 0;
    }

    [[nodiscard]] bool WriteChild(serialization::BinaryWriter& writer, const world::ProxyChildRecord& value) noexcept
    {
        return writer.WriteU64(value.proxyId) && writer.WriteU64(value.childId) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(static_cast<u8>(value.flags)) && writer.WriteU16(0) && writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadChild(serialization::BinaryReader& reader, world::ProxyChildRecord& value) noexcept
    {
        u8 kind = 0;
        u8 flags = 0;
        u16 reserved16 = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU64(value.proxyId) || !reader.ReadU64(value.childId) || !reader.ReadU8(kind) || !reader.ReadU8(flags) || !reader.ReadU16(reserved16) ||
            !reader.ReadU32(reserved32))
            return false;
        value.kind = static_cast<world::ProxyChildKind>(kind);
        value.flags = static_cast<world::ProxyChildFlags>(flags);
        return reserved16 == 0 && reserved32 == 0;
    }

    [[nodiscard]] bool WriteDependency(serialization::BinaryWriter& writer, const world::DependencyRecord& value) noexcept
    {
        return WriteReference(writer, value.resource) && writer.WriteU8(static_cast<u8>(value.kind)) && writer.WriteU8(0) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadDependency(serialization::BinaryReader& reader, world::DependencyRecord& value) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!ReadReference(reader, value.resource) || !reader.ReadU8(kind) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16))
            return false;
        value.kind = static_cast<resources::DependencyKind>(kind);
        return reserved8 == 0 && reserved16 == 0;
    }

    [[nodiscard]] world::Result BuildBody(const CanonicalData& data, ByteArray& body, crypto::Digest256& fingerprint) noexcept
    {
        filesystem::MemoryFileWriter file(body);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU64(data.worldId))
            return WriterResult(writer);
        for (const f64 coordinate : data.origin)
            if (!writer.WriteF64(coordinate))
                return WriterResult(writer);
        if (!WriteBounds(writer, data.bounds) || !WriteDigest(writer, data.sourceFingerprint) || !writer.WriteU32(data.cells.Size()) ||
            !writer.WriteU32(data.proxies.Size()) || !writer.WriteU32(data.children.Size()) || !writer.WriteU32(data.dependencies.Size()))
            return WriterResult(writer);
        for (const world::WorldCellRecord& value : data.cells)
            if (!WriteCell(writer, value))
                return WriterResult(writer);
        for (const world::DistantProxyRecord& value : data.proxies)
            if (!WriteProxy(writer, value))
                return WriterResult(writer);
        for (const world::ProxyChildRecord& value : data.children)
            if (!WriteChild(writer, value))
                return WriterResult(writer);
        for (const world::DependencyRecord& value : data.dependencies)
            if (!WriteDependency(writer, value))
                return WriterResult(writer);
        fingerprint = crypto::Sha256(body.Data(), body.Size());
        return world::Result::Success;
    }

    [[nodiscard]] world::Result WriteDocument(filesystem::IFile& file, const ByteArray& body, const crypto::Digest256& fingerprint) noexcept
    {
        ByteArray payload(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter payloadFile(payload);
        serialization::BinaryWriter payloadWriter(payloadFile);
        if (!payloadWriter.WriteU32(BodyWireVersion) || !WriteDigest(payloadWriter, fingerprint) || !payloadWriter.WriteBytes(body.Data(), body.Size()))
            return WriterResult(payloadWriter);
        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = world::WorldMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16))
            return WriterResult(writer);
        serialization::SectionDescriptor section;
        section.id = WorldSection;
        section.version = FileVersion;
        section.alignmentLog2 = 4;
        section.offset = writer.Position();
        section.storedSize = payload.Size();
        section.logicalSize = payload.Size();
        section.storedCrc64 = serialization::Crc64(payload.Data(), payload.Size());
        if (!writer.WriteBytes(payload.Data(), payload.Size()) || !writer.Align(16))
            return WriterResult(writer);
        header.sectionTableOffset = writer.Position();
        const serialization::Result sectionResult = serialization::WriteSectionDescriptor(writer, section);
        if (sectionResult != serialization::Result::Success)
            return Convert(sectionResult);
        header.fileSize = writer.Position();
        if (!writer.Seek(0))
            return WriterResult(writer);
        const serialization::Result headerResult = serialization::WriteDocumentHeader(writer, header);
        if (headerResult != serialization::Result::Success || !writer.Seek(header.fileSize) || !writer.Flush())
            return headerResult == serialization::Result::Success ? WriterResult(writer) : Convert(headerResult);
        return world::Result::Success;
    }

    template <typename Type> [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit)
            return false;
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] const world::DependencyRecord* FindDependency(const containers::ArraySpan<const world::DependencyRecord> dependencies,
                                                                const resources::ResourceReference reference) noexcept
    {
        for (const world::DependencyRecord& dependency : dependencies)
            if (dependency.resource == reference)
                return &dependency;
        return nullptr;
    }

    [[nodiscard]] world::Result ValidateLoaded(const world::WorldFile& file) noexcept
    {
        const auto cells = file.GetCells();
        const auto proxies = file.GetDistantProxies();
        const auto children = file.GetProxyChildren();
        const auto dependencies = file.GetDependencies();
        if (file.GetWorldId() == 0 || cells.Empty() || !IsFiniteBounds(file.Bounds()))
            return world::Result::InvalidLayout;
        for (const f64 coordinate : containers::ArraySpan<const f64>(file.GetOrigin(), 3))
            if (!std::isfinite(coordinate))
                return world::Result::InvalidLayout;
        for (u32 index = 0; index < cells.Size(); ++index)
        {
            const world::WorldCellRecord& cell = cells[index];
            if ((index > 0 && cells[index - 1u].cellId >= cell.cellId) || cell.cellId == world::InvalidCellId || !cell.cell.IsValid() ||
                cell.cell.ExpectedType() != world::CellResourceType || cell.category > world::CellCategory::Generic || !IsFiniteBounds(cell.bounds) ||
                !std::isfinite(cell.activationDistance) || cell.activationDistance < 0.0f || !std::isfinite(cell.retentionDistance) ||
                cell.retentionDistance < cell.activationDistance || (static_cast<u32>(cell.flags) & ~KnownCellFlags) != 0)
                return world::Result::InvalidLayout;
            for (u32 axis = 0; axis < 3; ++axis)
                if (!std::isfinite(cell.origin[axis]) || !std::isfinite(cell.streamingReferencePoint[axis]))
                    return world::Result::InvalidLayout;
            const world::DependencyRecord* dependency = FindDependency(dependencies, cell.cell);
            if (dependency == nullptr || dependency->kind != resources::DependencyKind::Soft)
                return world::Result::InvalidLayout;
        }
        containers::DynamicArray<world::WorldCellRecord> cellCopy(memory::pools::World::GetInstance());
        cellCopy.Reserve(cells.Size());
        for (const auto& cell : cells)
            cellCopy.PushBack(cell);
        world::Result result = ValidateCellHierarchy(cellCopy, file.Bounds());
        if (result != world::Result::Success)
            return result;
        u32 expectedChild = 0;
        for (u32 index = 0; index < proxies.Size(); ++index)
        {
            const world::DistantProxyRecord& proxy = proxies[index];
            if ((index > 0 && proxies[index - 1u].proxyId >= proxy.proxyId) || proxy.proxyId == world::InvalidProxyId ||
                FindById(cells, proxy.ownerCellId) == nullptr || !Contains(FindById(cells, proxy.ownerCellId)->bounds, proxy.bounds) || !proxy.mesh.IsValid() ||
                proxy.mesh.ExpectedType() != meshes::MeshResourceType || !IsFiniteBounds(proxy.bounds) || !std::isfinite(proxy.streamingDistance) ||
                !std::isfinite(proxy.secondaryReferencePointDistance) || !std::isfinite(proxy.nearHideDistance) || proxy.nearHideDistance < 0.0f ||
                proxy.secondaryReferencePointDistance < 0.0f || proxy.streamingDistance < proxy.nearHideDistance ||
                (static_cast<u32>(proxy.flags) & ~KnownProxyFlags) != 0 || proxy.firstChild != expectedChild ||
                proxy.childCount > children.Size() - expectedChild)
                return world::Result::InvalidLayout;
            for (u32 axis = 0; axis < 3; ++axis)
                if (!std::isfinite(proxy.streamingReferencePoint[axis]) || !std::isfinite(proxy.preboostStreamingReferencePoint[axis]) ||
                    !std::isfinite(proxy.secondaryReferencePoint[axis]))
                    return world::Result::InvalidLayout;
            const world::DependencyRecord* dependency = FindDependency(dependencies, proxy.mesh);
            if (dependency == nullptr || dependency->kind != resources::DependencyKind::Soft)
                return world::Result::InvalidLayout;
            bool hasRequiredChild = false;
            for (u32 local = 0; local < proxy.childCount; ++local)
            {
                const world::ProxyChildRecord& child = children[expectedChild + local];
                if (child.proxyId != proxy.proxyId || child.childId == 0 || child.kind > world::ProxyChildKind::Proxy ||
                    (static_cast<u8>(child.flags) & ~KnownChildFlags) != 0 || (local > 0 && !ChildLess(children[expectedChild + local - 1u], child)))
                    return world::Result::InvalidLayout;
                if (child.kind == world::ProxyChildKind::Cell)
                {
                    if (FindById(cells, child.childId) == nullptr)
                        return world::Result::InvalidReference;
                }
                else
                {
                    const world::DistantProxyRecord* childProxy = FindById(proxies, child.childId);
                    if (childProxy == nullptr || childProxy->parentProxyId != proxy.proxyId)
                        return world::Result::InvalidReference;
                }
                hasRequiredChild |= (static_cast<u8>(child.flags) & static_cast<u8>(world::ProxyChildFlags::RequiredForReplacement)) != 0;
            }
            const bool proxyOnly = (static_cast<u32>(proxy.flags) & static_cast<u32>(world::DistantProxyFlags::ProxyOnly)) != 0;
            if (!proxyOnly && !hasRequiredChild)
                return world::Result::InvalidLayout;
            expectedChild += proxy.childCount;
        }
        if (expectedChild != children.Size())
            return world::Result::InvalidLayout;
        containers::DynamicArray<world::DistantProxyRecord> proxyCopy(memory::pools::World::GetInstance());
        proxyCopy.Reserve(proxies.Size());
        for (const auto& proxy : proxies)
            proxyCopy.PushBack(proxy);
        result = ValidateProxyHierarchy(proxyCopy, file.Bounds());
        if (result != world::Result::Success)
            return result;
        for (u32 index = 0; index < dependencies.Size(); ++index)
            if (!dependencies[index].resource.IsValid() || !dependencies[index].resource.IsTyped() ||
                dependencies[index].kind != resources::DependencyKind::Soft || (index > 0 && !DependencyLess(dependencies[index - 1u], dependencies[index])))
                return world::Result::InvalidLayout;
        return world::Result::Success;
    }
} // namespace

namespace vanguard::world
{
    WorldFile::WorldFile() noexcept
        : m_cells(memory::pools::World::GetInstance()), m_distantProxies(memory::pools::World::GetInstance()),
          m_proxyChildren(memory::pools::World::GetInstance()), m_dependencies(memory::pools::Resources::GetInstance())
    {
    }

    Result WorldFile::Open(filesystem::IFile& file, const WorldReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 1;
        const serialization::Result headerResult = serialization::ReadDocumentHeader(reader, WorldMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
            return Convert(headerResult);
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success)
            return Convert(sectionResult);
        if (sections.Size() != 1 || sections[0].id != WorldSection || sections[0].version != FileVersion || sections[0].codec != serialization::Codec::None ||
            sections[0].storedSize != sections[0].logicalSize || sections[0].storedSize > limits.maximumFileSize || sections[0].storedSize > 0xffffffffull)
            return Result::InvalidLayout;
        ByteArray payload(memory::pools::Serialization::GetInstance());
        payload.Resize(static_cast<u32>(sections[0].storedSize));
        if (payload.Size() != sections[0].storedSize)
            return Result::LimitExceeded;
        if (!reader.Seek(sections[0].offset) || !reader.ReadBytes(payload.Data(), payload.Size()))
            return ReaderResult(reader);
        if (serialization::Crc64(payload.Data(), payload.Size()) != sections[0].storedCrc64)
            return Result::IntegrityFailure;
        if (payload.Size() < BodyPrefixSize)
            return Result::InvalidLayout;
        filesystem::MemoryFileReader payloadFile(payload, 0);
        serialization::BinaryReader bodyReader(payloadFile);
        u32 bodyVersion = 0;
        if (!bodyReader.ReadU32(bodyVersion) || !ReadDigest(bodyReader, m_contentFingerprint))
            return ReaderResult(bodyReader);
        if (bodyVersion != BodyWireVersion)
            return Result::UnsupportedVersion;
        if (crypto::Sha256(payload.TypedData() + BodyPrefixSize, payload.Size() - BodyPrefixSize) != m_contentFingerprint)
            return Result::IntegrityFailure;
        if (!bodyReader.ReadU64(m_worldId))
            return ReaderResult(bodyReader);
        for (f64& coordinate : m_origin)
            if (!bodyReader.ReadF64(coordinate))
                return ReaderResult(bodyReader);
        u32 cellCount = 0;
        u32 proxyCount = 0;
        u32 childCount = 0;
        u32 dependencyCount = 0;
        if (!ReadBounds(bodyReader, m_bounds) || !ReadDigest(bodyReader, m_sourceFingerprint) || !bodyReader.ReadU32(cellCount) ||
            !bodyReader.ReadU32(proxyCount) || !bodyReader.ReadU32(childCount) || !bodyReader.ReadU32(dependencyCount))
            return ReaderResult(bodyReader);
        if (!ResizeChecked(m_cells, cellCount, limits.maximumCells) || !ResizeChecked(m_distantProxies, proxyCount, limits.maximumDistantProxies) ||
            !ResizeChecked(m_proxyChildren, childCount, limits.maximumProxyChildren) ||
            !ResizeChecked(m_dependencies, dependencyCount, limits.maximumDependencies))
            return Result::LimitExceeded;
        for (WorldCellRecord& value : m_cells)
            if (!ReadCell(bodyReader, value))
                return Result::InvalidLayout;
        for (DistantProxyRecord& value : m_distantProxies)
            if (!ReadProxy(bodyReader, value))
                return Result::InvalidLayout;
        for (ProxyChildRecord& value : m_proxyChildren)
            if (!ReadChild(bodyReader, value))
                return Result::InvalidLayout;
        for (DependencyRecord& value : m_dependencies)
            if (!ReadDependency(bodyReader, value))
                return Result::InvalidLayout;
        if (bodyReader.Position() != bodyReader.Size())
            return Result::InvalidLayout;
        m_open = true;
        const Result validation = ValidateLoaded(*this);
        if (validation != Result::Success)
            Close();
        return validation;
    }

    void WorldFile::Close() noexcept
    {
        m_worldId = 0;
        for (f64& coordinate : m_origin)
            coordinate = 0.0;
        m_bounds = {};
        m_sourceFingerprint = {};
        m_contentFingerprint = {};
        m_cells.Clear();
        m_distantProxies.Clear();
        m_proxyChildren.Clear();
        m_dependencies.Clear();
        m_open = false;
    }

    bool WorldFile::IsOpen() const noexcept
    {
        return m_open;
    }

    resources::ResourceTypeId WorldResource::GetType() const noexcept
    {
        return WorldResourceType;
    }

    Result WorldResource::Open(const void* const data, const usize size, const WorldReadLimits& limits) noexcept
    {
        if (data == nullptr || size == 0 || size > static_cast<usize>(~u32{0}))
        {
            return Result::InvalidArgument;
        }
        filesystem::MemoryFileReader reader(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        return m_file.Open(reader, limits);
    }

    const WorldFile& WorldResource::GetFile() const noexcept
    {
        return m_file;
    }
    u64 WorldFile::GetWorldId() const noexcept
    {
        return m_worldId;
    }
    const f64* WorldFile::GetOrigin() const noexcept
    {
        return m_origin;
    }
    const WorldBounds& WorldFile::Bounds() const noexcept
    {
        return m_bounds;
    }
    const crypto::Digest256& WorldFile::GetSourceFingerprint() const noexcept
    {
        return m_sourceFingerprint;
    }
    const crypto::Digest256& WorldFile::GetContentFingerprint() const noexcept
    {
        return m_contentFingerprint;
    }
    containers::ArraySpan<const WorldCellRecord> WorldFile::GetCells() const noexcept
    {
        return m_cells;
    }
    containers::ArraySpan<const DistantProxyRecord> WorldFile::GetDistantProxies() const noexcept
    {
        return m_distantProxies;
    }
    containers::ArraySpan<const ProxyChildRecord> WorldFile::GetProxyChildren() const noexcept
    {
        return m_proxyChildren;
    }
    containers::ArraySpan<const DependencyRecord> WorldFile::GetDependencies() const noexcept
    {
        return m_dependencies;
    }

    containers::ArraySpan<const ProxyChildRecord> WorldFile::GetChildrenOf(const DistantProxyRecord& proxy) const noexcept
    {
        if (!m_open || proxy.firstChild > m_proxyChildren.Size() || proxy.childCount > m_proxyChildren.Size() - proxy.firstChild)
            return {};
        return {m_proxyChildren.TypedData() + proxy.firstChild, proxy.childCount};
    }

    const WorldCellRecord* WorldFile::FindCell(const u64 cellId) const noexcept
    {
        return m_open ? FindById(containers::ArraySpan<const WorldCellRecord>(m_cells), cellId) : nullptr;
    }

    const DistantProxyRecord* WorldFile::FindDistantProxy(const u64 proxyId) const noexcept
    {
        return m_open ? FindById(containers::ArraySpan<const DistantProxyRecord>(m_distantProxies), proxyId) : nullptr;
    }

    Result CookWorld(const WorldBuildDescription& description, filesystem::IFile& output) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
            return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        crypto::Digest256 fingerprint;
        result = BuildBody(canonical, body, fingerprint);
        return result == Result::Success ? WriteDocument(output, body, fingerprint) : result;
    }

    Result CalculateWorldContentFingerprint(const WorldBuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
            return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        return BuildBody(canonical, body, fingerprint);
    }
} // namespace vanguard::world
