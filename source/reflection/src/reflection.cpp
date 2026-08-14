#include <vanguard/reflection/reflection_backend.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;

    struct SchemaRegistry
    {
        SchemaRegistry() noexcept : schemas(memory::pools::Reflection::GetInstance()) {}

        concurrency::RWLock lock;
        containers::HashMap<reflection::SchemaTypeId, const reflection::Schema*> schemas;
    };

    SchemaRegistry* g_schemas = nullptr;

    [[nodiscard]] bool IsPowerOfTwo(const u32 value) noexcept
    {
        return value != 0 && (value & (value - 1u)) == 0;
    }

    [[nodiscard]] bool ValidateSchema(const reflection::Schema& schema) noexcept
    {
        if (schema.id == reflection::InvalidSchemaTypeId || schema.id != reflection::HashSchemaName(schema.name) || schema.size == 0 ||
            !IsPowerOfTwo(schema.alignment) || schema.alignment > 4096 || schema.currentVersion == 0 ||
            schema.minimumReadableVersion == 0 || schema.minimumReadableVersion > schema.currentVersion ||
            (schema.fieldCount != 0 && schema.fields == nullptr) || schema.fieldCount > 4096)
        {
            return false;
        }

        for (u32 index = 0; index < schema.fieldCount; ++index)
        {
            const reflection::SchemaField& field = schema.fields[index];
            if (field.id == reflection::InvalidSchemaFieldId || field.id != reflection::HashFieldName(field.name) ||
                field.valueType == reflection::InvalidSchemaTypeId || field.size == 0 || !IsPowerOfTwo(field.alignment) ||
                field.alignment > schema.alignment || (field.offset & (field.alignment - 1u)) != 0 || field.offset > schema.size ||
                field.size > schema.size - field.offset || field.introducedVersion == 0 ||
                field.introducedVersion > schema.currentVersion ||
                (field.removedVersion != 0 && field.removedVersion <= field.introducedVersion) ||
                (field.kind == reflection::ValueKind::Array &&
                 (field.arrayOperations == nullptr || !static_cast<bool>(*field.arrayOperations) ||
                  field.elementKind == reflection::ValueKind::Array)) ||
                (field.kind != reflection::ValueKind::Array && field.arrayOperations != nullptr) ||
                (reflection::HasFlag(field.flags, reflection::FieldFlags::Transient) &&
                 reflection::HasFlag(field.flags, reflection::FieldFlags::Required)))
            {
                return false;
            }
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (schema.fields[previous].id == field.id)
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] SchemaRegistry* CreateSchemaRegistry() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Reflection, sizeof(SchemaRegistry), alignof(SchemaRegistry));
        return block ? ::new (block.address) SchemaRegistry() : nullptr;
    }
} // namespace

namespace vanguard::reflection
{
    bool Initialize() noexcept
    {
        if (!backend::Initialize())
        {
            return false;
        }
        if (g_schemas == nullptr)
        {
            g_schemas = CreateSchemaRegistry();
        }
        return g_schemas != nullptr;
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    TypeDescriptor FindType(const char* name) noexcept
    {
        return backend::FindType(name);
    }

    TypeDescriptor FindTypeByHash(const u64 nameHash) noexcept
    {
        return backend::FindTypeByHash(nameHash);
    }

    bool RegisterSchema(const Schema& schema) noexcept
    {
        if (g_schemas == nullptr || !ValidateSchema(schema))
        {
            return false;
        }

        g_schemas->lock.Acquire();
        const bool inserted = g_schemas->schemas.Insert(schema.id, &schema).IsSuccessful();
        g_schemas->lock.Release();
        return inserted;
    }

    bool UnregisterSchema(const SchemaTypeId type) noexcept
    {
        if (g_schemas == nullptr || type == InvalidSchemaTypeId)
        {
            return false;
        }
        g_schemas->lock.Acquire();
        const bool removed = g_schemas->schemas.Remove(type).IsSuccessful();
        g_schemas->lock.Release();
        return removed;
    }

    const Schema* FindSchema(const SchemaTypeId type) noexcept
    {
        if (g_schemas == nullptr || type == InvalidSchemaTypeId)
        {
            return nullptr;
        }
        const Schema* schema = nullptr;
        g_schemas->lock.AcquireShared();
        static_cast<void>(g_schemas->schemas.Find(type, schema));
        g_schemas->lock.ReleaseShared();
        return schema;
    }

    const Schema* FindSchema(const char* const name) noexcept
    {
        return FindSchema(HashSchemaName(name));
    }

    u32 SchemaCount() noexcept
    {
        if (g_schemas == nullptr)
        {
            return 0;
        }
        g_schemas->lock.AcquireShared();
        const u32 count = g_schemas->schemas.Size();
        g_schemas->lock.ReleaseShared();
        return count;
    }
} // namespace vanguard::reflection
