#include <vanguard/shader_tools/shader_compiler.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>

#include <slang-com-ptr.h>
#include <slang.h>

namespace
{
    using namespace vanguard;
    namespace shader = vanguard::shaders;
    namespace tools = vanguard::shader_tools;

    constexpr u32 MaximumEntryPoints = 32;
    constexpr u64 MaximumCompiledBytes = 512ull * 1024ull * 1024ull;

    [[nodiscard]] bool EqualGuid(const SlangUUID& left, const SlangUUID& right) noexcept
    {
        const u8* const leftBytes = reinterpret_cast<const u8*>(&left);
        const u8* const rightBytes = reinterpret_cast<const u8*>(&right);
        for (usize index = 0; index < sizeof(SlangUUID); ++index)
            if (leftBytes[index] != rightBytes[index])
                return false;
        return true;
    }

    class SourceFileSystem final : public ISlangFileSystem
    {
    public:
        SourceFileSystem(const tools::LoadSourceFunction load, void* const userData) noexcept : m_load(load), m_userData(userData) {}

        SlangResult SLANG_MCALL queryInterface(const SlangUUID& uuid, void** output) noexcept override
        {
            if (output == nullptr)
                return SLANG_E_INVALID_ARG;
            *output = nullptr;
            if (EqualGuid(uuid, ISlangUnknown::getTypeGuid()) || EqualGuid(uuid, ISlangCastable::getTypeGuid()) ||
                EqualGuid(uuid, ISlangFileSystem::getTypeGuid()))
            {
                *output = static_cast<ISlangFileSystem*>(this);
                addRef();
                return SLANG_OK;
            }
            return SLANG_E_NO_INTERFACE;
        }

        uint32_t SLANG_MCALL addRef() noexcept override
        {
            return ++m_references;
        }
        uint32_t SLANG_MCALL release() noexcept override
        {
            return m_references != 0 ? --m_references : 0;
        }

        void* SLANG_MCALL castAs(const SlangUUID& uuid) noexcept override
        {
            return EqualGuid(uuid, ISlangUnknown::getTypeGuid()) || EqualGuid(uuid, ISlangCastable::getTypeGuid()) ||
                           EqualGuid(uuid, ISlangFileSystem::getTypeGuid())
                       ? static_cast<ISlangFileSystem*>(this)
                       : nullptr;
        }

        SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** output) noexcept override
        {
            if (path == nullptr || output == nullptr || m_load == nullptr)
                return SLANG_E_INVALID_ARG;
            *output = nullptr;
            tools::LoadedSource source;
            if (!m_load(path, source, m_userData) || source.content.Empty() || source.content.Data() == nullptr)
                return SLANG_E_NOT_FOUND;
            *output = slang_createBlob(source.content.Data(), source.content.SizeInBytes());
            return *output != nullptr ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
        }

    private:
        tools::LoadSourceFunction m_load = nullptr;
        void* m_userData = nullptr;
        u32 m_references = 1;
    };

    [[nodiscard]] u32 BoundedLength(const char* const value, const u32 capacity) noexcept
    {
        if (value == nullptr)
            return capacity;
        u32 length = 0;
        while (length < capacity && value[length] != '\0')
            ++length;
        return length;
    }

    template <usize Capacity> [[nodiscard]] bool CopyString(char (&destination)[Capacity], const char* const source) noexcept
    {
        const u32 length = BoundedLength(source, static_cast<u32>(Capacity));
        if (length == 0 || length >= Capacity)
            return false;
        for (u32 index = 0; index < length; ++index)
            destination[index] = source[index];
        destination[length] = '\0';
        return true;
    }

    [[nodiscard]] bool ConvertScalar(const slang::TypeReflection::ScalarType input, shader::ScalarType& output) noexcept
    {
        switch (input)
        {
        case slang::TypeReflection::Bool:
            output = shader::ScalarType::Bool;
            return true;
        case slang::TypeReflection::Int16:
            output = shader::ScalarType::I16;
            return true;
        case slang::TypeReflection::UInt16:
            output = shader::ScalarType::U16;
            return true;
        case slang::TypeReflection::Float16:
            output = shader::ScalarType::F16;
            return true;
        case slang::TypeReflection::Int32:
            output = shader::ScalarType::I32;
            return true;
        case slang::TypeReflection::UInt32:
            output = shader::ScalarType::U32;
            return true;
        case slang::TypeReflection::Float32:
            output = shader::ScalarType::F32;
            return true;
        case slang::TypeReflection::Int64:
            output = shader::ScalarType::I64;
            return true;
        case slang::TypeReflection::UInt64:
            output = shader::ScalarType::U64;
            return true;
        case slang::TypeReflection::Float64:
            output = shader::ScalarType::F64;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool ConvertNumeric(const slang::TypeReflection::ScalarType scalar, shader::NumericClass& output, u8& bits) noexcept
    {
        switch (scalar)
        {
        case slang::TypeReflection::Float16:
            output = shader::NumericClass::FloatingPoint;
            bits = 16;
            return true;
        case slang::TypeReflection::Float32:
            output = shader::NumericClass::FloatingPoint;
            bits = 32;
            return true;
        case slang::TypeReflection::Float64:
            output = shader::NumericClass::FloatingPoint;
            bits = 64;
            return true;
        case slang::TypeReflection::Int16:
            output = shader::NumericClass::SignedInteger;
            bits = 16;
            return true;
        case slang::TypeReflection::Int32:
            output = shader::NumericClass::SignedInteger;
            bits = 32;
            return true;
        case slang::TypeReflection::Int64:
            output = shader::NumericClass::SignedInteger;
            bits = 64;
            return true;
        case slang::TypeReflection::UInt16:
            output = shader::NumericClass::UnsignedInteger;
            bits = 16;
            return true;
        case slang::TypeReflection::UInt32:
            output = shader::NumericClass::UnsignedInteger;
            bits = 32;
            return true;
        case slang::TypeReflection::UInt64:
            output = shader::NumericClass::UnsignedInteger;
            bits = 64;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsKnownSize(const usize size) noexcept
    {
        return size != SLANG_UNBOUNDED_SIZE && size != SLANG_UNKNOWN_SIZE && size <= ~u32{0};
    }

    [[nodiscard]] bool AppendConstantMembers(slang::TypeLayoutReflection* layout, const u64 parentName, const u32 baseOffset,
                                             containers::DynamicArray<shader::ConstantMember>& output, const u32 inheritedArrayStride = 0,
                                             const u32 inheritedArrayCount = 1) noexcept
    {
        if (layout == nullptr || layout->getType() == nullptr)
            return false;
        if (layout->getKind() == slang::TypeReflection::Kind::Array)
        {
            if (inheritedArrayStride != 0)
                return false;
            const usize count = layout->getElementCount();
            const usize stride = layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
            if (!IsKnownSize(count) || count == 0 || !IsKnownSize(stride))
                return false;
            return AppendConstantMembers(layout->getElementTypeLayout(), parentName, baseOffset, output, static_cast<u32>(stride), static_cast<u32>(count));
        }
        if (layout->getKind() == slang::TypeReflection::Kind::Struct)
        {
            for (u32 index = 0; index < layout->getFieldCount(); ++index)
            {
                slang::VariableLayoutReflection* const field = layout->getFieldByIndex(index);
                if (field == nullptr || field->getTypeLayout() == nullptr)
                    return false;
                const usize offset = field->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
                if (!IsKnownSize(offset) || offset > ~u32{0} - baseOffset ||
                    !AppendConstantMembers(field->getTypeLayout(), shader::HashInterfaceChildName(parentName, field->getName()),
                                           baseOffset + static_cast<u32>(offset), output,
                                           inheritedArrayStride, inheritedArrayCount))
                    return false;
            }
            return true;
        }

        shader::ConstantMember member;
        member.name = parentName;
        member.byteOffset = baseOffset;
        slang::TypeLayoutReflection* valueLayout = layout;
        if (valueLayout == nullptr || valueLayout->getType() == nullptr)
            return false;
        const usize byteSize = layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
        if (!IsKnownSize(byteSize) || !ConvertScalar(valueLayout->getType()->getScalarType(), member.scalarType))
            return false;
        if (inheritedArrayCount > 1 && inheritedArrayStride > (~u32{0} - static_cast<u32>(byteSize)) / (inheritedArrayCount - 1u))
            return false;
        member.byteSize = inheritedArrayCount > 1 ? (inheritedArrayCount - 1u) * inheritedArrayStride + static_cast<u32>(byteSize) : static_cast<u32>(byteSize);
        member.arrayStride = inheritedArrayStride;
        member.rows = static_cast<u8>(valueLayout->getType()->getRowCount());
        member.columns = static_cast<u8>(valueLayout->getType()->getColumnCount());
        if (member.rows == 0)
            member.rows = 1;
        if (member.columns == 0)
            member.columns = 1;
        if (valueLayout->getKind() == slang::TypeReflection::Kind::Matrix)
        {
            member.rowMajor = valueLayout->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_ROW_MAJOR;
            const u32 majorVectorCount = member.rowMajor ? member.rows : member.columns;
            const usize matrixSize = valueLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
            if (majorVectorCount == 0 || !IsKnownSize(matrixSize) || matrixSize % majorVectorCount != 0)
                return false;
            member.matrixStride = static_cast<u32>(matrixSize / majorVectorCount);
        }
        output.PushBack(member);
        return true;
    }

    [[nodiscard]] bool ConvertBinding(slang::TypeLayoutReflection* layout, shader::BindingKind& kind, shader::BindingAccess& access) noexcept
    {
        if (layout == nullptr || layout->getType() == nullptr)
            return false;
        slang::TypeReflection* type = layout->getType();
        while (type->getKind() == slang::TypeReflection::Kind::Array)
            type = type->getElementType();
        if (type == nullptr)
            return false;
        if (type->getKind() == slang::TypeReflection::Kind::SamplerState)
        {
            kind = shader::BindingKind::Sampler;
            access = shader::BindingAccess::Read;
            return true;
        }
        if (type->getKind() == slang::TypeReflection::Kind::ConstantBuffer || type->getKind() == slang::TypeReflection::Kind::ParameterBlock)
        {
            kind = shader::BindingKind::ConstantBuffer;
            access = shader::BindingAccess::Read;
            return true;
        }
        const SlangResourceAccess resourceAccess = type->getResourceAccess();
        access = resourceAccess == SLANG_RESOURCE_ACCESS_READ    ? shader::BindingAccess::Read
                 : resourceAccess == SLANG_RESOURCE_ACCESS_WRITE ? shader::BindingAccess::Write
                                                                 : shader::BindingAccess::ReadWrite;
        const SlangResourceShape shape = type->getResourceShape();
        switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK)
        {
        case SLANG_TEXTURE_1D:
        case SLANG_TEXTURE_2D:
        case SLANG_TEXTURE_3D:
        case SLANG_TEXTURE_CUBE:
            kind = access == shader::BindingAccess::Read ? shader::BindingKind::SampledTexture : shader::BindingKind::StorageTexture;
            return true;
        case SLANG_TEXTURE_BUFFER:
            kind = access == shader::BindingAccess::Read ? shader::BindingKind::TypedBuffer : shader::BindingKind::ReadWriteTypedBuffer;
            return true;
        case SLANG_STRUCTURED_BUFFER:
            kind = access == shader::BindingAccess::Read ? shader::BindingKind::StructuredBuffer : shader::BindingKind::ReadWriteStructuredBuffer;
            return true;
        case SLANG_BYTE_ADDRESS_BUFFER:
            kind = access == shader::BindingAccess::Read ? shader::BindingKind::ByteAddressBuffer : shader::BindingKind::ReadWriteByteAddressBuffer;
            return true;
        case SLANG_ACCELERATION_STRUCTURE:
            kind = shader::BindingKind::AccelerationStructure;
            access = shader::BindingAccess::Read;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] SlangStage ConvertStage(const shader::ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case shader::ShaderStage::Vertex:
            return SLANG_STAGE_VERTEX;
        case shader::ShaderStage::Hull:
            return SLANG_STAGE_HULL;
        case shader::ShaderStage::Domain:
            return SLANG_STAGE_DOMAIN;
        case shader::ShaderStage::Geometry:
            return SLANG_STAGE_GEOMETRY;
        case shader::ShaderStage::Fragment:
            return SLANG_STAGE_FRAGMENT;
        case shader::ShaderStage::Compute:
            return SLANG_STAGE_COMPUTE;
        case shader::ShaderStage::Task:
            return SLANG_STAGE_AMPLIFICATION;
        case shader::ShaderStage::Mesh:
            return SLANG_STAGE_MESH;
        default:
            return SLANG_STAGE_NONE;
        }
    }

    [[nodiscard]] bool IsSystemSemantic(const char* semantic) noexcept
    {
        if (semantic == nullptr)
            return false;
        return (semantic[0] == 'S' || semantic[0] == 's') && (semantic[1] == 'V' || semantic[1] == 'v') && semantic[2] == '_';
    }

    [[nodiscard]] bool EqualTextIgnoreCase(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return false;
        while (*left != '\0' && *right != '\0')
        {
            const char leftValue = *left >= 'a' && *left <= 'z' ? static_cast<char>(*left - ('a' - 'A')) : *left;
            const char rightValue = *right >= 'a' && *right <= 'z' ? static_cast<char>(*right - ('a' - 'A')) : *right;
            if (leftValue != rightValue)
                return false;
            ++left;
            ++right;
        }
        return *left == *right;
    }

    [[nodiscard]] bool EqualText(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return false;
        while (*left != '\0' && *right != '\0')
        {
            if (*left++ != *right++)
                return false;
        }
        return *left == *right;
    }

    template <typename Reflection> [[nodiscard]] auto* FindAttribute(Reflection* reflection, const char* name) noexcept
    {
        if (reflection == nullptr || name == nullptr)
            return static_cast<slang::Attribute*>(nullptr);
        for (u32 index = 0; index < reflection->getUserAttributeCount(); ++index)
        {
            auto* const attribute = reflection->getUserAttributeByIndex(index);
            if (attribute != nullptr && EqualText(attribute->getName(), name))
                return attribute;
        }
        return static_cast<slang::Attribute*>(nullptr);
    }

    template <usize Capacity> [[nodiscard]] bool ReadAttributeString(slang::Attribute* attribute, const u32 index, char (&output)[Capacity]) noexcept
    {
        if (attribute == nullptr || index >= attribute->getArgumentCount())
            return false;
        usize size = 0;
        const char* const value = attribute->getArgumentValueString(index, &size);
        if (value == nullptr || size == 0 || size >= Capacity)
            return false;
        for (usize character = 0; character < size; ++character)
            output[character] = value[character];
        output[size] = '\0';
        return true;
    }

    [[nodiscard]] bool WriteTypeSignature(vanguard::serialization::BinaryWriter& writer, slang::TypeReflection* type, u32& nodes,
                                          const u32 depth = 0) noexcept
    {
        if (type == nullptr || depth >= 64 || nodes >= 4096)
            return false;
        ++nodes;
        const auto kind = type->getKind();
        const char* const name = type->getName();
        if (!writer.WriteU8(static_cast<u8>(kind)) || !writer.WriteU8(static_cast<u8>(type->getScalarType())) || !writer.WriteU16(0) ||
            !writer.WriteU32(type->getRowCount()) || !writer.WriteU32(type->getColumnCount()) || !writer.WriteU64(shader::HashInterfaceName(name)))
        {
            return false;
        }

        switch (kind)
        {
        case slang::TypeReflection::Kind::Scalar:
        case slang::TypeReflection::Kind::Vector:
        case slang::TypeReflection::Kind::Matrix:
        case slang::TypeReflection::Kind::SamplerState:
            return true;
        case slang::TypeReflection::Kind::Array:
        {
            const usize count = type->getElementCount();
            return IsKnownSize(count) && count != 0 && writer.WriteU64(count) && WriteTypeSignature(writer, type->getElementType(), nodes, depth + 1u);
        }
        case slang::TypeReflection::Kind::Struct:
            if (!writer.WriteU32(type->getFieldCount()))
                return false;
            for (u32 index = 0; index < type->getFieldCount(); ++index)
            {
                slang::VariableReflection* const field = type->getFieldByIndex(index);
                if (field == nullptr || !writer.WriteU64(shader::HashInterfaceName(field->getName())) ||
                    !WriteTypeSignature(writer, field->getType(), nodes, depth + 1u))
                    return false;
            }
            return true;
        case slang::TypeReflection::Kind::Resource:
        case slang::TypeReflection::Kind::TextureBuffer:
        case slang::TypeReflection::Kind::ShaderStorageBuffer:
        {
            if (!writer.WriteU32(static_cast<u32>(type->getResourceShape())) || !writer.WriteU32(static_cast<u32>(type->getResourceAccess())))
                return false;
            slang::TypeReflection* const resultType = type->getResourceResultType();
            // Byte-address buffers and acceleration structures have no element
            // type. Preserve every existing typed-resource signature and encode
            // the absent branch with a kind value Slang cannot produce.
            return resultType != nullptr ? WriteTypeSignature(writer, resultType, nodes, depth + 1u) : writer.WriteU8(0xffu);
        }
        default:
            return false;
        }
    }

    [[nodiscard]] bool FingerprintType(slang::TypeReflection* type, crypto::Digest256& fingerprint) noexcept
    {
        containers::DynamicArray<u8> bytes{memory::pools::Tools::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        vanguard::serialization::BinaryWriter writer(file);
        u32 nodes = 0;
        if (!WriteTypeSignature(writer, type, nodes) || !writer.IsGood())
            return false;
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return !fingerprint.IsEmpty();
    }

    [[nodiscard]] bool ConvertMaterialResourceAccess(const SlangResourceAccess input, shader::MaterialResourceAccess& output) noexcept
    {
        if (input == SLANG_RESOURCE_ACCESS_READ)
            output = shader::MaterialResourceAccess::Read;
        else if (input == SLANG_RESOURCE_ACCESS_WRITE)
            output = shader::MaterialResourceAccess::Write;
        else if (input == SLANG_RESOURCE_ACCESS_READ_WRITE)
            output = shader::MaterialResourceAccess::ReadWrite;
        else
            return false;
        return true;
    }

    [[nodiscard]] bool ConvertMaterialResourceShape(slang::ProgramLayout* const program, slang::TypeReflection* type,
                                                     shader::MaterialResourceKind& kind, shader::MaterialResourceShape& output) noexcept
    {
        output = {};
        if (program == nullptr || type == nullptr)
            return false;
        while (type->getKind() == slang::TypeReflection::Kind::Array)
            type = type->getElementType();
        if (type == nullptr)
            return false;
        if (type->getKind() == slang::TypeReflection::Kind::SamplerState)
        {
            kind = shader::MaterialResourceKind::Sampler;
            output.access = shader::MaterialResourceAccess::Read;
            output.samplerKind = EqualText(type->getName(), "SamplerComparisonState") ? shader::MaterialSamplerKind::Comparison
                                                                                       : shader::MaterialSamplerKind::Filtering;
            return shader::IsValidMaterialResourceShape(kind, output);
        }
        if (!ConvertMaterialResourceAccess(type->getResourceAccess(), output.access))
            return false;
        const SlangResourceShape reflectedShape = type->getResourceShape();
        const SlangResourceShape baseShape = static_cast<SlangResourceShape>(reflectedShape & SLANG_RESOURCE_BASE_SHAPE_MASK);
        switch (baseShape)
        {
        case SLANG_TEXTURE_1D:
        case SLANG_TEXTURE_2D:
        case SLANG_TEXTURE_3D:
        case SLANG_TEXTURE_CUBE:
        {
            kind = shader::MaterialResourceKind::Texture;
            output.textureDimension = baseShape == SLANG_TEXTURE_1D   ? shader::MaterialTextureDimension::D1
                                      : baseShape == SLANG_TEXTURE_2D ? shader::MaterialTextureDimension::D2
                                      : baseShape == SLANG_TEXTURE_3D ? shader::MaterialTextureDimension::D3
                                                                      : shader::MaterialTextureDimension::Cube;
            if ((reflectedShape & SLANG_TEXTURE_ARRAY_FLAG) != 0)
                output.flags = static_cast<shader::MaterialResourceShapeFlags>(static_cast<u8>(output.flags) |
                                                                               static_cast<u8>(shader::MaterialResourceShapeFlags::Arrayed));
            if ((reflectedShape & SLANG_TEXTURE_MULTISAMPLE_FLAG) != 0)
                output.flags = static_cast<shader::MaterialResourceShapeFlags>(static_cast<u8>(output.flags) |
                                                                               static_cast<u8>(shader::MaterialResourceShapeFlags::Multisampled));
            slang::TypeReflection* const result = type->getResourceResultType();
            const u32 componentCount = result != nullptr ? result->getRowCount() * result->getColumnCount() : 0;
            if (result == nullptr || componentCount == 0 || componentCount > 4 || !ConvertScalar(result->getScalarType(), output.scalarType))
                return false;
            output.componentCount = static_cast<u8>(componentCount);
            return shader::IsValidMaterialResourceShape(kind, output);
        }
        case SLANG_TEXTURE_BUFFER:
        {
            kind = shader::MaterialResourceKind::Buffer;
            output.bufferKind = shader::MaterialBufferKind::Typed;
            slang::TypeReflection* const result = type->getResourceResultType();
            const u32 componentCount = result != nullptr ? result->getRowCount() * result->getColumnCount() : 0;
            if (result == nullptr || componentCount == 0 || componentCount > 4 || !ConvertScalar(result->getScalarType(), output.scalarType))
                return false;
            output.componentCount = static_cast<u8>(componentCount);
            return shader::IsValidMaterialResourceShape(kind, output);
        }
        case SLANG_STRUCTURED_BUFFER:
        {
            kind = shader::MaterialResourceKind::Buffer;
            output.bufferKind = shader::MaterialBufferKind::Structured;
            slang::TypeReflection* const result = type->getResourceResultType();
            slang::TypeLayoutReflection* const resultLayout =
                result != nullptr ? program->getTypeLayout(result, slang::LayoutRules::DefaultStructuredBuffer) : nullptr;
            const usize stride = resultLayout != nullptr ? resultLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM) : 0;
            if (!IsKnownSize(stride) || stride == 0 || stride > ~u32{0})
                return false;
            output.elementStride = static_cast<u32>(stride);
            return shader::IsValidMaterialResourceShape(kind, output);
        }
        case SLANG_BYTE_ADDRESS_BUFFER:
            kind = shader::MaterialResourceKind::Buffer;
            output.bufferKind = shader::MaterialBufferKind::ByteAddress;
            return shader::IsValidMaterialResourceShape(kind, output);
        case SLANG_ACCELERATION_STRUCTURE:
            kind = shader::MaterialResourceKind::AccelerationStructure;
            return shader::IsValidMaterialResourceShape(kind, output);
        default:
            return false;
        }
    }

    [[nodiscard]] bool ExtractMaterialContract(slang::ProgramLayout* layout, const containers::ArraySpan<const tools::EntryPoint> entries,
                                               bool& hasContract, shader::MaterialDomainContract& domain, u32& accessorAbiVersion,
                                               u32& parameterByteSize, containers::DynamicArray<shader::ConstantMember>& parameters,
                                               containers::DynamicArray<shader::MaterialResourceRole>& resources) noexcept
    {
        hasContract = false;
        char domainFunctionName[256]{};
        char parameterTypeName[256]{};
        char resourceTypeName[256]{};
        shader::StageMask participatingStages = 0;

        for (u32 index = 0; index < entries.Size(); ++index)
        {
            slang::EntryPointReflection* const entry = layout->getEntryPointByIndex(index);
            slang::Attribute* const program = entry != nullptr ? FindAttribute(entry->getFunction(), "VanguardMaterialProgram") : nullptr;
            if (program == nullptr)
                continue;
            if (program->getArgumentCount() != 4)
                return false;
            char candidateDomain[256]{};
            char candidateParameters[256]{};
            char candidateResources[256]{};
            int candidateAbi = 0;
            if (!ReadAttributeString(program, 0, candidateDomain) || !ReadAttributeString(program, 1, candidateParameters) ||
                !ReadAttributeString(program, 2, candidateResources) || SLANG_FAILED(program->getArgumentValueInt(3, &candidateAbi)) || candidateAbi <= 0)
            {
                return false;
            }
            if (!hasContract)
            {
                if (!CopyString(domainFunctionName, candidateDomain) || !CopyString(parameterTypeName, candidateParameters) ||
                    !CopyString(resourceTypeName, candidateResources))
                    return false;
                accessorAbiVersion = static_cast<u32>(candidateAbi);
                hasContract = true;
            }
            else if (!EqualText(domainFunctionName, candidateDomain) || !EqualText(parameterTypeName, candidateParameters) ||
                     !EqualText(resourceTypeName, candidateResources) || accessorAbiVersion != static_cast<u32>(candidateAbi))
            {
                return false;
            }
            participatingStages |= shader::StageBit(entries[index].stage);
        }
        if (!hasContract)
            return true;

        slang::FunctionReflection* const domainFunction = layout->findFunctionByName(domainFunctionName);
        slang::Attribute* const domainAttribute = FindAttribute(domainFunction, "VanguardMaterialDomain");
        if (domainFunction == nullptr || domainAttribute == nullptr || domainAttribute->getArgumentCount() != 4 || domainFunction->getParameterCount() != 1)
            return false;
        char domainName[256]{};
        int schemaVersion = 0;
        int legalStages = 0;
        int requiredCapabilities = 0;
        if (!ReadAttributeString(domainAttribute, 0, domainName) || SLANG_FAILED(domainAttribute->getArgumentValueInt(1, &schemaVersion)) ||
            SLANG_FAILED(domainAttribute->getArgumentValueInt(2, &legalStages)) ||
            SLANG_FAILED(domainAttribute->getArgumentValueInt(3, &requiredCapabilities)) || schemaVersion <= 0 || legalStages <= 0 ||
            requiredCapabilities < 0 ||
            !shader::IsValidMaterialShaderCapabilityMask(static_cast<shader::MaterialShaderCapabilityMask>(requiredCapabilities)) ||
            (participatingStages & ~static_cast<shader::StageMask>(legalStages)) != 0)
        {
            return false;
        }
        domain.name = shader::HashInterfaceName(domainName);
        domain.schemaVersion = static_cast<u32>(schemaVersion);
        domain.legalStages = static_cast<shader::StageMask>(legalStages);
        domain.requiredCapabilities = static_cast<shader::MaterialShaderCapabilityMask>(requiredCapabilities);
        if (!FingerprintType(domainFunction->getParameterByIndex(0)->getType(), domain.inputType) ||
            !FingerprintType(domainFunction->getReturnType(), domain.outputType))
        {
            return false;
        }

        slang::TypeReflection* const parameterType = layout->findTypeByName(parameterTypeName);
        if (parameterType == nullptr || FindAttribute(parameterType, "VanguardMaterialParameters") == nullptr)
            return false;
        slang::TypeLayoutReflection* const parameterLayout = layout->getTypeLayout(parameterType, slang::LayoutRules::DefaultStructuredBuffer);
        if (parameterLayout == nullptr)
            return false;
        const usize reflectedByteSize = parameterLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
        if (!IsKnownSize(reflectedByteSize) || !AppendConstantMembers(parameterLayout, shader::HashInterfaceName(parameterTypeName), 0, parameters))
            return false;
        parameterByteSize = static_cast<u32>(reflectedByteSize);

        slang::TypeReflection* const resourceType = layout->findTypeByName(resourceTypeName);
        if (resourceType == nullptr || resourceType->getKind() != slang::TypeReflection::Kind::Struct ||
            FindAttribute(resourceType, "VanguardMaterialResources") == nullptr)
        {
            return false;
        }
        for (u32 fieldIndex = 0; fieldIndex < resourceType->getFieldCount(); ++fieldIndex)
        {
            slang::VariableReflection* const field = resourceType->getFieldByIndex(fieldIndex);
            slang::Attribute* const resourceAttribute = FindAttribute(field, "VanguardMaterialResource");
            int firstSlot = 0;
            int required = 0;
            if (field == nullptr || resourceAttribute == nullptr || resourceAttribute->getArgumentCount() != 2 ||
                SLANG_FAILED(resourceAttribute->getArgumentValueInt(0, &firstSlot)) || SLANG_FAILED(resourceAttribute->getArgumentValueInt(1, &required)) ||
                firstSlot < 0 || (required != 0 && required != 1))
            {
                return false;
            }
            shader::MaterialResourceKind kind;
            shader::MaterialResourceShape shape;
            crypto::Digest256 typeFingerprint;
            if (!ConvertMaterialResourceShape(layout, field->getType(), kind, shape) || !FingerprintType(field->getType(), typeFingerprint))
                return false;
            usize arrayCount = field->getType()->getKind() == slang::TypeReflection::Kind::Array ? field->getType()->getTotalArrayElementCount() : 1;
            if (!IsKnownSize(arrayCount) || arrayCount == 0 || arrayCount > ~u32{0} || static_cast<u64>(firstSlot) + arrayCount > 0x100000000ull)
                return false;
            for (u32 arrayIndex = 0; arrayIndex < arrayCount; ++arrayIndex)
            {
                shader::MaterialResourceRole role;
                role.name = shader::HashInterfaceName(field->getName());
                role.arrayIndex = arrayIndex;
                role.slot = static_cast<u32>(firstSlot) + arrayIndex;
                role.kind = kind;
                role.flags = required != 0 ? shader::MaterialResourceFlags::Required : shader::MaterialResourceFlags::None;
                role.typeFingerprint = typeFingerprint;
                role.shape = shape;
                resources.PushBack(role);
            }
        }
        return true;
    }

    [[nodiscard]] bool AppendVaryings(slang::VariableLayoutReflection* variable, const bool input, containers::DynamicArray<shader::VertexInput>& vertexInputs,
                                      containers::DynamicArray<shader::FragmentOutput>& fragmentOutputs, shader::PipelineInterface& interfaceData) noexcept
    {
        if (variable == nullptr || variable->getTypeLayout() == nullptr)
            return false;
        slang::TypeLayoutReflection* const layout = variable->getTypeLayout();
        if (layout->getKind() == slang::TypeReflection::Kind::Struct)
        {
            for (u32 index = 0; index < layout->getFieldCount(); ++index)
                if (!AppendVaryings(layout->getFieldByIndex(index), input, vertexInputs, fragmentOutputs, interfaceData))
                    return false;
            return true;
        }
        const char* const semantic = variable->getSemanticName();
        if (semantic == nullptr)
            return true;
        if (IsSystemSemantic(semantic))
        {
            if (input)
                return true;
            if (EqualTextIgnoreCase(semantic, "SV_Depth"))
            {
                interfaceData.flags = interfaceData.flags | shader::InterfaceFlags::WritesDepth;
                return true;
            }
            if (!EqualTextIgnoreCase(semantic, "SV_Target"))
                return true;
        }
        const usize location = variable->getOffset(input ? SLANG_PARAMETER_CATEGORY_VARYING_INPUT : SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT);
        if (!IsKnownSize(location))
            return false;
        shader::NumericClass numericClass;
        u8 componentBits = 0;
        if (!ConvertNumeric(layout->getType()->getScalarType(), numericClass, componentBits))
            return false;
        u32 componentCount = layout->getType()->getColumnCount();
        if (componentCount == 0)
            componentCount = layout->getType()->getRowCount();
        if (componentCount == 0)
            componentCount = 1;
        if (componentCount > 4)
            return false;
        if (input)
        {
            shader::VertexInput reflected;
            reflected.semantic = shader::HashInterfaceName(semantic);
            reflected.semanticIndex = static_cast<u32>(variable->getSemanticIndex());
            reflected.location = IsSystemSemantic(semantic) ? static_cast<u32>(variable->getSemanticIndex()) : static_cast<u32>(location);
            reflected.numericClass = numericClass;
            reflected.componentCount = static_cast<u8>(componentCount);
            reflected.componentBits = componentBits;
            vertexInputs.PushBack(reflected);
        }
        else
        {
            shader::FragmentOutput reflected;
            reflected.semantic = shader::HashInterfaceName(semantic);
            reflected.location = static_cast<u32>(location);
            reflected.numericClass = numericClass;
            reflected.componentMask = static_cast<u8>((1u << componentCount) - 1u);
            fragmentOutputs.PushBack(reflected);
        }
        return true;
    }

    [[nodiscard]] bool ExtractReflection(slang::ProgramLayout* layout, const containers::ArraySpan<const tools::EntryPoint> entries,
                                         shader::PipelineInterface& interfaceData, containers::DynamicArray<shader::DescriptorBinding>& bindings,
                                         containers::DynamicArray<shader::ConstantBuffer>& constantBuffers,
                                         containers::DynamicArray<shader::ConstantMember>& constantMembers,
                                         containers::DynamicArray<shader::VertexInput>& vertexInputs,
                                         containers::DynamicArray<shader::FragmentOutput>& fragmentOutputs,
                                         containers::DynamicArray<shader::SpecializationConstant>& specializationConstants) noexcept
    {
        if (layout == nullptr || layout->getEntryPointCount() != entries.Size())
            return false;
        shader::StageMask stages = 0;
        for (const tools::EntryPoint& entry : entries)
            stages |= shader::StageBit(entry.stage);
        interfaceData.stages = stages;
        interfaceData.primitiveClass = shader::PrimitiveClass::Any;

        bindings.Reserve(layout->getParameterCount());
        for (u32 index = 0; index < layout->getParameterCount(); ++index)
        {
            slang::VariableLayoutReflection* const parameter = layout->getParameterByIndex(index);
            if (parameter == nullptr || parameter->getTypeLayout() == nullptr || parameter->getType() == nullptr)
                return false;
            if (parameter->getCategory() == slang::ParameterCategory::SpecializationConstant)
            {
                const usize id = parameter->getOffset(SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT);
                shader::SpecializationConstant constant;
                constant.name = shader::HashInterfaceName(parameter->getName());
                constant.stages = stages;
                if (constant.name == 0 || !IsKnownSize(id) || !ConvertScalar(parameter->getType()->getScalarType(), constant.scalarType))
                    return false;
                constant.id = static_cast<u32>(id);
                Slang::ComPtr<ISlangBlob> defaultValue;
                const SlangResult defaultResult = parameter->getVariable()->getDefaultValueBlob(defaultValue.writeRef());
                if (SLANG_SUCCEEDED(defaultResult) && defaultValue)
                {
                    if (defaultValue->getBufferSize() > sizeof(constant.defaultValueBits))
                        return false;
                    const u8* const bytes = static_cast<const u8*>(defaultValue->getBufferPointer());
                    for (usize byteIndex = 0; byteIndex < defaultValue->getBufferSize(); ++byteIndex)
                        constant.defaultValueBits |= static_cast<u64>(bytes[byteIndex]) << (byteIndex * 8u);
                }
                specializationConstants.PushBack(constant);
                continue;
            }
            shader::DescriptorBinding binding;
            binding.name = shader::HashInterfaceName(parameter->getName());
            binding.space = parameter->getBindingSpace();
            binding.binding = parameter->getBindingIndex();
            binding.stages = stages;
            if (binding.name == 0 || binding.space == SLANG_UNKNOWN_SIZE || binding.binding == SLANG_UNKNOWN_SIZE ||
                !ConvertBinding(parameter->getTypeLayout(), binding.kind, binding.access))
                return false;

            const usize elementCount = parameter->getType()->getTotalArrayElementCount();
            if (parameter->getType()->isArray())
            {
                if (elementCount == SLANG_UNKNOWN_SIZE)
                    return false;
                // Slang currently reports unsized resource arrays as either its unbounded sentinel or zero,
                // depending on the target layout. Source-language zero-length resource arrays are not legal.
                if (elementCount == SLANG_UNBOUNDED_SIZE || elementCount == 0)
                {
                    binding.arrayCount = shader::UnboundedDescriptorCount;
                    binding.flags = shader::BindingFlags::Bindless;
                }
                else if (elementCount > ~u32{0})
                    return false;
                else
                    binding.arrayCount = static_cast<u32>(elementCount);
            }
            bindings.PushBack(binding);

            if (binding.kind == shader::BindingKind::ConstantBuffer)
            {
                slang::TypeLayoutReflection* memberLayout = parameter->getTypeLayout()->getElementTypeLayout();
                if (memberLayout == nullptr)
                    return false;
                const usize byteSize = memberLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
                if (!IsKnownSize(byteSize))
                    return false;
                shader::ConstantBuffer buffer;
                buffer.name = binding.name;
                buffer.space = binding.space;
                buffer.binding = binding.binding;
                buffer.byteSize = static_cast<u32>(byteSize);
                buffer.firstMember = constantMembers.Size();
                if (!AppendConstantMembers(memberLayout, buffer.name, 0, constantMembers))
                    return false;
                buffer.memberCount = constantMembers.Size() - buffer.firstMember;
                constantBuffers.PushBack(buffer);
            }
        }

        for (u32 index = 0; index < entries.Size(); ++index)
        {
            slang::EntryPointReflection* const entry = layout->getEntryPointByIndex(index);
            if (entry == nullptr || entry->getStage() != ConvertStage(entries[index].stage))
                return false;
            if (entries[index].stage == shader::ShaderStage::Compute)
            {
                SlangUInt size[3]{};
                entry->getComputeThreadGroupSize(3, size);
                if (size[0] == 0 || size[1] == 0 || size[2] == 0 || size[0] > ~u32{0} || size[1] > ~u32{0} || size[2] > ~u32{0})
                    return false;
                interfaceData.threadGroupSizeX = static_cast<u32>(size[0]);
                interfaceData.threadGroupSizeY = static_cast<u32>(size[1]);
                interfaceData.threadGroupSizeZ = static_cast<u32>(size[2]);
            }
            if (entries[index].stage == shader::ShaderStage::Vertex)
                for (u32 parameterIndex = 0; parameterIndex < entry->getParameterCount(); ++parameterIndex)
                    if (!AppendVaryings(entry->getParameterByIndex(parameterIndex), true, vertexInputs, fragmentOutputs, interfaceData))
                        return false;
            if (entries[index].stage == shader::ShaderStage::Fragment)
            {
                // A discard-only depth pixel entry returns void and has no
                // result layout. That is a valid zero-color-output interface.
                if (auto* result = entry->getResultVarLayout(); result != nullptr && !AppendVaryings(result, false, vertexInputs, fragmentOutputs, interfaceData))
                    return false;
                for (const shader::FragmentOutput& output : fragmentOutputs)
                    if (interfaceData.renderTargetCount <= output.location)
                        interfaceData.renderTargetCount = output.location + 1u;
            }
        }
        return true;
    }

    [[nodiscard]] SlangOptimizationLevel ConvertOptimization(const tools::Optimization optimization) noexcept
    {
        switch (optimization)
        {
        case tools::Optimization::None:
            return SLANG_OPTIMIZATION_LEVEL_NONE;
        case tools::Optimization::Default:
            return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
        case tools::Optimization::High:
            return SLANG_OPTIMIZATION_LEVEL_HIGH;
        case tools::Optimization::Maximum:
            return SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
        }
        return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
    }

    [[nodiscard]] SlangDebugInfoLevel ConvertDebugInformation(const tools::DebugInformation information) noexcept
    {
        switch (information)
        {
        case tools::DebugInformation::None:
            return SLANG_DEBUG_INFO_LEVEL_NONE;
        case tools::DebugInformation::Minimal:
            return SLANG_DEBUG_INFO_LEVEL_MINIMAL;
        case tools::DebugInformation::Standard:
            return SLANG_DEBUG_INFO_LEVEL_STANDARD;
        case tools::DebugInformation::Maximum:
            return SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
        }
        return SLANG_DEBUG_INFO_LEVEL_NONE;
    }

} // namespace

namespace vanguard::shader_tools
{
    struct ShaderCompiler::Impl final
    {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        concurrency::Mutex frontEndLock;
        crypto::Digest256 compilerFingerprint;
        char compilerVersion[64]{};
    };

    CompileOutput::CompileOutput() noexcept
        : m_stages(memory::pools::Tools::GetInstance()), m_bytecode(memory::pools::Tools::GetInstance()), m_dependencies(memory::pools::Tools::GetInstance()),
          m_diagnostics(memory::pools::Tools::GetInstance()), m_bindings(memory::pools::Tools::GetInstance()),
          m_constantBuffers(memory::pools::Tools::GetInstance()), m_constantMembers(memory::pools::Tools::GetInstance()),
          m_vertexInputs(memory::pools::Tools::GetInstance()), m_fragmentOutputs(memory::pools::Tools::GetInstance()),
          m_specializationConstants(memory::pools::Tools::GetInstance()), m_materialParameters(memory::pools::Tools::GetInstance()),
          m_materialResources(memory::pools::Tools::GetInstance())
    {
    }

    void CompileOutput::Reset() noexcept
    {
        m_stages.Clear();
        m_bytecode.Clear();
        m_dependencies.Clear();
        m_diagnostics.Clear();
        m_interface = {};
        m_bindings.Clear();
        m_constantBuffers.Clear();
        m_constantMembers.Clear();
        m_vertexInputs.Clear();
        m_fragmentOutputs.Clear();
        m_specializationConstants.Clear();
        m_hasMaterialContract = false;
        m_materialDomain = {};
        m_materialAccessorAbiVersion = 0;
        m_materialParameterByteSize = 0;
        m_materialParameters.Clear();
        m_materialResources.Clear();
        for (char& value : m_compilerVersion)
            value = '\0';
        m_compilerFingerprint = {};
    }

    containers::ArraySpan<const CompiledStage> CompileOutput::GetStages() const noexcept
    {
        return m_stages;
    }
    containers::ArraySpan<const u8> CompileOutput::GetBytecode() const noexcept
    {
        return m_bytecode;
    }
    containers::ArraySpan<const SourceDependency> CompileOutput::GetDependencies() const noexcept
    {
        return m_dependencies;
    }
    const char* CompileOutput::GetDiagnostics() const noexcept
    {
        return m_diagnostics.Empty() ? "" : static_cast<const char*>(m_diagnostics.Data());
    }
    const char* CompileOutput::CompilerVersion() const noexcept
    {
        return m_compilerVersion;
    }
    const crypto::Digest256& CompileOutput::CompilerFingerprint() const noexcept
    {
        return m_compilerFingerprint;
    }
    const shaders::PipelineInterface& CompileOutput::GetInterface() const noexcept
    {
        return m_interface;
    }
    containers::ArraySpan<const shaders::DescriptorBinding> CompileOutput::Bindings() const noexcept
    {
        return m_bindings;
    }
    containers::ArraySpan<const shaders::ConstantBuffer> CompileOutput::GetConstantBuffers() const noexcept
    {
        return m_constantBuffers;
    }
    containers::ArraySpan<const shaders::ConstantMember> CompileOutput::GetConstantMembers() const noexcept
    {
        return m_constantMembers;
    }
    containers::ArraySpan<const shaders::VertexInput> CompileOutput::GetVertexInputs() const noexcept
    {
        return m_vertexInputs;
    }
    containers::ArraySpan<const shaders::FragmentOutput> CompileOutput::GetFragmentOutputs() const noexcept
    {
        return m_fragmentOutputs;
    }
    containers::ArraySpan<const shaders::SpecializationConstant> CompileOutput::GetSpecializationConstants() const noexcept
    {
        return m_specializationConstants;
    }

    bool CompileOutput::HasMaterialContract() const noexcept
    {
        return m_hasMaterialContract;
    }

    const shaders::MaterialDomainContract& CompileOutput::GetMaterialDomain() const noexcept
    {
        return m_materialDomain;
    }

    u32 CompileOutput::GetMaterialAccessorAbiVersion() const noexcept
    {
        return m_materialAccessorAbiVersion;
    }

    u32 CompileOutput::GetMaterialParameterByteSize() const noexcept
    {
        return m_materialParameterByteSize;
    }

    containers::ArraySpan<const shaders::ConstantMember> CompileOutput::GetMaterialParameters() const noexcept
    {
        return m_materialParameters;
    }

    containers::ArraySpan<const shaders::MaterialResourceRole> CompileOutput::GetMaterialResources() const noexcept
    {
        return m_materialResources;
    }

    Result CompileOutput::WriteShader(filesystem::IFile& writer, const u64 program, const crypto::Digest256& permutation) const noexcept
    {
        if (program == 0 || m_stages.Empty() || m_bytecode.Empty() || m_compilerFingerprint.IsEmpty())
            return Result::InvalidState;
        containers::DynamicArray<shaders::StageBuildRecord> stages(memory::pools::Tools::GetInstance());
        stages.Reserve(m_stages.Size());
        for (const CompiledStage& compiled : m_stages)
        {
            if (compiled.bytecodeOffset > m_bytecode.Size() || compiled.bytecodeSize > m_bytecode.Size() - compiled.bytecodeOffset)
                return Result::InvalidState;
            stages.PushBack({compiled.stage, compiled.format, compiled.entryPoint, static_cast<const u8*>(m_bytecode.Data()) + compiled.bytecodeOffset,
                             static_cast<usize>(compiled.bytecodeSize), compiled.entryPointName});
        }
        shaders::ProgramKind kind = shaders::ProgramKind::Graphics;
        if (m_interface.stages == shaders::StageBit(shaders::ShaderStage::Compute))
            kind = shaders::ProgramKind::Compute;
        else if (m_interface.stages == shaders::StageBit(shaders::ShaderStage::Library))
            kind = shaders::ProgramKind::Library;
        shaders::BuildDescription description;
        description.kind = kind;
        description.program = program;
        description.permutation = permutation;
        description.compilerFingerprint = m_compilerFingerprint;
        description.pipelineInterface = m_interface;
        description.stages = stages;
        description.bindings = m_bindings;
        description.constantBuffers = m_constantBuffers;
        description.constantMembers = m_constantMembers;
        description.vertexInputs = m_vertexInputs;
        description.fragmentOutputs = m_fragmentOutputs;
        description.specializationConstants = m_specializationConstants;
        shaders::MaterialContractBuildDescription materialContract;
        if (m_hasMaterialContract)
        {
            materialContract.domain = m_materialDomain;
            materialContract.accessorAbiVersion = m_materialAccessorAbiVersion;
            materialContract.parameterByteSize = m_materialParameterByteSize;
            materialContract.parameters = m_materialParameters;
            materialContract.resources = m_materialResources;
            description.materialContract = &materialContract;
        }
        return shaders::WriteShader(writer, description) == shaders::Result::Success ? Result::Success : Result::WriteFailure;
    }

    void CompileOutput::AppendDiagnosticBytes(const void* const data, const usize size) noexcept
    {
        if (data == nullptr || size == 0)
            return;
        const u8* const bytes = static_cast<const u8*>(data);
        const usize available = MaximumDiagnosticBytes > m_diagnostics.Size() ? MaximumDiagnosticBytes - m_diagnostics.Size() : 0;
        const usize count = size < available ? size : available;
        m_diagnostics.Reserve(m_diagnostics.Size() + static_cast<u32>(count) + 2u);
        for (usize index = 0; index < count; ++index)
            if (bytes[index] != 0)
                m_diagnostics.PushBack(static_cast<char>(bytes[index]));
        if (!m_diagnostics.Empty() && m_diagnostics[m_diagnostics.Size() - 1u] != '\n')
            m_diagnostics.PushBack('\n');
    }

    void CompileOutput::TerminateDiagnostics() noexcept
    {
        if (m_diagnostics.Empty() || m_diagnostics[m_diagnostics.Size() - 1u] != '\0')
            m_diagnostics.PushBack('\0');
    }

    bool CompileRequest::IsValid() const noexcept
    {
        if (BoundedLength(sourceName, MaximumSourceNameLength) >= MaximumSourceNameLength ||
            BoundedLength(moduleName, MaximumModuleNameLength) >= MaximumModuleNameLength || source.Empty() || source.Data() == nullptr ||
            entryPoints.Empty() || entryPoints.Size() > MaximumEntryPoints || settings.target > Target::VulkanSpirV ||
            settings.optimization > Optimization::Maximum || settings.debugInformation > DebugInformation::Maximum ||
            BoundedLength(settings.profile, 64) >= 64 || (searchPathCount != 0 && searchPaths == nullptr) ||
            (loadSource == nullptr && loadSourceUserData != nullptr))
            return false;
        for (u32 index = 0; index < source.Size(); ++index)
            if (source[index] == 0)
                return false;
        shader::StageMask stages = 0;
        for (const EntryPoint& entry : entryPoints)
        {
            if (BoundedLength(entry.name, shader::MaximumEntryPointLength) >= shader::MaximumEntryPointLength ||
                ConvertStage(entry.stage) == SLANG_STAGE_NONE || (stages & shader::StageBit(entry.stage)) != 0)
                return false;
            stages |= shader::StageBit(entry.stage);
        }
        for (const Define& define : defines)
            if (BoundedLength(define.name, 256) >= 256 || (define.value != nullptr && BoundedLength(define.value, 1024) >= 1024))
                return false;
        return true;
    }

    ShaderCompiler::~ShaderCompiler()
    {
        Shutdown();
    }

    Result ShaderCompiler::Initialize() noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return Result::CompilerUnavailable;
        const SlangResult result = slang::createGlobalSession(implementation->globalSession.writeRef());
        if (SLANG_FAILED(result) || !implementation->globalSession)
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        const char* const version = implementation->globalSession->getBuildTagString();
        if (!CopyString(implementation->compilerVersion, version))
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        crypto::Sha256Builder fingerprint;
        constexpr char Policy[] = "VanguardShaderCompiler;Slang;Language2026;RowMajor;ExplicitEntryPoints;Reflection3";
        if (!fingerprint.Update(implementation->compilerVersion, BoundedLength(implementation->compilerVersion, 64)) ||
            !fingerprint.Update(Policy, sizeof(Policy) - 1u) || !fingerprint.Finalize(implementation->compilerFingerprint))
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        m_impl = implementation;
        return Result::Success;
    }

    void ShaderCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
    }

    bool ShaderCompiler::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }
    const crypto::Digest256& ShaderCompiler::CompilerFingerprint() const noexcept
    {
        static const crypto::Digest256 Empty;
        return m_impl != nullptr ? m_impl->compilerFingerprint : Empty;
    }

    Result ShaderCompiler::Compile(const CompileRequest& request, CompileOutput& output) noexcept
    {
        return Process(request, output, true);
    }

    Result ShaderCompiler::Reflect(const CompileRequest& request, CompileOutput& output) noexcept
    {
        return Process(request, output, false);
    }

    Result ShaderCompiler::Process(const CompileRequest& request, CompileOutput& output, const bool generateNativeCode) noexcept
    {
        output.Reset();
        if (m_impl == nullptr)
            return Result::InvalidState;
        if (!request.IsValid())
            return Result::InvalidArgument;

        concurrency::ScopedLock lock(m_impl->frontEndLock);
        const SlangCompileTarget targetFormat = request.settings.target == Target::D3D12Dxil ? SLANG_DXIL : SLANG_SPIRV;
        if (SLANG_FAILED(m_impl->globalSession->checkCompileTargetSupport(targetFormat)))
            return Result::UnsupportedTarget;

        slang::CompilerOptionEntry targetOptions[2]{};
        targetOptions[0].name = slang::CompilerOptionName::Optimization;
        targetOptions[0].value.intValue0 = static_cast<i32>(ConvertOptimization(request.settings.optimization));
        targetOptions[1].name = slang::CompilerOptionName::DebugInformation;
        targetOptions[1].value.intValue0 = static_cast<i32>(ConvertDebugInformation(request.settings.debugInformation));

        slang::TargetDesc target;
        target.format = targetFormat;
        target.profile = m_impl->globalSession->findProfile(request.settings.profile);
        target.floatingPointMode = request.settings.preciseFloatingPoint ? SLANG_FLOATING_POINT_MODE_PRECISE : SLANG_FLOATING_POINT_MODE_FAST;
        target.compilerOptionEntries = targetOptions;
        target.compilerOptionEntryCount = 2;
        if (target.profile == SLANG_PROFILE_UNKNOWN)
            return Result::UnsupportedTarget;

        containers::DynamicArray<slang::PreprocessorMacroDesc> macros(memory::pools::Tools::GetInstance());
        macros.Reserve(request.defines.Size());
        for (const Define& define : request.defines)
            macros.PushBack({define.name, define.value != nullptr ? define.value : "1"});

        slang::CompilerOptionEntry sessionOptions[3]{};
        sessionOptions[0].name = slang::CompilerOptionName::LanguageVersion;
        sessionOptions[0].value.intValue0 = SLANG_LANGUAGE_VERSION_2026;
        sessionOptions[1].name = slang::CompilerOptionName::DiagnosticColor;
        sessionOptions[1].value.intValue0 = SLANG_DIAGNOSTIC_COLOR_NEVER;
        sessionOptions[2].name = slang::CompilerOptionName::WarningsAsErrors;
        sessionOptions[2].value.kind = slang::CompilerOptionValueKind::String;
        sessionOptions[2].value.stringValue0 = request.settings.warningsAsErrors ? "all" : "none";

        slang::SessionDesc sessionDescription;
        sessionDescription.targets = &target;
        sessionDescription.targetCount = 1;
        sessionDescription.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDescription.searchPaths = request.searchPaths;
        sessionDescription.searchPathCount = request.searchPathCount;
        sessionDescription.preprocessorMacros = macros.Empty() ? nullptr : static_cast<const slang::PreprocessorMacroDesc*>(macros.Data());
        sessionDescription.preprocessorMacroCount = macros.Size();
        sessionDescription.compilerOptionEntries = sessionOptions;
        sessionDescription.compilerOptionEntryCount = request.settings.warningsAsErrors ? 3u : 2u;
        SourceFileSystem sourceFileSystem(request.loadSource, request.loadSourceUserData);
        if (request.loadSource != nullptr)
            sessionDescription.fileSystem = &sourceFileSystem;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(m_impl->globalSession->createSession(sessionDescription, session.writeRef())) || !session)
            return Result::SessionCreationFailure;

        containers::DynamicArray<char> source(memory::pools::Tools::GetInstance());
        source.Reserve(request.source.Size() + 1u);
        for (const u8 value : request.source)
            source.PushBack(static_cast<char>(value));
        source.PushBack('\0');

        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModuleFromSourceString(request.moduleName, request.sourceName, static_cast<const char*>(source.Data()), diagnostics.writeRef());
        if (diagnostics)
            output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (!module)
        {
            output.TerminateDiagnostics();
            return Result::SourceFailure;
        }

        const SlangInt32 dependencyCount = module->getDependencyFileCount();
        output.m_dependencies.Reserve(dependencyCount > 0 ? static_cast<u32>(dependencyCount) : 0u);
        for (SlangInt32 index = 0; index < dependencyCount; ++index)
        {
            SourceDependency dependency;
            if (!CopyString(dependency.path, module->getDependencyFilePath(index)))
            {
                output.TerminateDiagnostics();
                return Result::LimitExceeded;
            }
            output.m_dependencies.PushBack(dependency);
        }

        Slang::ComPtr<slang::IEntryPoint> entryPoints[MaximumEntryPoints];
        slang::IComponentType* components[MaximumEntryPoints + 1u]{};
        components[0] = module.get();
        for (u32 index = 0; index < request.entryPoints.Size(); ++index)
        {
            diagnostics.setNull();
            const EntryPoint& entry = request.entryPoints[index];
            const SlangResult entryResult =
                module->findAndCheckEntryPoint(entry.name, ConvertStage(entry.stage), entryPoints[index].writeRef(), diagnostics.writeRef());
            if (diagnostics)
                output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
            if (SLANG_FAILED(entryResult) || !entryPoints[index])
            {
                output.TerminateDiagnostics();
                return Result::EntryPointFailure;
            }
            components[index + 1u] = entryPoints[index].get();
        }

        Slang::ComPtr<slang::IComponentType> composed;
        diagnostics.setNull();
        SlangResult slangResult =
            session->createCompositeComponentType(components, request.entryPoints.Size() + 1u, composed.writeRef(), diagnostics.writeRef());
        if (diagnostics)
            output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (SLANG_FAILED(slangResult) || !composed)
        {
            output.TerminateDiagnostics();
            return Result::LinkFailure;
        }

        Slang::ComPtr<slang::IComponentType> linked;
        diagnostics.setNull();
        slangResult = composed->link(linked.writeRef(), diagnostics.writeRef());
        if (diagnostics)
            output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (SLANG_FAILED(slangResult) || !linked)
        {
            output.TerminateDiagnostics();
            return Result::LinkFailure;
        }

        diagnostics.setNull();
        slang::ProgramLayout* const programLayout = linked->getLayout(0, diagnostics.writeRef());
        if (diagnostics)
            output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (programLayout == nullptr ||
            !ExtractReflection(programLayout, request.entryPoints, output.m_interface, output.m_bindings, output.m_constantBuffers, output.m_constantMembers,
                               output.m_vertexInputs, output.m_fragmentOutputs, output.m_specializationConstants) ||
            !ExtractMaterialContract(programLayout, request.entryPoints, output.m_hasMaterialContract, output.m_materialDomain,
                                     output.m_materialAccessorAbiVersion, output.m_materialParameterByteSize, output.m_materialParameters,
                                     output.m_materialResources))
        {
            output.TerminateDiagnostics();
            return Result::ReflectionFailure;
        }

        if (generateNativeCode)
        {
            output.m_stages.Reserve(request.entryPoints.Size());
            for (u32 index = 0; index < request.entryPoints.Size(); ++index)
            {
                Slang::ComPtr<slang::IBlob> code;
                diagnostics.setNull();
                slangResult = linked->getEntryPointCode(index, 0, code.writeRef(), diagnostics.writeRef());
                if (diagnostics)
                    output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
                if (SLANG_FAILED(slangResult) || !code || code->getBufferPointer() == nullptr || code->getBufferSize() == 0 ||
                    code->getBufferSize() > MaximumCompiledBytes || output.m_bytecode.Size() > MaximumCompiledBytes - code->getBufferSize())
                {
                    output.TerminateDiagnostics();
                    return Result::CodeGenerationFailure;
                }

                CompiledStage stage;
                stage.stage = request.entryPoints[index].stage;
                stage.format = request.settings.target == Target::D3D12Dxil ? shader::NativeFormat::Dxil : shader::NativeFormat::SpirV;
                stage.entryPoint = shader::HashInterfaceName(request.entryPoints[index].name);
                if (!CopyString(stage.entryPointName, request.entryPoints[index].name))
                {
                    output.TerminateDiagnostics();
                    return Result::LimitExceeded;
                }
                stage.bytecodeOffset = output.m_bytecode.Size();
                stage.bytecodeSize = code->getBufferSize();
                stage.bytecodeDigest = crypto::Sha256(code->getBufferPointer(), code->getBufferSize());
                const u8* const codeBytes = static_cast<const u8*>(code->getBufferPointer());
                output.m_bytecode.Reserve(output.m_bytecode.Size() + static_cast<u32>(code->getBufferSize()));
                for (usize byteIndex = 0; byteIndex < code->getBufferSize(); ++byteIndex)
                    output.m_bytecode.PushBack(codeBytes[byteIndex]);
                output.m_stages.PushBack(stage);
            }
        }

        if (!CopyString(output.m_compilerVersion, m_impl->compilerVersion))
        {
            output.TerminateDiagnostics();
            return Result::CompilerUnavailable;
        }
        output.m_compilerFingerprint = m_impl->compilerFingerprint;
        output.TerminateDiagnostics();
        return Result::Success;
    }

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
        case Result::CompilerUnavailable:
            return "CompilerUnavailable";
        case Result::UnsupportedTarget:
            return "UnsupportedTarget";
        case Result::SessionCreationFailure:
            return "SessionCreationFailure";
        case Result::SourceFailure:
            return "SourceFailure";
        case Result::EntryPointFailure:
            return "EntryPointFailure";
        case Result::LinkFailure:
            return "LinkFailure";
        case Result::ReflectionFailure:
            return "ReflectionFailure";
        case Result::CodeGenerationFailure:
            return "CodeGenerationFailure";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::WriteFailure:
            return "WriteFailure";
        }
        return "Unknown";
    }
} // namespace vanguard::shader_tools
