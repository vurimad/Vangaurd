#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/vellum/instance_storage.hpp>

namespace vanguard::vellum::detail
{
    inline constexpr u32 SpawnControlCancelRequested = 1u << 0u;
    inline constexpr u32 SpawnControlReleaseRequested = 1u << 1u;
    inline constexpr u32 SpawnControlCommitClaimed = 1u << 2u;

    enum class SpawnSlotLifecycle : u8
    {
        Free,
        Occupied,
        DeferredFree,
        Exhausted
    };

    struct SpawnSlot
    {
        resources::ResourceReference reference;
        resources::PipelineRequest request;
        resources::ResourceHandle document;
        VellumInstanceHandle stagingInstance;
        VellumInstanceHandle resultInstance;
        resources::LoadPriority priority = resources::LoadPriority::Normal;
        resources::Failure resourceFailure = resources::Failure::None;
        FailureCode failure = FailureCode::None;
        concurrency::Atomic<u32> controlFlags;
        SpawnState state = SpawnState::Invalid;
        SpawnSlotLifecycle lifecycle = SpawnSlotLifecycle::Free;
        u32 generation = 1;
        u32 nextFree = InvalidIndex;
        u32 previousActive = InvalidIndex;
        u32 nextActive = InvalidIndex;
        u32 additionalNodeCapacity = 0;
        u32 documentGeneration = 0;
        u32 materializedNodes = 0;
        u32 totalNodes = 0;
    };

    [[nodiscard]] bool InitializeSpawnRegistry(Runtime::Impl& runtime, Failure* failure) noexcept;
    void DestroySpawnRegistry(Runtime::Impl& runtime) noexcept;
    void PromoteDeferredSpawns(Runtime::Impl& runtime) noexcept;
    [[nodiscard]] bool RequestSpawn(Runtime::Impl& runtime, const SpawnDesc& desc, SpawnHandle& spawn, Failure* failure) noexcept;
    [[nodiscard]] bool ProgressSpawns(Runtime::Impl& runtime, Failure* failure) noexcept;
    [[nodiscard]] bool CancelSpawn(Runtime::Impl& runtime, SpawnHandle spawn, SpawnState& outcome, Failure* failure) noexcept;
    [[nodiscard]] bool ReleaseSpawn(Runtime::Impl& runtime, SpawnHandle spawn, Failure* failure) noexcept;
    [[nodiscard]] bool TakeSpawnResult(Runtime::Impl& runtime, SpawnHandle spawn, VellumInstanceHandle& instance, Failure* failure) noexcept;
    [[nodiscard]] bool GetSpawnInfo(const Runtime::Impl& runtime, SpawnHandle spawn, SpawnInfo& info, Failure* failure) noexcept;
    [[nodiscard]] bool QuiesceSpawns(Runtime::Impl& runtime, Failure* failure) noexcept;
}
