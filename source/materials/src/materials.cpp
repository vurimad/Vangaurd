#include <vanguard/materials/materials.hpp>

#include <algorithm>

namespace
{
    using namespace vanguard;
    namespace material = vanguard::materials;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 MaterialSection = serialization::MakeFourCC('M', 'A', 'T', 'L');
    constexpr u32 MetadataWireVersion = 1;
    constexpr u32 InvalidIndex = 0xffffffffu;
    constexpr u32 BodyOffset = 4u + 32u;
    using ByteArray = containers::DynamicArray<u8>;

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

    [[nodiscard]] const shaders::ConstantBuffer* FindBuffer(const shaders::ShaderFile& shader, const u64 name, bool& ambiguous) noexcept
    {
        const shaders::ConstantBuffer* found = nullptr;
        for (const shaders::ConstantBuffer& buffer : shader.GetConstantBuffers())
        {
            if (buffer.name != name)
                continue;
            if (found != nullptr)
                ambiguous = true;
            found = &buffer;
        }
        return found;
    }

    [[nodiscard]] bool TechniqueLess(const material::TechniqueRecord& left, const material::TechniqueRecord& right) noexcept
    {
        if (left.name != right.name)
            return left.name < right.name;
        return left.pipeline.GetKey().path < right.pipeline.GetKey().path;
    }

    [[nodiscard]] bool BufferPointerLess(const shaders::ConstantBuffer* left, const shaders::ConstantBuffer* right) noexcept
    {
        return left->name < right->name;
    }

    [[nodiscard]] bool ResourceParameterLess(const material::ResourceParameterBuildRecord& left, const material::ResourceParameterBuildRecord& right) noexcept
    {
        if (left.name != right.name)
            return left.name < right.name;
        return left.kind < right.kind;
    }

    [[nodiscard]] material::Result Canonicalize(const material::BuildDescription& description, CanonicalData& output) noexcept
    {
        if (description.name == 0 || description.shaderReflection == nullptr || !description.shaderReflection->IsOpen() ||
            !IsTypedReference(description.shader, shaders::ShaderResourceType) || description.techniques.Empty())
            return material::Result::InvalidArgument;

        output.name = description.name;
        output.shader = description.shader;

        output.techniques.Reserve(description.techniques.Size());
        for (const material::TechniqueBuildRecord& source : description.techniques)
        {
            if (source.name == 0 || !IsTypedReference(source.pipeline, pipelines::PipelineResourceType) || source.pipelineReflection == nullptr ||
                !source.pipelineReflection->IsOpen() || source.pipelineReflection->GetKind() != pipelines::PipelineKind::Graphics)
                return material::Result::InvalidArgument;
            bool compatibleShader = false;
            for (const pipelines::ShaderReference& pipelineShader : source.pipelineReflection->GetShaders())
                if (pipelineShader.resource == description.shader.GetPath().Id() && pipelineShader.permutation == description.shaderReflection->GetPermutation() &&
                    pipelineShader.bindingLayout == description.shaderReflection->BindingLayoutFingerprint() &&
                    pipelineShader.pipelineInterface == description.shaderReflection->GetPipelineInterfaceFingerprint())
                    compatibleShader = true;
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

        containers::DynamicArray<const shaders::ConstantBuffer*> selectedBuffers(memory::pools::Rendering::GetInstance());
        selectedBuffers.Reserve(description.materialConstantBuffers.Size());
        for (const u64 name : description.materialConstantBuffers)
        {
            bool ambiguous = false;
            const shaders::ConstantBuffer* const buffer = FindBuffer(*description.shaderReflection, name, ambiguous);
            if (buffer == nullptr || ambiguous)
                return material::Result::UnknownShaderInterface;
            selectedBuffers.PushBack(buffer);
        }
        if (selectedBuffers.Size() != description.materialConstantBuffers.Size())
            return material::Result::LimitExceeded;
        std::sort(selectedBuffers.Begin(), selectedBuffers.End(), BufferPointerLess);
        for (u32 index = 1; index < selectedBuffers.Size(); ++index)
            if (selectedBuffers[index - 1u] == selectedBuffers[index])
                return material::Result::DuplicateResource;

        const containers::ArraySpan<const shaders::ConstantMember> shaderMembers = description.shaderReflection->GetConstantMembers();
        u64 totalDataSize = 0;
        for (const shaders::ConstantBuffer* const source : selectedBuffers)
        {
            if (totalDataSize > 0xfffffff0ull)
                return material::Result::LimitExceeded;
            totalDataSize = Align16(static_cast<u32>(totalDataSize));
            if (source->byteSize > 0xffffffffull - totalDataSize)
                return material::Result::LimitExceeded;
            material::ConstantBufferRecord buffer;
            buffer.name = source->name;
            buffer.byteSize = source->byteSize;
            buffer.dataOffset = static_cast<u32>(totalDataSize);
            buffer.firstParameter = output.parameters.Size();
            buffer.parameterCount = source->memberCount;
            const u32 bufferIndex = output.buffers.Size();
            output.buffers.PushBack(buffer);
            if (output.buffers.Size() != bufferIndex + 1u)
                return material::Result::LimitExceeded;
            for (u32 memberIndex = 0; memberIndex < source->memberCount; ++memberIndex)
            {
                const shaders::ConstantMember& member = shaderMembers[source->firstMember + memberIndex];
                output.parameters.PushBack({member.name, bufferIndex, member.byteOffset, member.byteSize, member.arrayStride, member.matrixStride,
                                            member.scalarType, member.rows, member.columns, member.rowMajor});
                if (output.parameters.Size() != buffer.firstParameter + memberIndex + 1u)
                    return material::Result::LimitExceeded;
            }
            totalDataSize += source->byteSize;
        }
        if (output.buffers.Size() != selectedBuffers.Size() || totalDataSize > 0xffffffffull)
            return material::Result::LimitExceeded;
        output.parameterData.Resize(static_cast<u32>(totalDataSize));
        if (output.parameterData.Size() != totalDataSize)
            return material::Result::LimitExceeded;
        ZeroBytes(output.parameterData.Data(), output.parameterData.Size());

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
            CopyBytes(output.parameterData.TypedData() + buffer.dataOffset + found->byteOffset, value.data, value.byteSize);
        }

        containers::DynamicArray<material::ResourceParameterBuildRecord> selectedResources(memory::pools::Rendering::GetInstance());
        selectedResources.Reserve(description.resourceParameters.Size());
        for (const material::ResourceParameterBuildRecord& parameter : description.resourceParameters)
        {
            if (parameter.name == 0 || parameter.arrayCount == 0 || !IsResourceParameterKindValid(parameter.kind))
                return material::Result::InvalidArgument;
            selectedResources.PushBack(parameter);
        }
        if (selectedResources.Size() != description.resourceParameters.Size())
            return material::Result::LimitExceeded;
        std::sort(selectedResources.Begin(), selectedResources.End(), ResourceParameterLess);
        for (u32 index = 1; index < selectedResources.Size(); ++index)
            if (selectedResources[index - 1u].name == selectedResources[index].name)
                return material::Result::DuplicateResource;
        for (const material::ResourceParameterBuildRecord& source : selectedResources)
        {
            if (source.arrayCount > 0xffffffffu - output.resourceParameters.Size())
                return material::Result::LimitExceeded;
            for (u32 arrayIndex = 0; arrayIndex < source.arrayCount; ++arrayIndex)
            {
                const u32 expectedSize = output.resourceParameters.Size() + 1u;
                output.resourceParameters.PushBack({source.name, arrayIndex, source.kind, {}, resources::DependencyKind::Optional});
                if (output.resourceParameters.Size() != expectedSize)
                    return material::Result::LimitExceeded;
            }
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
            for (u32 prior = 0; prior < valueIndex; ++prior)
                if (description.resources[prior].name == value.name && description.resources[prior].arrayIndex == value.arrayIndex)
                    return material::Result::DuplicateParameter;
            found->resource = value.resource;
            found->dependency = value.dependency;
        }
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
        return writer.WriteU64(value.name) && writer.WriteU32(value.arrayIndex) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(static_cast<u8>(value.dependency)) && writer.WriteU16(0) && WriteReference(writer, value.resource);
    }

    [[nodiscard]] material::Result WriteBody(serialization::BinaryWriter& writer, const CanonicalData& data) noexcept
    {
        if (!writer.WriteU64(data.name) || !WriteReference(writer, data.shader) || !writer.WriteU32(data.techniques.Size()) ||
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
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.arrayIndex) || !reader.ReadU8(kind) || !reader.ReadU8(dependency) ||
            !reader.ReadU16(reserved) || !ReadReference(reader, value.resource) || reserved != 0)
            return false;
        value.kind = static_cast<material::ResourceParameterKind>(kind);
        value.dependency = static_cast<resources::DependencyKind>(dependency);
        return true;
    }

    [[nodiscard]] material::Result ValidateLoaded(const u64 name, const resources::ResourceReference& shader,
                                                  const containers::DynamicArray<material::TechniqueRecord>& techniques,
                                                  const containers::DynamicArray<material::ConstantBufferRecord>& buffers,
                                                  const containers::DynamicArray<material::ParameterRecord>& parameters,
                                                  const containers::DynamicArray<material::ResourceParameterRecord>& resourceParameters,
                                                  const containers::DynamicArray<u8>& data) noexcept
    {
        if (name == 0 || !IsTypedReference(shader, shaders::ShaderResourceType) || techniques.Empty())
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
            u32 previousEnd = 0;
            for (u32 member = 0; member < buffer.parameterCount; ++member)
            {
                const material::ParameterRecord& parameter = parameters[expectedParameter + member];
                if (parameter.name == 0 || parameter.buffer != index || parameter.byteSize == 0 || parameter.byteOffset < previousEnd ||
                    parameter.byteOffset > buffer.byteSize || parameter.byteSize > buffer.byteSize - parameter.byteOffset ||
                    parameter.scalarType > shaders::ScalarType::F64 || parameter.rows == 0 || parameter.rows > 4 || parameter.columns == 0 ||
                    parameter.columns > 4)
                    return material::Result::InvalidLayout;
                previousEnd = parameter.byteOffset + parameter.byteSize;
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
            if (parameter.name == 0 || !IsResourceParameterKindValid(parameter.kind) || parameter.dependency > resources::DependencyKind::Soft ||
                (!parameter.resource.IsValid() && parameter.dependency == resources::DependencyKind::Required) ||
                (parameter.resource.IsValid() && !parameter.resource.IsTyped()))
                return material::Result::InvalidLayout;
            if (index > 0)
            {
                const material::ResourceParameterRecord& prior = resourceParameters[index - 1u];
                if (parameter.name < prior.name || (parameter.name == prior.name && parameter.arrayIndex != prior.arrayIndex + 1u))
                    return material::Result::InvalidLayout;
            }
            if ((index == 0 || parameter.name != resourceParameters[index - 1u].name) && parameter.arrayIndex != 0)
                return material::Result::InvalidLayout;
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
        const serialization::Result headerResult = serialization::ReadDocumentHeader(reader, MaterialMagic, {1, 0, 0}, documentLimits, header);
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
        if (!metadataReader.ReadU64(m_name) || !ReadReference(metadataReader, m_shader) || !metadataReader.ReadU32(techniqueCount) ||
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
        const Result validation = ValidateLoaded(m_name, m_shader, m_techniques, m_constantBuffers, m_parameters, m_resourceParameters, m_parameterData);
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
