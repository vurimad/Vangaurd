#include <vanguard/vellum/spawn_storage.hpp>

#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::vellum::detail
{
    namespace
    {
        constexpr u32 ExhaustedGeneration = 0xffffffffu;

        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] SpawnSlot* ResolveSpawn(Runtime::Impl& runtime, const SpawnHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.index >= runtime.config.maximumConcurrentSpawns || runtime.spawnSlots == nullptr)
                return nullptr;
            SpawnSlot& slot = runtime.spawnSlots[handle.index];
            return slot.lifecycle == SpawnSlotLifecycle::Occupied && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const SpawnSlot* ResolveSpawn(const Runtime::Impl& runtime, const SpawnHandle handle) noexcept
        {
            return ResolveSpawn(const_cast<Runtime::Impl&>(runtime), handle);
        }

        void AppendActive(Runtime::Impl& runtime, const u32 index) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            slot.previousActive = runtime.lastActiveSpawn;
            slot.nextActive = InvalidIndex;
            if (runtime.lastActiveSpawn != InvalidIndex)
                runtime.spawnSlots[runtime.lastActiveSpawn].nextActive = index;
            else
                runtime.firstActiveSpawn = index;
            runtime.lastActiveSpawn = index;
            if (runtime.materializationCursor == InvalidIndex)
                runtime.materializationCursor = index;
        }

        void RemoveActive(Runtime::Impl& runtime, const u32 index) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            const u32 previous = slot.previousActive;
            const u32 next = slot.nextActive;
            if (previous != InvalidIndex)
                runtime.spawnSlots[previous].nextActive = next;
            else
                runtime.firstActiveSpawn = next;
            if (next != InvalidIndex)
                runtime.spawnSlots[next].previousActive = previous;
            else
                runtime.lastActiveSpawn = previous;
            if (runtime.materializationCursor == index)
                runtime.materializationCursor = next != InvalidIndex ? next : runtime.firstActiveSpawn;
            slot.previousActive = InvalidIndex;
            slot.nextActive = InvalidIndex;
        }

        [[nodiscard]] u32 NextActive(const Runtime::Impl& runtime, const u32 index) noexcept
        {
            if (runtime.firstActiveSpawn == InvalidIndex)
                return InvalidIndex;
            if (index == InvalidIndex || index >= runtime.config.maximumConcurrentSpawns)
                return runtime.firstActiveSpawn;
            const u32 next = runtime.spawnSlots[index].nextActive;
            return next != InvalidIndex ? next : runtime.firstActiveSpawn;
        }

        [[nodiscard]] u32 MaterializationWeight(const resources::LoadPriority priority) noexcept
        {
            switch (priority)
            {
            case resources::LoadPriority::Background:
                return 1;
            case resources::LoadPriority::Low:
                return 2;
            case resources::LoadPriority::Normal:
                return 4;
            case resources::LoadPriority::High:
                return 8;
            case resources::LoadPriority::Critical:
                return 16;
            }
            return 1;
        }

        [[nodiscard]] u32* SpawnStateCounter(RuntimeStats& stats, const SpawnState state) noexcept
        {
            switch (state)
            {
            case SpawnState::Queued:
                return &stats.queuedSpawns;
            case SpawnState::Loading:
                return &stats.loadingSpawns;
            case SpawnState::Materializing:
                return &stats.materializingSpawns;
            case SpawnState::ReadyToCommit:
                return &stats.readyToCommitSpawns;
            case SpawnState::Succeeded:
                return &stats.succeededSpawns;
            case SpawnState::Failed:
                return &stats.failedSpawns;
            case SpawnState::Cancelled:
                return &stats.cancelledSpawns;
            default:
                return nullptr;
            }
        }

        void SetSpawnState(Runtime::Impl& runtime, SpawnSlot& slot, const SpawnState state) noexcept
        {
            if (slot.state == state)
                return;
            if (u32* const current = SpawnStateCounter(runtime.stats, slot.state))
            {
                VG_ASSERT_MSG(*current != 0, "Vellum spawn state accounting underflow");
                if (*current != 0)
                    --*current;
            }
            slot.state = state;
            if (u32* const next = SpawnStateCounter(runtime.stats, state))
                ++*next;
            if (state == SpawnState::Succeeded)
                ++runtime.stats.successfulSpawnOperations;
            else if (state == SpawnState::Failed)
                ++runtime.stats.failedSpawnOperations;
            else if (state == SpawnState::Cancelled)
                ++runtime.stats.cancelledSpawnOperations;
        }

        void ReleaseStagedNodeAccounting(Runtime::Impl& runtime, const SpawnSlot& slot) noexcept
        {
            VG_ASSERT_MSG(runtime.stats.stagedNodes >= slot.materializedNodes, "Vellum staged-node accounting underflow");
            runtime.stats.stagedNodes =
                runtime.stats.stagedNodes >= slot.materializedNodes ? runtime.stats.stagedNodes - slot.materializedNodes : 0;
        }

        [[nodiscard]] bool CleanupStaging(Runtime::Impl& runtime, SpawnSlot& slot, Failure* const failure) noexcept
        {
            if (slot.stagingInstance.IsValid())
            {
                if (!RetireInstance(runtime, slot.stagingInstance, failure))
                    return false;
                ReleaseStagedNodeAccounting(runtime, slot);
                slot.stagingInstance = {};
            }
            slot.document.Reset();
            return true;
        }

        [[nodiscard]] bool ClaimCommit(SpawnSlot& slot) noexcept
        {
            for (;;)
            {
                const u32 observed = slot.controlFlags.GetValue();
                if ((observed & SpawnControlCancelRequested) != 0)
                    return false;
                if ((observed & SpawnControlCommitClaimed) != 0)
                    return true;
                if (slot.controlFlags.CompareExchange(observed | SpawnControlCommitClaimed, observed) == observed)
                    return true;
            }
        }

        [[nodiscard]] bool Terminalize(Runtime::Impl& runtime, const u32 index, const SpawnState state, const FailureCode code,
                                       const resources::Failure resourceFailure, Failure* const failure) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            if (slot.state == SpawnState::Succeeded || slot.state == SpawnState::Failed || slot.state == SpawnState::Cancelled)
                return true;
            if (!CleanupStaging(runtime, slot, failure))
                return false;
            if (slot.request.IsValid())
                slot.request.Reset();
            slot.failure = code;
            slot.resourceFailure = resourceFailure;
            SetSpawnState(runtime, slot, state);
            RemoveActive(runtime, index);
            return true;
        }

        [[nodiscard]] bool FailSpawn(Runtime::Impl& runtime, const u32 index, const FailureCode code,
                                     const resources::Failure resourceFailure, Failure* const failure) noexcept
        {
            return Terminalize(runtime, index, SpawnState::Failed, code, resourceFailure, failure);
        }

        [[nodiscard]] bool StartQueued(Runtime::Impl& runtime, const u32 index, Failure* const failure) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            slot.request = runtime.streamer->Request(slot.reference, slot.priority);
            if (!slot.request.IsValid())
                return FailSpawn(runtime, index, FailureCode::ResourceFailure, resources::Failure::OutOfMemory, failure);
            SetSpawnState(runtime, slot, SpawnState::Loading);
            return true;
        }

        [[nodiscard]] bool PollLoading(Runtime::Impl& runtime, const u32 index, Failure* const failure) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            if (slot.request.HasLoaded())
            {
                resources::ResourceHandle handle = slot.request.Acquire();
                if (!handle.IsValid() || handle.GetType() != DocumentResourceType || handle.GetPath() != slot.reference.GetPath())
                    return FailSpawn(runtime, index, FailureCode::ResourceFailure, resources::Failure::IntegrityFailure, failure);
                auto* const document = static_cast<DocumentResource*>(handle.Get());
                if (document == nullptr || document->GetNodeCount() == 0)
                    return FailSpawn(runtime, index, FailureCode::DecodeFailure, resources::Failure::IntegrityFailure, failure);

                slot.documentGeneration = handle.GetGeneration();
                slot.totalNodes = document->GetNodeCount();
                slot.document = static_cast<resources::ResourceHandle&&>(handle);
                // The exact strong generation is owned before caller request interest
                // is released, so replacement cannot invalidate staged materialization.
                slot.request.Reset();

                if (slot.additionalNodeCapacity > runtime.config.maximumNodesPerDocument - slot.totalNodes)
                    return FailSpawn(runtime, index, FailureCode::CapacityExceeded, resources::Failure::None, failure);
                const u32 nodeCapacity = slot.totalNodes + slot.additionalNodeCapacity;
                InstanceState* instance = nullptr;
                Failure operationFailure;
                if (!AllocateInstance(runtime, nodeCapacity, slot.stagingInstance, instance, &operationFailure))
                {
                    const FailureCode code = operationFailure.code != FailureCode::None ? operationFailure.code : FailureCode::OutOfMemory;
                    return FailSpawn(runtime, index, code, resources::Failure::None, failure);
                }
                SetSpawnState(runtime, slot, SpawnState::Materializing);
                return true;
            }
            if (slot.request.HasFinished())
            {
                const resources::Failure resourceFailure = slot.request.GetError();
                return FailSpawn(runtime, index, resourceFailure == resources::Failure::Cancelled ? FailureCode::Cancelled : FailureCode::ResourceFailure,
                                 resourceFailure, failure);
            }
            return true;
        }

        [[nodiscard]] bool PublishReady(Runtime::Impl& runtime, const u32 index, Failure* const failure) noexcept
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            if (!ClaimCommit(slot))
                return Terminalize(runtime, index, SpawnState::Cancelled, FailureCode::Cancelled, resources::Failure::Cancelled, failure);
            if (runtime.stats.state != RuntimeState::Active || !runtime.stats.acceptingWork || !slot.document.IsValid() ||
                slot.document.GetPath() != slot.reference.GetPath() || slot.document.GetType() != DocumentResourceType ||
                slot.document.GetGeneration() != slot.documentGeneration || !slot.stagingInstance.IsValid())
                return Terminalize(runtime, index, SpawnState::Cancelled, FailureCode::Cancelled, resources::Failure::Cancelled, failure);

            auto* const document = static_cast<DocumentResource*>(slot.document.Get());
            InstanceState* const instance = ResolveInstance(runtime, slot.stagingInstance, true);
            if (document == nullptr || instance == nullptr || slot.materializedNodes != document->GetNodeCount())
                return FailSpawn(runtime, index, FailureCode::InternalError, resources::Failure::None, failure);

            instance->document = static_cast<resources::ResourceHandle&&>(slot.document);
            Failure operationFailure;
            if (!PublishInstance(runtime, slot.stagingInstance, document->GetRootIndex(), &operationFailure))
                return FailSpawn(runtime, index, operationFailure.code != FailureCode::None ? operationFailure.code : FailureCode::InternalError,
                                 resources::Failure::None, failure);

            ReleaseStagedNodeAccounting(runtime, slot);
            slot.resultInstance = slot.stagingInstance;
            slot.stagingInstance = {};
            slot.failure = FailureCode::None;
            slot.resourceFailure = resources::Failure::None;
            SetSpawnState(runtime, slot, SpawnState::Succeeded);
            RemoveActive(runtime, index);
            return true;
        }

        [[nodiscard]] bool ApplyControlRequests(Runtime::Impl& runtime, Failure* const failure) noexcept
        {
            for (u32 index = 0; index < runtime.config.maximumConcurrentSpawns; ++index)
            {
                SpawnSlot& slot = runtime.spawnSlots[index];
                if (slot.lifecycle != SpawnSlotLifecycle::Occupied)
                    continue;
                const u32 control = slot.controlFlags.GetValue();
                if ((control & SpawnControlCancelRequested) != 0 && slot.state != SpawnState::Succeeded &&
                    slot.state != SpawnState::Failed && slot.state != SpawnState::Cancelled)
                {
                    SpawnState outcome = SpawnState::Invalid;
                    if (!CancelSpawn(runtime, {index, slot.generation}, outcome, failure))
                        return false;
                }
                if (slot.lifecycle == SpawnSlotLifecycle::Occupied && (control & SpawnControlReleaseRequested) != 0)
                {
                    if (!ReleaseSpawn(runtime, {index, slot.generation}, failure))
                        return false;
                }
            }
            return true;
        }
    } // namespace

    bool InitializeSpawnRegistry(Runtime::Impl& runtime, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        usize slotsOffset = 0;
        BlockLayout layout;
        if (!layout.AddArray<SpawnSlot>(runtime.config.maximumConcurrentSpawns, slotsOffset) || !layout.Finish() ||
            !AllocateRuntimeBlock(layout, runtime.spawnBlock))
            return Fail(failure, FailureCode::OutOfMemory, "Vellum spawn registry allocation failed");
        runtime.spawnSlots = BindArray<SpawnSlot>(runtime.spawnBlock, slotsOffset);
        runtime.firstFreeSpawn = 0;
        for (u32 index = 0; index < runtime.config.maximumConcurrentSpawns; ++index)
        {
            ::new (&runtime.spawnSlots[index]) SpawnSlot;
            runtime.spawnSlots[index].nextFree = index + 1u < runtime.config.maximumConcurrentSpawns ? index + 1u : InvalidIndex;
        }
        runtime.stats.spawnCapacity = runtime.config.maximumConcurrentSpawns;
        runtime.stats.freeSpawnSlots = runtime.config.maximumConcurrentSpawns;
        return true;
    }

    void DestroySpawnRegistry(Runtime::Impl& runtime) noexcept
    {
        if (runtime.spawnSlots != nullptr)
        {
            for (u32 index = 0; index < runtime.config.maximumConcurrentSpawns; ++index)
                runtime.spawnSlots[index].~SpawnSlot();
        }
        memory::Free(runtime.spawnBlock);
        runtime.spawnSlots = nullptr;
        runtime.firstFreeSpawn = InvalidIndex;
        runtime.firstDeferredSpawn = InvalidIndex;
        runtime.firstActiveSpawn = InvalidIndex;
        runtime.lastActiveSpawn = InvalidIndex;
        runtime.materializationCursor = InvalidIndex;
    }

    void PromoteDeferredSpawns(Runtime::Impl& runtime) noexcept
    {
        u32 index = runtime.firstDeferredSpawn;
        runtime.firstDeferredSpawn = InvalidIndex;
        while (index != InvalidIndex)
        {
            SpawnSlot& slot = runtime.spawnSlots[index];
            const u32 next = slot.nextFree;
            slot.nextFree = InvalidIndex;
            if (runtime.stats.deferredSpawnSlots != 0)
                --runtime.stats.deferredSpawnSlots;
            if (slot.generation == ExhaustedGeneration)
            {
                slot.lifecycle = SpawnSlotLifecycle::Exhausted;
                ++runtime.stats.exhaustedSpawnSlots;
            }
            else
            {
                ++slot.generation;
                slot.lifecycle = SpawnSlotLifecycle::Free;
                slot.nextFree = runtime.firstFreeSpawn;
                runtime.firstFreeSpawn = index;
                ++runtime.stats.freeSpawnSlots;
            }
            index = next;
        }
    }

    bool RequestSpawn(Runtime::Impl& runtime, const SpawnDesc& desc, SpawnHandle& spawn, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        spawn = {};
        if (!desc.document.IsValid() || !desc.document.IsTyped() || desc.document.ExpectedType() != DocumentResourceType ||
            desc.additionalNodeCapacity > runtime.config.maximumNodesPerDocument)
            return Fail(failure, FailureCode::InvalidArgument, "Vellum spawn request is invalid");
        if (runtime.firstFreeSpawn == InvalidIndex)
        {
            ++runtime.stats.rejectedAdmissions;
            return Fail(failure, FailureCode::CapacityExceeded, "Vellum spawn capacity is exhausted");
        }

        const u32 index = runtime.firstFreeSpawn;
        SpawnSlot& slot = runtime.spawnSlots[index];
        runtime.firstFreeSpawn = slot.nextFree;
        slot.reference = desc.document;
        slot.priority = desc.priority;
        slot.additionalNodeCapacity = desc.additionalNodeCapacity;
        slot.request = {};
        slot.document = {};
        slot.stagingInstance = {};
        slot.resultInstance = {};
        slot.resourceFailure = resources::Failure::None;
        slot.failure = FailureCode::None;
        slot.controlFlags.SetValue(0);
        SetSpawnState(runtime, slot, SpawnState::Queued);
        slot.lifecycle = SpawnSlotLifecycle::Occupied;
        slot.nextFree = InvalidIndex;
        slot.previousActive = InvalidIndex;
        slot.nextActive = InvalidIndex;
        slot.documentGeneration = 0;
        slot.materializedNodes = 0;
        slot.totalNodes = 0;
        AppendActive(runtime, index);
        if (runtime.stats.freeSpawnSlots != 0)
            --runtime.stats.freeSpawnSlots;
        ++runtime.stats.liveSpawns;
        ++runtime.stats.totalSpawnRequests;
        if (runtime.stats.liveSpawns > runtime.stats.peakLiveSpawns)
            runtime.stats.peakLiveSpawns = runtime.stats.liveSpawns;
        spawn = {index, slot.generation};
        return true;
    }

    bool ProgressSpawns(Runtime::Impl& runtime, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!ApplyControlRequests(runtime, failure))
            return false;
        for (u32 index = runtime.firstActiveSpawn; index != InvalidIndex;)
        {
            const u32 next = runtime.spawnSlots[index].nextActive;
            if (runtime.spawnSlots[index].state == SpawnState::Queued && !StartQueued(runtime, index, failure))
                return false;
            if (runtime.spawnSlots[index].lifecycle == SpawnSlotLifecycle::Occupied &&
                runtime.spawnSlots[index].state == SpawnState::Loading && !PollLoading(runtime, index, failure))
                return false;
            index = next;
        }

        u32 materializingCount = 0;
        u64 remainingWeight = 0;
        for (u32 index = runtime.firstActiveSpawn; index != InvalidIndex; index = runtime.spawnSlots[index].nextActive)
            if (runtime.spawnSlots[index].state == SpawnState::Materializing)
            {
                ++materializingCount;
                remainingWeight += static_cast<u64>(MaterializationWeight(runtime.spawnSlots[index].priority));
            }

        u32 remainingBudget = runtime.config.maximumMaterializedNodesPerUpdate;
        u32 cursor = runtime.materializationCursor;
        if (cursor == InvalidIndex || cursor >= runtime.config.maximumConcurrentSpawns ||
            runtime.spawnSlots[cursor].lifecycle != SpawnSlotLifecycle::Occupied)
            cursor = runtime.firstActiveSpawn;
        u32 remainingSpawns = materializingCount;
        u32 searchedWithoutWork = 0;
        while (remainingBudget != 0 && remainingSpawns != 0 && cursor != InvalidIndex)
        {
            const u32 index = cursor;
            cursor = NextActive(runtime, index);
            SpawnSlot& slot = runtime.spawnSlots[index];
            const u32 weight = MaterializationWeight(slot.priority);
            if ((slot.controlFlags.GetValue() & SpawnControlCancelRequested) != 0)
            {
                SpawnState outcome = SpawnState::Invalid;
                if (!CancelSpawn(runtime, {index, slot.generation}, outcome, failure))
                    return false;
                remainingWeight = remainingWeight >= weight ? remainingWeight - static_cast<u64>(weight) : 0;
                --remainingSpawns;
                continue;
            }
            if (slot.state != SpawnState::Materializing)
            {
                if (++searchedWithoutWork > runtime.stats.liveSpawns + 1u)
                    break;
                continue;
            }
            searchedWithoutWork = 0;
            u32 quota = 1;
            if (remainingWeight != 0)
            {
                const u64 weighted = static_cast<u64>(remainingBudget) * static_cast<u64>(weight);
                const u64 share = weighted / remainingWeight;
                quota = static_cast<u32>(share == 0 ? 1 : (share > remainingBudget ? remainingBudget : share));
            }
            const auto* const document = static_cast<const DocumentResource*>(slot.document.Get());
            InstanceState* const instance = ResolveInstance(runtime, slot.stagingInstance, true);
            if (document == nullptr || instance == nullptr)
                return FailSpawn(runtime, index, FailureCode::InternalError, resources::Failure::None, failure);
            u32 used = 0;
            while (used < quota && slot.materializedNodes < slot.totalNodes)
            {
                Failure operationFailure;
                if (!MaterializeDocumentNode(runtime, *instance, *document, slot.materializedNodes, &operationFailure))
                    return FailSpawn(runtime, index, operationFailure.code != FailureCode::None ? operationFailure.code : FailureCode::InternalError,
                                     resources::Failure::None, failure);
                ++slot.materializedNodes;
                ++runtime.stats.materializedNodes;
                ++runtime.stats.stagedNodes;
                if (runtime.stats.stagedNodes > runtime.stats.peakStagedNodes)
                    runtime.stats.peakStagedNodes = runtime.stats.stagedNodes;
                ++used;
            }
            remainingBudget -= used;
            remainingWeight = remainingWeight >= weight ? remainingWeight - static_cast<u64>(weight) : 0;
            --remainingSpawns;
            if (slot.materializedNodes == slot.totalNodes)
                SetSpawnState(runtime, slot, SpawnState::ReadyToCommit);
        }
        runtime.materializationCursor = cursor;

        for (u32 index = runtime.firstActiveSpawn; index != InvalidIndex;)
        {
            const u32 next = runtime.spawnSlots[index].nextActive;
            if (runtime.spawnSlots[index].state == SpawnState::ReadyToCommit && !PublishReady(runtime, index, failure))
                return false;
            index = next;
        }
        return true;
    }

    bool CancelSpawn(Runtime::Impl& runtime, const SpawnHandle spawn, SpawnState& outcome, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        outcome = SpawnState::Invalid;
        SpawnSlot* const slot = ResolveSpawn(runtime, spawn);
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn cancellation references a stale handle");
        if (slot->state == SpawnState::Succeeded || slot->state == SpawnState::Failed || slot->state == SpawnState::Cancelled)
        {
            outcome = slot->state;
            return true;
        }
        static_cast<void>(slot->controlFlags.Or(SpawnControlCancelRequested));
        if (slot->request.IsValid())
        {
            resources::Failure terminalFailure = resources::Failure::None;
            const resources::PipelineCancelStatus status = slot->request.CancelWithStatus(&terminalFailure);
            if (status == resources::PipelineCancelStatus::Failed)
            {
                if (!FailSpawn(runtime, spawn.index, FailureCode::ResourceFailure, terminalFailure, failure))
                    return false;
                outcome = SpawnState::Failed;
                return true;
            }
        }
        const u32 index = spawn.index;
        if (!Terminalize(runtime, index, SpawnState::Cancelled, FailureCode::Cancelled, resources::Failure::Cancelled, failure))
            return false;
        outcome = SpawnState::Cancelled;
        return true;
    }

    bool ReleaseSpawn(Runtime::Impl& runtime, const SpawnHandle spawn, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        SpawnSlot* const slot = ResolveSpawn(runtime, spawn);
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn release references a stale handle");
        if (slot->state != SpawnState::Succeeded && slot->state != SpawnState::Failed && slot->state != SpawnState::Cancelled)
        {
            SpawnState outcome = SpawnState::Invalid;
            if (!CancelSpawn(runtime, spawn, outcome, failure))
                return false;
        }
        if (slot->resultInstance.IsValid())
        {
            if (ResolveInstance(runtime, slot->resultInstance) != nullptr && !RetireInstance(runtime, slot->resultInstance, failure))
                return false;
            slot->resultInstance = {};
        }
        slot->request.Reset();
        slot->document.Reset();
        slot->reference = {};
        slot->stagingInstance = {};
        slot->resourceFailure = resources::Failure::None;
        slot->failure = FailureCode::None;
        SetSpawnState(runtime, *slot, SpawnState::Invalid);
        slot->additionalNodeCapacity = 0;
        slot->documentGeneration = 0;
        slot->materializedNodes = 0;
        slot->totalNodes = 0;
        slot->controlFlags.SetValue(0);
        slot->lifecycle = SpawnSlotLifecycle::DeferredFree;
        slot->nextFree = runtime.firstDeferredSpawn;
        runtime.firstDeferredSpawn = spawn.index;
        ++runtime.stats.deferredSpawnSlots;
        if (runtime.stats.liveSpawns != 0)
            --runtime.stats.liveSpawns;
        return true;
    }

    bool TakeSpawnResult(Runtime::Impl& runtime, const SpawnHandle spawn, VellumInstanceHandle& instance, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        instance = {};
        SpawnSlot* const slot = ResolveSpawn(runtime, spawn);
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn result references a stale handle");
        if (slot->state != SpawnState::Succeeded || !slot->resultInstance.IsValid())
            return Fail(failure, FailureCode::InvalidState, "Vellum spawn result is not ready");
        if (ResolveInstance(runtime, slot->resultInstance) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn result instance is no longer live");
        instance = slot->resultInstance;
        slot->resultInstance = {};
        if (!ReleaseSpawn(runtime, spawn, failure))
        {
            slot->resultInstance = instance;
            instance = {};
            return false;
        }
        return true;
    }

    bool GetSpawnInfo(const Runtime::Impl& runtime, const SpawnHandle spawn, SpawnInfo& info, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        info = {};
        const SpawnSlot* const slot = ResolveSpawn(runtime, spawn);
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum spawn query references a stale handle");
        info.spawn = spawn;
        info.state = slot->state;
        info.document = slot->reference;
        info.instance = slot->resultInstance;
        info.failure = slot->failure;
        info.resourceFailure = slot->resourceFailure;
        info.documentGeneration = slot->documentGeneration;
        info.materializedNodes = slot->materializedNodes;
        info.totalNodes = slot->totalNodes;
        return true;
    }

    bool QuiesceSpawns(Runtime::Impl& runtime, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        while (runtime.firstActiveSpawn != InvalidIndex)
        {
            const u32 index = runtime.firstActiveSpawn;
            SpawnState outcome = SpawnState::Invalid;
            if (!CancelSpawn(runtime, {index, runtime.spawnSlots[index].generation}, outcome, failure))
                return false;
        }
        if (!ApplyControlRequests(runtime, failure))
            return false;
        return true;
    }
} // namespace vanguard::vellum::detail

namespace vanguard::vellum
{
    SpawnRequest::SpawnRequest(void* const control, const SpawnHandle handle) noexcept : m_control(control), m_handle(handle) {}

    SpawnRequest::SpawnRequest(SpawnRequest&& other) noexcept : m_control(other.m_control), m_handle(other.m_handle)
    {
        other.m_control = nullptr;
        other.m_handle = {};
    }

    SpawnRequest::~SpawnRequest()
    {
        Reset();
    }

    SpawnRequest& SpawnRequest::operator=(SpawnRequest&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_control = other.m_control;
        m_handle = other.m_handle;
        other.m_control = nullptr;
        other.m_handle = {};
        return *this;
    }

    bool SpawnRequest::IsValid() const noexcept
    {
        return m_control != nullptr && m_handle.IsValid();
    }

    void SpawnRequest::Reset() noexcept
    {
        if (m_control != nullptr && m_handle.IsValid())
        {
            auto* const slot = static_cast<detail::SpawnSlot*>(m_control);
            static_cast<void>(slot->controlFlags.Or(detail::SpawnControlCancelRequested | detail::SpawnControlReleaseRequested));
        }
        m_control = nullptr;
        m_handle = {};
    }
} // namespace vanguard::vellum
