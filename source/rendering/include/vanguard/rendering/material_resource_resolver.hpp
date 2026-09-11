#pragma once

#include <vanguard/rendering/gpu_scene_types.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>
#include <vanguard/shaders/shaders.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidMaterialResourceResolveIndex = 0xffffffffu;

    struct MaterialResourceResolveTicket
    {
        u32 index = InvalidMaterialResourceResolveIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialResourceResolveIndex && generation != 0;
        }
    };

    enum class MaterialResourceResolveStatus : u8
    {
        Pending,
        Ready,
        Failed
    };

    struct MaterialResourceResolvedIdentity
    {
        shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Texture;
        rhi::DescriptorDomainKind descriptorDomain = rhi::DescriptorDomainKind::Resources;
        u32 index = InvalidGpuSceneIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGpuSceneIndex && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const MaterialResourceResolvedIdentity&, const MaterialResourceResolvedIdentity&) noexcept = default;
    };

    /// Opaque provider operation identity. Providers own its namespace and must
    /// not recycle it until Cancel or Release is called.
    struct MaterialResourceProviderToken
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }
    };

    struct MaterialResourceProviderRequest
    {
        resources::ResourceHandle resource;
        shaders::MaterialResourceRole role;
        bool fallback = false;
    };

    enum class MaterialResourceProviderState : u8
    {
        Pending,
        Ready,
        Failed
    };

    /// Borrowed provider result. The provider must keep the selected object
    /// alive until Release is called for the operation token.
    struct MaterialResourceProviderResult
    {
        MaterialResourceProviderState state = MaterialResourceProviderState::Pending;
        /// Texture providers return an already bindless-ready stable residency
        /// identity. They never return the current physical descriptor.
        GpuTextureResidencyHandle texture;
        rhi::BufferRef buffer;
        rhi::SamplerStateRef sampler;
        rhi::AccelerationStructureRef accelerationStructure;
        rhi::BufferViewDesc bufferView;
        rhi::SamplerStateDesc samplerDesc;
        u64 identity = 0;
        u32 generation = 0;
        /// Required for a writable fallback. Ordinary resources and read-only
        /// fallbacks may be shared.
        bool exclusive = false;
    };

    using BeginMaterialResourceProviderFunction = bool (*)(void* userData, const MaterialResourceProviderRequest& request, MaterialResourceProviderToken& token, const char*& failureMessage) noexcept;
    using PollMaterialResourceProviderFunction = bool (*)(void* userData, MaterialResourceProviderToken token, MaterialResourceProviderResult& result, const char*& failureMessage) noexcept;
    using CancelMaterialResourceProviderFunction = void (*)(void* userData, MaterialResourceProviderToken token) noexcept;
    using ReleaseMaterialResourceProviderFunction = void (*)(void* userData, MaterialResourceProviderToken token, const rhi::ResidencyFenceSet& safeAfter) noexcept;
    /// Terminal device-loss cleanup. The provider must invalidate the token without waiting for GPU fences;
    /// Release and Cancel will not subsequently be called for it.
    using AbandonMaterialResourceProviderFunction = void (*)(void* userData, MaterialResourceProviderToken token) noexcept;

    struct MaterialResourceProviderDesc
    {
        shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Texture;
        resources::ResourceTypeId assetType = resources::InvalidResourceTypeId;
        BeginMaterialResourceProviderFunction begin = nullptr;
        PollMaterialResourceProviderFunction poll = nullptr;
        CancelMaterialResourceProviderFunction cancel = nullptr;
        ReleaseMaterialResourceProviderFunction release = nullptr;
        AbandonMaterialResourceProviderFunction abandon = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return assetType != resources::InvalidResourceTypeId && begin != nullptr && poll != nullptr && cancel != nullptr && release != nullptr && abandon != nullptr;
        }
    };

    struct MaterialResourceFallbackDesc
    {
        shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Texture;
        resources::ResourceTypeId assetType = resources::InvalidResourceTypeId;
        crypto::Digest256 typeFingerprint;
        shaders::MaterialResourceShape shape;
        /// Optional for provider-created fallbacks. When present it is retained
        /// as the exact loaded generation supplied to the provider.
        resources::ResourceHandle resource;
    };

    struct MaterialResourceResolveRequest
    {
        shaders::MaterialResourceRole role;
        resources::ResourceTypeId expectedAssetType = resources::InvalidResourceTypeId;
        resources::DependencyKind dependency = resources::DependencyKind::Required;
        resources::ResourceHandle resource;
    };

    struct MaterialResourceResolverConfig
    {
        u32 maximumProviders = 64;
        u32 maximumFallbacks = 256;
        u32 maximumOperations = 16'384;
        u32 maximumDescriptorCacheEntries = 16'384;
    };

    enum class MaterialResourceResolverFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidArgument,
        DuplicateProvider,
        DuplicateFallback,
        ProviderNotFound,
        FallbackNotFound,
        CapacityExceeded,
        StaleTicket,
        ProviderFailure,
        ShapeMismatch,
        UnsupportedShape,
        DependencyUnavailable,
        DescriptorFailure,
        MissingRetirementFence,
        LiveReferencesRemain
    };

    struct MaterialResourceResolverFailure
    {
        MaterialResourceResolverFailureCode code = MaterialResourceResolverFailureCode::None;
        const char* message = nullptr;
        shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Texture;
        resources::ResourceTypeId assetType = resources::InvalidResourceTypeId;
        rhi::Failure rhiFailure;
    };

    struct MaterialResourceResolverStats
    {
        u32 registeredProviders = 0;
        u32 registeredFallbacks = 0;
        u32 activeOperations = 0;
        u32 liveReferences = 0;
        u32 cachedDescriptors = 0;
        u64 resolutionsBegun = 0;
        u64 resolutionsReady = 0;
        u64 fallbacksSelected = 0;
        u64 descriptorReuses = 0;
        u64 rejectedOperations = 0;
    };

    class MaterialResourceResolver;

    /// Move-only retained reference to one ready material-resource role. Reset
    /// is for unpublished work; Retire supplies the renderer cutover after the
    /// reference has reached GPU work.
    class MaterialResourceReference final
    {
    public:
        MaterialResourceReference() noexcept = default;
        ~MaterialResourceReference();

        MaterialResourceReference(const MaterialResourceReference&) = delete;
        MaterialResourceReference& operator=(const MaterialResourceReference&) = delete;
        MaterialResourceReference(MaterialResourceReference&& other) noexcept;
        MaterialResourceReference& operator=(MaterialResourceReference&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const GpuMaterialResource& GetGpuResource() const noexcept;
        [[nodiscard]] MaterialResourceResolvedIdentity GetIdentity() const noexcept;
        void Reset() noexcept;
        [[nodiscard]] bool Retire(const rhi::ResidencyFenceSet& safeAfter, MaterialResourceResolverFailure* failure = nullptr) noexcept;

    private:
        void Abandon() noexcept;

        MaterialResourceResolver* m_owner = nullptr;
        u32 m_index = InvalidMaterialResourceResolveIndex;
        u32 m_generation = 0;
        GpuMaterialResource m_gpuResource;
        MaterialResourceResolvedIdentity m_identity;

        friend class MaterialResourceResolver;
        friend class MaterialMaterializer;
    };

    /// Bounded renderer-owned typed role resolver. It owns provider registration,
    /// exact fallback selection, and the central immutable descriptor caches.
    /// It never loads hidden Soft dependencies or publishes GpuMaterial handles.
    class MaterialResourceResolver final
    {
    public:
        struct Impl;

        MaterialResourceResolver() noexcept = default;
        ~MaterialResourceResolver();

        MaterialResourceResolver(const MaterialResourceResolver&) = delete;
        MaterialResourceResolver& operator=(const MaterialResourceResolver&) = delete;

        [[nodiscard]] bool Initialize(TextureResidencyRuntime& textures, rhi::DescriptorDomainRef resourceDescriptors, rhi::DescriptorDomainRef samplerDescriptors,
                                      const MaterialResourceResolverConfig& config = {}, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterProvider(const MaterialResourceProviderDesc& provider, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterFallback(const MaterialResourceFallbackDesc& fallback, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearFallbacks(MaterialResourceResolverFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Begin(const MaterialResourceResolveRequest& request, MaterialResourceResolveTicket& ticket, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] MaterialResourceResolveStatus Poll(MaterialResourceResolveTicket& ticket, MaterialResourceReference& reference, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(MaterialResourceResolveTicket& ticket, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        [[nodiscard]] MaterialResourceResolverStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool ReleaseReference(u32 index, u32 generation, const rhi::ResidencyFenceSet& safeAfter, bool retired, MaterialResourceResolverFailure* failure = nullptr) noexcept;
        void AbandonReference(u32 index, u32 generation) noexcept;
        [[nodiscard]] bool IsReferenceValid(u32 index, u32 generation) const noexcept;

        Impl* m_impl = nullptr;

        friend class MaterialResourceReference;
    };
} // namespace vanguard::rendering
