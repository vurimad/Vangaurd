#include <vanguard/shaders/shaders.hpp>

#include <algorithm>

namespace
{
    using namespace vanguard;
    namespace shader = vanguard::shaders;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 1};
    constexpr u32 MetadataSection = serialization::MakeFourCC('M', 'E', 'T', 'A');
    constexpr u32 BytecodeSection = serialization::MakeFourCC('C', 'O', 'D', 'E');
    constexpr u32 MetadataWireVersion = 2;
    constexpr u64 MaximumMetadataBytes = 64ull * 1024ull * 1024ull;
    constexpr u32 InvalidIndex = 0xffffffffu;

    using ByteArray = containers::DynamicArray<u8>;

    [[nodiscard]] u32 StringLength(const char* const value, const u32 capacity) noexcept
    {
        if (value == nullptr)
            return capacity;
        u32 length = 0;
        while (length < capacity && value[length] != '\0')
            ++length;
        return length;
    }

    [[nodiscard]] bool WriteName(serialization::BinaryWriter& writer, const char* const value) noexcept
    {
        const u32 length = StringLength(value, shader::MaximumEntryPointLength);
        return length != 0 && length < shader::MaximumEntryPointLength && writer.WriteU16(static_cast<u16>(length)) &&
               writer.WriteBytes(value, length);
    }

    [[nodiscard]] bool ReadName(serialization::BinaryReader& reader, char (&value)[shader::MaximumEntryPointLength]) noexcept
    {
        u16 length = 0;
        if (!reader.ReadU16(length) || length == 0 || length >= shader::MaximumEntryPointLength || !reader.ReadBytes(value, length))
            return false;
        value[length] = '\0';
        return true;
    }

    struct CanonicalData
    {
        CanonicalData() noexcept
            : stages(memory::pools::Rendering::GetInstance()), bindings(memory::pools::Rendering::GetInstance()),
              constantBuffers(memory::pools::Rendering::GetInstance()), constantMembers(memory::pools::Rendering::GetInstance()),
              vertexInputs(memory::pools::Rendering::GetInstance()), fragmentOutputs(memory::pools::Rendering::GetInstance()),
              specializationConstants(memory::pools::Rendering::GetInstance())
        {
        }

        containers::DynamicArray<shader::StageBuildRecord> stages;
        containers::DynamicArray<shader::DescriptorBinding> bindings;
        containers::DynamicArray<shader::ConstantBuffer> constantBuffers;
        containers::DynamicArray<shader::ConstantMember> constantMembers;
        containers::DynamicArray<shader::VertexInput> vertexInputs;
        containers::DynamicArray<shader::FragmentOutput> fragmentOutputs;
        containers::DynamicArray<shader::SpecializationConstant> specializationConstants;
    };

    [[nodiscard]] shader::Result ConvertSerializationResult(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return shader::Result::Success;
        case serialization::Result::InvalidMagic:
            return shader::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
            return shader::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return shader::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return shader::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return shader::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return shader::Result::IoFailure;
        default:
            return shader::Result::InvalidLayout;
        }
    }

    [[nodiscard]] shader::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? shader::Result::Success : ConvertSerializationResult(writer.Status());
    }

    [[nodiscard]] shader::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? shader::Result::Success : ConvertSerializationResult(reader.Status());
    }

    template <typename Type> void CopySpan(const containers::ArraySpan<const Type> source, containers::DynamicArray<Type>& destination)
    {
        destination.Reserve(source.Size());
        for (u32 index = 0; index < source.Size(); ++index)
        {
            destination.PushBack(source[index]);
        }
    }

    [[nodiscard]] bool IsValidStage(const shader::ShaderStage stage) noexcept
    {
        return stage < shader::ShaderStage::Count;
    }

    [[nodiscard]] bool IsValidFormat(const shader::NativeFormat format) noexcept
    {
        return format <= shader::NativeFormat::ConsoleNative;
    }

    [[nodiscard]] bool IsValidBindingKind(const shader::BindingKind kind) noexcept
    {
        return kind <= shader::BindingKind::AccelerationStructure;
    }

    [[nodiscard]] bool IsValidAccess(const shader::BindingAccess access) noexcept
    {
        return access <= shader::BindingAccess::ReadWrite;
    }

    [[nodiscard]] bool IsValidScalarType(const shader::ScalarType type) noexcept
    {
        return type <= shader::ScalarType::F64;
    }

    [[nodiscard]] bool IsValidNumericClass(const shader::NumericClass type) noexcept
    {
        return type <= shader::NumericClass::UnsignedInteger;
    }

    [[nodiscard]] bool IsValidPrimitiveClass(const shader::PrimitiveClass type) noexcept
    {
        return type <= shader::PrimitiveClass::Mesh;
    }

    [[nodiscard]] bool IsGraphicsStage(const shader::ShaderStage stage) noexcept
    {
        return stage == shader::ShaderStage::Vertex || stage == shader::ShaderStage::Hull || stage == shader::ShaderStage::Domain ||
               stage == shader::ShaderStage::Geometry || stage == shader::ShaderStage::Fragment || stage == shader::ShaderStage::Task ||
               stage == shader::ShaderStage::Mesh;
    }

    [[nodiscard]] shader::Result ValidateProgramStages(const shader::BuildDescription& description, const CanonicalData& data) noexcept
    {
        if (description.program == 0 || data.stages.Empty())
        {
            return shader::Result::InvalidArgument;
        }

        shader::StageMask mask = 0;
        for (const shader::StageBuildRecord& stage : data.stages)
        {
            if (!IsValidStage(stage.stage) || !IsValidFormat(stage.format) || stage.entryPoint == 0 ||
                StringLength(stage.entryPointName, shader::MaximumEntryPointLength) >= shader::MaximumEntryPointLength ||
                StringLength(stage.entryPointName, shader::MaximumEntryPointLength) == 0 || stage.bytecode == nullptr || stage.bytecodeSize == 0)
            {
                return shader::Result::InvalidBytecode;
            }
            const shader::StageMask bit = shader::StageBit(stage.stage);
            if ((mask & bit) != 0)
            {
                return shader::Result::DuplicateStage;
            }
            mask |= bit;
        }

        if (description.pipelineInterface.stages != 0 && description.pipelineInterface.stages != mask)
        {
            return shader::Result::InvalidLayout;
        }

        switch (description.kind)
        {
        case shader::ProgramKind::Graphics:
            if ((mask & (shader::StageBit(shader::ShaderStage::Compute) | shader::StageBit(shader::ShaderStage::Library))) != 0)
            {
                return shader::Result::InvalidLayout;
            }
            for (const shader::StageBuildRecord& stage : data.stages)
            {
                if (!IsGraphicsStage(stage.stage))
                {
                    return shader::Result::InvalidLayout;
                }
            }
            if ((mask & shader::StageBit(shader::ShaderStage::Mesh)) == 0 && (mask & shader::StageBit(shader::ShaderStage::Vertex)) == 0)
            {
                return shader::Result::InvalidLayout;
            }
            break;
        case shader::ProgramKind::Compute:
            if (mask != shader::StageBit(shader::ShaderStage::Compute) || description.pipelineInterface.threadGroupSizeX == 0 ||
                description.pipelineInterface.threadGroupSizeY == 0 || description.pipelineInterface.threadGroupSizeZ == 0)
            {
                return shader::Result::InvalidLayout;
            }
            break;
        case shader::ProgramKind::Library:
            if (mask != shader::StageBit(shader::ShaderStage::Library))
            {
                return shader::Result::InvalidLayout;
            }
            break;
        default:
            return shader::Result::InvalidLayout;
        }
        return shader::Result::Success;
    }

    [[nodiscard]] shader::Result Canonicalize(const shader::BuildDescription& description, CanonicalData& output) noexcept
    {
        enum class DescriptorNamespace : u8 { ConstantBuffer, ShaderResource, UnorderedAccess, Sampler };
        const auto bindingNamespace = [](const shader::BindingKind kind) noexcept
        {
            if (kind == shader::BindingKind::ConstantBuffer) return DescriptorNamespace::ConstantBuffer;
            if (kind == shader::BindingKind::Sampler) return DescriptorNamespace::Sampler;
            if (kind == shader::BindingKind::StorageTexture || kind == shader::BindingKind::ReadWriteStructuredBuffer ||
                kind == shader::BindingKind::ReadWriteByteAddressBuffer)
                return DescriptorNamespace::UnorderedAccess;
            return DescriptorNamespace::ShaderResource;
        };

        CopySpan(description.stages, output.stages);
        CopySpan(description.bindings, output.bindings);
        CopySpan(description.vertexInputs, output.vertexInputs);
        CopySpan(description.fragmentOutputs, output.fragmentOutputs);
        CopySpan(description.specializationConstants, output.specializationConstants);

        std::sort(output.stages.Begin(), output.stages.End(),
                  [](const shader::StageBuildRecord& left, const shader::StageBuildRecord& right) { return left.stage < right.stage; });
        std::sort(output.bindings.Begin(), output.bindings.End(),
                  [&bindingNamespace](const shader::DescriptorBinding& left, const shader::DescriptorBinding& right)
                  {
                      if (left.space != right.space) return left.space < right.space;
                      const DescriptorNamespace leftNamespace = bindingNamespace(left.kind);
                      const DescriptorNamespace rightNamespace = bindingNamespace(right.kind);
                      if (leftNamespace != rightNamespace) return leftNamespace < rightNamespace;
                      if (left.binding != right.binding) return left.binding < right.binding;
                      return left.kind < right.kind;
                  });
        std::sort(output.vertexInputs.Begin(), output.vertexInputs.End(),
                  [](const shader::VertexInput& left, const shader::VertexInput& right)
                  { return left.location != right.location ? left.location < right.location : left.semanticIndex < right.semanticIndex; });
        std::sort(output.fragmentOutputs.Begin(), output.fragmentOutputs.End(),
                  [](const shader::FragmentOutput& left, const shader::FragmentOutput& right)
                  { return left.location != right.location ? left.location < right.location : left.blendSource < right.blendSource; });
        std::sort(output.specializationConstants.Begin(), output.specializationConstants.End(),
                  [](const shader::SpecializationConstant& left, const shader::SpecializationConstant& right)
                  { return left.id < right.id; });

        containers::DynamicArray<u32> bufferOrder{memory::pools::Rendering::GetInstance()};
        bufferOrder.Reserve(description.constantBuffers.Size());
        for (u32 index = 0; index < description.constantBuffers.Size(); ++index)
        {
            bufferOrder.PushBack(index);
        }
        std::sort(bufferOrder.Begin(), bufferOrder.End(),
                  [&description](const u32 left, const u32 right)
                  {
                      const shader::ConstantBuffer& a = description.constantBuffers[left];
                      const shader::ConstantBuffer& b = description.constantBuffers[right];
                      return a.space != b.space ? a.space < b.space : a.binding < b.binding;
                  });

        for (const u32 sourceIndex : bufferOrder)
        {
            shader::ConstantBuffer buffer = description.constantBuffers[sourceIndex];
            if (buffer.firstMember > description.constantMembers.Size() ||
                buffer.memberCount > description.constantMembers.Size() - buffer.firstMember)
            {
                return shader::Result::InvalidLayout;
            }

            containers::DynamicArray<shader::ConstantMember> members{memory::pools::Rendering::GetInstance()};
            members.Reserve(buffer.memberCount);
            for (u32 memberIndex = 0; memberIndex < buffer.memberCount; ++memberIndex)
            {
                members.PushBack(description.constantMembers[buffer.firstMember + memberIndex]);
            }
            std::sort(members.Begin(), members.End(), [](const shader::ConstantMember& left, const shader::ConstantMember& right)
                      { return left.byteOffset != right.byteOffset ? left.byteOffset < right.byteOffset : left.name < right.name; });
            buffer.firstMember = output.constantMembers.Size();
            output.constantBuffers.PushBack(buffer);
            for (const shader::ConstantMember& member : members)
            {
                output.constantMembers.PushBack(member);
            }
        }

        shader::Result result = ValidateProgramStages(description, output);
        if (result != shader::Result::Success)
        {
            return result;
        }
        if (!IsValidPrimitiveClass(description.pipelineInterface.primitiveClass) || description.pipelineInterface.renderTargetCount > 8)
        {
            return shader::Result::InvalidLayout;
        }

        for (u32 index = 0; index < output.bindings.Size(); ++index)
        {
            const shader::DescriptorBinding& binding = output.bindings[index];
            const u16 flags = static_cast<u16>(binding.flags);
            const bool bindless = shader::HasFlag(binding.flags, shader::BindingFlags::Bindless);
            if (binding.name == 0 || binding.arrayCount == 0 || !IsValidBindingKind(binding.kind) || !IsValidAccess(binding.access) ||
                binding.stages == 0 || (binding.stages & ~description.pipelineInterface.stages) != 0 ||
                (flags & ~static_cast<u16>(shader::BindingFlags::Bindless)) != 0 ||
                bindless != (binding.arrayCount == shader::UnboundedDescriptorCount) ||
                (!bindless && (binding.arrayCount > 0xffffu ||
                               static_cast<u64>(binding.binding) + binding.arrayCount > 0x100000000ull)))
            {
                return shader::Result::InvalidLayout;
            }
            for (u32 previousIndex = 0; previousIndex < index; ++previousIndex)
            {
                const shader::DescriptorBinding& previous = output.bindings[previousIndex];
                if (previous.space == binding.space && bindingNamespace(previous.kind) == bindingNamespace(binding.kind))
                {
                    const u64 previousEnd = previous.arrayCount == shader::UnboundedDescriptorCount
                                                ? 0x100000000ull
                                                : static_cast<u64>(previous.binding) + previous.arrayCount;
                    const u64 bindingEnd = binding.arrayCount == shader::UnboundedDescriptorCount
                                               ? 0x100000000ull
                                               : static_cast<u64>(binding.binding) + binding.arrayCount;
                    if (static_cast<u64>(binding.binding) < previousEnd && static_cast<u64>(previous.binding) < bindingEnd)
                        return previous.binding == binding.binding && previous.kind == binding.kind
                                   ? shader::Result::DuplicateBinding
                                   : shader::Result::OverlappingBinding;
                }
            }
        }

        for (u32 index = 0; index < output.constantBuffers.Size(); ++index)
        {
            const shader::ConstantBuffer& buffer = output.constantBuffers[index];
            if (buffer.name == 0 || buffer.byteSize == 0)
            {
                return shader::Result::InvalidLayout;
            }
            if (index != 0)
            {
                const shader::ConstantBuffer& previous = output.constantBuffers[index - 1];
                if (previous.space == buffer.space && previous.binding == buffer.binding)
                {
                    return shader::Result::DuplicateBinding;
                }
            }
            u32 previousEnd = 0;
            for (u32 memberIndex = 0; memberIndex < buffer.memberCount; ++memberIndex)
            {
                const shader::ConstantMember& member = output.constantMembers[buffer.firstMember + memberIndex];
                if (member.name == 0 || member.byteSize == 0 || !IsValidScalarType(member.scalarType) || member.rows == 0 ||
                    member.rows > 4 || member.columns == 0 || member.columns > 4 || member.byteOffset < previousEnd ||
                    member.byteOffset > buffer.byteSize || member.byteSize > buffer.byteSize - member.byteOffset)
                {
                    return shader::Result::InvalidLayout;
                }
                previousEnd = member.byteOffset + member.byteSize;
            }
        }

        for (u32 index = 0; index < output.vertexInputs.Size(); ++index)
        {
            const shader::VertexInput& input = output.vertexInputs[index];
            if (input.semantic == 0 || !IsValidNumericClass(input.numericClass) || input.componentCount == 0 || input.componentCount > 4 ||
                (input.componentBits != 8 && input.componentBits != 16 && input.componentBits != 32 && input.componentBits != 64))
            {
                return shader::Result::InvalidLayout;
            }
            if (index != 0 && output.vertexInputs[index - 1].location == input.location)
            {
                return shader::Result::DuplicateInput;
            }
        }

        for (u32 index = 0; index < output.fragmentOutputs.Size(); ++index)
        {
            const shader::FragmentOutput& fragment = output.fragmentOutputs[index];
            if (fragment.semantic == 0 || fragment.location >= 8 || fragment.blendSource > 1 ||
                !IsValidNumericClass(fragment.numericClass) || fragment.componentMask == 0 || (fragment.componentMask & ~0x0fu) != 0)
            {
                return shader::Result::InvalidLayout;
            }
            if (index != 0 && output.fragmentOutputs[index - 1].location == fragment.location &&
                output.fragmentOutputs[index - 1].blendSource == fragment.blendSource)
            {
                return shader::Result::DuplicateOutput;
            }
        }

        for (u32 index = 0; index < output.specializationConstants.Size(); ++index)
        {
            const shader::SpecializationConstant& constant = output.specializationConstants[index];
            if (constant.name == 0 || !IsValidScalarType(constant.scalarType) || constant.stages == 0 ||
                (constant.stages & ~description.pipelineInterface.stages) != 0 ||
                (index != 0 && output.specializationConstants[index - 1].id == constant.id))
            {
                return shader::Result::InvalidLayout;
            }
        }
        return shader::Result::Success;
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool WriteInterface(serialization::BinaryWriter& writer, const shader::PipelineInterface& value) noexcept
    {
        return writer.WriteU32(value.stages) && writer.WriteU8(static_cast<u8>(value.primitiveClass)) && writer.WriteU8(0) &&
               writer.WriteU16(0) && writer.WriteU32(static_cast<u32>(value.flags)) && writer.WriteU32(value.renderTargetCount) &&
               writer.WriteU32(value.threadGroupSizeX) && writer.WriteU32(value.threadGroupSizeY) &&
               writer.WriteU32(value.threadGroupSizeZ);
    }

    [[nodiscard]] bool ReadInterface(serialization::BinaryReader& reader, shader::PipelineInterface& value) noexcept
    {
        u8 primitiveClass = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        u32 flags = 0;
        if (!reader.ReadU32(value.stages) || !reader.ReadU8(primitiveClass) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) ||
            !reader.ReadU32(flags) || !reader.ReadU32(value.renderTargetCount) || !reader.ReadU32(value.threadGroupSizeX) ||
            !reader.ReadU32(value.threadGroupSizeY) || !reader.ReadU32(value.threadGroupSizeZ) || reserved8 != 0 || reserved16 != 0)
        {
            return false;
        }
        value.primitiveClass = static_cast<shader::PrimitiveClass>(primitiveClass);
        value.flags = static_cast<shader::InterfaceFlags>(flags);
        return true;
    }

    [[nodiscard]] bool WriteBinding(serialization::BinaryWriter& writer, const shader::DescriptorBinding& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.space) && writer.WriteU32(value.binding) &&
               writer.WriteU32(value.arrayCount) && writer.WriteU8(static_cast<u8>(value.kind)) &&
               writer.WriteU8(static_cast<u8>(value.access)) && writer.WriteU16(static_cast<u16>(value.flags)) && writer.WriteU32(value.stages);
    }

    [[nodiscard]] bool ReadBinding(serialization::BinaryReader& reader, shader::DescriptorBinding& value) noexcept
    {
        u8 kind = 0;
        u8 access = 0;
        u16 flags = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.space) || !reader.ReadU32(value.binding) ||
            !reader.ReadU32(value.arrayCount) || !reader.ReadU8(kind) || !reader.ReadU8(access) || !reader.ReadU16(flags) ||
            !reader.ReadU32(value.stages))
        {
            return false;
        }
        value.kind = static_cast<shader::BindingKind>(kind);
        value.access = static_cast<shader::BindingAccess>(access);
        value.flags = static_cast<shader::BindingFlags>(flags);
        return true;
    }

    [[nodiscard]] bool WriteConstantBuffer(serialization::BinaryWriter& writer, const shader::ConstantBuffer& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.space) && writer.WriteU32(value.binding) &&
               writer.WriteU32(value.byteSize) && writer.WriteU32(value.firstMember) && writer.WriteU32(value.memberCount);
    }

    [[nodiscard]] bool ReadConstantBuffer(serialization::BinaryReader& reader, shader::ConstantBuffer& value) noexcept
    {
        return reader.ReadU64(value.name) && reader.ReadU32(value.space) && reader.ReadU32(value.binding) &&
               reader.ReadU32(value.byteSize) && reader.ReadU32(value.firstMember) && reader.ReadU32(value.memberCount);
    }

    [[nodiscard]] bool WriteConstantMember(serialization::BinaryWriter& writer, const shader::ConstantMember& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.byteOffset) && writer.WriteU32(value.byteSize) &&
               writer.WriteU32(value.arrayStride) && writer.WriteU32(value.matrixStride) &&
               writer.WriteU8(static_cast<u8>(value.scalarType)) && writer.WriteU8(value.rows) && writer.WriteU8(value.columns) &&
               writer.WriteBool(value.rowMajor);
    }

    [[nodiscard]] bool ReadConstantMember(serialization::BinaryReader& reader, shader::ConstantMember& value) noexcept
    {
        u8 scalarType = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.byteOffset) || !reader.ReadU32(value.byteSize) ||
            !reader.ReadU32(value.arrayStride) || !reader.ReadU32(value.matrixStride) || !reader.ReadU8(scalarType) ||
            !reader.ReadU8(value.rows) || !reader.ReadU8(value.columns) || !reader.ReadBool(value.rowMajor))
        {
            return false;
        }
        value.scalarType = static_cast<shader::ScalarType>(scalarType);
        return true;
    }

    [[nodiscard]] bool WriteVertexInput(serialization::BinaryWriter& writer, const shader::VertexInput& value) noexcept
    {
        return writer.WriteU64(value.semantic) && writer.WriteU32(value.semanticIndex) && writer.WriteU32(value.location) &&
               writer.WriteU8(static_cast<u8>(value.numericClass)) && writer.WriteU8(value.componentCount) &&
               writer.WriteU8(value.componentBits) && writer.WriteU8(0);
    }

    [[nodiscard]] bool ReadVertexInput(serialization::BinaryReader& reader, shader::VertexInput& value) noexcept
    {
        u8 numericClass = 0;
        u8 reserved = 0;
        if (!reader.ReadU64(value.semantic) || !reader.ReadU32(value.semanticIndex) || !reader.ReadU32(value.location) ||
            !reader.ReadU8(numericClass) || !reader.ReadU8(value.componentCount) || !reader.ReadU8(value.componentBits) ||
            !reader.ReadU8(reserved) || reserved != 0)
        {
            return false;
        }
        value.numericClass = static_cast<shader::NumericClass>(numericClass);
        return true;
    }

    [[nodiscard]] bool WriteFragmentOutput(serialization::BinaryWriter& writer, const shader::FragmentOutput& value) noexcept
    {
        return writer.WriteU64(value.semantic) && writer.WriteU32(value.location) && writer.WriteU32(value.blendSource) &&
               writer.WriteU8(static_cast<u8>(value.numericClass)) && writer.WriteU8(value.componentMask) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadFragmentOutput(serialization::BinaryReader& reader, shader::FragmentOutput& value) noexcept
    {
        u8 numericClass = 0;
        u16 reserved = 0;
        if (!reader.ReadU64(value.semantic) || !reader.ReadU32(value.location) || !reader.ReadU32(value.blendSource) ||
            !reader.ReadU8(numericClass) || !reader.ReadU8(value.componentMask) || !reader.ReadU16(reserved) || reserved != 0)
        {
            return false;
        }
        value.numericClass = static_cast<shader::NumericClass>(numericClass);
        return true;
    }

    [[nodiscard]] bool WriteSpecializationConstant(serialization::BinaryWriter& writer,
                                                   const shader::SpecializationConstant& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.id) && writer.WriteU8(static_cast<u8>(value.scalarType)) &&
               writer.WriteU8(0) && writer.WriteU16(0) && writer.WriteU64(value.defaultValueBits) && writer.WriteU32(value.stages);
    }

    [[nodiscard]] bool ReadSpecializationConstant(serialization::BinaryReader& reader, shader::SpecializationConstant& value) noexcept
    {
        u8 scalarType = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU32(value.id) || !reader.ReadU8(scalarType) || !reader.ReadU8(reserved8) ||
            !reader.ReadU16(reserved16) || !reader.ReadU64(value.defaultValueBits) || !reader.ReadU32(value.stages) || reserved8 != 0 ||
            reserved16 != 0)
        {
            return false;
        }
        value.scalarType = static_cast<shader::ScalarType>(scalarType);
        return true;
    }

    [[nodiscard]] shader::Result WriteLayoutBytes(serialization::BinaryWriter& writer, const shader::BuildDescription& description,
                                                  const CanonicalData& data) noexcept
    {
        if (!WriteInterface(writer, description.pipelineInterface) || !writer.WriteU32(data.bindings.Size()) ||
            !writer.WriteU32(data.constantBuffers.Size()) || !writer.WriteU32(data.constantMembers.Size()) ||
            !writer.WriteU32(data.vertexInputs.Size()) || !writer.WriteU32(data.fragmentOutputs.Size()) ||
            !writer.WriteU32(data.specializationConstants.Size()))
        {
            return WriterResult(writer);
        }
        for (const shader::DescriptorBinding& value : data.bindings)
        {
            if (!WriteBinding(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::ConstantBuffer& value : data.constantBuffers)
        {
            if (!WriteConstantBuffer(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::ConstantMember& value : data.constantMembers)
        {
            if (!WriteConstantMember(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::VertexInput& value : data.vertexInputs)
        {
            if (!WriteVertexInput(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::FragmentOutput& value : data.fragmentOutputs)
        {
            if (!WriteFragmentOutput(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::SpecializationConstant& value : data.specializationConstants)
        {
            if (!WriteSpecializationConstant(writer, value))
            {
                return WriterResult(writer);
            }
        }
        return shader::Result::Success;
    }

    [[nodiscard]] shader::Result BuildLayoutFingerprint(const shader::BuildDescription& description, const CanonicalData& data,
                                                        crypto::Digest256& fingerprint) noexcept
    {
        ByteArray bytes{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU8(static_cast<u8>(description.kind)))
        {
            return WriterResult(writer);
        }
        const shader::Result result = WriteLayoutBytes(writer, description, data);
        if (result != shader::Result::Success)
        {
            return result;
        }
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return shader::Result::Success;
    }

    [[nodiscard]] shader::Result BuildBindingLayoutFingerprint(const CanonicalData& data, crypto::Digest256& fingerprint) noexcept
    {
        ByteArray bytes{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(data.bindings.Size()) || !writer.WriteU32(data.constantBuffers.Size()) ||
            !writer.WriteU32(data.constantMembers.Size()) || !writer.WriteU32(data.specializationConstants.Size()))
        {
            return WriterResult(writer);
        }
        for (const shader::DescriptorBinding& value : data.bindings)
        {
            if (!WriteBinding(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::ConstantBuffer& value : data.constantBuffers)
        {
            if (!WriteConstantBuffer(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::ConstantMember& value : data.constantMembers)
        {
            if (!WriteConstantMember(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::SpecializationConstant& value : data.specializationConstants)
        {
            if (!WriteSpecializationConstant(writer, value))
            {
                return WriterResult(writer);
            }
        }
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return shader::Result::Success;
    }

    [[nodiscard]] shader::Result BuildPipelineInterfaceFingerprint(const shader::BuildDescription& description, const CanonicalData& data,
                                                                   crypto::Digest256& fingerprint) noexcept
    {
        ByteArray bytes{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU8(static_cast<u8>(description.kind)) || !WriteInterface(writer, description.pipelineInterface) ||
            !writer.WriteU32(data.vertexInputs.Size()) || !writer.WriteU32(data.fragmentOutputs.Size()))
        {
            return WriterResult(writer);
        }
        for (const shader::VertexInput& value : data.vertexInputs)
        {
            if (!WriteVertexInput(writer, value))
            {
                return WriterResult(writer);
            }
        }
        for (const shader::FragmentOutput& value : data.fragmentOutputs)
        {
            if (!WriteFragmentOutput(writer, value))
            {
                return WriterResult(writer);
            }
        }
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return shader::Result::Success;
    }

    [[nodiscard]] shader::Result WriteMetadata(ByteArray& metadata, ByteArray& bytecode, const shader::BuildDescription& description,
                                               const CanonicalData& data, const crypto::Digest256& layoutFingerprint,
                                               const crypto::Digest256& bindingLayoutFingerprint,
                                               const crypto::Digest256& pipelineInterfaceFingerprint) noexcept
    {
        metadata.Clear();
        bytecode.Clear();
        filesystem::MemoryFileWriter metadataFile(metadata);
        serialization::BinaryWriter writer(metadataFile);
        if (!writer.WriteU32(MetadataWireVersion) || !writer.WriteU8(static_cast<u8>(description.kind)) || !writer.WriteU8(0) ||
            !writer.WriteU16(0) || !writer.WriteU64(description.program) || !WriteDigest(writer, description.permutation) ||
            !WriteDigest(writer, description.compilerFingerprint) || !WriteDigest(writer, layoutFingerprint) ||
            !WriteDigest(writer, bindingLayoutFingerprint) || !WriteDigest(writer, pipelineInterfaceFingerprint) ||
            !writer.WriteU32(data.stages.Size()))
        {
            return WriterResult(writer);
        }

        u64 bytecodeOffset = 0;
        for (const shader::StageBuildRecord& stage : data.stages)
        {
            const crypto::Digest256 digest = crypto::Sha256(stage.bytecode, stage.bytecodeSize);
            if (!writer.WriteU8(static_cast<u8>(stage.stage)) || !writer.WriteU8(static_cast<u8>(stage.format)) || !writer.WriteU16(0) ||
                !writer.WriteU64(stage.entryPoint) || !WriteName(writer, stage.entryPointName) || !writer.WriteU64(bytecodeOffset) ||
                !writer.WriteU64(stage.bytecodeSize) ||
                !WriteDigest(writer, digest))
            {
                return WriterResult(writer);
            }
            if (stage.bytecodeSize > ~u64{0} - bytecodeOffset || bytecode.Size() > ~u32{0} - stage.bytecodeSize)
            {
                return shader::Result::LimitExceeded;
            }
            const u8* const source = static_cast<const u8*>(stage.bytecode);
            bytecode.Reserve(bytecode.Size() + static_cast<u32>(stage.bytecodeSize));
            for (usize index = 0; index < stage.bytecodeSize; ++index)
            {
                bytecode.PushBack(source[index]);
            }
            bytecodeOffset += stage.bytecodeSize;
        }
        return WriteLayoutBytes(writer, description, data);
    }

    [[nodiscard]] shader::Result WriteDocument(filesystem::IFile& file, const ByteArray& metadata, const ByteArray& bytecode) noexcept
    {
        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = shader::ShaderMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 2;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16))
        {
            return WriterResult(writer);
        }

        serialization::SectionDescriptor sections[2];
        sections[0].id = MetadataSection;
        sections[0].version = {1, 0};
        sections[0].alignmentLog2 = 4;
        sections[0].offset = writer.Position();
        sections[0].storedSize = metadata.Size();
        sections[0].logicalSize = metadata.Size();
        sections[0].storedCrc64 = serialization::Crc64(metadata.Data(), metadata.Size());
        if (!writer.WriteBytes(metadata.Data(), metadata.Size()) || !writer.Align(16))
        {
            return WriterResult(writer);
        }

        sections[1].id = BytecodeSection;
        sections[1].version = {1, 0};
        sections[1].flags = serialization::SectionFlags::Streamable;
        sections[1].alignmentLog2 = 4;
        sections[1].offset = writer.Position();
        sections[1].storedSize = bytecode.Size();
        sections[1].logicalSize = bytecode.Size();
        sections[1].storedCrc64 = serialization::Crc64(bytecode.Data(), bytecode.Size());
        if (!writer.WriteBytes(bytecode.Data(), bytecode.Size()) || !writer.Align(16))
        {
            return WriterResult(writer);
        }

        header.sectionTableOffset = writer.Position();
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success)
            {
                return WriterResult(writer);
            }
        }
        header.fileSize = writer.Position();
        if (!writer.Seek(0) || serialization::WriteDocumentHeader(writer, header) != serialization::Result::Success ||
            !writer.Seek(header.fileSize) || !writer.Flush())
        {
            return WriterResult(writer);
        }
        return shader::Result::Success;
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

    [[nodiscard]] bool DigestsEqual(const crypto::Digest256& left, const crypto::Digest256& right) noexcept
    {
        return left == right;
    }

    [[nodiscard]] bool VertexInputsCompatible(const containers::ArraySpan<const shader::VertexInput> expected,
                                              const containers::ArraySpan<const shader::VertexInput> provided) noexcept
    {
        if (expected.Size() != provided.Size())
        {
            return false;
        }
        for (u32 expectedIndex = 0; expectedIndex < expected.Size(); ++expectedIndex)
        {
            bool found = false;
            for (u32 providedIndex = 0; providedIndex < provided.Size(); ++providedIndex)
            {
                const shader::VertexInput& left = expected[expectedIndex];
                const shader::VertexInput& right = provided[providedIndex];
                if (left.location == right.location)
                {
                    found = left.numericClass == right.numericClass && left.componentCount == right.componentCount &&
                            left.componentBits == right.componentBits;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

namespace vanguard::shaders
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
        case Result::DuplicateStage:
            return "DuplicateStage";
        case Result::DuplicateBinding:
            return "DuplicateBinding";
        case Result::OverlappingBinding:
            return "OverlappingBinding";
        case Result::DuplicateInput:
            return "DuplicateInput";
        case Result::DuplicateOutput:
            return "DuplicateOutput";
        case Result::InvalidBytecode:
            return "InvalidBytecode";
        case Result::IncompatiblePipeline:
            return "IncompatiblePipeline";
        case Result::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    ShaderFile::ShaderFile() noexcept
        : m_stages(memory::pools::Rendering::GetInstance()), m_bindings(memory::pools::Rendering::GetInstance()),
          m_constantBuffers(memory::pools::Rendering::GetInstance()), m_constantMembers(memory::pools::Rendering::GetInstance()),
          m_vertexInputs(memory::pools::Rendering::GetInstance()), m_fragmentOutputs(memory::pools::Rendering::GetInstance()),
          m_specializationConstants(memory::pools::Rendering::GetInstance()), m_bytecode(memory::pools::Rendering::GetInstance())
    {
    }

    Result ShaderFile::Open(filesystem::IFile& file, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 2;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(reader, ShaderMagic, {1, 1, 1}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(headerResult);
        }

        containers::DynamicArray<serialization::SectionDescriptor> sections{memory::pools::Serialization::GetInstance()};
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success || sections.Size() != 2)
        {
            return sectionResult == serialization::Result::Success ? Result::InvalidLayout : ConvertSerializationResult(sectionResult);
        }
        const serialization::SectionDescriptor* const metadataSection = FindSection(sections, MetadataSection);
        const serialization::SectionDescriptor* const bytecodeSection = FindSection(sections, BytecodeSection);
        if (metadataSection == nullptr || bytecodeSection == nullptr || metadataSection->codec != serialization::Codec::None ||
            bytecodeSection->codec != serialization::Codec::None || metadataSection->version != serialization::Version{1, 0} ||
            bytecodeSection->version != serialization::Version{1, 0} || metadataSection->logicalSize != metadataSection->storedSize ||
            bytecodeSection->logicalSize != bytecodeSection->storedSize)
        {
            return Result::InvalidLayout;
        }
        if (metadataSection->storedSize > MaximumMetadataBytes || bytecodeSection->storedSize > limits.maximumBytecodeBytes ||
            metadataSection->storedSize > ~u32{0} || bytecodeSection->storedSize > ~u32{0})
        {
            return Result::LimitExceeded;
        }

        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        metadata.Resize(static_cast<u32>(metadataSection->storedSize));
        m_bytecode.Resize(static_cast<u32>(bytecodeSection->storedSize));
        if (!reader.Seek(metadataSection->offset) || !reader.ReadBytes(metadata.Data(), metadata.Size()) ||
            serialization::Crc64(metadata.Data(), metadata.Size()) != metadataSection->storedCrc64 ||
            !reader.Seek(bytecodeSection->offset) || !reader.ReadBytes(m_bytecode.Data(), m_bytecode.Size()) ||
            serialization::Crc64(m_bytecode.Data(), m_bytecode.Size()) != bytecodeSection->storedCrc64)
        {
            Close();
            return reader.Good() ? Result::IntegrityFailure : ReaderResult(reader);
        }

        filesystem::MemoryFileReader metadataFile(metadata, 0);
        serialization::BinaryReader metadataReader(metadataFile);
        u32 metadataVersion = 0;
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        u32 stageCount = 0;
        if (!metadataReader.ReadU32(metadataVersion) || !metadataReader.ReadU8(kind) || !metadataReader.ReadU8(reserved8) ||
            !metadataReader.ReadU16(reserved16) || !metadataReader.ReadU64(m_program) || !ReadDigest(metadataReader, m_permutation) ||
            !ReadDigest(metadataReader, m_compilerFingerprint) || !ReadDigest(metadataReader, m_layoutFingerprint) ||
            !ReadDigest(metadataReader, m_bindingLayoutFingerprint) || !ReadDigest(metadataReader, m_pipelineInterfaceFingerprint) ||
            !metadataReader.ReadU32(stageCount))
        {
            Close();
            return ReaderResult(metadataReader);
        }
        if (metadataVersion != MetadataWireVersion || kind > static_cast<u8>(ProgramKind::Library) || reserved8 != 0 || reserved16 != 0 ||
            m_program == 0 || !ResizeChecked(m_stages, stageCount, limits.maximumStages))
        {
            Close();
            return metadataVersion != MetadataWireVersion ? Result::UnsupportedVersion : Result::InvalidLayout;
        }
        m_kind = static_cast<ProgramKind>(kind);

        StageMask actualStages = 0;
        u64 expectedOffset = 0;
        for (StageRecord& stage : m_stages)
        {
            u8 stageValue = 0;
            u8 format = 0;
            u16 reserved = 0;
            if (!metadataReader.ReadU8(stageValue) || !metadataReader.ReadU8(format) || !metadataReader.ReadU16(reserved) ||
                !metadataReader.ReadU64(stage.entryPoint) || !ReadName(metadataReader, stage.entryPointName) ||
                !metadataReader.ReadU64(stage.bytecodeOffset) ||
                !metadataReader.ReadU64(stage.bytecodeSize) || !ReadDigest(metadataReader, stage.bytecodeDigest))
            {
                Close();
                return ReaderResult(metadataReader);
            }
            stage.stage = static_cast<ShaderStage>(stageValue);
            stage.format = static_cast<NativeFormat>(format);
            const StageMask bit = StageBit(stage.stage);
            if (reserved != 0 || !IsValidStage(stage.stage) || !IsValidFormat(stage.format) || stage.entryPoint == 0 ||
                stage.bytecodeSize == 0 || stage.bytecodeOffset != expectedOffset || stage.bytecodeOffset > m_bytecode.Size() ||
                stage.bytecodeSize > m_bytecode.Size() - stage.bytecodeOffset || (actualStages & bit) != 0)
            {
                Close();
                return Result::InvalidLayout;
            }
            const crypto::Digest256 actualDigest =
                crypto::Sha256(m_bytecode.TypedData() + stage.bytecodeOffset, static_cast<usize>(stage.bytecodeSize));
            if (!DigestsEqual(actualDigest, stage.bytecodeDigest))
            {
                Close();
                return Result::IntegrityFailure;
            }
            actualStages |= bit;
            expectedOffset += stage.bytecodeSize;
        }
        if (expectedOffset != m_bytecode.Size() || !ReadInterface(metadataReader, m_interface))
        {
            Close();
            return metadataReader.Good() ? Result::InvalidLayout : ReaderResult(metadataReader);
        }

        u32 counts[6]{};
        for (u32& count : counts)
        {
            if (!metadataReader.ReadU32(count))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        if (!ResizeChecked(m_bindings, counts[0], limits.maximumBindings) ||
            !ResizeChecked(m_constantBuffers, counts[1], limits.maximumConstantBuffers) ||
            !ResizeChecked(m_constantMembers, counts[2], limits.maximumConstantMembers) ||
            !ResizeChecked(m_vertexInputs, counts[3], limits.maximumVertexInputs) ||
            !ResizeChecked(m_fragmentOutputs, counts[4], limits.maximumFragmentOutputs) ||
            !ResizeChecked(m_specializationConstants, counts[5], limits.maximumSpecializationConstants))
        {
            Close();
            return Result::LimitExceeded;
        }
        for (DescriptorBinding& value : m_bindings)
        {
            if (!ReadBinding(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        for (ConstantBuffer& value : m_constantBuffers)
        {
            if (!ReadConstantBuffer(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        for (ConstantMember& value : m_constantMembers)
        {
            if (!ReadConstantMember(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        for (VertexInput& value : m_vertexInputs)
        {
            if (!ReadVertexInput(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        for (FragmentOutput& value : m_fragmentOutputs)
        {
            if (!ReadFragmentOutput(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        for (SpecializationConstant& value : m_specializationConstants)
        {
            if (!ReadSpecializationConstant(metadataReader, value))
            {
                Close();
                return ReaderResult(metadataReader);
            }
        }
        if (metadataReader.Position() != metadataReader.Size() || m_interface.stages != actualStages)
        {
            Close();
            return Result::InvalidLayout;
        }

        BuildDescription validation;
        validation.kind = m_kind;
        validation.program = m_program;
        validation.pipelineInterface = m_interface;
        containers::DynamicArray<StageBuildRecord> stageBuild{memory::pools::Rendering::GetInstance()};
        stageBuild.Reserve(m_stages.Size());
        for (const StageRecord& stage : m_stages)
        {
            stageBuild.PushBack({stage.stage, stage.format, stage.entryPoint, m_bytecode.TypedData() + stage.bytecodeOffset,
                                 static_cast<usize>(stage.bytecodeSize), stage.entryPointName});
        }
        validation.stages = stageBuild;
        validation.bindings = m_bindings;
        validation.constantBuffers = m_constantBuffers;
        validation.constantMembers = m_constantMembers;
        validation.vertexInputs = m_vertexInputs;
        validation.fragmentOutputs = m_fragmentOutputs;
        validation.specializationConstants = m_specializationConstants;
        CanonicalData canonical;
        Result result = Canonicalize(validation, canonical);
        crypto::Digest256 actualLayout;
        crypto::Digest256 actualBindingLayout;
        crypto::Digest256 actualPipelineInterface;
        if (result == Result::Success)
        {
            result = BuildLayoutFingerprint(validation, canonical, actualLayout);
        }
        if (result == Result::Success)
        {
            result = BuildBindingLayoutFingerprint(canonical, actualBindingLayout);
        }
        if (result == Result::Success)
        {
            result = BuildPipelineInterfaceFingerprint(validation, canonical, actualPipelineInterface);
        }
        if (result != Result::Success || !DigestsEqual(actualLayout, m_layoutFingerprint) ||
            !DigestsEqual(actualBindingLayout, m_bindingLayoutFingerprint) ||
            !DigestsEqual(actualPipelineInterface, m_pipelineInterfaceFingerprint))
        {
            Close();
            return result == Result::Success ? Result::IntegrityFailure : result;
        }
        m_open = true;
        return Result::Success;
    }

    void ShaderFile::Close() noexcept
    {
        m_kind = ProgramKind::Graphics;
        m_program = 0;
        m_permutation = {};
        m_compilerFingerprint = {};
        m_layoutFingerprint = {};
        m_bindingLayoutFingerprint = {};
        m_pipelineInterfaceFingerprint = {};
        m_interface = {};
        m_stages.Clear();
        m_bindings.Clear();
        m_constantBuffers.Clear();
        m_constantMembers.Clear();
        m_vertexInputs.Clear();
        m_fragmentOutputs.Clear();
        m_specializationConstants.Clear();
        m_bytecode.Clear();
        m_open = false;
    }

    bool ShaderFile::IsOpen() const noexcept
    {
        return m_open;
    }
    ProgramKind ShaderFile::Kind() const noexcept
    {
        return m_kind;
    }
    u64 ShaderFile::Program() const noexcept
    {
        return m_program;
    }
    const crypto::Digest256& ShaderFile::Permutation() const noexcept
    {
        return m_permutation;
    }
    const crypto::Digest256& ShaderFile::CompilerFingerprint() const noexcept
    {
        return m_compilerFingerprint;
    }
    const crypto::Digest256& ShaderFile::LayoutFingerprint() const noexcept
    {
        return m_layoutFingerprint;
    }
    const crypto::Digest256& ShaderFile::BindingLayoutFingerprint() const noexcept
    {
        return m_bindingLayoutFingerprint;
    }
    const crypto::Digest256& ShaderFile::PipelineInterfaceFingerprint() const noexcept
    {
        return m_pipelineInterfaceFingerprint;
    }
    const PipelineInterface& ShaderFile::Interface() const noexcept
    {
        return m_interface;
    }
    containers::ArraySpan<const StageRecord> ShaderFile::Stages() const noexcept
    {
        return m_stages;
    }
    containers::ArraySpan<const DescriptorBinding> ShaderFile::Bindings() const noexcept
    {
        return m_bindings;
    }
    containers::ArraySpan<const ConstantBuffer> ShaderFile::ConstantBuffers() const noexcept
    {
        return m_constantBuffers;
    }
    containers::ArraySpan<const ConstantMember> ShaderFile::ConstantMembers() const noexcept
    {
        return m_constantMembers;
    }
    containers::ArraySpan<const VertexInput> ShaderFile::VertexInputs() const noexcept
    {
        return m_vertexInputs;
    }
    containers::ArraySpan<const FragmentOutput> ShaderFile::FragmentOutputs() const noexcept
    {
        return m_fragmentOutputs;
    }
    containers::ArraySpan<const SpecializationConstant> ShaderFile::SpecializationConstants() const noexcept
    {
        return m_specializationConstants;
    }

    containers::ArraySpan<const u8> ShaderFile::Bytecode(const StageRecord& stage) const noexcept
    {
        if (!m_open || stage.bytecodeOffset > m_bytecode.Size() || stage.bytecodeSize > m_bytecode.Size() - stage.bytecodeOffset ||
            stage.bytecodeSize > ~u32{0})
        {
            return {};
        }
        return {m_bytecode.TypedData() + stage.bytecodeOffset, static_cast<u32>(stage.bytecodeSize)};
    }

    Result WriteShader(filesystem::IFile& writer, const BuildDescription& description) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
        {
            return result;
        }
        crypto::Digest256 layoutFingerprint;
        result = BuildLayoutFingerprint(description, canonical, layoutFingerprint);
        if (result != Result::Success)
        {
            return result;
        }
        crypto::Digest256 bindingLayoutFingerprint;
        result = BuildBindingLayoutFingerprint(canonical, bindingLayoutFingerprint);
        if (result != Result::Success)
        {
            return result;
        }
        crypto::Digest256 pipelineInterfaceFingerprint;
        result = BuildPipelineInterfaceFingerprint(description, canonical, pipelineInterfaceFingerprint);
        if (result != Result::Success)
        {
            return result;
        }
        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        ByteArray bytecode{memory::pools::Rendering::GetInstance()};
        result = WriteMetadata(metadata, bytecode, description, canonical, layoutFingerprint, bindingLayoutFingerprint,
                               pipelineInterfaceFingerprint);
        return result == Result::Success ? WriteDocument(writer, metadata, bytecode) : result;
    }

    Result CalculateLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        const Result result = Canonicalize(description, canonical);
        return result == Result::Success ? BuildLayoutFingerprint(description, canonical, fingerprint) : result;
    }

    Result CalculateBindingLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        const Result result = Canonicalize(description, canonical);
        return result == Result::Success ? BuildBindingLayoutFingerprint(canonical, fingerprint) : result;
    }

    Result CalculatePipelineInterfaceFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        const Result result = Canonicalize(description, canonical);
        return result == Result::Success ? BuildPipelineInterfaceFingerprint(description, canonical, fingerprint) : result;
    }

    Result ValidatePipeline(const ShaderFile& shader, const PipelineCompatibility& pipeline) noexcept
    {
        if (!shader.IsOpen() || pipeline.sampleCount == 0)
        {
            return Result::InvalidArgument;
        }
        const PipelineInterface& interfaceData = shader.Interface();
        switch (shader.Kind())
        {
        case ProgramKind::Graphics:
            if (pipeline.kind != PipelineKind::Graphics || pipeline.renderTargetCount != interfaceData.renderTargetCount ||
                (interfaceData.primitiveClass != PrimitiveClass::Any && interfaceData.primitiveClass != pipeline.primitiveClass) ||
                HasFlag(interfaceData.flags, InterfaceFlags::WritesDepth) && !pipeline.depthStencilFormatPresent ||
                HasFlag(interfaceData.flags, InterfaceFlags::DualSourceBlending) != pipeline.dualSourceBlendEnabled ||
                !VertexInputsCompatible(shader.VertexInputs(), pipeline.vertexLayout))
            {
                return Result::IncompatiblePipeline;
            }
            for (const FragmentOutput& output : shader.FragmentOutputs())
            {
                if (output.location >= pipeline.renderTargetCount || pipeline.renderTargetClasses[output.location] != output.numericClass)
                {
                    return Result::IncompatiblePipeline;
                }
            }
            break;
        case ProgramKind::Compute:
            if (pipeline.kind != PipelineKind::Compute)
            {
                return Result::IncompatiblePipeline;
            }
            break;
        case ProgramKind::Library:
            if (pipeline.kind != PipelineKind::RayTracing)
            {
                return Result::IncompatiblePipeline;
            }
            break;
        }
        if (!pipeline.bindingLayoutFingerprint.IsEmpty() &&
            !DigestsEqual(pipeline.bindingLayoutFingerprint, shader.BindingLayoutFingerprint()))
        {
            return Result::IncompatiblePipeline;
        }
        if (!pipeline.pipelineInterfaceFingerprint.IsEmpty() &&
            !DigestsEqual(pipeline.pipelineInterfaceFingerprint, shader.PipelineInterfaceFingerprint()))
        {
            return Result::IncompatiblePipeline;
        }
        return Result::Success;
    }
} // namespace vanguard::shaders
