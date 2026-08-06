#include <vanguard/meshes/meshes.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace vanguard;
    namespace mesh = vanguard::meshes;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 MetadataSection = serialization::MakeFourCC('M', 'E', 'T', 'A');
    constexpr u32 GeometrySection = serialization::MakeFourCC('G', 'E', 'O', 'M');
    constexpr u32 MetadataWireVersion = 1;
    constexpr u64 ContentFingerprintOffset = 112;
    constexpr u8 GeometryAlignmentLog2 = 6;
    constexpr u64 GeometryAlignment = 1ull << GeometryAlignmentLog2;
    constexpr u16 KnownPageFlags = static_cast<u16>(mesh::PageFlags::RequiredForLowestLod) |
                                   static_cast<u16>(mesh::PageFlags::DirectGpuUpload);
    constexpr u16 KnownSubmeshFlags = static_cast<u16>(mesh::SubmeshFlags::CastsShadow) |
                                      static_cast<u16>(mesh::SubmeshFlags::RayTracing) |
                                      static_cast<u16>(mesh::SubmeshFlags::TwoSided);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : buffers(memory::pools::Rendering::GetInstance()), pages(memory::pools::Rendering::GetInstance()),
              vertexLayouts(memory::pools::Rendering::GetInstance()), vertexStreams(memory::pools::Rendering::GetInstance()),
              materialSlots(memory::pools::Rendering::GetInstance()),
              lods(memory::pools::Rendering::GetInstance()), submeshes(memory::pools::Rendering::GetInstance()),
              bufferRecords(memory::pools::Rendering::GetInstance()), pageRecords(memory::pools::Rendering::GetInstance()),
              layoutRecords(memory::pools::Rendering::GetInstance()), streamRecords(memory::pools::Rendering::GetInstance()),
              materialRecords(memory::pools::Rendering::GetInstance()),
              lodRecords(memory::pools::Rendering::GetInstance()), submeshRecords(memory::pools::Rendering::GetInstance())
        {
        }

        containers::DynamicArray<mesh::BufferBuildRecord> buffers;
        containers::DynamicArray<mesh::PageBuildRecord> pages;
        containers::DynamicArray<mesh::VertexLayoutBuildRecord> vertexLayouts;
        containers::DynamicArray<mesh::VertexStreamBuildRecord> vertexStreams;
        containers::DynamicArray<mesh::MaterialSlotBuildRecord> materialSlots;
        containers::DynamicArray<mesh::LodBuildRecord> lods;
        containers::DynamicArray<mesh::SubmeshBuildRecord> submeshes;
        containers::DynamicArray<mesh::BufferRecord> bufferRecords;
        containers::DynamicArray<mesh::PageRecord> pageRecords;
        containers::DynamicArray<mesh::VertexLayoutRecord> layoutRecords;
        containers::DynamicArray<mesh::VertexStream> streamRecords;
        containers::DynamicArray<mesh::MaterialSlot> materialRecords;
        containers::DynamicArray<mesh::LodRecord> lodRecords;
        containers::DynamicArray<mesh::SubmeshRecord> submeshRecords;
        u64 geometrySize = 0;
    };

    [[nodiscard]] mesh::Result ConvertSerializationResult(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return mesh::Result::Success;
        case serialization::Result::InvalidMagic:
            return mesh::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
            return mesh::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return mesh::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return mesh::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return mesh::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return mesh::Result::IoFailure;
        default:
            return mesh::Result::InvalidLayout;
        }
    }

    [[nodiscard]] mesh::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? mesh::Result::Success : ConvertSerializationResult(writer.Status());
    }

    [[nodiscard]] mesh::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? mesh::Result::Success : ConvertSerializationResult(reader.Status());
    }

    template <typename Type> void CopySpan(const containers::ArraySpan<const Type> source, containers::DynamicArray<Type>& destination)
    {
        destination.Reserve(source.Size());
        for (u32 index = 0; index < source.Size(); ++index)
        {
            destination.PushBack(source[index]);
        }
    }

    [[nodiscard]] bool IsFinite(const f32 value) noexcept
    {
        return std::isfinite(value);
    }

    [[nodiscard]] bool IsValidBounds(const mesh::Bounds& bounds) noexcept
    {
        if (!IsFinite(bounds.sphereRadius) || bounds.sphereRadius < 0.0f)
        {
            return false;
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!IsFinite(bounds.minimum[axis]) || !IsFinite(bounds.maximum[axis]) || !IsFinite(bounds.sphereCenter[axis]) ||
                bounds.minimum[axis] > bounds.maximum[axis])
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsValidQuantization(const mesh::PositionQuantization& quantization) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!IsFinite(quantization.scale[axis]) || quantization.scale[axis] == 0.0f || !IsFinite(quantization.bias[axis]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool IsValidMeshKind(const mesh::MeshKind kind) noexcept
    {
        return kind <= mesh::MeshKind::Skinned;
    }

    [[nodiscard]] bool IsValidBufferKind(const mesh::BufferKind kind) noexcept
    {
        return kind <= mesh::BufferKind::Custom;
    }

    [[nodiscard]] bool IsValidSemantic(const mesh::VertexSemantic semantic) noexcept
    {
        return semantic <= mesh::VertexSemantic::Custom;
    }

    [[nodiscard]] bool IsValidVertexFormat(const mesh::VertexFormat format) noexcept
    {
        return format <= mesh::VertexFormat::R10G10B10A2UNorm;
    }

    [[nodiscard]] bool IsValidIndexFormat(const mesh::IndexFormat format) noexcept
    {
        return format <= mesh::IndexFormat::UInt32;
    }

    [[nodiscard]] bool IsValidTopology(const mesh::PrimitiveTopology topology) noexcept
    {
        return topology <= mesh::PrimitiveTopology::TriangleList;
    }

    [[nodiscard]] u64 AlignUp(const u64 value, const u64 alignment) noexcept
    {
        if (alignment == 0 || value > ~u64{0} - (alignment - 1))
        {
            return ~u64{0};
        }
        return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] u32 FindBufferIndex(const CanonicalData& data, const u32 id) noexcept
    {
        for (u32 index = 0; index < data.buffers.Size(); ++index)
        {
            if (data.buffers[index].id == id)
            {
                return index;
            }
        }
        return mesh::InvalidRecordIndex;
    }

    [[nodiscard]] u32 FindLayoutIndex(const CanonicalData& data, const u32 id) noexcept
    {
        for (u32 index = 0; index < data.vertexLayouts.Size(); ++index)
        {
            if (data.vertexLayouts[index].id == id)
            {
                return index;
            }
        }
        return mesh::InvalidRecordIndex;
    }

    [[nodiscard]] u32 FindMaterialIndex(const CanonicalData& data, const u32 id) noexcept
    {
        for (u32 index = 0; index < data.materialSlots.Size(); ++index)
        {
            if (data.materialSlots[index].id == id)
            {
                return index;
            }
        }
        return mesh::InvalidRecordIndex;
    }

    [[nodiscard]] bool RequiredPagesCoverRange(
        const containers::ArraySpan<const mesh::BufferRecord> buffers,
        const containers::ArraySpan<const mesh::PageRecord> pages,
        const u32 bufferIndex,
        const u64 byteOffset,
        const u64 byteSize) noexcept
    {
        if (bufferIndex >= buffers.Size() || byteSize == 0 || byteOffset > buffers[bufferIndex].byteSize ||
            byteSize > buffers[bufferIndex].byteSize - byteOffset)
        {
            return false;
        }
        const u64 rangeEnd = byteOffset + byteSize;
        const mesh::BufferRecord& buffer = buffers[bufferIndex];
        for (u32 pageOffset = 0; pageOffset < buffer.pageCount; ++pageOffset)
        {
            const mesh::PageRecord& page = pages[buffer.firstPage + pageOffset];
            const u64 pageEnd = page.bufferOffset + page.byteSize;
            if (page.bufferOffset < rangeEnd && byteOffset < pageEnd &&
                !mesh::HasFlag(page.flags, mesh::PageFlags::RequiredForLowestLod))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool WriteBounds(serialization::BinaryWriter& writer, const mesh::Bounds& bounds) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!writer.WriteF32(bounds.minimum[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!writer.WriteF32(bounds.maximum[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!writer.WriteF32(bounds.sphereCenter[axis]))
            {
                return false;
            }
        }
        return writer.WriteF32(bounds.sphereRadius);
    }

    [[nodiscard]] bool ReadBounds(serialization::BinaryReader& reader, mesh::Bounds& bounds) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!reader.ReadF32(bounds.minimum[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!reader.ReadF32(bounds.maximum[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!reader.ReadF32(bounds.sphereCenter[axis]))
            {
                return false;
            }
        }
        return reader.ReadF32(bounds.sphereRadius);
    }

    [[nodiscard]] bool WriteQuantization(serialization::BinaryWriter& writer,
                                         const mesh::PositionQuantization& quantization) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!writer.WriteF32(quantization.scale[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!writer.WriteF32(quantization.bias[axis]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ReadQuantization(serialization::BinaryReader& reader,
                                        mesh::PositionQuantization& quantization) noexcept
    {
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!reader.ReadF32(quantization.scale[axis]))
            {
                return false;
            }
        }
        for (u32 axis = 0; axis < 3; ++axis)
        {
            if (!reader.ReadF32(quantization.bias[axis]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] mesh::Result ValidateAndBuildRecords(const mesh::BuildDescription& description, CanonicalData& data) noexcept
    {
        const mesh::ReadLimits limits;
        if (!IsValidMeshKind(description.kind) || description.name == 0 || !IsValidBounds(description.bounds) ||
            !IsValidQuantization(description.positionQuantization) || description.buffers.Empty() || description.pages.Empty() ||
            description.vertexLayouts.Empty() || description.vertexStreams.Empty() || description.lods.Empty() ||
            description.submeshes.Empty())
        {
            return mesh::Result::InvalidArgument;
        }
        if ((description.kind == mesh::MeshKind::Skinned) != description.skeleton.IsValid() ||
            (description.skeleton.IsValid() && !description.skeleton.IsTyped()))
        {
            return mesh::Result::InvalidArgument;
        }
        if (description.buffers.Size() > limits.maximumBuffers || description.pages.Size() > limits.maximumPages ||
            description.vertexLayouts.Size() > limits.maximumVertexLayouts ||
            description.vertexStreams.Size() > limits.maximumVertexStreams ||
            description.materialSlots.Size() > limits.maximumMaterialSlots || description.lods.Size() > limits.maximumLods ||
            description.submeshes.Size() > limits.maximumSubmeshes)
        {
            return mesh::Result::LimitExceeded;
        }

        CopySpan(description.buffers, data.buffers);
        CopySpan(description.pages, data.pages);
        CopySpan(description.vertexLayouts, data.vertexLayouts);
        CopySpan(description.vertexStreams, data.vertexStreams);
        CopySpan(description.materialSlots, data.materialSlots);
        CopySpan(description.lods, data.lods);
        CopySpan(description.submeshes, data.submeshes);

        std::sort(data.buffers.Begin(), data.buffers.End(),
                  [](const mesh::BufferBuildRecord& left, const mesh::BufferBuildRecord& right) { return left.id < right.id; });
        std::sort(data.pages.Begin(), data.pages.End(), [](const mesh::PageBuildRecord& left, const mesh::PageBuildRecord& right)
        {
            return left.bufferId != right.bufferId ? left.bufferId < right.bufferId : left.bufferOffset < right.bufferOffset;
        });
        std::sort(data.vertexLayouts.Begin(), data.vertexLayouts.End(),
                  [](const mesh::VertexLayoutBuildRecord& left, const mesh::VertexLayoutBuildRecord& right)
        {
            return left.id < right.id;
        });
        std::sort(data.vertexStreams.Begin(), data.vertexStreams.End(),
                  [](const mesh::VertexStreamBuildRecord& left, const mesh::VertexStreamBuildRecord& right)
        {
            if (left.layoutId != right.layoutId)
            {
                return left.layoutId < right.layoutId;
            }
            if (left.semantic != right.semantic)
            {
                return left.semantic < right.semantic;
            }
            if (left.semanticIndex != right.semanticIndex)
            {
                return left.semanticIndex < right.semanticIndex;
            }
            if (left.binding != right.binding)
            {
                return left.binding < right.binding;
            }
            return left.byteOffset < right.byteOffset;
        });
        std::sort(data.materialSlots.Begin(), data.materialSlots.End(),
                  [](const mesh::MaterialSlotBuildRecord& left, const mesh::MaterialSlotBuildRecord& right) { return left.id < right.id; });
        std::sort(data.lods.Begin(), data.lods.End(),
                  [](const mesh::LodBuildRecord& left, const mesh::LodBuildRecord& right) { return left.level < right.level; });
        std::sort(data.submeshes.Begin(), data.submeshes.End(),
                  [](const mesh::SubmeshBuildRecord& left, const mesh::SubmeshBuildRecord& right)
        {
            return left.lod != right.lod ? left.lod < right.lod : left.stableId < right.stableId;
        });

        data.bufferRecords.Resize(data.buffers.Size());
        for (u32 index = 0; index < data.buffers.Size(); ++index)
        {
            const mesh::BufferBuildRecord& source = data.buffers[index];
            if (source.id == 0 || source.byteSize == 0 || source.stride == 0 || !IsValidBufferKind(source.kind) ||
                (index != 0 && data.buffers[index - 1].id == source.id))
            {
                return index != 0 && data.buffers[index - 1].id == source.id ? mesh::Result::DuplicateIdentifier :
                                                                             mesh::Result::InvalidBuffer;
            }
            if ((source.kind == mesh::BufferKind::Index && source.stride != 2 && source.stride != 4) ||
                source.byteSize % source.stride != 0)
            {
                return mesh::Result::InvalidBuffer;
            }
            mesh::BufferRecord& record = data.bufferRecords[index];
            record.kind = source.kind;
            record.stride = source.stride;
            record.byteSize = source.byteSize;
        }

        data.pageRecords.Reserve(data.pages.Size());
        u64 geometryOffset = 0;
        for (u32 index = 0; index < data.pages.Size(); ++index)
        {
            const mesh::PageBuildRecord& source = data.pages[index];
            const u32 bufferIndex = FindBufferIndex(data, source.bufferId);
            if (bufferIndex == mesh::InvalidRecordIndex || source.data == nullptr || source.byteSize == 0 ||
                source.byteSize > limits.maximumPageBytes || source.alignmentLog2 < 4 || source.alignmentLog2 > 20 ||
                (static_cast<u16>(source.flags) & ~KnownPageFlags) != 0)
            {
                return mesh::Result::InvalidPage;
            }
            const u64 byteSize = source.byteSize;
            const mesh::BufferBuildRecord& buffer = data.buffers[bufferIndex];
            if (source.bufferOffset > buffer.byteSize || byteSize > buffer.byteSize - source.bufferOffset)
            {
                return mesh::Result::InvalidPage;
            }
            mesh::BufferRecord& bufferRecord = data.bufferRecords[bufferIndex];
            if (bufferRecord.pageCount == 0)
            {
                bufferRecord.firstPage = data.pageRecords.Size();
            }
            const u64 expectedBufferOffset = bufferRecord.pageCount == 0
                                                 ? 0
                                                 : data.pageRecords[bufferRecord.firstPage + bufferRecord.pageCount - 1].bufferOffset +
                                                       data.pageRecords[bufferRecord.firstPage + bufferRecord.pageCount - 1].byteSize;
            if (source.bufferOffset != expectedBufferOffset)
            {
                return mesh::Result::InvalidPage;
            }
            geometryOffset = AlignUp(geometryOffset, 1ull << source.alignmentLog2);
            if (geometryOffset == ~u64{0} || byteSize > limits.maximumGeometryBytes - geometryOffset)
            {
                return mesh::Result::LimitExceeded;
            }
            mesh::PageRecord record;
            record.buffer = bufferIndex;
            record.flags = source.flags;
            record.alignmentLog2 = source.alignmentLog2;
            record.bufferOffset = source.bufferOffset;
            record.dataOffset = geometryOffset;
            record.byteSize = byteSize;
            record.digest = crypto::Sha256(source.data, source.byteSize);
            data.pageRecords.PushBack(record);
            ++bufferRecord.pageCount;
            geometryOffset += byteSize;
        }
        data.geometrySize = geometryOffset;
        for (u32 index = 0; index < data.bufferRecords.Size(); ++index)
        {
            const mesh::BufferRecord& buffer = data.bufferRecords[index];
            if (buffer.pageCount == 0)
            {
                return mesh::Result::InvalidBuffer;
            }
            const mesh::PageRecord& last = data.pageRecords[buffer.firstPage + buffer.pageCount - 1];
            if (last.bufferOffset + last.byteSize != buffer.byteSize)
            {
                return mesh::Result::InvalidPage;
            }
        }

        data.layoutRecords.Resize(data.vertexLayouts.Size());
        for (u32 index = 0; index < data.vertexLayouts.Size(); ++index)
        {
            if (data.vertexLayouts[index].id == 0 ||
                (index != 0 && data.vertexLayouts[index - 1].id == data.vertexLayouts[index].id))
            {
                return index != 0 && data.vertexLayouts[index - 1].id == data.vertexLayouts[index].id
                           ? mesh::Result::DuplicateIdentifier
                           : mesh::Result::InvalidLayout;
            }
        }

        data.streamRecords.Reserve(data.vertexStreams.Size());
        for (u32 index = 0; index < data.vertexStreams.Size(); ++index)
        {
            const mesh::VertexStreamBuildRecord& source = data.vertexStreams[index];
            const u32 layoutIndex = FindLayoutIndex(data, source.layoutId);
            if (layoutIndex == mesh::InvalidRecordIndex || !IsValidSemantic(source.semantic) ||
                !IsValidVertexFormat(source.format) || source.stride == 0 ||
                mesh::VertexFormatByteSize(source.format) == 0)
            {
                return mesh::Result::InvalidLayout;
            }
            if (index != 0 && data.vertexStreams[index - 1].layoutId == source.layoutId &&
                data.vertexStreams[index - 1].semantic == source.semantic &&
                data.vertexStreams[index - 1].semanticIndex == source.semanticIndex)
            {
                return mesh::Result::DuplicateStream;
            }
            const u32 bufferIndex = FindBufferIndex(data, source.bufferId);
            if (bufferIndex == mesh::InvalidRecordIndex || data.buffers[bufferIndex].kind != mesh::BufferKind::Vertex ||
                source.stride != data.buffers[bufferIndex].stride || source.byteOffset > source.stride ||
                mesh::VertexFormatByteSize(source.format) > source.stride - source.byteOffset)
            {
                return mesh::Result::InvalidBuffer;
            }
            mesh::VertexStream record;
            record.semantic = source.semantic;
            record.semanticIndex = source.semanticIndex;
            record.format = source.format;
            record.binding = source.binding;
            record.buffer = bufferIndex;
            record.byteOffset = source.byteOffset;
            record.stride = source.stride;
            data.streamRecords.PushBack(record);
            mesh::VertexLayoutRecord& layout = data.layoutRecords[layoutIndex];
            if (layout.streamCount == 0)
            {
                layout.firstStream = index;
            }
            ++layout.streamCount;
        }
        for (mesh::VertexLayoutRecord& layout : data.layoutRecords)
        {
            if (layout.streamCount == 0)
            {
                return mesh::Result::InvalidLayout;
            }
            bool hasPosition = false;
            ByteArray fingerprintBytes{memory::pools::Serialization::GetInstance()};
            filesystem::MemoryFileWriter fingerprintFile(fingerprintBytes);
            serialization::BinaryWriter fingerprintWriter(fingerprintFile);
            if (!fingerprintWriter.WriteU32(layout.streamCount))
            {
                return WriterResult(fingerprintWriter);
            }
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = data.streamRecords[layout.firstStream + streamOffset];
                hasPosition |= stream.semantic == mesh::VertexSemantic::Position && stream.semanticIndex == 0;
                if (!fingerprintWriter.WriteU8(static_cast<u8>(stream.semantic)) ||
                    !fingerprintWriter.WriteU8(stream.semanticIndex) ||
                    !fingerprintWriter.WriteU8(static_cast<u8>(stream.format)) ||
                    !fingerprintWriter.WriteU8(stream.binding) || !fingerprintWriter.WriteU32(stream.byteOffset) ||
                    !fingerprintWriter.WriteU32(stream.stride))
                {
                    return WriterResult(fingerprintWriter);
                }
            }
            if (!hasPosition)
            {
                return mesh::Result::MissingPositionStream;
            }
            layout.fingerprint = crypto::Sha256(fingerprintBytes.Data(), fingerprintBytes.Size());
        }

        data.materialRecords.Reserve(data.materialSlots.Size());
        for (u32 index = 0; index < data.materialSlots.Size(); ++index)
        {
            const mesh::MaterialSlotBuildRecord& source = data.materialSlots[index];
            if (source.id == 0 || source.name == 0 || !source.material.IsValid() || !source.material.IsTyped())
            {
                return mesh::Result::InvalidMaterial;
            }
            if (index != 0 && data.materialSlots[index - 1].id == source.id)
            {
                return mesh::Result::DuplicateIdentifier;
            }
            data.materialRecords.PushBack({source.name, source.material});
        }

        data.lodRecords.Resize(data.lods.Size());
        for (u32 index = 0; index < data.lods.Size(); ++index)
        {
            const mesh::LodBuildRecord& source = data.lods[index];
            if (source.level != index || !IsFinite(source.minimumScreenCoverage) || source.minimumScreenCoverage <= 0.0f ||
                source.minimumScreenCoverage > 1.0f ||
                (index != 0 && source.minimumScreenCoverage >= data.lods[index - 1].minimumScreenCoverage))
            {
                return mesh::Result::InvalidLod;
            }
            data.lodRecords[index].minimumScreenCoverage = source.minimumScreenCoverage;
        }

        data.submeshRecords.Reserve(data.submeshes.Size());
        for (u32 index = 0; index < data.submeshes.Size(); ++index)
        {
            const mesh::SubmeshBuildRecord& source = data.submeshes[index];
            if (source.stableId == 0 || source.name == 0 || source.lod >= data.lods.Size() || !IsValidIndexFormat(source.indexFormat) ||
                !IsValidTopology(source.topology) || !IsValidBounds(source.bounds) || source.vertexCount == 0 || source.indexCount == 0 ||
                (static_cast<u16>(source.flags) & ~KnownSubmeshFlags) != 0 ||
                (index != 0 && data.submeshes[index - 1].lod == source.lod &&
                 data.submeshes[index - 1].stableId == source.stableId))
            {
                return mesh::Result::InvalidSubmesh;
            }
            if ((source.topology == mesh::PrimitiveTopology::TriangleList && source.indexCount % 3 != 0) ||
                (source.topology == mesh::PrimitiveTopology::LineList && source.indexCount % 2 != 0))
            {
                return mesh::Result::InvalidSubmesh;
            }
            const u32 materialIndex = FindMaterialIndex(data, source.materialSlotId);
            const u32 vertexLayout = FindLayoutIndex(data, source.vertexLayoutId);
            const u32 indexBuffer = FindBufferIndex(data, source.indexBufferId);
            if (materialIndex == mesh::InvalidRecordIndex || vertexLayout == mesh::InvalidRecordIndex ||
                indexBuffer == mesh::InvalidRecordIndex ||
                data.buffers[indexBuffer].kind != mesh::BufferKind::Index ||
                data.buffers[indexBuffer].stride != (source.indexFormat == mesh::IndexFormat::UInt16 ? 2u : 4u))
            {
                return mesh::Result::InvalidSubmesh;
            }
            const u64 indexCapacity = data.buffers[indexBuffer].byteSize / data.buffers[indexBuffer].stride;
            if (source.firstIndex > indexCapacity || source.indexCount > indexCapacity - source.firstIndex)
            {
                return mesh::Result::InvalidSubmesh;
            }
            const mesh::VertexLayoutRecord& layout = data.layoutRecords[vertexLayout];
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = data.streamRecords[layout.firstStream + streamOffset];
                const u64 vertexCapacity = data.bufferRecords[stream.buffer].byteSize / stream.stride;
                if (source.firstVertex > vertexCapacity || source.vertexCount > vertexCapacity - source.firstVertex)
                {
                    return mesh::Result::InvalidSubmesh;
                }
            }
            mesh::SubmeshRecord record;
            record.stableId = source.stableId;
            record.name = source.name;
            record.materialSlot = materialIndex;
            record.vertexLayout = vertexLayout;
            record.indexBuffer = indexBuffer;
            record.indexFormat = source.indexFormat;
            record.topology = source.topology;
            record.flags = source.flags;
            record.firstVertex = source.firstVertex;
            record.vertexCount = source.vertexCount;
            record.firstIndex = source.firstIndex;
            record.indexCount = source.indexCount;
            record.bounds = source.bounds;
            data.submeshRecords.PushBack(record);

            mesh::LodRecord& lod = data.lodRecords[source.lod];
            if (lod.submeshCount == 0)
            {
                lod.firstSubmesh = index;
            }
            ++lod.submeshCount;
        }
        for (const mesh::LodRecord& lod : data.lodRecords)
        {
            if (lod.submeshCount == 0)
            {
                return mesh::Result::InvalidLod;
            }
        }
        const mesh::LodRecord& lowestLod = data.lodRecords.Back();
        const containers::ArraySpan<const mesh::BufferRecord> buffers{data.bufferRecords.TypedData(), data.bufferRecords.Size()};
        const containers::ArraySpan<const mesh::PageRecord> pages{data.pageRecords.TypedData(), data.pageRecords.Size()};
        for (u32 submeshOffset = 0; submeshOffset < lowestLod.submeshCount; ++submeshOffset)
        {
            const mesh::SubmeshRecord& submesh = data.submeshRecords[lowestLod.firstSubmesh + submeshOffset];
            const mesh::BufferRecord& indexBuffer = data.bufferRecords[submesh.indexBuffer];
            if (!RequiredPagesCoverRange(buffers, pages, submesh.indexBuffer, submesh.firstIndex * indexBuffer.stride,
                                         static_cast<u64>(submesh.indexCount) * indexBuffer.stride))
            {
                return mesh::Result::InvalidPage;
            }
            const mesh::VertexLayoutRecord& layout = data.layoutRecords[submesh.vertexLayout];
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = data.streamRecords[layout.firstStream + streamOffset];
                if (!RequiredPagesCoverRange(buffers, pages, stream.buffer, submesh.firstVertex * stream.stride,
                                             static_cast<u64>(submesh.vertexCount) * stream.stride))
                {
                    return mesh::Result::InvalidPage;
                }
            }
        }
        return mesh::Result::Success;
    }

    [[nodiscard]] mesh::Result WriteMetadata(ByteArray& metadata, const mesh::BuildDescription& description,
                                             const CanonicalData& data, const crypto::Digest256& contentFingerprint) noexcept
    {
        metadata.Clear();
        filesystem::MemoryFileWriter file(metadata);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(MetadataWireVersion) || !writer.WriteU8(static_cast<u8>(description.kind)) || !writer.WriteU8(0) ||
            !writer.WriteU16(0) || !writer.WriteU64(description.name) || !WriteBounds(writer, description.bounds) ||
            !WriteQuantization(writer, description.positionQuantization) || !WriteDigest(writer, description.sourceFingerprint) ||
            !WriteDigest(writer, contentFingerprint) || !writer.WriteU64(description.skeleton.Path().Id()) ||
            !writer.WriteU32(description.skeleton.ExpectedType()) || !writer.WriteU32(data.bufferRecords.Size()) ||
            !writer.WriteU32(data.pageRecords.Size()) || !writer.WriteU32(data.layoutRecords.Size()) ||
            !writer.WriteU32(data.streamRecords.Size()) ||
            !writer.WriteU32(data.materialRecords.Size()) || !writer.WriteU32(data.lodRecords.Size()) ||
            !writer.WriteU32(data.submeshRecords.Size()))
        {
            return WriterResult(writer);
        }
        for (const mesh::BufferRecord& record : data.bufferRecords)
        {
            if (!writer.WriteU8(static_cast<u8>(record.kind)) || !writer.WriteU8(0) || !writer.WriteU16(0) ||
                !writer.WriteU32(record.stride) || !writer.WriteU64(record.byteSize) || !writer.WriteU32(record.firstPage) ||
                !writer.WriteU32(record.pageCount))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::PageRecord& record : data.pageRecords)
        {
            if (!writer.WriteU32(record.buffer) || !writer.WriteU16(static_cast<u16>(record.flags)) ||
                !writer.WriteU8(record.alignmentLog2) || !writer.WriteU8(0) || !writer.WriteU64(record.bufferOffset) ||
                !writer.WriteU64(record.dataOffset) || !writer.WriteU64(record.byteSize) || !WriteDigest(writer, record.digest))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::VertexLayoutRecord& record : data.layoutRecords)
        {
            if (!writer.WriteU32(record.firstStream) || !writer.WriteU32(record.streamCount) ||
                !WriteDigest(writer, record.fingerprint))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::VertexStream& record : data.streamRecords)
        {
            if (!writer.WriteU8(static_cast<u8>(record.semantic)) || !writer.WriteU8(record.semanticIndex) ||
                !writer.WriteU8(static_cast<u8>(record.format)) || !writer.WriteU8(record.binding) || !writer.WriteU32(record.buffer) ||
                !writer.WriteU32(record.byteOffset) || !writer.WriteU32(record.stride))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::MaterialSlot& record : data.materialRecords)
        {
            if (!writer.WriteU64(record.name) || !writer.WriteU64(record.material.Path().Id()) ||
                !writer.WriteU32(record.material.ExpectedType()) || !writer.WriteU32(0))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::LodRecord& record : data.lodRecords)
        {
            if (!writer.WriteF32(record.minimumScreenCoverage) || !writer.WriteU32(record.firstSubmesh) ||
                !writer.WriteU32(record.submeshCount) || !writer.WriteU32(0))
            {
                return WriterResult(writer);
            }
        }
        for (const mesh::SubmeshRecord& record : data.submeshRecords)
        {
            if (!writer.WriteU64(record.stableId) || !writer.WriteU64(record.name) ||
                !writer.WriteU32(record.materialSlot) || !writer.WriteU32(record.vertexLayout) ||
                !writer.WriteU32(record.indexBuffer) || !writer.WriteU32(0) ||
                !writer.WriteU8(static_cast<u8>(record.indexFormat)) ||
                !writer.WriteU8(static_cast<u8>(record.topology)) || !writer.WriteU16(static_cast<u16>(record.flags)) ||
                !writer.WriteU64(record.firstVertex) || !writer.WriteU32(record.vertexCount) || !writer.WriteU32(0) ||
                !writer.WriteU64(record.firstIndex) || !writer.WriteU32(record.indexCount) || !writer.WriteU32(0) ||
                !WriteBounds(writer, record.bounds))
            {
                return WriterResult(writer);
            }
        }
        return writer.Flush() ? mesh::Result::Success : WriterResult(writer);
    }

    [[nodiscard]] bool WriteZeroBytes(serialization::BinaryWriter& writer, u64 count, u64& checksum) noexcept
    {
        constexpr u8 zeros[4096]{};
        while (count != 0)
        {
            const usize batch = count > sizeof(zeros) ? sizeof(zeros) : static_cast<usize>(count);
            if (!writer.WriteBytes(zeros, batch))
            {
                return false;
            }
            checksum = serialization::Crc64(zeros, batch, checksum);
            count -= batch;
        }
        return true;
    }

    [[nodiscard]] mesh::Result WriteDocument(filesystem::IFile& file, const ByteArray& metadata,
                                             const CanonicalData& data) noexcept
    {
        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = mesh::MeshMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 2;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        const u8 emptySectionTable[serialization::SectionDescriptor::WireSize * 2]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)))
        {
            return WriterResult(writer);
        }
        header.sectionTableOffset = writer.Position();
        if (!writer.WriteBytes(emptySectionTable, sizeof(emptySectionTable)) || !writer.Align(GeometryAlignment))
        {
            return WriterResult(writer);
        }

        serialization::SectionDescriptor sections[2];
        sections[0].id = MetadataSection;
        sections[0].version = {1, 0};
        sections[0].alignmentLog2 = GeometryAlignmentLog2;
        sections[0].offset = writer.Position();
        sections[0].storedSize = metadata.Size();
        sections[0].logicalSize = metadata.Size();
        sections[0].storedCrc64 = serialization::Crc64(metadata.Data(), metadata.Size());
        if (!writer.WriteBytes(metadata.Data(), metadata.Size()) || !writer.Align(GeometryAlignment))
        {
            return WriterResult(writer);
        }

        sections[1].id = GeometrySection;
        sections[1].version = {1, 0};
        sections[1].flags = serialization::SectionFlags::Streamable;
        sections[1].alignmentLog2 = GeometryAlignmentLog2;
        sections[1].offset = writer.Position();
        sections[1].storedSize = data.geometrySize;
        sections[1].logicalSize = data.geometrySize;
        u64 geometryPosition = 0;
        u64 geometryCrc = 0;
        for (u32 index = 0; index < data.pageRecords.Size(); ++index)
        {
            const mesh::PageRecord& record = data.pageRecords[index];
            if (record.dataOffset < geometryPosition ||
                !WriteZeroBytes(writer, record.dataOffset - geometryPosition, geometryCrc))
            {
                return mesh::Result::IoFailure;
            }
            const mesh::PageBuildRecord& source = data.pages[index];
            if (!writer.WriteBytes(source.data, source.byteSize))
            {
                return WriterResult(writer);
            }
            geometryCrc = serialization::Crc64(source.data, source.byteSize, geometryCrc);
            geometryPosition = record.dataOffset + record.byteSize;
        }
        if (geometryPosition != data.geometrySize)
        {
            return mesh::Result::InvalidState;
        }
        sections[1].storedCrc64 = geometryCrc;
        if (!writer.Align(GeometryAlignment))
        {
            return WriterResult(writer);
        }

        header.fileSize = writer.Position();
        if (!writer.Seek(header.sectionTableOffset))
        {
            return WriterResult(writer);
        }
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success)
            {
                return WriterResult(writer);
            }
        }
        if (!writer.Seek(0) || serialization::WriteDocumentHeader(writer, header) != serialization::Result::Success ||
            !writer.Seek(header.fileSize) || !writer.Flush())
        {
            return WriterResult(writer);
        }
        return mesh::Result::Success;
    }

    [[nodiscard]] const serialization::SectionDescriptor* FindSection(
        const containers::ArraySpan<const serialization::SectionDescriptor> sections, const u32 id) noexcept
    {
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (section.id == id)
            {
                return &section;
            }
        }
        return nullptr;
    }

    template <typename Type>
    [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit)
        {
            return false;
        }
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] mesh::Result ValidateLoadedRecords(const mesh::MeshFile& file) noexcept
    {
        const auto buffers = file.Buffers();
        const auto pages = file.Pages();
        const auto layouts = file.VertexLayouts();
        const auto streams = file.VertexStreams();
        const auto materials = file.MaterialSlots();
        const auto lods = file.Lods();
        const auto submeshes = file.Submeshes();
        if (buffers.Empty() || pages.Empty() || layouts.Empty() || streams.Empty() || lods.Empty() || submeshes.Empty() ||
            !IsValidMeshKind(file.Kind()) || !IsValidBounds(file.MeshBounds()) || !IsValidQuantization(file.Quantization()))
        {
            return mesh::Result::InvalidLayout;
        }
        if ((file.Kind() == mesh::MeshKind::Skinned) != file.Skeleton().IsValid() ||
            (file.Skeleton().IsValid() && !file.Skeleton().IsTyped()))
        {
            return mesh::Result::InvalidLayout;
        }
        for (u32 bufferIndex = 0; bufferIndex < buffers.Size(); ++bufferIndex)
        {
            const mesh::BufferRecord& buffer = buffers[bufferIndex];
            if (!IsValidBufferKind(buffer.kind) || buffer.stride == 0 || buffer.byteSize == 0 || buffer.pageCount == 0 ||
                buffer.firstPage > pages.Size() || buffer.pageCount > pages.Size() - buffer.firstPage ||
                (buffer.kind == mesh::BufferKind::Index && buffer.stride != 2 && buffer.stride != 4) ||
                buffer.byteSize % buffer.stride != 0)
            {
                return mesh::Result::InvalidBuffer;
            }
            u64 expectedOffset = 0;
            for (u32 pageOffset = 0; pageOffset < buffer.pageCount; ++pageOffset)
            {
                const mesh::PageRecord& page = pages[buffer.firstPage + pageOffset];
                if (page.buffer != bufferIndex || page.bufferOffset != expectedOffset || page.byteSize == 0 ||
                    page.bufferOffset > buffer.byteSize || page.byteSize > buffer.byteSize - page.bufferOffset ||
                    page.alignmentLog2 < 4 || page.alignmentLog2 > 20 ||
                    (static_cast<u16>(page.flags) & ~KnownPageFlags) != 0)
                {
                    return mesh::Result::InvalidPage;
                }
                expectedOffset += page.byteSize;
            }
            if (expectedOffset != buffer.byteSize)
            {
                return mesh::Result::InvalidPage;
            }
        }
        for (u32 index = 0; index < streams.Size(); ++index)
        {
            const mesh::VertexStream& stream = streams[index];
            if (!IsValidSemantic(stream.semantic) || !IsValidVertexFormat(stream.format) || stream.buffer >= buffers.Size() ||
                buffers[stream.buffer].kind != mesh::BufferKind::Vertex || stream.stride != buffers[stream.buffer].stride ||
                stream.byteOffset > stream.stride || mesh::VertexFormatByteSize(stream.format) > stream.stride - stream.byteOffset)
            {
                return mesh::Result::InvalidLayout;
            }
        }
        u32 expectedFirstStream = 0;
        for (const mesh::VertexLayoutRecord& layout : layouts)
        {
            if (layout.streamCount == 0 || layout.firstStream != expectedFirstStream || layout.firstStream > streams.Size() ||
                layout.streamCount > streams.Size() - layout.firstStream)
            {
                return mesh::Result::InvalidLayout;
            }
            bool hasPosition = false;
            ByteArray fingerprintBytes{memory::pools::Serialization::GetInstance()};
            filesystem::MemoryFileWriter fingerprintFile(fingerprintBytes);
            serialization::BinaryWriter fingerprintWriter(fingerprintFile);
            if (!fingerprintWriter.WriteU32(layout.streamCount))
            {
                return mesh::Result::InvalidLayout;
            }
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = streams[layout.firstStream + streamOffset];
                if (streamOffset != 0)
                {
                    const mesh::VertexStream& previous = streams[layout.firstStream + streamOffset - 1];
                    if (previous.semantic == stream.semantic && previous.semanticIndex == stream.semanticIndex)
                    {
                        return mesh::Result::DuplicateStream;
                    }
                }
                hasPosition |= stream.semantic == mesh::VertexSemantic::Position && stream.semanticIndex == 0;
                if (!fingerprintWriter.WriteU8(static_cast<u8>(stream.semantic)) ||
                    !fingerprintWriter.WriteU8(stream.semanticIndex) ||
                    !fingerprintWriter.WriteU8(static_cast<u8>(stream.format)) ||
                    !fingerprintWriter.WriteU8(stream.binding) || !fingerprintWriter.WriteU32(stream.byteOffset) ||
                    !fingerprintWriter.WriteU32(stream.stride))
                {
                    return mesh::Result::InvalidLayout;
                }
            }
            if (!hasPosition || crypto::Sha256(fingerprintBytes.Data(), fingerprintBytes.Size()) != layout.fingerprint)
            {
                return hasPosition ? mesh::Result::IntegrityFailure : mesh::Result::MissingPositionStream;
            }
            expectedFirstStream += layout.streamCount;
        }
        if (expectedFirstStream != streams.Size())
        {
            return mesh::Result::InvalidLayout;
        }
        for (const mesh::MaterialSlot& material : materials)
        {
            if (material.name == 0 || !material.material.IsValid() || !material.material.IsTyped())
            {
                return mesh::Result::InvalidMaterial;
            }
        }
        u32 expectedFirstSubmesh = 0;
        f32 previousCoverage = 2.0f;
        for (const mesh::LodRecord& lod : lods)
        {
            if (!IsFinite(lod.minimumScreenCoverage) || lod.minimumScreenCoverage <= 0.0f ||
                lod.minimumScreenCoverage > 1.0f || lod.minimumScreenCoverage >= previousCoverage || lod.submeshCount == 0 ||
                lod.firstSubmesh != expectedFirstSubmesh || lod.firstSubmesh > submeshes.Size() ||
                lod.submeshCount > submeshes.Size() - lod.firstSubmesh)
            {
                return mesh::Result::InvalidLod;
            }
            expectedFirstSubmesh += lod.submeshCount;
            previousCoverage = lod.minimumScreenCoverage;
        }
        if (expectedFirstSubmesh != submeshes.Size())
        {
            return mesh::Result::InvalidLod;
        }
        for (u32 submeshIndex = 0; submeshIndex < submeshes.Size(); ++submeshIndex)
        {
            const mesh::SubmeshRecord& submesh = submeshes[submeshIndex];
            if (submesh.stableId == 0 || submesh.name == 0 || submesh.materialSlot >= materials.Size() ||
                submesh.vertexLayout >= layouts.Size() ||
                submesh.indexBuffer >= buffers.Size() || buffers[submesh.indexBuffer].kind != mesh::BufferKind::Index ||
                !IsValidIndexFormat(submesh.indexFormat) || !IsValidTopology(submesh.topology) ||
                !IsValidBounds(submesh.bounds) || submesh.vertexCount == 0 || submesh.indexCount == 0 ||
                (static_cast<u16>(submesh.flags) & ~KnownSubmeshFlags) != 0 ||
                buffers[submesh.indexBuffer].stride != (submesh.indexFormat == mesh::IndexFormat::UInt16 ? 2u : 4u))
            {
                return mesh::Result::InvalidSubmesh;
            }
            const u64 indexCapacity = buffers[submesh.indexBuffer].byteSize / buffers[submesh.indexBuffer].stride;
            if (submesh.firstIndex > indexCapacity || submesh.indexCount > indexCapacity - submesh.firstIndex ||
                (submesh.topology == mesh::PrimitiveTopology::TriangleList && submesh.indexCount % 3 != 0) ||
                (submesh.topology == mesh::PrimitiveTopology::LineList && submesh.indexCount % 2 != 0))
            {
                return mesh::Result::InvalidSubmesh;
            }
            const mesh::VertexLayoutRecord& layout = layouts[submesh.vertexLayout];
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = streams[layout.firstStream + streamOffset];
                const u64 vertexCapacity = buffers[stream.buffer].byteSize / stream.stride;
                if (submesh.firstVertex > vertexCapacity || submesh.vertexCount > vertexCapacity - submesh.firstVertex)
                {
                    return mesh::Result::InvalidSubmesh;
                }
            }
        }
        const mesh::LodRecord& lowestLod = lods.Back();
        for (u32 submeshOffset = 0; submeshOffset < lowestLod.submeshCount; ++submeshOffset)
        {
            const mesh::SubmeshRecord& submesh = submeshes[lowestLod.firstSubmesh + submeshOffset];
            const mesh::BufferRecord& indexBuffer = buffers[submesh.indexBuffer];
            if (!RequiredPagesCoverRange(buffers, pages, submesh.indexBuffer, submesh.firstIndex * indexBuffer.stride,
                                         static_cast<u64>(submesh.indexCount) * indexBuffer.stride))
            {
                return mesh::Result::InvalidPage;
            }
            const mesh::VertexLayoutRecord& layout = layouts[submesh.vertexLayout];
            for (u32 streamOffset = 0; streamOffset < layout.streamCount; ++streamOffset)
            {
                const mesh::VertexStream& stream = streams[layout.firstStream + streamOffset];
                if (!RequiredPagesCoverRange(buffers, pages, stream.buffer, submesh.firstVertex * stream.stride,
                                             static_cast<u64>(submesh.vertexCount) * stream.stride))
                {
                    return mesh::Result::InvalidPage;
                }
            }
        }
        return mesh::Result::Success;
    }
} // namespace

namespace vanguard::meshes
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
        case Result::InvalidMagic:
            return "InvalidMagic";
        case Result::UnsupportedVersion:
            return "UnsupportedVersion";
        case Result::InvalidLayout:
            return "InvalidLayout";
        case Result::IntegrityFailure:
            return "IntegrityFailure";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::DuplicateIdentifier:
            return "DuplicateIdentifier";
        case Result::DuplicateStream:
            return "DuplicateStream";
        case Result::MissingPositionStream:
            return "MissingPositionStream";
        case Result::InvalidLod:
            return "InvalidLod";
        case Result::InvalidSubmesh:
            return "InvalidSubmesh";
        case Result::InvalidBuffer:
            return "InvalidBuffer";
        case Result::InvalidPage:
            return "InvalidPage";
        case Result::InvalidMaterial:
            return "InvalidMaterial";
        case Result::BufferTooSmall:
            return "BufferTooSmall";
        case Result::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    u32 VertexFormatByteSize(const VertexFormat format) noexcept
    {
        switch (format)
        {
        case VertexFormat::R32Float:
        case VertexFormat::R16G16Float:
        case VertexFormat::R16G16SNorm:
        case VertexFormat::R16G16UNorm:
        case VertexFormat::R8G8B8A8UNorm:
        case VertexFormat::R8G8B8A8SNorm:
        case VertexFormat::R8G8B8A8UInt:
        case VertexFormat::R32UInt:
        case VertexFormat::R10G10B10A2UNorm:
            return 4;
        case VertexFormat::R32G32Float:
        case VertexFormat::R16G16B16A16Float:
        case VertexFormat::R16G16B16A16SNorm:
        case VertexFormat::R16G16B16A16UNorm:
        case VertexFormat::R16G16B16A16UInt:
            return 8;
        case VertexFormat::R32G32B32Float:
            return 12;
        case VertexFormat::R32G32B32A32Float:
            return 16;
        }
        return 0;
    }

    MeshFile::MeshFile() noexcept
        : m_buffers(memory::pools::Rendering::GetInstance()), m_pages(memory::pools::Rendering::GetInstance()),
          m_vertexLayouts(memory::pools::Rendering::GetInstance()), m_vertexStreams(memory::pools::Rendering::GetInstance()),
          m_materialSlots(memory::pools::Rendering::GetInstance()),
          m_lods(memory::pools::Rendering::GetInstance()), m_submeshes(memory::pools::Rendering::GetInstance())
    {
    }

    Result MeshFile::Open(filesystem::IFile& reader, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader documentReader(reader);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 2;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(documentReader, MeshMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(headerResult);
        }
        containers::DynamicArray<serialization::SectionDescriptor> sections{memory::pools::Serialization::GetInstance()};
        const serialization::Result sectionResult =
            serialization::ReadSectionTable(documentReader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(sectionResult);
        }
        const serialization::SectionDescriptor* const metadataSection = FindSection(sections, MetadataSection);
        const serialization::SectionDescriptor* const geometrySection = FindSection(sections, GeometrySection);
        if (metadataSection == nullptr || geometrySection == nullptr || metadataSection->version != serialization::Version{1, 0} ||
            geometrySection->version != serialization::Version{1, 0} || metadataSection->codec != serialization::Codec::None ||
            geometrySection->codec != serialization::Codec::None || metadataSection->storedSize != metadataSection->logicalSize ||
            geometrySection->storedSize != geometrySection->logicalSize ||
            (static_cast<u32>(geometrySection->flags) & static_cast<u32>(serialization::SectionFlags::Streamable)) == 0 ||
            metadataSection->storedSize > limits.maximumMetadataBytes || geometrySection->storedSize > limits.maximumGeometryBytes ||
            metadataSection->storedSize > ~u32{0})
        {
            return Result::InvalidLayout;
        }

        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        metadata.Resize(static_cast<u32>(metadataSection->storedSize));
        if (!documentReader.Seek(metadataSection->offset) || !documentReader.ReadBytes(metadata.Data(), metadata.Size()))
        {
            return ReaderResult(documentReader);
        }
        if (serialization::Crc64(metadata.Data(), metadata.Size()) != metadataSection->storedCrc64)
        {
            return Result::IntegrityFailure;
        }
        filesystem::MemoryFileReader metadataFile(metadata, 0);
        serialization::BinaryReader metadataReader(metadataFile);
        u32 metadataVersion = 0;
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        u64 skeletonPath = 0;
        u32 skeletonType = 0;
        u32 bufferCount = 0;
        u32 pageCount = 0;
        u32 layoutCount = 0;
        u32 streamCount = 0;
        u32 materialCount = 0;
        u32 lodCount = 0;
        u32 submeshCount = 0;
        if (!metadataReader.ReadU32(metadataVersion) || !metadataReader.ReadU8(kind) || !metadataReader.ReadU8(reserved8) ||
            !metadataReader.ReadU16(reserved16) || !metadataReader.ReadU64(m_name) || !ReadBounds(metadataReader, m_bounds) ||
            !ReadQuantization(metadataReader, m_quantization) || !ReadDigest(metadataReader, m_sourceFingerprint) ||
            !ReadDigest(metadataReader, m_contentFingerprint) || !metadataReader.ReadU64(skeletonPath) ||
            !metadataReader.ReadU32(skeletonType) || !metadataReader.ReadU32(bufferCount) ||
            !metadataReader.ReadU32(pageCount) || !metadataReader.ReadU32(layoutCount) ||
            !metadataReader.ReadU32(streamCount) ||
            !metadataReader.ReadU32(materialCount) || !metadataReader.ReadU32(lodCount) ||
            !metadataReader.ReadU32(submeshCount))
        {
            Close();
            return ReaderResult(metadataReader);
        }
        if (metadataVersion != MetadataWireVersion || reserved8 != 0 || reserved16 != 0 ||
            !IsValidMeshKind(static_cast<MeshKind>(kind)) || m_name == 0)
        {
            Close();
            return Result::InvalidLayout;
        }
        if (!ResizeChecked(m_buffers, bufferCount, limits.maximumBuffers) ||
            !ResizeChecked(m_pages, pageCount, limits.maximumPages) ||
            !ResizeChecked(m_vertexLayouts, layoutCount, limits.maximumVertexLayouts) ||
            !ResizeChecked(m_vertexStreams, streamCount, limits.maximumVertexStreams) ||
            !ResizeChecked(m_materialSlots, materialCount, limits.maximumMaterialSlots) ||
            !ResizeChecked(m_lods, lodCount, limits.maximumLods) ||
            !ResizeChecked(m_submeshes, submeshCount, limits.maximumSubmeshes))
        {
            Close();
            return Result::LimitExceeded;
        }
        m_kind = static_cast<MeshKind>(kind);
        m_skeleton = resources::ResourceReference(resources::ResourcePath::FromId(skeletonPath), skeletonType);

        for (BufferRecord& record : m_buffers)
        {
            u8 bufferKind = 0;
            if (!metadataReader.ReadU8(bufferKind) || !metadataReader.ReadU8(reserved8) ||
                !metadataReader.ReadU16(reserved16) || !metadataReader.ReadU32(record.stride) ||
                !metadataReader.ReadU64(record.byteSize) || !metadataReader.ReadU32(record.firstPage) ||
                !metadataReader.ReadU32(record.pageCount) || reserved8 != 0 || reserved16 != 0)
            {
                Close();
                return Result::InvalidLayout;
            }
            record.kind = static_cast<BufferKind>(bufferKind);
        }
        for (PageRecord& record : m_pages)
        {
            u16 flags = 0;
            if (!metadataReader.ReadU32(record.buffer) || !metadataReader.ReadU16(flags) ||
                !metadataReader.ReadU8(record.alignmentLog2) || !metadataReader.ReadU8(reserved8) ||
                !metadataReader.ReadU64(record.bufferOffset) || !metadataReader.ReadU64(record.dataOffset) ||
                !metadataReader.ReadU64(record.byteSize) || !ReadDigest(metadataReader, record.digest) || reserved8 != 0 ||
                record.byteSize > limits.maximumPageBytes || record.dataOffset > geometrySection->storedSize ||
                record.byteSize > geometrySection->storedSize - record.dataOffset)
            {
                Close();
                return Result::InvalidPage;
            }
            record.flags = static_cast<PageFlags>(flags);
        }
        for (VertexLayoutRecord& record : m_vertexLayouts)
        {
            if (!metadataReader.ReadU32(record.firstStream) || !metadataReader.ReadU32(record.streamCount) ||
                !ReadDigest(metadataReader, record.fingerprint))
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        for (VertexStream& record : m_vertexStreams)
        {
            u8 semantic = 0;
            u8 format = 0;
            if (!metadataReader.ReadU8(semantic) || !metadataReader.ReadU8(record.semanticIndex) ||
                !metadataReader.ReadU8(format) || !metadataReader.ReadU8(record.binding) ||
                !metadataReader.ReadU32(record.buffer) || !metadataReader.ReadU32(record.byteOffset) ||
                !metadataReader.ReadU32(record.stride))
            {
                Close();
                return Result::InvalidLayout;
            }
            record.semantic = static_cast<VertexSemantic>(semantic);
            record.format = static_cast<VertexFormat>(format);
        }
        for (MaterialSlot& record : m_materialSlots)
        {
            u64 path = 0;
            u32 type = 0;
            u32 reserved = 0;
            if (!metadataReader.ReadU64(record.name) || !metadataReader.ReadU64(path) || !metadataReader.ReadU32(type) ||
                !metadataReader.ReadU32(reserved) || reserved != 0)
            {
                Close();
                return Result::InvalidLayout;
            }
            record.material = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
        }
        for (LodRecord& record : m_lods)
        {
            u32 reserved = 0;
            if (!metadataReader.ReadF32(record.minimumScreenCoverage) || !metadataReader.ReadU32(record.firstSubmesh) ||
                !metadataReader.ReadU32(record.submeshCount) || !metadataReader.ReadU32(reserved) || reserved != 0)
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        for (SubmeshRecord& record : m_submeshes)
        {
            u8 indexFormat = 0;
            u8 topology = 0;
            u16 flags = 0;
            u32 reserved = 0;
            if (!metadataReader.ReadU64(record.stableId) || !metadataReader.ReadU64(record.name) ||
                !metadataReader.ReadU32(record.materialSlot) || !metadataReader.ReadU32(record.vertexLayout) ||
                !metadataReader.ReadU32(record.indexBuffer) || !metadataReader.ReadU32(reserved) || reserved != 0 ||
                !metadataReader.ReadU8(indexFormat) ||
                !metadataReader.ReadU8(topology) || !metadataReader.ReadU16(flags) ||
                !metadataReader.ReadU64(record.firstVertex) || !metadataReader.ReadU32(record.vertexCount) ||
                !metadataReader.ReadU32(reserved) || reserved != 0 || !metadataReader.ReadU64(record.firstIndex) ||
                !metadataReader.ReadU32(record.indexCount) || !metadataReader.ReadU32(reserved) || reserved != 0 ||
                !ReadBounds(metadataReader, record.bounds))
            {
                Close();
                return Result::InvalidLayout;
            }
            record.indexFormat = static_cast<IndexFormat>(indexFormat);
            record.topology = static_cast<PrimitiveTopology>(topology);
            record.flags = static_cast<SubmeshFlags>(flags);
        }
        if (metadataReader.Position() != metadataReader.Size() || metadata.Size() < ContentFingerprintOffset + crypto::Digest256::ByteCount)
        {
            Close();
            return Result::InvalidLayout;
        }

        const crypto::Digest256 declaredFingerprint = m_contentFingerprint;
        for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
        {
            metadata[static_cast<u32>(ContentFingerprintOffset) + index] = 0;
        }
        if (crypto::Sha256(metadata.Data(), metadata.Size()) != declaredFingerprint)
        {
            Close();
            return Result::IntegrityFailure;
        }
        m_contentFingerprint = declaredFingerprint;
        m_geometryOffset = geometrySection->offset;
        m_geometrySize = geometrySection->storedSize;
        m_open = true;
        const Result validation = ValidateLoadedRecords(*this);
        if (validation != Result::Success)
        {
            Close();
            return validation;
        }
        return Result::Success;
    }

    void MeshFile::Close() noexcept
    {
        m_open = false;
        m_kind = MeshKind::Static;
        m_name = 0;
        m_bounds = {};
        m_quantization = {};
        m_sourceFingerprint = {};
        m_contentFingerprint = {};
        m_skeleton = {};
        m_geometryOffset = 0;
        m_geometrySize = 0;
        m_buffers.Clear();
        m_pages.Clear();
        m_vertexLayouts.Clear();
        m_vertexStreams.Clear();
        m_materialSlots.Clear();
        m_lods.Clear();
        m_submeshes.Clear();
    }

    bool MeshFile::IsOpen() const noexcept
    {
        return m_open;
    }

    MeshKind MeshFile::Kind() const noexcept
    {
        return m_kind;
    }

    u64 MeshFile::Name() const noexcept
    {
        return m_name;
    }

    const Bounds& MeshFile::MeshBounds() const noexcept
    {
        return m_bounds;
    }

    const PositionQuantization& MeshFile::Quantization() const noexcept
    {
        return m_quantization;
    }

    const crypto::Digest256& MeshFile::SourceFingerprint() const noexcept
    {
        return m_sourceFingerprint;
    }

    const crypto::Digest256& MeshFile::ContentFingerprint() const noexcept
    {
        return m_contentFingerprint;
    }

    resources::ResourceReference MeshFile::Skeleton() const noexcept
    {
        return m_skeleton;
    }

    containers::ArraySpan<const BufferRecord> MeshFile::Buffers() const noexcept
    {
        return m_buffers;
    }

    containers::ArraySpan<const PageRecord> MeshFile::Pages() const noexcept
    {
        return m_pages;
    }

    containers::ArraySpan<const VertexLayoutRecord> MeshFile::VertexLayouts() const noexcept
    {
        return m_vertexLayouts;
    }

    containers::ArraySpan<const VertexStream> MeshFile::VertexStreams() const noexcept
    {
        return m_vertexStreams;
    }

    containers::ArraySpan<const MaterialSlot> MeshFile::MaterialSlots() const noexcept
    {
        return m_materialSlots;
    }

    containers::ArraySpan<const LodRecord> MeshFile::Lods() const noexcept
    {
        return m_lods;
    }

    containers::ArraySpan<const SubmeshRecord> MeshFile::Submeshes() const noexcept
    {
        return m_submeshes;
    }

    u64 MeshFile::GeometryOffset() const noexcept
    {
        return m_geometryOffset;
    }

    u64 MeshFile::GeometrySize() const noexcept
    {
        return m_geometrySize;
    }

    Result BuildStorageSegments(const MeshFile& mesh, const u64 documentSize,
                                containers::DynamicArray<StorageSegment>& segments,
                                const u32 maximumSegments) noexcept
    {
        segments.Clear();
        if (!mesh.IsOpen() || maximumSegments == 0 || mesh.Pages().Empty() ||
            mesh.Pages().Size() >= maximumSegments || mesh.GeometryOffset() > documentSize ||
            mesh.GeometrySize() > documentSize - mesh.GeometryOffset())
        {
            return Result::InvalidArgument;
        }

        containers::DynamicArray<u32> pageOrder(memory::pools::Serialization::GetInstance());
        pageOrder.Resize(mesh.Pages().Size());
        if (pageOrder.Size() != mesh.Pages().Size())
        {
            return Result::LimitExceeded;
        }
        for (u32 index = 0; index < pageOrder.Size(); ++index)
        {
            pageOrder[index] = index;
        }
        for (u32 index = 1; index < pageOrder.Size(); ++index)
        {
            const u32 page = pageOrder[index];
            u32 insertion = index;
            while (insertion > 0 && mesh.Pages()[page].dataOffset < mesh.Pages()[pageOrder[insertion - 1u]].dataOffset)
            {
                pageOrder[insertion] = pageOrder[insertion - 1u];
                --insertion;
            }
            pageOrder[insertion] = page;
        }

        const u64 firstPageOffset = mesh.GeometryOffset() + mesh.Pages()[pageOrder[0]].dataOffset;
        if (firstPageOffset == 0 || firstPageOffset > documentSize)
        {
            return Result::InvalidLayout;
        }
        segments.Reserve(pageOrder.Size() + 1u);
        segments.PushBack({0, firstPageOffset, 4, StorageSegmentFlags::Metadata, InvalidRecordIndex});
        for (u32 orderIndex = 0; orderIndex < pageOrder.Size(); ++orderIndex)
        {
            const u32 pageIndex = pageOrder[orderIndex];
            const PageRecord& page = mesh.Pages()[pageIndex];
            const u64 begin = mesh.GeometryOffset() + page.dataOffset;
            const u64 end = orderIndex + 1u < pageOrder.Size()
                ? mesh.GeometryOffset() + mesh.Pages()[pageOrder[orderIndex + 1u]].dataOffset
                : documentSize;
            if (begin >= end || page.byteSize > end - begin)
            {
                segments.Clear();
                return Result::InvalidLayout;
            }
            StorageSegmentFlags flags = StorageSegmentFlags::Streamable;
            if (HasFlag(page.flags, PageFlags::RequiredForLowestLod))
            {
                flags = flags | StorageSegmentFlags::RequiredForLowestLod;
            }
            segments.PushBack({begin, end - begin, page.alignmentLog2, flags, pageIndex});
        }
        return segments.Size() == pageOrder.Size() + 1u ? Result::Success : Result::LimitExceeded;
    }

    Result CollectLodPages(const MeshFile& mesh, const u16 lod, containers::DynamicArray<u32>& pages,
                           const u32 maximumPages) noexcept
    {
        pages.Clear();
        if (!mesh.IsOpen() || lod >= mesh.Lods().Size() || maximumPages == 0)
        {
            return Result::InvalidArgument;
        }

        const auto addBufferPages = [&](const u32 bufferIndex, const u64 firstElement,
                                        const u32 elementCount) -> Result
        {
            if (bufferIndex >= mesh.Buffers().Size())
            {
                return Result::InvalidBuffer;
            }
            const BufferRecord& buffer = mesh.Buffers()[bufferIndex];
            if (firstElement > ~u64{0} / buffer.stride ||
                elementCount > (~u64{0} - firstElement * buffer.stride) / buffer.stride)
            {
                return Result::InvalidBuffer;
            }
            const u64 begin = firstElement * buffer.stride;
            const u64 end = begin + static_cast<u64>(elementCount) * buffer.stride;
            for (u32 localPage = 0; localPage < buffer.pageCount; ++localPage)
            {
                const u32 pageIndex = buffer.firstPage + localPage;
                const PageRecord& page = mesh.Pages()[pageIndex];
                const u64 pageEnd = page.bufferOffset + page.byteSize;
                if (page.bufferOffset >= end || pageEnd <= begin)
                {
                    continue;
                }
                bool duplicate = false;
                for (const u32 existing : pages)
                {
                    duplicate = duplicate || existing == pageIndex;
                }
                if (!duplicate)
                {
                    if (pages.Size() >= maximumPages)
                    {
                        return Result::LimitExceeded;
                    }
                    pages.PushBack(pageIndex);
                }
            }
            return Result::Success;
        };

        const LodRecord& selectedLod = mesh.Lods()[lod];
        for (u32 localSubmesh = 0; localSubmesh < selectedLod.submeshCount; ++localSubmesh)
        {
            const SubmeshRecord& submesh = mesh.Submeshes()[selectedLod.firstSubmesh + localSubmesh];
            const VertexLayoutRecord& layout = mesh.VertexLayouts()[submesh.vertexLayout];
            for (u32 localStream = 0; localStream < layout.streamCount; ++localStream)
            {
                const VertexStream& stream = mesh.VertexStreams()[layout.firstStream + localStream];
                const Result result = addBufferPages(stream.buffer, submesh.firstVertex, submesh.vertexCount);
                if (result != Result::Success)
                {
                    pages.Clear();
                    return result;
                }
            }
            const Result indexResult = addBufferPages(submesh.indexBuffer, submesh.firstIndex, submesh.indexCount);
            if (indexResult != Result::Success)
            {
                pages.Clear();
                return indexResult;
            }
        }

        for (u32 index = 1; index < pages.Size(); ++index)
        {
            const u32 page = pages[index];
            u32 insertion = index;
            while (insertion > 0 && page < pages[insertion - 1u])
            {
                pages[insertion] = pages[insertion - 1u];
                --insertion;
            }
            pages[insertion] = page;
        }
        return pages.Empty() ? Result::InvalidLod : Result::Success;
    }

    Result MeshFile::ReadPage(filesystem::IFile& reader, const u32 pageIndex, void* const destination,
                              const usize capacity) const noexcept
    {
        if (!m_open)
        {
            return Result::InvalidState;
        }
        if (pageIndex >= m_pages.Size() || destination == nullptr)
        {
            return Result::InvalidArgument;
        }
        const PageRecord& page = m_pages[pageIndex];
        if (page.byteSize > capacity)
        {
            return Result::BufferTooSmall;
        }
        serialization::BinaryReader binaryReader(reader);
        if (!binaryReader.Seek(m_geometryOffset + page.dataOffset) ||
            !binaryReader.ReadBytes(destination, static_cast<usize>(page.byteSize)))
        {
            return ReaderResult(binaryReader);
        }
        return crypto::Sha256(destination, static_cast<usize>(page.byteSize)) == page.digest ? Result::Success :
                                                                                              Result::IntegrityFailure;
    }

    Result WriteMesh(filesystem::IFile& writer, const BuildDescription& description) noexcept
    {
        CanonicalData canonical;
        Result result = ValidateAndBuildRecords(description, canonical);
        if (result != Result::Success)
        {
            return result;
        }
        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        crypto::Digest256 emptyFingerprint;
        result = WriteMetadata(metadata, description, canonical, emptyFingerprint);
        if (result != Result::Success)
        {
            return result;
        }
        const crypto::Digest256 contentFingerprint = crypto::Sha256(metadata.Data(), metadata.Size());
        result = WriteMetadata(metadata, description, canonical, contentFingerprint);
        return result == Result::Success ? WriteDocument(writer, metadata, canonical) : result;
    }
} // namespace vanguard::meshes
