#include <vanguard/material_tools/material_ir.hpp>

#include <algorithm>
#include <bit>
#include <limits>

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;

    constexpr u32 MaterialIrSchemaVersion = 3;

    [[nodiscard]] bool IsNumeric(const mt::MaterialIrType& type) noexcept
    {
        return type.kind == mt::MaterialIrTypeKind::Numeric;
    }

    [[nodiscard]] bool IsScalar(const mt::MaterialIrType& type) noexcept
    {
        return IsNumeric(type) && type.rows == 1 && type.columns == 1 && type.arrayCount == 1;
    }

    [[nodiscard]] bool IsBooleanScalar(const mt::MaterialIrType& type) noexcept
    {
        return IsScalar(type) && type.scalarType == shaders::ScalarType::Bool;
    }

    [[nodiscard]] u32 ScalarByteSize(const shaders::ScalarType type) noexcept
    {
        switch (type)
        {
        case shaders::ScalarType::Bool:
            return 1;
        case shaders::ScalarType::I16:
        case shaders::ScalarType::U16:
        case shaders::ScalarType::F16:
            return 2;
        case shaders::ScalarType::I32:
        case shaders::ScalarType::U32:
        case shaders::ScalarType::F32:
            return 4;
        case shaders::ScalarType::I64:
        case shaders::ScalarType::U64:
        case shaders::ScalarType::F64:
            return 8;
        }
        return 0;
    }

    [[nodiscard]] bool ValidConstantSize(const mt::MaterialIrValue& value) noexcept
    {
        if (value.type.kind == mt::MaterialIrTypeKind::Aggregate)
            return value.dataSize != 0;
        if (value.type.kind != mt::MaterialIrTypeKind::Numeric)
            return false;
        const u64 scalarBytes = ScalarByteSize(value.type.scalarType);
        const u64 expected = scalarBytes * value.type.rows * value.type.columns * value.type.arrayCount;
        return expected != 0 && expected <= ~u32{0} && value.dataSize == expected;
    }

    [[nodiscard]] bool ValidConstantData(const mt::MaterialIrValue& value, const containers::DynamicArray<u8>& data) noexcept
    {
        if (!ValidConstantSize(value) || value.dataOffset > data.Size() || value.dataSize > data.Size() - value.dataOffset)
            return false;
        if (value.type.kind != mt::MaterialIrTypeKind::Numeric || value.type.scalarType != shaders::ScalarType::Bool)
            return true;
        for (u32 index = 0; index < value.dataSize; ++index)
            if (data[value.dataOffset + index] > 1)
                return false;
        return true;
    }

    [[nodiscard]] bool HasDefaultResourceShape(const mt::MaterialIrType& type) noexcept
    {
        return type.textureDimension == mt::MaterialIrTextureDimension::None && type.resourceAccess == mt::MaterialIrResourceAccess::Read && type.bufferKind == mt::MaterialIrBufferKind::None &&
               type.samplerKind == mt::MaterialIrSamplerKind::Filtering && !type.textureArrayed && !type.textureMultisampled;
    }

    void AddDiagnostic(containers::DynamicArray<mt::MaterialIrDiagnostic>& diagnostics, const mt::MaterialIrResult code, const mt::MaterialIrValue& value, const mt::MaterialIrValueId id) noexcept
    {
        diagnostics.PushBack({code, value.sourceNode, value.sourcePin, id});
    }

    void UpdateU8(crypto::Sha256Builder& builder, const u8 value) noexcept
    {
        (void)builder.Update(&value, sizeof(value));
    }

    void UpdateU32(crypto::Sha256Builder& builder, const u32 value) noexcept
    {
        const u8 bytes[4]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        (void)builder.Update(bytes, sizeof(bytes));
    }

    void UpdateU64(crypto::Sha256Builder& builder, const u64 value) noexcept
    {
        const u8 bytes[8]{static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                          static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        (void)builder.Update(bytes, sizeof(bytes));
    }

    void UpdateType(crypto::Sha256Builder& builder, const mt::MaterialIrType& type) noexcept
    {
        UpdateU8(builder, static_cast<u8>(type.kind));
        UpdateU8(builder, static_cast<u8>(type.scalarType));
        UpdateU8(builder, type.rows);
        UpdateU8(builder, type.columns);
        UpdateU32(builder, type.arrayCount);
        UpdateU8(builder, static_cast<u8>(type.matrixOrder));
        UpdateU8(builder, static_cast<u8>(type.textureDimension));
        UpdateU8(builder, static_cast<u8>(type.resourceAccess));
        UpdateU8(builder, static_cast<u8>(type.bufferKind));
        UpdateU8(builder, static_cast<u8>(type.samplerKind));
        UpdateU8(builder, type.textureArrayed ? 1u : 0u);
        UpdateU8(builder, type.textureMultisampled ? 1u : 0u);
        (void)builder.Update(type.aggregate.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool FingerprintType(const mt::MaterialIrType& type, crypto::Digest256& fingerprint) noexcept
    {
        crypto::Sha256Builder hash;
        UpdateU32(hash, MaterialIrSchemaVersion);
        UpdateType(hash, type);
        return hash.Finalize(fingerprint);
    }

    [[nodiscard]] i32 CompareDigest(const crypto::Digest256& left, const crypto::Digest256& right) noexcept
    {
        for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
        {
            if (left.bytes[index] < right.bytes[index])
                return -1;
            if (left.bytes[index] > right.bytes[index])
                return 1;
        }
        return 0;
    }

    [[nodiscard]] bool IsValidType(const mt::MaterialIrType& type, const mt::MaterialIrTypeRegistry* const types) noexcept
    {
        if (type.kind > mt::MaterialIrTypeKind::AccelerationStructure || type.scalarType > shaders::ScalarType::F64 || type.rows == 0 || type.rows > 4 || type.columns == 0 || type.columns > 4 ||
            type.arrayCount == 0 || type.matrixOrder > mt::MaterialIrMatrixOrder::ColumnMajor || type.textureDimension > mt::MaterialIrTextureDimension::Cube ||
            type.resourceAccess > mt::MaterialIrResourceAccess::ReadWrite || type.bufferKind > mt::MaterialIrBufferKind::ByteAddress || type.samplerKind > mt::MaterialIrSamplerKind::Comparison)
            return false;

        const bool matrix = type.rows > 1 && type.columns > 1;
        switch (type.kind)
        {
        case mt::MaterialIrTypeKind::Poison:
            return false;
        case mt::MaterialIrTypeKind::Void:
            return type.rows == 1 && type.columns == 1 && type.arrayCount == 1 && type.aggregate.IsEmpty() && type.matrixOrder == mt::MaterialIrMatrixOrder::None && HasDefaultResourceShape(type);
        case mt::MaterialIrTypeKind::Numeric:
            return type.aggregate.IsEmpty() && HasDefaultResourceShape(type) && (matrix ? type.matrixOrder != mt::MaterialIrMatrixOrder::None : type.matrixOrder == mt::MaterialIrMatrixOrder::None);
        case mt::MaterialIrTypeKind::Aggregate:
        {
            if (type.aggregate.IsEmpty() || type.rows != 1 || type.columns != 1 || type.matrixOrder != mt::MaterialIrMatrixOrder::None || !HasDefaultResourceShape(type) || types == nullptr ||
                !types->IsFrozen())
                return false;
            mt::MaterialIrAggregateView view;
            return types->FindAggregate(type.aggregate, view);
        }
        case mt::MaterialIrTypeKind::Texture:
            return type.aggregate.IsEmpty() && type.columns == 1 && type.matrixOrder == mt::MaterialIrMatrixOrder::None && type.textureDimension != mt::MaterialIrTextureDimension::None &&
                   type.resourceAccess == mt::MaterialIrResourceAccess::Read && type.bufferKind == mt::MaterialIrBufferKind::None && type.samplerKind == mt::MaterialIrSamplerKind::Filtering &&
                   type.scalarType != shaders::ScalarType::Bool && !(type.textureArrayed && type.textureDimension == mt::MaterialIrTextureDimension::D3) &&
                   (!type.textureMultisampled || type.textureDimension == mt::MaterialIrTextureDimension::D2);
        case mt::MaterialIrTypeKind::Sampler:
            return type.rows == 1 && type.columns == 1 && type.aggregate.IsEmpty() && type.matrixOrder == mt::MaterialIrMatrixOrder::None && type.textureDimension == mt::MaterialIrTextureDimension::None &&
                   type.resourceAccess == mt::MaterialIrResourceAccess::Read && type.bufferKind == mt::MaterialIrBufferKind::None && !type.textureArrayed && !type.textureMultisampled;
        case mt::MaterialIrTypeKind::Buffer:
            if (type.columns != 1 || type.matrixOrder != mt::MaterialIrMatrixOrder::None || type.textureDimension != mt::MaterialIrTextureDimension::None ||
                type.samplerKind != mt::MaterialIrSamplerKind::Filtering || type.textureArrayed || type.textureMultisampled || type.bufferKind == mt::MaterialIrBufferKind::None)
                return false;
            if (type.bufferKind == mt::MaterialIrBufferKind::ByteAddress)
                return type.aggregate.IsEmpty() && type.scalarType == shaders::ScalarType::U32 && type.rows == 1;
            if (!type.aggregate.IsEmpty())
            {
                if (type.bufferKind != mt::MaterialIrBufferKind::Structured || types == nullptr || !types->IsFrozen())
                    return false;
                mt::MaterialIrAggregateView view;
                return types->FindAggregate(type.aggregate, view);
            }
            return type.scalarType != shaders::ScalarType::Bool;
        case mt::MaterialIrTypeKind::AccelerationStructure:
            return type.rows == 1 && type.columns == 1 && type.aggregate.IsEmpty() && type.matrixOrder == mt::MaterialIrMatrixOrder::None && type.textureDimension == mt::MaterialIrTextureDimension::None &&
                   type.resourceAccess == mt::MaterialIrResourceAccess::Read && type.bufferKind == mt::MaterialIrBufferKind::None && type.samplerKind == mt::MaterialIrSamplerKind::Filtering &&
                   !type.textureArrayed && !type.textureMultisampled;
        }
        return false;
    }

    [[nodiscard]] mt::MaterialIrType ArrayElementType(mt::MaterialIrType type) noexcept
    {
        type.arrayCount = 1;
        return type;
    }

    [[nodiscard]] bool ReadIndex(const mt::MaterialIrValue& value, const containers::DynamicArray<u8>& data, u32& index) noexcept
    {
        if (value.dataSize != 4 || value.dataOffset > data.Size() || value.dataSize > data.Size() - value.dataOffset)
            return false;
        const u8* const bytes = data.TypedData() + value.dataOffset;
        index = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) | (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
        return true;
    }

    enum class FoldResult : u8
    {
        Unchanged,
        Folded,
        Poisoned,
        LimitExceeded
    };

    [[nodiscard]] bool IsUnsignedInteger(const shaders::ScalarType type) noexcept
    {
        return type == shaders::ScalarType::U16 || type == shaders::ScalarType::U32 || type == shaders::ScalarType::U64;
    }

    [[nodiscard]] bool IsSignedInteger(const shaders::ScalarType type) noexcept
    {
        return type == shaders::ScalarType::I16 || type == shaders::ScalarType::I32 || type == shaders::ScalarType::I64;
    }

    [[nodiscard]] bool IsInteger(const shaders::ScalarType type) noexcept
    {
        return IsUnsignedInteger(type) || IsSignedInteger(type);
    }

    [[nodiscard]] u64 ReadUnsigned(const u8* const bytes, const u32 size) noexcept
    {
        u64 value = 0;
        for (u32 index = 0; index < size; ++index)
            value |= static_cast<u64>(bytes[index]) << (index * 8u);
        return value;
    }

    void WriteUnsigned(u8* const bytes, const u32 size, const u64 value) noexcept
    {
        for (u32 index = 0; index < size; ++index)
            bytes[index] = static_cast<u8>(value >> (index * 8u));
    }

    [[nodiscard]] i64 DecodeSigned(const u64 bits, const u32 size) noexcept
    {
        if (size == 8)
            return std::bit_cast<i64>(bits);
        const u32 bitCount = size * 8u;
        const u64 sign = u64{1} << (bitCount - 1u);
        return static_cast<i64>((bits ^ sign) - sign);
    }

    [[nodiscard]] bool CheckedSignedAdd(const i64 left, const i64 right, i64& result) noexcept
    {
        if ((right > 0 && left > std::numeric_limits<i64>::max() - right) || (right < 0 && left < std::numeric_limits<i64>::min() - right))
            return false;
        result = left + right;
        return true;
    }

    [[nodiscard]] bool CheckedSignedSubtract(const i64 left, const i64 right, i64& result) noexcept
    {
        if ((right < 0 && left > std::numeric_limits<i64>::max() + right) || (right > 0 && left < std::numeric_limits<i64>::min() + right))
            return false;
        result = left - right;
        return true;
    }

    [[nodiscard]] bool CheckedSignedMultiply(const i64 left, const i64 right, i64& result) noexcept
    {
        if (left == 0 || right == 0)
        {
            result = 0;
            return true;
        }
        if ((left == -1 && right == std::numeric_limits<i64>::min()) || (right == -1 && left == std::numeric_limits<i64>::min()))
            return false;
        if (left > 0)
        {
            if ((right > 0 && left > std::numeric_limits<i64>::max() / right) || (right < 0 && right < std::numeric_limits<i64>::min() / left))
                return false;
        }
        else if ((right > 0 && left < std::numeric_limits<i64>::min() / right) || (right < 0 && left < std::numeric_limits<i64>::max() / right))
            return false;
        result = left * right;
        return true;
    }

    [[nodiscard]] bool SignedInRange(const i64 value, const u32 size) noexcept
    {
        if (size == 8)
            return true;
        const u32 bits = size * 8u;
        const i64 minimum = -(i64{1} << (bits - 1u));
        const i64 maximum = (i64{1} << (bits - 1u)) - 1;
        return value >= minimum && value <= maximum;
    }

    [[nodiscard]] bool StoreFoldedConstant(mt::MaterialIrValue& value, containers::DynamicArray<u8>& data, const containers::ArraySpan<const u8> bytes, const u32 maximumDataBytes) noexcept
    {
        if (data.Size() > maximumDataBytes || bytes.Size() > maximumDataBytes - data.Size())
            return false;
        const u32 dataOffset = data.Size();
        for (const u8 byte : bytes)
            data.PushBack(byte);
        value.kind = mt::MaterialIrValueKind::Constant;
        value.opcode = mt::MaterialIrOpcode::None;
        value.typeId = mt::InvalidMaterialIrType;
        value.semantic = 0;
        value.firstOperand = 0;
        value.operandCount = 0;
        value.dataOffset = dataOffset;
        value.dataSize = bytes.Size();
        value.structuralFingerprint = {};
        return true;
    }

    void CopyValueAtSite(mt::MaterialIrValue& destination, const mt::MaterialIrValue& source) noexcept
    {
        const shaders::StageMask legalStages = destination.legalStages & source.legalStages;
        const u64 sourceNode = destination.sourceNode;
        const u32 sourcePin = destination.sourcePin;
        destination = source;
        destination.legalStages = legalStages;
        destination.requiredStages = 0;
        destination.sourceNode = sourceNode;
        destination.sourcePin = sourcePin;
        destination.typeId = mt::InvalidMaterialIrType;
        destination.structuralFingerprint = {};
    }

    [[nodiscard]] mt::MaterialIrValueId ResolveAlias(const containers::DynamicArray<mt::MaterialIrValueId>& aliases, mt::MaterialIrValueId value) noexcept
    {
        while (value < aliases.Size() && aliases[value] != value)
            value = aliases[value];
        return value;
    }

    [[nodiscard]] bool CompareInteger(const u64 leftBits, const u64 rightBits, const u32 size, const bool signedValue, const mt::MaterialIrComparePredicate predicate) noexcept
    {
        if (predicate == mt::MaterialIrComparePredicate::Equal)
            return leftBits == rightBits;
        if (predicate == mt::MaterialIrComparePredicate::NotEqual)
            return leftBits != rightBits;
        bool less = false;
        bool greater = false;
        if (signedValue)
        {
            const i64 left = DecodeSigned(leftBits, size);
            const i64 right = DecodeSigned(rightBits, size);
            less = left < right;
            greater = left > right;
        }
        else
        {
            less = leftBits < rightBits;
            greater = leftBits > rightBits;
        }
        switch (predicate)
        {
        case mt::MaterialIrComparePredicate::Less:
            return less;
        case mt::MaterialIrComparePredicate::LessEqual:
            return !greater;
        case mt::MaterialIrComparePredicate::Greater:
            return greater;
        case mt::MaterialIrComparePredicate::GreaterEqual:
            return !less;
        case mt::MaterialIrComparePredicate::Equal:
        case mt::MaterialIrComparePredicate::NotEqual:
            break;
        }
        return false;
    }

    [[nodiscard]] FoldResult FoldValue(const mt::MaterialIrValueId id, containers::DynamicArray<mt::MaterialIrValue>& values, containers::DynamicArray<mt::MaterialIrValueId>& operands,
                                       containers::DynamicArray<u8>& data, containers::DynamicArray<mt::MaterialIrValueId>& aliases, containers::DynamicArray<mt::MaterialIrValueId>& poisonRoots,
                                       const u32 maximumDataBytes) noexcept
    {
        mt::MaterialIrValue& value = values[id];
        if (value.kind != mt::MaterialIrValueKind::Instruction)
            return FoldResult::Unchanged;
        const auto operandId = [&operands, &value](const u32 index) { return operands[value.firstOperand + index]; };
        const auto operand = [&values, &operandId](const u32 index) -> const mt::MaterialIrValue& { return values[operandId(index)]; };

        // Select is the only lazy opcode. Its unchosen branch cannot poison the
        // selected result and is removed by the second reachability traversal.
        if (value.opcode == mt::MaterialIrOpcode::Select && operand(0).kind == mt::MaterialIrValueKind::Constant)
        {
            const bool condition = data[operand(0).dataOffset] != 0;
            const mt::MaterialIrValueId selectedId = operandId(condition ? 1u : 2u);
            const mt::MaterialIrValue& selected = values[selectedId];
            if (poisonRoots[selectedId] != mt::InvalidMaterialIrValue)
            {
                aliases[id] = selectedId;
                poisonRoots[id] = poisonRoots[selectedId];
                return FoldResult::Poisoned;
            }
            const shaders::StageMask foldedStages = value.legalStages & operand(0).legalStages & selected.legalStages;
            if (foldedStages == selected.legalStages)
                aliases[id] = selectedId;
            else
            {
                CopyValueAtSite(value, selected);
                value.legalStages = foldedStages;
            }
            return FoldResult::Folded;
        }

        for (u32 index = 0; index < value.operandCount; ++index)
        {
            const mt::MaterialIrValueId dependency = operandId(index);
            if (poisonRoots[dependency] == mt::InvalidMaterialIrValue)
                continue;
            poisonRoots[id] = poisonRoots[dependency];
            return FoldResult::Poisoned;
        }

        if (value.opcode == mt::MaterialIrOpcode::Cast && value.type == operand(0).type)
        {
            const mt::MaterialIrValueId selectedId = operandId(0);
            if ((operand(0).legalStages & value.legalStages) == operand(0).legalStages)
                aliases[id] = selectedId;
            else
                CopyValueAtSite(value, operand(0));
            return FoldResult::Folded;
        }

        if (value.opcode == mt::MaterialIrOpcode::Add || value.opcode == mt::MaterialIrOpcode::Subtract || value.opcode == mt::MaterialIrOpcode::Multiply || value.opcode == mt::MaterialIrOpcode::Divide)
        {
            if (operand(0).kind != mt::MaterialIrValueKind::Constant || operand(1).kind != mt::MaterialIrValueKind::Constant || !IsInteger(value.type.scalarType))
                return FoldResult::Unchanged;
            const u32 scalarBytes = ScalarByteSize(value.type.scalarType);
            containers::DynamicArray<u8> folded{memory::pools::Tools::GetInstance()};
            folded.Resize(value.dataSize == 0 ? operand(0).dataSize : value.dataSize);
            const u32 componentCount = operand(0).dataSize / scalarBytes;
            const u64 mask = scalarBytes == 8 ? ~u64{0} : (u64{1} << (scalarBytes * 8u)) - 1u;
            for (u32 component = 0; component < componentCount; ++component)
            {
                const u8* const leftBytes = data.TypedData() + operand(0).dataOffset + component * scalarBytes;
                const u8* const rightBytes = data.TypedData() + operand(1).dataOffset + component * scalarBytes;
                const u64 leftBits = ReadUnsigned(leftBytes, scalarBytes);
                const u64 rightBits = ReadUnsigned(rightBytes, scalarBytes);
                u64 resultBits = 0;
                if (IsUnsignedInteger(value.type.scalarType))
                {
                    if (value.opcode == mt::MaterialIrOpcode::Add)
                        resultBits = (leftBits + rightBits) & mask;
                    else if (value.opcode == mt::MaterialIrOpcode::Subtract)
                        resultBits = (leftBits - rightBits) & mask;
                    else if (value.opcode == mt::MaterialIrOpcode::Multiply)
                        resultBits = (leftBits * rightBits) & mask;
                    else
                    {
                        if (rightBits == 0)
                        {
                            poisonRoots[id] = id;
                            return FoldResult::Poisoned;
                        }
                        resultBits = leftBits / rightBits;
                    }
                }
                else
                {
                    const i64 left = DecodeSigned(leftBits, scalarBytes);
                    const i64 right = DecodeSigned(rightBits, scalarBytes);
                    i64 result = 0;
                    bool exact = false;
                    if (value.opcode == mt::MaterialIrOpcode::Add)
                        exact = CheckedSignedAdd(left, right, result);
                    else if (value.opcode == mt::MaterialIrOpcode::Subtract)
                        exact = CheckedSignedSubtract(left, right, result);
                    else if (value.opcode == mt::MaterialIrOpcode::Multiply)
                        exact = CheckedSignedMultiply(left, right, result);
                    else
                    {
                        if (right == 0 || (left == std::numeric_limits<i64>::min() && right == -1))
                        {
                            poisonRoots[id] = id;
                            return FoldResult::Poisoned;
                        }
                        result = left / right;
                        exact = true;
                    }
                    if (!exact || !SignedInRange(result, scalarBytes))
                        return FoldResult::Unchanged;
                    resultBits = std::bit_cast<u64>(result) & mask;
                }
                WriteUnsigned(folded.TypedData() + component * scalarBytes, scalarBytes, resultBits);
            }
            const shaders::StageMask operandStages = operand(0).legalStages & operand(1).legalStages;
            value.legalStages &= operandStages;
            return StoreFoldedConstant(value, data, folded, maximumDataBytes) ? FoldResult::Folded : FoldResult::LimitExceeded;
        }

        if (value.opcode == mt::MaterialIrOpcode::Compare && operand(0).kind == mt::MaterialIrValueKind::Constant && operand(1).kind == mt::MaterialIrValueKind::Constant &&
            (IsInteger(operand(0).type.scalarType) || operand(0).type.scalarType == shaders::ScalarType::Bool))
        {
            const u32 scalarBytes = ScalarByteSize(operand(0).type.scalarType);
            const u32 componentCount = operand(0).dataSize / scalarBytes;
            containers::DynamicArray<u8> folded{memory::pools::Tools::GetInstance()};
            folded.Resize(componentCount);
            const auto predicate = static_cast<mt::MaterialIrComparePredicate>(data[value.dataOffset]);
            for (u32 component = 0; component < componentCount; ++component)
            {
                const u64 left = ReadUnsigned(data.TypedData() + operand(0).dataOffset + component * scalarBytes, scalarBytes);
                const u64 right = ReadUnsigned(data.TypedData() + operand(1).dataOffset + component * scalarBytes, scalarBytes);
                folded[component] = CompareInteger(left, right, scalarBytes, IsSignedInteger(operand(0).type.scalarType), predicate) ? 1u : 0u;
            }
            value.legalStages &= operand(0).legalStages & operand(1).legalStages;
            return StoreFoldedConstant(value, data, folded, maximumDataBytes) ? FoldResult::Folded : FoldResult::LimitExceeded;
        }

        if (value.opcode == mt::MaterialIrOpcode::Construct && IsNumeric(value.type) && !(value.type.rows > 1 && value.type.columns > 1))
        {
            containers::DynamicArray<u8> folded{memory::pools::Tools::GetInstance()};
            shaders::StageMask legalStages = value.legalStages;
            for (u32 index = 0; index < value.operandCount; ++index)
            {
                if (operand(index).kind != mt::MaterialIrValueKind::Constant || !IsNumeric(operand(index).type) || (operand(index).type.rows > 1 && operand(index).type.columns > 1))
                    return FoldResult::Unchanged;
                legalStages &= operand(index).legalStages;
                for (u32 byte = 0; byte < operand(index).dataSize; ++byte)
                    folded.PushBack(data[operand(index).dataOffset + byte]);
            }
            const u64 expected = static_cast<u64>(ScalarByteSize(value.type.scalarType)) * value.type.rows * value.type.columns * value.type.arrayCount;
            if (folded.Size() != expected)
                return FoldResult::Unchanged;
            value.legalStages = legalStages;
            return StoreFoldedConstant(value, data, folded, maximumDataBytes) ? FoldResult::Folded : FoldResult::LimitExceeded;
        }

        if (value.opcode == mt::MaterialIrOpcode::Extract && operand(0).kind == mt::MaterialIrValueKind::Constant && IsNumeric(operand(0).type) && !(operand(0).type.rows > 1 && operand(0).type.columns > 1))
        {
            u32 index = 0;
            if (!ReadIndex(value, data, index))
                return FoldResult::Unchanged;
            const u32 elementBytes = operand(0).type.arrayCount > 1 ? operand(0).dataSize / operand(0).type.arrayCount : ScalarByteSize(operand(0).type.scalarType);
            const u64 expected = static_cast<u64>(ScalarByteSize(value.type.scalarType)) * value.type.rows * value.type.columns * value.type.arrayCount;
            if (elementBytes != expected)
                return FoldResult::Unchanged;
            const u8* const selected = data.TypedData() + operand(0).dataOffset + index * elementBytes;
            value.legalStages &= operand(0).legalStages;
            return StoreFoldedConstant(value, data, {selected, elementBytes}, maximumDataBytes) ? FoldResult::Folded : FoldResult::LimitExceeded;
        }
        return FoldResult::Unchanged;
    }

    [[nodiscard]] u64 FingerprintKey(const crypto::Digest256& fingerprint) noexcept
    {
        u64 key = 14695981039346656037ull;
        for (const u8 byte : fingerprint.bytes)
        {
            key ^= byte;
            key *= 1099511628211ull;
        }
        return key != 0 ? key : 1;
    }

    [[nodiscard]] bool ValidateOperation(const mt::MaterialIrValue& value, const containers::DynamicArray<mt::MaterialIrValue>& values, const containers::DynamicArray<mt::MaterialIrValueId>& operands,
                                         const containers::DynamicArray<u8>& data, const mt::MaterialIrTypeRegistry* const types) noexcept
    {
        const auto operand = [&values, &operands, &value](const u32 index) -> const mt::MaterialIrValue& { return values[operands[value.firstOperand + index]]; };

        switch (value.kind)
        {
        case mt::MaterialIrValueKind::Constant:
            return value.opcode == mt::MaterialIrOpcode::None && value.operandCount == 0 && ValidConstantData(value, data);
        case mt::MaterialIrValueKind::DynamicParameter:
            return value.opcode == mt::MaterialIrOpcode::None && value.operandCount == 0 && value.semantic != 0;
        case mt::MaterialIrValueKind::StaticParameter:
            return value.opcode == mt::MaterialIrOpcode::None && value.operandCount == 0 && value.semantic != 0 && IsNumeric(value.type) && ValidConstantData(value, data);
        case mt::MaterialIrValueKind::DomainInput:
            return value.opcode == mt::MaterialIrOpcode::None && value.operandCount == 0 && value.semantic != 0;
        case mt::MaterialIrValueKind::Instruction:
            break;
        }

        switch (value.opcode)
        {
        case mt::MaterialIrOpcode::Add:
        case mt::MaterialIrOpcode::Subtract:
        case mt::MaterialIrOpcode::Multiply:
        case mt::MaterialIrOpcode::Divide:
            return value.dataSize == 0 && value.operandCount == 2 && IsNumeric(value.type) && value.type.scalarType != shaders::ScalarType::Bool && operand(0).type == value.type &&
                   operand(1).type == value.type;
        case mt::MaterialIrOpcode::Compare:
            return value.operandCount == 2 && value.dataSize == 1 && IsNumeric(value.type) && data[value.dataOffset] <= static_cast<u8>(mt::MaterialIrComparePredicate::GreaterEqual) &&
                   value.type.scalarType == shaders::ScalarType::Bool && operand(0).type == operand(1).type && IsNumeric(operand(0).type) && value.type.rows == operand(0).type.rows &&
                   value.type.columns == operand(0).type.columns && value.type.arrayCount == operand(0).type.arrayCount;
        case mt::MaterialIrOpcode::Select:
            return value.dataSize == 0 && value.operandCount == 3 && IsBooleanScalar(operand(0).type) && operand(1).type == value.type && operand(2).type == value.type;
        case mt::MaterialIrOpcode::Cast:
            return value.dataSize == 0 && value.operandCount == 1 && IsNumeric(value.type) && IsNumeric(operand(0).type) && value.type.rows == operand(0).type.rows &&
                   value.type.columns == operand(0).type.columns && value.type.arrayCount == operand(0).type.arrayCount;
        case mt::MaterialIrOpcode::Construct:
            if (value.dataSize != 0 || value.operandCount == 0)
                return false;
            if (value.type.arrayCount > 1)
            {
                const mt::MaterialIrType element = ArrayElementType(value.type);
                if (value.operandCount != value.type.arrayCount)
                    return false;
                for (u32 index = 0; index < value.operandCount; ++index)
                    if (!(operand(index).type == element))
                        return false;
                return true;
            }
            if (IsNumeric(value.type))
            {
                u32 components = 0;
                for (u32 index = 0; index < value.operandCount; ++index)
                {
                    const mt::MaterialIrType& input = operand(index).type;
                    if (!IsNumeric(input) || input.scalarType != value.type.scalarType || input.arrayCount != 1)
                        return false;
                    components += input.rows * input.columns;
                }
                return components == static_cast<u32>(value.type.rows) * value.type.columns;
            }
            if (value.type.kind == mt::MaterialIrTypeKind::Aggregate && types != nullptr)
            {
                mt::MaterialIrAggregateView aggregate;
                if (!types->FindAggregate(value.type.aggregate, aggregate) || value.operandCount != aggregate.fields.Count())
                    return false;
                for (u32 index = 0; index < value.operandCount; ++index)
                    if (!(operand(index).type == aggregate.fields[index].type))
                        return false;
                return true;
            }
            return false;
        case mt::MaterialIrOpcode::Extract:
        {
            u32 index = 0;
            if (value.operandCount != 1 || !ReadIndex(value, data, index))
                return false;
            const mt::MaterialIrType& source = operand(0).type;
            if (source.arrayCount > 1)
                return index < source.arrayCount && value.type == ArrayElementType(source);
            if (source.kind == mt::MaterialIrTypeKind::Aggregate && types != nullptr)
            {
                mt::MaterialIrAggregateView aggregate;
                return types->FindAggregate(source.aggregate, aggregate) && index < aggregate.fields.Count() && value.type == aggregate.fields[index].type;
            }
            if (IsNumeric(source) && static_cast<u32>(source.rows) * source.columns > 1)
            {
                mt::MaterialIrType scalar = source;
                scalar.rows = 1;
                scalar.columns = 1;
                scalar.matrixOrder = mt::MaterialIrMatrixOrder::None;
                return index < static_cast<u32>(source.rows) * source.columns && value.type == scalar;
            }
            return false;
        }
        case mt::MaterialIrOpcode::TextureSample:
        {
            if (value.dataSize != 0 || value.operandCount != 3 || operand(0).type.kind != mt::MaterialIrTypeKind::Texture || operand(1).type.kind != mt::MaterialIrTypeKind::Sampler ||
                operand(1).type.samplerKind != mt::MaterialIrSamplerKind::Filtering || operand(0).type.arrayCount != 1 || operand(1).type.arrayCount != 1 || operand(0).type.textureMultisampled ||
                !IsNumeric(operand(2).type) || operand(2).type.scalarType != shaders::ScalarType::F32 || operand(2).type.columns != 1 || operand(2).type.arrayCount != 1 || !IsNumeric(value.type))
                return false;
            u8 coordinates = 0;
            switch (operand(0).type.textureDimension)
            {
            case mt::MaterialIrTextureDimension::D1:
                coordinates = 1;
                break;
            case mt::MaterialIrTextureDimension::D2:
                coordinates = 2;
                break;
            case mt::MaterialIrTextureDimension::D3:
            case mt::MaterialIrTextureDimension::Cube:
                coordinates = 3;
                break;
            case mt::MaterialIrTextureDimension::None:
                return false;
            }
            if (operand(0).type.textureArrayed)
                ++coordinates;
            mt::MaterialIrType sampled;
            sampled.kind = mt::MaterialIrTypeKind::Numeric;
            sampled.scalarType = operand(0).type.scalarType;
            sampled.rows = operand(0).type.rows;
            return operand(2).type.rows == coordinates && value.type == sampled;
        }
        case mt::MaterialIrOpcode::None:
            return false;
        }
        return false;
    }
} // namespace

namespace vanguard::material_tools
{
    namespace
    {
        MaterialIrScalarConstant EncodeInteger(const u64 value, const u8 size) noexcept
        {
            MaterialIrScalarConstant result;
            result.size = size;
            for (u8 index = 0; index < size; ++index)
                result.bytes[index] = static_cast<u8>(value >> (index * 8u));
            return result;
        }
    } // namespace

    MaterialIrScalarConstant EncodeMaterialBool(const bool value) noexcept
    {
        return EncodeInteger(value ? 1u : 0u, 1);
    }
    MaterialIrScalarConstant EncodeMaterialI32(const i32 value) noexcept
    {
        return EncodeInteger(static_cast<u32>(value), 4);
    }
    MaterialIrScalarConstant EncodeMaterialU32(const u32 value) noexcept
    {
        return EncodeInteger(value, 4);
    }
    MaterialIrScalarConstant EncodeMaterialI64(const i64 value) noexcept
    {
        return EncodeInteger(static_cast<u64>(value), 8);
    }
    MaterialIrScalarConstant EncodeMaterialU64(const u64 value) noexcept
    {
        return EncodeInteger(value, 8);
    }
    MaterialIrScalarConstant EncodeMaterialF32(const float value) noexcept
    {
        u32 bits = std::bit_cast<u32>(value);
        if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0)
            bits = 0x7fc00000u;
        return EncodeInteger(bits, 4);
    }
    MaterialIrScalarConstant EncodeMaterialF64(const double value) noexcept
    {
        u64 bits = std::bit_cast<u64>(value);
        if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0x000fffffffffffffull) != 0)
            bits = 0x7ff8000000000000ull;
        return EncodeInteger(bits, 8);
    }

    bool operator==(const MaterialIrType& left, const MaterialIrType& right) noexcept
    {
        return left.kind == right.kind && left.scalarType == right.scalarType && left.rows == right.rows && left.columns == right.columns && left.arrayCount == right.arrayCount &&
               left.matrixOrder == right.matrixOrder && left.textureDimension == right.textureDimension && left.resourceAccess == right.resourceAccess && left.bufferKind == right.bufferKind &&
               left.samplerKind == right.samplerKind && left.textureArrayed == right.textureArrayed && left.textureMultisampled == right.textureMultisampled && left.aggregate == right.aggregate;
    }

    MaterialIrTypeRegistry::MaterialIrTypeRegistry() noexcept : m_records(memory::pools::Tools::GetInstance()), m_fields(memory::pools::Tools::GetInstance()) {}

    bool MaterialIrTypeRegistry::RegisterAggregate(const MaterialIrAggregateDescription& description, MaterialIrType& type) noexcept
    {
        type = {};
        if (m_frozen || description.name == 0 || description.fields.Empty() || description.fields.Data() == nullptr || m_fields.Size() > ~u32{0} - description.fields.Count())
            return false;

        for (u32 index = 0; index < description.fields.Count(); ++index)
        {
            const MaterialIrAggregateFieldDescription& field = description.fields[index];
            if (field.name == 0 || field.type.kind == MaterialIrTypeKind::Poison || field.type.kind == MaterialIrTypeKind::Void || field.type.kind > MaterialIrTypeKind::AccelerationStructure ||
                field.type.scalarType > shaders::ScalarType::F64 || field.type.rows == 0 || field.type.rows > 4 || field.type.columns == 0 || field.type.columns > 4 || field.type.arrayCount == 0 ||
                field.type.matrixOrder > MaterialIrMatrixOrder::ColumnMajor || field.type.textureDimension > MaterialIrTextureDimension::Cube ||
                field.type.resourceAccess > MaterialIrResourceAccess::ReadWrite || field.type.bufferKind > MaterialIrBufferKind::ByteAddress || field.type.samplerKind > MaterialIrSamplerKind::Comparison)
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (description.fields[previous].name == field.name)
                    return false;
            if (!field.type.aggregate.IsEmpty())
            {
                bool found = false;
                for (const Record& record : m_records)
                    found = found || record.fingerprint == field.type.aggregate;
                if (!found)
                    return false;
                if (field.type.kind == MaterialIrTypeKind::Aggregate)
                {
                    if (field.type.rows != 1 || field.type.columns != 1 || field.type.matrixOrder != MaterialIrMatrixOrder::None || !HasDefaultResourceShape(field.type))
                        return false;
                }
                else if (field.type.kind == MaterialIrTypeKind::Buffer)
                {
                    if (field.type.columns != 1 || field.type.matrixOrder != MaterialIrMatrixOrder::None || field.type.textureDimension != MaterialIrTextureDimension::None ||
                        field.type.samplerKind != MaterialIrSamplerKind::Filtering || field.type.textureArrayed || field.type.textureMultisampled || field.type.bufferKind != MaterialIrBufferKind::Structured)
                        return false;
                }
                else
                    return false;
            }
            else if (!IsValidType(field.type, nullptr))
                return false;
        }

        crypto::Sha256Builder hash;
        UpdateU32(hash, MaterialIrSchemaVersion);
        UpdateU64(hash, description.name);
        UpdateU32(hash, description.fields.Count());
        for (const MaterialIrAggregateFieldDescription& field : description.fields)
        {
            UpdateU64(hash, field.name);
            UpdateType(hash, field.type);
        }
        crypto::Digest256 fingerprint;
        if (!hash.Finalize(fingerprint))
            return false;
        for (const Record& record : m_records)
            if (record.name == description.name || record.fingerprint == fingerprint)
                return false;

        Record record;
        record.name = description.name;
        record.fingerprint = fingerprint;
        record.firstField = m_fields.Size();
        record.fieldCount = description.fields.Count();
        for (const MaterialIrAggregateFieldDescription& field : description.fields)
            m_fields.PushBack(field);
        m_records.PushBack(record);

        type.kind = MaterialIrTypeKind::Aggregate;
        type.aggregate = fingerprint;
        return true;
    }

    bool MaterialIrTypeRegistry::Freeze() noexcept
    {
        if (m_frozen)
            return true;
        crypto::Sha256Builder hash;
        UpdateU32(hash, MaterialIrSchemaVersion);
        containers::DynamicArray<u32> order{memory::pools::Tools::GetInstance()};
        order.Reserve(m_records.Size());
        for (u32 index = 0; index < m_records.Size(); ++index)
            order.PushBack(index);
        std::sort(order.Begin(), order.End(), [this](const u32 left, const u32 right) { return CompareDigest(m_records[left].fingerprint, m_records[right].fingerprint) < 0; });
        UpdateU32(hash, order.Size());
        for (const u32 index : order)
            static_cast<void>(hash.Update(m_records[index].fingerprint.bytes, crypto::Digest256::ByteCount));
        m_frozen = hash.Finalize(m_fingerprint);
        if (m_frozen && m_records.Size() > 1)
            std::sort(m_records.Begin(), m_records.End(), [](const Record& left, const Record& right) { return CompareDigest(left.fingerprint, right.fingerprint) < 0; });
        return m_frozen;
    }

    bool MaterialIrTypeRegistry::IsFrozen() const noexcept
    {
        return m_frozen;
    }
    const crypto::Digest256& MaterialIrTypeRegistry::Fingerprint() const noexcept
    {
        return m_fingerprint;
    }

    bool MaterialIrTypeRegistry::FindAggregate(const crypto::Digest256& fingerprint, MaterialIrAggregateView& view) const noexcept
    {
        view = {};
        if (!m_frozen || fingerprint.IsEmpty())
            return false;
        const auto record =
            std::lower_bound(m_records.Begin(), m_records.End(), fingerprint, [](const Record& candidate, const crypto::Digest256& key) { return CompareDigest(candidate.fingerprint, key) < 0; });
        if (record == m_records.End() || record->fingerprint != fingerprint)
            return false;
        view.name = record->name;
        view.fingerprint = record->fingerprint;
        view.fields = {m_fields.TypedData() + record->firstField, record->fieldCount};
        return true;
    }

    MaterialIrModule::MaterialIrModule() noexcept
        : m_types(memory::pools::Tools::GetInstance()), m_typeFields(memory::pools::Tools::GetInstance()), m_values(memory::pools::Tools::GetInstance()), m_operands(memory::pools::Tools::GetInstance()),
          m_data(memory::pools::Tools::GetInstance()), m_outputs(memory::pools::Tools::GetInstance()), m_attributions(memory::pools::Tools::GetInstance())
    {
    }

    void MaterialIrModule::Reset() noexcept
    {
        m_domainFingerprint = {};
        m_contentFingerprint = {};
        m_types.Clear();
        m_typeFields.Clear();
        m_values.Clear();
        m_operands.Clear();
        m_data.Clear();
        m_outputs.Clear();
        m_attributions.Clear();
    }

    const crypto::Digest256& MaterialIrModule::GetDomainFingerprint() const noexcept
    {
        return m_domainFingerprint;
    }
    const crypto::Digest256& MaterialIrModule::GetContentFingerprint() const noexcept
    {
        return m_contentFingerprint;
    }
    containers::ArraySpan<const MaterialIrTypeRecord> MaterialIrModule::GetTypes() const noexcept
    {
        return m_types;
    }
    containers::ArraySpan<const MaterialIrTypeField> MaterialIrModule::GetTypeFields() const noexcept
    {
        return m_typeFields;
    }
    containers::ArraySpan<const MaterialIrValue> MaterialIrModule::GetValues() const noexcept
    {
        return m_values;
    }
    containers::ArraySpan<const MaterialIrValueId> MaterialIrModule::GetOperands() const noexcept
    {
        return m_operands;
    }
    containers::ArraySpan<const u8> MaterialIrModule::GetData() const noexcept
    {
        return m_data;
    }
    containers::ArraySpan<const MaterialIrOutput> MaterialIrModule::GetOutputs() const noexcept
    {
        return m_outputs;
    }
    containers::ArraySpan<const MaterialIrAttribution> MaterialIrModule::GetAttributions() const noexcept
    {
        return m_attributions;
    }

    MaterialIrBuilder::MaterialIrBuilder() noexcept
        : m_values(memory::pools::Tools::GetInstance()), m_operands(memory::pools::Tools::GetInstance()), m_data(memory::pools::Tools::GetInstance()), m_outputs(memory::pools::Tools::GetInstance())
    {
    }

    void MaterialIrBuilder::Reset(const crypto::Digest256& domainFingerprint, const MaterialIrTypeRegistry* const types) noexcept
    {
        m_domainFingerprint = domainFingerprint;
        m_types = types;
        m_values.Clear();
        m_operands.Clear();
        m_data.Clear();
        m_outputs.Clear();
    }

    MaterialIrResult MaterialIrBuilder::AddValue(const MaterialIrValueBuildDescription& description, MaterialIrValueId& id) noexcept
    {
        id = InvalidMaterialIrValue;
        if (description.legalStages == 0 || description.kind > MaterialIrValueKind::DomainInput || description.opcode > MaterialIrOpcode::TextureSample ||
            (!description.data.Empty() && description.data.Data() == nullptr) || m_operands.Size() > 0xffffffffu - description.operands.Size() || m_data.Size() > 0xffffffffu - description.data.Size())
            return MaterialIrResult::InvalidArgument;

        MaterialIrValue value;
        value.kind = description.kind;
        value.opcode = description.opcode;
        value.type = description.type;
        value.legalStages = description.legalStages;
        value.semantic = description.semantic;
        value.sourceNode = description.sourceNode;
        value.sourcePin = description.sourcePin;
        value.firstOperand = m_operands.Size();
        value.operandCount = description.operands.Size();
        value.dataOffset = m_data.Size();
        value.dataSize = description.data.Size();
        for (const MaterialIrValueId operand : description.operands)
            m_operands.PushBack(operand);
        for (const u8 byte : description.data)
            m_data.PushBack(byte);
        id = m_values.Size();
        m_values.PushBack(value);
        return MaterialIrResult::Success;
    }

    MaterialIrResult MaterialIrBuilder::AddOutput(const MaterialIrOutputBuildDescription& description) noexcept
    {
        if (description.name == 0 || description.value == InvalidMaterialIrValue || description.stages == 0)
            return MaterialIrResult::InvalidArgument;
        m_outputs.PushBack({description.name, description.value, description.stages});
        return MaterialIrResult::Success;
    }

    MaterialIrResult MaterialIrBuilder::Finalize(MaterialIrModule& module, containers::DynamicArray<MaterialIrDiagnostic>& diagnostics, const MaterialIrLimits& limits,
                                                 containers::DynamicArray<MaterialIrValueId>* const sourceValueRemap) noexcept
    {
        module.Reset();
        diagnostics.Clear();
        if (sourceValueRemap != nullptr)
            sourceValueRemap->Clear();
        if (m_domainFingerprint.IsEmpty() || m_outputs.Empty())
            return MaterialIrResult::InvalidArgument;
        if (m_types != nullptr && !m_types->IsFrozen())
            return MaterialIrResult::InvalidType;
        if (m_values.Size() > limits.maximumValues || m_operands.Size() > limits.maximumOperands || m_data.Size() > limits.maximumDataBytes || m_outputs.Size() > limits.maximumOutputs)
            return MaterialIrResult::LimitExceeded;

        containers::DynamicArray<MaterialIrOutput> effectiveOutputs{memory::pools::Tools::GetInstance()};
        effectiveOutputs.Reserve(m_outputs.Size());
        for (const MaterialIrOutput& output : m_outputs)
            effectiveOutputs.PushBack(output);
        containers::DynamicArray<u32> outputOrder{memory::pools::Tools::GetInstance()};
        outputOrder.Reserve(effectiveOutputs.Size());
        for (u32 index = 0; index < effectiveOutputs.Size(); ++index)
            outputOrder.PushBack(index);
        std::sort(outputOrder.Begin(), outputOrder.End(),
                  [&effectiveOutputs](const u32 left, const u32 right)
                  {
                      if (effectiveOutputs[left].name != effectiveOutputs[right].name)
                          return effectiveOutputs[left].name < effectiveOutputs[right].name;
                      return effectiveOutputs[left].stages < effectiveOutputs[right].stages;
                  });
        for (u32 index = 1; index < outputOrder.Size(); ++index)
            if (effectiveOutputs[outputOrder[index - 1u]].name == effectiveOutputs[outputOrder[index]].name)
                return MaterialIrResult::InvalidArgument;

        containers::DynamicArray<u8> colors{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<MaterialIrValueId> topological{memory::pools::Tools::GetInstance()};
        struct Frame
        {
            MaterialIrValueId value;
            u32 nextOperand;
        };
        containers::DynamicArray<Frame> stack{memory::pools::Tools::GetInstance()};
        const auto buildTopological = [&]() -> MaterialIrResult
        {
            colors.Clear();
            colors.Resize(m_values.Size());
            topological.Clear();
            stack.Clear();
            for (const u32 outputIndex : outputOrder)
            {
                const MaterialIrOutput& output = effectiveOutputs[outputIndex];
                if (output.value >= m_values.Size())
                    return MaterialIrResult::InvalidOperand;
                if (colors[output.value] == 2)
                    continue;
                stack.PushBack({output.value, 0});
                while (!stack.Empty())
                {
                    Frame& frame = stack.Back();
                    MaterialIrValue& value = m_values[frame.value];
                    if (colors[frame.value] == 0)
                        colors[frame.value] = 1;
                    if (frame.nextOperand < value.operandCount)
                    {
                        const MaterialIrValueId dependency = m_operands[value.firstOperand + frame.nextOperand++];
                        if (dependency >= m_values.Size())
                        {
                            AddDiagnostic(diagnostics, MaterialIrResult::InvalidOperand, value, frame.value);
                            return MaterialIrResult::InvalidOperand;
                        }
                        if (colors[dependency] == 1)
                        {
                            AddDiagnostic(diagnostics, MaterialIrResult::CycleDetected, m_values[dependency], dependency);
                            return MaterialIrResult::CycleDetected;
                        }
                        if (colors[dependency] == 0)
                            stack.PushBack({dependency, 0});
                        continue;
                    }
                    colors[frame.value] = 2;
                    topological.PushBack(frame.value);
                    stack.PopBack();
                }
            }
            return MaterialIrResult::Success;
        };
        MaterialIrResult traversal = buildTopological();
        if (traversal != MaterialIrResult::Success)
            return traversal;

        for (const MaterialIrValueId id : topological)
        {
            const MaterialIrValue& value = m_values[id];
            if (!IsValidType(value.type, m_types))
            {
                AddDiagnostic(diagnostics, MaterialIrResult::InvalidType, value, id);
                return MaterialIrResult::InvalidType;
            }
            if (!ValidateOperation(value, m_values, m_operands, m_data, m_types))
            {
                AddDiagnostic(diagnostics, MaterialIrResult::InvalidOperand, value, id);
                return MaterialIrResult::InvalidOperand;
            }
        }

        containers::DynamicArray<MaterialIrValueId> optimizationAliases{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<MaterialIrValueId> poisonRoots{memory::pools::Tools::GetInstance()};
        optimizationAliases.Resize(m_values.Size());
        poisonRoots.Resize(m_values.Size());
        for (u32 index = 0; index < m_values.Size(); ++index)
        {
            optimizationAliases[index] = index;
            poisonRoots[index] = InvalidMaterialIrValue;
        }
        for (const MaterialIrValueId id : topological)
        {
            MaterialIrValue& value = m_values[id];
            for (u32 operandIndex = 0; operandIndex < value.operandCount; ++operandIndex)
            {
                MaterialIrValueId& dependency = m_operands[value.firstOperand + operandIndex];
                dependency = ResolveAlias(optimizationAliases, dependency);
            }
            const FoldResult folded = FoldValue(id, m_values, m_operands, m_data, optimizationAliases, poisonRoots, limits.maximumDataBytes);
            if (folded == FoldResult::LimitExceeded)
                return MaterialIrResult::LimitExceeded;
        }
        for (MaterialIrOutput& output : effectiveOutputs)
            output.value = ResolveAlias(optimizationAliases, output.value);
        for (const MaterialIrOutput& output : effectiveOutputs)
        {
            if (poisonRoots[output.value] == InvalidMaterialIrValue)
                continue;
            const MaterialIrValueId root = poisonRoots[output.value];
            AddDiagnostic(diagnostics, MaterialIrResult::Poisoned, m_values[root], root);
            return MaterialIrResult::Poisoned;
        }

        // Folding can remove entire branches. Rebuild reachability before
        // parameter, stage, type-table, identity, and compact-module work.
        traversal = buildTopological();
        if (traversal != MaterialIrResult::Success)
            return traversal;

        containers::DynamicArray<MaterialIrValueId> parameters{memory::pools::Tools::GetInstance()};
        for (const MaterialIrValueId id : topological)
            if (m_values[id].kind == MaterialIrValueKind::DynamicParameter || m_values[id].kind == MaterialIrValueKind::StaticParameter)
                parameters.PushBack(id);
        std::sort(parameters.Begin(), parameters.End(), [this](const MaterialIrValueId left, const MaterialIrValueId right) { return m_values[left].semantic < m_values[right].semantic; });
        for (u32 index = 1; index < parameters.Size(); ++index)
        {
            const MaterialIrValue& previous = m_values[parameters[index - 1u]];
            const MaterialIrValue& current = m_values[parameters[index]];
            if (previous.semantic != current.semantic)
                continue;
            bool identical = previous.kind == current.kind && previous.type == current.type && previous.legalStages == current.legalStages && previous.dataSize == current.dataSize;
            for (u32 byte = 0; identical && byte < current.dataSize; ++byte)
                identical = m_data[previous.dataOffset + byte] == m_data[current.dataOffset + byte];
            if (!identical)
                return MaterialIrResult::DuplicateParameter;
        }

        containers::DynamicArray<shaders::StageMask> requiredStages{memory::pools::Tools::GetInstance()};
        requiredStages.Resize(m_values.Size());
        for (const MaterialIrOutput& output : effectiveOutputs)
            requiredStages[output.value] |= output.stages;

        struct PendingType
        {
            MaterialIrType type;
            crypto::Digest256 fingerprint;
        };
        containers::DynamicArray<PendingType> pendingTypes{memory::pools::Tools::GetInstance()};
        containers::HashMap<u64, u32> typeHeads{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<u32> typeNext{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<MaterialIrType> typeStack{memory::pools::Tools::GetInstance()};
        for (const MaterialIrValueId id : topological)
            typeStack.PushBack(m_values[id].type);
        while (!typeStack.Empty())
        {
            const MaterialIrType type = typeStack.Back();
            typeStack.PopBack();
            PendingType pending;
            pending.type = type;
            if (!FingerprintType(type, pending.fingerprint))
                return MaterialIrResult::InvalidType;
            const u64 typeKey = FingerprintKey(pending.fingerprint);
            u32 candidate = InvalidMaterialIrType;
            static_cast<void>(typeHeads.Find(typeKey, candidate));
            u32 collisionTail = InvalidMaterialIrType;
            bool exists = false;
            while (candidate != InvalidMaterialIrType)
            {
                if (pendingTypes[candidate].fingerprint == pending.fingerprint)
                {
                    if (!(pendingTypes[candidate].type == pending.type))
                        return MaterialIrResult::InvalidType;
                    exists = true;
                    break;
                }
                collisionTail = candidate;
                candidate = typeNext[candidate];
            }
            if (exists)
                continue;
            if (pendingTypes.Size() >= limits.maximumTypes)
                return MaterialIrResult::LimitExceeded;
            const u32 pendingId = pendingTypes.Size();
            pendingTypes.PushBack(pending);
            typeNext.PushBack(InvalidMaterialIrType);
            if (collisionTail != InvalidMaterialIrType)
                typeNext[collisionTail] = pendingId;
            else if (!typeHeads.Insert(typeKey, pendingId).IsSuccessful())
                return MaterialIrResult::InvalidType;

            if (type.arrayCount > 1)
                typeStack.PushBack(ArrayElementType(type));
            if (!type.aggregate.IsEmpty())
            {
                MaterialIrAggregateView aggregate;
                if (m_types == nullptr || !m_types->FindAggregate(type.aggregate, aggregate))
                    return MaterialIrResult::InvalidType;
                for (const MaterialIrAggregateFieldDescription& field : aggregate.fields)
                    typeStack.PushBack(field.type);
                MaterialIrType aggregateType;
                aggregateType.kind = MaterialIrTypeKind::Aggregate;
                aggregateType.aggregate = type.aggregate;
                if (!(aggregateType == type))
                    typeStack.PushBack(aggregateType);
            }
        }
        std::sort(pendingTypes.Begin(), pendingTypes.End(), [](const PendingType& left, const PendingType& right) { return CompareDigest(left.fingerprint, right.fingerprint) < 0; });
        u64 totalTypeFields = 0;
        for (const PendingType& pending : pendingTypes)
        {
            MaterialIrTypeRecord record;
            record.type = pending.type;
            record.fingerprint = pending.fingerprint;
            if (pending.type.kind == MaterialIrTypeKind::Aggregate)
            {
                MaterialIrAggregateView aggregate;
                if (m_types == nullptr || !m_types->FindAggregate(pending.type.aggregate, aggregate))
                    return MaterialIrResult::InvalidType;
                record.name = aggregate.name;
                record.firstField = static_cast<u32>(totalTypeFields);
                record.fieldCount = aggregate.fields.Count();
                totalTypeFields += aggregate.fields.Count();
                if (totalTypeFields > limits.maximumTypeFields || totalTypeFields > ~u32{0})
                    return MaterialIrResult::LimitExceeded;
            }
            module.m_types.PushBack(record);
        }
        const auto findType = [&pendingTypes](const MaterialIrType& type) noexcept -> MaterialIrTypeId
        {
            crypto::Digest256 fingerprint;
            if (!FingerprintType(type, fingerprint))
                return InvalidMaterialIrType;
            const auto found =
                std::lower_bound(pendingTypes.Begin(), pendingTypes.End(), fingerprint, [](const PendingType& candidate, const crypto::Digest256& key) { return CompareDigest(candidate.fingerprint, key) < 0; });
            if (found == pendingTypes.End() || found->fingerprint != fingerprint || !(found->type == type))
                return InvalidMaterialIrType;
            return static_cast<MaterialIrTypeId>(found - pendingTypes.Begin());
        };
        for (const MaterialIrTypeRecord& record : module.m_types)
        {
            if (record.type.kind != MaterialIrTypeKind::Aggregate)
                continue;
            MaterialIrAggregateView aggregate;
            if (m_types == nullptr || !m_types->FindAggregate(record.type.aggregate, aggregate))
                return MaterialIrResult::InvalidType;
            for (const MaterialIrAggregateFieldDescription& field : aggregate.fields)
            {
                const MaterialIrTypeId fieldType = findType(field.type);
                if (fieldType == InvalidMaterialIrType)
                    return MaterialIrResult::InvalidType;
                module.m_typeFields.PushBack({field.name, fieldType});
            }
        }
        for (const MaterialIrValueId id : topological)
        {
            m_values[id].typeId = findType(m_values[id].type);
            if (m_values[id].typeId == InvalidMaterialIrType)
                return MaterialIrResult::InvalidType;
        }

        for (u32 index = topological.Size(); index > 0; --index)
        {
            const MaterialIrValueId id = topological[index - 1u];
            MaterialIrValue& value = m_values[id];
            value.requiredStages = requiredStages[id];
            if ((value.requiredStages & ~value.legalStages) != 0)
            {
                AddDiagnostic(diagnostics, MaterialIrResult::InvalidStage, value, id);
                return MaterialIrResult::InvalidStage;
            }
            for (u32 operandIndex = 0; operandIndex < value.operandCount; ++operandIndex)
                requiredStages[m_operands[value.firstOperand + operandIndex]] |= value.requiredStages;
        }

        for (const MaterialIrValueId id : topological)
        {
            MaterialIrValue& value = m_values[id];
            crypto::Sha256Builder builder;
            UpdateU32(builder, MaterialIrSchemaVersion);
            UpdateU8(builder, static_cast<u8>(value.kind));
            UpdateU8(builder, static_cast<u8>(value.opcode));
            UpdateType(builder, value.type);
            UpdateU32(builder, value.legalStages);
            UpdateU64(builder, value.semantic);
            UpdateU32(builder, value.operandCount);
            for (u32 operandIndex = 0; operandIndex < value.operandCount; ++operandIndex)
            {
                const crypto::Digest256& dependency = m_values[m_operands[value.firstOperand + operandIndex]].structuralFingerprint;
                (void)builder.Update(dependency.bytes, crypto::Digest256::ByteCount);
            }
            const u32 identityDataSize = value.kind == MaterialIrValueKind::DynamicParameter ? 0u : value.dataSize;
            UpdateU32(builder, identityDataSize);
            if (identityDataSize != 0)
                (void)builder.Update(m_data.TypedData() + value.dataOffset, value.dataSize);
            if (!builder.Finalize(value.structuralFingerprint))
                return MaterialIrResult::InvalidArgument;
        }

        crypto::Sha256Builder moduleIdentity;
        UpdateU32(moduleIdentity, MaterialIrSchemaVersion);
        (void)moduleIdentity.Update(m_domainFingerprint.bytes, crypto::Digest256::ByteCount);
        UpdateU32(moduleIdentity, outputOrder.Size());
        for (const u32 index : outputOrder)
        {
            const MaterialIrOutput& output = effectiveOutputs[index];
            UpdateU64(moduleIdentity, output.name);
            UpdateU32(moduleIdentity, output.stages);
            (void)moduleIdentity.Update(m_values[output.value].structuralFingerprint.bytes, crypto::Digest256::ByteCount);
        }

        module.m_domainFingerprint = m_domainFingerprint;
        if (!moduleIdentity.Finalize(module.m_contentFingerprint))
            return MaterialIrResult::InvalidArgument;

        containers::DynamicArray<MaterialIrValueId> remap{memory::pools::Tools::GetInstance()};
        containers::HashMap<u64, MaterialIrValueId> structuralHeads{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<MaterialIrValueId> structuralNext{memory::pools::Tools::GetInstance()};
        remap.Resize(m_values.Size());
        for (MaterialIrValueId& id : remap)
            id = InvalidMaterialIrValue;
        for (const MaterialIrValueId id : topological)
        {
            const MaterialIrValue& source = m_values[id];
            MaterialIrValue compact = source;
            compact.firstOperand = module.m_operands.Size();
            compact.dataOffset = module.m_data.Size();
            containers::DynamicArray<MaterialIrValueId> compactOperands{memory::pools::Tools::GetInstance()};
            compactOperands.Reserve(source.operandCount);
            for (u32 operandIndex = 0; operandIndex < source.operandCount; ++operandIndex)
            {
                const MaterialIrValueId oldOperand = m_operands[source.firstOperand + operandIndex];
                if (remap[oldOperand] == InvalidMaterialIrValue)
                    return MaterialIrResult::InvalidOperand;
                compactOperands.PushBack(remap[oldOperand]);
            }
            MaterialIrValueId existing = InvalidMaterialIrValue;
            MaterialIrValueId candidateId = InvalidMaterialIrValue;
            const u64 structuralKey = FingerprintKey(source.structuralFingerprint);
            static_cast<void>(structuralHeads.Find(structuralKey, candidateId));
            MaterialIrValueId collisionTail = InvalidMaterialIrValue;
            while (candidateId != InvalidMaterialIrValue)
            {
                const MaterialIrValue& candidate = module.m_values[candidateId];
                if (candidate.structuralFingerprint != source.structuralFingerprint || candidate.kind != source.kind || candidate.opcode != source.opcode || !(candidate.type == source.type) ||
                    candidate.legalStages != source.legalStages || candidate.semantic != source.semantic || candidate.operandCount != compactOperands.Size() || candidate.dataSize != source.dataSize)
                {
                    collisionTail = candidateId;
                    candidateId = structuralNext[candidateId];
                    continue;
                }
                bool equal = true;
                for (u32 operandIndex = 0; equal && operandIndex < candidate.operandCount; ++operandIndex)
                    equal = module.m_operands[candidate.firstOperand + operandIndex] == compactOperands[operandIndex];
                for (u32 byteIndex = 0; equal && byteIndex < candidate.dataSize; ++byteIndex)
                    equal = module.m_data[candidate.dataOffset + byteIndex] == m_data[source.dataOffset + byteIndex];
                if (equal)
                {
                    existing = candidateId;
                    break;
                }
                collisionTail = candidateId;
                candidateId = structuralNext[candidateId];
            }
            if (existing == InvalidMaterialIrValue)
            {
                existing = module.m_values.Size();
                compact.firstOperand = module.m_operands.Size();
                compact.dataOffset = module.m_data.Size();
                for (const MaterialIrValueId operand : compactOperands)
                    module.m_operands.PushBack(operand);
                for (u32 byteIndex = 0; byteIndex < source.dataSize; ++byteIndex)
                    module.m_data.PushBack(m_data[source.dataOffset + byteIndex]);
                module.m_values.PushBack(compact);
                structuralNext.PushBack(InvalidMaterialIrValue);
                if (collisionTail != InvalidMaterialIrValue)
                    structuralNext[collisionTail] = existing;
                else if (!structuralHeads.Insert(structuralKey, existing).IsSuccessful())
                    return MaterialIrResult::LimitExceeded;
            }
            remap[id] = existing;
        }
        for (const u32 index : outputOrder)
        {
            MaterialIrOutput output = effectiveOutputs[index];
            output.value = remap[output.value];
            if (output.value == InvalidMaterialIrValue)
                return MaterialIrResult::InvalidOperand;
            module.m_outputs.PushBack(output);
        }
        for (u32 index = 0; index < m_values.Size(); ++index)
        {
            const MaterialIrValue& source = m_values[index];
            if (source.sourceNode == 0 && source.sourcePin == 0)
                continue;
            const MaterialIrValueId representative = ResolveAlias(optimizationAliases, index);
            const MaterialIrValueId compact = representative < remap.Size() ? remap[representative] : InvalidMaterialIrValue;
            if (compact == InvalidMaterialIrValue)
                continue;
            bool duplicateAttribution = false;
            for (const MaterialIrAttribution& attribution : module.m_attributions)
                duplicateAttribution = duplicateAttribution || (attribution.value == compact && attribution.sourceNode == source.sourceNode && attribution.sourcePin == source.sourcePin);
            if (!duplicateAttribution)
                module.m_attributions.PushBack({compact, source.sourceNode, source.sourcePin});
        }
        if (sourceValueRemap != nullptr)
        {
            sourceValueRemap->Resize(remap.Size());
            if (sourceValueRemap->Size() != remap.Size())
                return MaterialIrResult::LimitExceeded;
            for (u32 index = 0; index < remap.Size(); ++index)
            {
                const MaterialIrValueId representative = ResolveAlias(optimizationAliases, index);
                (*sourceValueRemap)[index] = representative < remap.Size() ? remap[representative] : InvalidMaterialIrValue;
            }
        }
        return MaterialIrResult::Success;
    }

    const char* ToString(const MaterialIrResult result) noexcept
    {
        switch (result)
        {
        case MaterialIrResult::Success:
            return "Success";
        case MaterialIrResult::InvalidArgument:
            return "InvalidArgument";
        case MaterialIrResult::InvalidType:
            return "InvalidType";
        case MaterialIrResult::InvalidOperand:
            return "InvalidOperand";
        case MaterialIrResult::CycleDetected:
            return "CycleDetected";
        case MaterialIrResult::DuplicateParameter:
            return "DuplicateParameter";
        case MaterialIrResult::InvalidStage:
            return "InvalidStage";
        case MaterialIrResult::Poisoned:
            return "Poisoned";
        case MaterialIrResult::LimitExceeded:
            return "LimitExceeded";
        }
        return "Unknown";
    }
} // namespace vanguard::material_tools
