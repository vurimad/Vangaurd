#pragma once

#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rendering/material_resource_resolver.hpp>
#include <vanguard/resources/resources.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidMaterialMaterializationIndex = 0xffffffffu;

    struct MaterialMaterializationTicket
    {
        u32 index = InvalidMaterialMaterializationIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialMaterializationIndex && generation != 0;
        }
    };

    enum class MaterialMaterializationStatus : u8
    {
        Pending,
        PublicationPending,
        Ready,
        Failed
    };

    struct MaterialMaterializerConfig
    {
        u32 maximumOperations = 4096;
        u32 maximumRolesPerMaterial = 256;
        u32 maximumMaterialsPerBatch = 256;
        u32 maximumOperationsProgressedPerUpdate = 256;
        u32 maximumResourceRolePollsPerUpdate = 4096;
    };

    enum class MaterialMaterializerFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidArgument,
        InvalidClosure,
        CapacityExceeded,
        StaleTicket,
        LayoutFailure,
        ResourceFailure,
        DefinitionFailure,
        MissingRetirementFence,
        LiveWorkRemains
    };

    struct MaterialMaterializerFailure
    {
        MaterialMaterializerFailureCode code = MaterialMaterializerFailureCode::None;
        const char* message = nullptr;
        MaterialProgramLayoutFailure layoutFailure;
        MaterialResourceResolverFailure resourceFailure;
        GpuSceneDefinitionFailure definitionFailure;
    };

    struct MaterialMaterializerStats
    {
        u32 activeOperations = 0;
        u32 pendingOperations = 0;
        u32 publicationPending = 0;
        u32 readyResults = 0;
        u32 liveReferences = 0;
        u64 requestsBegun = 0;
        u64 materializationsPublished = 0;
        u64 materializationsCancelled = 0;
        u64 materializationsFailed = 0;
        u64 definitionReuses = 0;
        u64 operationStateChecks = 0;
        u64 resourceRolePolls = 0;
    };

    class MaterialMaterializer;

    namespace detail
    {
        struct MaterialMaterializerAccess;
    } // namespace detail

    /// Move-only ownership of one accepted GPU material definition and its exact
    /// CPU closure/resource references. Retire must cover every renderer queue that
    /// could have consumed the published handle.
    class GpuMaterialReference final
    {
    public:
        GpuMaterialReference() noexcept = default;
        ~GpuMaterialReference();

        GpuMaterialReference(const GpuMaterialReference&) = delete;
        GpuMaterialReference& operator=(const GpuMaterialReference&) = delete;
        GpuMaterialReference(GpuMaterialReference&& other) noexcept;
        GpuMaterialReference& operator=(GpuMaterialReference&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] GpuMaterialHandle GetHandle() const noexcept;
        [[nodiscard]] GpuSceneDefinitionKey GetKey() const noexcept;
        [[nodiscard]] bool Retire(const rhi::ResidencyFenceSet& safeAfter, MaterialMaterializerFailure* failure = nullptr) noexcept;

    private:
        void Abandon() noexcept;

        MaterialMaterializer* m_owner = nullptr;
        u32 m_index = InvalidMaterialMaterializationIndex;
        u32 m_generation = 0;
        GpuMaterialHandle m_handle;
        GpuSceneDefinitionKey m_key;

        friend class MaterialMaterializer;
        friend class MaterialResidencyRuntime;
    };

    /// Main-thread coordinator joining an immutable loaded VMAT closure, typed
    /// role references, and the deferred GPU Scene material contribution. It never
    /// requests resources or submits/waits for a private command list.
    class MaterialMaterializer final
    {
    public:
        struct Impl;

        MaterialMaterializer() noexcept = default;
        ~MaterialMaterializer();

        MaterialMaterializer(const MaterialMaterializer&) = delete;
        MaterialMaterializer& operator=(const MaterialMaterializer&) = delete;

        [[nodiscard]] bool Initialize(MaterialProgramLayoutRegistry& layouts, MaterialResourceResolver& resources, GpuSceneRuntime& runtime, const MaterialMaterializerConfig& config = {},
                                      MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool Begin(const resources::ResourceHandle& material, MaterialMaterializationTicket& ticket, MaterialMaterializerFailure* failure = nullptr) noexcept;
        /// Advances role polling and the single bounded deferred publication
        /// queue. Shared GPU execution remains owned by GpuSceneRuntime.
        [[nodiscard]] bool Update(MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] MaterialMaterializationStatus Poll(MaterialMaterializationTicket& ticket, GpuMaterialReference& material, MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(MaterialMaterializationTicket& ticket, MaterialMaterializerFailure* failure = nullptr) noexcept;
        [[nodiscard]] MaterialMaterializerStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool RetireReference(u32 index, u32 generation, const rhi::ResidencyFenceSet& safeAfter, MaterialMaterializerFailure* failure) noexcept;
        [[nodiscard]] bool AbandonReference(u32 index, u32 generation) noexcept;
        [[nodiscard]] bool IsReferenceValid(u32 index, u32 generation) const noexcept;

        Impl* m_impl = nullptr;

        friend class GpuMaterialReference;
        friend struct detail::MaterialMaterializerAccess;
    };
} // namespace vanguard::rendering
