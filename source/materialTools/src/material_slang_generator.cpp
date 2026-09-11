#include <vanguard/material_tools/material_slang_generator.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
    namespace mt = vanguard::material_tools;
    using namespace vanguard;

    constexpr char OffsetMarker[] = "/*VANGUARD_MATERIAL_OFFSET:";
    constexpr char MatrixStrideMarker[] = "/*VANGUARD_MATERIAL_MATRIX_STRIDE:";
    constexpr char ArrayStrideMarker[] = "/*VANGUARD_MATERIAL_ARRAY_STRIDE:";

    class SourceWriter final
    {
    public:
        SourceWriter(containers::DynamicArray<u8>& bytes, const u32 limit) noexcept : m_bytes(bytes), m_limit(limit) {}

        [[nodiscard]] bool Text(const char* const value) noexcept
        {
            return value != nullptr && Bytes(value, static_cast<u32>(std::strlen(value)));
        }

        [[nodiscard]] bool Bytes(const void* const data, const u32 size) noexcept
        {
            if (data == nullptr || m_bytes.Size() > m_limit || size > m_limit - m_bytes.Size())
                return false;
            const auto* const source = static_cast<const u8*>(data);
            for (u32 index = 0; index < size; ++index)
            {
                m_bytes.PushBack(source[index]);
                if (source[index] == '\n')
                    ++m_line;
            }
            return true;
        }

        [[nodiscard]] u32 Line() const noexcept { return m_line; }

        [[nodiscard]] bool U32(const u32 value) noexcept
        {
            char text[16]{};
            const int count = std::snprintf(text, sizeof(text), "%u", value);
            return count > 0 && Bytes(text, static_cast<u32>(count));
        }

        [[nodiscard]] bool Hex32(const u32 value) noexcept
        {
            char text[9]{};
            const int count = std::snprintf(text, sizeof(text), "%08x", value);
            return count == 8 && Bytes(text, 8);
        }

        [[nodiscard]] bool Hex64(const u64 value) noexcept
        {
            char text[17]{};
            const int count = std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
            return count == 16 && Bytes(text, 16);
        }

    private:
        containers::DynamicArray<u8>& m_bytes;
        u32 m_limit = 0;
        u32 m_line = 1;
    };

    [[nodiscard]] bool Identifier(const char* const value) noexcept
    {
        if (value == nullptr || !(value[0] == '_' || (value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')))
            return false;
        for (u32 index = 1; value[index] != '\0'; ++index)
            if (!(value[index] == '_' || (value[index] >= 'A' && value[index] <= 'Z') || (value[index] >= 'a' && value[index] <= 'z') ||
                  (value[index] >= '0' && value[index] <= '9')))
                return false;
        return true;
    }

    [[nodiscard]] const mt::MaterialSlangSymbol* FindSymbol(const containers::ArraySpan<const mt::MaterialSlangSymbol> symbols,
                                                            const u64 semantic) noexcept
    {
        for (const mt::MaterialSlangSymbol& symbol : symbols)
            if (symbol.semantic == semantic)
                return &symbol;
        return nullptr;
    }

    [[nodiscard]] const char* ScalarName(const shaders::ScalarType type) noexcept
    {
        switch (type)
        {
        case shaders::ScalarType::Bool: return "bool";
        case shaders::ScalarType::I16: return "int16_t";
        case shaders::ScalarType::U16: return "uint16_t";
        case shaders::ScalarType::F16: return "float16_t";
        case shaders::ScalarType::I32: return "int";
        case shaders::ScalarType::U32: return "uint";
        case shaders::ScalarType::F32: return "float";
        case shaders::ScalarType::I64: return "int64_t";
        case shaders::ScalarType::U64: return "uint64_t";
        case shaders::ScalarType::F64: return "double";
        }
        return nullptr;
    }

    [[nodiscard]] constexpr u32 ScalarByteSize(const shaders::ScalarType type) noexcept
    {
        switch (type)
        {
        case shaders::ScalarType::Bool: return 1;
        case shaders::ScalarType::I16:
        case shaders::ScalarType::U16:
        case shaders::ScalarType::F16: return 2;
        case shaders::ScalarType::I32:
        case shaders::ScalarType::U32:
        case shaders::ScalarType::F32: return 4;
        case shaders::ScalarType::I64:
        case shaders::ScalarType::U64:
        case shaders::ScalarType::F64: return 8;
        }
        return 0;
    }

    [[nodiscard]] bool SupportedType(const mt::MaterialIrType& type) noexcept
    {
        if (type.kind == mt::MaterialIrTypeKind::Aggregate)
            return true;
        if (type.kind == mt::MaterialIrTypeKind::Texture)
            return (type.scalarType == shaders::ScalarType::I32 || type.scalarType == shaders::ScalarType::U32 ||
                    type.scalarType == shaders::ScalarType::F32) &&
                   type.rows >= 1 && type.rows <= 4 && type.columns == 1 &&
                   (type.textureDimension == mt::MaterialIrTextureDimension::D1 || type.textureDimension == mt::MaterialIrTextureDimension::D2 ||
                    type.textureDimension == mt::MaterialIrTextureDimension::D3 || type.textureDimension == mt::MaterialIrTextureDimension::Cube);
        if (type.kind == mt::MaterialIrTypeKind::Sampler)
            return true;
        if (type.kind == mt::MaterialIrTypeKind::AccelerationStructure)
            return true;
        if (type.kind == mt::MaterialIrTypeKind::Buffer)
            return type.bufferKind == mt::MaterialIrBufferKind::ByteAddress || ScalarName(type.scalarType) != nullptr || !type.aggregate.IsEmpty();
        return type.kind == mt::MaterialIrTypeKind::Numeric && type.rows >= 1 && type.rows <= 4 && type.columns >= 1 && type.columns <= 4 &&
               (type.columns == 1 || type.rows > 1) && ScalarName(type.scalarType) != nullptr &&
               ((type.rows > 1 && type.columns > 1) == (type.matrixOrder != mt::MaterialIrMatrixOrder::None));
    }

    [[nodiscard]] bool IsResourceType(const mt::MaterialIrType& type) noexcept
    {
        return type.kind == mt::MaterialIrTypeKind::Texture || type.kind == mt::MaterialIrTypeKind::Sampler ||
               type.kind == mt::MaterialIrTypeKind::Buffer || type.kind == mt::MaterialIrTypeKind::AccelerationStructure;
    }

    [[nodiscard]] bool WriteAggregateTypeName(SourceWriter& writer, const mt::MaterialIrTypeId type) noexcept
    {
        return writer.Text("VanguardMaterialType_") && writer.U32(type);
    }

    [[nodiscard]] bool WriteArrayTypeName(SourceWriter& writer, const mt::MaterialIrTypeId type) noexcept
    {
        return writer.Text("VanguardMaterialArray_") && writer.U32(type);
    }

    [[nodiscard]] mt::MaterialIrTypeId FindType(const mt::MaterialIrModule& module, const mt::MaterialIrType& type) noexcept
    {
        const auto types = module.GetTypes();
        for (u32 typeId = 0; typeId < types.Size(); ++typeId)
            if (types[typeId].type == type)
                return typeId;
        return mt::InvalidMaterialIrType;
    }

    [[nodiscard]] mt::MaterialIrTypeId ArrayElementType(const mt::MaterialIrModule& module, mt::MaterialIrType type) noexcept
    {
        type.arrayCount = 1;
        return FindType(module, type);
    }

    [[nodiscard]] bool WriteType(SourceWriter& writer, const mt::MaterialIrModule& module,
                                 mt::MaterialIrTypeId typeId) noexcept;

    [[nodiscard]] bool WriteResourceElementType(SourceWriter& writer, const mt::MaterialIrModule& module,
                                                const mt::MaterialIrType& type) noexcept
    {
        if (type.kind == mt::MaterialIrTypeKind::Sampler)
            return writer.Text(type.samplerKind == mt::MaterialIrSamplerKind::Comparison ? "SamplerComparisonState" : "SamplerState");
        if (type.kind == mt::MaterialIrTypeKind::AccelerationStructure)
            return writer.Text("RaytracingAccelerationStructure");
        if (type.kind == mt::MaterialIrTypeKind::Buffer)
        {
            const bool writable = type.resourceAccess != mt::MaterialIrResourceAccess::Read;
            if (type.bufferKind == mt::MaterialIrBufferKind::ByteAddress)
                return writer.Text(writable ? "RWByteAddressBuffer" : "ByteAddressBuffer");
            if (type.bufferKind == mt::MaterialIrBufferKind::Typed)
            {
                if (!writer.Text(writable ? "RWBuffer<" : "Buffer<")) return false;
            }
            else if (type.bufferKind == mt::MaterialIrBufferKind::Structured)
            {
                if (!writer.Text(writable ? "RWStructuredBuffer<" : "StructuredBuffer<")) return false;
            }
            else return false;
            if (!type.aggregate.IsEmpty())
            {
                mt::MaterialIrType aggregateType;
                aggregateType.kind = mt::MaterialIrTypeKind::Aggregate;
                aggregateType.aggregate = type.aggregate;
                const mt::MaterialIrTypeId aggregateId = FindType(module, aggregateType);
                if (aggregateId == mt::InvalidMaterialIrType || !WriteType(writer, module, aggregateId)) return false;
            }
            else
            {
                const char* const scalar = ScalarName(type.scalarType);
                if (scalar == nullptr || !writer.Text(scalar) || (type.rows > 1 && !writer.U32(type.rows))) return false;
            }
            return writer.Text(">");
        }
        if (type.kind != mt::MaterialIrTypeKind::Texture)
            return false;
        switch (type.textureDimension)
        {
        case mt::MaterialIrTextureDimension::D1: if (!writer.Text("Texture1D")) return false; break;
        case mt::MaterialIrTextureDimension::D2: if (!writer.Text("Texture2D")) return false; break;
        case mt::MaterialIrTextureDimension::D3: if (!writer.Text("Texture3D")) return false; break;
        case mt::MaterialIrTextureDimension::Cube: if (!writer.Text("TextureCube")) return false; break;
        case mt::MaterialIrTextureDimension::None: return false;
        }
        if (type.textureMultisampled && !writer.Text("MS")) return false;
        if (type.textureArrayed && !writer.Text("Array")) return false;
        const char* const scalar = ScalarName(type.scalarType);
        if (scalar == nullptr || !writer.Text("<") || !writer.Text(scalar)) return false;
        if (type.rows > 1 && !writer.U32(type.rows)) return false;
        return writer.Text(">");
    }

    [[nodiscard]] bool WriteType(SourceWriter& writer, const mt::MaterialIrModule& module, const mt::MaterialIrTypeId typeId) noexcept
    {
        if (typeId >= module.GetTypes().Size())
            return false;
        const mt::MaterialIrType& type = module.GetTypes()[typeId].type;
        if (type.arrayCount > 1)
            return WriteArrayTypeName(writer, typeId);
        if (type.kind == mt::MaterialIrTypeKind::Aggregate)
            return WriteAggregateTypeName(writer, typeId);
        if (IsResourceType(type))
            return writer.Text("uint");
        const char* const scalar = ScalarName(type.scalarType);
        if (scalar == nullptr || !writer.Text(scalar))
            return false;
        if (type.rows > 1 && !writer.U32(type.rows))
            return false;
        return type.columns == 1 || (writer.Text("x") && writer.U32(type.columns));
    }

    [[nodiscard]] bool WriteMatrixQualifier(SourceWriter& writer, const mt::MaterialIrType& type) noexcept
    {
        if (type.arrayCount > 1 || type.rows <= 1 || type.columns <= 1)
            return true;
        return writer.Text(type.matrixOrder == mt::MaterialIrMatrixOrder::RowMajor ? "row_major " : "column_major ");
    }

    [[nodiscard]] bool WriteParameterFieldName(SourceWriter& writer, const u64 semantic) noexcept
    {
        return writer.Text("p_") && writer.Hex64(semantic);
    }

    [[nodiscard]] bool WriteAggregateFieldName(SourceWriter& writer, const u64 name) noexcept
    {
        return writer.Text("f_") && writer.Hex64(name);
    }

    [[nodiscard]] bool WriteResourceFieldName(SourceWriter& writer, const u64 semantic) noexcept
    {
        return writer.Text("r_") && writer.Hex64(semantic);
    }

    [[nodiscard]] bool WriteValueName(SourceWriter& writer, const u32 value) noexcept
    {
        return writer.Text("v_") && writer.U32(value);
    }

    [[nodiscard]] u32 ReadU32(const u8* const bytes) noexcept
    {
        return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) |
               (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
    }

    [[nodiscard]] u16 ReadU16(const u8* const bytes) noexcept
    {
        return static_cast<u16>(bytes[0]) | static_cast<u16>(static_cast<u16>(bytes[1]) << 8u);
    }

    [[nodiscard]] u64 ReadU64(const u8* const bytes) noexcept
    {
        return static_cast<u64>(ReadU32(bytes)) | (static_cast<u64>(ReadU32(bytes + 4)) << 32u);
    }

    [[nodiscard]] bool WriteScalarLiteral(SourceWriter& writer, const shaders::ScalarType type, const u8* const bytes) noexcept
    {
        if (type == shaders::ScalarType::Bool)
            return writer.Text(bytes[0] != 0 ? "true" : "false");
        if (type == shaders::ScalarType::I16)
            return writer.Text("int16_t(") && writer.U32(ReadU16(bytes)) && writer.Text("u)");
        if (type == shaders::ScalarType::U16)
            return writer.Text("uint16_t(") && writer.U32(ReadU16(bytes)) && writer.Text("u)");
        if (type == shaders::ScalarType::F16)
            return writer.Text("float16_t(f16tof32(") && writer.U32(ReadU16(bytes)) && writer.Text("u))");
        if (type == shaders::ScalarType::I64)
            return writer.Text("int64_t(0x") && writer.Hex64(ReadU64(bytes)) && writer.Text("ull)");
        if (type == shaders::ScalarType::U64)
            return writer.Text("0x") && writer.Hex64(ReadU64(bytes)) && writer.Text("ull");
        if (type == shaders::ScalarType::F64)
            return writer.Text("asdouble(0x") && writer.Hex32(ReadU32(bytes)) && writer.Text("u, 0x") &&
                   writer.Hex32(ReadU32(bytes + 4)) && writer.Text("u)");
        const u32 bits = ReadU32(bytes);
        if (type == shaders::ScalarType::U32)
            return writer.Text("0x") && writer.Hex32(bits) && writer.Text("u");
        if (type == shaders::ScalarType::I32)
            return writer.Text("asint(0x") && writer.Hex32(bits) && writer.Text("u)");
        return writer.Text("asfloat(0x") && writer.Hex32(bits) && writer.Text("u)");
    }

    [[nodiscard]] bool WriteArrayConstructorName(SourceWriter& writer, const mt::MaterialIrTypeId type) noexcept
    {
        return writer.Text("VanguardMaterialConstructArray_") && writer.U32(type);
    }

    [[nodiscard]] bool WriteNumericLiteral(SourceWriter& writer, const mt::MaterialIrModule& module, const mt::MaterialIrTypeId typeId,
                                           const u8* const bytes, const u32 byteCount) noexcept
    {
        if (typeId >= module.GetTypes().Size())
            return false;
        const mt::MaterialIrType& type = module.GetTypes()[typeId].type;
        if (type.kind != mt::MaterialIrTypeKind::Numeric)
            return false;
        const u32 scalarBytes = ScalarByteSize(type.scalarType);
        const u32 elementComponents = static_cast<u32>(type.rows) * type.columns;
        const u32 elementBytes = scalarBytes * elementComponents;
        if (byteCount != elementBytes * type.arrayCount)
            return false;
        if (type.arrayCount > 1)
        {
            const mt::MaterialIrTypeId elementType = ArrayElementType(module, type);
            if (elementType == mt::InvalidMaterialIrType || !WriteArrayConstructorName(writer, typeId) || !writer.Text("("))
                return false;
            for (u32 index = 0; index < type.arrayCount; ++index)
                if ((index != 0 && !writer.Text(", ")) || !WriteNumericLiteral(writer, module, elementType, bytes + index * elementBytes, elementBytes))
                    return false;
            return writer.Text(")");
        }
        const u32 componentCount = elementComponents;
        if (componentCount > 1 && (!WriteType(writer, module, typeId) || !writer.Text("(")))
            return false;
        for (u32 component = 0; component < componentCount; ++component)
        {
            if (component != 0 && !writer.Text(", "))
                return false;
            if (!WriteScalarLiteral(writer, type.scalarType, bytes + component * scalarBytes))
                return false;
        }
        return componentCount == 1 || writer.Text(")");
    }

    [[nodiscard]] bool WriteLiteral(SourceWriter& writer, const mt::MaterialIrModule& module, const mt::MaterialIrValue& value,
                                    const containers::ArraySpan<const u8> data) noexcept
    {
        if (value.dataOffset > data.Size() || value.dataSize > data.Size() - value.dataOffset)
            return false;
        return WriteNumericLiteral(writer, module, value.typeId, data.Data() + value.dataOffset, value.dataSize);
    }

    [[nodiscard]] bool WriteParameterWord(SourceWriter& writer, const u64 memberName, const mt::MaterialIrType& valueType,
                                          const u32 row, const u32 column, const u32 arrayIndex, const u32 wordOffset) noexcept
    {
        if (!writer.Text("VanguardLoadMaterialParameterWord(") || !writer.Text(OffsetMarker) ||
            !writer.Hex64(memberName) || !writer.Text("*/0u"))
            return false;
        if (arrayIndex != 0 && (!writer.Text(" + ") || !writer.U32(arrayIndex) || !writer.Text("u * ") ||
                                !writer.Text(ArrayStrideMarker) || !writer.Hex64(memberName) || !writer.Text("*/0u")))
            return false;
        if (valueType.rows > 1 && valueType.columns > 1)
        {
            const u32 major = valueType.matrixOrder == mt::MaterialIrMatrixOrder::RowMajor ? row : column;
            const u32 minor = valueType.matrixOrder == mt::MaterialIrMatrixOrder::RowMajor ? column : row;
            if (major != 0 && (!writer.Text(" + ") || !writer.U32(major) || !writer.Text("u * ") ||
                               !writer.Text(MatrixStrideMarker) || !writer.Hex64(memberName) || !writer.Text("*/0u")))
                return false;
            if (minor != 0 && (!writer.Text(" + ") || !writer.U32(minor * ScalarByteSize(valueType.scalarType)) || !writer.Text("u")))
                return false;
        }
        else if (row != 0 && (!writer.Text(" + ") || !writer.U32(row * ScalarByteSize(valueType.scalarType)) || !writer.Text("u")))
            return false;
        if (wordOffset != 0 && (!writer.Text(" + ") || !writer.U32(wordOffset) || !writer.Text("u")))
            return false;
        return writer.Text(")");
    }

    [[nodiscard]] bool WriteLoadComponent(SourceWriter& writer, const shaders::ScalarType type, const u64 memberName,
                                          const mt::MaterialIrType& valueType, const u32 row, const u32 column,
                                          const u32 arrayIndex = 0) noexcept
    {
        const auto word = [&writer, memberName, &valueType, row, column, arrayIndex](const u32 offset) noexcept
        { return WriteParameterWord(writer, memberName, valueType, row, column, arrayIndex, offset); };
        switch (type)
        {
        case shaders::ScalarType::Bool: return writer.Text("(") && word(0) && writer.Text(" != 0u)");
        case shaders::ScalarType::I16: return writer.Text("int16_t(") && word(0) && writer.Text(" & 0xffffu)");
        case shaders::ScalarType::U16: return writer.Text("uint16_t(") && word(0) && writer.Text(" & 0xffffu)");
        case shaders::ScalarType::F16: return writer.Text("float16_t(f16tof32(") && word(0) && writer.Text(" & 0xffffu))");
        case shaders::ScalarType::I32: return writer.Text("asint(") && word(0) && writer.Text(")");
        case shaders::ScalarType::U32: return word(0);
        case shaders::ScalarType::F32: return writer.Text("asfloat(") && word(0) && writer.Text(")");
        case shaders::ScalarType::I64:
            return writer.Text("int64_t(uint64_t(") && word(0) && writer.Text(") | (uint64_t(") && word(4) && writer.Text(") << 32u))");
        case shaders::ScalarType::U64:
            return writer.Text("uint64_t(") && word(0) && writer.Text(") | (uint64_t(") && word(4) && writer.Text(") << 32u)");
        case shaders::ScalarType::F64:
            return writer.Text("asdouble(") && word(0) && writer.Text(", ") && word(4) && writer.Text(")");
        }
        return false;
    }

    [[nodiscard]] bool WriteAggregateConstructorName(SourceWriter& writer, const mt::MaterialIrTypeId type) noexcept
    {
        return writer.Text("VanguardMaterialConstruct_") && writer.U32(type);
    }

    [[nodiscard]] bool GeneratedName(char* const output, const usize capacity, const char prefix, const u64 value) noexcept
    {
        const int length = std::snprintf(output, capacity, "%c_%016llx", prefix, static_cast<unsigned long long>(value));
        return length == 18;
    }

    [[nodiscard]] bool WriteReflectedValue(SourceWriter& writer, const mt::MaterialIrModule& module,
                                           const mt::MaterialIrTypeId typeId, const u64 memberName,
                                           const u32 arrayIndex = 0) noexcept
    {
        if (typeId >= module.GetTypes().Size())
            return false;
        const mt::MaterialIrTypeRecord& record = module.GetTypes()[typeId];
        if (record.type.arrayCount > 1)
        {
            const mt::MaterialIrTypeId elementType = ArrayElementType(module, record.type);
            if (elementType == mt::InvalidMaterialIrType || !WriteArrayConstructorName(writer, typeId) || !writer.Text("("))
                return false;
            const u64 elementName = shaders::HashInterfaceChildName(memberName, "values");
            for (u32 index = 0; index < record.type.arrayCount; ++index)
                if ((index != 0 && !writer.Text(", ")) || !WriteReflectedValue(writer, module, elementType, elementName, index))
                    return false;
            return writer.Text(")");
        }
        if (record.type.kind == mt::MaterialIrTypeKind::Aggregate)
        {
            if (!WriteAggregateConstructorName(writer, typeId) || !writer.Text("("))
                return false;
            const auto fields = module.GetTypeFields();
            if (record.firstField > fields.Size() || record.fieldCount > fields.Size() - record.firstField)
                return false;
            for (u32 index = 0; index < record.fieldCount; ++index)
            {
                const mt::MaterialIrTypeField& field = fields[record.firstField + index];
                char fieldName[32]{};
                if ((index != 0 && !writer.Text(", ")) || !GeneratedName(fieldName, sizeof(fieldName), 'f', field.name) ||
                    !WriteReflectedValue(writer, module, field.type, shaders::HashInterfaceChildName(memberName, fieldName), arrayIndex))
                    return false;
            }
            return writer.Text(")");
        }
        if (record.type.kind != mt::MaterialIrTypeKind::Numeric)
            return false;
        const u32 componentCount = static_cast<u32>(record.type.rows) * record.type.columns;
        if (componentCount > 1 && (!WriteType(writer, module, typeId) || !writer.Text("(")))
            return false;
        for (u32 row = 0; row < record.type.rows; ++row)
            for (u32 column = 0; column < record.type.columns; ++column)
            {
                if ((row != 0 || column != 0) && !writer.Text(", "))
                    return false;
                if (!WriteLoadComponent(writer, record.type.scalarType, memberName, record.type, row, column, arrayIndex))
                    return false;
            }
        return componentCount == 1 || writer.Text(")");
    }

    struct ResourceParameter
    {
        u32 value = 0;
        u32 firstSlot = 0;
    };

    [[nodiscard]] const ResourceParameter* FindResourceParameter(const containers::ArraySpan<const ResourceParameter> resources,
                                                                 const u32 value) noexcept
    {
        for (const ResourceParameter& resource : resources)
            if (resource.value == value)
                return &resource;
        return nullptr;
    }

    [[nodiscard]] bool WriteResourceElement(SourceWriter& writer, const mt::MaterialIrType& type, const u32 slot) noexcept
    {
        if (type.kind == mt::MaterialIrTypeKind::Texture || type.kind == mt::MaterialIrTypeKind::Buffer ||
            type.kind == mt::MaterialIrTypeKind::AccelerationStructure)
            return writer.Text("VanguardLoadMaterialResourceDescriptor(") && writer.U32(slot) && writer.Text("u)");
        if (type.kind == mt::MaterialIrTypeKind::Sampler)
            return writer.Text("VanguardLoadMaterialSamplerDescriptor(") && writer.U32(slot) && writer.Text("u)");
        return false;
    }

    [[nodiscard]] bool WriteResourceValue(SourceWriter& writer, const mt::MaterialIrModule& module,
                                          const mt::MaterialIrTypeId typeId, const u32 firstSlot) noexcept
    {
        if (typeId >= module.GetTypes().Size()) return false;
        const mt::MaterialIrType& type = module.GetTypes()[typeId].type;
        if (type.arrayCount == 1)
            return WriteResourceElement(writer, type, firstSlot);
        if (!WriteArrayConstructorName(writer, typeId) || !writer.Text("(")) return false;
        mt::MaterialIrType element = type;
        element.arrayCount = 1;
        for (u32 index = 0; index < type.arrayCount; ++index)
            if ((index != 0 && !writer.Text(", ")) || !WriteResourceElement(writer, element, firstSlot + index)) return false;
        return writer.Text(")");
    }

    [[nodiscard]] bool WriteResourceAccessor(SourceWriter& writer, const mt::MaterialIrModule& module,
                                             const mt::MaterialIrValue& value, const u32 firstSlot) noexcept
    {
        return WriteType(writer, module, value.typeId) && writer.Text(" VanguardMaterialLoad_") &&
               WriteParameterFieldName(writer, value.semantic) && writer.Text("()\n{\n    return ") &&
               WriteResourceValue(writer, module, value.typeId, firstSlot) && writer.Text(";\n}\n");
    }

    [[nodiscard]] bool WriteParameterAccessor(SourceWriter& writer, const mt::MaterialIrModule& module,
                                              const mt::MaterialIrValue& value, const char* const parameterTypeName) noexcept
    {
        if (!WriteType(writer, module, value.typeId) || !writer.Text(" VanguardMaterialLoad_") || !WriteParameterFieldName(writer, value.semantic) ||
            !writer.Text("()\n{\n    return "))
            return false;
        char fieldName[32]{};
        if (!GeneratedName(fieldName, sizeof(fieldName), 'p', value.semantic))
            return false;
        const u64 memberName = shaders::HashInterfaceChildName(shaders::HashInterfaceName(parameterTypeName), fieldName);
        return WriteReflectedValue(writer, module, value.typeId, memberName) && writer.Text(";\n}\n");
    }

    [[nodiscard]] bool WriteOperand(SourceWriter& writer, const mt::MaterialIrModule& module,
                                    const mt::MaterialIrValue& value, const u32 operand) noexcept
    {
        const containers::ArraySpan<const mt::MaterialIrValueId> operands = module.GetOperands();
        return operand < value.operandCount && value.firstOperand + operand < operands.Size() &&
               WriteValueName(writer, operands[value.firstOperand + operand]);
    }

    [[nodiscard]] bool WriteResolvedResourceName(SourceWriter& writer, const u32 value, const u32 operand) noexcept
    {
        return writer.Text("VanguardMaterialResolved_") && writer.U32(value) && writer.Text("_") && writer.U32(operand);
    }

    [[nodiscard]] bool WriteTextureSampleResources(SourceWriter& writer, const mt::MaterialIrModule& module,
                                                    const mt::MaterialIrValue& value, const u32 valueId) noexcept
    {
        const containers::ArraySpan<const mt::MaterialIrValueId> operands = module.GetOperands();
        if (value.operandCount != 3 || value.firstOperand > operands.Size() || value.operandCount > operands.Size() - value.firstOperand)
            return false;
        const mt::MaterialIrValueId textureId = operands[value.firstOperand];
        const mt::MaterialIrValueId samplerId = operands[value.firstOperand + 1u];
        if (textureId >= module.GetValues().Size() || samplerId >= module.GetValues().Size())
            return false;
        const mt::MaterialIrType& texture = module.GetValues()[textureId].type;
        const mt::MaterialIrType& sampler = module.GetValues()[samplerId].type;
        if (texture.kind != mt::MaterialIrTypeKind::Texture || texture.arrayCount != 1 || sampler.kind != mt::MaterialIrTypeKind::Sampler || sampler.arrayCount != 1)
            return false;
        return writer.Text("    ") && WriteResourceElementType(writer, module, texture) && writer.Text(" ") && WriteResolvedResourceName(writer, valueId, 0) &&
               writer.Text(" = ResourceDescriptorHeap[NonUniformResourceIndex(") && WriteValueName(writer, textureId) && writer.Text(")];\n    ") && WriteResourceElementType(writer, module, sampler) &&
               writer.Text(" ") && WriteResolvedResourceName(writer, valueId, 1) && writer.Text(" = SamplerDescriptorHeap[NonUniformResourceIndex(") && WriteValueName(writer, samplerId) && writer.Text(")];\n");
    }

    [[nodiscard]] bool WriteInstruction(SourceWriter& writer, const mt::MaterialIrModule& module, const mt::MaterialIrValue& value, const u32 valueId) noexcept
    {
        const auto binary = [&writer, &module, &value](const char* const operation) noexcept
        { return WriteOperand(writer, module, value, 0) && writer.Text(operation) && WriteOperand(writer, module, value, 1); };
        switch (value.opcode)
        {
        case mt::MaterialIrOpcode::Add:
            return binary(" + ");
        case mt::MaterialIrOpcode::Subtract:
            return binary(" - ");
        case mt::MaterialIrOpcode::Multiply:
            return binary(" * ");
        case mt::MaterialIrOpcode::Divide:
            return binary(" / ");
        case mt::MaterialIrOpcode::Select:
            return WriteOperand(writer, module, value, 0) && writer.Text(" ? ") && WriteOperand(writer, module, value, 1) &&
                   writer.Text(" : ") && WriteOperand(writer, module, value, 2);
        case mt::MaterialIrOpcode::Cast:
            return WriteType(writer, module, value.typeId) && writer.Text("(") && WriteOperand(writer, module, value, 0) && writer.Text(")");
        case mt::MaterialIrOpcode::Construct:
            if (value.type.arrayCount > 1)
            {
                if (!WriteArrayConstructorName(writer, value.typeId) || !writer.Text("(")) return false;
            }
            else if (value.type.kind == mt::MaterialIrTypeKind::Aggregate)
            {
                if (!WriteAggregateConstructorName(writer, value.typeId) || !writer.Text("(")) return false;
            }
            else if (!WriteType(writer, module, value.typeId) || !writer.Text("(")) return false;
            for (u32 operand = 0; operand < value.operandCount; ++operand)
                if ((operand != 0 && !writer.Text(", ")) || !WriteOperand(writer, module, value, operand)) return false;
            return writer.Text(")");
        case mt::MaterialIrOpcode::Extract:
        {
            const auto data = module.GetData();
            if (value.dataOffset > data.Size() || value.dataSize != 4 || value.dataSize > data.Size() - value.dataOffset) return false;
            const auto operands = module.GetOperands();
            if (value.firstOperand >= operands.Size() || operands[value.firstOperand] >= module.GetValues().Size()) return false;
            const mt::MaterialIrValue& source = module.GetValues()[operands[value.firstOperand]];
            const u32 index = ReadU32(data.Data() + value.dataOffset);
            if (!WriteOperand(writer, module, value, 0)) return false;
            if (source.type.arrayCount > 1)
                return writer.Text(".values[") && writer.U32(index) && writer.Text("]");
            if (source.type.kind == mt::MaterialIrTypeKind::Aggregate)
            {
                if (source.typeId >= module.GetTypes().Size()) return false;
                const mt::MaterialIrTypeRecord& record = module.GetTypes()[source.typeId];
                if (index >= record.fieldCount || record.firstField + index >= module.GetTypeFields().Size()) return false;
                return writer.Text(".") && WriteAggregateFieldName(writer, module.GetTypeFields()[record.firstField + index].name);
            }
            if (source.type.rows > 1 && source.type.columns > 1)
                return writer.Text("[") && writer.U32(index / source.type.columns) && writer.Text("][") &&
                       writer.U32(index % source.type.columns) && writer.Text("]");
            return writer.Text("[") && writer.U32(index) && writer.Text("]");
        }
        case mt::MaterialIrOpcode::TextureSample:
            return WriteResolvedResourceName(writer, valueId, 0) && writer.Text(".Sample(") &&
                   WriteResolvedResourceName(writer, valueId, 1) && writer.Text(", ") &&
                   WriteOperand(writer, module, value, 2) && writer.Text(")");
        case mt::MaterialIrOpcode::Compare:
        {
            const auto data = module.GetData();
            if (value.dataOffset >= data.Size() || value.dataSize != 1) return false;
            const char* operation = nullptr;
            switch (static_cast<mt::MaterialIrComparePredicate>(data[value.dataOffset]))
            {
            case mt::MaterialIrComparePredicate::Equal: operation = " == "; break;
            case mt::MaterialIrComparePredicate::NotEqual: operation = " != "; break;
            case mt::MaterialIrComparePredicate::Less: operation = " < "; break;
            case mt::MaterialIrComparePredicate::LessEqual: operation = " <= "; break;
            case mt::MaterialIrComparePredicate::Greater: operation = " > "; break;
            case mt::MaterialIrComparePredicate::GreaterEqual: operation = " >= "; break;
            }
            return operation != nullptr && binary(operation);
        }
        default: return false;
        }
    }

    [[nodiscard]] bool WriteTypeDeclarations(SourceWriter& writer, const mt::MaterialIrModule& module) noexcept
    {
        const auto types = module.GetTypes();
        const auto fields = module.GetTypeFields();
        containers::DynamicArray<u8> emitted{memory::pools::Tools::GetInstance()};
        emitted.Resize(types.Size());
        u32 remaining = 0;
        for (const mt::MaterialIrTypeRecord& record : types)
            if (record.type.arrayCount > 1 || record.type.kind == mt::MaterialIrTypeKind::Aggregate)
                ++remaining;
        while (remaining != 0)
        {
            bool progress = false;
            for (u32 typeId = 0; typeId < types.Size(); ++typeId)
            {
                const mt::MaterialIrTypeRecord& record = types[typeId];
                if ((record.type.arrayCount == 1 && record.type.kind != mt::MaterialIrTypeKind::Aggregate) || emitted[typeId] != 0 ||
                    record.firstField > fields.Size() || record.fieldCount > fields.Size() - record.firstField)
                    continue;
                bool ready = true;
                if (record.type.arrayCount > 1)
                {
                    const mt::MaterialIrTypeId elementType = ArrayElementType(module, record.type);
                    ready = elementType < types.Size() &&
                            ((types[elementType].type.arrayCount == 1 && types[elementType].type.kind != mt::MaterialIrTypeKind::Aggregate) || emitted[elementType] != 0);
                }
                for (u32 index = 0; ready && record.type.arrayCount == 1 && index < record.fieldCount; ++index)
                {
                    const mt::MaterialIrTypeId fieldType = fields[record.firstField + index].type;
                    ready = fieldType < types.Size() &&
                            ((types[fieldType].type.arrayCount == 1 && types[fieldType].type.kind != mt::MaterialIrTypeKind::Aggregate) || emitted[fieldType] != 0);
                }
                if (!ready)
                    continue;
                if (!writer.Text("struct ") ||
                    !(record.type.arrayCount > 1 ? WriteArrayTypeName(writer, typeId) : WriteAggregateTypeName(writer, typeId)) ||
                    !writer.Text("\n{\n"))
                    return false;
                if (record.type.arrayCount > 1)
                {
                    const mt::MaterialIrTypeId elementType = ArrayElementType(module, record.type);
                    if (!writer.Text("    ") || !WriteMatrixQualifier(writer, types[elementType].type) || !WriteType(writer, module, elementType) ||
                        !writer.Text(" values[") || !writer.U32(record.type.arrayCount) || !writer.Text("];\n"))
                        return false;
                }
                for (u32 index = 0; record.type.arrayCount == 1 && index < record.fieldCount; ++index)
                {
                    const mt::MaterialIrTypeField& field = fields[record.firstField + index];
                    if (!writer.Text("    ") || !WriteMatrixQualifier(writer, types[field.type].type) ||
                        !WriteType(writer, module, field.type) || !writer.Text(" ") || !WriteAggregateFieldName(writer, field.name) ||
                        !writer.Text(";\n"))
                        return false;
                }
                if (!writer.Text("};\n"))
                    return false;
                emitted[typeId] = 1;
                --remaining;
                progress = true;
            }
            if (!progress)
                return false;
        }

        for (u32 typeId = 0; typeId < types.Size(); ++typeId)
        {
            const mt::MaterialIrTypeRecord& record = types[typeId];
            if (record.type.arrayCount == 1 && record.type.kind != mt::MaterialIrTypeKind::Aggregate)
                continue;
            const u32 argumentCount = record.type.arrayCount > 1 ? record.type.arrayCount : record.fieldCount;
            if (!(record.type.arrayCount > 1 ? WriteArrayTypeName(writer, typeId) : WriteAggregateTypeName(writer, typeId)) || !writer.Text(" ") ||
                !(record.type.arrayCount > 1 ? WriteArrayConstructorName(writer, typeId) : WriteAggregateConstructorName(writer, typeId)) || !writer.Text("("))
                return false;
            const mt::MaterialIrTypeId elementType = record.type.arrayCount > 1 ? ArrayElementType(module, record.type) : mt::InvalidMaterialIrType;
            for (u32 index = 0; index < argumentCount; ++index)
            {
                const mt::MaterialIrTypeId argumentType = record.type.arrayCount > 1 ? elementType : fields[record.firstField + index].type;
                if ((index != 0 && !writer.Text(", ")) || !WriteType(writer, module, argumentType) ||
                    !writer.Text(" a_") || !writer.U32(index))
                    return false;
            }
            if (!writer.Text(")\n{\n    ") || !(record.type.arrayCount > 1 ? WriteArrayTypeName(writer, typeId) : WriteAggregateTypeName(writer, typeId)) || !writer.Text(" value;\n"))
                return false;
            for (u32 index = 0; index < argumentCount; ++index)
                if (!writer.Text("    value.") ||
                    !(record.type.arrayCount > 1 ? writer.Text("values[") && writer.U32(index) && writer.Text("]")
                                                 : WriteAggregateFieldName(writer, fields[record.firstField + index].name)) ||
                    !writer.Text(" = a_") || !writer.U32(index) || !writer.Text(";\n"))
                    return false;
            if (!writer.Text("    return value;\n}\n"))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool HexDigit(const u8 value, u64& digit) noexcept
    {
        if (value >= '0' && value <= '9') digit = value - '0';
        else if (value >= 'a' && value <= 'f') digit = value - 'a' + 10u;
        else if (value >= 'A' && value <= 'F') digit = value - 'A' + 10u;
        else return false;
        return true;
    }

    enum class ReflectedProperty : u8
    {
        None,
        Offset,
        MatrixStride,
        ArrayStride
    };

    [[nodiscard]] ReflectedProperty MarkerAt(const containers::ArraySpan<const u8> source, const u32 offset,
                                             u32& prefixSize) noexcept
    {
        constexpr u32 offsetSize = sizeof(OffsetMarker) - 1u;
        constexpr u32 matrixSize = sizeof(MatrixStrideMarker) - 1u;
        constexpr u32 arraySize = sizeof(ArrayStrideMarker) - 1u;
        if (offset + offsetSize <= source.Size() && std::memcmp(source.Data() + offset, OffsetMarker, offsetSize) == 0)
        {
            prefixSize = offsetSize;
            return ReflectedProperty::Offset;
        }
        if (offset + matrixSize <= source.Size() && std::memcmp(source.Data() + offset, MatrixStrideMarker, matrixSize) == 0)
        {
            prefixSize = matrixSize;
            return ReflectedProperty::MatrixStride;
        }
        if (offset + arraySize <= source.Size() && std::memcmp(source.Data() + offset, ArrayStrideMarker, arraySize) == 0)
        {
            prefixSize = arraySize;
            return ReflectedProperty::ArrayStride;
        }
        prefixSize = 0;
        return ReflectedProperty::None;
    }
} // namespace

namespace vanguard::material_tools
{
    MaterialSlangResult GenerateMaterialSlangProbe(const MaterialIrModule& module, const MaterialSlangDomain& domain,
                                                    containers::DynamicArray<u8>& source, const MaterialSlangLimits& limits,
                                                    containers::DynamicArray<MaterialGeneratedSourceRange>* const sourceRanges) noexcept
    {
        source.Clear();
        if (sourceRanges != nullptr)
            sourceRanges->Clear();
        if (limits.maximumSourceBytes == 0 || !Identifier(domain.stableName) || domain.schemaVersion == 0 || domain.legalStages == 0 ||
            !shaders::IsValidMaterialShaderCapabilityMask(domain.requiredCapabilities) ||
            !Identifier(domain.inputTypeName) || !Identifier(domain.outputTypeName) || !Identifier(domain.parameterTypeName) ||
            !Identifier(domain.resourceTypeName) || !Identifier(domain.evaluationFunctionName) || domain.accessorAbiVersion == 0 ||
            domain.prefix.Empty() || domain.suffix.Empty() || module.GetValues().Empty() || module.GetOutputs().Empty())
            return MaterialSlangResult::InvalidArgument;
        for (const MaterialSlangSymbol& symbol : domain.inputs) if (symbol.semantic == 0 || !Identifier(symbol.name)) return MaterialSlangResult::InvalidArgument;
        for (const MaterialSlangSymbol& symbol : domain.outputs) if (symbol.semantic == 0 || !Identifier(symbol.name)) return MaterialSlangResult::InvalidArgument;
        for (const MaterialIrTypeRecord& type : module.GetTypes())
            if (!SupportedType(type.type)) return MaterialSlangResult::UnsupportedType;
        for (const MaterialIrValue& value : module.GetValues())
            if (!SupportedType(value.type) || value.typeId >= module.GetTypes().Size()) return MaterialSlangResult::UnsupportedType;

        SourceWriter writer(source, limits.maximumSourceBytes);
        if (!writer.Text("[__AttributeUsage(_AttributeTargets.Function)]\nstruct VanguardMaterialDomainAttribute { string stableName; int schemaVersion; int legalStages; int requiredCapabilities; };\n") ||
            !writer.Text("[__AttributeUsage(_AttributeTargets.Function)]\nstruct VanguardMaterialProgramAttribute { string domainFunction; string parameterType; string resourceType; int accessorAbi; };\n") ||
            !writer.Text("[__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialParametersAttribute {};\n") ||
            !writer.Text("[__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialResourcesAttribute {};\n") ||
            !writer.Text("[__AttributeUsage(_AttributeTargets.Var)] struct VanguardMaterialResourceAttribute { int firstSlot; int required; };\n") ||
            !writer.Bytes(domain.prefix.Data(), domain.prefix.Size()) || !writer.Text("\n") || !WriteTypeDeclarations(writer, module) ||
            !writer.Text("[VanguardMaterialParameters]\nstruct ") ||
            !writer.Text(domain.parameterTypeName) || !writer.Text("\n{\n"))
            return MaterialSlangResult::LimitExceeded;

        containers::DynamicArray<ResourceParameter> resources{memory::pools::Tools::GetInstance()};
        for (u32 valueId = 0; valueId < module.GetValues().Size(); ++valueId)
        {
            const MaterialIrValue& value = module.GetValues()[valueId];
            if (value.kind == MaterialIrValueKind::DynamicParameter && IsResourceType(value.type))
                resources.PushBack({valueId, 0});
        }
        if (resources.Size() > 1)
            std::sort(resources.Begin(), resources.End(), [&module](const ResourceParameter& left, const ResourceParameter& right)
                      { return module.GetValues()[left.value].semantic < module.GetValues()[right.value].semantic; });
        u64 nextResourceSlot = 0;
        for (ResourceParameter& resource : resources)
        {
            resource.firstSlot = static_cast<u32>(nextResourceSlot);
            nextResourceSlot += module.GetValues()[resource.value].type.arrayCount;
            if (nextResourceSlot > 0x100000000ull)
                return MaterialSlangResult::LimitExceeded;
        }

        for (const MaterialIrValue& value : module.GetValues())
            if (value.kind == MaterialIrValueKind::DynamicParameter && !IsResourceType(value.type) &&
                (!writer.Text("    ") || !WriteMatrixQualifier(writer, value.type) || !WriteType(writer, module, value.typeId) ||
                 !writer.Text(" ") || !WriteParameterFieldName(writer, value.semantic) || !writer.Text(";\n")))
                return MaterialSlangResult::LimitExceeded;
        if (!writer.Text("};\n[VanguardMaterialResources]\nstruct ") || !writer.Text(domain.resourceTypeName) || !writer.Text("\n{\n"))
            return MaterialSlangResult::LimitExceeded;
        for (const ResourceParameter& resource : resources)
        {
            const MaterialIrValue& value = module.GetValues()[resource.value];
            mt::MaterialIrType elementType = value.type;
            elementType.arrayCount = 1;
            if (!writer.Text("    [VanguardMaterialResource(") || !writer.U32(resource.firstSlot) || !writer.Text(", 0)] ") ||
                !WriteResourceElementType(writer, module, elementType) || !writer.Text(" ") || !WriteResourceFieldName(writer, value.semantic) ||
                (value.type.arrayCount > 1 && (!writer.Text("[") || !writer.U32(value.type.arrayCount) || !writer.Text("]"))) ||
                !writer.Text(";\n"))
                return MaterialSlangResult::LimitExceeded;
        }
        if (!writer.Text("};\n")) return MaterialSlangResult::LimitExceeded;

        for (u32 valueId = 0; valueId < module.GetValues().Size(); ++valueId)
        {
            const MaterialIrValue& value = module.GetValues()[valueId];
            if (value.kind != MaterialIrValueKind::DynamicParameter)
                continue;
            const ResourceParameter* const resource = FindResourceParameter(resources, valueId);
            if (resource != nullptr ? !WriteResourceAccessor(writer, module, value, resource->firstSlot)
                                    : !WriteParameterAccessor(writer, module, value, domain.parameterTypeName))
                return MaterialSlangResult::LimitExceeded;
        }

        if (!writer.Text("[VanguardMaterialDomain(\"") || !writer.Text(domain.stableName) || !writer.Text("\", ") ||
            !writer.U32(domain.schemaVersion) || !writer.Text(", ") || !writer.U32(domain.legalStages) || !writer.Text(", ") ||
            !writer.U32(domain.requiredCapabilities) || !writer.Text(")]\n") ||
            !writer.Text(domain.outputTypeName) || !writer.Text(" ") || !writer.Text(domain.evaluationFunctionName) || !writer.Text("(") ||
            !writer.Text(domain.inputTypeName) || !writer.Text(" input)\n{\n"))
            return MaterialSlangResult::LimitExceeded;

        const auto data = module.GetData();
        for (u32 id = 0; id < module.GetValues().Size(); ++id)
        {
            const MaterialIrValue& value = module.GetValues()[id];
            const u32 firstLine = writer.Line();
            if (value.kind == MaterialIrValueKind::Instruction && value.opcode == MaterialIrOpcode::TextureSample &&
                !WriteTextureSampleResources(writer, module, value, id))
                return MaterialSlangResult::UnsupportedValue;
            if (!writer.Text("    ") || !WriteType(writer, module, value.typeId) || !writer.Text(" ") || !WriteValueName(writer, id) || !writer.Text(" = "))
                return MaterialSlangResult::LimitExceeded;
            bool emitted = false;
            if (value.kind == MaterialIrValueKind::Constant || value.kind == MaterialIrValueKind::StaticParameter)
                emitted = WriteLiteral(writer, module, value, data);
            else if (value.kind == MaterialIrValueKind::DynamicParameter)
                emitted = writer.Text("VanguardMaterialLoad_") && WriteParameterFieldName(writer, value.semantic) && writer.Text("()");
            else if (value.kind == MaterialIrValueKind::DomainInput)
            {
                const MaterialSlangSymbol* const symbol = FindSymbol(domain.inputs, value.semantic);
                if (symbol == nullptr) return MaterialSlangResult::MissingBinding;
                emitted = writer.Text("input.") && writer.Text(symbol->name);
            }
            else if (value.kind == MaterialIrValueKind::Instruction)
                emitted = WriteInstruction(writer, module, value, id);
            if (!emitted) return MaterialSlangResult::UnsupportedValue;
            if (!writer.Text(";\n")) return MaterialSlangResult::LimitExceeded;
            if (sourceRanges != nullptr)
                sourceRanges->PushBack({id, firstLine, writer.Line() - 1u, value.sourceNode, value.sourcePin});
        }

        if (!writer.Text("    ") || !writer.Text(domain.outputTypeName) || !writer.Text(" output;\n")) return MaterialSlangResult::LimitExceeded;
        for (const MaterialIrOutput& output : module.GetOutputs())
        {
            const MaterialSlangSymbol* const symbol = FindSymbol(domain.outputs, output.name);
            if (symbol == nullptr) return MaterialSlangResult::MissingBinding;
            if (output.value >= module.GetValues().Size() || !writer.Text("    output.") || !writer.Text(symbol->name) || !writer.Text(" = ") ||
                !WriteValueName(writer, output.value) || !writer.Text(";\n"))
                return MaterialSlangResult::UnsupportedValue;
        }
        if (!writer.Text("    return output;\n}\n") || !writer.Bytes(domain.suffix.Data(), domain.suffix.Size()))
            return MaterialSlangResult::LimitExceeded;
        return MaterialSlangResult::Success;
    }

    MaterialSlangResult FinalizeMaterialSlangSource(const containers::ArraySpan<const u8> probeSource,
                                                     const shader_tools::CompileOutput& reflection,
                                                     containers::DynamicArray<u8>& source,
                                                     const MaterialSlangLimits& limits) noexcept
    {
        source.Clear();
        if (probeSource.Empty() || limits.maximumSourceBytes == 0 || !reflection.HasMaterialContract())
            return MaterialSlangResult::InvalidArgument;
        SourceWriter writer(source, limits.maximumSourceBytes);
        u32 cursor = 0;
        while (cursor < probeSource.Size())
        {
            u32 marker = cursor;
            u32 prefixSize = 0;
            ReflectedProperty property = MarkerAt(probeSource, marker, prefixSize);
            while (marker < probeSource.Size() && property == ReflectedProperty::None)
            {
                ++marker;
                property = MarkerAt(probeSource, marker, prefixSize);
            }
            if (property == ReflectedProperty::None)
            {
                if (!writer.Bytes(probeSource.Data() + cursor, probeSource.Size() - cursor))
                    return MaterialSlangResult::LimitExceeded;
                break;
            }
            if (!writer.Bytes(probeSource.Data() + cursor, marker - cursor)) return MaterialSlangResult::LimitExceeded;
            const u32 hashOffset = marker + prefixSize;
            if (hashOffset + 19u > probeSource.Size()) return MaterialSlangResult::InvalidArgument;
            u64 memberName = 0;
            for (u32 index = 0; index < 16; ++index)
            {
                u64 digit = 0;
                if (!HexDigit(probeSource[hashOffset + index], digit)) return MaterialSlangResult::InvalidArgument;
                memberName = (memberName << 4u) | digit;
            }
            if (probeSource[hashOffset + 16u] != '*' || probeSource[hashOffset + 17u] != '/' || probeSource[hashOffset + 18u] != '0')
                return MaterialSlangResult::InvalidArgument;
            const shaders::ConstantMember* reflected = nullptr;
            for (const shaders::ConstantMember& member : reflection.GetMaterialParameters())
                if (member.name == memberName) { reflected = &member; break; }
            if (reflected == nullptr) return MaterialSlangResult::ReflectionMismatch;
            const u32 reflectedValue = property == ReflectedProperty::Offset ? reflected->byteOffset
                                       : property == ReflectedProperty::MatrixStride ? reflected->matrixStride
                                                                                    : reflected->arrayStride;
            if ((property == ReflectedProperty::MatrixStride || property == ReflectedProperty::ArrayStride) && reflectedValue == 0)
                return MaterialSlangResult::ReflectionMismatch;
            if (!writer.U32(reflectedValue))
                return MaterialSlangResult::LimitExceeded;
            cursor = hashOffset + 19u;
        }
        return MaterialSlangResult::Success;
    }

    const char* ToString(const MaterialSlangResult result) noexcept
    {
        switch (result)
        {
        case MaterialSlangResult::Success: return "success";
        case MaterialSlangResult::InvalidArgument: return "invalid argument";
        case MaterialSlangResult::UnsupportedType: return "unsupported type";
        case MaterialSlangResult::UnsupportedValue: return "unsupported value";
        case MaterialSlangResult::MissingBinding: return "missing domain binding";
        case MaterialSlangResult::ReflectionMismatch: return "reflection mismatch";
        case MaterialSlangResult::LimitExceeded: return "limit exceeded";
        }
        return "unknown material Slang generation result";
    }
} // namespace vanguard::material_tools
