#include <vanguard/rendering/texture_residency.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <cstring>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(TextureResidencyFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TextureResidencyFailure* const failure, const TextureResidencyFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] constexpr rhi::DescriptorRetirement ToDescriptorRetirement(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return {fences.graphics, fences.compute, fences.copy};
        }

        [[nodiscard]] constexpr bool HasCompleteCutover(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return fences.Covers(rhi::QueueType::Graphics) && fences.Covers(rhi::QueueType::Compute) && fences.Covers(rhi::QueueType::Copy);
        }

        [[nodiscard]] constexpr rhi::ResidencyFenceSet MergeCutover(rhi::ResidencyFenceSet left, const rhi::ResidencyFenceSet& right) noexcept
        {
            if (left.graphics < right.graphics)
                left.graphics = right.graphics;
            if (left.compute < right.compute)
                left.compute = right.compute;
            if (left.copy < right.copy)
                left.copy = right.copy;
            return left;
        }
    } // namespace

    struct TextureResidencyManager::Impl
    {
        struct Installation
        {
            rhi::Texture texture;
            rhi::DescriptorHandle descriptor;
            resources::WeakResourceHandle source;
            crypto::Digest256 contentFingerprint;
            u32 firstResidentMip = 0;
            u32 residentMipCount = 0;
            u32 revision = 0;
            rhi::GpuFence activation;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return texture.IsValid() && descriptor.IsValid() && residentMipCount != 0;
            }
        };

        struct Slot
        {
            struct ActiveTransition
            {
                rhi::Texture sourceTexture;
                resources::WeakResourceHandle source;
                crypto::Digest256 contentFingerprint;
                u32 serial = 0;
                u32 expectedInstallationRevision = 0;
                u32 currentFirstResidentMip = 0;
                u32 currentResidentMipCount = 0;
                u32 targetFirstResidentMip = 0;

                [[nodiscard]] bool IsValid() const noexcept
                {
                    return serial != 0;
                }
                void Reset() noexcept
                {
                    *this = {};
                }
            };

            GpuSceneAllocation allocation;
            Installation current;
            Installation pending;
            ActiveTransition transition;
            u32 installationRevision = 0;
            u32 pendingPrevious = InvalidGpuSceneIndex;
            u32 pendingNext = InvalidGpuSceneIndex;
            TextureResidencyState state = TextureResidencyState::Invalid;
            bool pendingQueued = false;
            bool pendingFrozen = false;
            bool retirementSealed = false;
            bool active = false;
        };

        struct Retirement
        {
            Installation installation;
            rhi::ResidencyFenceSet floor;
        };

        explicit Impl(const TextureResidencyConfig& value) noexcept
            : slots(memory::pools::Rendering::GetInstance()), recycledSlots(memory::pools::Rendering::GetInstance()), slotByGpuIndex(memory::pools::Rendering::GetInstance()),
              frozenSlots(memory::pools::Rendering::GetInstance()), pendingRetirements(memory::pools::Rendering::GetInstance()), retiringSlots(memory::pools::Rendering::GetInstance()), config(value)
        {
            slots.Reserve(config.maximumTextures);
            recycledSlots.Reserve(config.maximumTextures);
            slotByGpuIndex.Reserve(config.maximumTextures);
            frozenSlots.Reserve(config.maximumPendingInstallations);
            pendingRetirements.Reserve(config.maximumPendingRetirements);
            retiringSlots.Reserve(config.maximumTextures);
        }

        [[nodiscard]] Slot* Find(const GpuTextureResidencyHandle handle) noexcept
        {
            u32 slotIndex = InvalidGpuSceneIndex;
            if (!handle.IsValid() || !slotByGpuIndex.Find(handle.index, slotIndex) || slotIndex >= slots.Size())
                return nullptr;
            Slot& slot = slots[slotIndex];
            return slot.active && slot.allocation.first == handle.index && slot.allocation.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const Slot* Find(const GpuTextureResidencyHandle handle) const noexcept
        {
            u32 slotIndex = InvalidGpuSceneIndex;
            if (!handle.IsValid() || !slotByGpuIndex.Find(handle.index, slotIndex) || slotIndex >= slots.Size())
                return nullptr;
            const Slot& slot = slots[slotIndex];
            return slot.active && slot.allocation.first == handle.index && slot.allocation.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] u32 AllocateSlot() noexcept
        {
            if (!recycledSlots.Empty())
            {
                const u32 slot = recycledSlots.Back();
                recycledSlots.PopBack();
                return slot;
            }
            slots.PushBack({});
            return slots.Size() - 1u;
        }

        void EnqueuePendingBack(const u32 slotIndex) noexcept
        {
            Slot& slot = slots[slotIndex];
            VG_ASSERT_MSG(!slot.pendingQueued && !slot.pendingFrozen, "a texture installation can enter the pending queue only once");
            slot.pendingPrevious = pendingTail;
            slot.pendingNext = InvalidGpuSceneIndex;
            slot.pendingQueued = true;
            if (pendingTail != InvalidGpuSceneIndex)
                slots[pendingTail].pendingNext = slotIndex;
            else
                pendingHead = slotIndex;
            pendingTail = slotIndex;
            ++pendingQueueCount;
        }

        void EnqueuePendingFront(const u32 slotIndex) noexcept
        {
            Slot& slot = slots[slotIndex];
            VG_ASSERT_MSG(!slot.pendingQueued && !slot.pendingFrozen, "a texture installation can enter the pending queue only once");
            slot.pendingPrevious = InvalidGpuSceneIndex;
            slot.pendingNext = pendingHead;
            slot.pendingQueued = true;
            if (pendingHead != InvalidGpuSceneIndex)
                slots[pendingHead].pendingPrevious = slotIndex;
            else
                pendingTail = slotIndex;
            pendingHead = slotIndex;
            ++pendingQueueCount;
        }

        void RemovePendingSlot(const u32 slotIndex) noexcept
        {
            Slot& slot = slots[slotIndex];
            VG_ASSERT_MSG(slot.pendingQueued && !slot.pendingFrozen, "a non-frozen pending texture installation must have one queue entry");
            if (slot.pendingPrevious != InvalidGpuSceneIndex)
                slots[slot.pendingPrevious].pendingNext = slot.pendingNext;
            else
                pendingHead = slot.pendingNext;
            if (slot.pendingNext != InvalidGpuSceneIndex)
                slots[slot.pendingNext].pendingPrevious = slot.pendingPrevious;
            else
                pendingTail = slot.pendingPrevious;
            slot.pendingPrevious = InvalidGpuSceneIndex;
            slot.pendingNext = InvalidGpuSceneIndex;
            slot.pendingQueued = false;
            --pendingQueueCount;
        }

        [[nodiscard]] u32 PopPendingFront() noexcept
        {
            VG_ASSERT_MSG(pendingHead != InvalidGpuSceneIndex, "the pending texture queue must not be empty");
            const u32 result = pendingHead;
            RemovePendingSlot(result);
            return result;
        }

        containers::DynamicArray<Slot> slots;
        containers::DynamicArray<u32> recycledSlots;
        containers::HashMap<u32, u32> slotByGpuIndex;
        containers::DynamicArray<u32> frozenSlots;
        containers::DynamicArray<Retirement> pendingRetirements;
        containers::DynamicArray<u32> retiringSlots;
        TextureResidencyConfig config;
        TextureResidencyStats stats;
        u32 pendingInstallationCount = 0;
        u32 pendingReplacementCount = 0;
        u32 pendingHead = InvalidGpuSceneIndex;
        u32 pendingTail = InvalidGpuSceneIndex;
        u32 pendingQueueCount = 0;
        u32 nextBatchSerial = 1;
        u32 nextTransitionSerial = 1;
        u32 frozenBatchSerial = 0;
        bool frozenBatchWritten = false;

        void ResetFrozenBatch() noexcept
        {
            frozenSlots.Clear();
            frozenBatchSerial = 0;
            frozenBatchWritten = false;
        }
    };

    namespace
    {
        void RetireUnusedDescriptor(const rhi::DescriptorDomainRef domain, rhi::DescriptorHandle& descriptor) noexcept
        {
            if (!descriptor.IsValid())
                return;
            static_cast<void>(rhi::RetireDescriptor(domain, descriptor, {}));
            descriptor = {};
        }

        void ReleaseUnusedInstallation(const rhi::DescriptorDomainRef domain, TextureResidencyManager::Impl::Installation& installation) noexcept
        {
            RetireUnusedDescriptor(domain, installation.descriptor);
            installation.texture.Reset();
            installation = {};
        }
    } // namespace

    TextureResidencyManager::~TextureResidencyManager()
    {
        VG_ASSERT_MSG(m_impl == nullptr && m_lifetime == nullptr && !m_resourceDescriptors.IsValid(), "texture residency manager must be shut down before destruction");
    }

    bool TextureResidencyManager::Initialize(GpuSceneLifetime& lifetime, const rhi::DescriptorDomainRef resourceDescriptors, const TextureResidencyConfig& config,
                                             TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (IsInitialized() || m_impl != nullptr || m_resourceDescriptors.IsValid())
            return Fail(failure, TextureResidencyFailureCode::AlreadyInitialized, "texture residency manager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture residency manager must initialize on the main thread");
        if (!lifetime.IsInitialized() || !resourceDescriptors.IsValid())
            return Fail(failure, TextureResidencyFailureCode::InvalidDependency, "texture residency manager requires GPU Scene lifetime and resource descriptors");
        if (config.maximumTextures == 0 || config.maximumPendingInstallations == 0 || config.maximumPendingRetirements == 0)
            return Fail(failure, TextureResidencyFailureCode::InvalidConfiguration, "texture residency configuration is invalid");

        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyFailureCode::CapacityExceeded, "texture residency storage allocation failed");
        m_lifetime = &lifetime;
        m_resourceDescriptors.Reset(resourceDescriptors);
        return true;
    }

    bool TextureResidencyManager::Shutdown(TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized() && m_impl == nullptr && !m_resourceDescriptors.IsValid())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture residency manager must shut down on the main thread");
        if (m_impl != nullptr && (m_impl->stats.liveResidencies != 0 || m_impl->pendingInstallationCount != 0 || m_impl->frozenBatchSerial != 0 || !m_impl->pendingRetirements.Empty()))
            return Fail(failure, TextureResidencyFailureCode::LiveResidenciesRemain, "texture residency manager still owns live identities or descriptor retirements");
        DeleteObject(m_impl);
        m_impl = nullptr;
        m_lifetime = nullptr;
        m_resourceDescriptors.Reset();
        return true;
    }

    void TextureResidencyManager::AbandonDevice() noexcept
    {
        DeleteObject(m_impl);
        m_impl = nullptr;
        m_lifetime = nullptr;
        m_resourceDescriptors.Reset();
    }

    bool TextureResidencyManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_lifetime != nullptr && m_resourceDescriptors.IsValid();
    }

    bool TextureResidencyManager::Allocate(GpuTextureResidencyHandle& handle, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        handle = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture residency allocation must run on the main thread");
        if (m_impl->stats.liveResidencies >= m_impl->config.maximumTextures)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::CapacityExceeded, "texture residency capacity is exhausted");
        }

        GpuSceneAllocation allocation;
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_lifetime->Allocate<GpuTextureResidency>(1, allocation, &lifetimeFailure))
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyFailureCode::LifetimeFailure;
                failure->message = "texture residency GPU Scene allocation failed";
                failure->lifetimeFailure = lifetimeFailure;
            }
            ++m_impl->stats.rejectedOperations;
            return false;
        }

        const u32 slotIndex = m_impl->AllocateSlot();
        Impl::Slot& slot = m_impl->slots[slotIndex];
        slot = {};
        slot.allocation = allocation;
        slot.state = TextureResidencyState::Allocated;
        slot.active = true;
        if (!m_impl->slotByGpuIndex.Insert(allocation.first, slotIndex).IsSuccessful())
        {
            slot = {};
            m_impl->recycledSlots.PushBack(slotIndex);
            static_cast<void>(m_lifetime->Cancel(allocation));
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::CapacityExceeded, "texture residency identity lookup capacity is exhausted");
        }

        handle = allocation.AsSlotHandle<GpuTextureResidencyHandle>();
        ++m_impl->stats.liveResidencies;
        ++m_impl->stats.allocations;
        return true;
    }

    bool TextureResidencyManager::Install(const GpuTextureResidencyHandle handle, const TextureInstallationDesc& installation, TextureInstallationTicket& ticket, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        ticket = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture installation must run on the main thread");
        Impl::Slot* const slot = m_impl->Find(handle);
        if (slot == nullptr || slot->state == TextureResidencyState::Retiring)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture installation references a stale or retiring identity");
        }
        if (slot->pending.IsValid())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::Busy, "texture already has a pending installation");
        }
        const bool hasTransition = slot->transition.IsValid();
        if (hasTransition != installation.transition.IsValid())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, hasTransition ? TextureResidencyFailureCode::Busy : TextureResidencyFailureCode::StaleHandle,
                        hasTransition ? "manager-owned texture transition requires its retained token" : "texture installation references a stale transition token");
        }
        if (hasTransition)
        {
            const Impl::Slot::ActiveTransition& transition = slot->transition;
            const bool sameSource = transition.source.IsValid() && installation.source.IsValid() && transition.source.GetPath().Id() == installation.source.GetPath().Id() &&
                                    transition.source.GetGeneration() == installation.source.GetGeneration();
            const bool currentMatches = transition.expectedInstallationRevision == slot->installationRevision && transition.currentFirstResidentMip == slot->current.firstResidentMip &&
                                        transition.currentResidentMipCount == slot->current.residentMipCount &&
                                        ((!slot->current.IsValid() && !transition.sourceTexture.IsValid()) || (slot->current.IsValid() && transition.sourceTexture.GetRef() == slot->current.texture.GetRef()));
            if (installation.transition.texture != handle || installation.transition.serial != transition.serial || transition.targetFirstResidentMip != installation.firstResidentMip || !sameSource ||
                !(transition.contentFingerprint == installation.contentFingerprint) || !currentMatches)
            {
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture transition source, content, target mip, or installation revision changed before admission");
            }
        }
        const bool hasSource = installation.source.IsValid();
        const bool hasContentFingerprint = !installation.contentFingerprint.IsEmpty();
        if (!installation.texture.IsValid() || installation.residentMipCount == 0 || installation.firstResidentMip > 0xffffffffu - installation.residentMipCount || hasSource != hasContentFingerprint ||
            (hasSource && installation.source.IsStale()))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::InvalidArgument, "texture installation is invalid");
        }
        if (m_impl->pendingInstallationCount >= m_impl->config.maximumPendingInstallations)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::CapacityExceeded, "texture installation admission capacity is exhausted");
        }
        if (slot->current.IsValid() && m_impl->pendingRetirements.Size() + m_impl->pendingReplacementCount >= m_impl->config.maximumPendingRetirements)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::PendingRetirementCapacityExceeded, "texture replacement retirement capacity is exhausted");
        }

        rhi::Failure rhiFailure;
        rhi::DescriptorHandle descriptor = rhi::AllocateDescriptor(m_resourceDescriptors.GetRef(), &rhiFailure);
        if (!descriptor.IsValid() || !rhi::WriteDescriptor(m_resourceDescriptors.GetRef(), descriptor, installation.texture, rhi::BindingType::TextureShaderResource, {}, &rhiFailure))
        {
            RetireUnusedDescriptor(m_resourceDescriptors.GetRef(), descriptor);
            if (failure != nullptr)
            {
                failure->code = TextureResidencyFailureCode::DescriptorFailure;
                failure->message = "immutable texture descriptor installation failed";
                failure->rhiFailure = rhiFailure;
            }
            ++m_impl->stats.rejectedOperations;
            return false;
        }

        const u32 revision = slot->installationRevision + 1u != 0 ? slot->installationRevision + 1u : 1u;
        slot->pending.texture.Reset(installation.texture);
        slot->pending.descriptor = descriptor;
        slot->pending.source = installation.source;
        slot->pending.contentFingerprint = installation.contentFingerprint;
        slot->pending.firstResidentMip = installation.firstResidentMip;
        slot->pending.residentMipCount = installation.residentMipCount;
        slot->pending.revision = revision;
        m_impl->EnqueuePendingBack(static_cast<u32>(slot - m_impl->slots.TypedData()));
        ++m_impl->pendingInstallationCount;
        if (slot->current.IsValid())
            ++m_impl->pendingReplacementCount;
        ticket = {handle, revision};
        return true;
    }

    bool TextureResidencyManager::BeginTransition(const GpuTextureResidencyHandle handle, const resources::WeakResourceHandle& source, const crypto::Digest256& contentFingerprint,
                                                  const u32 targetFirstResidentMip, TextureTransitionToken& token, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        token = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture transition must begin on the main thread");
        Impl::Slot* const slot = m_impl->Find(handle);
        if (slot == nullptr || slot->state == TextureResidencyState::Retiring)
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture transition references a stale or retiring identity");
        if (slot->transition.IsValid() || slot->pending.IsValid())
            return Fail(failure, TextureResidencyFailureCode::Busy, "texture identity already has an active transition or pending installation");
        if (!source.IsValid() || source.IsStale() || contentFingerprint.IsEmpty())
            return Fail(failure, TextureResidencyFailureCode::InvalidArgument, "texture transition requires a live source generation and content identity");
        if (slot->current.IsValid())
        {
            const bool sameSource = slot->current.source.IsValid() && !slot->current.source.IsStale() && slot->current.source.GetPath().Id() == source.GetPath().Id() &&
                                    slot->current.source.GetGeneration() == source.GetGeneration();
            if (!sameSource || !(slot->current.contentFingerprint == contentFingerprint))
                return Fail(failure, TextureResidencyFailureCode::InvalidArgument, "texture transition cannot reuse shared mips from different source content");
        }

        u32 serial = m_impl->nextTransitionSerial++;
        if (serial == 0)
            serial = m_impl->nextTransitionSerial++;
        Impl::Slot::ActiveTransition& transition = slot->transition;
        if (slot->current.IsValid())
            transition.sourceTexture.Reset(slot->current.texture.GetRef());
        transition.source = source;
        transition.contentFingerprint = contentFingerprint;
        transition.serial = serial;
        transition.expectedInstallationRevision = slot->installationRevision;
        transition.currentFirstResidentMip = slot->current.firstResidentMip;
        transition.currentResidentMipCount = slot->current.residentMipCount;
        transition.targetFirstResidentMip = targetFirstResidentMip;
        token = {handle, serial};
        return true;
    }

    bool TextureResidencyManager::GetTransitionInfo(const TextureTransitionToken token, TextureTransitionInfo& info, TextureResidencyFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        const Impl::Slot* const slot = m_impl->Find(token.texture);
        if (slot == nullptr || !token.IsValid() || !slot->transition.IsValid() || slot->transition.serial != token.serial)
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture transition token is stale");
        const Impl::Slot::ActiveTransition& transition = slot->transition;
        info.token = token;
        info.currentTexture = transition.sourceTexture.GetRef();
        info.source = transition.source;
        info.contentFingerprint = transition.contentFingerprint;
        info.expectedInstallationRevision = transition.expectedInstallationRevision;
        info.currentFirstResidentMip = transition.currentFirstResidentMip;
        info.currentResidentMipCount = transition.currentResidentMipCount;
        info.targetFirstResidentMip = transition.targetFirstResidentMip;
        return true;
    }

    bool TextureResidencyManager::EndTransition(const TextureTransitionToken token, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture transition must end on the main thread");
        Impl::Slot* const slot = m_impl->Find(token.texture);
        if (slot == nullptr || !token.IsValid() || !slot->transition.IsValid() || slot->transition.serial != token.serial)
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture transition token is stale");
        if (slot->pending.IsValid())
            return Fail(failure, TextureResidencyFailureCode::Busy, "an admitted texture transition remains manager-owned until its shared GPU Scene batch is accepted or discarded");
        slot->transition.Reset();
        return true;
    }

    bool TextureResidencyManager::PrepareBatch(const u32 maximumInstallations, TextureResidencyBatch& batch, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        batch = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture installation batch preparation must run on the main thread");
        if (maximumInstallations == 0)
            return Fail(failure, TextureResidencyFailureCode::InvalidArgument, "texture installation batch contribution capacity must be non-zero");
        if (m_impl->frozenBatchSerial != 0)
            return Fail(failure, TextureResidencyFailureCode::Busy, "a texture installation batch is already frozen");

        const u32 frozenCount = m_impl->pendingQueueCount < maximumInstallations ? m_impl->pendingQueueCount : maximumInstallations;
        m_impl->frozenSlots.Reserve(frozenCount);
        for (u32 index = 0; index < frozenCount; ++index)
        {
            const u32 slotIndex = m_impl->PopPendingFront();
            Impl::Slot& slot = m_impl->slots[slotIndex];
            VG_ASSERT_MSG(slot.active && slot.pending.IsValid(), "the texture pending queue must contain live installations");
            VG_ASSERT_MSG(!slot.pendingFrozen, "an unfrozen texture batch cannot contain frozen pending state");
            slot.pendingFrozen = true;
            m_impl->frozenSlots.PushBack(slotIndex);
        }
        u32 serial = m_impl->nextBatchSerial++;
        if (serial == 0)
        {
            serial = m_impl->nextBatchSerial++;
            VG_ASSERT_MSG(serial != 0, "texture installation batch serial must remain non-zero");
        }
        m_impl->frozenBatchSerial = serial;
        m_impl->frozenBatchWritten = m_impl->frozenSlots.Empty();
        batch = {serial, m_impl->frozenSlots.Size()};
        return true;
    }

    bool TextureResidencyManager::BuildUploadRequests(const TextureResidencyBatch& batch, const containers::ArraySpan<GpuSceneUploadRequest> requests, TextureResidencyFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!batch.IsValid() || batch.serial != m_impl->frozenBatchSerial || batch.installationCount != m_impl->frozenSlots.Size())
            return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation batch is stale or invalid");
        if (requests.Size() != batch.installationCount || (requests.Size() != 0 && requests.Data() == nullptr))
            return Fail(failure, TextureResidencyFailureCode::ReservationMismatch, "texture installation upload-request span does not match the frozen batch");
        for (u32 index = 0; index < m_impl->frozenSlots.Size(); ++index)
        {
            const Impl::Slot& slot = m_impl->slots[m_impl->frozenSlots[index]];
            if (!slot.active || !slot.pending.IsValid() || !slot.pendingFrozen)
                return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation changed after its batch was frozen");
            requests[index] = {slot.allocation, 0, 1};
        }
        return true;
    }

    bool TextureResidencyManager::WriteBatch(const TextureResidencyBatch& batch, const containers::ArraySpan<const GpuSceneUploadReservation> reservations, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!batch.IsValid() || batch.serial != m_impl->frozenBatchSerial || batch.installationCount != m_impl->frozenSlots.Size())
            return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation batch is stale or invalid");
        if (m_impl->frozenBatchWritten)
            return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation batch was already written");
        if (reservations.Size() != batch.installationCount || (reservations.Size() != 0 && reservations.Data() == nullptr))
            return Fail(failure, TextureResidencyFailureCode::ReservationMismatch, "texture installation reservation span does not match the frozen batch");

        for (u32 index = 0; index < m_impl->frozenSlots.Size(); ++index)
        {
            const Impl::Slot& slot = m_impl->slots[m_impl->frozenSlots[index]];
            const GpuSceneUploadReservation& reservation = reservations[index];
            if (!slot.active || !slot.pending.IsValid() || !slot.pendingFrozen || !reservation.IsValid() || reservation.size != sizeof(GpuTextureResidency))
                return Fail(failure, TextureResidencyFailureCode::ReservationMismatch, "texture installation contains an invalid GPU Scene reservation");
            GpuTextureResidency value;
            value.descriptor = slot.pending.descriptor.GpuIndex();
            value.firstResidentMip = slot.pending.firstResidentMip;
            value.residentMipCount = slot.pending.residentMipCount;
            value.generationAndFlags = PackGpuTextureResidencyGenerationAndFlags(slot.allocation.generation, GpuTextureResidencyFlags::BindlessReady);
            std::memcpy(reservation.destination, &value, sizeof(value));
        }
        m_impl->frozenBatchWritten = true;
        return true;
    }

    void TextureResidencyManager::AcceptSubmittedBatch(const TextureResidencyBatch& batch, const rhi::GpuFence sharedCompletion) noexcept
    {
        const bool valid = IsInitialized() && concurrency::IsMainThread() && batch.IsValid() && batch.serial == m_impl->frozenBatchSerial && batch.installationCount == m_impl->frozenSlots.Size() &&
                           m_impl->frozenBatchWritten && (batch.installationCount == 0 || sharedCompletion.IsValid());
        VG_ASSERT_MSG(valid, "submitted texture installation batch must have been validated and written before submission");
        if (!valid)
            return;

        for (const u32 slotIndex : m_impl->frozenSlots)
        {
            Impl::Slot& slot = m_impl->slots[slotIndex];
            VG_ASSERT_MSG(slot.active && slot.pending.IsValid() && slot.pendingFrozen, "a committed texture installation must still own its frozen candidate");
            if (slot.current.IsValid())
            {
                Impl::Retirement retirement;
                retirement.installation = std::move(slot.current);
                retirement.floor.Include(retirement.installation.activation);
                retirement.floor.Include(sharedCompletion);
                m_impl->pendingRetirements.PushBack(std::move(retirement));
                --m_impl->pendingReplacementCount;
                ++m_impl->stats.replacements;
            }
            slot.current = std::move(slot.pending);
            slot.pending = {};
            slot.current.activation = sharedCompletion;
            slot.installationRevision = slot.current.revision;
            slot.transition.Reset();
            slot.pendingFrozen = false;
            --m_impl->pendingInstallationCount;
            if (slot.state != TextureResidencyState::BindlessReady)
                ++m_impl->stats.bindlessReady;
            slot.state = TextureResidencyState::BindlessReady;
            ++m_impl->stats.installations;
        }
        if (batch.installationCount != 0)
            ++m_impl->stats.installationBatches;
        m_impl->ResetFrozenBatch();
    }

    bool TextureResidencyManager::RetryBatch(const TextureResidencyBatch& batch, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture installation batch retry must run on the main thread");
        if (!batch.IsValid() || batch.serial != m_impl->frozenBatchSerial || batch.installationCount != m_impl->frozenSlots.Size())
            return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation batch is stale or invalid");
        for (const u32 slotIndex : m_impl->frozenSlots)
        {
            const Impl::Slot& slot = m_impl->slots[slotIndex];
            if (!slot.active || !slot.pending.IsValid() || !slot.pendingFrozen)
                return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation changed before its batch was retried");
        }
        const u32 retryCount = m_impl->frozenSlots.Size();
        for (u32 index = retryCount; index > 0; --index)
        {
            const u32 slotIndex = m_impl->frozenSlots[index - 1u];
            Impl::Slot& slot = m_impl->slots[slotIndex];
            slot.pendingFrozen = false;
            m_impl->EnqueuePendingFront(slotIndex);
        }
        m_impl->ResetFrozenBatch();
        return true;
    }

    bool TextureResidencyManager::DiscardBatch(const TextureResidencyBatch& batch, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture installation batch discard must run on the main thread");
        if (!batch.IsValid() || batch.serial != m_impl->frozenBatchSerial || batch.installationCount != m_impl->frozenSlots.Size())
            return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation batch is stale or invalid");
        for (const u32 slotIndex : m_impl->frozenSlots)
        {
            Impl::Slot& slot = m_impl->slots[slotIndex];
            if (!slot.active || !slot.pending.IsValid() || !slot.pendingFrozen)
                return Fail(failure, TextureResidencyFailureCode::InvalidBatch, "texture installation changed before its batch was discarded");
            if (slot.current.IsValid())
                --m_impl->pendingReplacementCount;
            ReleaseUnusedInstallation(m_resourceDescriptors.GetRef(), slot.pending);
            slot.transition.Reset();
            slot.pendingFrozen = false;
            --m_impl->pendingInstallationCount;
        }
        m_impl->ResetFrozenBatch();
        return true;
    }

    bool TextureResidencyManager::Retire(const GpuTextureResidencyHandle handle, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture residency retirement must run on the main thread");
        Impl::Slot* const slot = m_impl->Find(handle);
        if (slot == nullptr || slot->state == TextureResidencyState::Retiring)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture retirement references a stale identity");
        }
        if (slot->pendingFrozen)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::Busy, "texture retirement cannot mutate a frozen installation batch");
        }
        if (slot->transition.IsValid())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::Busy, "texture retirement cannot invalidate an active mip transition");
        }
        const u32 releasedReplacementReservation = slot->current.IsValid() && slot->pending.IsValid() ? 1u : 0u;
        if (slot->current.IsValid() && m_impl->pendingRetirements.Size() + m_impl->pendingReplacementCount - releasedReplacementReservation >= m_impl->config.maximumPendingRetirements)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::PendingRetirementCapacityExceeded, "texture retirement capacity is exhausted");
        }
        if (slot->pending.IsValid())
        {
            if (slot->current.IsValid())
                --m_impl->pendingReplacementCount;
            m_impl->RemovePendingSlot(static_cast<u32>(slot - m_impl->slots.TypedData()));
            ReleaseUnusedInstallation(m_resourceDescriptors.GetRef(), slot->pending);
            --m_impl->pendingInstallationCount;
        }

        GpuSceneLifetimeFailure lifetimeFailure;
        if (!slot->current.IsValid())
        {
            if (!m_lifetime->Cancel(slot->allocation, &lifetimeFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = TextureResidencyFailureCode::LifetimeFailure;
                    failure->message = "uninstalled texture residency cancellation failed";
                    failure->lifetimeFailure = lifetimeFailure;
                }
                ++m_impl->stats.rejectedOperations;
                return false;
            }
            const u32 slotIndex = static_cast<u32>(slot - m_impl->slots.TypedData());
            static_cast<void>(m_impl->slotByGpuIndex.Remove(slot->allocation.first));
            *slot = {};
            m_impl->recycledSlots.PushBack(slotIndex);
            --m_impl->stats.liveResidencies;
            ++m_impl->stats.retirements;
            ++m_impl->stats.reclaimed;
            return true;
        }
        if (!m_lifetime->Retire(slot->allocation, &lifetimeFailure))
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyFailureCode::LifetimeFailure;
                failure->message = "texture residency GPU Scene retirement failed";
                failure->lifetimeFailure = lifetimeFailure;
            }
            ++m_impl->stats.rejectedOperations;
            return false;
        }
        if (slot->current.IsValid())
        {
            Impl::Retirement retirement;
            retirement.installation = std::move(slot->current);
            retirement.floor.Include(retirement.installation.activation);
            m_impl->pendingRetirements.PushBack(std::move(retirement));
            --m_impl->stats.bindlessReady;
        }
        slot->current = {};
        slot->state = TextureResidencyState::Retiring;
        slot->retirementSealed = false;
        m_impl->retiringSlots.PushBack(static_cast<u32>(slot - m_impl->slots.TypedData()));
        ++m_impl->stats.retirements;
        return true;
    }

    bool TextureResidencyManager::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyFailureCode::WrongThread, "texture retirement sealing must run on the main thread");
        bool hasUnsealedIdentity = false;
        for (const u32 slotIndex : m_impl->retiringSlots)
        {
            const Impl::Slot& slot = m_impl->slots[slotIndex];
            VG_ASSERT_MSG(slot.active && slot.state == TextureResidencyState::Retiring, "retiring texture queue must contain retiring identities");
            if (!slot.retirementSealed)
            {
                hasUnsealedIdentity = true;
                break;
            }
        }
        if (m_impl->pendingRetirements.Empty() && !hasUnsealedIdentity)
            return true;
        if (!HasCompleteCutover(safeAfter))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, TextureResidencyFailureCode::MissingRetirementFence, "texture descriptor retirement requires graphics, compute, and copy cutover fences");
        }

        for (Impl::Retirement& pending : m_impl->pendingRetirements)
        {
            Impl::Installation& installation = pending.installation;
            if (!installation.descriptor.IsValid())
                continue;
            const rhi::DescriptorRetirement retirement = ToDescriptorRetirement(MergeCutover(pending.floor, safeAfter));
            rhi::Failure rhiFailure;
            if (!rhi::RetireDescriptor(m_resourceDescriptors.GetRef(), installation.descriptor, retirement, &rhiFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = TextureResidencyFailureCode::DescriptorFailure;
                    failure->message = "texture descriptor retirement failed";
                    failure->rhiFailure = rhiFailure;
                }
                ++m_impl->stats.rejectedOperations;
                return false;
            }
            installation.descriptor = {};
            installation.texture.Reset();
        }
        m_impl->pendingRetirements.Clear();
        for (const u32 slotIndex : m_impl->retiringSlots)
        {
            Impl::Slot& slot = m_impl->slots[slotIndex];
            if (!slot.retirementSealed)
                slot.retirementSealed = true;
        }
        return true;
    }

    u32 TextureResidencyManager::CollectRetirements(TextureResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
        {
            static_cast<void>(Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized"));
            return 0;
        }
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, TextureResidencyFailureCode::WrongThread, "texture retirement collection must run on the main thread"));
            return 0;
        }

        u32 collected = 0;
        for (u32 retiringIndex = 0; retiringIndex < m_impl->retiringSlots.Size();)
        {
            const u32 slotIndex = m_impl->retiringSlots[retiringIndex];
            Impl::Slot& slot = m_impl->slots[slotIndex];
            VG_ASSERT_MSG(slot.active && slot.state == TextureResidencyState::Retiring, "retiring texture queue must contain retiring identities");
            if (!slot.retirementSealed || m_lifetime->IsValid(slot.allocation))
            {
                ++retiringIndex;
                continue;
            }
            static_cast<void>(m_impl->slotByGpuIndex.Remove(slot.allocation.first));
            slot = {};
            m_impl->recycledSlots.PushBack(slotIndex);
            m_impl->retiringSlots[retiringIndex] = m_impl->retiringSlots.Back();
            m_impl->retiringSlots.PopBack();
            --m_impl->stats.liveResidencies;
            ++m_impl->stats.reclaimed;
            ++collected;
        }
        return collected;
    }

    bool TextureResidencyManager::GetInfo(const GpuTextureResidencyHandle handle, TextureResidencyInfo& info, TextureResidencyFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (!IsInitialized())
            return Fail(failure, TextureResidencyFailureCode::NotInitialized, "texture residency manager is not initialized");
        const Impl::Slot* const slot = m_impl->Find(handle);
        if (slot == nullptr)
            return Fail(failure, TextureResidencyFailureCode::StaleHandle, "texture residency query references a stale identity");
        info.handle = handle;
        info.state = slot->state;
        info.texture = slot->current.texture.GetRef();
        info.descriptor = slot->current.descriptor;
        info.firstResidentMip = slot->current.firstResidentMip;
        info.residentMipCount = slot->current.residentMipCount;
        info.installationRevision = slot->installationRevision;
        info.source = slot->current.source;
        info.contentFingerprint = slot->current.contentFingerprint;
        return true;
    }

    TextureResidencyStats TextureResidencyManager::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        TextureResidencyStats stats = m_impl->stats;
        stats.pendingInstallations = m_impl->pendingInstallationCount;
        stats.frozenInstallations = m_impl->frozenSlots.Size();
        stats.pendingDescriptorRetirements = m_impl->pendingRetirements.Size();
        return stats;
    }
} // namespace vanguard::rendering
