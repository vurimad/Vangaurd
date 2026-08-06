#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::reflection
{
    using SchemaTypeId = u64;
    using SchemaFieldId = u64;

    inline constexpr SchemaTypeId InvalidSchemaTypeId = 0;
    inline constexpr SchemaFieldId InvalidSchemaFieldId = 0;

    enum class ValueKind : u8
    {
        Bool,
        U8,
        U16,
        U32,
        U64,
        I8,
        I16,
        I32,
        I64,
        F32,
        F64,
        Enumeration,
        Structure,
        ResourceReference,
        String,
        Array,
        Blob
    };

    enum class FieldFlags : u16
    {
        None = 0,
        Required = 1u << 0u,
        EditorOnly = 1u << 1u,
        Transient = 1u << 2u,
        OptionalDependency = 1u << 3u,
        SoftDependency = 1u << 4u
    };

    [[nodiscard]] constexpr FieldFlags operator|(const FieldFlags left, const FieldFlags right) noexcept
    {
        return static_cast<FieldFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const FieldFlags value, const FieldFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool IsStableNameByte(const char byte, const bool allowDot) noexcept
    {
        return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '_' || (allowDot && byte == '.');
    }

    [[nodiscard]] constexpr u64 HashStableName(const char* const name, const bool allowDot) noexcept
    {
        if (name == nullptr || name[0] == '\0')
        {
            return 0;
        }

        constexpr u64 offsetBasis = 14695981039346656037ull;
        constexpr u64 prime = 1099511628211ull;
        u64 hash = offsetBasis;
        bool previousWasDot = false;
        for (usize index = 0; name[index] != '\0'; ++index)
        {
            const char byte = name[index];
            if (!IsStableNameByte(byte, allowDot) || (byte == '.' && (index == 0 || previousWasDot)))
            {
                return 0;
            }
            hash ^= static_cast<u8>(byte);
            hash *= prime;
            previousWasDot = byte == '.';
        }
        return previousWasDot ? 0 : hash;
    }

    [[nodiscard]] constexpr SchemaTypeId HashSchemaName(const char* const name) noexcept
    {
        return HashStableName(name, true);
    }

    [[nodiscard]] constexpr SchemaFieldId HashFieldName(const char* const name) noexcept
    {
        return HashStableName(name, false);
    }

    namespace builtin
    {
        inline constexpr SchemaTypeId Bool = HashSchemaName("vanguard.bool");
        inline constexpr SchemaTypeId U8 = HashSchemaName("vanguard.u8");
        inline constexpr SchemaTypeId U16 = HashSchemaName("vanguard.u16");
        inline constexpr SchemaTypeId U32 = HashSchemaName("vanguard.u32");
        inline constexpr SchemaTypeId U64 = HashSchemaName("vanguard.u64");
        inline constexpr SchemaTypeId I8 = HashSchemaName("vanguard.i8");
        inline constexpr SchemaTypeId I16 = HashSchemaName("vanguard.i16");
        inline constexpr SchemaTypeId I32 = HashSchemaName("vanguard.i32");
        inline constexpr SchemaTypeId I64 = HashSchemaName("vanguard.i64");
        inline constexpr SchemaTypeId F32 = HashSchemaName("vanguard.f32");
        inline constexpr SchemaTypeId F64 = HashSchemaName("vanguard.f64");
        inline constexpr SchemaTypeId ResourceReference = HashSchemaName("vanguard.resource_reference");
        inline constexpr SchemaTypeId String = HashSchemaName("vanguard.string");
        inline constexpr SchemaTypeId Blob = HashSchemaName("vanguard.blob");
    } // namespace builtin

    struct ArrayOperations
    {
        using SizeFunction = u32 (*)(const void* array) noexcept;
        using ConstElementFunction = const void* (*)(const void* array, u32 index) noexcept;
        using ResizeFunction = bool (*)(void* array, u32 size) noexcept;
        using ElementFunction = void* (*)(void* array, u32 index) noexcept;

        u32 elementSize = 0;
        u32 elementAlignment = 0;
        SizeFunction size = nullptr;
        ConstElementFunction constElement = nullptr;
        ResizeFunction resize = nullptr;
        ElementFunction element = nullptr;

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return elementSize != 0 && elementAlignment != 0 && size != nullptr && constElement != nullptr && resize != nullptr &&
                   element != nullptr;
        }
    };

    struct SchemaField
    {
        SchemaFieldId id = InvalidSchemaFieldId;
        SchemaTypeId valueType = InvalidSchemaTypeId;
        const char* name = "";
        ValueKind kind = ValueKind::Blob;
        FieldFlags flags = FieldFlags::None;
        u32 offset = 0;
        u32 size = 0;
        u32 alignment = 0;
        u16 introducedVersion = 1;
        u16 removedVersion = 0;
        ValueKind elementKind = ValueKind::Blob;
        const ArrayOperations* arrayOperations = nullptr;

        [[nodiscard]] constexpr bool IsActive(const u16 schemaVersion) const noexcept
        {
            return introducedVersion <= schemaVersion && (removedVersion == 0 || schemaVersion < removedVersion);
        }
    };

    struct Schema
    {
        SchemaTypeId id = InvalidSchemaTypeId;
        const char* name = "";
        u32 size = 0;
        u32 alignment = 0;
        u16 currentVersion = 1;
        u16 minimumReadableVersion = 1;
        const SchemaField* fields = nullptr;
        u32 fieldCount = 0;

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return id != InvalidSchemaTypeId;
        }
    };

    [[nodiscard]] constexpr SchemaField MakeField(const char* const name, const SchemaTypeId valueType, const ValueKind kind,
                                                  const u32 offset, const u32 size, const u32 alignment, const u16 introducedVersion = 1,
                                                  const u16 removedVersion = 0, const FieldFlags flags = FieldFlags::None) noexcept
    {
        return {HashFieldName(name), valueType,       name,   kind, flags, offset, size, alignment, introducedVersion,
                removedVersion,      ValueKind::Blob, nullptr};
    }

    enum class TypeKind : u8
    {
        Name,
        Fundamental,
        Class,
        Array,
        Simple,
        Enumeration,
        StaticArray,
        NativeArray,
        Pointer,
        Handle,
        WeakHandle,
        ResourceReference,
        AsyncResourceReference,
        BitField,
        LegacyCurve,
        ScriptReference,
        Count
    };

    struct TypeDescriptor
    {
        const void* nativeType = nullptr;
        const char* name = "";
        u32 size = 0;
        u32 alignment = 0;
        TypeKind kind = TypeKind::Count;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return nativeType != nullptr;
        }
    };

    // Reflection startup is a composition-root operation. Call it after
    // Vanguard Memory is initialized and before registering game modules.
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    [[nodiscard]] TypeDescriptor FindType(const char* name) noexcept;
    [[nodiscard]] TypeDescriptor FindTypeByHash(u64 nameHash) noexcept;

    // Schema descriptors and their field arrays are non-owning static
    // metadata. Registration mutation is a composition-root operation and
    // must not race schema lookup, serialization, or editor inspection.
    [[nodiscard]] bool RegisterSchema(const Schema& schema) noexcept;
    [[nodiscard]] bool UnregisterSchema(SchemaTypeId type) noexcept;
    [[nodiscard]] const Schema* FindSchema(SchemaTypeId type) noexcept;
    [[nodiscard]] const Schema* FindSchema(const char* name) noexcept;
    [[nodiscard]] u32 SchemaCount() noexcept;
} // namespace vanguard::reflection
