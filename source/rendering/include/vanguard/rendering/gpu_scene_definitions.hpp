#pragma once

#include <vanguard/rendering/gpu_scene_upload.hpp>

namespace vanguard::rendering
{
    /// Content identity supplied by the resource resolver. Reimporting or changing runtime resolution
    /// produces a new key; equal keys must describe byte-identical GPU definitions.
    struct GpuSceneDefinitionKey
    {
        u64 words[4]{};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return words[0] != 0 || words[1] != 0 || words[2] != 0 || words[3] != 0;
        }

        [[nodiscard]] u32 CalcHash() const noexcept;
        [[nodiscard]] friend constexpr bool operator==(const GpuSceneDefinitionKey&,
                                                       const GpuSceneDefinitionKey&) noexcept = default;
    };

    struct GpuGeometryDefinition
    {
        GpuSceneDefinitionKey key;
        GpuGeometryRange geometry;
        containers::ArraySpan<const GpuVertexStream> vertexStreams;
        const GpuPositionDecode* positionDecode = nullptr;
    };

    struct GpuMaterialDefinition
    {
        GpuSceneDefinitionKey key;
        GpuMaterial material;
        containers::ArraySpan<const GpuMaterialResource> resources;
    };

    /// CPU registration image of one primitive. Table indices are resolved from generational definition
    /// handles; phase offsets remain local to GpuRenderableDefinition::phaseParticipations.
    struct GpuPrimitiveDefinition
    {
        GpuGeometryHandle geometry;
        GpuMaterialHandle material;
        u32 firstPhaseParticipation = 0;
        u32 phaseParticipationCount = 0;
        u32 stableSubmesh = 0;
        GpuPrimitiveFlags flags = GpuPrimitiveFlags::None;
    };

    /// LOD primitive offsets remain local to the primitive span below and are rebased during publication.
    struct GpuRenderableDefinition
    {
        GpuSceneDefinitionKey key;
        GpuRenderableFlags flags = GpuRenderableFlags::None;
        containers::ArraySpan<const GpuLod> lods;
        containers::ArraySpan<const GpuPrimitiveDefinition> primitives;
        containers::ArraySpan<const GpuPhaseParticipation> phaseParticipations;
    };

    struct GpuSceneDefinitionsConfig
    {
        u32 maximumGeometries = 65'536;
        u32 maximumMaterials = 65'536;
        u32 maximumRenderables = 65'536;
        u32 maximumDefinitionsPerBatch = 4'096;
        u32 maximumAllocationsPerBatch = 16'384;
    };

    enum class GpuSceneDefinitionFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidDefinition,
        IncompatibleDefinition,
        InvalidDependency,
        CapacityExceeded,
        ReferenceUnderflow,
        LiveDefinitionsRemain,
        LifetimeFailure,
        UploadFailure
    };

    struct GpuSceneDefinitionFailure
    {
        GpuSceneDefinitionFailureCode code = GpuSceneDefinitionFailureCode::None;
        GpuSceneDefinitionKey key;
        const char* message = nullptr;
        GpuSceneLifetimeFailure lifetimeFailure;
        GpuSceneUploadFailure uploadFailure;
    };

    struct GpuSceneDefinitionPublication
    {
        rhi::GpuFence completion;
        u32 requestedDefinitions = 0;
        u32 createdDefinitions = 0;
        u32 reusedDefinitions = 0;
        u32 uploadedRanges = 0;
        u64 uploadedBytes = 0;
    };

    struct GpuSceneDefinitionsStats
    {
        u32 geometries = 0;
        u32 materials = 0;
        u32 renderables = 0;
        u64 references = 0;
        u64 acquisitions = 0;
        u64 reuses = 0;
        u64 releases = 0;
        u64 retirements = 0;
        u64 rejectedOperations = 0;
    };

    /// Owns immutable, content-addressed GPU Scene definitions. Calls are main-thread transactions;
    /// producer jobs still write unrelated mutable instance updates directly through GpuSceneUploader.
    class GpuSceneDefinitions final
    {
    public:
        struct Impl;

        GpuSceneDefinitions() noexcept = default;
        ~GpuSceneDefinitions();

        GpuSceneDefinitions(const GpuSceneDefinitions&) = delete;
        GpuSceneDefinitions& operator=(const GpuSceneDefinitions&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneLifetime& lifetime, GpuSceneUploader& uploader,
                                      const GpuSceneDefinitionsConfig& config = {},
                                      GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool AcquireGeometries(containers::ArraySpan<const GpuGeometryDefinition> definitions,
                                             containers::ArraySpan<GpuGeometryHandle> handles,
                                             GpuSceneDefinitionPublication& publication,
                                             GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireMaterials(containers::ArraySpan<const GpuMaterialDefinition> definitions,
                                            containers::ArraySpan<GpuMaterialHandle> handles,
                                            GpuSceneDefinitionPublication& publication,
                                            GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireRenderables(containers::ArraySpan<const GpuRenderableDefinition> definitions,
                                              containers::ArraySpan<GpuRenderableHandle> handles,
                                              GpuSceneDefinitionPublication& publication,
                                              GpuSceneDefinitionFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool AddReference(GpuGeometryHandle handle,
                                        GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AddReference(GpuMaterialHandle handle,
                                        GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AddReference(GpuRenderableHandle handle,
                                        GpuSceneDefinitionFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Release(GpuGeometryHandle handle,
                                   GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Release(GpuMaterialHandle handle,
                                   GpuSceneDefinitionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Release(GpuRenderableHandle handle,
                                   GpuSceneDefinitionFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool IsValid(GpuGeometryHandle handle) const noexcept;
        [[nodiscard]] bool IsValid(GpuMaterialHandle handle) const noexcept;
        [[nodiscard]] bool IsValid(GpuRenderableHandle handle) const noexcept;
        [[nodiscard]] GpuSceneDefinitionsStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
