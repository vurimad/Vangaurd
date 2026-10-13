#pragma once

#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resources.hpp>

#include <cstddef>

namespace vanguard::entities
{
    inline constexpr reflection::SchemaTypeId TransformComponentType = reflection::HashSchemaName("vanguard.transform_component");

    /// TransformComponent has no component-specific authored payload. Its placement and
    /// same-entity parent relationship live in the prefab's generic placed-component data.
    struct TransformComponentData
    {
        u8 reserved = 0;
    };

    inline const reflection::Schema TransformComponentSchema{TransformComponentType, "vanguard.transform_component",
                                                             sizeof(TransformComponentData), alignof(TransformComponentData), 1, 1,
                                                             nullptr, 0};

    [[nodiscard]] inline const reflection::Schema& GetTransformComponentSchema() noexcept
    {
        return TransformComponentSchema;
    }

    inline constexpr reflection::SchemaTypeId StaticMeshComponentType = reflection::HashSchemaName("vanguard.static_mesh_component");

    // Serialized values decoded by ComponentRegistry::RegisterObject. Transform placement
    // remains in the prefab's generic placed-component records.
    struct StaticMeshComponentData
    {
        resources::ResourceReference mesh;
        f32 scaleX = 1.0f;
        f32 scaleY = 1.0f;
        f32 scaleZ = 1.0f;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        bool castsShadow = true;
        bool receivesDecals = true;
    };

    inline const reflection::SchemaField StaticMeshComponentFields[]{
        reflection::MakeField("mesh", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              offsetof(StaticMeshComponentData, mesh), sizeof(StaticMeshComponentData::mesh),
                              alignof(resources::ResourceReference), 1, 0, reflection::FieldFlags::Required),
        reflection::MakeField("scale_x", reflection::builtin::F32, reflection::ValueKind::F32,
                              offsetof(StaticMeshComponentData, scaleX), sizeof(f32), alignof(f32)),
        reflection::MakeField("scale_y", reflection::builtin::F32, reflection::ValueKind::F32,
                              offsetof(StaticMeshComponentData, scaleY), sizeof(f32), alignof(f32)),
        reflection::MakeField("scale_z", reflection::builtin::F32, reflection::ValueKind::F32,
                              offsetof(StaticMeshComponentData, scaleZ), sizeof(f32), alignof(f32)),
        reflection::MakeField("layer_mask", reflection::builtin::U64, reflection::ValueKind::U64,
                              offsetof(StaticMeshComponentData, layerMask), sizeof(u64), alignof(u64)),
        reflection::MakeField("visibility_mask", reflection::builtin::U32, reflection::ValueKind::U32,
                              offsetof(StaticMeshComponentData, visibilityMask), sizeof(u32), alignof(u32)),
        reflection::MakeField("casts_shadow", reflection::builtin::Bool, reflection::ValueKind::Bool,
                              offsetof(StaticMeshComponentData, castsShadow), sizeof(bool), alignof(bool)),
        reflection::MakeField("receives_decals", reflection::builtin::Bool, reflection::ValueKind::Bool,
                              offsetof(StaticMeshComponentData, receivesDecals), sizeof(bool), alignof(bool))};

    inline const reflection::Schema StaticMeshComponentSchema{
        StaticMeshComponentType, "vanguard.static_mesh_component", sizeof(StaticMeshComponentData), alignof(StaticMeshComponentData), 1, 1,
        StaticMeshComponentFields, static_cast<u32>(sizeof(StaticMeshComponentFields) / sizeof(StaticMeshComponentFields[0]))};

    [[nodiscard]] inline const reflection::Schema& GetStaticMeshComponentSchema() noexcept
    {
        return StaticMeshComponentSchema;
    }
} // namespace vanguard::entities
