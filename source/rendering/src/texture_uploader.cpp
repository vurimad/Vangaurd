#include <vanguard/rendering/texture_uploader.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <cassert>
#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u64 TextureUploadCommandListRole = 0x545855504c4f4144ull; // TXUPLOAD

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(TextureUploadFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TextureUploadFailure* const failure, const TextureUploadFailureCode code, const char* const message, const TextureUploadRequestId request = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, request, message};
            return false;
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) &&
                   (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        struct UploadRequestKey
        {
            u64 resourcePath = resources::InvalidResourceId;
            u32 resourceGeneration = 0;
            GpuTextureResidencyHandle residency;
            u32 targetFirstResidentMip = 0xffffffffu;
            bool managerOwned = false;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return resourcePath != resources::InvalidResourceId && resourceGeneration != 0 && residency.IsValid() && targetFirstResidentMip != 0xffffffffu;
            }

            [[nodiscard]] u32 CalcHash() const noexcept
            {
                u64 value = resourcePath;
                value ^= static_cast<u64>(resourceGeneration) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(residency.index) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(residency.generation) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(targetFirstResidentMip) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(managerOwned) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= value >> 33u;
                value *= 0xff51afd7ed558ccdull;
                value ^= value >> 33u;
                return static_cast<u32>(value ^ (value >> 32u));
            }

            [[nodiscard]] friend constexpr bool operator==(const UploadRequestKey&, const UploadRequestKey&) noexcept = default;
        };

        [[nodiscard]] UploadRequestKey MakeUploadRequestKey(const resources::ResourceHandle& resource, const GpuTextureResidencyHandle residency, const u32 targetFirstResidentMip,
                                                            const bool managerOwned) noexcept
        {
            return {resource.GetPath().Id(), resource.GetGeneration(), residency, targetFirstResidentMip, managerOwned};
        }

        struct UploadEntry
        {
            resources::ResourceHandle resource;
            TextureUploadCandidatePlan plan;
            TextureMipTransitionPlan transitionPlan;
            textures::TextureMipAcquisition acquisition;
            SubmittedTextureCandidate submitted;
            rhi::Texture candidateTexture;
            TextureUploadFailure failure;
            UploadRequestKey key;
            GpuTextureResidencyHandle residency;
            TextureResidencyManager* residencyManager = nullptr;
            TextureTransitionToken transitionToken;
            io::AsyncPriority priority = io::eAsyncPriority_Streaming;
            TextureUploadState state = TextureUploadState::Invalid;
            u32 interests = 1;
            u32 nextSharedCopy = 0;
            u64 pendingBytes = 0;
            u64 candidateGpuBytes = 0;
            u64 sourceBytesUploaded = 0;
            u64 gpuBytesCopied = 0;
            u32 completionQueueIndex = InvalidTextureUploadRequestIndex;
            TextureUploadRequestId readyPrevious;
            TextureUploadRequestId readyNext;
            bool readyQueued = false;
            bool pendingBytesCharged = false;
            bool candidateGpuBytesCharged = false;
            bool finalizedInBatch = false;
            bool touchedInBatch = false;
            bool requiresAcquisition = true;
            bool discardOnCompletion = false;
        };
    } // namespace

    bool SubmittedTextureCandidate::IsValid() const noexcept
    {
        return !contentFingerprint.IsEmpty() && residency.IsValid() && texture.IsValid() && copyCompletion.IsValid() && residentMipCount != 0 && subresourceCount != 0 &&
               candidateGpuBytes != 0 && sourceBytesUploaded <= candidateGpuBytes && gpuBytesCopied <= candidateGpuBytes;
    }

    void SubmittedTextureCandidate::Reset() noexcept
    {
        resource.Reset();
        contentFingerprint = {};
        residency = {};
        texture.Reset();
        copyCompletion = {};
        firstResidentMip = 0;
        residentMipCount = 0;
        subresourceCount = 0;
        sourceBytesUploaded = 0;
        gpuBytesCopied = 0;
        candidateGpuBytes = 0;
    }

    struct TextureUploader::Impl
    {
        struct Slot
        {
            UploadEntry* entry = nullptr;
            u32 generation = 1;
        };

        explicit Impl(const TextureUploaderConfig& uploaderConfig) noexcept
            : slots(memory::pools::Rendering::GetInstance()), recycledSlots(memory::pools::Rendering::GetInstance()), touchedSlots(memory::pools::Rendering::GetInstance()),
              pendingCompletionSlots(memory::pools::Rendering::GetInstance()), requestSlots(memory::pools::Rendering::GetInstance()), config(uploaderConfig)
        {
            slots.Reserve(config.maximumRequests);
            recycledSlots.Reserve(config.maximumRequests);
            touchedSlots.Reserve(config.maximumCandidatesPerBatch);
            pendingCompletionSlots.Reserve(config.maximumRequests);
            requestSlots.Reserve(config.maximumRequests);
        }

        [[nodiscard]] UploadEntry* Find(const TextureUploadRequestId id) const noexcept
        {
            return id.IsValid() && id.index < slots.Size() && slots[id.index].generation == id.generation ? slots[id.index].entry : nullptr;
        }

        [[nodiscard]] TextureUploadRequestId Id(const u32 index) const noexcept
        {
            return {index, slots[index].generation};
        }

        [[nodiscard]] u32 ReadyCapacity() const noexcept
        {
            return config.maximumReadyCandidates < config.maximumRequests ? config.maximumReadyCandidates : config.maximumRequests;
        }

        void RemovePendingCompletion(const u32 slotIndex, UploadEntry& entry) noexcept
        {
            const u32 queueIndex = entry.completionQueueIndex;
            if (queueIndex == InvalidTextureUploadRequestIndex)
                return;
            assert(queueIndex < pendingCompletionSlots.Size() && pendingCompletionSlots[queueIndex] == Id(slotIndex));
            const TextureUploadRequestId moved = pendingCompletionSlots.Back();
            pendingCompletionSlots[queueIndex] = moved;
            pendingCompletionSlots.PopBack();
            entry.completionQueueIndex = InvalidTextureUploadRequestIndex;
            if (queueIndex < pendingCompletionSlots.Size())
            {
                UploadEntry* const movedEntry = Find(moved);
                assert(movedEntry != nullptr);
                movedEntry->completionQueueIndex = queueIndex;
            }
            nextCompletionSlot = queueIndex < pendingCompletionSlots.Size() ? queueIndex : 0;
        }

        void EnqueueReadyBack(const TextureUploadRequestId request, UploadEntry& entry) noexcept
        {
            assert(!entry.readyQueued);
            entry.readyPrevious = readyTail;
            entry.readyNext = {};
            entry.readyQueued = true;
            if (readyTail.IsValid())
            {
                UploadEntry* const tail = Find(readyTail);
                assert(tail != nullptr && tail->readyQueued);
                tail->readyNext = request;
            }
            else
                readyHead = request;
            readyTail = request;
            ++readyCount;
        }

        void RemoveReady(UploadEntry& entry) noexcept
        {
            if (!entry.readyQueued)
                return;
            if (entry.readyPrevious.IsValid())
            {
                UploadEntry* const previous = Find(entry.readyPrevious);
                assert(previous != nullptr && previous->readyQueued);
                previous->readyNext = entry.readyNext;
            }
            else
                readyHead = entry.readyNext;
            if (entry.readyNext.IsValid())
            {
                UploadEntry* const next = Find(entry.readyNext);
                assert(next != nullptr && next->readyQueued);
                next->readyPrevious = entry.readyPrevious;
            }
            else
                readyTail = entry.readyPrevious;
            entry.readyPrevious = {};
            entry.readyNext = {};
            entry.readyQueued = false;
            --readyCount;
        }

        void ReleasePendingBudgets(UploadEntry& entry) noexcept
        {
            if (entry.pendingBytesCharged)
            {
                assert(pendingCandidateBytes >= entry.pendingBytes);
                pendingCandidateBytes -= entry.pendingBytes;
                entry.pendingBytesCharged = false;
            }
            if (entry.candidateGpuBytesCharged)
            {
                assert(pendingCandidateGpuBytes >= entry.candidateGpuBytes);
                pendingCandidateGpuBytes -= entry.candidateGpuBytes;
                entry.candidateGpuBytesCharged = false;
            }
        }

        [[nodiscard]] bool CanRetainCandidate(const UploadEntry& entry) const noexcept
        {
            return entry.candidateGpuBytesCharged ||
                   (entry.candidateGpuBytes != 0 && entry.candidateGpuBytes <= config.maximumPendingCandidateGpuBytes &&
                    pendingCandidateGpuBytes <= config.maximumPendingCandidateGpuBytes - entry.candidateGpuBytes);
        }

        void ChargeCandidate(UploadEntry& entry) noexcept
        {
            assert(!entry.candidateGpuBytesCharged && entry.candidateGpuBytes != 0 && CanRetainCandidate(entry));
            pendingCandidateGpuBytes += entry.candidateGpuBytes;
            entry.candidateGpuBytesCharged = true;
        }

        void RemoveRequestLookup(const u32 index, const UploadEntry& entry) noexcept
        {
            u32 mapped = InvalidTextureUploadRequestIndex;
            if (entry.key.IsValid() && requestSlots.Find(entry.key, mapped) && mapped == index)
                static_cast<void>(requestSlots.Remove(entry.key));
        }

        void Remove(const u32 index) noexcept
        {
            UploadEntry* const entry = slots[index].entry;
            if (entry == nullptr)
                return;
            RemovePendingCompletion(index, *entry);
            RemoveReady(*entry);
            RemoveRequestLookup(index, *entry);
            ReleasePendingBudgets(*entry);
            if (entry->residencyManager != nullptr && entry->transitionToken.IsValid())
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
            DeleteObject(entry);
            slots[index].entry = nullptr;
            ++slots[index].generation;
            if (slots[index].generation == 0)
                ++slots[index].generation;
            recycledSlots.PushBack(index);
            --stats.liveRequests;
        }

        void SetFailed(const u32 index, TextureUploadFailure requestFailure) noexcept
        {
            UploadEntry& entry = *slots[index].entry;
            RemovePendingCompletion(index, entry);
            RemoveReady(entry);
            RemoveRequestLookup(index, entry);
            static_cast<void>(entry.acquisition.Cancel());
            entry.acquisition.Reset();
            entry.resource.Reset();
            entry.candidateTexture.Reset();
            entry.submitted.Reset();
            ReleasePendingBudgets(entry);
            if (entry.residencyManager != nullptr && entry.transitionToken.IsValid())
                static_cast<void>(entry.residencyManager->EndTransition(entry.transitionToken));
            entry.residencyManager = nullptr;
            entry.transitionToken = {};
            requestFailure.request = Id(index);
            entry.failure = requestFailure;
            entry.state = TextureUploadState::Failed;
            ++stats.requestsFailed;
        }

        containers::DynamicArray<Slot> slots;
        containers::DynamicArray<u32> recycledSlots;
        containers::DynamicArray<u32> touchedSlots;
        containers::DynamicArray<TextureUploadRequestId> pendingCompletionSlots;
        containers::HashMap<UploadRequestKey, u32> requestSlots;
        TextureUploaderConfig config;
        TextureUploaderStats stats;
        u64 pendingCandidateBytes = 0;
        u64 pendingCandidateGpuBytes = 0;
        u32 nextScanSlot = 0;
        u32 nextCompletionSlot = 0;
        TextureUploadRequestId readyHead;
        TextureUploadRequestId readyTail;
        u32 readyCount = 0;

        void PollCompletions() noexcept
        {
            u32 polled = 0;
            while (polled < config.maximumCompletionPollsPerTick && !pendingCompletionSlots.Empty() && readyCount < ReadyCapacity())
            {
                if (nextCompletionSlot >= pendingCompletionSlots.Size())
                    nextCompletionSlot = 0;
                const TextureUploadRequestId request = pendingCompletionSlots[nextCompletionSlot];
                UploadEntry* const entry = Find(request);
                ++polled;
                assert(entry != nullptr && entry->state == TextureUploadState::Submitted);
                if (!rhi::IsGpuFenceComplete(entry->submitted.copyCompletion))
                {
                    ++nextCompletionSlot;
                    continue;
                }

                RemovePendingCompletion(request.index, *entry);
                if (entry->discardOnCompletion)
                {
                    Remove(request.index);
                    continue;
                }
                entry->state = TextureUploadState::ReadyToInstall;
                EnqueueReadyBack(request, *entry);
            }
            if (pendingCompletionSlots.Empty() || nextCompletionSlot >= pendingCompletionSlots.Size())
                nextCompletionSlot = 0;
        }
    };

    TextureUploader::~TextureUploader()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool TextureUploader::Initialize(const TextureUploaderConfig& config, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, TextureUploadFailureCode::AlreadyInitialized, "texture uploader is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "texture uploader must initialize on the main thread");
        if (!rhi::IsInitialized())
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader requires an initialized RHI");
        if (config.maximumRequests == 0 || config.maximumAcquisitionStartsPerTick == 0 || config.maximumCandidatesPerBatch == 0 || config.maximumCompletionPollsPerTick == 0 ||
            config.maximumReadyCandidates == 0 || config.maximumWritesPerBatch == 0 || config.maximumCopiesPerBatch == 0 || config.maximumAcquisitionWindowSubresources == 0 ||
            config.maximumBytesPerBatch == 0 || config.maximumCopyBytesPerBatch == 0 || config.maximumCandidateGpuBytesPerBatch == 0 ||
            config.maximumPendingCandidateGpuBytes == 0 || config.maximumPendingCandidateBytes == 0 ||
            config.maximumAcquisitionWindowBytes == 0)
            return Fail(failure, TextureUploadFailureCode::InvalidConfiguration, "texture uploader configuration is invalid");
        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture uploader storage allocation failed");
        return true;
    }

    bool TextureUploader::Shutdown(TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "texture uploader must shut down on the main thread");
        if (m_impl->stats.liveRequests != 0)
            return Fail(failure, TextureUploadFailureCode::LiveRequestsRemain, "texture uploader still owns acquiring, failed, or submitted requests");
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureUploader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool TextureUploader::RequestMipTail(const resources::ResourceHandle& resource, const GpuTextureResidencyHandle residency, TextureUploadRequestId& request,
                                         const io::AsyncPriority priority, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        request = {};
        if (!resource.IsValid() || resource.GetType() != textures::TextureResourceType)
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture mip-tail request requires a live texture resource");
        auto* const object = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (object == nullptr || !object->IsOpen())
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture mip-tail request references a closed texture resource");
        return RequestInternal(resource, nullptr, residency, object->GetMetadata().GetMipTailFirstLevel(), request, priority, failure);
    }

    bool TextureUploader::RequestMipTransition(const resources::ResourceHandle& resource, TextureResidencyManager& residencyManager, const GpuTextureResidencyHandle residency,
                                               const u32 targetFirstResidentMip, TextureUploadRequestId& request, const io::AsyncPriority priority,
                                               TextureUploadFailure* const failure) noexcept
    {
        return RequestInternal(resource, &residencyManager, residency, targetFirstResidentMip, request, priority, failure);
    }

    bool TextureUploader::RequestMipTail(const resources::ResourceHandle& resource, TextureResidencyManager& residencyManager, const GpuTextureResidencyHandle residency,
                                         TextureUploadRequestId& request, const io::AsyncPriority priority, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        request = {};
        if (!resource.IsValid() || resource.GetType() != textures::TextureResourceType)
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture mip-tail request requires a live texture resource");
        auto* const object = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (object == nullptr || !object->IsOpen())
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture mip-tail request references a closed texture resource");
        return RequestInternal(resource, &residencyManager, residency, object->GetMetadata().GetMipTailFirstLevel(), request, priority, failure);
    }

    bool TextureUploader::RequestInternal(const resources::ResourceHandle& resource, TextureResidencyManager* const residencyManager, const GpuTextureResidencyHandle residency,
                                          const u32 requestedFirstResidentMip, TextureUploadRequestId& request, const io::AsyncPriority priority,
                                          TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        request = {};
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "texture upload requests must be issued on the main thread");
        if (!resource.IsValid() || resource.GetType() != textures::TextureResourceType || !residency.IsValid())
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture upload request requires a live texture resource and residency handle");
        auto* const object = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (object == nullptr || !object->IsOpen())
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture upload request references a closed texture resource");

        const textures::TextureFile& metadata = object->GetMetadata();
        const u32 totalMipCount = metadata.GetMipCount();
        const u32 guaranteedTailFirstMip = metadata.GetMipTailFirstLevel();
        if (requestedFirstResidentMip >= totalMipCount)
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture upload target mip is outside the asset");
        const u32 targetFirstResidentMip = requestedFirstResidentMip > guaranteedTailFirstMip ? guaranteedTailFirstMip : requestedFirstResidentMip;
        const UploadRequestKey requestKey = MakeUploadRequestKey(resource, residency, targetFirstResidentMip, residencyManager != nullptr);
        u32 existingSlot = InvalidTextureUploadRequestIndex;
        if (m_impl->requestSlots.Find(requestKey, existingSlot))
        {
            UploadEntry* const entry = existingSlot < m_impl->slots.Size() ? m_impl->slots[existingSlot].entry : nullptr;
            if (entry != nullptr && entry->key == requestKey && entry->state != TextureUploadState::Failed)
            {
                if (entry->interests == 0xffffffffu)
                    return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture upload interest count overflowed");
                ++entry->interests;
                request = m_impl->Id(existingSlot);
                ++m_impl->stats.requestsCoalesced;
                return true;
            }
            static_cast<void>(m_impl->requestSlots.Remove(requestKey));
        }
        if (m_impl->stats.liveRequests >= m_impl->config.maximumRequests)
            return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture upload request capacity is exhausted");

        UploadEntry* const entry = AllocateObject<UploadEntry>();
        if (entry == nullptr)
            return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture upload request allocation failed");
        TextureUploadCandidateFailure candidateFailure;
        if (residencyManager != nullptr)
        {
            TextureResidencyInfo current;
            TextureResidencyFailure residencyFailure;
            if (!residencyManager->GetInfo(residency, current, &residencyFailure))
            {
                DeleteObject(entry);
                if (failure != nullptr)
                {
                    failure->code = TextureUploadFailureCode::ResidencyFailure;
                    failure->message = "texture transition cannot inspect its residency identity";
                    failure->residencyFailure = residencyFailure;
                }
                return false;
            }
            if (current.texture.IsValid() && current.residentMipCount != totalMipCount - current.firstResidentMip)
            {
                DeleteObject(entry);
                return Fail(failure, TextureUploadFailureCode::InvalidArgument, "current texture residency is not a complete compact mip suffix");
            }
            if (current.texture.IsValid() && !entry->transitionPlan.Build(totalMipCount, guaranteedTailFirstMip, current.firstResidentMip, targetFirstResidentMip, &candidateFailure))
            {
                DeleteObject(entry);
                if (failure != nullptr)
                {
                    failure->code = TextureUploadFailureCode::CandidateFailure;
                    failure->message = "texture mip transition plan is invalid";
                    failure->candidateFailure = candidateFailure;
                }
                return false;
            }
            if (!residencyManager->BeginTransition(residency, resource.ToWeak(), metadata.GetContentFingerprint(), targetFirstResidentMip, entry->transitionToken, &residencyFailure))
            {
                DeleteObject(entry);
                if (failure != nullptr)
                {
                    failure->code = TextureUploadFailureCode::ResidencyFailure;
                    failure->message = "texture residency transition admission failed";
                    failure->residencyFailure = residencyFailure;
                }
                return false;
            }
            entry->residencyManager = residencyManager;
            if (entry->transitionPlan.IsValid() && entry->transitionPlan.GetKind() == TextureMipTransitionKind::NoOp)
            {
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
                DeleteObject(entry);
                return true;
            }
        }

        if (!entry->plan.Initialize(metadata, targetFirstResidentMip, totalMipCount - targetFirstResidentMip, rhi::GetCapabilities(), &candidateFailure))
        {
            if (entry->residencyManager != nullptr && entry->transitionToken.IsValid())
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
            DeleteObject(entry);
            if (failure != nullptr)
            {
                failure->code = TextureUploadFailureCode::CandidateFailure;
                failure->message = "texture mip tail cannot form a physical upload candidate";
                failure->candidateFailure = candidateFailure;
            }
            return false;
        }
        entry->pendingBytes = entry->plan.GetExpectedByteCount();
        if (entry->pendingBytes > m_impl->config.maximumPendingCandidateBytes || m_impl->pendingCandidateBytes > m_impl->config.maximumPendingCandidateBytes - entry->pendingBytes)
        {
            if (entry->residencyManager != nullptr && entry->transitionToken.IsValid())
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
            DeleteObject(entry);
            return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture pending candidate byte budget is exhausted");
        }

        const u32 uploadFirstMip = entry->transitionPlan.IsValid() ? entry->transitionPlan.GetUploadFirstMip() : targetFirstResidentMip;
        const u32 uploadMipCount = entry->transitionPlan.IsValid() ? entry->transitionPlan.GetUploadMipCount() : totalMipCount - targetFirstResidentMip;
        entry->requiresAcquisition = uploadMipCount != 0;
        const textures::TextureMipReadWindowLimits windowLimits{m_impl->config.maximumAcquisitionWindowBytes, m_impl->config.maximumAcquisitionWindowSubresources};
        const textures::Result opened =
            entry->requiresAcquisition ? entry->acquisition.Open(resource, static_cast<u8>(uploadFirstMip), static_cast<u8>(uploadMipCount), windowLimits) : textures::Result::Success;
        if (opened != textures::Result::Success)
        {
            if (entry->residencyManager != nullptr && entry->transitionToken.IsValid())
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
            DeleteObject(entry);
            if (failure != nullptr)
            {
                failure->code = TextureUploadFailureCode::AcquisitionFailure;
                failure->message = "texture mip-tail acquisition could not be opened";
                failure->acquisitionFailure = opened;
            }
            return false;
        }
        entry->resource = resource;
        entry->key = requestKey;
        entry->residency = residency;
        entry->priority = priority;
        entry->state = TextureUploadState::Acquiring;

        u32 slot = InvalidTextureUploadRequestIndex;
        const bool recycledSlot = !m_impl->recycledSlots.Empty();
        if (recycledSlot)
        {
            slot = m_impl->recycledSlots.Back();
            m_impl->recycledSlots.PopBack();
            m_impl->slots[slot].entry = entry;
        }
        else
        {
            slot = m_impl->slots.Size();
            m_impl->slots.PushBack({entry, 1});
        }

        if (!m_impl->requestSlots.Insert(entry->key, slot).IsSuccessful())
        {
            if (recycledSlot)
            {
                m_impl->slots[slot].entry = nullptr;
                m_impl->recycledSlots.PushBack(slot);
            }
            else
            {
                m_impl->slots.PopBack();
            }
            if (entry->residencyManager != nullptr && entry->transitionToken.IsValid())
                static_cast<void>(entry->residencyManager->EndTransition(entry->transitionToken));
            DeleteObject(entry);
            return Fail(failure, TextureUploadFailureCode::CapacityExceeded, "texture upload request lookup allocation failed");
        }

        m_impl->pendingCandidateBytes += entry->pendingBytes;
        entry->pendingBytesCharged = true;
        request = m_impl->Id(slot);
        ++m_impl->stats.requestsIssued;
        ++m_impl->stats.liveRequests;
        return true;
    }

    bool TextureUploader::Cancel(const TextureUploadRequestId request, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized", request);
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "texture upload cancellation must run on the main thread", request);
        UploadEntry* const entry = m_impl->Find(request);
        if (entry == nullptr)
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture upload request is stale", request);
        if (entry->interests > 1)
        {
            --entry->interests;
            ++m_impl->stats.requestsCancelled;
            return true;
        }
        if (entry->state == TextureUploadState::Submitted)
        {
            m_impl->RemoveRequestLookup(request.index, *entry);
            entry->interests = 0;
            entry->discardOnCompletion = true;
            ++m_impl->stats.requestsCancelled;
            return true;
        }
        if (entry->state == TextureUploadState::ReadyToInstall)
        {
            m_impl->Remove(request.index);
            ++m_impl->stats.requestsCancelled;
            return true;
        }
        static_cast<void>(entry->acquisition.Cancel());
        m_impl->Remove(request.index);
        ++m_impl->stats.requestsCancelled;
        return true;
    }

    bool TextureUploader::Tick(const rhi::ResidencyFenceSet& prerequisiteFences, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "texture uploader tick must run on the main thread");
        m_impl->PollCompletions();
        if (rhi::GetBoundCommandList().IsValid())
            return Fail(failure, TextureUploadFailureCode::CommandListBusy, "texture uploader cannot record while another command list is bound");

        m_impl->touchedSlots.Clear();
        rhi::CommandListRef commandList;
        u32 acquisitionStarts = 0;
        u32 candidateAttempts = 0;
        u32 writeCount = 0;
        u32 copyCount = 0;
        u64 byteCount = 0;
        u64 copyByteCount = 0;
        u64 candidateByteCount = 0;
        const bool copiesAreSafe = FencesComplete(prerequisiteFences);

        const auto Touch = [&](const u32 slot, UploadEntry& entry) noexcept
        {
            if (!entry.touchedInBatch)
            {
                entry.touchedInBatch = true;
                m_impl->touchedSlots.PushBack(slot);
            }
        };
        const auto EnsureCommandList = [&]() noexcept -> bool
        {
            if (commandList.IsValid())
                return true;
            rhi::Failure rhiFailure;
            commandList = rhi::CreateCommandList(rhi::CommandListType::CopySync, TextureUploadCommandListRole, &rhiFailure);
            if (commandList.IsValid() && rhi::BindCommandList(commandList, &rhiFailure))
                return true;
            rhi::DiscardCommandList(commandList);
            if (failure != nullptr)
            {
                failure->code = TextureUploadFailureCode::RhiFailure;
                failure->message = "texture transition CopySync command list could not be opened";
                failure->rhiFailure = rhiFailure;
            }
            return false;
        };
        const auto EnsureCandidate = [&](const u32 slot, UploadEntry& entry) noexcept -> bool
        {
            if (entry.candidateTexture.IsValid())
                return true;

            const auto FailUnretainableCandidate = [&]() noexcept
            {
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::CapacityExceeded;
                requestFailure.message = "one compact texture exceeds a physical candidate budget";
                m_impl->SetFailed(slot, requestFailure);
            };

            // Once discovered, the exact requirement is cached even when this tick cannot retain the
            // allocation. That makes later ticks reject it from the two budgets without create/reset churn.
            if (entry.candidateGpuBytes != 0)
            {
                if (entry.candidateGpuBytes > m_impl->config.maximumCandidateGpuBytesPerBatch ||
                    entry.candidateGpuBytes > m_impl->config.maximumPendingCandidateGpuBytes)
                {
                    FailUnretainableCandidate();
                    return false;
                }
                if (entry.candidateGpuBytes > m_impl->config.maximumCandidateGpuBytesPerBatch - candidateByteCount || !m_impl->CanRetainCandidate(entry))
                    return false;
            }
            if (candidateAttempts == m_impl->config.maximumCandidatesPerBatch)
                return false;
            ++candidateAttempts;

            rhi::Failure rhiFailure;
            entry.candidateTexture = rhi::Texture::Create(entry.plan.GetTextureDesc(), rhi::TextureInitData{}, &rhiFailure);
            if (!entry.candidateTexture.IsValid())
            {
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::RhiFailure;
                requestFailure.message = "compact texture candidate creation failed";
                requestFailure.rhiFailure = rhiFailure;
                m_impl->SetFailed(slot, requestFailure);
                return false;
            }

            const u64 exactGpuBytes = rhi::GetMemoryRequirements(entry.candidateTexture.GetRef()).size;
            if (exactGpuBytes == 0)
            {
                entry.candidateTexture.Reset();
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::RhiFailure;
                requestFailure.message = "compact texture candidate has no physical memory requirements";
                m_impl->SetFailed(slot, requestFailure);
                return false;
            }
            entry.candidateGpuBytes = exactGpuBytes;
            if (entry.candidateGpuBytes > m_impl->config.maximumCandidateGpuBytesPerBatch ||
                entry.candidateGpuBytes > m_impl->config.maximumPendingCandidateGpuBytes)
            {
                entry.candidateTexture.Reset();
                FailUnretainableCandidate();
                return false;
            }
            if (entry.candidateGpuBytes > m_impl->config.maximumCandidateGpuBytesPerBatch - candidateByteCount ||
                !m_impl->CanRetainCandidate(entry))
            {
                entry.candidateTexture.Reset();
                return false;
            }

            m_impl->ChargeCandidate(entry);
            candidateByteCount += entry.candidateGpuBytes;
            return true;
        };

        const u32 slotCount = m_impl->slots.Size();
        u32 scanSlot = slotCount != 0 ? m_impl->nextScanSlot % slotCount : 0;
        for (u32 visited = 0; visited < slotCount; ++visited)
        {
            const u32 slot = scanSlot;
            scanSlot = scanSlot + 1u == slotCount ? 0u : scanSlot + 1u;
            m_impl->nextScanSlot = scanSlot;
            UploadEntry* const entry = m_impl->slots[slot].entry;
            if (entry == nullptr || entry->state != TextureUploadState::Acquiring)
                continue;

            textures::TextureMipAcquisitionState acquisitionState = entry->requiresAcquisition ? entry->acquisition.GetState() : textures::TextureMipAcquisitionState::Complete;
            if (entry->requiresAcquisition && acquisitionState == textures::TextureMipAcquisitionState::Idle)
            {
                if (acquisitionStarts == m_impl->config.maximumAcquisitionStartsPerTick)
                    continue;
                ++acquisitionStarts;
                const textures::Result issued = entry->acquisition.BeginNextWindow(entry->priority);
                if (issued != textures::Result::Success)
                {
                    TextureUploadFailure requestFailure;
                    requestFailure.code = TextureUploadFailureCode::AcquisitionFailure;
                    requestFailure.message = "texture subresource window could not be issued";
                    requestFailure.acquisitionFailure = issued;
                    m_impl->SetFailed(slot, requestFailure);
                    continue;
                }
                ++m_impl->stats.acquisitionWindowsStarted;
                acquisitionState = entry->acquisition.GetState();
            }
            if (entry->requiresAcquisition && acquisitionState == textures::TextureMipAcquisitionState::Reading)
                acquisitionState = entry->acquisition.Poll();
            if (acquisitionState == textures::TextureMipAcquisitionState::Reading)
                continue;
            if (acquisitionState == textures::TextureMipAcquisitionState::Failed || acquisitionState == textures::TextureMipAcquisitionState::Cancelled)
            {
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::AcquisitionFailure;
                requestFailure.message = "verified texture subresource acquisition failed";
                requestFailure.acquisitionFailure = entry->acquisition.GetResult();
                m_impl->SetFailed(slot, requestFailure);
                continue;
            }
            if (acquisitionState == textures::TextureMipAcquisitionState::Ready)
            {
                const containers::ArraySpan<const textures::TextureSubresourceView> window = entry->acquisition.GetWindow();
                u64 windowBytes = 0;
                for (const textures::TextureSubresourceView& view : window)
                {
                    if (view.bytes.Count() > ~u64{0} - windowBytes)
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = TextureUploadFailureCode::CapacityExceeded;
                        requestFailure.message = "texture upload window byte count overflowed";
                        m_impl->SetFailed(slot, requestFailure);
                        break;
                    }
                    windowBytes += view.bytes.Count();
                }
                if (entry->state == TextureUploadState::Failed)
                    continue;
                if (window.Count() == 0 || window.Count() > m_impl->config.maximumWritesPerBatch || windowBytes > m_impl->config.maximumBytesPerBatch)
                {
                    TextureUploadFailure requestFailure;
                    requestFailure.code = TextureUploadFailureCode::CapacityExceeded;
                    requestFailure.message = "texture upload window exceeds the hard copy-batch limits";
                    m_impl->SetFailed(slot, requestFailure);
                    continue;
                }
                if (window.Count() > m_impl->config.maximumWritesPerBatch - writeCount || windowBytes > m_impl->config.maximumBytesPerBatch - byteCount)
                    continue;
                if (!EnsureCandidate(slot, *entry))
                    continue;
                if (!EnsureCommandList())
                    return false;
                bool wroteWindow = true;
                for (const textures::TextureSubresourceView& view : window)
                {
                    TextureUploadCandidateSubresource mapped;
                    TextureUploadCandidateFailure candidateFailure;
                    if (!entry->plan.MapSubresource(view, mapped, &candidateFailure))
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = TextureUploadFailureCode::CandidateFailure;
                        requestFailure.message = "verified texture subresource did not match the candidate plan";
                        requestFailure.candidateFailure = candidateFailure;
                        m_impl->SetFailed(slot, requestFailure);
                        wroteWindow = false;
                        break;
                    }
                    rhi::Failure rhiFailure;
                    if (!rhi::WriteTexture(entry->candidateTexture.GetRef(), mapped.upload, &rhiFailure) || !entry->plan.MarkUploaded(mapped, &candidateFailure))
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = rhiFailure.code != rhi::FailureCode::None ? TextureUploadFailureCode::RhiFailure : TextureUploadFailureCode::CandidateFailure;
                        requestFailure.message = "texture source upload failed";
                        requestFailure.rhiFailure = rhiFailure;
                        requestFailure.candidateFailure = candidateFailure;
                        m_impl->SetFailed(slot, requestFailure);
                        wroteWindow = false;
                        break;
                    }
                    ++writeCount;
                    byteCount += mapped.upload.size;
                    entry->sourceBytesUploaded += mapped.upload.size;
                }
                if (!wroteWindow)
                    continue;
                Touch(slot, *entry);
                const textures::Result released = entry->acquisition.ReleaseWindow();
                if (released != textures::Result::Success)
                {
                    TextureUploadFailure requestFailure;
                    requestFailure.code = TextureUploadFailureCode::AcquisitionFailure;
                    requestFailure.message = "staged texture window could not release its CPU bytes";
                    requestFailure.acquisitionFailure = released;
                    m_impl->SetFailed(slot, requestFailure);
                    continue;
                }
                acquisitionState = entry->acquisition.GetState();
            }
            if (entry->requiresAcquisition && acquisitionState != textures::TextureMipAcquisitionState::Complete)
                continue;

            if (entry->transitionPlan.IsValid() && entry->nextSharedCopy < entry->transitionPlan.GetSharedMipCount() * entry->plan.GetTextureDesc().arraySize)
            {
                if (!copiesAreSafe)
                    continue;
                if (!EnsureCandidate(slot, *entry))
                    continue;
                TextureTransitionInfo transitionInfo;
                TextureResidencyFailure residencyFailure;
                if (entry->residencyManager == nullptr || !entry->residencyManager->GetTransitionInfo(entry->transitionToken, transitionInfo, &residencyFailure) ||
                    !transitionInfo.currentTexture.IsValid())
                {
                    TextureUploadFailure requestFailure;
                    requestFailure.code = TextureUploadFailureCode::ResidencyFailure;
                    requestFailure.message = "texture shared-mip copy source is no longer valid";
                    requestFailure.residencyFailure = residencyFailure;
                    m_impl->SetFailed(slot, requestFailure);
                    continue;
                }
                const u32 arraySize = entry->plan.GetTextureDesc().arraySize;
                const u32 totalCopies = entry->transitionPlan.GetSharedMipCount() * arraySize;
                while (entry->nextSharedCopy < totalCopies && copyCount < m_impl->config.maximumCopiesPerBatch)
                {
                    const u32 sharedMip = entry->nextSharedCopy / arraySize;
                    const u32 arraySlice = entry->nextSharedCopy % arraySize;
                    TextureCandidateCopySubresource mapped;
                    TextureUploadCandidateFailure candidateFailure;
                    if (!entry->plan.MapSharedCopy(entry->transitionPlan, sharedMip, arraySlice, mapped, &candidateFailure))
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = TextureUploadFailureCode::CandidateFailure;
                        requestFailure.message = "shared texture subresource did not match the transition plan";
                        requestFailure.candidateFailure = candidateFailure;
                        m_impl->SetFailed(slot, requestFailure);
                        break;
                    }
                    if (mapped.byteSize > m_impl->config.maximumCopyBytesPerBatch)
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = TextureUploadFailureCode::CapacityExceeded;
                        requestFailure.message = "one shared texture subresource exceeds the copy-byte batch budget";
                        m_impl->SetFailed(slot, requestFailure);
                        break;
                    }
                    if (mapped.byteSize > m_impl->config.maximumCopyBytesPerBatch - copyByteCount)
                        break;
                    if (!EnsureCommandList())
                        return false;
                    const rhi::ResourceState shaderRead = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
                    const rhi::SubresourceRange sourceRange{mapped.copy.source.mipLevel, 1, mapped.copy.source.arraySlice, 1};
                    const rhi::SubresourceRange destinationRange{mapped.copy.destination.mipLevel, 1, mapped.copy.destination.arraySlice, 1};
                    rhi::Failure rhiFailure;
                    if (!rhi::TransitionTexture(transitionInfo.currentTexture, shaderRead, rhi::ResourceState::CopySource, sourceRange, &rhiFailure) ||
                        !rhi::TransitionTexture(entry->candidateTexture.GetRef(), shaderRead, rhi::ResourceState::CopyDestination, destinationRange, &rhiFailure) ||
                        !rhi::CopyTexture(entry->candidateTexture.GetRef(), transitionInfo.currentTexture, mapped.copy, &rhiFailure) || !entry->plan.MarkCopied(mapped, &candidateFailure))
                    {
                        TextureUploadFailure requestFailure;
                        requestFailure.code = rhiFailure.code != rhi::FailureCode::None ? TextureUploadFailureCode::RhiFailure : TextureUploadFailureCode::CandidateFailure;
                        requestFailure.message = "shared texture subresource copy failed";
                        requestFailure.rhiFailure = rhiFailure;
                        requestFailure.candidateFailure = candidateFailure;
                        m_impl->SetFailed(slot, requestFailure);
                        break;
                    }
                    ++entry->nextSharedCopy;
                    ++copyCount;
                    copyByteCount += mapped.byteSize;
                    entry->gpuBytesCopied += mapped.byteSize;
                    Touch(slot, *entry);
                }
                if (entry->state == TextureUploadState::Failed || entry->nextSharedCopy != totalCopies)
                    continue;
            }

            if (!entry->plan.IsComplete())
            {
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::CandidateFailure;
                requestFailure.message = "texture transition finished without exact candidate coverage";
                requestFailure.candidateFailure.code = TextureUploadCandidateFailureCode::InvalidState;
                m_impl->SetFailed(slot, requestFailure);
                continue;
            }
            entry->finalizedInBatch = entry->touchedInBatch;
        }

        if (!commandList.IsValid())
            return true;

        // Candidate validation or the first WriteTexture call may fail after the list is opened. Do not submit
        // an empty command list in that case; there are no staged source bytes whose lifetime must be fenced.
        if (m_impl->touchedSlots.Empty())
        {
            rhi::UnbindCommandList();
            rhi::DiscardCommandList(commandList);
            return true;
        }

        rhi::UnbindCommandList();
        rhi::GpuFence completion;
        rhi::Failure submissionFailure;
        const rhi::CommandListRef lists[]{commandList};
        if (!rhi::CloseAndSubmitCommandLists("TextureUpload.Batch", {lists, 1}, rhi::CommandListSyncType::None, completion, &submissionFailure))
        {
            rhi::DiscardCommandList(commandList);
            for (const u32 slot : m_impl->touchedSlots)
            {
                UploadEntry* const entry = slot < m_impl->slots.Size() ? m_impl->slots[slot].entry : nullptr;
                if (entry == nullptr || entry->state == TextureUploadState::Failed)
                    continue;
                TextureUploadFailure requestFailure;
                requestFailure.code = TextureUploadFailureCode::SubmissionFailure;
                requestFailure.message = "texture upload batch submission failed";
                requestFailure.rhiFailure = submissionFailure;
                m_impl->SetFailed(slot, requestFailure);
            }
            if (failure != nullptr)
            {
                failure->code = TextureUploadFailureCode::SubmissionFailure;
                failure->message = "texture upload batch submission failed";
                failure->rhiFailure = submissionFailure;
            }
            return false;
        }

        u32 submittedCandidates = 0;
        for (const u32 slot : m_impl->touchedSlots)
        {
            UploadEntry* const entry = slot < m_impl->slots.Size() ? m_impl->slots[slot].entry : nullptr;
            if (entry == nullptr || entry->state == TextureUploadState::Failed)
                continue;
            const bool finalized = entry->finalizedInBatch;
            entry->finalizedInBatch = false;
            entry->touchedInBatch = false;
            if (!finalized)
                continue;
            entry->submitted.resource = entry->resource.ToWeak();
            entry->submitted.contentFingerprint = entry->plan.GetContentFingerprint();
            entry->submitted.residency = entry->residency;
            entry->submitted.texture = std::move(entry->candidateTexture);
            entry->submitted.copyCompletion = completion;
            entry->submitted.firstResidentMip = entry->plan.GetFirstAssetMip();
            entry->submitted.residentMipCount = entry->plan.GetResidentMipCount();
            entry->submitted.subresourceCount = entry->plan.GetExpectedSubresourceCount();
            entry->submitted.sourceBytesUploaded = entry->sourceBytesUploaded;
            entry->submitted.gpuBytesCopied = entry->gpuBytesCopied;
            entry->submitted.candidateGpuBytes = entry->candidateGpuBytes;
            entry->acquisition.Reset();
            entry->resource.Reset();
            entry->state = TextureUploadState::Submitted;
            entry->completionQueueIndex = m_impl->pendingCompletionSlots.Size();
            m_impl->pendingCompletionSlots.PushBack(m_impl->Id(slot));
            ++submittedCandidates;
        }
        ++m_impl->stats.batchesSubmitted;
        m_impl->stats.candidatesSubmitted += submittedCandidates;
        m_impl->stats.writesSubmitted += writeCount;
        m_impl->stats.copiesSubmitted += copyCount;
        m_impl->stats.bytesSubmitted += byteCount;
        m_impl->stats.copyBytesSubmitted += copyByteCount;
        m_impl->stats.candidateGpuBytesSubmitted += candidateByteCount;
        return true;
    }

    bool TextureUploader::InstallReadyCandidates(TextureResidencyManager& residencyManager, const u32 maximumInstallations, u32& installed, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        installed = 0;
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "completed texture installation must run on the main thread");
        if (!residencyManager.IsInitialized() || maximumInstallations == 0)
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "completed texture installation requires an initialized residency manager and non-zero capacity");

        while (installed < maximumInstallations && m_impl->readyHead.IsValid())
        {
            const TextureUploadRequestId request = m_impl->readyHead;
            UploadEntry* const entry = m_impl->Find(request);
            if (entry == nullptr || entry->state != TextureUploadState::ReadyToInstall)
            {
                assert(false && "the ready texture FIFO must contain a live ready request");
                return Fail(failure, TextureUploadFailureCode::InvalidArgument, "ready texture FIFO contains an invalid request", request);
            }
            if (entry->residencyManager != nullptr && entry->residencyManager != &residencyManager)
                return Fail(failure, TextureUploadFailureCode::InvalidArgument, "completed texture candidate belongs to a different residency manager", request);

            resources::ResourceHandle source = entry->submitted.resource.Lock();
            auto* const texture = source.IsValid() && source.GetType() == textures::TextureResourceType ? static_cast<textures::TextureResourceObject*>(source.Get()) : nullptr;
            if (texture == nullptr || !texture->IsOpen() || !(texture->GetMetadata().GetContentFingerprint() == entry->submitted.contentFingerprint))
            {
                TextureUploadFailure staleFailure;
                staleFailure.code = TextureUploadFailureCode::InvalidArgument;
                staleFailure.request = request;
                staleFailure.message = "completed texture candidate source generation or content is stale";
                m_impl->SetFailed(request.index, staleFailure);
                if (failure != nullptr)
                    *failure = staleFailure;
                return false;
            }

            TextureInstallationTicket ticket;
            TextureResidencyFailure residencyFailure;
            TextureInstallationDesc installation;
            installation.texture = entry->submitted.texture.GetRef();
            installation.firstResidentMip = entry->submitted.firstResidentMip;
            installation.residentMipCount = entry->submitted.residentMipCount;
            installation.source = entry->submitted.resource;
            installation.contentFingerprint = entry->submitted.contentFingerprint;
            installation.transition = entry->transitionToken;
            if (!residencyManager.Install(entry->submitted.residency, installation, ticket, &residencyFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = TextureUploadFailureCode::ResidencyFailure;
                    failure->request = request;
                    failure->message = "completed texture candidate could not enter residency installation";
                    failure->residencyFailure = residencyFailure;
                }
                return false;
            }

            // Residency now owns the transition until the shared table batch accepts or discards it.
            entry->transitionToken = {};
            entry->residencyManager = nullptr;
            m_impl->Remove(request.index);
            ++installed;
        }
        return true;
    }

    TextureUploadState TextureUploader::GetState(const TextureUploadRequestId request) const noexcept
    {
        const UploadEntry* const entry = m_impl != nullptr ? m_impl->Find(request) : nullptr;
        return entry != nullptr ? entry->state : TextureUploadState::Invalid;
    }

    bool TextureUploader::GetRequestFailure(const TextureUploadRequestId request, TextureUploadFailure& failure) const noexcept
    {
        failure = {};
        const UploadEntry* const entry = m_impl != nullptr ? m_impl->Find(request) : nullptr;
        if (entry == nullptr || entry->state != TextureUploadState::Failed)
            return false;
        failure = entry->failure;
        return true;
    }

    bool TextureUploader::TakeSubmitted(const TextureUploadRequestId request, SubmittedTextureCandidate& candidate, TextureUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TextureUploadFailureCode::NotInitialized, "texture uploader is not initialized", request);
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureUploadFailureCode::WrongThread, "submitted texture candidate ownership must be taken on the main thread", request);
        UploadEntry* const entry = m_impl->Find(request);
        if (entry == nullptr || (entry->state != TextureUploadState::Submitted && entry->state != TextureUploadState::ReadyToInstall) || entry->discardOnCompletion ||
            candidate.texture.IsValid())
            return Fail(failure, TextureUploadFailureCode::InvalidArgument, "texture upload request is not submitted or output already owns a candidate", request);
        candidate = std::move(entry->submitted);
        m_impl->Remove(request.index);
        return true;
    }

    TextureUploaderStats TextureUploader::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        TextureUploaderStats stats = m_impl->stats;
        stats.acquiringRequests = 0;
        stats.submittedRequests = 0;
        stats.readyToInstallRequests = 0;
        stats.failedRequests = 0;
        for (const Impl::Slot& slot : m_impl->slots)
        {
            if (slot.entry == nullptr)
                continue;
            switch (slot.entry->state)
            {
            case TextureUploadState::Acquiring:
                ++stats.acquiringRequests;
                break;
            case TextureUploadState::Submitted:
                ++stats.submittedRequests;
                break;
            case TextureUploadState::ReadyToInstall:
                ++stats.readyToInstallRequests;
                break;
            case TextureUploadState::Failed:
                ++stats.failedRequests;
                break;
            default:
                break;
            }
        }
        stats.pendingCandidateGpuBytes = m_impl->pendingCandidateGpuBytes;
        stats.pendingCandidateBytes = m_impl->pendingCandidateBytes;
        return stats;
    }
} // namespace vanguard::rendering
