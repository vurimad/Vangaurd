#include <vanguard/rendering/visibility_feedback.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 InvalidSlotIndex = ~u32{0};

        void ClearFailure(RenderSceneFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneFailure* const failure, const RenderSceneFailureCode code, const char* const message,
                                const RenderSceneHandle scene = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, scene, {}, message};
            return false;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] bool ValidBounds(const RenderProxyBounds& bounds) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
                if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) || bounds.minimum[axis] > bounds.maximum[axis])
                    return false;
            return true;
        }

        [[nodiscard]] bool ValidFrustum(const VisibilityFrustum& frustum) noexcept
        {
            if (frustum.planeCount == 0 || frustum.planeCount > MaximumVisibilityFrustumPlanes)
                return false;
            for (u32 planeIndex = 0; planeIndex < frustum.planeCount; ++planeIndex)
            {
                const VisibilityPlane& plane = frustum.planes[planeIndex];
                if (!std::isfinite(plane.normal[0]) || !std::isfinite(plane.normal[1]) || !std::isfinite(plane.normal[2]) || !std::isfinite(plane.distance))
                    return false;
                const f32 lengthSquared = plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] + plane.normal[2] * plane.normal[2];
                if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool BoundsIntersectsFrustum(const RenderProxyBounds& bounds, const VisibilityFrustum& frustum) noexcept
        {
            for (u32 planeIndex = 0; planeIndex < frustum.planeCount; ++planeIndex)
            {
                const VisibilityPlane& plane = frustum.planes[planeIndex];
                const f32 x = plane.normal[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
                const f32 y = plane.normal[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
                const f32 z = plane.normal[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
                if (plane.normal[0] * x + plane.normal[1] * y + plane.normal[2] * z + plane.distance < 0.0f)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidPolicy(const VisibilityViewPolicy& policy) noexcept
        {
            switch (policy.kind)
            {
            case VisibilityViewPolicyKind::SpecificView:
                return policy.view.IsValid();
            case VisibilityViewPolicyKind::ViewFamily:
                return policy.family.IsValid();
            case VisibilityViewPolicyKind::StreamingAuthority:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool CopyName(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < capacity && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }
    } // namespace

    struct VisibilityFeedbackService::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct ProbeSlot
        {
            VisibilityProbeSnapshot snapshot;
            VisibilityFeedback feedback[2];
            u32 generation = 0;
            u32 activeIndex = InvalidSlotIndex;
            u32 nextFree = InvalidSlotIndex;
            bool active = false;
        };

        Impl(RenderSceneManager& manager, const RenderSceneHandle sceneHandle, const VisibilityFeedbackServiceConfig& value) noexcept
            : scenes(&manager), scene(sceneHandle), config(value), probes(memory::pools::Rendering::GetInstance()),
              activeProbeIndices(memory::pools::Rendering::GetInstance()), evaluationViews(memory::pools::Rendering::GetInstance()),
              evaluationViewNext(memory::pools::Rendering::GetInstance()), authorityViewOrdinals(memory::pools::Rendering::GetInstance()),
              completedBatchSerials(memory::pools::Rendering::GetInstance())
        {
            probes.Reserve(config.maximumProbes);
            activeProbeIndices.Reserve(config.maximumProbes);
            evaluationViews.Resize(config.maximumViews);
            evaluationViewNext.Resize(config.maximumViews);
            authorityViewOrdinals.Resize(config.maximumViews);
            completedBatchSerials.Resize(config.maximumProbes);
            for (u32 index = 0; index < completedBatchSerials.Size(); ++index)
                completedBatchSerials[index] = 0;
            stats.initialized = true;
        }

        [[nodiscard]] ProbeSlot* ResolveProbe(const VisibilityProbeHandle probe) noexcept
        {
            if (!probe.IsValid() || probe.index >= probes.Size())
                return nullptr;
            ProbeSlot& slot = probes[probe.index];
            return slot.active && slot.generation == probe.generation ? &slot : nullptr;
        }

        [[nodiscard]] const ProbeSlot* ResolveProbe(const VisibilityProbeHandle probe) const noexcept
        {
            return const_cast<Impl*>(this)->ResolveProbe(probe);
        }

        [[nodiscard]] bool StorageReady() const noexcept
        {
            return probes.Capacity() >= config.maximumProbes && activeProbeIndices.Capacity() >= config.maximumProbes &&
                   evaluationViews.Size() == config.maximumViews && evaluationViewNext.Size() == config.maximumViews &&
                   authorityViewOrdinals.Size() == config.maximumViews && completedBatchSerials.Size() == config.maximumProbes;
        }

        [[nodiscard]] u32 AcquireProbeSlot() noexcept
        {
            if (firstFreeProbe != InvalidSlotIndex)
            {
                const u32 index = firstFreeProbe;
                firstFreeProbe = probes[index].nextFree;
                probes[index].nextFree = InvalidSlotIndex;
                return index;
            }
            if (probes.Size() >= config.maximumProbes)
                return InvalidSlotIndex;
            probes.PushBack({});
            return probes.Size() - 1u;
        }

        void ReleaseProbeSlot(const u32 index) noexcept
        {
            ProbeSlot& slot = probes[index];
            const u32 lastActive = activeProbeIndices.Size() - 1u;
            const u32 movedProbe = activeProbeIndices[lastActive];
            activeProbeIndices[slot.activeIndex] = movedProbe;
            probes[movedProbe].activeIndex = slot.activeIndex;
            activeProbeIndices.PopBack();
            const u32 generation = slot.generation;
            slot = {};
            slot.generation = generation;
            slot.nextFree = firstFreeProbe;
            firstFreeProbe = index;
        }

        RenderSceneManager* scenes = nullptr;
        RenderSceneHandle scene;
        VisibilityFeedbackServiceConfig config;
        containers::DynamicArray<ProbeSlot> probes;
        containers::DynamicArray<u32> activeProbeIndices;
        containers::DynamicArray<VisibilityFeedbackView> evaluationViews;
        containers::DynamicArray<u32> evaluationViewNext;
        containers::DynamicArray<u32> authorityViewOrdinals;
        containers::DynamicArray<u64> completedBatchSerials;
        u32 firstFreeProbe = InvalidSlotIndex;
        u32 evaluationViewCount = 0;
        u32 authorityViewCount = 0;
        u32 evaluationProbeCount = 0;
        u32 evaluationBatchCount = 0;
        u32 evaluationTargetProbesPerBatch = 0;
        u32 publishedBank = 0;
        u32 writeBank = 1;
        u32 viewAdmissionStamp = 0;
        u32 viewAdmissionStamps[MaximumRenderViews]{};
        u32 viewAdmissionGenerations[MaximumRenderViews]{};
        u32 viewOrdinals[MaximumRenderViews]{};
        u32 familyAdmissionStamp = 0;
        u32 familyAdmissionStamps[MaximumRenderViewFamilies]{};
        u32 familyAdmissionGenerations[MaximumRenderViewFamilies]{};
        u32 familyFirstViews[MaximumRenderViewFamilies]{};
        u64 evaluationSerial = 0;
        u64 evaluationMutationEpoch = 0;
        u64 evaluationFrame = 0;
        u64 evaluationViewSetRevision = 0;
        concurrency::Atomic<bool> evaluationOpen{false};
        VisibilityFeedbackStats stats;
    };

    VisibilityFeedbackService::~VisibilityFeedbackService()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool VisibilityFeedbackService::Initialize(RenderSceneManager& scenes, const RenderSceneHandle scene, const VisibilityFeedbackServiceConfig& config,
                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderSceneFailureCode::AlreadyInitialized, "VisibilityFeedbackService is already initialized", scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "VisibilityFeedbackService must initialize on the main thread", scene);
        if (!scenes.IsAlive(scene) || config.maximumProbes == 0 || config.maximumProbes > MaximumVisibilityProbes || config.maximumViews == 0 ||
            config.maximumViews > MaximumVisibilityFeedbackViews)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility feedback configuration", scene);

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "visibility feedback service allocation failed", scene);
        Impl* const impl = ::new (block.address) Impl(scenes, scene, config);
        if (!impl->StorageReady())
        {
            impl->~Impl();
            memory::Free(block);
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "visibility feedback retained storage allocation failed", scene);
        }
        if (!scenes.AttachVisibilityFeedback(scene))
        {
            impl->~Impl();
            memory::Free(block);
            return Fail(failure, RenderSceneFailureCode::Busy, "RenderScene already owns a visibility feedback service", scene);
        }
        m_impl = impl;
        return true;
    }

    bool VisibilityFeedbackService::Shutdown(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "VisibilityFeedbackService must shutdown on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback shutdown is blocked by an open evaluation", m_impl->scene);
        if (!m_impl->activeProbeIndices.Empty())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback shutdown is blocked by live probes", m_impl->scene);
        if (!m_impl->scenes->DetachVisibilityFeedback(m_impl->scene))
            return Fail(failure, RenderSceneFailureCode::InvalidState, "visibility feedback service could not detach from its scene", m_impl->scene);
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool VisibilityFeedbackService::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool VisibilityFeedbackService::CreateProbe(const VisibilityProbeDesc& desc, VisibilityProbeHandle& probe, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        probe = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility probes must be created on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility probes cannot change during evaluation", m_impl->scene);
        char name[MaximumVisibilityProbeNameBytes]{};
        if (!ValidBounds(desc.bounds) || !ValidPolicy(desc.viewPolicy) || desc.queryMask == 0 ||
            !CopyName(name, MaximumVisibilityProbeNameBytes, desc.debugName))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility probe descriptor", m_impl->scene);
        const u32 index = m_impl->AcquireProbeSlot();
        if (index == InvalidSlotIndex)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "visibility probe capacity exceeded", m_impl->scene);
        }
        Impl::ProbeSlot& slot = m_impl->probes[index];
        const u32 generation = NextGeneration(slot.generation);
        slot = {};
        slot.active = true;
        slot.generation = generation;
        slot.activeIndex = m_impl->activeProbeIndices.Size();
        slot.snapshot.handle = {index, generation};
        slot.snapshot.scene = m_impl->scene;
        slot.snapshot.bounds = desc.bounds;
        slot.snapshot.viewPolicy = desc.viewPolicy;
        slot.snapshot.queryMask = desc.queryMask;
        slot.snapshot.descriptorRevision = 1;
        static_cast<void>(CopyName(slot.snapshot.debugName, MaximumVisibilityProbeNameBytes, name));
        for (u32 bank = 0; bank < 2; ++bank)
        {
            slot.feedback[bank].probe = slot.snapshot.handle;
            slot.feedback[bank].scene = m_impl->scene;
            slot.feedback[bank].viewPolicy = desc.viewPolicy;
        }
        m_impl->activeProbeIndices.PushBack(index);
        ++m_impl->stats.activeProbes;
        ++m_impl->stats.createdProbes;
        probe = slot.snapshot.handle;
        return true;
    }

    bool VisibilityFeedbackService::UpdateProbe(const VisibilityProbeHandle probe, const VisibilityProbeDesc& desc, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility probes must update on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility probes cannot change during evaluation", m_impl->scene);
        Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        char name[MaximumVisibilityProbeNameBytes]{};
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale visibility probe handle", m_impl->scene);
        if (!ValidBounds(desc.bounds) || !ValidPolicy(desc.viewPolicy) || desc.queryMask == 0 ||
            !CopyName(name, MaximumVisibilityProbeNameBytes, desc.debugName))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility probe descriptor", m_impl->scene);
        slot->snapshot.bounds = desc.bounds;
        slot->snapshot.viewPolicy = desc.viewPolicy;
        slot->snapshot.queryMask = desc.queryMask;
        ++slot->snapshot.descriptorRevision;
        if (slot->snapshot.descriptorRevision == 0)
            ++slot->snapshot.descriptorRevision;
        static_cast<void>(CopyName(slot->snapshot.debugName, MaximumVisibilityProbeNameBytes, name));
        for (u32 bank = 0; bank < 2; ++bank)
        {
            slot->feedback[bank] = {};
            slot->feedback[bank].probe = probe;
            slot->feedback[bank].scene = m_impl->scene;
            slot->feedback[bank].viewPolicy = desc.viewPolicy;
        }
        return true;
    }

    bool VisibilityFeedbackService::DestroyProbe(VisibilityProbeHandle& probe, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility probes must be destroyed on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility probes cannot change during evaluation", m_impl->scene);
        Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale visibility probe handle", m_impl->scene);
        m_impl->ReleaseProbeSlot(probe.index);
        --m_impl->stats.activeProbes;
        ++m_impl->stats.destroyedProbes;
        probe = {};
        return true;
    }

    bool VisibilityFeedbackService::ReadProbe(const VisibilityProbeHandle probe, VisibilityProbeSnapshot& snapshot,
                                              RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility probes must be read on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility probes cannot be read during evaluation", m_impl->scene);
        const Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale visibility probe handle", m_impl->scene);
        snapshot = slot->snapshot;
        return true;
    }

    bool VisibilityFeedbackService::ReadFeedback(const VisibilityProbeHandle probe, const u64 currentFrame, VisibilityFeedback& feedback,
                                                 RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        feedback = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility feedback must be read on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback cannot be read during evaluation", m_impl->scene);
        const Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "invalid or stale visibility probe handle", m_impl->scene);
        const VisibilityFeedback& published = slot->feedback[m_impl->publishedBank];
        if (currentFrame < published.publishedFrame)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "visibility feedback cannot be read from an earlier frame", m_impl->scene);
        feedback = published;
        feedback.ageInFrames = feedback.evaluatedFrame != 0 && currentFrame >= feedback.evaluatedFrame ? currentFrame - feedback.evaluatedFrame : currentFrame;
        return true;
    }

    bool VisibilityFeedbackService::PrepareEvaluation(const VisibilityFeedbackEvaluationRequest& request,
                                                      const containers::ArraySpan<VisibilityFeedbackEvaluationBatch> batchStorage,
                                                      VisibilityFeedbackEvaluationPlan& plan, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        plan = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility feedback planning must run on the main thread", m_impl->scene);
        if (m_impl->evaluationOpen.GetValue())
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback evaluation is already open", m_impl->scene);
        if (request.frameSerial == 0 || request.frameSerial <= m_impl->stats.publishedFrame || request.viewSetRevision == 0 ||
            request.targetProbesPerBatch == 0 || request.views.Size() > m_impl->config.maximumViews)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility feedback evaluation request", m_impl->scene);

        RenderSceneSnapshot sceneSnapshot;
        if (!m_impl->scenes->GetSnapshot(m_impl->scene, sceneSnapshot))
            return Fail(failure, RenderSceneFailureCode::InvalidHandle, "visibility feedback scene is no longer alive", m_impl->scene);
        if (sceneSnapshot.framePrepared || sceneSnapshot.pendingProxyMutations != 0 || sceneSnapshot.completedMutationEpoch != request.mutationEpoch)
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback requires the exact completed scene epoch", m_impl->scene);

        u32 admissionStamp = ++m_impl->viewAdmissionStamp;
        if (admissionStamp == 0)
        {
            for (u32 index = 0; index < MaximumRenderViews; ++index)
                m_impl->viewAdmissionStamps[index] = 0;
            admissionStamp = ++m_impl->viewAdmissionStamp;
        }
        u32 familyStamp = ++m_impl->familyAdmissionStamp;
        if (familyStamp == 0)
        {
            for (u32 index = 0; index < MaximumRenderViewFamilies; ++index)
                m_impl->familyAdmissionStamps[index] = 0;
            familyStamp = ++m_impl->familyAdmissionStamp;
        }
        m_impl->authorityViewCount = 0;
        for (u32 index = 0; index < request.views.Size(); ++index)
        {
            const VisibilityFeedbackView& view = request.views[index];
            if (!view.view.IsValid() || !view.family.IsValid() || !ValidFrustum(view.frustum) || view.queryMask == 0 ||
                m_impl->viewAdmissionStamps[view.view.index] == admissionStamp)
                return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid or duplicate visibility feedback view", m_impl->scene);
            m_impl->viewAdmissionStamps[view.view.index] = admissionStamp;
            m_impl->viewAdmissionGenerations[view.view.index] = view.view.generation;
            m_impl->viewOrdinals[view.view.index] = index;
            if (m_impl->familyAdmissionStamps[view.family.index] != familyStamp)
            {
                m_impl->familyAdmissionStamps[view.family.index] = familyStamp;
                m_impl->familyAdmissionGenerations[view.family.index] = view.family.generation;
                m_impl->familyFirstViews[view.family.index] = InvalidSlotIndex;
            }
            else if (m_impl->familyAdmissionGenerations[view.family.index] != view.family.generation)
                return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "visibility feedback view family generations are inconsistent", m_impl->scene);
            m_impl->evaluationViewNext[index] = m_impl->familyFirstViews[view.family.index];
            m_impl->familyFirstViews[view.family.index] = index;
            if (view.streamingAuthority)
                m_impl->authorityViewOrdinals[m_impl->authorityViewCount++] = index;
            m_impl->evaluationViews[index] = view;
        }

        const u32 probeCount = m_impl->activeProbeIndices.Size();
        const u32 batchCount = probeCount / request.targetProbesPerBatch + (probeCount % request.targetProbesPerBatch != 0 ? 1u : 0u);
        plan.scene = m_impl->scene;
        plan.mutationEpoch = request.mutationEpoch;
        plan.frameSerial = request.frameSerial;
        plan.viewSetRevision = request.viewSetRevision;
        plan.probeCount = probeCount;
        plan.batchCount = batchCount;
        if (batchCount > batchStorage.Size())
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded, "visibility feedback batch storage capacity was exceeded", m_impl->scene);

        u64 serial = ++m_impl->evaluationSerial;
        if (serial == 0)
            serial = ++m_impl->evaluationSerial;
        plan.serial = serial;
        for (u32 batchIndex = 0; batchIndex < batchCount; ++batchIndex)
        {
            const u32 first = batchIndex * request.targetProbesPerBatch;
            const u32 remaining = probeCount - first;
            const u32 count = remaining < request.targetProbesPerBatch ? remaining : request.targetProbesPerBatch;
            batchStorage[batchIndex] = {serial, batchIndex, first, count};
        }
        m_impl->evaluationViewCount = request.views.Size();
        m_impl->evaluationProbeCount = probeCount;
        m_impl->evaluationBatchCount = batchCount;
        m_impl->evaluationTargetProbesPerBatch = request.targetProbesPerBatch;
        m_impl->evaluationMutationEpoch = request.mutationEpoch;
        m_impl->evaluationFrame = request.frameSerial;
        m_impl->evaluationViewSetRevision = request.viewSetRevision;
        m_impl->writeBank = m_impl->publishedBank ^ 1u;
        m_impl->evaluationOpen.SetValue(true);
        m_impl->stats.evaluationOpen = true;
        return true;
    }

    bool VisibilityFeedbackService::EvaluateBatch(const VisibilityFeedbackEvaluationPlan& plan, const VisibilityFeedbackEvaluationBatch& batch,
                                                  VisibilityFeedbackBatchResult& result, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized", plan.scene);
        if (!plan.IsValid() || !batch.IsValid() || plan.scene != m_impl->scene || batch.serial != plan.serial ||
            plan.probeCount != m_impl->evaluationProbeCount || plan.batchCount != m_impl->evaluationBatchCount || batch.batchIndex >= plan.batchCount ||
            batch.firstActiveProbe > plan.probeCount || batch.probeCount > plan.probeCount - batch.firstActiveProbe)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "invalid visibility feedback evaluation batch", plan.scene);
        if (!m_impl->evaluationOpen.GetValue() || m_impl->evaluationSerial != plan.serial || m_impl->evaluationMutationEpoch != plan.mutationEpoch ||
            m_impl->evaluationFrame != plan.frameSerial || m_impl->evaluationViewSetRevision != plan.viewSetRevision)
            return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback plan is not the active evaluation", plan.scene);
        const u32 expectedFirst = batch.batchIndex * m_impl->evaluationTargetProbesPerBatch;
        const u32 expectedRemaining = plan.probeCount - expectedFirst;
        const u32 expectedCount = expectedRemaining < m_impl->evaluationTargetProbesPerBatch ? expectedRemaining : m_impl->evaluationTargetProbesPerBatch;
        if (batch.firstActiveProbe != expectedFirst || batch.probeCount != expectedCount)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor, "visibility feedback batch does not match its prepared disjoint range", plan.scene);

        const u32 end = batch.firstActiveProbe + batch.probeCount;
        for (u32 activeIndex = batch.firstActiveProbe; activeIndex < end; ++activeIndex)
        {
            Impl::ProbeSlot& slot = m_impl->probes[m_impl->activeProbeIndices[activeIndex]];
            VisibilityFeedback output = slot.feedback[m_impl->publishedBank];
            output.probe = slot.snapshot.handle;
            output.scene = m_impl->scene;
            output.mutationEpoch = plan.mutationEpoch;
            output.viewPolicy = slot.snapshot.viewPolicy;
            output.viewSetRevision = plan.viewSetRevision;
            output.publishedFrame = plan.frameSerial;
            output.ageInFrames = 0;
            output.contributingViews = 0;
            bool visible = false;
            const auto evaluateView = [&slot, &output, &visible, this](const u32 viewIndex) noexcept
            {
                const VisibilityFeedbackView& view = m_impl->evaluationViews[viewIndex];
                if ((slot.snapshot.queryMask & view.queryMask) == 0)
                    return;
                ++output.contributingViews;
                if (BoundsIntersectsFrustum(slot.snapshot.bounds, view.frustum))
                    visible = true;
            };
            switch (slot.snapshot.viewPolicy.kind)
            {
            case VisibilityViewPolicyKind::SpecificView:
            {
                const RenderViewId view = slot.snapshot.viewPolicy.view;
                if (m_impl->viewAdmissionStamps[view.index] == m_impl->viewAdmissionStamp && m_impl->viewAdmissionGenerations[view.index] == view.generation)
                    evaluateView(m_impl->viewOrdinals[view.index]);
                break;
            }
            case VisibilityViewPolicyKind::ViewFamily:
            {
                const RenderViewFamilyId family = slot.snapshot.viewPolicy.family;
                if (m_impl->familyAdmissionStamps[family.index] == m_impl->familyAdmissionStamp &&
                    m_impl->familyAdmissionGenerations[family.index] == family.generation)
                    for (u32 viewIndex = m_impl->familyFirstViews[family.index]; viewIndex != InvalidSlotIndex;
                         viewIndex = m_impl->evaluationViewNext[viewIndex])
                        evaluateView(viewIndex);
                break;
            }
            case VisibilityViewPolicyKind::StreamingAuthority:
                for (u32 authorityIndex = 0; authorityIndex < m_impl->authorityViewCount; ++authorityIndex)
                    evaluateView(m_impl->authorityViewOrdinals[authorityIndex]);
                break;
            }
            if (output.contributingViews == 0)
            {
                output.state = VisibilityFeedbackState::Unknown;
                ++result.unknownProbes;
            }
            else if (visible)
            {
                output.state = VisibilityFeedbackState::Visible;
                output.evaluatedFrame = plan.frameSerial;
                ++result.visibleProbes;
            }
            else
            {
                output.state = VisibilityFeedbackState::NotVisible;
                output.evaluatedFrame = plan.frameSerial;
                ++result.notVisibleProbes;
            }
            slot.feedback[m_impl->writeBank] = output;
            ++result.evaluatedProbes;
        }
        m_impl->completedBatchSerials[batch.batchIndex] = plan.serial;
        result.completed = true;
        return true;
    }

    bool VisibilityFeedbackService::CompleteEvaluation(const VisibilityFeedbackEvaluationPlan& plan, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized", plan.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility feedback completion must run on the main thread", plan.scene);
        if (!plan.IsValid() || plan.scene != m_impl->scene || !m_impl->evaluationOpen.GetValue() || m_impl->evaluationSerial != plan.serial ||
            m_impl->evaluationMutationEpoch != plan.mutationEpoch || m_impl->evaluationFrame != plan.frameSerial ||
            m_impl->evaluationViewSetRevision != plan.viewSetRevision || m_impl->evaluationProbeCount != plan.probeCount ||
            m_impl->evaluationBatchCount != plan.batchCount)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "visibility feedback plan is stale or already closed", plan.scene);
        for (u32 batchIndex = 0; batchIndex < plan.batchCount; ++batchIndex)
            if (m_impl->completedBatchSerials[batchIndex] != plan.serial)
                return Fail(failure, RenderSceneFailureCode::Busy, "visibility feedback evaluation batches have not completed", plan.scene);
        m_impl->publishedBank = m_impl->writeBank;
        m_impl->evaluationOpen.SetValue(false);
        m_impl->stats.publishedFrame = plan.frameSerial;
        m_impl->stats.evaluatedProbes += plan.probeCount;
        m_impl->stats.evaluationOpen = false;
        return true;
    }

    bool VisibilityFeedbackService::CancelEvaluation(const VisibilityFeedbackEvaluationPlan& plan, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized, "VisibilityFeedbackService is not initialized", plan.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread, "visibility feedback cancellation must run on the main thread", plan.scene);
        if (!plan.IsValid() || plan.scene != m_impl->scene || !m_impl->evaluationOpen.GetValue() || m_impl->evaluationSerial != plan.serial ||
            m_impl->evaluationMutationEpoch != plan.mutationEpoch || m_impl->evaluationFrame != plan.frameSerial ||
            m_impl->evaluationViewSetRevision != plan.viewSetRevision || m_impl->evaluationProbeCount != plan.probeCount ||
            m_impl->evaluationBatchCount != plan.batchCount)
            return Fail(failure, RenderSceneFailureCode::InvalidState, "visibility feedback plan is stale or already closed", plan.scene);
        m_impl->evaluationOpen.SetValue(false);
        ++m_impl->stats.cancelledEvaluations;
        m_impl->stats.evaluationOpen = false;
        return true;
    }

    VisibilityFeedbackStats VisibilityFeedbackService::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : VisibilityFeedbackStats{};
    }
} // namespace vanguard::rendering
