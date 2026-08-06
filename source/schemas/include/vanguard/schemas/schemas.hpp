#pragma once

#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::schemas
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        IoFailure,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        SchemaNotFound,
        SchemaMismatch,
        FieldMismatch,
        MissingRequiredField,
        UnsupportedValueKind,
        OutOfMemory
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class ObjectFlags : u16
    {
        None = 0,
        Deterministic = 1u << 0u,
        HasEditorData = 1u << 1u
    };

    [[nodiscard]] constexpr ObjectFlags operator|(const ObjectFlags left, const ObjectFlags right) noexcept
    {
        return static_cast<ObjectFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    struct ObjectHeader
    {
        static constexpr u32 Magic = serialization::MakeFourCC('V', 'O', 'B', 'J');
        static constexpr u8 EncodingVersion = 1;
        static constexpr u8 LittleEndian = 1;
        static constexpr u16 WireSize = 48;

        reflection::SchemaTypeId schema = reflection::InvalidSchemaTypeId;
        u16 schemaVersion = 0;
        ObjectFlags flags = ObjectFlags::None;
        u64 objectSize = 0;
        u64 fieldTableOffset = WireSize;
        u32 fieldCount = 0;
    };

    struct FieldRecord
    {
        static constexpr u16 WireSize = 40;

        reflection::SchemaFieldId field = reflection::InvalidSchemaFieldId;
        reflection::SchemaTypeId valueType = reflection::InvalidSchemaTypeId;
        u64 dataOffset = 0;
        u64 dataSize = 0;
        reflection::ValueKind kind = reflection::ValueKind::Blob;
        reflection::ValueKind elementKind = reflection::ValueKind::Bool;
        reflection::FieldFlags flags = reflection::FieldFlags::None;
    };

    struct WriteOptions
    {
        bool includeEditorFields = false;
        u32 maximumFields = 4096;
        u32 maximumNestingDepth = 32;
        u64 maximumObjectBytes = 256ull * 1024ull * 1024ull;
        u32 maximumStringBytes = 16u * 1024u * 1024u;
        u32 maximumArrayElements = 1024u * 1024u;
    };

    struct ReadLimits
    {
        u32 maximumFields = 4096;
        u32 maximumNestingDepth = 32;
        u64 maximumObjectBytes = 256ull * 1024ull * 1024ull;
        u32 maximumStringBytes = 16u * 1024u * 1024u;
        u32 maximumArrayElements = 1024u * 1024u;
    };

    struct ReadInfo
    {
        u16 sourceSchemaVersion = 0;
        ObjectFlags sourceFlags = ObjectFlags::None;
        u32 fieldsRead = 0;
        u32 unknownFieldsSkipped = 0;
    };

    using DependencyKind = resources::DependencyKind;

    using DependencyVisitor = bool (*)(resources::ResourceReference reference, DependencyKind kind, void* userData) noexcept;

    template <typename Element> struct DynamicArrayAdapter
    {
        [[nodiscard]] static u32 Size(const void* const array) noexcept
        {
            return static_cast<const containers::DynamicArray<Element>*>(array)->Size();
        }

        [[nodiscard]] static const void* ConstElement(const void* const array, const u32 index) noexcept
        {
            const auto& values = *static_cast<const containers::DynamicArray<Element>*>(array);
            return index < values.Size() ? &values[index] : nullptr;
        }

        [[nodiscard]] static bool Resize(void* const array, const u32 size) noexcept
        {
            auto& values = *static_cast<containers::DynamicArray<Element>*>(array);
            values.Resize(size);
            return values.Size() == size;
        }

        [[nodiscard]] static void* ElementAt(void* const array, const u32 index) noexcept
        {
            auto& values = *static_cast<containers::DynamicArray<Element>*>(array);
            return index < values.Size() ? &values[index] : nullptr;
        }

        inline static constexpr reflection::ArrayOperations Operations{sizeof(Element), alignof(Element), &Size,
                                                                       &ConstElement,   &Resize,          &ElementAt};
    };

    template <typename Element>
    [[nodiscard]] constexpr reflection::SchemaField MakeDynamicArrayField(
        const char* const name, const reflection::SchemaTypeId elementType, const reflection::ValueKind elementKind, const u32 offset,
        const u16 introducedVersion = 1, const u16 removedVersion = 0,
        const reflection::FieldFlags flags = reflection::FieldFlags::None) noexcept
    {
        reflection::SchemaField field =
            reflection::MakeField(name, elementType, reflection::ValueKind::Array, offset, sizeof(containers::DynamicArray<Element>),
                                  alignof(containers::DynamicArray<Element>), introducedVersion, removedVersion, flags);
        field.elementKind = elementKind;
        field.arrayOperations = &DynamicArrayAdapter<Element>::Operations;
        return field;
    }

    // Object field data is serialized in stable field-ID order. Missing
    // fields retain the caller's preinitialized defaults; unknown fields are
    // skipped using their explicit byte ranges.
    [[nodiscard]] Result WriteObject(serialization::BinaryWriter& writer, const reflection::Schema& schema, const void* object,
                                     const WriteOptions& options = {}) noexcept;

    // Deserialization writes into an already constructed object. Load into a
    // staging/default object when transactional publication is required.
    [[nodiscard]] Result ReadObject(serialization::BinaryReader& reader, const reflection::Schema& schema, void* object,
                                    const ReadLimits& limits = {}, ReadInfo* info = nullptr) noexcept;

    [[nodiscard]] Result VisitDependencies(const reflection::Schema& schema, const void* object, DependencyVisitor visitor,
                                           void* userData = nullptr, u32 maximumNestingDepth = 32) noexcept;
} // namespace vanguard::schemas
