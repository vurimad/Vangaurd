#pragma once

#include <vanguard/rendering/material_materializer.hpp>
#include <vanguard/rendering/render_pipeline_factory.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidMaterialResidencyIndex = 0xffffffffu;
    inline constexpr u32 InvalidMaterialDemandIndex = 0xffffffffu;
    inline constexpr u32 InvalidMaterialTechniqueIndex = 0xffffffffu;

    struct MaterialResidencyHandle
    {
        u32 index = InvalidMaterialResidencyIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialResidencyIndex && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const MaterialResidencyHandle&, const MaterialResidencyHandle&) noexcept = default;
    };

    struct MaterialDemandId
    {
        u32 index = InvalidMaterialDemandIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialDemandIndex && generation != 0;
        }
    };

    struct MaterialTechniqueId
    {
        u32 index = InvalidMaterialTechniqueIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialTechniqueIndex && generation != 0;
        }
    };

    class MaterialResidencyRuntime;

    /// One caller's move-only interest in an exact loaded VMAT generation.
    /// Equal path/generation/object identities share one residency record.
    class MaterialDemandHandle final
    {
    public:
        MaterialDemandHandle() noexcept = default;
        ~MaterialDemandHandle();

        MaterialDemandHandle(const MaterialDemandHandle&) = delete;
        MaterialDemandHandle& operator=(const MaterialDemandHandle&) = delete;
        MaterialDemandHandle(MaterialDemandHandle&& other) noexcept;
        MaterialDemandHandle& operator=(MaterialDemandHandle&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] MaterialResidencyHandle GetResidency() const noexcept;
        void Reset() noexcept;

    private:
        MaterialResidencyRuntime* m_runtime = nullptr;
        MaterialDemandId m_demand;
        MaterialResidencyHandle m_residency;

        friend class MaterialResidencyRuntime;
    };

    enum class MaterialResidencyState : u8
    {
        Invalid,
        Resolving,
        PublicationPending,
        Resident,
        Failed,
        Retiring
    };

    struct MaterialResidencyInfo
    {
        MaterialResidencyHandle residency;
        resources::ResourcePath resourcePath;
        u32 resourceGeneration = 0;
        u32 demandCount = 0;
        u32 techniqueCount = 0;
        MaterialResidencyState state = MaterialResidencyState::Invalid;
        GpuMaterialHandle material;
        MaterialMaterializerFailure failure;
    };

    enum class MaterialTechniqueState : u8
    {
        Invalid,
        Pending,
        Ready,
        Failed
    };

    struct MaterialTechniqueInfo
    {
        MaterialTechniqueState state = MaterialTechniqueState::Invalid;
        MaterialResidencyHandle residency;
        GpuMaterialHandle material;
        rhi::PipelineRef pipeline;
        pipeline_cache::FailureEvidence pipelineFailure;
        bool depthTest = false;
        bool reverseDepth = false;
    };

    /// Move-only ownership of one lazily admitted native technique request.
    /// It keeps the resident material and its exact native shader generations alive.
    class MaterialTechniqueRequest final
    {
    public:
        MaterialTechniqueRequest() noexcept = default;
        ~MaterialTechniqueRequest();

        MaterialTechniqueRequest(const MaterialTechniqueRequest&) = delete;
        MaterialTechniqueRequest& operator=(const MaterialTechniqueRequest&) = delete;
        MaterialTechniqueRequest(MaterialTechniqueRequest&& other) noexcept;
        MaterialTechniqueRequest& operator=(MaterialTechniqueRequest&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        void Reset() noexcept;

    private:
        MaterialResidencyRuntime* m_runtime = nullptr;
        MaterialTechniqueId m_technique;

        friend class MaterialResidencyRuntime;
        friend class MeshResidencyManager;
    };

    struct MaterialTechniqueDesc
    {
        MaterialResidencyHandle material;
        u64 technique = 0;
        const pipelines::AttachmentSignature* attachments = nullptr;
        pipeline_cache::Priority priority = pipeline_cache::Priority::Normal;
        bool mirrored = false;
        bool twoSided = false;
    };

    struct MaterialResidencyRuntimeConfig
    {
        u32 maximumResidencies = 4096;
        u32 maximumDemands = 16'384;
        u32 maximumTechniqueRequests = 4096;
        u32 maximumNativePrograms = 4096;
        u32 maximumResidencyChecksPerUpdate = 256;
        u32 maximumRetirementsPerSeal = 256;
    };

    enum class MaterialResidencyRuntimeFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDependency,
        InvalidConfiguration,
        InvalidArgument,
        InvalidMaterial,
        CapacityExceeded,
        StaleHandle,
        MaterializationFailure,
        TechniqueNotFound,
        NativeShaderFailure,
        PipelineFailure,
        MissingRetirementFence,
        LiveWorkRemains
    };

    struct MaterialResidencyRuntimeFailure
    {
        MaterialResidencyRuntimeFailureCode code = MaterialResidencyRuntimeFailureCode::None;
        const char* message = nullptr;
        MaterialMaterializerFailure materializationFailure;
        RenderShaderResult shaderResult = RenderShaderResult::Success;
        rhi::Failure rhiFailure;
        RenderPipelineResult pipelineResult = RenderPipelineResult::Success;
    };

    struct MaterialResidencyRuntimeStats
    {
        u32 residencyRecords = 0;
        u32 liveDemands = 0;
        u32 liveTechniqueRequests = 0;
        u32 cachedNativePrograms = 0;
        u32 referencedNativePrograms = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
        u64 materializationsFailed = 0;
        u64 techniquesRequested = 0;
        u64 nativeProgramLoads = 0;
        u64 nativeProgramReuses = 0;
        u64 residencyLookupProbes = 0;
        u64 nativeProgramLookupProbes = 0;
        u64 residencyStateChecks = 0;
        u64 retirementChecks = 0;
    };

    /// Renderer-thread coordinator over the existing materializer and pipeline cache.
    /// It accepts only already-loaded VMAT handles and performs no loading, draw submission,
    /// render-graph work, command-list recording, or fence synthesis.
    class MaterialResidencyRuntime final
    {
    public:
        struct Impl;

        MaterialResidencyRuntime() noexcept = default;
        ~MaterialResidencyRuntime();

        MaterialResidencyRuntime(const MaterialResidencyRuntime&) = delete;
        MaterialResidencyRuntime& operator=(const MaterialResidencyRuntime&) = delete;

        [[nodiscard]] bool Initialize(MaterialMaterializer& materializer, PipelineCache& pipelines, const PipelineInterfaceResources& interfaceResources = {}, const MaterialResidencyRuntimeConfig& config = {},
                                      MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearNativeProgramCache(MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RequestMaterial(const resources::ResourceHandle& material, MaterialDemandHandle& demand, MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelDemand(MaterialDemandHandle& demand, MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Update(MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool GetInfo(MaterialResidencyHandle material, MaterialResidencyInfo& info, MaterialResidencyRuntimeFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool RequestTechnique(const MaterialTechniqueDesc& desc, MaterialTechniqueRequest& request, MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetTechniqueInfo(const MaterialTechniqueRequest& request, MaterialTechniqueInfo& info, MaterialResidencyRuntimeFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReleaseTechnique(MaterialTechniqueRequest& request, MaterialResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] MaterialResidencyRuntimeStats GetStats() const noexcept;

    private:
        void ReleaseDemand(MaterialDemandId demand) noexcept;
        void ReleaseTechnique(MaterialTechniqueId technique) noexcept;
        [[nodiscard]] bool IsDemandValid(MaterialDemandId demand) const noexcept;
        [[nodiscard]] bool IsTechniqueValid(MaterialTechniqueId technique) const noexcept;

        Impl* m_impl = nullptr;

        friend class MaterialDemandHandle;
        friend class MaterialTechniqueRequest;
    };
} // namespace vanguard::rendering
