#include <vanguard/rendering/render_scene_feedback.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(RenderSceneFailure* const failure) noexcept
        {
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(RenderSceneFailure* const failure, const RenderSceneFailureCode code,
                                const char* const message, const RenderSceneHandle scene = {}) noexcept
        {
            if (failure != nullptr) *failure = {code, scene, {}, message};
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
                if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                    bounds.minimum[axis] > bounds.maximum[axis])
                    return false;
            return true;
        }

        [[nodiscard]] bool BoundsOverlap(const RenderProxyBounds& left,
                                         const RenderProxyBounds& right) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
                if (left.maximum[axis] < right.minimum[axis] || left.minimum[axis] > right.maximum[axis])
                    return false;
            return true;
        }

        [[nodiscard]] bool BoundsIntersectsFrustum(const RenderProxyBounds& bounds,
                                                   const VisibilityFrustum& frustum) noexcept
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

        void CopyName(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            u32 index = 0;
            if (source != nullptr)
                while (source[index] != '\0' && index + 1u < capacity)
                {
                    destination[index] = source[index];
                    ++index;
                }
            destination[index] = '\0';
        }
    } // namespace

    struct VisibilityFeedbackService::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct ViewSlot
        {
            VisibilityFeedbackViewDesc desc;
            bool active = false;
        };

        struct ProbeSlot
        {
            VisibilityProbeSnapshot snapshot;
            VisibilityFeedback feedback;
            VisibilityFeedbackState pendingState = VisibilityFeedbackState::Unknown;
            RenderSceneVersion pendingVersion;
            u64 pendingFrame = 0;
            u64 pendingViewSetRevision = 0;
            u32 pendingViews = 0;
            u32 generation = 0;
            bool active = false;
            bool pending = false;
        };

        Impl(RenderSceneManager& manager, const VisibilityFeedbackServiceConfig& config) noexcept
            : scenes(&manager), views(memory::pools::Rendering::GetInstance()),
              probes(memory::pools::Rendering::GetInstance())
        {
            views.Reserve(config.maximumViews);
            probes.Reserve(config.maximumProbes);
            for (u32 index = 0; index < config.maximumViews; ++index) views.PushBack({});
            for (u32 index = 0; index < config.maximumProbes; ++index) probes.PushBack({});
            stats.initialized = true;
            stats.viewSetRevision = 1;
        }

        [[nodiscard]] ViewSlot* FindView(const RenderSceneViewHandle view) noexcept
        {
            for (u32 index = 0; index < views.Size(); ++index)
                if (views[index].active && views[index].desc.view == view) return &views[index];
            return nullptr;
        }

        [[nodiscard]] const ViewSlot* FindView(const RenderSceneViewHandle view) const noexcept
        {
            return const_cast<Impl*>(this)->FindView(view);
        }

        [[nodiscard]] ProbeSlot* ResolveProbe(const VisibilityProbeHandle probe) noexcept
        {
            if (!probe.IsValid() || probe.index >= probes.Size()) return nullptr;
            ProbeSlot& slot = probes[probe.index];
            return slot.active && slot.generation == probe.generation ? &slot : nullptr;
        }

        [[nodiscard]] const ProbeSlot* ResolveProbe(const VisibilityProbeHandle probe) const noexcept
        {
            return const_cast<Impl*>(this)->ResolveProbe(probe);
        }

        [[nodiscard]] static bool PolicyMatches(const VisibilityViewPolicy& policy,
                                                const VisibilityFeedbackViewDesc& view) noexcept
        {
            switch (policy.kind)
            {
            case VisibilityViewPolicyKind::SpecificView:
                return policy.view == view.view;
            case VisibilityViewPolicyKind::ViewFamily:
                return policy.family != 0 && policy.family == view.family;
            case VisibilityViewPolicyKind::StreamingAuthority:
                return view.streamingAuthority;
            }
            return false;
        }

        RenderSceneManager* scenes = nullptr;
        containers::DynamicArray<ViewSlot> views;
        containers::DynamicArray<ProbeSlot> probes;
        VisibilityFeedbackStats stats;
        u64 frameSerial = 0;
        bool frameOpen = false;
    };

    VisibilityFeedbackService::~VisibilityFeedbackService()
    {
        if (m_impl != nullptr) static_cast<void>(Shutdown());
    }

    bool VisibilityFeedbackService::Initialize(RenderSceneManager& scenes,
                                               const VisibilityFeedbackServiceConfig& config,
                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderSceneFailureCode::AlreadyInitialized,
                        "VisibilityFeedbackService is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "VisibilityFeedbackService must initialize on the main thread");
        if (!scenes.IsInitialized() || config.maximumProbes == 0 ||
            config.maximumProbes > MaximumVisibilityProbes || config.maximumViews == 0 ||
            config.maximumViews > MaximumVisibilityFeedbackViews)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility feedback configuration");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                        "visibility feedback service allocation failed");
        m_impl = ::new (block.address) Impl(scenes, config);
        return true;
    }

    bool VisibilityFeedbackService::Shutdown(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "VisibilityFeedbackService must shutdown on the main thread");
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility feedback shutdown is blocked by an open frame");
        if (m_impl->stats.activeProbes != 0 || m_impl->stats.registeredViews != 0)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility feedback shutdown is blocked by live probes or views");
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

    bool VisibilityFeedbackService::RegisterView(const VisibilityFeedbackViewDesc& desc,
                                                 RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility feedback view registration must run on the main thread", desc.scene);
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility feedback views cannot change during end-frame publication", desc.scene);
        if (!desc.view.IsValid() || !m_impl->scenes->IsAlive(desc.scene))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility feedback view descriptor", desc.scene);
        if (Impl::ViewSlot* const existing = m_impl->FindView(desc.view))
        {
            existing->desc = desc;
            m_impl->stats.viewSetRevision = m_impl->stats.viewSetRevision + 1u;
            if (m_impl->stats.viewSetRevision == 0) m_impl->stats.viewSetRevision = 1;
            return true;
        }
        for (u32 index = 0; index < m_impl->views.Size(); ++index)
        {
            Impl::ViewSlot& slot = m_impl->views[index];
            if (slot.active) continue;
            slot.desc = desc;
            slot.active = true;
            ++m_impl->stats.registeredViews;
            m_impl->stats.viewSetRevision = m_impl->stats.viewSetRevision + 1u;
            if (m_impl->stats.viewSetRevision == 0) m_impl->stats.viewSetRevision = 1;
            return true;
        }
        ++m_impl->stats.rejectedOperations;
        return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                    "visibility feedback view capacity exceeded", desc.scene);
    }

    bool VisibilityFeedbackService::UnregisterView(const RenderSceneViewHandle view,
                                                   RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility feedback view removal must run on the main thread");
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility feedback views cannot change during end-frame publication");
        Impl::ViewSlot* const slot = m_impl->FindView(view);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale visibility feedback view");
        *slot = {};
        --m_impl->stats.registeredViews;
        m_impl->stats.viewSetRevision = m_impl->stats.viewSetRevision + 1u;
        if (m_impl->stats.viewSetRevision == 0) m_impl->stats.viewSetRevision = 1;
        return true;
    }

    bool VisibilityFeedbackService::CreateProbe(const VisibilityProbeDesc& desc,
                                                VisibilityProbeHandle& probe,
                                                RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        probe = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility probe creation must run on the main thread", desc.scene);
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility probes cannot change during end-frame publication", desc.scene);
        if (!m_impl->scenes->IsAlive(desc.scene) || !ValidBounds(desc.bounds) || desc.queryMask == 0 ||
            (desc.viewPolicy.kind == VisibilityViewPolicyKind::SpecificView && !desc.viewPolicy.view.IsValid()) ||
            (desc.viewPolicy.kind == VisibilityViewPolicyKind::ViewFamily && desc.viewPolicy.family == 0))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility probe descriptor", desc.scene);

        for (u32 index = 0; index < m_impl->probes.Size(); ++index)
        {
            Impl::ProbeSlot& slot = m_impl->probes[index];
            if (slot.active) continue;
            slot.generation = NextGeneration(slot.generation);
            slot.active = true;
            slot.pending = false;
            slot.snapshot = {};
            slot.snapshot.handle = {index, slot.generation};
            slot.snapshot.scene = desc.scene;
            slot.snapshot.bounds = desc.bounds;
            slot.snapshot.viewPolicy = desc.viewPolicy;
            slot.snapshot.queryMask = desc.queryMask;
            slot.snapshot.descriptorRevision = 1;
            CopyName(slot.snapshot.debugName, MaximumVisibilityProbeNameBytes, desc.debugName);
            slot.feedback = {};
            slot.feedback.probe = slot.snapshot.handle;
            slot.feedback.viewPolicy = slot.snapshot.viewPolicy;
            slot.feedback.viewSetRevision = m_impl->stats.viewSetRevision;
            probe = slot.snapshot.handle;
            ++m_impl->stats.activeProbes;
            ++m_impl->stats.createdProbes;
            return true;
        }
        ++m_impl->stats.rejectedOperations;
        return Fail(failure, RenderSceneFailureCode::CapacityExceeded,
                    "visibility probe capacity exceeded", desc.scene);
    }

    bool VisibilityFeedbackService::UpdateProbe(const VisibilityProbeHandle probe,
                                                const VisibilityProbeDesc& desc,
                                                RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized", desc.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility probe update must run on the main thread", desc.scene);
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility probes cannot change during end-frame publication", desc.scene);
        Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale visibility probe", desc.scene);
        if (!(slot->snapshot.scene == desc.scene) || !ValidBounds(desc.bounds) || desc.queryMask == 0 ||
            (desc.viewPolicy.kind == VisibilityViewPolicyKind::SpecificView && !desc.viewPolicy.view.IsValid()) ||
            (desc.viewPolicy.kind == VisibilityViewPolicyKind::ViewFamily && desc.viewPolicy.family == 0))
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid visibility probe update", desc.scene);
        slot->snapshot.bounds = desc.bounds;
        slot->snapshot.viewPolicy = desc.viewPolicy;
        slot->snapshot.queryMask = desc.queryMask;
        ++slot->snapshot.descriptorRevision;
        CopyName(slot->snapshot.debugName, MaximumVisibilityProbeNameBytes, desc.debugName);
        slot->feedback = {};
        slot->feedback.probe = probe;
        slot->feedback.viewPolicy = slot->snapshot.viewPolicy;
        slot->feedback.viewSetRevision = m_impl->stats.viewSetRevision;
        return true;
    }

    bool VisibilityFeedbackService::DestroyProbe(VisibilityProbeHandle& probe,
                                                 RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!probe.IsValid()) return true;
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility probe destruction must run on the main thread");
        if (m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::Busy,
                        "visibility probes cannot change during end-frame publication");
        Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale visibility probe");
        slot->active = false;
        slot->pending = false;
        slot->snapshot = {};
        slot->feedback = {};
        --m_impl->stats.activeProbes;
        ++m_impl->stats.destroyedProbes;
        probe = {};
        return true;
    }

    bool VisibilityFeedbackService::ReadProbe(const VisibilityProbeHandle probe,
                                              VisibilityProbeSnapshot& snapshot,
                                              RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        snapshot = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized");
        const Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale visibility probe");
        snapshot = slot->snapshot;
        return true;
    }

    bool VisibilityFeedbackService::ReadFeedback(const VisibilityProbeHandle probe, const u64 currentFrame,
                                                 VisibilityFeedback& feedback,
                                                 RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        feedback = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized");
        const Impl::ProbeSlot* const slot = m_impl->ResolveProbe(probe);
        if (slot == nullptr)
            return Fail(failure, RenderSceneFailureCode::InvalidHandle,
                        "invalid or stale visibility probe");
        if (currentFrame < slot->feedback.evaluatedFrame)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "visibility feedback age cannot be evaluated before its source frame",
                        slot->snapshot.scene);
        feedback = slot->feedback;
        feedback.ageInFrames = feedback.evaluatedFrame != 0 ? currentFrame - feedback.evaluatedFrame : currentFrame;
        return true;
    }

    VisibilityFeedbackStats VisibilityFeedbackService::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : VisibilityFeedbackStats{};
    }

    bool VisibilityFeedbackService::BeginFrame(const u64 frameSerial,
                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility feedback publication must run on the main thread");
        if (m_impl->frameOpen || frameSerial == 0 || frameSerial <= m_impl->stats.publishedFrame)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "visibility feedback frames must be published once in increasing order");
        m_impl->frameSerial = frameSerial;
        m_impl->frameOpen = true;
        for (u32 index = 0; index < m_impl->probes.Size(); ++index)
        {
            Impl::ProbeSlot& slot = m_impl->probes[index];
            slot.pending = false;
            slot.pendingState = VisibilityFeedbackState::Unknown;
            slot.pendingVersion = {};
            slot.pendingFrame = 0;
            slot.pendingViewSetRevision = m_impl->stats.viewSetRevision;
            slot.pendingViews = 0;
        }
        return true;
    }

    bool VisibilityFeedbackService::Capture(
        const ViewCollectionRequest& request,
        containers::DynamicArray<VisibilityProbeEvaluationInput>& inputs,
        RenderSceneFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        inputs.Clear();
        if (m_impl == nullptr)
            return Fail(failure, RenderSceneFailureCode::NotInitialized,
                        "VisibilityFeedbackService is not initialized", request.visibility.lease.scene);
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "visibility probe capture must run on the main thread", request.visibility.lease.scene);
        const Impl::ViewSlot* const view = m_impl->FindView(request.view);
        if (view == nullptr || !(view->desc.scene == request.visibility.lease.scene)) return true;

        for (u32 index = 0; index < m_impl->probes.Size(); ++index)
        {
            const Impl::ProbeSlot& slot = m_impl->probes[index];
            if (!slot.active || !(slot.snapshot.scene == request.visibility.lease.scene) ||
                !Impl::PolicyMatches(slot.snapshot.viewPolicy, view->desc) ||
                (slot.snapshot.queryMask & request.visibility.visibilityMask) == 0)
                continue;

            VisibilityProbeEvaluationInput input;
            input.probe = slot.snapshot.handle;
            input.bounds = slot.snapshot.bounds;
            input.descriptorRevision = slot.snapshot.descriptorRevision;
            input.viewSetRevision = m_impl->stats.viewSetRevision;
            inputs.PushBack(input);
        }
        return true;
    }

    void VisibilityFeedbackService::Evaluate(
        const ViewCollectionRequest& request, const ViewCollectionResult& result,
        const containers::DynamicArray<VisibilityProbeEvaluationInput>& inputs,
        containers::DynamicArray<VisibilityProbeObservation>& observations) noexcept
    {
        observations.Clear();
        if (!result.completed || !result.succeeded) return;
        observations.Reserve(inputs.Size());
        for (u32 index = 0; index < inputs.Size(); ++index)
        {
            const VisibilityProbeEvaluationInput& input = inputs[index];
            VisibilityProbeObservation observation;
            observation.probe = input.probe;
            observation.state = (!request.visibility.useBounds ||
                                 BoundsOverlap(input.bounds, request.visibility.bounds)) &&
                                (!request.visibility.useFrustum ||
                                 BoundsIntersectsFrustum(input.bounds, request.visibility.frustum)) ?
                                    VisibilityFeedbackState::Visible :
                                    VisibilityFeedbackState::NotVisible;
            observation.sceneVersion = result.version;
            observation.descriptorRevision = input.descriptorRevision;
            observation.viewSetRevision = input.viewSetRevision;
            observation.frameSerial = result.frameSerial;
            observations.PushBack(observation);
        }
    }

    bool VisibilityFeedbackService::Accumulate(
        const containers::DynamicArray<VisibilityProbeObservation>& observations,
        const ViewCollectionResult& result, RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "visibility feedback accumulation requires an open end frame", result.scene);
        if (!result.completed || result.frameSerial > m_impl->frameSerial)
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "invalid completed collection for visibility feedback", result.scene);

        for (u32 index = 0; index < observations.Size(); ++index)
        {
            const VisibilityProbeObservation& observation = observations[index];
            Impl::ProbeSlot* const slot = m_impl->ResolveProbe(observation.probe);
            if (slot == nullptr || slot->snapshot.descriptorRevision != observation.descriptorRevision ||
                observation.viewSetRevision != m_impl->stats.viewSetRevision)
                continue;

            const bool newer = !slot->pending || observation.frameSerial > slot->pendingFrame ||
                               (observation.frameSerial == slot->pendingFrame &&
                                observation.sceneVersion.value > slot->pendingVersion.value);
            if (newer)
            {
                slot->pending = true;
                slot->pendingState = observation.state;
                slot->pendingVersion = observation.sceneVersion;
                slot->pendingFrame = observation.frameSerial;
                slot->pendingViewSetRevision = observation.viewSetRevision;
                slot->pendingViews = 1;
            }
            else if (observation.frameSerial == slot->pendingFrame &&
                     observation.sceneVersion == slot->pendingVersion)
            {
                if (observation.state == VisibilityFeedbackState::Visible)
                    slot->pendingState = VisibilityFeedbackState::Visible;
                ++slot->pendingViews;
            }
        }
        return true;
    }

    bool VisibilityFeedbackService::Publish(RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->frameOpen)
            return Fail(failure, RenderSceneFailureCode::InvalidState,
                        "visibility feedback publish requires an open end frame");
        for (u32 index = 0; index < m_impl->probes.Size(); ++index)
        {
            Impl::ProbeSlot& slot = m_impl->probes[index];
            if (!slot.active) continue;
            if (slot.pending)
            {
                slot.feedback.probe = slot.snapshot.handle;
                slot.feedback.state = slot.pendingState;
                slot.feedback.sceneVersion = slot.pendingVersion;
                slot.feedback.viewPolicy = slot.snapshot.viewPolicy;
                slot.feedback.viewSetRevision = slot.pendingViewSetRevision;
                slot.feedback.evaluatedFrame = slot.pendingFrame;
                slot.feedback.ageInFrames = m_impl->frameSerial - slot.pendingFrame;
                slot.feedback.contributingViews = slot.pendingViews;
                ++m_impl->stats.evaluatedProbes;
            }
            else
            {
                slot.feedback.state = VisibilityFeedbackState::Unknown;
                slot.feedback.viewPolicy = slot.snapshot.viewPolicy;
                slot.feedback.viewSetRevision = m_impl->stats.viewSetRevision;
                slot.feedback.ageInFrames = slot.feedback.evaluatedFrame != 0 ?
                                                m_impl->frameSerial - slot.feedback.evaluatedFrame :
                                                m_impl->frameSerial;
                slot.feedback.contributingViews = 0;
            }
        }
        m_impl->stats.publishedFrame = m_impl->frameSerial;
        m_impl->frameOpen = false;
        return true;
    }

    bool RenderSceneFrameLifecycle::Initialize(RenderSceneManager& scenes, RenderSceneCollector& collector,
                                               VisibilityFeedbackService& feedback,
                                               RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (IsInitialized())
            return Fail(failure, RenderSceneFailureCode::AlreadyInitialized,
                        "RenderSceneFrameLifecycle is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderSceneFailureCode::WrongThread,
                        "RenderSceneFrameLifecycle must initialize on the main thread");
        if (!scenes.IsInitialized() || !collector.IsInitialized() || !feedback.IsInitialized())
            return Fail(failure, RenderSceneFailureCode::InvalidDescriptor,
                        "RenderSceneFrameLifecycle requires initialized scene services");
        m_scenes = &scenes;
        m_collector = &collector;
        m_feedback = &feedback;
        m_lastEndedFrame = 0;
        m_collector->AttachFeedback(m_feedback);
        return true;
    }

    void RenderSceneFrameLifecycle::Shutdown() noexcept
    {
        if (m_collector != nullptr) m_collector->AttachFeedback(nullptr);
        m_scenes = nullptr;
        m_collector = nullptr;
        m_feedback = nullptr;
        m_lastEndedFrame = 0;
    }

    bool RenderSceneFrameLifecycle::IsInitialized() const noexcept
    {
        return m_scenes != nullptr && m_collector != nullptr && m_feedback != nullptr;
    }

    RenderSceneEndFrameStatus RenderSceneFrameLifecycle::EndFrame(const u64 frameSerial,
                                                                 RenderSceneEndFrameResult& result,
                                                                 RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        result.frameSerial = frameSerial;
        if (!IsInitialized())
        {
            static_cast<void>(Fail(failure, RenderSceneFailureCode::NotInitialized,
                                   "RenderSceneFrameLifecycle is not initialized"));
            return RenderSceneEndFrameStatus::Failure;
        }
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, RenderSceneFailureCode::WrongThread,
                                   "RenderScene end frame must run on the main thread"));
            return RenderSceneEndFrameStatus::Failure;
        }
        if (frameSerial == 0 || frameSerial <= m_lastEndedFrame)
        {
            static_cast<void>(Fail(failure, RenderSceneFailureCode::InvalidState,
                                   "RenderScene end frames must complete once in increasing order"));
            return RenderSceneEndFrameStatus::Failure;
        }

        u32 eligibleCollections = 0;
        if (!m_collector->InspectRetirement(frameSerial, eligibleCollections, result.pendingCollections,
                                            failure))
            return RenderSceneEndFrameStatus::Failure;
        if (result.pendingCollections != 0) return RenderSceneEndFrameStatus::Pending;

        if (!m_feedback->BeginFrame(frameSerial, failure)) return RenderSceneEndFrameStatus::Failure;
        if (!m_collector->RetireThroughFrame(frameSerial, *m_feedback, result.retiredCollections, failure))
            return RenderSceneEndFrameStatus::Failure;
        if (!m_feedback->Publish(failure)) return RenderSceneEndFrameStatus::Failure;
        result.feedbackPublished = true;

        RenderSceneVersionRetirementResult retirement;
        if (!m_scenes->RetirePublishedVersions(retirement, failure))
            return RenderSceneEndFrameStatus::Failure;
        result.reclaimedVersions = retirement.reclaimedVersions;
        result.retainedVersions = retirement.retainedVersions;
        result.versionsBlockedByReaders = retirement.versionsBlockedByReaders;
        result.liveReadLeases = retirement.liveReadLeases;
        result.fullyRetired = result.versionsBlockedByReaders == 0;
        m_lastEndedFrame = frameSerial;
        return RenderSceneEndFrameStatus::Complete;
    }
} // namespace vanguard::rendering
