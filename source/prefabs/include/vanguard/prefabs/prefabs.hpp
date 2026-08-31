#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/schemas/schemas.hpp>

namespace vanguard::prefabs
{
    inline constexpr u32 PrefabMagic = serialization::MakeFourCC('V', 'P', 'F', 'B');
    inline constexpr resources::ResourceTypeId PrefabResourceType = serialization::MakeFourCC('V', 'P', 'F', 'B');
    inline constexpr u64 InvalidStableId = 0;

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        DuplicateIdentifier,
        MissingParent,
        HierarchyCycle,
        UnknownSchema,
        SchemaFailure,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class EntityFlags : u16
    {
        None = 0,
        Root = 1u << 0u,
        DisabledByDefault = 1u << 1u,
        EditorOnly = 1u << 2u
    };

    enum class ComponentFlags : u16
    {
        None = 0,
        DisabledByDefault = 1u << 0u,
        EditorOnly = 1u << 1u
    };

    [[nodiscard]] constexpr EntityFlags operator|(EntityFlags left, EntityFlags right) noexcept
    {
        return static_cast<EntityFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr ComponentFlags operator|(ComponentFlags left, ComponentFlags right) noexcept
    {
        return static_cast<ComponentFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    /// One entity within a reusable prefab hierarchy. Stable IDs are authored identities,
    /// never runtime ECS identifiers. A root has parentStableId == InvalidStableId.
    struct EntityBuildRecord
    {
        u64 stableId = InvalidStableId;
        u64 parentStableId = InvalidStableId;
        u64 name = 0;
        EntityFlags flags = EntityFlags::None;
    };

    /// Reflection-driven component source. CookPrefab serializes the object through its
    /// registered schema and extracts resource dependencies from the same schema.
    struct ComponentBuildRecord
    {
        u64 stableId = InvalidStableId;
        u64 entityStableId = InvalidStableId;
        const reflection::Schema* schema = nullptr;
        const void* object = nullptr;
        ComponentFlags flags = ComponentFlags::None;
    };

    struct ExplicitDependency
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    /// Input is importer/editor neutral. It represents the already resolved prototype;
    /// inheritance and include resolution happen before this deterministic runtime cook.
    struct CookDescription
    {
        u64 name = 0;
        containers::ArraySpan<const EntityBuildRecord> entities;
        containers::ArraySpan<const ComponentBuildRecord> components;
        containers::ArraySpan<const ExplicitDependency> explicitDependencies;
        crypto::Digest256 sourceFingerprint;
        bool includeEditorData = false;
    };

    struct EntityRecord
    {
        u64 stableId = InvalidStableId;
        u64 parentStableId = InvalidStableId;
        u64 name = 0;
        u32 firstComponent = 0;
        u32 componentCount = 0;
        EntityFlags flags = EntityFlags::None;
    };

    struct ComponentRecord
    {
        u64 stableId = InvalidStableId;
        u64 entityStableId = InvalidStableId;
        reflection::SchemaTypeId schema = reflection::InvalidSchemaTypeId;
        u16 schemaVersion = 0;
        ComponentFlags flags = ComponentFlags::None;
        u64 dataOffset = 0;
        u64 dataSize = 0;
        crypto::Digest256 dataFingerprint;
    };

    struct DependencyRecord
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 512ull * 1024ull * 1024ull;
        u32 maximumEntities = 1u << 20u;
        u32 maximumComponents = 1u << 22u;
        u32 maximumDependencies = 1u << 20u;
        u64 maximumComponentDataBytes = 384ull * 1024ull * 1024ull;
    };

    class PrefabFile final
    {
    public:
        PrefabFile() noexcept;

        PrefabFile(const PrefabFile&) = delete;
        PrefabFile& operator=(const PrefabFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 GetName() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetSourceFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const EntityRecord> GetEntities() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ComponentRecord> GetComponents() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DependencyRecord> GetDependencies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetComponentData(const ComponentRecord& component) const noexcept;
        [[nodiscard]] const EntityRecord* FindEntity(u64 stableId) const noexcept;
        [[nodiscard]] const ComponentRecord* FindComponent(u64 stableId) const noexcept;

    private:
        u64 m_name = 0;
        crypto::Digest256 m_sourceFingerprint;
        crypto::Digest256 m_contentFingerprint;
        containers::DynamicArray<EntityRecord> m_entities;
        containers::DynamicArray<ComponentRecord> m_components;
        containers::DynamicArray<DependencyRecord> m_dependencies;
        containers::DynamicArray<u8> m_componentData;
        bool m_open = false;
    };

    class PrefabResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;
        [[nodiscard]] Result Open(const void* data, usize size, const ReadLimits& limits = {}) noexcept;
        [[nodiscard]] const PrefabFile& GetFile() const noexcept;

    private:
        PrefabFile m_file;
    };

    [[nodiscard]] Result CookPrefab(const CookDescription& description, filesystem::IFile& output) noexcept;
    [[nodiscard]] Result CalculateContentFingerprint(const CookDescription& description, crypto::Digest256& fingerprint) noexcept;
} // namespace vanguard::prefabs
