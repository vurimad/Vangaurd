#pragma once

#include <cstdint>

#define AI_CONFIG_VANGUARD_MODEL_PROVENANCE "VANGUARD_MODEL_PROVENANCE"

enum class aiVanguardModelSourceKind : std::uint32_t
{
    Unknown = 0,
    Gltf2 = 1,
    Fbx = 2
};

enum aiVanguardGeometryUseFlags : std::uint32_t
{
    aiVanguardGeometryUse_None = 0,
    aiVanguardGeometryUse_HasSkin = 1u << 0
};

struct aiVanguardPrimitiveProvenance
{
    aiVanguardModelSourceKind sourceKind = aiVanguardModelSourceKind::Unknown;
    std::uint64_t sourceGeometryKey = 0;
    std::uint32_t sourcePartitionOrdinal = 0;
    std::uint32_t aiMeshIndex = 0;
};

struct aiVanguardGeometryUseProvenance
{
    aiVanguardModelSourceKind sourceKind = aiVanguardModelSourceKind::Unknown;
    std::uint64_t sourceGeometryKey = 0;
    std::uint64_t bindingKey = 0;
    std::uint32_t flags = aiVanguardGeometryUse_None;
};

struct aiVanguardModelProvenanceSink
{
    void* userData = nullptr;
    bool (*recordPrimitive)(void* userData, const aiVanguardPrimitiveProvenance& primitive) noexcept = nullptr;
    bool (*recordGeometryUse)(void* userData, const aiVanguardGeometryUseProvenance& use) noexcept = nullptr;
    bool (*recordStructuralDependency)(void* userData, aiVanguardModelSourceKind sourceKind, const char* path) noexcept = nullptr;
};
