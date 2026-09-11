#include <vanguard/materials/materials.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/resources/resource_pipeline.hpp>

#include <algorithm>
#include <new>

namespace
{
    using namespace vanguard;
    namespace material = vanguard::materials;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 2};
    constexpr u32 MaterialSection = serialization::MakeFourCC('M', 'A', 'T', 'L');
    constexpr u32 MetadataWireVersion = 3;
    constexpr u32 InvalidIndex = 0xffffffffu;
    constexpr u32 BodyOffset = 4u + 32u;
    using ByteArray = containers::DynamicArray<u8>;

    [[nodiscard]] resources::Failure ToResourceFailure(const material::Result result) noexcept
    {
        switch (result)
        {
        case material::Result::Success:
            return resources::Failure::None;
        case material::Result::UnsupportedVersion:
            return resources::Failure::UnsupportedVersion;
        case material::Result::LimitExceeded:
            return resources::Failure::OutOfMemory;
        case material::Result::IoFailure:
            return resources::Failure::IoFailure;
        case material::Result::InvalidMagic:
        case material::Result::InvalidLayout:
        case material::Result::IntegrityFailure:
        case material::Result::DuplicateTechnique:
        case material::Result::DuplicateParameter:
        case material::Result::DuplicateResource:
        case material::Result::UnknownShaderInterface:
        case material::Result::TypeMismatch:
            return resources::Failure::IntegrityFailure;
        default:
            return resources::Failure::DeserializationFailure;
        }
    }

    template <typename T> [[nodiscard]] T* AllocateResourceObject() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Resources, sizeof(T), alignof(T));
        return block ? new (block.address) T() : nullptr;
    }

    template <typename T> void DeleteResourceObject(T* const object) noexcept
    {
        if (object == nullptr)
            return;
        object->~T();
        memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Resources};
        memory::Free(block);
    }

    struct CanonicalData final
    {
        CanonicalData() noexcept
            : techniques(memory::pools::Rendering::GetInstance()), buffers(memory::pools::Rendering::GetInstance()),
              parameters(memory::pools::Rendering::GetInstance()), resourceParameters(memory::pools::Rendering::GetInstance()),
              parameterData(memory::pools::Rendering::GetInstance())
        {
        }

        u64 name = 0;
        resources::ResourceReference shader;
        crypto::Digest256 materialDomainFingerprint;
        crypto::Digest256 materialLayoutFingerprint;
        containers::DynamicArray<material::TechniqueRecord> techniques;
        containers::DynamicArray<material::ConstantBufferRecord> buffers;
        containers::DynamicArray<material::ParameterRecord> parameters;
        containers::DynamicArray<material::ResourceParameterRecord> resourceParameters;
        containers::DynamicArray<u8> parameterData;
    };

    [[nodiscard]] material::Result Convert(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return material::Result::Success;
        case serialization::Result::InvalidMagic:
            return material::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
            return material::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return material::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return material::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return material::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return material::Result::IoFailure;
        default:
            return material::Result::InvalidLayout;
        }
    }

    [[nodiscard]] material::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.IsGood() ? material::Result::Success : Convert(writer.GetStatus());
    }

    [[nodiscard]] material::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.IsGood() ? material::Result::Success : Convert(reader.GetStatus());
    }

    [[nodiscard]] constexpr u32 Align16(const u32 value) noexcept
    {
        return (value + 15u) & ~15u;
    }

    void CopyBytes(void* destination, const void* source, const u32 size) noexcept
    {
        auto* const output = static_cast<u8*>(destination);
        const auto* const input = static_cast<const u8*>(source);
        for (u32 index = 0; index < size; ++index)
            output[index] = input[index];
    }

    void ZeroBytes(void* destination, const u32 size) noexcept
    {
        auto* const output = static_cast<u8*>(destination);
        for (u32 index = 0; index < size; ++index)
            output[index] = 0;
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, sizeof(digest.bytes));
    }

    [[nodiscard]] bool WriteReference(serialization::BinaryWriter& writer, const resources::ResourceReference& reference) noexcept
    {
        return writer.WriteU64(reference.GetPath().Id()) && writer.WriteU32(reference.ExpectedType()) && writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadReference(serialization::BinaryReader& reader, resources::ResourceReference& reference) noexcept
    {
        u64 path = 0;
        u32 type = 0;
        u32 reserved = 0;
        if (!reader.ReadU64(path) || !reader.ReadU32(type) || !reader.ReadU32(reserved) || reserved != 0)
            return false;
        reference = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
        return true;
    }

    [[nodiscard]] bool IsTypedReference(const resources::ResourceReference& reference, const resources::ResourceTypeId type) noexcept
    {
        return reference.IsValid() && reference.ExpectedType() == type;
    }

    [[nodiscard]] bool IsResourceParameterKindValid(const material::ResourceParameterKind kind) noexcept
    {
        return kind <= material::ResourceParameterKind::AccelerationStructure;
    }

    [[nodiscard]] material::ResourceParameterKind ConvertResourceKind(const shaders::MaterialResourceKind kind) noexcept
    {
        switch (kind)
        {
        case shaders::MaterialResourceKind::Texture:
            return material::ResourceParameterKind::Texture;
        case shaders::MaterialResourceKind::Buffer:
            return material::ResourceParameterKind::Buffer;
        case shaders::MaterialResourceKind::Sampler:
            return material::ResourceParameterKind::Sampler;
        case shaders::MaterialResourceKind::AccelerationStructure:
            return material::ResourceParameterKind::AccelerationStructure;
        }
        return material::ResourceParameterKind::Texture;
    }

    [[nodiscard]] material::Result ValidateResourceTypeCompatibility(
        const containers::ArraySpan<const material::ResourceTypeCompatibility> compatibility) noexcept
    {
        for (u32 index = 0; index < compatibility.Size(); ++index)
        {
            const material::ResourceTypeCompatibility& value = compatibility[index];
            if (!IsResourceParameterKindValid(value.kind) || value.assetType == resources::InvalidResourceTypeId)
                return material::Result::InvalidArgument;
            for (u32 previousIndex = 0; previousIndex < index; ++previousIndex)
            {
                const material::ResourceTypeCompatibility& previous = compatibility[previousIndex];
                if (value.kind == previous.kind)
                    return material::Result::DuplicateResource;
            }
        }
        return material::Result::Success;
    }

    [[nodiscard]] resources::ResourceTypeId FindCompatibleAssetType(
        const containers::ArraySpan<const material::ResourceTypeCompatibility> compatibility,
        const material::ResourceParameterKind kind) noexcept
    {
        for (const material::ResourceTypeCompatibility& value : compatibility)
            if (value.kind == kind)
                return value.assetType;
        return resources::InvalidResourceTypeId;
    }

    [[nodiscard]] bool TechniqueLess(const material::TechniqueRecord& left, const material::TechniqueRecord& right) noexcept
    {
        if (left.name != right.name)
            return left.name < right.name;
        return left.pipeline.GetKey().path < right.pipeline.GetKey().path;
    }

    [[nodiscard]] bool ParametersAreDisjoint(const material::ParameterRecord& left,
                                             const material::ParameterRecord& right) noexcept
    {
        const u64 leftEnd = static_cast<u64>(left.byteOffset) + left.byteSize;
        const u64 rightEnd = static_cast<u64>(right.byteOffset) + right.byteSize;
        if (leftEnd <= right.byteOffset || rightEnd <= left.byteOffset)
            return true;
        if (left.arrayStride == 0 || left.arrayStride != right.arrayStride)
            return false;
        const u32 stride = left.arrayStride;
        const u32 leftElementSize = (left.byteSize - 1u) % stride + 1u;
        const u32 rightElementSize = (right.byteSize - 1u) % stride + 1u;
        const u32 leftCount = (left.byteSize - leftElementSize) / stride + 1u;
        const u32 rightCount = (right.byteSize - rightElementSize) / stride + 1u;
        if (leftCount != rightCount)
            return false;
        const material::ParameterRecord& first = left.byteOffset < right.byteOffset ? left : right;
        const material::ParameterRecord& second = left.byteOffset < right.byteOffset ? right : left;
        const u32 firstSize = left.byteOffset < right.byteOffset ? leftElementSize : rightElementSize;
        const u32 secondSize = left.byteOffset < right.byteOffset ? rightElementSize : leftElementSize;
        return static_cast<u64>(first.byteOffset) + firstSize <= second.byteOffset &&
               static_cast<u64>(second.byteOffset) + secondSize <= static_cast<u64>(first.byteOffset) + stride;
    }

    void CopyParameterValue(u8* const destination, const void* const source,
                            const material::ParameterRecord& parameter) noexcept
    {
        if (parameter.arrayStride == 0)
        {
            CopyBytes(destination, source, parameter.byteSize);
            return;
        }
        const u32 elementSize = (parameter.byteSize - 1u) % parameter.arrayStride + 1u;
        const u32 elementCount = (parameter.byteSize - elementSize) / parameter.arrayStride + 1u;
        const auto* const sourceBytes = static_cast<const u8*>(source);
        for (u32 index = 0; index < elementCount; ++index)
            CopyBytes(destination + index * parameter.arrayStride,
                      sourceBytes + index * parameter.arrayStride, elementSize);
    }

    [[nodiscard]] material::Result Canonicalize(const material::BuildDescription& description, CanonicalData& output) noexcept
    {
        if (description.name == 0 || description.shaderReflection == nullptr || !description.shaderReflection->IsOpen() ||
            description.shaderReflection->GetMaterialContract() == nullptr ||
            !IsTypedReference(description.shader, shaders::ShaderResourceType) || description.techniques.Empty())
            return material::Result::InvalidArgument;
        const material::Result compatibilityResult = ValidateResourceTypeCompatibility(description.resourceTypeCompatibility);
        if (compatibilityResult != material::Result::Success)
            return compatibilityResult;

        output.name = description.name;
        output.shader = description.shader;
        const shaders::MaterialContract* const materialContract = description.shaderReflection->GetMaterialContract();
        output.materialDomainFingerprint = materialContract->domainFingerprint;
        output.materialLayoutFingerprint = materialContract->layoutFingerprint;

        output.techniques.Reserve(description.techniques.Size());
        for (const material::TechniqueBuildRecord& source : description.techniques)
        {
            if (source.name == 0 || !IsTypedReference(source.pipeline, pipelines::PipelineResourceType) || source.pipelineReflection == nullptr ||
                !source.pipelineReflection->IsOpen())
                return material::Result::InvalidArgument;
            bool compatibleShader = false;
            for (const pipelines::ShaderReference& pipelineShader : source.pipelineReflection->GetShaders())
            {
                // Techniques may compile different entries (depth, GBuffer,
                // shadow) over one graph permutation and reflected material ABI.
                // Every material-bearing stage must agree, not just one of them.
                if (pipelineShader.materialDomain.IsEmpty() && pipelineShader.materialLayout.IsEmpty())
                    continue;
                if (pipelineShader.materialDomain != output.materialDomainFingerprint || pipelineShader.materialLayout != output.materialLayoutFingerprint ||
                    pipelineShader.permutation != description.shaderReflection->GetPermutation())
                    return material::Result::TypeMismatch;
                if (pipelineShader.resource == description.shader.GetPath().Id() &&
                    (pipelineShader.bindingLayout != description.shaderReflection->BindingLayoutFingerprint() ||
                     pipelineShader.pipelineInterface != description.shaderReflection->GetPipelineInterfaceFingerprint()))
                    return material::Result::TypeMismatch;
                compatibleShader = true;
            }
            if (!compatibleShader)
                return material::Result::TypeMismatch;
            output.techniques.PushBack({source.name, source.pipeline});
        }
        if (output.techniques.Size() != description.techniques.Size())
            return material::Result::LimitExceeded;
        std::sort(output.techniques.Begin(), output.techniques.End(), TechniqueLess);
        for (u32 index = 1; index < output.techniques.Size(); ++index)
            if (output.techniques[index - 1u].name == output.techniques[index].name)
                return material::Result::DuplicateTechnique;

        const containers::ArraySpan<const shaders::ConstantMember> reflectedParameters = description.shaderReflection->GetMaterialParameters();
        if (materialContract->parameterByteSize != 0)
        {
            material::ConstantBufferRecord buffer;
            buffer.name = materialContract->domain.name;
            buffer.byteSize = materialContract->parameterByteSize;
            buffer.dataOffset = 0;
            buffer.firstParameter = 0;
            buffer.parameterCount = reflectedParameters.Size();
            output.buffers.PushBack(buffer);
            for (const shaders::ConstantMember& parameter : reflectedParameters)
            {
                output.parameters.PushBack({parameter.name, 0, parameter.byteOffset, parameter.byteSize, parameter.arrayStride, parameter.matrixStride,
                                            parameter.scalarType, parameter.rows, parameter.columns, parameter.rowMajor});
            }
            output.parameterData.Resize(materialContract->parameterByteSize);
            if (output.parameterData.Size() != materialContract->parameterByteSize)
                return material::Result::LimitExceeded;
            ZeroBytes(output.parameterData.Data(), output.parameterData.Size());
        }

        for (u32 valueIndex = 0; valueIndex < description.constants.Size(); ++valueIndex)
        {
            const material::ConstantValueBuildRecord& value = description.constants[valueIndex];
            const material::ParameterRecord* found = nullptr;
            for (const material::ParameterRecord& parameter : output.parameters)
                if (parameter.name == value.name)
                {
                    if (found != nullptr)
                        return material::Result::UnknownShaderInterface;
                    found = &parameter;
                }
            if (found == nullptr)
                return material::Result::UnknownShaderInterface;
            if (value.data == nullptr || value.byteSize != found->byteSize)
                return material::Result::TypeMismatch;
            for (u32 prior = 0; prior < valueIndex; ++prior)
                if (description.constants[prior].name == value.name)
                    return material::Result::DuplicateParameter;
            const material::ConstantBufferRecord& buffer = output.buffers[found->buffer];
            CopyParameterValue(output.parameterData.TypedData() + buffer.dataOffset + found->byteOffset, value.data, *found);
        }

        const containers::ArraySpan<const shaders::MaterialResourceRole> roles = description.shaderReflection->GetMaterialResources();
        output.resourceParameters.Reserve(roles.Size());
        for (const shaders::MaterialResourceRole& role : roles)
        {
            const material::ResourceParameterKind kind = ConvertResourceKind(role.kind);
            output.resourceParameters.PushBack({role.name, role.arrayIndex, role.slot, kind,
                                                FindCompatibleAssetType(description.resourceTypeCompatibility, kind), {},
                                                shaders::HasFlag(role.flags, shaders::MaterialResourceFlags::Required)
                                                    ? resources::DependencyKind::Required
                                                    : resources::DependencyKind::Optional});
        }

        for (u32 valueIndex = 0; valueIndex < description.resources.Size(); ++valueIndex)
        {
            const material::ResourceValueBuildRecord& value = description.resources[valueIndex];
            material::ResourceParameterRecord* found = nullptr;
            for (material::ResourceParameterRecord& parameter : output.resourceParameters)
                if (parameter.name == value.name && parameter.arrayIndex == value.arrayIndex)
                {
                    if (found != nullptr)
                        return material::Result::UnknownShaderInterface;
                    found = &parameter;
                }
            if (found == nullptr || value.dependency > resources::DependencyKind::Soft)
                return material::Result::UnknownShaderInterface;
            if ((!value.resource.IsValid() && value.dependency == resources::DependencyKind::Required) ||
                (value.resource.IsValid() && !value.resource.IsTyped()))
                return material::Result::InvalidArgument;
            if (value.resource.IsValid() &&
                (found->expectedAssetType == resources::InvalidResourceTypeId || value.resource.ExpectedType() != found->expectedAssetType))
                return material::Result::TypeMismatch;
            if (found->dependency == resources::DependencyKind::Required &&
                value.dependency != resources::DependencyKind::Required)
                return material::Result::TypeMismatch;
            for (u32 prior = 0; prior < valueIndex; ++prior)
                if (description.resources[prior].name == value.name && description.resources[prior].arrayIndex == value.arrayIndex)
                    return material::Result::DuplicateParameter;
            found->resource = value.resource;
            found->dependency = value.dependency;
        }
        for (const material::ResourceParameterRecord& parameter : output.resourceParameters)
            if ((!parameter.resource.IsValid() && parameter.dependency == resources::DependencyKind::Required) ||
                (parameter.resource.IsValid() &&
                 (parameter.expectedAssetType == resources::InvalidResourceTypeId ||
                  parameter.resource.ExpectedType() != parameter.expectedAssetType)))
                return material::Result::TypeMismatch;
        return material::Result::Success;
    }

    [[nodiscard]] bool WriteTechnique(serialization::BinaryWriter& writer, const material::TechniqueRecord& value) noexcept
    {
        return writer.WriteU64(value.name) && WriteReference(writer, value.pipeline);
    }

    [[nodiscard]] bool WriteBuffer(serialization::BinaryWriter& writer, const material::ConstantBufferRecord& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.byteSize) && writer.WriteU32(value.dataOffset) && writer.WriteU32(value.firstParameter) &&
               writer.WriteU32(value.parameterCount);
    }

    [[nodiscard]] bool WriteParameter(serialization::BinaryWriter& writer, const material::ParameterRecord& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.buffer) && writer.WriteU32(value.byteOffset) && writer.WriteU32(value.byteSize) &&
               writer.WriteU32(value.arrayStride) && writer.WriteU32(value.matrixStride) && writer.WriteU8(static_cast<u8>(value.scalarType)) &&
               writer.WriteU8(value.rows) && writer.WriteU8(value.columns) && writer.WriteBool(value.rowMajor);
    }

    [[nodiscard]] bool WriteResourceParameter(serialization::BinaryWriter& writer, const material::ResourceParameterRecord& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.arrayIndex) && writer.WriteU32(value.slot) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(static_cast<u8>(value.dependency)) && writer.WriteU16(0) && writer.WriteU32(value.expectedAssetType) &&
               WriteReference(writer, value.resource);
    }

    [[nodiscard]] material::Result WriteBody(serialization::BinaryWriter& writer, const CanonicalData& data) noexcept
    {
        if (!writer.WriteU64(data.name) || !WriteReference(writer, data.shader) || !WriteDigest(writer, data.materialDomainFingerprint) ||
            !WriteDigest(writer, data.materialLayoutFingerprint) || !writer.WriteU32(data.techniques.Size()) ||
            !writer.WriteU32(data.buffers.Size()) || !writer.WriteU32(data.parameters.Size()) || !writer.WriteU32(data.resourceParameters.Size()) ||
            !writer.WriteU32(data.parameterData.Size()))
            return WriterResult(writer);
        for (const material::TechniqueRecord& value : data.techniques)
            if (!WriteTechnique(writer, value))
                return WriterResult(writer);
        for (const material::ConstantBufferRecord& value : data.buffers)
            if (!WriteBuffer(writer, value))
                return WriterResult(writer);
        for (const material::ParameterRecord& value : data.parameters)
            if (!WriteParameter(writer, value))
                return WriterResult(writer);
        for (const material::ResourceParameterRecord& value : data.resourceParameters)
            if (!WriteResourceParameter(writer, value))
                return WriterResult(writer);
        if (!writer.WriteBytes(data.parameterData.Data(), data.parameterData.Size()))
            return WriterResult(writer);
        return material::Result::Success;
    }

    [[nodiscard]] material::Result BuildBody(const CanonicalData& data, ByteArray& body, crypto::Digest256& fingerprint) noexcept
    {
        filesystem::MemoryFileWriter file(body);
        serialization::BinaryWriter writer(file);
        const material::Result result = WriteBody(writer, data);
        if (result != material::Result::Success)
            return result;
        fingerprint = crypto::Sha256(body.Data(), body.Size());
        return material::Result::Success;
    }

    [[nodiscard]] material::Result WriteDocument(filesystem::IFile& file, const ByteArray& body, const crypto::Digest256& fingerprint) noexcept
    {
        ByteArray metadata(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter metadataFile(metadata);
        serialization::BinaryWriter metadataWriter(metadataFile);
        if (!metadataWriter.WriteU32(MetadataWireVersion) || !WriteDigest(metadataWriter, fingerprint) || !metadataWriter.WriteBytes(body.Data(), body.Size()))
            return WriterResult(metadataWriter);

        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = material::MaterialMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16))
            return WriterResult(writer);
        serialization::SectionDescriptor section;
        section.id = MaterialSection;
        section.version = FileVersion;
        section.alignmentLog2 = 4;
        section.offset = writer.Position();
        section.storedSize = metadata.Size();
        section.logicalSize = metadata.Size();
        section.storedCrc64 = serialization::Crc64(metadata.Data(), metadata.Size());
        if (!writer.WriteBytes(metadata.Data(), metadata.Size()) || !writer.Align(16))
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
        return material::Result::Success;
    }

    template <typename Type> [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit)
            return false;
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] bool ReadTechnique(serialization::BinaryReader& reader, material::TechniqueRecord& value) noexcept
    {
        return reader.ReadU64(value.name) && ReadReference(reader, value.pipeline);
    }

    [[nodiscard]] bool ReadBuffer(serialization::BinaryReader& reader, material::ConstantBufferRecord& value) noexcept
    {
        return reader.ReadU64(value.name) && reader.ReadU32(value.byteSize) && reader.ReadU32(value.dataOffset) && reader.ReadU32(value.firstParameter) &&
               reader.ReadU32(value.parameterCount);
    }

    [[nodiscard]] bool ReadParameter(serialization::BinaryReader& reader, material::ParameterRecord& value) noexcept
    {
        u8 scalarType = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.buffer) || !reader.ReadU32(value.byteOffset) || !reader.ReadU32(value.byteSize) ||
            !reader.ReadU32(value.arrayStride) || !reader.ReadU32(value.matrixStride) || !reader.ReadU8(scalarType) || !reader.ReadU8(value.rows) ||
            !reader.ReadU8(value.columns) || !reader.ReadBool(value.rowMajor))
            return false;
        value.scalarType = static_cast<shaders::ScalarType>(scalarType);
        return true;
    }

    [[nodiscard]] bool ReadResourceParameter(serialization::BinaryReader& reader, material::ResourceParameterRecord& value) noexcept
    {
        u8 kind = 0;
        u8 dependency = 0;
        u16 reserved = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.arrayIndex) || !reader.ReadU32(value.slot) || !reader.ReadU8(kind) || !reader.ReadU8(dependency) ||
            !reader.ReadU16(reserved) || !reader.ReadU32(value.expectedAssetType) || !ReadReference(reader, value.resource) || reserved != 0)
            return false;
        value.kind = static_cast<material::ResourceParameterKind>(kind);
        value.dependency = static_cast<resources::DependencyKind>(dependency);
        return true;
    }

    [[nodiscard]] material::Result ValidateLoaded(const u64 name, const resources::ResourceReference& shader,
                                                  const crypto::Digest256& materialDomainFingerprint,
                                                  const crypto::Digest256& materialLayoutFingerprint,
                                                  const containers::DynamicArray<material::TechniqueRecord>& techniques,
                                                  const containers::DynamicArray<material::ConstantBufferRecord>& buffers,
                                                  const containers::DynamicArray<material::ParameterRecord>& parameters,
                                                  const containers::DynamicArray<material::ResourceParameterRecord>& resourceParameters,
                                                  const containers::DynamicArray<u8>& data) noexcept
    {
        if (name == 0 || !IsTypedReference(shader, shaders::ShaderResourceType) || techniques.Empty() ||
            materialDomainFingerprint.IsEmpty() != materialLayoutFingerprint.IsEmpty())
            return material::Result::InvalidLayout;
        for (u32 index = 0; index < techniques.Size(); ++index)
        {
            if (techniques[index].name == 0 || !IsTypedReference(techniques[index].pipeline, pipelines::PipelineResourceType) ||
                (index > 0 && !TechniqueLess(techniques[index - 1u], techniques[index])))
                return material::Result::InvalidLayout;
        }
        u32 expectedParameter = 0;
        u32 expectedDataOffset = 0;
        for (u32 index = 0; index < buffers.Size(); ++index)
        {
            const material::ConstantBufferRecord& buffer = buffers[index];
            expectedDataOffset = Align16(expectedDataOffset);
            if (buffer.name == 0 || buffer.byteSize == 0 || buffer.firstParameter != expectedParameter || buffer.dataOffset != expectedDataOffset ||
                buffer.dataOffset > data.Size() || buffer.byteSize > data.Size() - buffer.dataOffset || (index > 0 && buffer.name <= buffers[index - 1u].name))
                return material::Result::InvalidLayout;
            if (buffer.parameterCount > parameters.Size() - expectedParameter)
                return material::Result::InvalidLayout;
            for (u32 member = 0; member < buffer.parameterCount; ++member)
            {
                const material::ParameterRecord& parameter = parameters[expectedParameter + member];
                if (parameter.name == 0 || parameter.buffer != index || parameter.byteSize == 0 ||
                    parameter.byteOffset > buffer.byteSize || parameter.byteSize > buffer.byteSize - parameter.byteOffset ||
                    parameter.scalarType > shaders::ScalarType::F64 || parameter.rows == 0 || parameter.rows > 4 || parameter.columns == 0 ||
                    parameter.columns > 4)
                    return material::Result::InvalidLayout;
                for (u32 previous = 0; previous < member; ++previous)
                    if (!ParametersAreDisjoint(parameters[expectedParameter + previous], parameter))
                        return material::Result::InvalidLayout;
            }
            expectedParameter += buffer.parameterCount;
            if (buffer.byteSize > 0xffffffffu - expectedDataOffset)
                return material::Result::InvalidLayout;
            expectedDataOffset += buffer.byteSize;
        }
        if (expectedParameter != parameters.Size() || expectedDataOffset != data.Size())
            return material::Result::InvalidLayout;
        for (u32 index = 0; index < resourceParameters.Size(); ++index)
        {
            const material::ResourceParameterRecord& parameter = resourceParameters[index];
            if (parameter.name == 0 || parameter.slot != index || !IsResourceParameterKindValid(parameter.kind) ||
                parameter.dependency > resources::DependencyKind::Soft ||
                (!parameter.resource.IsValid() && parameter.dependency == resources::DependencyKind::Required) ||
                (parameter.resource.IsValid() &&
                 (!parameter.resource.IsTyped() || parameter.expectedAssetType == resources::InvalidResourceTypeId ||
                  parameter.resource.ExpectedType() != parameter.expectedAssetType)))
                return material::Result::InvalidLayout;
            for (u32 previousIndex = 0; previousIndex < index; ++previousIndex)
            {
                const material::ResourceParameterRecord& prior = resourceParameters[previousIndex];
                if (parameter.name == prior.name && parameter.arrayIndex == prior.arrayIndex)
                    return material::Result::InvalidLayout;
                if (parameter.kind == prior.kind && parameter.expectedAssetType != prior.expectedAssetType)
                    return material::Result::InvalidLayout;
            }
        }
        return material::Result::Success;
    }

    [[nodiscard]] bool DependencyLess(const material::ResourceDependency& left, const material::ResourceDependency& right) noexcept
    {
        if (left.resource.GetPath() != right.resource.GetPath())
            return left.resource.GetPath() < right.resource.GetPath();
        return left.resource.ExpectedType() < right.resource.ExpectedType();
    }

    [[nodiscard]] bool AddDependency(containers::DynamicArray<material::ResourceDependency>& dependencies, const resources::ResourceReference& reference,
                                     const resources::DependencyKind kind) noexcept
    {
        if (!reference.IsValid())
            return true;
        for (material::ResourceDependency& existing : dependencies)
            if (existing.resource == reference)
            {
                if (kind < existing.kind)
                    existing.kind = kind;
                return true;
            }
        const u32 expectedSize = dependencies.Size() + 1u;
        dependencies.PushBack({reference, kind});
        return dependencies.Size() == expectedSize;
    }

    [[nodiscard]] const material::LoadedMaterialDependency* FindLoadedDependency(
        const containers::ArraySpan<const material::LoadedMaterialDependency> dependencies,
        const resources::ResourceReference reference) noexcept
    {
        for (const material::LoadedMaterialDependency& dependency : dependencies)
            if (dependency.resource == reference)
                return &dependency;
        return nullptr;
    }

    [[nodiscard]] bool SameGeneration(const resources::ResourceHandle& left, const resources::ResourceHandle& right) noexcept
    {
        return left.IsValid() && right.IsValid() && left.GetPath() == right.GetPath() && left.GetType() == right.GetType() &&
               left.GetGeneration() == right.GetGeneration() && left.Get() == right.Get();
    }

    [[nodiscard]] bool ValidateMaterialClosure(const material::MaterialResourceObject& object) noexcept
    {
        const material::MaterialFile& file = object.GetFile();
        const containers::ArraySpan<const material::LoadedMaterialDependency> dependencies = object.GetLoadedDependencies();
        const material::LoadedMaterialDependency* const shaderDependency = FindLoadedDependency(dependencies, file.GetShader());
        if (shaderDependency == nullptr || shaderDependency->kind != resources::DependencyKind::Required || !shaderDependency->handle.IsValid() ||
            shaderDependency->handle.GetType() != shaders::ShaderResourceType)
            return false;
        const auto* const shaderObject = static_cast<const shaders::ShaderResourceObject*>(shaderDependency->handle.Get());
        if (shaderObject == nullptr || !shaderObject->IsOpen())
            return false;
        const shaders::ShaderFile& shader = shaderObject->GetFile();
        const shaders::MaterialContract* const contract = shader.GetMaterialContract();
        if (contract == nullptr || file.GetMaterialDomainFingerprint() != contract->domainFingerprint ||
            file.GetMaterialLayoutFingerprint() != contract->layoutFingerprint)
            return false;

        const containers::ArraySpan<const shaders::ConstantMember> reflectedParameters = shader.GetMaterialParameters();
        const containers::ArraySpan<const material::ConstantBufferRecord> buffers = file.GetConstantBuffers();
        const containers::ArraySpan<const material::ParameterRecord> parameters = file.GetParameters();
        if (parameters.Size() != reflectedParameters.Size() || file.GetParameterData().Size() != contract->parameterByteSize)
            return false;
        if (contract->parameterByteSize == 0)
        {
            if (!buffers.Empty() || !parameters.Empty())
                return false;
        }
        else if (buffers.Size() != 1 || buffers[0].name != contract->domain.name || buffers[0].byteSize != contract->parameterByteSize ||
                 buffers[0].dataOffset != 0 || buffers[0].firstParameter != 0 || buffers[0].parameterCount != parameters.Size())
        {
            return false;
        }
        for (u32 index = 0; index < parameters.Size(); ++index)
        {
            const material::ParameterRecord& parameter = parameters[index];
            const shaders::ConstantMember& reflected = reflectedParameters[index];
            if (parameter.name != reflected.name || parameter.buffer != 0 || parameter.byteOffset != reflected.byteOffset ||
                parameter.byteSize != reflected.byteSize || parameter.arrayStride != reflected.arrayStride ||
                parameter.matrixStride != reflected.matrixStride || parameter.scalarType != reflected.scalarType || parameter.rows != reflected.rows ||
                parameter.columns != reflected.columns || parameter.rowMajor != reflected.rowMajor)
                return false;
        }

        const containers::ArraySpan<const shaders::MaterialResourceRole> reflectedResources = shader.GetMaterialResources();
        const containers::ArraySpan<const material::ResourceParameterRecord> resourceParameters = file.GetResourceParameters();
        if (resourceParameters.Size() != reflectedResources.Size())
            return false;
        for (u32 index = 0; index < resourceParameters.Size(); ++index)
        {
            const material::ResourceParameterRecord& resource = resourceParameters[index];
            const shaders::MaterialResourceRole& reflected = reflectedResources[index];
            const bool required = shaders::HasFlag(reflected.flags, shaders::MaterialResourceFlags::Required);
            if (resource.name != reflected.name || resource.arrayIndex != reflected.arrayIndex || resource.slot != reflected.slot ||
                resource.kind != ConvertResourceKind(reflected.kind) ||
                (required ? resource.dependency != resources::DependencyKind::Required
                          : resource.dependency == resources::DependencyKind::Required) ||
                (resource.resource.IsValid() && (!resource.resource.IsTyped() || resource.resource.ExpectedType() != resource.expectedAssetType)))
                return false;
        }

        for (const material::TechniqueRecord& technique : file.GetTechniques())
        {
            const material::LoadedMaterialDependency* const pipelineDependency = FindLoadedDependency(dependencies, technique.pipeline);
            if (pipelineDependency == nullptr || pipelineDependency->kind != resources::DependencyKind::Required || !pipelineDependency->handle.IsValid() ||
                pipelineDependency->handle.GetType() != pipelines::PipelineResourceType)
                return false;
            const auto* const pipelineObject = static_cast<const pipelines::PipelineResourceObject*>(pipelineDependency->handle.Get());
            if (pipelineObject == nullptr || !pipelineObject->IsOpen())
                return false;
            const containers::ArraySpan<const pipelines::ShaderReference> shaderReferences = pipelineObject->GetFile().GetShaders();
            const containers::ArraySpan<const resources::ResourceHandle> shaderHandles = pipelineObject->GetShaderDependencies();
            if (shaderReferences.Size() != shaderHandles.Size())
                return false;
            bool matched = false;
            for (u32 index = 0; index < shaderReferences.Size(); ++index)
            {
                const pipelines::ShaderReference& reference = shaderReferences[index];
                const resources::ResourceHandle& techniqueShaderHandle = shaderHandles[index];
                if (!techniqueShaderHandle.IsValid() || techniqueShaderHandle.GetType() != shaders::ShaderResourceType ||
                    techniqueShaderHandle.GetPath().Id() != reference.resource)
                    return false;
                const auto* const techniqueShaderObject = static_cast<const shaders::ShaderResourceObject*>(techniqueShaderHandle.Get());
                if (techniqueShaderObject == nullptr || !techniqueShaderObject->IsOpen())
                    return false;
                const shaders::ShaderFile& techniqueShader = techniqueShaderObject->GetFile();
                if (reference.permutation != techniqueShader.GetPermutation() || reference.bindingLayout != techniqueShader.BindingLayoutFingerprint() ||
                    reference.pipelineInterface != techniqueShader.GetPipelineInterfaceFingerprint())
                    return false;
                // Preserve exact-generation closure for a shared layout-authority
                // shader. Other technique shaders are retained by their pipeline.
                if (reference.resource == file.GetShader().GetPath().Id() && !SameGeneration(techniqueShaderHandle, shaderDependency->handle))
                    return false;
                const shaders::MaterialContract* const techniqueContract = techniqueShader.GetMaterialContract();
                if (techniqueContract == nullptr)
                {
                    if (!reference.materialDomain.IsEmpty() || !reference.materialLayout.IsEmpty())
                        return false;
                    continue;
                }
                if (reference.materialDomain != techniqueContract->domainFingerprint || reference.materialLayout != techniqueContract->layoutFingerprint ||
                    techniqueContract->domainFingerprint != contract->domainFingerprint || techniqueContract->layoutFingerprint != contract->layoutFingerprint ||
                    techniqueShader.GetPermutation() != shader.GetPermutation())
                    return false;
                matched = true;
            }
            if (!matched)
                return false;
        }
        return true;
    }
} // namespace

namespace vanguard::materials
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
        case Result::DuplicateTechnique:
            return "DuplicateTechnique";
        case Result::DuplicateParameter:
            return "DuplicateParameter";
        case Result::DuplicateResource:
            return "DuplicateResource";
        case Result::UnknownShaderInterface:
            return "UnknownShaderInterface";
        case Result::TypeMismatch:
            return "TypeMismatch";
        case Result::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    MaterialFile::MaterialFile() noexcept
        : m_techniques(memory::pools::Rendering::GetInstance()), m_constantBuffers(memory::pools::Rendering::GetInstance()),
          m_parameters(memory::pools::Rendering::GetInstance()), m_resourceParameters(memory::pools::Rendering::GetInstance()),
          m_dependencies(memory::pools::Resources::GetInstance()), m_parameterData(memory::pools::Rendering::GetInstance())
    {
    }

    Result MaterialFile::Open(filesystem::IFile& file, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 1;
        const serialization::Result headerResult = serialization::ReadDocumentHeader(reader, MaterialMagic, {1, 2, 2}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
            return Convert(headerResult);
        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success)
            return Convert(sectionResult);
        if (sections.Size() != 1 || sections[0].id != MaterialSection || sections[0].version != FileVersion ||
            sections[0].codec != serialization::Codec::None || sections[0].storedSize != sections[0].logicalSize ||
            sections[0].storedSize > limits.maximumFileSize || sections[0].storedSize > 0xffffffffull)
            return Result::InvalidLayout;
        ByteArray metadata(memory::pools::Serialization::GetInstance());
        metadata.Resize(static_cast<u32>(sections[0].storedSize));
        if (metadata.Size() != sections[0].storedSize)
            return Result::LimitExceeded;
        if (!reader.Seek(sections[0].offset) || !reader.ReadBytes(metadata.Data(), metadata.Size()))
            return ReaderResult(reader);
        if (serialization::Crc64(metadata.Data(), metadata.Size()) != sections[0].storedCrc64)
            return Result::IntegrityFailure;
        if (metadata.Size() < BodyOffset)
            return Result::InvalidLayout;
        filesystem::MemoryFileReader metadataFile(metadata, 0);
        serialization::BinaryReader metadataReader(metadataFile);
        u32 version = 0;
        if (!metadataReader.ReadU32(version) || !ReadDigest(metadataReader, m_contentFingerprint))
            return ReaderResult(metadataReader);
        if (version != MetadataWireVersion)
            return Result::UnsupportedVersion;
        const crypto::Digest256 actual = crypto::Sha256(metadata.TypedData() + BodyOffset, metadata.Size() - BodyOffset);
        if (actual != m_contentFingerprint)
            return Result::IntegrityFailure;
        u32 techniqueCount = 0;
        u32 bufferCount = 0;
        u32 parameterCount = 0;
        u32 resourceParameterCount = 0;
        u32 dataSize = 0;
        if (!metadataReader.ReadU64(m_name) || !ReadReference(metadataReader, m_shader) ||
            !ReadDigest(metadataReader, m_materialDomainFingerprint) || !ReadDigest(metadataReader, m_materialLayoutFingerprint) ||
            !metadataReader.ReadU32(techniqueCount) ||
            !metadataReader.ReadU32(bufferCount) || !metadataReader.ReadU32(parameterCount) || !metadataReader.ReadU32(resourceParameterCount) ||
            !metadataReader.ReadU32(dataSize))
            return ReaderResult(metadataReader);
        if (!ResizeChecked(m_techniques, techniqueCount, limits.maximumTechniques) ||
            !ResizeChecked(m_constantBuffers, bufferCount, limits.maximumConstantBuffers) ||
            !ResizeChecked(m_parameters, parameterCount, limits.maximumParameters) ||
            !ResizeChecked(m_resourceParameters, resourceParameterCount, limits.maximumResourceParameters) ||
            !ResizeChecked(m_parameterData, dataSize, limits.maximumParameterBytes))
            return Result::LimitExceeded;
        for (TechniqueRecord& value : m_techniques)
            if (!ReadTechnique(metadataReader, value))
                return ReaderResult(metadataReader);
        for (ConstantBufferRecord& value : m_constantBuffers)
            if (!ReadBuffer(metadataReader, value))
                return ReaderResult(metadataReader);
        for (ParameterRecord& value : m_parameters)
            if (!ReadParameter(metadataReader, value))
                return ReaderResult(metadataReader);
        for (ResourceParameterRecord& value : m_resourceParameters)
            if (!ReadResourceParameter(metadataReader, value))
                return ReaderResult(metadataReader);
        if (!metadataReader.ReadBytes(m_parameterData.Data(), m_parameterData.Size()))
            return ReaderResult(metadataReader);
        if (metadataReader.Position() != metadataReader.Size())
            return Result::InvalidLayout;
        const Result validation = ValidateLoaded(m_name, m_shader, m_materialDomainFingerprint, m_materialLayoutFingerprint, m_techniques, m_constantBuffers,
                                                 m_parameters, m_resourceParameters, m_parameterData);
        if (validation != Result::Success)
        {
            Close();
            return validation;
        }
        bool dependenciesValid = AddDependency(m_dependencies, m_shader, resources::DependencyKind::Required);
        for (const TechniqueRecord& technique : m_techniques)
            dependenciesValid = AddDependency(m_dependencies, technique.pipeline, resources::DependencyKind::Required) && dependenciesValid;
        for (const ResourceParameterRecord& parameter : m_resourceParameters)
            dependenciesValid = AddDependency(m_dependencies, parameter.resource, parameter.dependency) && dependenciesValid;
        if (!dependenciesValid || m_dependencies.Size() > limits.maximumDependencies)
        {
            Close();
            return Result::LimitExceeded;
        }
        std::sort(m_dependencies.Begin(), m_dependencies.End(), DependencyLess);
        m_open = true;
        return Result::Success;
    }

    void MaterialFile::Close() noexcept
    {
        m_name = 0;
        m_shader = {};
        m_contentFingerprint = {};
        m_materialDomainFingerprint = {};
        m_materialLayoutFingerprint = {};
        m_techniques.Clear();
        m_constantBuffers.Clear();
        m_parameters.Clear();
        m_resourceParameters.Clear();
        m_dependencies.Clear();
        m_parameterData.Clear();
        m_open = false;
    }

    bool MaterialFile::IsOpen() const noexcept
    {
        return m_open;
    }
    u64 MaterialFile::GetName() const noexcept
    {
        return m_name;
    }
    const resources::ResourceReference& MaterialFile::GetShader() const noexcept
    {
        return m_shader;
    }
    const crypto::Digest256& MaterialFile::GetContentFingerprint() const noexcept
    {
        return m_contentFingerprint;
    }
    const crypto::Digest256& MaterialFile::GetMaterialDomainFingerprint() const noexcept
    {
        return m_materialDomainFingerprint;
    }
    const crypto::Digest256& MaterialFile::GetMaterialLayoutFingerprint() const noexcept
    {
        return m_materialLayoutFingerprint;
    }
    containers::ArraySpan<const TechniqueRecord> MaterialFile::GetTechniques() const noexcept
    {
        return m_techniques;
    }
    containers::ArraySpan<const ConstantBufferRecord> MaterialFile::GetConstantBuffers() const noexcept
    {
        return m_constantBuffers;
    }
    containers::ArraySpan<const ParameterRecord> MaterialFile::GetParameters() const noexcept
    {
        return m_parameters;
    }
    containers::ArraySpan<const ResourceParameterRecord> MaterialFile::GetResourceParameters() const noexcept
    {
        return m_resourceParameters;
    }
    containers::ArraySpan<const ResourceDependency> MaterialFile::GetDependencies() const noexcept
    {
        return m_dependencies;
    }
    containers::ArraySpan<const u8> MaterialFile::GetParameterData() const noexcept
    {
        return m_parameterData;
    }

    containers::ArraySpan<const u8> MaterialFile::GetConstantBufferData(const ConstantBufferRecord& buffer) const noexcept
    {
        if (!m_open || buffer.dataOffset > m_parameterData.Size() || buffer.byteSize > m_parameterData.Size() - buffer.dataOffset)
            return {};
        return {m_parameterData.TypedData() + buffer.dataOffset, buffer.byteSize};
    }

    MaterialResourceObject::MaterialResourceObject() noexcept : m_loadedDependencies(memory::pools::Resources::GetInstance()) {}

    resources::ResourceTypeId MaterialResourceObject::GetType() const noexcept
    {
        return MaterialResourceType;
    }

    bool MaterialResourceObject::IsOpen() const noexcept
    {
        return m_file.IsOpen();
    }

    const MaterialFile& MaterialResourceObject::GetFile() const noexcept
    {
        return m_file;
    }

    containers::ArraySpan<const LoadedMaterialDependency> MaterialResourceObject::GetLoadedDependencies() const noexcept
    {
        return m_loadedDependencies;
    }

    resources::ResourceObject* DecodeMaterialResource(const resources::ResourceReference reference, const void* const data, const usize size,
                                                      const resources::LoadContext& context, resources::Failure& failure, void* const userData) noexcept
    {
        failure = resources::Failure::None;
        if (context.IsCancellationRequested())
        {
            failure = resources::Failure::Cancelled;
            return nullptr;
        }
        if (!reference.IsTyped() || reference.ExpectedType() != MaterialResourceType || context.Reference() != reference || data == nullptr || size == 0 ||
            size > ~u32{0})
        {
            failure = resources::Failure::DeserializationFailure;
            return nullptr;
        }

        MaterialResourceObject* const object = AllocateResourceObject<MaterialResourceObject>();
        if (object == nullptr)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        filesystem::MemoryFileReader reader(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        const MaterialResourceDecoderConfig defaults;
        const auto& config = userData != nullptr ? *static_cast<const MaterialResourceDecoderConfig*>(userData) : defaults;
        const Result result = object->m_file.Open(reader, config.limits);
        if (result != Result::Success)
        {
            failure = ToResourceFailure(result);
            DeleteResourceObject(object);
            return nullptr;
        }

        u32 expectedCount = 0;
        for (const ResourceDependency& dependency : object->m_file.GetDependencies())
            expectedCount += dependency.kind != resources::DependencyKind::Soft ? 1u : 0u;
        if (context.GetDependencyCount() != expectedCount)
        {
            failure = resources::Failure::IntegrityFailure;
            DeleteResourceObject(object);
            return nullptr;
        }

        containers::HashMap<resources::ResourceId, u32> contextByPath{memory::pools::Resources::GetInstance()};
        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
        {
            const resources::ResourceReference dependency = context.GetDependencyReference(index);
            if (!dependency.IsTyped() || !contextByPath.Insert(dependency.GetPath().Id(), index).IsSuccessful())
            {
                failure = resources::Failure::IntegrityFailure;
                DeleteResourceObject(object);
                return nullptr;
            }
        }

        containers::DynamicArray<u8> matched{memory::pools::Resources::GetInstance()};
        matched.Resize(context.GetDependencyCount());
        for (u32 index = 0; index < matched.Size(); ++index)
            matched[index] = 0;
        object->m_loadedDependencies.Reserve(expectedCount);
        for (const ResourceDependency& expected : object->m_file.GetDependencies())
        {
            if (expected.kind == resources::DependencyKind::Soft)
                continue;
            const resources::DependencyRequirement requirement = expected.kind == resources::DependencyKind::Optional
                                                                            ? resources::DependencyRequirement::Optional
                                                                            : resources::DependencyRequirement::Required;
            u32 contextIndex = ~u32{0};
            if (!expected.resource.IsTyped() || !contextByPath.Find(expected.resource.GetPath().Id(), contextIndex) || contextIndex >= matched.Size() ||
                matched[contextIndex] != 0 || context.GetDependencyReference(contextIndex) != expected.resource ||
                context.GetDependencyRequirementAt(contextIndex) != requirement)
            {
                failure = resources::Failure::IntegrityFailure;
                DeleteResourceObject(object);
                return nullptr;
            }

            const resources::ResourceHandle& handle = context.GetDependency(contextIndex);
            const resources::Failure dependencyFailure = context.GetDependencyError(contextIndex);
            if ((handle.IsValid() && (handle.GetPath() != expected.resource.GetPath() || handle.GetType() != expected.resource.ExpectedType() ||
                                      dependencyFailure != resources::Failure::None)) ||
                (!handle.IsValid() && (requirement == resources::DependencyRequirement::Required || dependencyFailure == resources::Failure::None)))
            {
                failure = resources::Failure::IntegrityFailure;
                DeleteResourceObject(object);
                return nullptr;
            }

            matched[contextIndex] = 1;
            LoadedMaterialDependency loaded;
            loaded.resource = expected.resource;
            loaded.kind = expected.kind;
            loaded.handle = handle;
            loaded.failure = dependencyFailure;
            object->m_loadedDependencies.PushBack(static_cast<LoadedMaterialDependency&&>(loaded));
        }
        if (!ValidateMaterialClosure(*object))
        {
            failure = resources::Failure::IntegrityFailure;
            DeleteResourceObject(object);
            return nullptr;
        }
        return object;
    }

    void DestroyMaterialResource(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResourceObject(static_cast<MaterialResourceObject*>(resource));
    }

    Result WriteMaterial(filesystem::IFile& writer, const BuildDescription& description) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
            return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        crypto::Digest256 fingerprint;
        result = BuildBody(canonical, body, fingerprint);
        return result == Result::Success ? WriteDocument(writer, body, fingerprint) : result;
    }

    Result CalculateContentFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
            return result;
        ByteArray body(memory::pools::Serialization::GetInstance());
        return BuildBody(canonical, body, fingerprint);
    }
} // namespace vanguard::materials
