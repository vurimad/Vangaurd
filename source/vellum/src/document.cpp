#include <vanguard/vellum/document_internal.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/vellum/fixed_storage.hpp>
#include <vanguard/vellum/layout_storage.hpp>

#include <bit>
#include <new>

namespace vanguard::vellum
{
    u32 DocumentResource::FindNodeByAuthoredId(const AuthoredNodeId authoredId) const noexcept
    {
        if (authoredId == InvalidAuthoredNodeId || m_authoredNodeLookup == nullptr)
            return InvalidDocumentNodeIndex;
        u32 first = 0;
        u32 count = m_nodeCount;
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            const AuthoredNodeLookup& entry = m_authoredNodeLookup[index];
            if (entry.authoredId < authoredId)
            {
                first = index + 1u;
                count -= step + 1u;
            }
            else
            {
                count = step;
            }
        }
        return first < m_nodeCount && m_authoredNodeLookup[first].authoredId == authoredId ? m_authoredNodeLookup[first].nodeIndex
                                                                                           : InvalidDocumentNodeIndex;
    }
} // namespace vanguard::vellum

namespace vanguard::vellum::detail
{
    namespace
    {
        [[nodiscard]] resources::Failure ToResourceFailure(const serialization::Result result) noexcept
        {
            switch (result)
            {
            case serialization::Result::Success:
                return resources::Failure::None;
            case serialization::Result::UnsupportedVersion:
            case serialization::Result::UnsupportedByteOrder:
            case serialization::Result::UnsupportedHeader:
                return resources::Failure::UnsupportedVersion;
            case serialization::Result::IntegrityFailure:
                return resources::Failure::IntegrityFailure;
            case serialization::Result::LimitExceeded:
                return resources::Failure::OutOfMemory;
            default:
                return resources::Failure::DeserializationFailure;
            }
        }

        [[nodiscard]] bool ReadCanonicalSectionDescriptor(serialization::BinaryReader& reader,
                                                          serialization::SectionDescriptor& section) noexcept
        {
            u8 reserved = 0;
            u16 codec = 0;
            u32 flags = 0;
            if (!reader.Seek(CanonicalSectionTableOffset) || !reader.ReadU32(section.id) || !reader.ReadU16(section.version.major) ||
                !reader.ReadU16(section.version.minor) || !reader.ReadU32(flags) || !reader.ReadU16(codec) ||
                !reader.ReadU8(section.alignmentLog2) || !reader.ReadU8(reserved) || !reader.ReadU64(section.offset) ||
                !reader.ReadU64(section.storedSize) || !reader.ReadU64(section.logicalSize) || !reader.ReadU64(section.storedCrc64))
                return false;
            section.flags = static_cast<serialization::SectionFlags>(flags);
            section.codec = static_cast<serialization::Codec>(codec);
            return reserved == 0;
        }

        [[nodiscard]] bool DecodeNodeKind(const u8 wire, NodeKind& kind) noexcept
        {
            switch (static_cast<WireNodeKind>(wire))
            {
            case WireNodeKind::StackPanel:
                kind = NodeKind::StackPanel;
                return true;
            case WireNodeKind::Spacer:
                kind = NodeKind::Spacer;
                return true;
            }
            kind = NodeKind::Invalid;
            return false;
        }

        [[nodiscard]] bool DecodePropertyType(const u8 wire, PropertyType& type) noexcept
        {
            switch (static_cast<WirePropertyType>(wire))
            {
            case WirePropertyType::Float:
                type = PropertyType::Float;
                return true;
            case WirePropertyType::Visibility:
                type = PropertyType::Visibility;
                return true;
            case WirePropertyType::Orientation:
                type = PropertyType::Orientation;
                return true;
            }
            type = PropertyType::Invalid;
            return false;
        }

        [[nodiscard]] const PropertyDescriptor* FindPropertyDescriptor(const NodeKind kind, const PropertyId id) noexcept
        {
            const PropertySchema* const schema = GetPropertySchema(kind);
            if (schema == nullptr)
                return nullptr;
            for (u16 index = 0; index < schema->count; ++index)
            {
                if (schema->descriptors[index].id == id)
                    return &schema->descriptors[index];
            }
            return nullptr;
        }

        [[nodiscard]] bool DecodeCanonicalPropertyValue(const PropertyDescriptor& descriptor, const u32 wireBits,
                                                        u32& canonicalBits) noexcept
        {
            canonicalBits = 0;
            if (descriptor.type == PropertyType::Float)
            {
                const f32 value = std::bit_cast<f32>(wireBits);
                f32 canonical = 0.0f;
                if (!CanonicalizePropertyFloat(descriptor, value, canonical) || std::bit_cast<u32>(canonical) != wireBits)
                    return false;
                canonicalBits = wireBits;
                return true;
            }
            if (descriptor.type == PropertyType::Visibility)
            {
                switch (static_cast<WireVisibility>(wireBits))
                {
                case WireVisibility::Visible:
                    canonicalBits = static_cast<u32>(Visibility::Visible);
                    return true;
                case WireVisibility::Hidden:
                    canonicalBits = static_cast<u32>(Visibility::Hidden);
                    return true;
                case WireVisibility::Collapsed:
                    canonicalBits = static_cast<u32>(Visibility::Collapsed);
                    return true;
                }
                return false;
            }
            if (descriptor.type == PropertyType::Orientation)
            {
                switch (static_cast<WireOrientation>(wireBits))
                {
                case WireOrientation::Horizontal:
                    canonicalBits = static_cast<u32>(Orientation::Horizontal);
                    return true;
                case WireOrientation::Vertical:
                    canonicalBits = static_cast<u32>(Orientation::Vertical);
                    return true;
                }
                return false;
            }
            return false;
        }

        void LookupSwap(AuthoredNodeLookup& left, AuthoredNodeLookup& right) noexcept
        {
            const AuthoredNodeLookup temporary = left;
            left = right;
            right = temporary;
        }

        void SiftLookupDown(AuthoredNodeLookup* const values, const u32 count, u32 root) noexcept
        {
            for (;;)
            {
                const u32 left = root * 2u + 1u;
                if (left >= count)
                    return;
                const u32 right = left + 1u;
                u32 largest = left;
                if (right < count && values[right].authoredId > values[left].authoredId)
                    largest = right;
                if (values[root].authoredId >= values[largest].authoredId)
                    return;
                LookupSwap(values[root], values[largest]);
                root = largest;
            }
        }

        void SortAuthoredLookup(AuthoredNodeLookup* const values, const u32 count) noexcept
        {
            if (values == nullptr || count < 2)
                return;
            for (u32 root = count / 2u; root != 0; --root)
                SiftLookupDown(values, count, root - 1u);
            for (u32 end = count; end > 1u; --end)
            {
                LookupSwap(values[0], values[end - 1u]);
                SiftLookupDown(values, end - 1u, 0);
            }
        }

        [[nodiscard]] bool ComputeTreeBodySize(const u32 nodeCount, const u32 propertyCount, usize& size) noexcept
        {
            usize nodeBytes = 0;
            usize propertyBytes = 0;
            usize total = TreeBodyHeaderWireSize;
            if (!CheckedMultiply(nodeCount, TreeNodeWireSize, nodeBytes) || !CheckedMultiply(propertyCount, TreePropertyWireSize, propertyBytes) ||
                !CheckedAdd(total, nodeBytes, total) || !CheckedAdd(total, propertyBytes, total))
                return false;
            size = total;
            return true;
        }

        [[nodiscard]] bool IsCancelled(const resources::LoadContext& context) noexcept
        {
            return context.IsCancellationRequested();
        }
    } // namespace

    DocumentResource* DocumentResourceAccess::Allocate(const u32 nodeCount, const u32 propertyCount) noexcept
    {
        usize objectOffset = 0;
        usize nodesOffset = 0;
        usize propertiesOffset = 0;
        usize lookupOffset = 0;
        BlockLayout layout;
        if (!layout.AddArray<DocumentResource>(1, objectOffset) || objectOffset != 0 || !layout.AddArray<DocumentNode>(nodeCount, nodesOffset) ||
            !layout.AddArray<DocumentProperty>(propertyCount, propertiesOffset) ||
            !layout.AddArray<AuthoredNodeLookup>(nodeCount, lookupOffset) || !layout.Finish())
            return nullptr;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Resources, layout.size, layout.alignment);
        if (!block)
            return nullptr;
        auto* const resource = ::new (block.address) DocumentResource();
        resource->m_allocationSize = block.size;
        resource->m_nodes = BindArray<DocumentNode>(block, nodesOffset);
        resource->m_properties = BindArray<DocumentProperty>(block, propertiesOffset);
        resource->m_authoredNodeLookup = BindArray<AuthoredNodeLookup>(block, lookupOffset);
        resource->m_rootIndex = 0;
        resource->m_nodeCount = nodeCount;
        resource->m_propertyCount = propertyCount;
        for (u32 index = 0; index < nodeCount; ++index)
        {
            ::new (&resource->m_nodes[index]) DocumentNode;
            ::new (&resource->m_authoredNodeLookup[index]) AuthoredNodeLookup;
        }
        for (u32 index = 0; index < propertyCount; ++index)
            ::new (&resource->m_properties[index]) DocumentProperty;
        return resource;
    }

    void DocumentResourceAccess::Destroy(DocumentResource* const resource) noexcept
    {
        if (resource == nullptr)
            return;
        const usize allocationSize = resource->m_allocationSize;
        resource->~DocumentResource();
        memory::MemoryBlock block{resource, allocationSize, memory::PoolId::Resources};
        memory::Free(block);
    }

    resources::ResourceObject* DecodeDocumentResource(const resources::ResourceReference reference, const void* const data, const usize size,
                                                      const resources::LoadContext& context, resources::Failure& failure,
                                                      void* const userData) noexcept
    {
        failure = resources::Failure::None;
        auto* const limits = static_cast<DocumentDecoderState*>(userData);
        if (IsCancelled(context))
        {
            failure = resources::Failure::Cancelled;
            return nullptr;
        }
        if (limits == nullptr || !reference.IsTyped() || reference.ExpectedType() != DocumentResourceType || context.Reference() != reference ||
            data == nullptr || size == 0)
        {
            failure = resources::Failure::DeserializationFailure;
            return nullptr;
        }
        if (size > limits->maximumDocumentBytes || size > 0xffffffffull)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        if (context.GetDependencyCount() != 0)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }

        filesystem::MemoryFileReader file(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        serialization::BinaryReader reader(file);
        serialization::ReadLimits readLimits;
        readLimits.maximumFileSize = limits->maximumDocumentBytes;
        readLimits.maximumSections = 1;
        readLimits.maximumAlignmentLog2 = CanonicalTreeAlignmentLog2;
        serialization::DocumentHeader header;
        serialization::Result result =
            serialization::ReadDocumentHeader(reader, DocumentMagic, SupportedDocumentVersions, readLimits, header);
        if (result != serialization::Result::Success)
        {
            failure = ToResourceFailure(result);
            return nullptr;
        }
        if (header.headerSize != serialization::DocumentHeader::WireSize || header.flags != serialization::DocumentFlags::Deterministic ||
            header.sectionTableOffset != CanonicalSectionTableOffset || header.sectionCount != 1)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }

        serialization::SectionDescriptor section;
        if (!ReadCanonicalSectionDescriptor(reader, section))
        {
            failure = reader.IsGood() ? resources::Failure::IntegrityFailure : ToResourceFailure(reader.GetStatus());
            return nullptr;
        }
        if (section.id != TreeSectionId || section.flags != serialization::SectionFlags::None || section.codec != serialization::Codec::None ||
            section.alignmentLog2 != CanonicalTreeAlignmentLog2 ||
            section.offset != CanonicalTreeOffset || section.storedSize != section.logicalSize || section.offset > header.fileSize ||
            section.storedSize != header.fileSize - section.offset)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }
        if (section.version != TreeSectionVersion)
        {
            failure = resources::Failure::UnsupportedVersion;
            return nullptr;
        }
        const auto* const treeBytes = static_cast<const u8*>(data) + static_cast<usize>(section.offset);
        if (serialization::Crc64(treeBytes, static_cast<usize>(section.storedSize)) != section.storedCrc64)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }
        if (!reader.Seek(section.offset))
        {
            failure = ToResourceFailure(reader.GetStatus());
            return nullptr;
        }

        u32 bodyWireVersion = 0;
        u32 propertySchemaVersion = 0;
        u32 requiredFeatureBits = 0;
        u32 rootIndex = InvalidDocumentNodeIndex;
        u32 nodeCount = 0;
        u32 propertyCount = 0;
        if (!reader.ReadU32(bodyWireVersion) || !reader.ReadU32(propertySchemaVersion) || !reader.ReadU32(requiredFeatureBits) ||
            !reader.ReadU32(rootIndex) || !reader.ReadU32(nodeCount) || !reader.ReadU32(propertyCount))
        {
            failure = ToResourceFailure(reader.GetStatus());
            return nullptr;
        }
        if (bodyWireVersion != TreeBodyWireVersion || propertySchemaVersion != RuntimePropertySchemaVersion)
        {
            failure = resources::Failure::UnsupportedVersion;
            return nullptr;
        }
        if ((requiredFeatureBits & ~SupportedRequiredFeatureBits) != 0)
        {
            failure = resources::Failure::UnsupportedVersion;
            return nullptr;
        }
        if (rootIndex != 0 || nodeCount == 0)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }
        if (nodeCount > limits->maximumNodes || propertyCount > limits->maximumProperties)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        usize expectedBodySize = 0;
        if (!ComputeTreeBodySize(nodeCount, propertyCount, expectedBodySize) || expectedBodySize != section.logicalSize)
        {
            failure = resources::Failure::IntegrityFailure;
            return nullptr;
        }

        DocumentResource* const resource = DocumentResourceAccess::Allocate(nodeCount, propertyCount);
        if (resource == nullptr)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto failResource = [&](const resources::Failure code) noexcept -> resources::ResourceObject* {
            failure = code;
            DocumentResourceAccess::Destroy(resource);
            return nullptr;
        };

        u32 nextProperty = 0;
        for (u32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        {
            if ((nodeIndex & 0x3ffu) == 0 && IsCancelled(context))
                return failResource(resources::Failure::Cancelled);
            u64 authoredId = 0;
            u32 parentIndex = InvalidDocumentNodeIndex;
            u32 firstProperty = 0;
            u32 nodePropertyCount = 0;
            u8 wireKind = 0;
            u8 reserved0 = 0;
            u16 reserved1 = 0;
            if (!reader.ReadU64(authoredId) || !reader.ReadU32(parentIndex) || !reader.ReadU32(firstProperty) ||
                !reader.ReadU32(nodePropertyCount) || !reader.ReadU8(wireKind) || !reader.ReadU8(reserved0) || !reader.ReadU16(reserved1))
                return failResource(ToResourceFailure(reader.GetStatus()));
            NodeKind kind = NodeKind::Invalid;
            if (authoredId == InvalidAuthoredNodeId || reserved0 != 0 || reserved1 != 0 || !DecodeNodeKind(wireKind, kind) ||
                firstProperty != nextProperty || nodePropertyCount > propertyCount - nextProperty)
                return failResource(resources::Failure::IntegrityFailure);
            u32 depth = 1;
            if (nodeIndex == 0)
            {
                if (parentIndex != InvalidDocumentNodeIndex)
                    return failResource(resources::Failure::IntegrityFailure);
            }
            else
            {
                if (parentIndex >= nodeIndex)
                    return failResource(resources::Failure::IntegrityFailure);
                if (resource->GetNodes()[parentIndex].kind == NodeKind::Spacer)
                    return failResource(resources::Failure::IntegrityFailure);
                const u32 parentDepth = resource->GetNodes()[parentIndex].depth;
                if (parentDepth == 0xffffffffu)
                    return failResource(resources::Failure::IntegrityFailure);
                depth = parentDepth + 1u;
            }
            if (depth > limits->maximumHierarchyDepth)
                return failResource(resources::Failure::OutOfMemory);

            DocumentNode& node = const_cast<DocumentNode&>(resource->GetNodes()[nodeIndex]);
            node = {authoredId, parentIndex, firstProperty, nodePropertyCount, depth, kind};
            AuthoredNodeLookup& lookup = const_cast<AuthoredNodeLookup&>(resource->GetAuthoredNodeLookup()[nodeIndex]);
            lookup = {authoredId, nodeIndex, 0};
            nextProperty += nodePropertyCount;
        }
        if (nextProperty != propertyCount)
            return failResource(resources::Failure::IntegrityFailure);

        auto lookup = resource->GetAuthoredNodeLookup();
        SortAuthoredLookup(const_cast<AuthoredNodeLookup*>(lookup.Data()), lookup.Size());
        for (u32 index = 1; index < lookup.Size(); ++index)
        {
            if (lookup[index - 1u].authoredId == lookup[index].authoredId)
                return failResource(resources::Failure::IntegrityFailure);
        }

        u32 propertyIndex = 0;
        for (const DocumentNode& node : resource->GetNodes())
        {
            u16 previousPropertyId = 0;
            for (u32 local = 0; local < node.propertyCount; ++local, ++propertyIndex)
            {
                if ((propertyIndex & 0x7ffu) == 0 && IsCancelled(context))
                    return failResource(resources::Failure::Cancelled);
                u16 rawPropertyId = 0;
                u8 wireType = 0;
                u8 valueSize = 0;
                u32 wireBits = 0;
                if (!reader.ReadU16(rawPropertyId) || !reader.ReadU8(wireType) || !reader.ReadU8(valueSize) || !reader.ReadU32(wireBits))
                    return failResource(ToResourceFailure(reader.GetStatus()));
                const PropertyId propertyId = static_cast<PropertyId>(rawPropertyId);
                const PropertyDescriptor* const descriptor = FindPropertyDescriptor(node.kind, propertyId);
                PropertyType propertyType = PropertyType::Invalid;
                u32 canonicalBits = 0;
                if (rawPropertyId == 0 || rawPropertyId <= previousPropertyId || valueSize != sizeof(u32) || descriptor == nullptr ||
                    !DecodePropertyType(wireType, propertyType) || propertyType != descriptor->type ||
                    !DecodeCanonicalPropertyValue(*descriptor, wireBits, canonicalBits))
                    return failResource(resources::Failure::IntegrityFailure);
                DocumentProperty& property = const_cast<DocumentProperty&>(resource->GetProperties()[propertyIndex]);
                property = {propertyId, propertyType, canonicalBits};
                previousPropertyId = rawPropertyId;
            }
        }
        if (propertyIndex != propertyCount || reader.Position() != header.fileSize)
            return failResource(resources::Failure::IntegrityFailure);
        const u32 live = limits->liveDocuments.Increment();
        auto& peak = limits->peakDocuments;
        u32 observedPeak = peak.GetValue();
        while (live > observedPeak)
        {
            const u32 previous = peak.CompareExchange(live, observedPeak);
            if (previous == observedPeak)
                break;
            observedPeak = previous;
        }
        failure = resources::Failure::None;
        return resource;
    }

    void DestroyDocumentResource(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        DocumentResourceAccess::Destroy(static_cast<DocumentResource*>(resource));
        if (auto* const state = static_cast<DocumentDecoderState*>(userData))
            static_cast<void>(state->liveDocuments.Decrement());
    }
} // namespace vanguard::vellum::detail
