#include <vanguard/schemas/schemas.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/pool.hpp>

#include <algorithm>
#include <cstring>

namespace
{
    using namespace vanguard;

    constexpr u16 KnownObjectFlags =
        static_cast<u16>(schemas::ObjectFlags::Deterministic) | static_cast<u16>(schemas::ObjectFlags::HasEditorData);
    constexpr u16 KnownFieldFlags =
        static_cast<u16>(reflection::FieldFlags::Required) | static_cast<u16>(reflection::FieldFlags::EditorOnly) |
        static_cast<u16>(reflection::FieldFlags::OptionalDependency) | static_cast<u16>(reflection::FieldFlags::SoftDependency);

    void StoreU16(u8* const output, const u16 value) noexcept
    {
        output[0] = static_cast<u8>(value);
        output[1] = static_cast<u8>(value >> 8u);
    }

    void StoreU32(u8* const output, const u32 value) noexcept
    {
        for (u32 index = 0; index < 4; ++index)
        {
            output[index] = static_cast<u8>(value >> (index * 8u));
        }
    }

    void StoreU64(u8* const output, const u64 value) noexcept
    {
        for (u32 index = 0; index < 8; ++index)
        {
            output[index] = static_cast<u8>(value >> (index * 8u));
        }
    }

    [[nodiscard]] u16 LoadU16(const u8* const input) noexcept
    {
        return static_cast<u16>(static_cast<u16>(input[0]) | (static_cast<u16>(input[1]) << 8u));
    }

    [[nodiscard]] u32 LoadU32(const u8* const input) noexcept
    {
        u32 value = 0;
        for (u32 index = 0; index < 4; ++index)
        {
            value |= static_cast<u32>(input[index]) << (index * 8u);
        }
        return value;
    }

    [[nodiscard]] u64 LoadU64(const u8* const input) noexcept
    {
        u64 value = 0;
        for (u32 index = 0; index < 8; ++index)
        {
            value |= static_cast<u64>(input[index]) << (index * 8u);
        }
        return value;
    }

    [[nodiscard]] u64 AlignUp(const u64 value, const u64 alignment) noexcept
    {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    [[nodiscard]] bool WriteRelativeAlignment(vanguard::serialization::BinaryWriter& writer, const u64 origin, const u32 alignment) noexcept
    {
        const u64 relative = writer.Position() - origin;
        const u64 padding = AlignUp(relative, alignment) - relative;
        constexpr u8 zeros[16] = {};
        return padding == 0 || writer.WriteBytes(zeros, static_cast<usize>(padding));
    }

    [[nodiscard]] bool ReadRelativeAlignment(vanguard::serialization::BinaryReader& reader, const u64 origin, const u32 alignment) noexcept
    {
        const u64 relative = reader.Position() - origin;
        u64 padding = AlignUp(relative, alignment) - relative;
        u8 bytes[16];
        while (padding != 0)
        {
            const usize batch = padding > sizeof(bytes) ? sizeof(bytes) : static_cast<usize>(padding);
            if (!reader.ReadBytes(bytes, batch))
            {
                return false;
            }
            for (usize index = 0; index < batch; ++index)
            {
                if (bytes[index] != 0)
                {
                    return false;
                }
            }
            padding -= batch;
        }
        return true;
    }

    [[nodiscard]] schemas::Result ConvertStatus(const vanguard::serialization::Result result) noexcept
    {
        switch (result)
        {
        case vanguard::serialization::Result::Success:
            return schemas::Result::Success;
        case vanguard::serialization::Result::IoFailure:
        case vanguard::serialization::Result::EndOfStream:
            return schemas::Result::IoFailure;
        case vanguard::serialization::Result::InvalidMagic:
            return schemas::Result::InvalidMagic;
        case vanguard::serialization::Result::UnsupportedVersion:
        case vanguard::serialization::Result::UnsupportedByteOrder:
        case vanguard::serialization::Result::UnsupportedHeader:
            return schemas::Result::UnsupportedVersion;
        case vanguard::serialization::Result::IntegrityFailure:
            return schemas::Result::IntegrityFailure;
        case vanguard::serialization::Result::LimitExceeded:
        case vanguard::serialization::Result::Overflow:
            return schemas::Result::LimitExceeded;
        default:
            return schemas::Result::InvalidLayout;
        }
    }

    void EncodeHeader(const schemas::ObjectHeader& header, u8 (&bytes)[schemas::ObjectHeader::WireSize]) noexcept
    {
        std::memset(bytes, 0, sizeof(bytes));
        StoreU32(bytes, schemas::ObjectHeader::Magic);
        bytes[4] = schemas::ObjectHeader::LittleEndian;
        bytes[5] = schemas::ObjectHeader::EncodingVersion;
        StoreU16(bytes + 6, schemas::ObjectHeader::WireSize);
        StoreU64(bytes + 8, header.schema);
        StoreU16(bytes + 16, header.schemaVersion);
        StoreU16(bytes + 18, static_cast<u16>(header.flags));
        StoreU64(bytes + 20, header.objectSize);
        StoreU64(bytes + 28, header.fieldTableOffset);
        StoreU32(bytes + 36, header.fieldCount);
        StoreU32(bytes + 44, vanguard::serialization::Crc32(bytes, 44));
    }

    [[nodiscard]] schemas::Result DecodeHeader(const u8 (&bytes)[schemas::ObjectHeader::WireSize], schemas::ObjectHeader& header) noexcept
    {
        if (LoadU32(bytes) != schemas::ObjectHeader::Magic)
        {
            return schemas::Result::InvalidMagic;
        }
        if (bytes[4] != schemas::ObjectHeader::LittleEndian || bytes[5] != schemas::ObjectHeader::EncodingVersion ||
            LoadU16(bytes + 6) != schemas::ObjectHeader::WireSize)
        {
            return schemas::Result::UnsupportedVersion;
        }
        if (LoadU32(bytes + 40) != 0 || vanguard::serialization::Crc32(bytes, 44) != LoadU32(bytes + 44))
        {
            return schemas::Result::IntegrityFailure;
        }

        header.schema = LoadU64(bytes + 8);
        header.schemaVersion = LoadU16(bytes + 16);
        header.flags = static_cast<schemas::ObjectFlags>(LoadU16(bytes + 18));
        header.objectSize = LoadU64(bytes + 20);
        header.fieldTableOffset = LoadU64(bytes + 28);
        header.fieldCount = LoadU32(bytes + 36);
        if ((static_cast<u16>(header.flags) & ~KnownObjectFlags) != 0)
        {
            return schemas::Result::UnsupportedVersion;
        }
        return schemas::Result::Success;
    }

    void EncodeField(const schemas::FieldRecord& field, u8 (&bytes)[schemas::FieldRecord::WireSize]) noexcept
    {
        std::memset(bytes, 0, sizeof(bytes));
        StoreU64(bytes, field.field);
        StoreU64(bytes + 8, field.valueType);
        StoreU64(bytes + 16, field.dataOffset);
        StoreU64(bytes + 24, field.dataSize);
        bytes[32] = static_cast<u8>(field.kind);
        bytes[33] = field.kind == reflection::ValueKind::Array ? static_cast<u8>(field.elementKind) : 0;
        StoreU16(bytes + 34, static_cast<u16>(field.flags) & KnownFieldFlags);
    }

    [[nodiscard]] schemas::Result DecodeField(const u8 (&bytes)[schemas::FieldRecord::WireSize], schemas::FieldRecord& field) noexcept
    {
        if (LoadU32(bytes + 36) != 0)
        {
            return schemas::Result::InvalidLayout;
        }
        field.field = LoadU64(bytes);
        field.valueType = LoadU64(bytes + 8);
        field.dataOffset = LoadU64(bytes + 16);
        field.dataSize = LoadU64(bytes + 24);
        field.kind = static_cast<reflection::ValueKind>(bytes[32]);
        field.elementKind = static_cast<reflection::ValueKind>(bytes[33]);
        field.flags = static_cast<reflection::FieldFlags>(LoadU16(bytes + 34));
        if (field.field == reflection::InvalidSchemaFieldId || field.valueType == reflection::InvalidSchemaTypeId ||
            bytes[32] > static_cast<u8>(reflection::ValueKind::Blob) ||
            (field.kind == reflection::ValueKind::Array
                 ? bytes[33] > static_cast<u8>(reflection::ValueKind::Blob) || field.elementKind == reflection::ValueKind::Array
                 : bytes[33] != 0) ||
            (static_cast<u16>(field.flags) & ~KnownFieldFlags) != 0)
        {
            return schemas::Result::InvalidLayout;
        }
        return schemas::Result::Success;
    }

    [[nodiscard]] const reflection::SchemaField* FindField(const reflection::Schema& schema, const reflection::SchemaFieldId id) noexcept
    {
        for (u32 index = 0; index < schema.fieldCount; ++index)
        {
            if (schema.fields[index].id == id)
            {
                return &schema.fields[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool ShouldWrite(const reflection::SchemaField& field, const u16 version, const schemas::WriteOptions& options) noexcept
    {
        return field.IsActive(version) && !reflection::HasFlag(field.flags, reflection::FieldFlags::Transient) &&
               (options.includeEditorFields || !reflection::HasFlag(field.flags, reflection::FieldFlags::EditorOnly));
    }

    [[nodiscard]] schemas::Result WriteObjectInternal(vanguard::serialization::BinaryWriter& writer, const reflection::Schema& schema,
                                                      const void* object, const schemas::WriteOptions& options, u32 depth) noexcept;

    [[nodiscard]] schemas::Result ReadObjectInternal(vanguard::serialization::BinaryReader& reader, const reflection::Schema& schema,
                                                     void* object, const schemas::ReadLimits& limits, schemas::ReadInfo* info,
                                                     u32 depth) noexcept;

    [[nodiscard]] schemas::Result WriteValue(vanguard::serialization::BinaryWriter& writer, const reflection::SchemaField& field,
                                             const void* data, const schemas::WriteOptions& options, const u32 depth) noexcept
    {
        bool success = false;
        switch (field.kind)
        {
        case reflection::ValueKind::Bool:
            if (field.size != sizeof(bool) || field.valueType != reflection::builtin::Bool)
            {
                return schemas::Result::FieldMismatch;
            }
            success = writer.WriteBool(*static_cast<const bool*>(data));
            break;
        case reflection::ValueKind::U8:
            success =
                field.size == sizeof(u8) && field.valueType == reflection::builtin::U8 && writer.WriteU8(*static_cast<const u8*>(data));
            break;
        case reflection::ValueKind::U16:
            success =
                field.size == sizeof(u16) && field.valueType == reflection::builtin::U16 && writer.WriteU16(*static_cast<const u16*>(data));
            break;
        case reflection::ValueKind::U32:
            success =
                field.size == sizeof(u32) && field.valueType == reflection::builtin::U32 && writer.WriteU32(*static_cast<const u32*>(data));
            break;
        case reflection::ValueKind::U64:
            success =
                field.size == sizeof(u64) && field.valueType == reflection::builtin::U64 && writer.WriteU64(*static_cast<const u64*>(data));
            break;
        case reflection::ValueKind::I8:
            success =
                field.size == sizeof(i8) && field.valueType == reflection::builtin::I8 && writer.WriteI8(*static_cast<const i8*>(data));
            break;
        case reflection::ValueKind::I16:
            success =
                field.size == sizeof(i16) && field.valueType == reflection::builtin::I16 && writer.WriteI16(*static_cast<const i16*>(data));
            break;
        case reflection::ValueKind::I32:
            success =
                field.size == sizeof(i32) && field.valueType == reflection::builtin::I32 && writer.WriteI32(*static_cast<const i32*>(data));
            break;
        case reflection::ValueKind::I64:
            success =
                field.size == sizeof(i64) && field.valueType == reflection::builtin::I64 && writer.WriteI64(*static_cast<const i64*>(data));
            break;
        case reflection::ValueKind::F32:
            success =
                field.size == sizeof(f32) && field.valueType == reflection::builtin::F32 && writer.WriteF32(*static_cast<const f32*>(data));
            break;
        case reflection::ValueKind::F64:
            success =
                field.size == sizeof(f64) && field.valueType == reflection::builtin::F64 && writer.WriteF64(*static_cast<const f64*>(data));
            break;
        case reflection::ValueKind::Enumeration:
            switch (field.size)
            {
            case 1:
                success = writer.WriteU8(*static_cast<const u8*>(data));
                break;
            case 2:
                success = writer.WriteU16(*static_cast<const u16*>(data));
                break;
            case 4:
                success = writer.WriteU32(*static_cast<const u32*>(data));
                break;
            case 8:
                success = writer.WriteU64(*static_cast<const u64*>(data));
                break;
            default:
                return schemas::Result::FieldMismatch;
            }
            break;
        case reflection::ValueKind::ResourceReference:
        {
            if (field.size != sizeof(resources::ResourceReference) || field.valueType != reflection::builtin::ResourceReference)
            {
                return schemas::Result::FieldMismatch;
            }
            const auto& reference = *static_cast<const resources::ResourceReference*>(data);
            if (reference.IsValid() != reference.IsTyped())
            {
                return schemas::Result::FieldMismatch;
            }
            success = writer.WriteU64(reference.Path().Id()) && writer.WriteU32(reference.ExpectedType());
            break;
        }
        case reflection::ValueKind::String:
        {
            if (field.size != sizeof(containers::String) || field.valueType != reflection::builtin::String)
            {
                return schemas::Result::FieldMismatch;
            }
            const auto& value = *static_cast<const containers::String*>(data);
            if (value.Length() > options.maximumStringBytes)
            {
                return schemas::Result::LimitExceeded;
            }
            success = writer.WriteVarUInt(value.Length()) && writer.WriteBytes(value.AsChar(), value.Length());
            break;
        }
        case reflection::ValueKind::Structure:
        {
            const reflection::Schema* const nested = reflection::FindSchema(field.valueType);
            if (nested == nullptr)
            {
                return schemas::Result::SchemaNotFound;
            }
            if (nested->size != field.size)
            {
                return schemas::Result::FieldMismatch;
            }
            return WriteObjectInternal(writer, *nested, data, options, depth + 1u);
        }
        case reflection::ValueKind::Blob:
            success = writer.WriteBytes(data, field.size);
            break;
        case reflection::ValueKind::Array:
        {
            if (field.arrayOperations == nullptr || !static_cast<bool>(*field.arrayOperations) ||
                field.elementKind == reflection::ValueKind::Array)
            {
                return schemas::Result::FieldMismatch;
            }
            const u32 count = field.arrayOperations->size(data);
            if (count > options.maximumArrayElements || !writer.WriteVarUInt(count))
            {
                return count > options.maximumArrayElements ? schemas::Result::LimitExceeded : ConvertStatus(writer.Status());
            }
            reflection::SchemaField element;
            element.valueType = field.valueType;
            element.kind = field.elementKind;
            element.size = field.arrayOperations->elementSize;
            element.alignment = field.arrayOperations->elementAlignment;
            for (u32 index = 0; index < count; ++index)
            {
                const void* const value = field.arrayOperations->constElement(data, index);
                if (value == nullptr)
                {
                    return schemas::Result::FieldMismatch;
                }
                const schemas::Result result = WriteValue(writer, element, value, options, depth);
                if (result != schemas::Result::Success)
                {
                    return result;
                }
            }
            return schemas::Result::Success;
        }
        }
        return success ? schemas::Result::Success : writer.Good() ? schemas::Result::FieldMismatch : ConvertStatus(writer.Status());
    }

    [[nodiscard]] schemas::Result ReadValue(vanguard::serialization::BinaryReader& reader, const reflection::SchemaField& field, void* data,
                                            const schemas::ReadLimits& limits, schemas::ReadInfo* info, const u32 depth) noexcept
    {
        bool success = false;
        switch (field.kind)
        {
        case reflection::ValueKind::Bool:
            if (field.size != sizeof(bool) || field.valueType != reflection::builtin::Bool)
            {
                return schemas::Result::FieldMismatch;
            }
            success = reader.ReadBool(*static_cast<bool*>(data));
            break;
        case reflection::ValueKind::U8:
            success = field.size == sizeof(u8) && field.valueType == reflection::builtin::U8 && reader.ReadU8(*static_cast<u8*>(data));
            break;
        case reflection::ValueKind::U16:
            success = field.size == sizeof(u16) && field.valueType == reflection::builtin::U16 && reader.ReadU16(*static_cast<u16*>(data));
            break;
        case reflection::ValueKind::U32:
            success = field.size == sizeof(u32) && field.valueType == reflection::builtin::U32 && reader.ReadU32(*static_cast<u32*>(data));
            break;
        case reflection::ValueKind::U64:
            success = field.size == sizeof(u64) && field.valueType == reflection::builtin::U64 && reader.ReadU64(*static_cast<u64*>(data));
            break;
        case reflection::ValueKind::I8:
            success = field.size == sizeof(i8) && field.valueType == reflection::builtin::I8 && reader.ReadI8(*static_cast<i8*>(data));
            break;
        case reflection::ValueKind::I16:
            success = field.size == sizeof(i16) && field.valueType == reflection::builtin::I16 && reader.ReadI16(*static_cast<i16*>(data));
            break;
        case reflection::ValueKind::I32:
            success = field.size == sizeof(i32) && field.valueType == reflection::builtin::I32 && reader.ReadI32(*static_cast<i32*>(data));
            break;
        case reflection::ValueKind::I64:
            success = field.size == sizeof(i64) && field.valueType == reflection::builtin::I64 && reader.ReadI64(*static_cast<i64*>(data));
            break;
        case reflection::ValueKind::F32:
            success = field.size == sizeof(f32) && field.valueType == reflection::builtin::F32 && reader.ReadF32(*static_cast<f32*>(data));
            break;
        case reflection::ValueKind::F64:
            success = field.size == sizeof(f64) && field.valueType == reflection::builtin::F64 && reader.ReadF64(*static_cast<f64*>(data));
            break;
        case reflection::ValueKind::Enumeration:
            switch (field.size)
            {
            case 1:
                success = reader.ReadU8(*static_cast<u8*>(data));
                break;
            case 2:
                success = reader.ReadU16(*static_cast<u16*>(data));
                break;
            case 4:
                success = reader.ReadU32(*static_cast<u32*>(data));
                break;
            case 8:
                success = reader.ReadU64(*static_cast<u64*>(data));
                break;
            default:
                return schemas::Result::FieldMismatch;
            }
            break;
        case reflection::ValueKind::ResourceReference:
        {
            if (field.size != sizeof(resources::ResourceReference) || field.valueType != reflection::builtin::ResourceReference)
            {
                return schemas::Result::FieldMismatch;
            }
            u64 path = 0;
            u32 type = 0;
            success = reader.ReadU64(path) && reader.ReadU32(type);
            if (success)
            {
                const resources::ResourceReference reference(resources::ResourcePath::FromId(path), type);
                if (reference.IsValid() != reference.IsTyped())
                {
                    return schemas::Result::FieldMismatch;
                }
                *static_cast<resources::ResourceReference*>(data) = reference;
            }
            break;
        }
        case reflection::ValueKind::String:
        {
            if (field.size != sizeof(containers::String) || field.valueType != reflection::builtin::String)
            {
                return schemas::Result::FieldMismatch;
            }
            u64 length = 0;
            if (!reader.ReadVarUInt(length))
            {
                return ConvertStatus(reader.Status());
            }
            if (length > limits.maximumStringBytes || length > static_cast<u64>(~u32{0}))
            {
                return schemas::Result::LimitExceeded;
            }
            auto& value = *static_cast<containers::String*>(data);
            if (!value.Resize(static_cast<u32>(length)))
            {
                return schemas::Result::OutOfMemory;
            }
            success = reader.ReadBytes(value.AsChar(), static_cast<usize>(length));
            break;
        }
        case reflection::ValueKind::Structure:
        {
            const reflection::Schema* const nested = reflection::FindSchema(field.valueType);
            if (nested == nullptr)
            {
                return schemas::Result::SchemaNotFound;
            }
            if (nested->size != field.size)
            {
                return schemas::Result::FieldMismatch;
            }
            return ReadObjectInternal(reader, *nested, data, limits, info, depth + 1u);
        }
        case reflection::ValueKind::Blob:
            success = reader.ReadBytes(data, field.size);
            break;
        case reflection::ValueKind::Array:
        {
            if (field.arrayOperations == nullptr || !static_cast<bool>(*field.arrayOperations) ||
                field.elementKind == reflection::ValueKind::Array)
            {
                return schemas::Result::FieldMismatch;
            }
            u64 count64 = 0;
            if (!reader.ReadVarUInt(count64))
            {
                return ConvertStatus(reader.Status());
            }
            if (count64 > limits.maximumArrayElements || count64 > static_cast<u64>(~u32{0}))
            {
                return schemas::Result::LimitExceeded;
            }
            const u32 count = static_cast<u32>(count64);
            if (field.arrayOperations->elementSize > limits.maximumObjectBytes ||
                (count != 0 && count > limits.maximumObjectBytes / field.arrayOperations->elementSize))
            {
                return schemas::Result::LimitExceeded;
            }
            if (!field.arrayOperations->resize(data, count))
            {
                return schemas::Result::OutOfMemory;
            }
            reflection::SchemaField element;
            element.valueType = field.valueType;
            element.kind = field.elementKind;
            element.size = field.arrayOperations->elementSize;
            element.alignment = field.arrayOperations->elementAlignment;
            for (u32 index = 0; index < count; ++index)
            {
                void* const value = field.arrayOperations->element(data, index);
                if (value == nullptr)
                {
                    return schemas::Result::FieldMismatch;
                }
                const schemas::Result result = ReadValue(reader, element, value, limits, info, depth);
                if (result != schemas::Result::Success)
                {
                    return result;
                }
            }
            return schemas::Result::Success;
        }
        }
        return success ? schemas::Result::Success : reader.Good() ? schemas::Result::FieldMismatch : ConvertStatus(reader.Status());
    }

    [[nodiscard]] schemas::Result WriteObjectInternal(vanguard::serialization::BinaryWriter& writer, const reflection::Schema& schema,
                                                      const void* const object, const schemas::WriteOptions& options,
                                                      const u32 depth) noexcept
    {
        if (object == nullptr || schema.id == reflection::InvalidSchemaTypeId || options.maximumFields == 0 ||
            options.maximumObjectBytes < schemas::ObjectHeader::WireSize || depth > options.maximumNestingDepth)
        {
            return depth > options.maximumNestingDepth ? schemas::Result::LimitExceeded : schemas::Result::InvalidArgument;
        }

        containers::DynamicArray<const reflection::SchemaField*> fields{memory::pools::Serialization::GetInstance()};
        fields.Reserve(schema.fieldCount);
        for (u32 index = 0; index < schema.fieldCount; ++index)
        {
            if (ShouldWrite(schema.fields[index], schema.currentVersion, options))
            {
                fields.PushBack(&schema.fields[index]);
            }
        }
        if (fields.Size() > options.maximumFields)
        {
            return schemas::Result::LimitExceeded;
        }
        std::sort(fields.Begin(), fields.End(), [](const reflection::SchemaField* const left, const reflection::SchemaField* const right)
                  { return left->id < right->id; });

        const u64 origin = writer.Position();
        schemas::ObjectHeader header;
        header.schema = schema.id;
        header.schemaVersion = schema.currentVersion;
        header.flags = schemas::ObjectFlags::Deterministic |
                       (options.includeEditorFields ? schemas::ObjectFlags::HasEditorData : schemas::ObjectFlags::None);
        header.fieldCount = fields.Size();

        u8 headerBytes[schemas::ObjectHeader::WireSize] = {};
        if (!writer.WriteBytes(headerBytes, sizeof(headerBytes)))
        {
            return ConvertStatus(writer.Status());
        }
        constexpr u8 zeros[256] = {};
        u64 tableBytes = static_cast<u64>(fields.Size()) * schemas::FieldRecord::WireSize;
        while (tableBytes != 0)
        {
            const usize batch = tableBytes > sizeof(zeros) ? sizeof(zeros) : static_cast<usize>(tableBytes);
            if (!writer.WriteBytes(zeros, batch))
            {
                return ConvertStatus(writer.Status());
            }
            tableBytes -= batch;
        }

        for (u32 index = 0; index < fields.Size(); ++index)
        {
            if (!WriteRelativeAlignment(writer, origin, 8))
            {
                return ConvertStatus(writer.Status());
            }
            const reflection::SchemaField& field = *fields[index];
            const u64 fieldBegin = writer.Position();
            const auto* const objectBytes = static_cast<const u8*>(object);
            const schemas::Result result = WriteValue(writer, field, objectBytes + field.offset, options, depth);
            if (result != schemas::Result::Success)
            {
                return result;
            }
            if (writer.Position() - origin > options.maximumObjectBytes)
            {
                return schemas::Result::LimitExceeded;
            }

            schemas::FieldRecord record;
            record.field = field.id;
            record.valueType = field.valueType;
            record.dataOffset = fieldBegin - origin;
            record.dataSize = writer.Position() - fieldBegin;
            record.kind = field.kind;
            record.elementKind = field.elementKind;
            record.flags = field.flags;
            u8 recordBytes[schemas::FieldRecord::WireSize];
            EncodeField(record, recordBytes);
            if (!writer.WriteBytesAt(origin + schemas::ObjectHeader::WireSize + static_cast<u64>(index) * schemas::FieldRecord::WireSize,
                                     recordBytes, sizeof(recordBytes)))
            {
                return ConvertStatus(writer.Status());
            }
        }

        header.objectSize = writer.Position() - origin;
        if (header.objectSize > options.maximumObjectBytes)
        {
            return schemas::Result::LimitExceeded;
        }
        EncodeHeader(header, headerBytes);
        return writer.WriteBytesAt(origin, headerBytes, sizeof(headerBytes)) ? schemas::Result::Success : ConvertStatus(writer.Status());
    }

    [[nodiscard]] schemas::Result ReadObjectInternal(vanguard::serialization::BinaryReader& reader, const reflection::Schema& schema,
                                                     void* const object, const schemas::ReadLimits& limits, schemas::ReadInfo* const info,
                                                     const u32 depth) noexcept
    {
        if (object == nullptr || schema.id == reflection::InvalidSchemaTypeId || limits.maximumFields == 0 ||
            limits.maximumObjectBytes < schemas::ObjectHeader::WireSize || depth > limits.maximumNestingDepth)
        {
            return depth > limits.maximumNestingDepth ? schemas::Result::LimitExceeded : schemas::Result::InvalidArgument;
        }

        const u64 origin = reader.Position();
        u8 headerBytes[schemas::ObjectHeader::WireSize];
        if (!reader.ReadBytes(headerBytes, sizeof(headerBytes)))
        {
            return ConvertStatus(reader.Status());
        }
        schemas::ObjectHeader header;
        schemas::Result result = DecodeHeader(headerBytes, header);
        if (result != schemas::Result::Success)
        {
            return result;
        }
        if (header.schema != schema.id)
        {
            return schemas::Result::SchemaMismatch;
        }
        if (header.schemaVersion < schema.minimumReadableVersion || header.schemaVersion > schema.currentVersion)
        {
            return schemas::Result::UnsupportedVersion;
        }
        if (header.fieldCount > limits.maximumFields || header.objectSize > limits.maximumObjectBytes)
        {
            return schemas::Result::LimitExceeded;
        }
        if (header.fieldTableOffset != schemas::ObjectHeader::WireSize || origin > reader.Size() ||
            header.objectSize > reader.Size() - origin)
        {
            return schemas::Result::InvalidLayout;
        }
        const u64 tableBytes = static_cast<u64>(header.fieldCount) * schemas::FieldRecord::WireSize;
        if (tableBytes > header.objectSize - header.fieldTableOffset)
        {
            return schemas::Result::InvalidLayout;
        }

        containers::DynamicArray<schemas::FieldRecord> records{memory::pools::Serialization::GetInstance()};
        records.Reserve(header.fieldCount);
        for (u32 index = 0; index < header.fieldCount; ++index)
        {
            u8 recordBytes[schemas::FieldRecord::WireSize];
            if (!reader.ReadBytes(recordBytes, sizeof(recordBytes)))
            {
                return ConvertStatus(reader.Status());
            }
            schemas::FieldRecord record;
            result = DecodeField(recordBytes, record);
            if (result != schemas::Result::Success)
            {
                return result;
            }
            if (index != 0 && records.Back().field >= record.field)
            {
                return schemas::Result::InvalidLayout;
            }
            records.PushBack(record);
        }

        u64 expectedOffset = schemas::ObjectHeader::WireSize + tableBytes;
        for (const schemas::FieldRecord& record : records)
        {
            expectedOffset = AlignUp(expectedOffset, 8);
            if (record.dataOffset != expectedOffset || record.dataOffset > header.objectSize ||
                record.dataSize > header.objectSize - record.dataOffset)
            {
                return schemas::Result::InvalidLayout;
            }
            expectedOffset = record.dataOffset + record.dataSize;

            const reflection::SchemaField* const field = FindField(schema, record.field);
            if (field != nullptr)
            {
                if (!field->IsActive(header.schemaVersion) || reflection::HasFlag(field->flags, reflection::FieldFlags::Transient) ||
                    field->valueType != record.valueType || field->kind != record.kind ||
                    (field->kind == reflection::ValueKind::Array && field->elementKind != record.elementKind))
                {
                    return schemas::Result::FieldMismatch;
                }
            }
        }
        if (expectedOffset != header.objectSize)
        {
            return schemas::Result::InvalidLayout;
        }

        for (u32 fieldIndex = 0; fieldIndex < schema.fieldCount; ++fieldIndex)
        {
            const reflection::SchemaField& field = schema.fields[fieldIndex];
            if (!field.IsActive(header.schemaVersion) || !reflection::HasFlag(field.flags, reflection::FieldFlags::Required) ||
                reflection::HasFlag(field.flags, reflection::FieldFlags::Transient) ||
                (reflection::HasFlag(field.flags, reflection::FieldFlags::EditorOnly) &&
                 (static_cast<u16>(header.flags) & static_cast<u16>(schemas::ObjectFlags::HasEditorData)) == 0))
            {
                continue;
            }
            const auto found =
                std::lower_bound(records.Begin(), records.End(), field.id,
                                 [](const schemas::FieldRecord& record, const reflection::SchemaFieldId id) { return record.field < id; });
            if (found == records.End() || found->field != field.id)
            {
                return schemas::Result::MissingRequiredField;
            }
        }

        for (const schemas::FieldRecord& record : records)
        {
            if (!ReadRelativeAlignment(reader, origin, 8) || reader.Position() != origin + record.dataOffset)
            {
                return reader.Good() ? schemas::Result::InvalidLayout : ConvertStatus(reader.Status());
            }
            const reflection::SchemaField* const field = FindField(schema, record.field);
            if (field == nullptr)
            {
                if (!reader.Skip(record.dataSize))
                {
                    return ConvertStatus(reader.Status());
                }
                if (info != nullptr)
                {
                    ++info->unknownFieldsSkipped;
                }
                continue;
            }

            const u64 fieldBegin = reader.Position();
            auto* const objectBytes = static_cast<u8*>(object);
            result = ReadValue(reader, *field, objectBytes + field->offset, limits, info, depth);
            if (result != schemas::Result::Success)
            {
                return result;
            }
            if (reader.Position() - fieldBegin != record.dataSize)
            {
                return schemas::Result::FieldMismatch;
            }
            if (info != nullptr)
            {
                ++info->fieldsRead;
            }
        }

        if (reader.Position() != origin + header.objectSize && !reader.Seek(origin + header.objectSize))
        {
            return ConvertStatus(reader.Status());
        }
        if (info != nullptr && depth == 0)
        {
            info->sourceSchemaVersion = header.schemaVersion;
            info->sourceFlags = header.flags;
        }
        return schemas::Result::Success;
    }

    [[nodiscard]] schemas::Result VisitDependenciesInternal(const reflection::Schema& schema, const void* const object,
                                                            const schemas::DependencyVisitor visitor, void* const userData,
                                                            const u32 maximumDepth, const u32 depth) noexcept
    {
        if (depth > maximumDepth)
        {
            return schemas::Result::LimitExceeded;
        }
        const auto* const objectBytes = static_cast<const u8*>(object);
        for (u32 index = 0; index < schema.fieldCount; ++index)
        {
            const reflection::SchemaField& field = schema.fields[index];
            if (!field.IsActive(schema.currentVersion) || reflection::HasFlag(field.flags, reflection::FieldFlags::Transient))
            {
                continue;
            }
            const void* const data = objectBytes + field.offset;
            if (field.kind == reflection::ValueKind::ResourceReference)
            {
                const auto& reference = *static_cast<const resources::ResourceReference*>(data);
                if (!reference.IsValid())
                {
                    if (reference.IsTyped())
                    {
                        return schemas::Result::FieldMismatch;
                    }
                    continue;
                }
                if (!reference.IsTyped())
                {
                    return schemas::Result::FieldMismatch;
                }
                schemas::DependencyKind kind = schemas::DependencyKind::Required;
                if (reflection::HasFlag(field.flags, reflection::FieldFlags::SoftDependency))
                {
                    kind = schemas::DependencyKind::Soft;
                }
                else if (reflection::HasFlag(field.flags, reflection::FieldFlags::OptionalDependency))
                {
                    kind = schemas::DependencyKind::Optional;
                }
                if (!visitor(reference, kind, userData))
                {
                    return schemas::Result::LimitExceeded;
                }
            }
            else if (field.kind == reflection::ValueKind::Structure)
            {
                const reflection::Schema* const nested = reflection::FindSchema(field.valueType);
                if (nested == nullptr)
                {
                    return schemas::Result::SchemaNotFound;
                }
                const schemas::Result result = VisitDependenciesInternal(*nested, data, visitor, userData, maximumDepth, depth + 1u);
                if (result != schemas::Result::Success)
                {
                    return result;
                }
            }
            else if (field.kind == reflection::ValueKind::Array)
            {
                if (field.arrayOperations == nullptr || !static_cast<bool>(*field.arrayOperations) ||
                    field.elementKind == reflection::ValueKind::Array)
                {
                    return schemas::Result::FieldMismatch;
                }
                const u32 count = field.arrayOperations->size(data);
                reflection::SchemaField element;
                element.valueType = field.valueType;
                element.kind = field.elementKind;
                element.size = field.arrayOperations->elementSize;
                element.alignment = field.arrayOperations->elementAlignment;
                for (u32 elementIndex = 0; elementIndex < count; ++elementIndex)
                {
                    const void* const value = field.arrayOperations->constElement(data, elementIndex);
                    if (value == nullptr)
                    {
                        return schemas::Result::FieldMismatch;
                    }
                    if (element.kind == reflection::ValueKind::ResourceReference)
                    {
                        const auto& reference = *static_cast<const resources::ResourceReference*>(value);
                        if (!reference.IsValid())
                        {
                            if (reference.IsTyped())
                            {
                                return schemas::Result::FieldMismatch;
                            }
                            continue;
                        }
                        if (!reference.IsTyped())
                        {
                            return schemas::Result::FieldMismatch;
                        }
                        schemas::DependencyKind kind = schemas::DependencyKind::Required;
                        if (reflection::HasFlag(field.flags, reflection::FieldFlags::SoftDependency))
                        {
                            kind = schemas::DependencyKind::Soft;
                        }
                        else if (reflection::HasFlag(field.flags, reflection::FieldFlags::OptionalDependency))
                        {
                            kind = schemas::DependencyKind::Optional;
                        }
                        if (!visitor(reference, kind, userData))
                        {
                            return schemas::Result::LimitExceeded;
                        }
                    }
                    else if (element.kind == reflection::ValueKind::Structure)
                    {
                        const reflection::Schema* const nested = reflection::FindSchema(element.valueType);
                        if (nested == nullptr)
                        {
                            return schemas::Result::SchemaNotFound;
                        }
                        const schemas::Result result =
                            VisitDependenciesInternal(*nested, value, visitor, userData, maximumDepth, depth + 1u);
                        if (result != schemas::Result::Success)
                        {
                            return result;
                        }
                    }
                }
            }
        }
        return schemas::Result::Success;
    }
} // namespace

namespace vanguard::schemas
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::IoFailure:
            return "IoFailure";
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
        case Result::SchemaNotFound:
            return "SchemaNotFound";
        case Result::SchemaMismatch:
            return "SchemaMismatch";
        case Result::FieldMismatch:
            return "FieldMismatch";
        case Result::MissingRequiredField:
            return "MissingRequiredField";
        case Result::UnsupportedValueKind:
            return "UnsupportedValueKind";
        case Result::OutOfMemory:
            return "OutOfMemory";
        }
        return "Unknown";
    }

    Result WriteObject(serialization::BinaryWriter& writer, const reflection::Schema& schema, const void* const object,
                       const WriteOptions& options) noexcept
    {
        return WriteObjectInternal(writer, schema, object, options, 0);
    }

    Result ReadObject(serialization::BinaryReader& reader, const reflection::Schema& schema, void* const object, const ReadLimits& limits,
                      ReadInfo* const info) noexcept
    {
        if (info != nullptr)
        {
            *info = {};
        }
        return ReadObjectInternal(reader, schema, object, limits, info, 0);
    }

    Result VisitDependencies(const reflection::Schema& schema, const void* const object, const DependencyVisitor visitor,
                             void* const userData, const u32 maximumNestingDepth) noexcept
    {
        if (object == nullptr || visitor == nullptr || maximumNestingDepth == 0)
        {
            return Result::InvalidArgument;
        }
        return VisitDependenciesInternal(schema, object, visitor, userData, maximumNestingDepth, 0);
    }
} // namespace vanguard::schemas
