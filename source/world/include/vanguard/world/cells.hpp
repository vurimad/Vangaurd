#pragma once

#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/resources/resource_pipeline.hpp>

namespace vanguard::world
{
    inline constexpr u32 CellMagic = serialization::MakeFourCC('V', 'C', 'E', 'L');
    inline constexpr resources::ResourceTypeId CellResourceType = serialization::MakeFourCC('V', 'C', 'E', 'L');
    inline constexpr u64 AlwaysActiveGroup = 0;
    inline constexpr u64 InvalidEntityId = 0;

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
        UnknownActivationGroup,
        InvalidReference,
        UnknownSchema,
        SchemaFailure,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class CellCategory : u8
    {
        Exterior,
        Interior,
        Mission,
        Navigation,
        AlwaysLoaded,
        Generic
    };

    enum class ActivationGroupFlags : u16
    {
        None = 0,
        DefaultActive = 1u << 0u,
        EditorOnly = 1u << 1u
    };

    enum class PlacementFlags : u32
    {
        None = 0,
        Persistent = 1u << 0u,
        InitiallyDisabled = 1u << 1u,
        EditorOnly = 1u << 2u,
        NoCollision = 1u << 3u,
        Interior = 1u << 4u,
        Mission = 1u << 5u,
        Decoration = 1u << 6u,
        TwoDimensionalStreaming = 1u << 7u,
        AllowDistanceBoosting = 1u << 8u,
        PersistTransform = 1u << 9u
    };

    enum class OverrideMode : u8
    {
        Replace,
        Add,
        Remove
    };

    enum class OverrideFlags : u16
    {
        None = 0,
        EditorOnly = 1u << 0u
    };

    enum class EntityReferenceKind : u8
    {
        RequiredLocal,
        OptionalLocal,
        RequiredWorld,
        OptionalWorld
    };

    template<typename Enum>
    [[nodiscard]] constexpr Enum CombineFlags(const Enum left, const Enum right) noexcept
    {
        return static_cast<Enum>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    struct Bounds
    {
        f32 minimum[3]{};
        f32 maximum[3]{};
    };

    /// Cell-local transform. Translation is relative to CellBuildDescription::origin,
    /// which preserves precision in large worlds without serializing runtime math objects.
    struct PlacementTransform
    {
        f32 translation[3]{};
        f32 rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        f32 scale[3]{1.0f, 1.0f, 1.0f};
    };

    struct ActivationGroupBuildRecord
    {
        u64 stableId = AlwaysActiveGroup;
        u64 name = 0;
        ActivationGroupFlags flags = ActivationGroupFlags::None;
    };

    struct PlacementBuildRecord
    {
        u64 entityId = InvalidEntityId;
        u64 parentEntityId = InvalidEntityId;
        u64 name = 0;
        u64 activationGroup = AlwaysActiveGroup;
        resources::ResourceReference prefab;
        PlacementTransform transform;
        Bounds bounds;
        f32 streamingReferencePoint[3]{};
        f32 streamingDistance = 0.0f;
        f32 visibilityDistance = 0.0f;
        u64 layerMask = ~0ull;
        PlacementFlags flags = PlacementFlags::None;
        u16 timeOfDayVisibilityMask = 0xffffu;
        u8 streamingPriority = 128;
    };

    /// A sparse instance override keyed by the stable component identity stored in vprefab.
    /// Replace and Add serialize a complete component value through schema. Remove has no data.
    /// Replace and Remove may target any existing prefab component because its stable ID identifies
    /// the owning prefab entity. Add has no existing owner and therefore targets the prefab root.
    struct ComponentOverrideBuildRecord
    {
        u64 entityId = InvalidEntityId;
        u64 componentStableId = prefabs::InvalidStableId;
        OverrideMode mode = OverrideMode::Replace;
        const reflection::Schema* schema = nullptr;
        const void* object = nullptr;
        OverrideFlags flags = OverrideFlags::None;
    };

    /// Named entity-reference slots are resolved after all entities in the cell are created.
    /// World references may target an unloaded cell and therefore remain stable IDs at runtime.
    struct EntityReferenceBuildRecord
    {
        u64 sourceEntityId = InvalidEntityId;
        u64 slot = 0;
        u64 targetEntityId = InvalidEntityId;
        EntityReferenceKind kind = EntityReferenceKind::RequiredLocal;
    };

    struct ExplicitDependency
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    struct CellBuildDescription
    {
        u64 cellId = 0;
        u64 worldId = 0;
        i32 gridCoordinate[3]{};
        u8 hierarchyLevel = 0;
        CellCategory category = CellCategory::Generic;
        f64 origin[3]{};
        Bounds bounds;
        containers::ArraySpan<const ActivationGroupBuildRecord> activationGroups;
        containers::ArraySpan<const PlacementBuildRecord> placements;
        containers::ArraySpan<const ComponentOverrideBuildRecord> overrides;
        containers::ArraySpan<const EntityReferenceBuildRecord> entityReferences;
        containers::ArraySpan<const ExplicitDependency> explicitDependencies;
        crypto::Digest256 sourceFingerprint;
        bool includeEditorData = false;
    };

    struct ActivationGroupRecord
    {
        u64 stableId = AlwaysActiveGroup;
        u64 name = 0;
        u32 firstPlacement = 0;
        u32 placementCount = 0;
        ActivationGroupFlags flags = ActivationGroupFlags::None;
    };

    struct PlacementRecord
    {
        u64 entityId = InvalidEntityId;
        u64 parentEntityId = InvalidEntityId;
        u64 name = 0;
        u64 activationGroup = AlwaysActiveGroup;
        resources::ResourceReference prefab;
        PlacementTransform transform;
        Bounds bounds;
        f32 streamingReferencePoint[3]{};
        f32 streamingDistance = 0.0f;
        f32 visibilityDistance = 0.0f;
        u64 layerMask = ~0ull;
        u32 firstOverride = 0;
        u32 overrideCount = 0;
        u32 firstReference = 0;
        u32 referenceCount = 0;
        PlacementFlags flags = PlacementFlags::None;
        u16 timeOfDayVisibilityMask = 0xffffu;
        u8 streamingPriority = 128;
    };

    struct EntityLookupRecord
    {
        u64 entityId = InvalidEntityId;
        u32 placementIndex = 0;
    };

    struct ComponentOverrideRecord
    {
        u64 entityId = InvalidEntityId;
        u64 componentStableId = prefabs::InvalidStableId;
        reflection::SchemaTypeId schema = reflection::InvalidSchemaTypeId;
        u16 schemaVersion = 0;
        OverrideFlags flags = OverrideFlags::None;
        OverrideMode mode = OverrideMode::Replace;
        u64 dataOffset = 0;
        u64 dataSize = 0;
        crypto::Digest256 dataFingerprint;
    };

    struct EntityReferenceRecord
    {
        u64 sourceEntityId = InvalidEntityId;
        u64 slot = 0;
        u64 targetEntityId = InvalidEntityId;
        EntityReferenceKind kind = EntityReferenceKind::RequiredLocal;
    };

    struct DependencyRecord
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 512ull * 1024ull * 1024ull;
        u32 maximumActivationGroups = 65536;
        u32 maximumPlacements = 1u << 20u;
        u32 maximumOverrides = 1u << 22u;
        u32 maximumEntityReferences = 1u << 22u;
        u32 maximumDependencies = 1u << 20u;
        u64 maximumOverrideDataBytes = 384ull * 1024ull * 1024ull;
    };

    class CellFile final
    {
    public:
        CellFile() noexcept;

        CellFile(const CellFile&) = delete;
        CellFile& operator=(const CellFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 CellId() const noexcept;
        [[nodiscard]] u64 WorldId() const noexcept;
        [[nodiscard]] const i32* GridCoordinate() const noexcept;
        [[nodiscard]] u8 HierarchyLevel() const noexcept;
        [[nodiscard]] CellCategory Category() const noexcept;
        [[nodiscard]] const f64* Origin() const noexcept;
        [[nodiscard]] const Bounds& CellBounds() const noexcept;
        [[nodiscard]] const crypto::Digest256& SourceFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& ContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ActivationGroupRecord> ActivationGroups() const noexcept;
        [[nodiscard]] containers::ArraySpan<const PlacementRecord> Placements() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ComponentOverrideRecord> Overrides() const noexcept;
        [[nodiscard]] containers::ArraySpan<const EntityReferenceRecord> EntityReferences() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DependencyRecord> Dependencies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const PlacementRecord> PlacementsInGroup(
            const ActivationGroupRecord& group) const noexcept;
        [[nodiscard]] containers::ArraySpan<const ComponentOverrideRecord> OverridesFor(
            const PlacementRecord& placement) const noexcept;
        [[nodiscard]] containers::ArraySpan<const EntityReferenceRecord> ReferencesFor(
            const PlacementRecord& placement) const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> OverrideData(const ComponentOverrideRecord& record) const noexcept;
        [[nodiscard]] const PlacementRecord* FindPlacement(u64 entityId) const noexcept;
        [[nodiscard]] const ActivationGroupRecord* FindActivationGroup(u64 stableId) const noexcept;

    private:
        u64 m_cellId = 0;
        u64 m_worldId = 0;
        i32 m_gridCoordinate[3]{};
        u8 m_hierarchyLevel = 0;
        CellCategory m_category = CellCategory::Generic;
        f64 m_origin[3]{};
        Bounds m_bounds;
        crypto::Digest256 m_sourceFingerprint;
        crypto::Digest256 m_contentFingerprint;
        containers::DynamicArray<ActivationGroupRecord> m_activationGroups;
        containers::DynamicArray<PlacementRecord> m_placements;
        containers::DynamicArray<EntityLookupRecord> m_entityLookup;
        containers::DynamicArray<ComponentOverrideRecord> m_overrides;
        containers::DynamicArray<EntityReferenceRecord> m_entityReferences;
        containers::DynamicArray<DependencyRecord> m_dependencies;
        containers::DynamicArray<u8> m_overrideData;
        bool m_open = false;
    };

    class CellResource final : public resources::ResourceObject
    {
    public:
        CellResource() noexcept;

        [[nodiscard]] resources::ResourceTypeId Type() const noexcept override;
        [[nodiscard]] Result Open(const void* data, usize size, const ReadLimits& limits = {}) noexcept;
        [[nodiscard]] bool BindDependencies(const resources::LoadContext& context) noexcept;
        [[nodiscard]] const prefabs::PrefabFile* ResolvePrefab(resources::ResourceReference reference) const noexcept;
        [[nodiscard]] const CellFile& File() const noexcept;

    private:
        CellFile m_file;
        containers::DynamicArray<resources::ResourceHandle> m_prefabs;
    };

    [[nodiscard]] Result CookCell(const CellBuildDescription& description, filesystem::IFile& output) noexcept;
    [[nodiscard]] Result CalculateContentFingerprint(const CellBuildDescription& description,
                                                     crypto::Digest256& fingerprint) noexcept;
} // namespace vanguard::world
