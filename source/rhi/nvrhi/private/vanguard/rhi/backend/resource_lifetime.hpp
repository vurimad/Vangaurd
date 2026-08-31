#pragma once

#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rhi::backend
{
    inline constexpr u32 MaximumRetirementBuckets = 64;

    struct FenceSet
    {
        u64 graphics = 0;
        u64 compute = 0;
        u64 copy = 0;

        [[nodiscard]] u64 Get(QueueType queue) const noexcept;
        void Include(QueueType queue, u64 value) noexcept;
        void Include(const FenceSet& other) noexcept;
    };

    struct ResourceLifetimeConfig
    {
        u32 textureCapacity = 65535;
        u32 textureReadbackCapacity = 4096;
        u32 bufferCapacity = 65535;
        u32 heapCapacity = 8192;
        u32 samplerStateCapacity = 4096;
        u32 shaderCapacity = 32768;
        u32 pipelineCapacity = 32768;
        u32 bindingLayoutCapacity = 16384;
        u32 descriptorDomainCapacity = 64;
        u32 accelerationStructureCapacity = 32768;
        u32 shaderTableCapacity = 4096;
        u32 swapChainCapacity = 64;
        u32 commandListCapacity = 4096;
        u32 retirementBucketCount = 32;
    };

    using FenceCompleteCallback = bool (*)(void* context, QueueType queue, u64 value) noexcept;
    using DestroyResourceCallback = void (*)(void* context, ResourceRef resource, void* payload) noexcept;
    using ResourceDestroyedCallback = void (*)(void* context, ResourceRef resource) noexcept;

    class ResourceLifetimeManager final
    {
    public:
        ResourceLifetimeManager() noexcept = default;
        ~ResourceLifetimeManager();
        ResourceLifetimeManager(const ResourceLifetimeManager&) = delete;
        ResourceLifetimeManager& operator=(const ResourceLifetimeManager&) = delete;

        [[nodiscard]] bool Initialize(const ResourceLifetimeConfig& config, FenceCompleteCallback fenceComplete, void* fenceContext,
                                      ResourceDestroyedCallback resourceDestroyed = nullptr, void* resourceDestroyedContext = nullptr) noexcept;
        // Requires the GPU to be idle and all externally owned references to be released.
        [[nodiscard]] bool ShutdownAfterGpuIdle() noexcept;
        // Emergency backend teardown after device idle/removal. Outstanding owners are invalidated and
        // native payloads are destroyed; normal shutdown must use ShutdownAfterGpuIdle and reject leaks.
        void ForceShutdownAfterGpuIdle() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] ResourceRef Create(ResourceKind kind, void* payload, DestroyResourceCallback destroy, void* destroyContext = nullptr,
                                         u32 initialReferences = 1) noexcept;
        [[nodiscard]] bool IsValid(ResourceRef resource) const noexcept;
        [[nodiscard]] bool AddRef(ResourceRef resource) noexcept;
        [[nodiscard]] i32 Release(ResourceRef resource) noexcept;
        [[nodiscard]] i32 GetRefCount(ResourceRef resource) const noexcept;
        // The caller must own a live reference for the entire duration of payload access.
        [[nodiscard]] void* GetPayload(ResourceRef resource) noexcept;
        [[nodiscard]] const void* GetPayload(ResourceRef resource) const noexcept;

        // Recording holds a reference until submission, then records the returned queue fence before releasing it.
        [[nodiscard]] bool RecordUse(ResourceRef resource, QueueType queue, u64 submissionFence) noexcept;
        [[nodiscard]] FenceSet GetLastUse(ResourceRef resource) const noexcept;

        // Seals the current retirement bucket, advances the producer epoch, and schedules reclamation.
        void SealRetirementEpoch(const FenceSet& submittedFences) noexcept;
        void CollectGarbage() noexcept;
        void WaitForReclamation() noexcept;

        [[nodiscard]] ResourceLifetimeStats GetStats() const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rhi::backend
