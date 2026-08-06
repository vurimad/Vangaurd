#include <vanguard/entities/component_registry.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <new>

namespace
{
    using namespace vanguard;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateEntityObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Gameplay, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template<typename Type>
    void DeleteEntityObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Gameplay};
        memory::Free(block);
    }
} // namespace

namespace vanguard::entities
{
    struct ComponentRegistry::Impl
    {
        struct Descriptor
        {
            const reflection::Schema* schema = nullptr;
            ecs::ComponentId runtimeComponent = ecs::InvalidComponentId;
            const ecs_world_t* world = nullptr;
            ConstructFunction construct = nullptr;
            DestroyFunction destroy = nullptr;
            QueueFunction queue = nullptr;
        };

        explicit Impl(ecs::World& target) noexcept
            : world(&target), descriptors(memory::pools::Gameplay::GetInstance())
        {
            descriptors.Reserve(256);
        }

        ecs::World* world = nullptr;
        containers::HashMap<reflection::SchemaTypeId, Descriptor> descriptors;
        bool sealed = false;
    };

    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success: return "Success";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::InvalidState: return "InvalidState";
        case Result::DuplicateIdentifier: return "DuplicateIdentifier";
        case Result::UnknownSchema: return "UnknownSchema";
        case Result::UnsupportedSchemaVersion: return "UnsupportedSchemaVersion";
        case Result::SchemaFailure: return "SchemaFailure";
        case Result::MissingPrefab: return "MissingPrefab";
        case Result::InvalidPrefab: return "InvalidPrefab";
        case Result::MissingOverrideTarget: return "MissingOverrideTarget";
        case Result::DuplicateEntity: return "DuplicateEntity";
        case Result::StaleGeneration: return "StaleGeneration";
        case Result::QueueFailure: return "QueueFailure";
        case Result::InvalidReference: return "InvalidReference";
        case Result::NotReady: return "NotReady";
        case Result::LimitExceeded: return "LimitExceeded";
        case Result::OutOfMemory: return "OutOfMemory";
        }
        return "Unknown";
    }

    DecodedComponent::DecodedComponent(DecodedComponent&& other) noexcept
        : m_value(other.m_value), m_schema(other.m_schema), m_runtimeComponent(other.m_runtimeComponent),
          m_world(other.m_world), m_sourceVersion(other.m_sourceVersion), m_destroy(other.m_destroy), m_queue(other.m_queue)
    {
        other.m_value = {};
        other.m_schema = reflection::InvalidSchemaTypeId;
        other.m_runtimeComponent = ecs::InvalidComponentId;
        other.m_world = nullptr;
        other.m_sourceVersion = 0;
        other.m_destroy = nullptr;
        other.m_queue = nullptr;
    }

    DecodedComponent::~DecodedComponent() { Reset(); }

    DecodedComponent& DecodedComponent::operator=(DecodedComponent&& other) noexcept
    {
        if (this == &other) return *this;
        Reset();
        m_value = other.m_value;
        m_schema = other.m_schema;
        m_runtimeComponent = other.m_runtimeComponent;
        m_world = other.m_world;
        m_sourceVersion = other.m_sourceVersion;
        m_destroy = other.m_destroy;
        m_queue = other.m_queue;
        other.m_value = {};
        other.m_schema = reflection::InvalidSchemaTypeId;
        other.m_runtimeComponent = ecs::InvalidComponentId;
        other.m_world = nullptr;
        other.m_sourceVersion = 0;
        other.m_destroy = nullptr;
        other.m_queue = nullptr;
        return *this;
    }

    bool DecodedComponent::IsValid() const noexcept
    {
        return m_value && m_schema != reflection::InvalidSchemaTypeId &&
               m_runtimeComponent != ecs::InvalidComponentId && m_world != nullptr && m_destroy != nullptr && m_queue != nullptr;
    }

    reflection::SchemaTypeId DecodedComponent::Schema() const noexcept { return m_schema; }
    u16 DecodedComponent::SourceVersion() const noexcept { return m_sourceVersion; }

    void DecodedComponent::Reset() noexcept
    {
        if (m_value)
        {
            if (m_destroy != nullptr) m_destroy(m_value.address);
            memory::Free(m_value);
        }
        m_schema = reflection::InvalidSchemaTypeId;
        m_runtimeComponent = ecs::InvalidComponentId;
        m_world = nullptr;
        m_sourceVersion = 0;
        m_destroy = nullptr;
        m_queue = nullptr;
    }

    ComponentRegistry::~ComponentRegistry()
    {
        DeleteEntityObject(m_impl);
        m_impl = nullptr;
    }

    bool ComponentRegistry::Initialize(ecs::World& world) noexcept
    {
        if (m_impl != nullptr || !world.IsInitialized() || !reflection::IsInitialized()) return false;
        m_impl = AllocateEntityObject<Impl>(world);
        return m_impl != nullptr;
    }

    bool ComponentRegistry::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        DeleteEntityObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ComponentRegistry::IsInitialized() const noexcept { return m_impl != nullptr; }
    bool ComponentRegistry::IsSealed() const noexcept { return m_impl != nullptr && m_impl->sealed; }

    bool ComponentRegistry::Seal() noexcept
    {
        if (m_impl == nullptr || m_impl->descriptors.Empty()) return false;
        m_impl->sealed = true;
        return true;
    }

    bool ComponentRegistry::RegisterDescriptor(const reflection::Schema& schema,
                                               const Registration& registration) noexcept
    {
        if (m_impl == nullptr || m_impl->sealed || !schema || reflection::FindSchema(schema.id) != &schema ||
            registration.runtimeComponent == ecs::InvalidComponentId || registration.world != m_impl->world->Native() ||
            registration.objectSize != schema.size || registration.objectAlignment != schema.alignment ||
            registration.construct == nullptr || registration.destroy == nullptr || registration.queue == nullptr ||
            schema.size == 0 || schema.alignment == 0 || HasSchema(schema.id)) return false;
        const Impl::Descriptor descriptor{&schema, registration.runtimeComponent, registration.world,
                                          registration.construct, registration.destroy, registration.queue};
        return m_impl->descriptors.Insert(schema.id, descriptor).IsSuccessful();
    }

    Result ComponentRegistry::Decode(const reflection::SchemaTypeId schemaId, const u16 sourceVersion,
                                     const containers::ArraySpan<const u8> data, DecodedComponent& output) const noexcept
    {
        output.Reset();
        if (m_impl == nullptr || schemaId == reflection::InvalidSchemaTypeId || sourceVersion == 0 || data.Empty())
            return Result::InvalidArgument;
        if (!m_impl->sealed) return Result::InvalidState;
        Impl::Descriptor descriptor;
        if (!m_impl->descriptors.Find(schemaId, descriptor) || descriptor.schema == nullptr) return Result::UnknownSchema;
        if (sourceVersion < descriptor.schema->minimumReadableVersion || sourceVersion > descriptor.schema->currentVersion)
            return Result::UnsupportedSchemaVersion;
        memory::MemoryBlock value = memory::Allocate(memory::PoolId::Gameplay, descriptor.schema->size,
                                                     descriptor.schema->alignment);
        if (!value) return Result::OutOfMemory;
        descriptor.construct(value.address);
        filesystem::MemoryFileReader source(data.Data(), data.Size(), 0);
        serialization::BinaryReader reader(source);
        schemas::ReadInfo readInfo;
        const schemas::Result readResult = schemas::ReadObject(reader, *descriptor.schema, value.address, {}, &readInfo);
        if (readResult != schemas::Result::Success || readInfo.sourceSchemaVersion != sourceVersion)
        {
            descriptor.destroy(value.address);
            memory::Free(value);
            return readResult == schemas::Result::UnsupportedVersion ? Result::UnsupportedSchemaVersion : Result::SchemaFailure;
        }
        output.m_value = value;
        output.m_schema = schemaId;
        output.m_runtimeComponent = descriptor.runtimeComponent;
        output.m_world = descriptor.world;
        output.m_sourceVersion = sourceVersion;
        output.m_destroy = descriptor.destroy;
        output.m_queue = descriptor.queue;
        return Result::Success;
    }

    bool ComponentRegistry::Queue(const ecs::CommandBatch batch, const ecs::EntityId entity,
                                  const DecodedComponent& component, const bool enabled) const noexcept
    {
        return m_impl != nullptr && batch && batch.world == m_impl->world->Native() && component.IsValid() &&
               component.m_world == m_impl->world->Native() &&
               component.m_queue(*m_impl->world, batch, entity, component.m_runtimeComponent, component.m_world,
                                 component.m_value.address, enabled);
    }

    bool ComponentRegistry::HasSchema(const reflection::SchemaTypeId schema) const noexcept
    {
        if (m_impl == nullptr || schema == reflection::InvalidSchemaTypeId) return false;
        Impl::Descriptor descriptor;
        return m_impl->descriptors.Find(schema, descriptor);
    }

    ecs::ComponentId ComponentRegistry::RuntimeComponent(const reflection::SchemaTypeId schema) const noexcept
    {
        if (m_impl == nullptr || schema == reflection::InvalidSchemaTypeId) return ecs::InvalidComponentId;
        Impl::Descriptor descriptor;
        return m_impl->descriptors.Find(schema, descriptor) ? descriptor.runtimeComponent : ecs::InvalidComponentId;
    }

    u32 ComponentRegistry::Count() const noexcept { return m_impl != nullptr ? m_impl->descriptors.Size() : 0; }
    ecs::World* ComponentRegistry::RegisteredWorld() const noexcept { return m_impl != nullptr ? m_impl->world : nullptr; }
} // namespace vanguard::entities
