#include <vanguard/rendering/texture_residency_runtime.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 InvalidRuntimeRecordIndex = 0xffffffffu;
        inline constexpr u32 InvalidFirstResidentMip = 0xffffffffu;
        inline constexpr u32 MaximumTextureMipLevels = 32u;
        inline constexpr u32 TextureDemandPriorityCount = static_cast<u32>(TextureDemandPriority::Count);

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] constexpr bool IsValidDemandPriority(const TextureDemandPriority priority) noexcept
        {
            return static_cast<u32>(priority) < TextureDemandPriorityCount;
        }

        [[nodiscard]] constexpr io::AsyncPriority ToAsyncPriority(const TextureDemandPriority priority) noexcept
        {
            switch (priority)
            {
            case TextureDemandPriority::Background:
                return io::eAsyncPriority_Background;
            case TextureDemandPriority::Interactive:
            case TextureDemandPriority::Focused:
                return io::eAsyncPriority_UI;
            case TextureDemandPriority::Streaming:
            default:
                return io::eAsyncPriority_Streaming;
            }
        }

        [[nodiscard]] u32 ClampRequestedFirstMip(const textures::TextureFile& metadata, const u32 requestedFirstResidentMip) noexcept
        {
            const u32 mipCount = metadata.GetMipCount();
            VG_ASSERT_MSG(mipCount != 0 && mipCount <= MaximumTextureMipLevels, "runtime texture mip count exceeds bounded demand aggregation storage");
            const u32 tailFirstMip = metadata.GetMipTailFirstLevel();
            return requestedFirstResidentMip == InvalidFirstResidentMip || requestedFirstResidentMip > tailFirstMip ? tailFirstMip : requestedFirstResidentMip;
        }

        [[nodiscard]] constexpr u32 NormalizeDemandFirstMip(const u32 requestedFirstResidentMip) noexcept
        {
            return requestedFirstResidentMip == InvalidFirstResidentMip || requestedFirstResidentMip >= MaximumTextureMipLevels ? InvalidFirstResidentMip : requestedFirstResidentMip;
        }

        [[nodiscard]] bool IsTransientTransitionAdmissionFailure(const TextureUploadFailure& failure) noexcept
        {
            if (failure.code == TextureUploadFailureCode::CapacityExceeded)
                return true;
            if (failure.code != TextureUploadFailureCode::ResidencyFailure)
                return false;
            return failure.residencyFailure.code == TextureResidencyFailureCode::Busy || failure.residencyFailure.code == TextureResidencyFailureCode::CapacityExceeded ||
                   failure.residencyFailure.code == TextureResidencyFailureCode::PendingRetirementCapacityExceeded;
        }

        [[nodiscard]] bool EstimateCandidateGpuBytes(const textures::TextureFile& metadata, const u32 firstResidentMip, u64& gpuBytes,
                                                     TextureUploadFailure& failure) noexcept
        {
            gpuBytes = 0;
            failure = {};
            const u32 mipCount = metadata.GetMipCount();
            if (mipCount == 0 || firstResidentMip >= mipCount)
            {
                failure.code = TextureUploadFailureCode::InvalidArgument;
                failure.message = "texture committed-byte estimate target mip is outside the asset";
                return false;
            }

            TextureUploadCandidatePlan plan;
            TextureUploadCandidateFailure candidateFailure;
            if (!plan.Initialize(metadata, firstResidentMip, mipCount - firstResidentMip, rhi::GetCapabilities(), &candidateFailure))
            {
                failure.code = TextureUploadFailureCode::CandidateFailure;
                failure.message = "texture committed-byte estimate could not form a physical candidate";
                failure.candidateFailure = candidateFailure;
                return false;
            }

            rhi::MemoryRequirements requirements;
            rhi::Failure rhiFailure;
            if (!rhi::GetMemoryRequirements(plan.GetTextureDesc(), requirements, &rhiFailure) || requirements.size == 0)
            {
                failure.code = TextureUploadFailureCode::RhiFailure;
                failure.message = "texture committed-byte estimate could not query physical memory requirements";
                failure.rhiFailure = rhiFailure;
                return false;
            }
            gpuBytes = requirements.size;
            return true;
        }

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

        void ClearFailure(TextureResidencyRuntimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TextureResidencyRuntimeFailure* const failure, const TextureResidencyRuntimeFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool FailResidency(TextureResidencyRuntimeFailure* const failure, const TextureResidencyFailure& residencyFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::ResidencyFailure;
                failure->message = message;
                failure->residencyFailure = residencyFailure;
            }
            return false;
        }

        [[nodiscard]] bool FailUploader(TextureResidencyRuntimeFailure* const failure, const TextureUploadFailure& uploadFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::UploaderFailure;
                failure->message = message;
                failure->uploadFailure = uploadFailure;
            }
            return false;
        }

        [[nodiscard]] bool FailGpuScene(TextureResidencyRuntimeFailure* const failure, const GpuSceneRuntimeFailure& gpuSceneFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::GpuSceneFailure;
                failure->message = message;
                failure->gpuSceneFailure = gpuSceneFailure;
            }
            return false;
        }

        [[nodiscard]] constexpr GpuSceneContributionToken EncodeTextureBatch(const TextureResidencyBatch batch) noexcept
        {
            return {static_cast<u64>(batch.serial) | (static_cast<u64>(batch.installationCount) << 32u), 0x5458545245534944ull};
        }

        [[nodiscard]] constexpr TextureResidencyBatch DecodeTextureBatch(const GpuSceneContributionToken token) noexcept
        {
            return {static_cast<u32>(token.value0), static_cast<u32>(token.value0 >> 32u)};
        }

        [[nodiscard]] bool WriteTextureContribution(void* const owner, const GpuSceneContributionToken token, const containers::ArraySpan<const GpuSceneUploadReservation> reservations,
                                                    const char*& failureMessage) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            TextureResidencyFailure failure;
            const bool result = runtime != nullptr && runtime->GetResidencyManager().WriteBatch(DecodeTextureBatch(token), reservations, &failure);
            failureMessage = result ? nullptr : failure.message;
            return result;
        }

        void AcceptTextureContribution(void* const owner, const GpuSceneContributionToken token, const rhi::GpuFence sharedCompletion) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            if (runtime != nullptr)
                runtime->GetResidencyManager().AcceptSubmittedBatch(DecodeTextureBatch(token), sharedCompletion);
        }

        [[nodiscard]] bool RetryTextureContribution(void* const owner, const GpuSceneContributionToken token, const char*& failureMessage) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            TextureResidencyFailure failure;
            const bool result = runtime != nullptr && runtime->GetResidencyManager().RetryBatch(DecodeTextureBatch(token), &failure);
            failureMessage = result ? nullptr : failure.message;
            return result;
        }
    } // namespace

    struct TextureResidencyRuntime::Impl
    {
        struct ResidencySlot
        {
            resources::ResourceHandle resource;
            resources::ResourceHandle replacementResource;
            GpuTextureResidencyHandle residency;
            TextureUploadRequestId uploadRequest;
            TextureUploadRequestId qualityUploadRequest;
            TextureUploadRequestId replacementUploadRequest;
            SubmittedTextureCandidate replacementCandidate;
            TextureUploadFailure uploadFailure;
            TextureUploadFailure qualityFailure;
            TextureUploadFailure replacementFailure;
            crypto::Digest256 replacementContentFingerprint;
            u32 demandMipCounts[MaximumTextureMipLevels]{};
            u32 tailDemandCount = 0;
            u32 demandPriorityCounts[TextureDemandPriorityCount]{};
            u32 generation = 1;
            u32 demandCount = 0;
            u32 mipCount = 0;
            u32 mipTailFirstLevel = 0;
            u32 desiredFirstResidentMip = InvalidFirstResidentMip;
            u32 initialTargetFirstResidentMip = InvalidFirstResidentMip;
            u32 currentFirstResidentMip = InvalidFirstResidentMip;
            u32 qualityTargetFirstResidentMip = InvalidFirstResidentMip;
            u32 replacementTargetFirstResidentMip = InvalidFirstResidentMip;
            u32 replacementMipCount = 0;
            u32 replacementMipTailFirstLevel = 0;
            u64 committedGpuBytes = 0;
            u64 initialTargetGpuBytes = 0;
            u64 qualityTargetGpuBytes = 0;
            u64 replacementTargetGpuBytes = 0;
            u64 qualityPromotionReservedGpuBytes = 0;
            u32 nextSamePath = InvalidRuntimeRecordIndex;
            u32 workIndex = InvalidRuntimeRecordIndex;
            u64 desiredChangedTick = 0;
            TextureRuntimeState state = TextureRuntimeState::Invalid;
            TextureDemandPriority aggregatePriority = TextureDemandPriority::Streaming;
            TextureQualityState qualityState = TextureQualityState::Stable;
            TextureReplacementState replacementState = TextureReplacementState::None;
            bool qualityRetryRequired = false;
            bool replacementRetryRequired = false;
            bool active = false;
        };

        struct DemandSlot
        {
            u32 residencyIndex = InvalidRuntimeRecordIndex;
            u32 residencyGeneration = 0;
            u32 requestedFirstResidentMip = InvalidFirstResidentMip;
            u32 generation = 1;
            TextureDemandPriority priority = TextureDemandPriority::Streaming;
            bool active = false;
        };

        struct WorkRef
        {
            u32 index = InvalidRuntimeRecordIndex;
            u32 generation = 0;
        };

        struct DeferredResidencyOwnership
        {
            resources::ResourceHandle resource;
            resources::ResourceHandle replacementResource;
            SubmittedTextureCandidate replacementCandidate;
        };

        explicit Impl(const TextureResidencyRuntimeConfig& runtimeConfig, const u32 runtimeSession) noexcept
            : residencies(memory::pools::Rendering::GetInstance()), recycledResidencies(memory::pools::Rendering::GetInstance()), residencyByPath(memory::pools::Rendering::GetInstance()),
              residencyByGpuIndex(memory::pools::Rendering::GetInstance()), demands(memory::pools::Rendering::GetInstance()), recycledDemands(memory::pools::Rendering::GetInstance()),
              activeWork(memory::pools::Rendering::GetInstance()), workScratch(memory::pools::Rendering::GetInstance()), config(runtimeConfig), session(runtimeSession)
        {
            residencies.Reserve(config.maximumResidencyRecords);
            recycledResidencies.Reserve(config.maximumResidencyRecords);
            residencyByPath.Reserve(config.maximumResidencyRecords);
            residencyByGpuIndex.Reserve(config.maximumResidencyRecords);
            demands.Reserve(config.maximumDemands);
            recycledDemands.Reserve(config.maximumDemands);
            activeWork.Reserve(config.maximumResidencyRecords);
            workScratch.Reserve(config.maximumStateChecksPerTick);
            contributionRequests.Resize(config.maximumTableInstallationsPerFrame);
        }

        [[nodiscard]] ResidencySlot* FindRecord(const u32 index, const u32 generation) noexcept
        {
            return index < residencies.Size() && residencies[index].active && residencies[index].generation == generation ? &residencies[index] : nullptr;
        }

        [[nodiscard]] const ResidencySlot* FindRecord(const u32 index, const u32 generation) const noexcept
        {
            return index < residencies.Size() && residencies[index].active && residencies[index].generation == generation ? &residencies[index] : nullptr;
        }

        [[nodiscard]] DemandSlot* FindDemand(const TextureDemandId demand) noexcept
        {
            return demand.IsValid() && demand.session == session && demand.index < demands.Size() && demands[demand.index].active && demands[demand.index].generation == demand.generation ? &demands[demand.index]
                                                                                                                                                                                 : nullptr;
        }

        [[nodiscard]] TextureDemandId DemandId(const u32 index) const noexcept
        {
            return {index, demands[index].generation, session};
        }

        [[nodiscard]] static u32 RecomputeDesiredFirstResidentMipForTail(const ResidencySlot& slot, const u32 mipTailFirstLevel) noexcept
        {
            for (u32 mip = 0; mip < MaximumTextureMipLevels; ++mip)
            {
                if (slot.demandMipCounts[mip] != 0)
                    return mip > mipTailFirstLevel ? mipTailFirstLevel : mip;
            }
            return mipTailFirstLevel;
        }

        [[nodiscard]] static u32 RecomputeDesiredFirstResidentMip(const ResidencySlot& slot) noexcept
        {
            return RecomputeDesiredFirstResidentMipForTail(slot, slot.mipTailFirstLevel);
        }

        [[nodiscard]] static TextureDemandPriority RecomputeAggregatePriority(const ResidencySlot& slot) noexcept
        {
            for (u32 priority = TextureDemandPriorityCount; priority > 0; --priority)
            {
                if (slot.demandPriorityCounts[priority - 1u] != 0)
                    return static_cast<TextureDemandPriority>(priority - 1u);
            }
            return TextureDemandPriority::Streaming;
        }

        void AddDemandBookkeeping(ResidencySlot& slot, const u32 requestedFirstResidentMip, const TextureDemandPriority priority) noexcept
        {
            VG_ASSERT_MSG((requestedFirstResidentMip == InvalidFirstResidentMip || requestedFirstResidentMip < MaximumTextureMipLevels) && IsValidDemandPriority(priority),
                          "texture demand bookkeeping input is invalid");
            if (requestedFirstResidentMip == InvalidFirstResidentMip)
                ++slot.tailDemandCount;
            else
                ++slot.demandMipCounts[requestedFirstResidentMip];
            ++slot.demandPriorityCounts[static_cast<u32>(priority)];
            slot.desiredFirstResidentMip = RecomputeDesiredFirstResidentMip(slot);
            slot.aggregatePriority = RecomputeAggregatePriority(slot);
        }

        void RemoveDemandBookkeeping(ResidencySlot& slot, const u32 requestedFirstResidentMip, const TextureDemandPriority priority) noexcept
        {
            VG_ASSERT_MSG((requestedFirstResidentMip == InvalidFirstResidentMip || requestedFirstResidentMip < MaximumTextureMipLevels) && IsValidDemandPriority(priority),
                          "texture demand bookkeeping input is invalid");
            VG_ASSERT_MSG((requestedFirstResidentMip == InvalidFirstResidentMip ? slot.tailDemandCount : slot.demandMipCounts[requestedFirstResidentMip]) != 0 &&
                              slot.demandPriorityCounts[static_cast<u32>(priority)] != 0,
                          "texture demand bookkeeping underflow");
            if (requestedFirstResidentMip == InvalidFirstResidentMip)
                --slot.tailDemandCount;
            else
                --slot.demandMipCounts[requestedFirstResidentMip];
            --slot.demandPriorityCounts[static_cast<u32>(priority)];
            slot.desiredFirstResidentMip = RecomputeDesiredFirstResidentMip(slot);
            slot.aggregatePriority = RecomputeAggregatePriority(slot);
        }

        void SetCommittedGpuBytes(ResidencySlot& slot, const u64 gpuBytes) noexcept
        {
            VG_ASSERT_MSG(committedTextureGpuBytes >= slot.committedGpuBytes, "texture committed-byte accounting underflow");
            committedTextureGpuBytes -= slot.committedGpuBytes;
            committedTextureGpuBytes += gpuBytes;
            slot.committedGpuBytes = gpuBytes;
            if (committedTextureGpuBytes > peakCommittedTextureGpuBytes)
                peakCommittedTextureGpuBytes = committedTextureGpuBytes;
        }

        void ReleaseQualityPromotionReservation(ResidencySlot& slot) noexcept
        {
            if (slot.qualityPromotionReservedGpuBytes == 0)
                return;
            VG_ASSERT_MSG(reservedQualityPromotionGpuBytes >= slot.qualityPromotionReservedGpuBytes,
                          "texture quality promotion reservation accounting underflow");
            reservedQualityPromotionGpuBytes -= slot.qualityPromotionReservedGpuBytes;
            slot.qualityPromotionReservedGpuBytes = 0;
        }

        [[nodiscard]] bool TryReserveQualityPromotion(ResidencySlot& slot, const u64 targetGpuBytes) noexcept
        {
            VG_ASSERT_MSG(slot.qualityPromotionReservedGpuBytes == 0, "one texture residency may own at most one quality promotion reservation");
            if (targetGpuBytes <= slot.committedGpuBytes)
                return true;
            const u64 delta = targetGpuBytes - slot.committedGpuBytes;
            const u64 limit = config.maximumCommittedTextureGpuBytes;
            if (committedTextureGpuBytes > limit || reservedQualityPromotionGpuBytes > limit - committedTextureGpuBytes)
                return false;
            const u64 available = limit - committedTextureGpuBytes - reservedQualityPromotionGpuBytes;
            if (delta > available)
                return false;
            reservedQualityPromotionGpuBytes += delta;
            slot.qualityPromotionReservedGpuBytes = delta;
            return true;
        }

        [[nodiscard]] bool CanRetainReplacementCandidate(const SubmittedTextureCandidate& candidate) const noexcept
        {
            return candidate.IsValid() && candidate.candidateGpuBytes <= config.maximumRetainedReplacementCandidateGpuBytes &&
                   retainedReplacementCandidateGpuBytes <= config.maximumRetainedReplacementCandidateGpuBytes - candidate.candidateGpuBytes;
        }

        static void TransferResourceOwnershipToEmpty(resources::ResourceHandle& destination, resources::ResourceHandle& source) noexcept
        {
            // ResourceHandle move-assignment only reaches ResourceRegistry when it must release the destination.
            // Runtime lock-held transfers therefore require a structurally empty destination.
            VG_ASSERT_MSG(destination.GetGeneration() == 0, "texture runtime ownership transfer destination must be empty");
            destination = static_cast<resources::ResourceHandle&&>(source);
        }

        static void TransferCandidateOwnershipToEmpty(SubmittedTextureCandidate& destination, SubmittedTextureCandidate& source) noexcept
        {
            // SubmittedTextureCandidate's default move-assignment delegates to WeakResourceHandle/rhi::Ref move
            // assignment. With an empty destination those paths only steal the references and cannot release into
            // ResourceRegistry or the RHI while the runtime spinlock is held.
            VG_ASSERT_MSG(destination.resource.GetGeneration() == 0 && !destination.texture.IsValid() && destination.candidateGpuBytes == 0,
                          "texture runtime candidate ownership transfer destination must be empty");
            destination = static_cast<SubmittedTextureCandidate&&>(source);
            // The default move clears owning handles but copies scalar/aggregate fields. Normalize the moved-from
            // source to the runtime's structural-empty invariant without touching ResourceRegistry or the RHI.
            source.contentFingerprint = {};
            source.residency = {};
            source.copyCompletion = {};
            source.firstResidentMip = 0;
            source.residentMipCount = 0;
            source.subresourceCount = 0;
            source.sourceBytesUploaded = 0;
            source.gpuBytesCopied = 0;
            source.candidateGpuBytes = 0;
        }

        void DetachReplacementCandidate(ResidencySlot& slot, SubmittedTextureCandidate& detached) noexcept
        {
            if (!slot.replacementCandidate.IsValid())
                return;
            VG_ASSERT_MSG(retainedReplacementCandidateGpuBytes >= slot.replacementCandidate.candidateGpuBytes,
                          "retained texture replacement candidate accounting underflow");
            retainedReplacementCandidateGpuBytes -= slot.replacementCandidate.candidateGpuBytes;
            TransferCandidateOwnershipToEmpty(detached, slot.replacementCandidate);
        }

        [[nodiscard]] bool RetainReplacementCandidate(ResidencySlot& slot, SubmittedTextureCandidate&& candidate) noexcept
        {
            VG_ASSERT_MSG(!slot.replacementCandidate.IsValid(), "texture replacement slot already retains a physical candidate");
            if (!CanRetainReplacementCandidate(candidate))
                return false;
            retainedReplacementCandidateGpuBytes += candidate.candidateGpuBytes;
            slot.replacementTargetGpuBytes = candidate.candidateGpuBytes;
            TransferCandidateOwnershipToEmpty(slot.replacementCandidate, candidate);
            return true;
        }

        u32 AllocateResidency() noexcept
        {
            if (!recycledResidencies.Empty())
            {
                const u32 index = recycledResidencies.Back();
                recycledResidencies.PopBack();
                return index;
            }
            residencies.PushBack({});
            return residencies.Size() - 1u;
        }

        u32 AllocateDemand() noexcept
        {
            if (!recycledDemands.Empty())
            {
                const u32 index = recycledDemands.Back();
                recycledDemands.PopBack();
                return index;
            }
            demands.PushBack({});
            return demands.Size() - 1u;
        }

        void AddWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.workIndex != InvalidRuntimeRecordIndex)
                return;
            slot.workIndex = activeWork.Size();
            activeWork.PushBack(index);
        }

        void RemoveWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.workIndex == InvalidRuntimeRecordIndex)
                return;
            const u32 workIndex = slot.workIndex;
            const u32 moved = activeWork.Back();
            activeWork[workIndex] = moved;
            activeWork.PopBack();
            slot.workIndex = InvalidRuntimeRecordIndex;
            if (workIndex < activeWork.Size())
                residencies[moved].workIndex = workIndex;
            if (nextWork >= activeWork.Size())
                nextWork = 0;
        }

        void RemoveWorkUnlessCleanupRequired(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.demandCount == 0)
            {
                AddWork(index);
                return;
            }
            RemoveWork(index);
        }

        void BuildWorkScratch(const u32 maximumChecks) noexcept
        {
            workScratch.Clear();
            if (activeWork.Empty() || maximumChecks == 0)
            {
                if (activeWork.Empty())
                    nextWork = 0;
                return;
            }
            const u32 count = activeWork.Size() < maximumChecks ? activeWork.Size() : maximumChecks;
            const u64 committedLimit = config.maximumCommittedTextureGpuBytes;
            const bool underBudgetPressure = committedTextureGpuBytes >= committedLimit ||
                                             reservedQualityPromotionGpuBytes >= committedLimit - committedTextureGpuBytes;
            const auto IsEligibleDemotion = [this](const ResidencySlot& slot) noexcept
            {
                return slot.active && slot.demandCount != 0 && slot.state == TextureRuntimeState::BindlessReady &&
                       slot.replacementState == TextureReplacementState::None && slot.qualityState == TextureQualityState::Stable &&
                       slot.currentFirstResidentMip != InvalidFirstResidentMip && slot.desiredFirstResidentMip > slot.currentFirstResidentMip &&
                       tickSerial - slot.desiredChangedTick >= config.mipDemotionHysteresisTicks;
            };

            // Under committed-memory pressure, release eligible quality bytes before admitting refinements from
            // the same bounded round-robin window. Priority still orders peers within each class.
            if (underBudgetPressure)
            {
                for (u32 priority = TextureDemandPriorityCount; priority > 0; --priority)
                {
                    for (u32 offset = 0; offset < count; ++offset)
                    {
                        const u32 workIndex = (nextWork + offset) % activeWork.Size();
                        const u32 recordIndex = activeWork[workIndex];
                        const ResidencySlot& slot = residencies[recordIndex];
                        if (slot.aggregatePriority == static_cast<TextureDemandPriority>(priority - 1u) && IsEligibleDemotion(slot))
                            workScratch.PushBack({recordIndex, slot.generation});
                    }
                }
            }
            for (u32 priority = TextureDemandPriorityCount; priority > 0; --priority)
            {
                for (u32 offset = 0; offset < count; ++offset)
                {
                    const u32 workIndex = (nextWork + offset) % activeWork.Size();
                    const u32 recordIndex = activeWork[workIndex];
                    const ResidencySlot& slot = residencies[recordIndex];
                    if (slot.aggregatePriority == static_cast<TextureDemandPriority>(priority - 1u) && (!underBudgetPressure || !IsEligibleDemotion(slot)))
                        workScratch.PushBack({recordIndex, slot.generation});
                }
            }
            nextWork = (nextWork + count) % activeWork.Size();
        }

        void RemoveResidency(const u32 index, DeferredResidencyOwnership& deferred) noexcept
        {
            ResidencySlot& slot = residencies[index];
            VG_ASSERT_MSG(slot.active && slot.demandCount == 0, "only an unreferenced texture residency record may be recycled");
            RemoveWork(index);
            ReleaseQualityPromotionReservation(slot);
            const resources::ResourceId path = slot.resource.GetPath().Id();
            u32 head = InvalidRuntimeRecordIndex;
            const bool found = residencyByPath.Find(path, head);
            VG_ASSERT_MSG(found, "active texture residency record must be reachable from its resource-path lookup");
            if (head == index)
            {
                if (slot.nextSamePath == InvalidRuntimeRecordIndex)
                    static_cast<void>(residencyByPath.Remove(path));
                else
                    static_cast<void>(residencyByPath.Set(path, slot.nextSamePath));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidRuntimeRecordIndex && residencies[previous].nextSamePath != index)
                    previous = residencies[previous].nextSamePath;
                VG_ASSERT_MSG(previous != InvalidRuntimeRecordIndex, "active texture residency record must be linked from its path head");
                if (previous != InvalidRuntimeRecordIndex)
                    residencies[previous].nextSamePath = slot.nextSamePath;
            }
            static_cast<void>(residencyByGpuIndex.Remove(slot.residency.index));
            SetCommittedGpuBytes(slot, 0);
            TransferResourceOwnershipToEmpty(deferred.resource, slot.resource);
            TransferResourceOwnershipToEmpty(deferred.replacementResource, slot.replacementResource);
            DetachReplacementCandidate(slot, deferred.replacementCandidate);
            slot.residency = {};
            slot.uploadRequest = {};
            slot.qualityUploadRequest = {};
            slot.replacementUploadRequest = {};
            slot.uploadFailure = {};
            slot.qualityFailure = {};
            slot.replacementFailure = {};
            slot.replacementContentFingerprint = {};
            for (u32& count : slot.demandMipCounts)
                count = 0;
            slot.tailDemandCount = 0;
            for (u32& count : slot.demandPriorityCounts)
                count = 0;
            slot.generation = NextGeneration(slot.generation);
            slot.mipCount = 0;
            slot.mipTailFirstLevel = 0;
            slot.desiredFirstResidentMip = InvalidFirstResidentMip;
            slot.initialTargetFirstResidentMip = InvalidFirstResidentMip;
            slot.currentFirstResidentMip = InvalidFirstResidentMip;
            slot.qualityTargetFirstResidentMip = InvalidFirstResidentMip;
            slot.replacementTargetFirstResidentMip = InvalidFirstResidentMip;
            slot.replacementMipCount = 0;
            slot.replacementMipTailFirstLevel = 0;
            slot.initialTargetGpuBytes = 0;
            slot.qualityTargetGpuBytes = 0;
            slot.replacementTargetGpuBytes = 0;
            slot.qualityPromotionReservedGpuBytes = 0;
            slot.nextSamePath = InvalidRuntimeRecordIndex;
            slot.desiredChangedTick = 0;
            slot.state = TextureRuntimeState::Invalid;
            slot.aggregatePriority = TextureDemandPriority::Streaming;
            slot.qualityState = TextureQualityState::Stable;
            slot.replacementState = TextureReplacementState::None;
            slot.qualityRetryRequired = false;
            slot.replacementRetryRequired = false;
            slot.active = false;
            recycledResidencies.PushBack(index);
            --residencyCount;
        }

        mutable concurrency::SpinLock lock;
        containers::DynamicArray<ResidencySlot> residencies;
        containers::DynamicArray<u32> recycledResidencies;
        containers::HashMap<resources::ResourceId, u32> residencyByPath;
        containers::HashMap<u32, u32> residencyByGpuIndex;
        containers::DynamicArray<DemandSlot> demands;
        containers::DynamicArray<u32> recycledDemands;
        containers::DynamicArray<u32> activeWork;
        containers::DynamicArray<WorkRef> workScratch;
        containers::DynamicArray<GpuSceneUploadRequest> contributionRequests{memory::pools::Rendering::GetInstance()};
        TextureResidencyRuntimeConfig config;
        u32 session = 0;
        u32 nextWork = 0;
        u64 tickSerial = 0;
        u32 residencyCount = 0;
        u32 demandCount = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
        u64 failedResidencies = 0;
        u64 qualityTransitionsStarted = 0;
        u64 qualityTransitionFailures = 0;
        u64 qualityTransitionAdmissionDeferrals = 0;
        u64 qualityPromotionBudgetDeferrals = 0;
        u64 committedTextureGpuBytes = 0;
        u64 reservedQualityPromotionGpuBytes = 0;
        u64 peakCommittedTextureGpuBytes = 0;
        u64 retainedReplacementCandidateGpuBytes = 0;
    };

    TextureDemandHandle::TextureDemandHandle(TextureResidencyRuntime& runtime, const TextureDemandId demand, const GpuTextureResidencyHandle residency) noexcept
        : m_runtime(&runtime), m_demand(demand), m_residency(residency)
    {
    }

    TextureDemandHandle::~TextureDemandHandle()
    {
        Reset();
    }

    TextureDemandHandle::TextureDemandHandle(TextureDemandHandle&& other) noexcept : m_runtime(other.m_runtime), m_demand(other.m_demand), m_residency(other.m_residency)
    {
        other.m_runtime = nullptr;
        other.m_demand = {};
        other.m_residency = {};
    }

    TextureDemandHandle& TextureDemandHandle::operator=(TextureDemandHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_runtime = other.m_runtime;
            m_demand = other.m_demand;
            m_residency = other.m_residency;
            other.m_runtime = nullptr;
            other.m_demand = {};
            other.m_residency = {};
        }
        return *this;
    }

    bool TextureDemandHandle::IsValid() const noexcept
    {
        return m_runtime != nullptr && m_runtime->IsDemandValid(m_demand);
    }

    GpuTextureResidencyHandle TextureDemandHandle::GetResidency() const noexcept
    {
        return IsValid() ? m_residency : GpuTextureResidencyHandle{};
    }

    void TextureDemandHandle::Reset() noexcept
    {
        if (m_runtime != nullptr && m_demand.IsValid())
            m_runtime->ReleaseDemand(m_demand);
        m_runtime = nullptr;
        m_demand = {};
        m_residency = {};
    }

    TextureResidencyRuntime::~TextureResidencyRuntime()
    {
        VG_ASSERT_MSG(m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized(), "texture residency runtime must be shut down before destruction");
    }

    bool TextureResidencyRuntime::Initialize(GpuSceneLifetime& lifetime, const rhi::DescriptorDomainRef resourceDescriptors, const TextureResidencyRuntimeConfig& config,
                                             TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr || m_residency.IsInitialized() || m_uploader.IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::AlreadyInitialized, "texture residency runtime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime must initialize on the main thread");
        if (!lifetime.IsInitialized() || !resourceDescriptors.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidDependency, "texture residency runtime requires GPU Scene lifetime and the global resource descriptor domain");
        if (config.maximumResidencyRecords == 0 || config.maximumDemands == 0 || config.maximumInstallationsPerTick == 0 || config.maximumTableInstallationsPerFrame == 0 ||
            config.maximumStateChecksPerTick == 0 || config.maximumMipTransitionsPerTick == 0 || config.maximumCommittedTextureGpuBytes == 0 || config.maximumRetainedReplacementCandidateGpuBytes == 0 ||
            config.maximumResidencyRecords > config.residency.maximumTextures)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidConfiguration, "texture residency runtime configuration is invalid");

        TextureResidencyFailure residencyFailure;
        if (!m_residency.Initialize(lifetime, resourceDescriptors, config.residency, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency manager initialization failed");
        TextureUploadFailure uploadFailure;
        if (!m_uploader.Initialize(config.uploader, &uploadFailure))
        {
            static_cast<void>(m_residency.Shutdown());
            return FailUploader(failure, uploadFailure, "texture uploader initialization failed");
        }
        const u32 session = NextGeneration(m_session);
        m_impl = AllocateObject<Impl>(config, session);
        if (m_impl == nullptr)
        {
            static_cast<void>(m_uploader.Shutdown());
            static_cast<void>(m_residency.Shutdown());
            return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture residency runtime storage allocation failed");
        }
        m_session = session;
        return true;
    }

    bool TextureResidencyRuntime::Shutdown(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime must shut down on the main thread");
        if (m_impl != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->demandCount != 0 || m_impl->residencyCount != 0)
                return Fail(failure, TextureResidencyRuntimeFailureCode::LiveResidencyRemains, "texture residency runtime still owns live demands or residency records");
        }
        TextureUploadFailure uploadFailure;
        if (m_uploader.IsInitialized() && !m_uploader.Shutdown(&uploadFailure))
            return FailUploader(failure, uploadFailure, "texture uploader shutdown failed");
        TextureResidencyFailure residencyFailure;
        if (m_residency.IsInitialized() && !m_residency.Shutdown(&residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency manager shutdown failed");
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureResidencyRuntime::AbandonDevice(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency abandonment must run on the main thread");
        m_uploader.AbandonDevice();
        m_residency.AbandonDevice();
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureResidencyRuntime::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_residency.IsInitialized() && m_uploader.IsInitialized();
    }

    bool TextureResidencyRuntime::RequestTexture(const resources::ResourceHandle& resource, TextureDemandHandle& demand, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        return RequestTexture(resource, TextureDemandDesc{}, demand, failure);
    }

    bool TextureResidencyRuntime::RequestTexture(const resources::ResourceHandle& resource, const TextureDemandDesc& desc, TextureDemandHandle& demand,
                                                 TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency demand must be issued on the main thread");
        // RequestTexture publishes by move-assigning the result while holding the runtime lock. Require the caller's
        // output to be structurally empty up front: a stale handle from an abandoned/reinitialized session is not
        // IsValid(), but resetting it during publication would recursively enter ReleaseDemand and deadlock here.
        const bool demandOutputOccupied = demand.m_runtime != nullptr || demand.m_demand.IsValid() || demand.m_residency.IsValid();
        if (demandOutputOccupied || !resource.IsValid() || resource.GetType() != textures::TextureResourceType || !IsValidDemandPriority(desc.priority))
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand requires a live texture resource and empty output handle");
        auto* const texture = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen() || texture->GetMetadata().GetMipCount() == 0)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand references closed or empty texture metadata");
        const textures::TextureFile& metadata = texture->GetMetadata();
        const u32 requestedFirstResidentMip = NormalizeDemandFirstMip(desc.firstResidentMip);
        const u32 initialFirstResidentMip = ClampRequestedFirstMip(metadata, requestedFirstResidentMip);

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            u32 candidate = InvalidRuntimeRecordIndex;
            if (m_impl->residencyByPath.Find(resource.GetPath().Id(), candidate))
            {
                while (candidate != InvalidRuntimeRecordIndex)
                {
                    Impl::ResidencySlot& slot = m_impl->residencies[candidate];
                    if (slot.active && slot.resource.GetGeneration() == resource.GetGeneration() && slot.state != TextureRuntimeState::Failed && slot.state != TextureRuntimeState::Cancelling &&
                        slot.state != TextureRuntimeState::Retiring)
                    {
                        if (m_impl->demandCount >= m_impl->config.maximumDemands || slot.demandCount == 0xffffffffu)
                            return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture demand capacity is exhausted");
                        const u32 demandIndex = m_impl->AllocateDemand();
                        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
                        demandSlot.residencyIndex = candidate;
                        demandSlot.residencyGeneration = slot.generation;
                        demandSlot.requestedFirstResidentMip = requestedFirstResidentMip;
                        demandSlot.priority = desc.priority;
                        demandSlot.active = true;
                        const u32 previousDesired = slot.desiredFirstResidentMip;
                        ++slot.demandCount;
                        m_impl->AddDemandBookkeeping(slot, requestedFirstResidentMip, desc.priority);
                        ++m_impl->demandCount;
                        ++m_impl->demandsIssued;
                        ++m_impl->demandsCoalesced;
                        if (slot.desiredFirstResidentMip != previousDesired)
                        {
                            slot.desiredChangedTick = m_impl->tickSerial;
                            if (slot.qualityState == TextureQualityState::Failed)
                            {
                                slot.qualityState = TextureQualityState::Stable;
                                slot.qualityFailure = {};
                                slot.qualityRetryRequired = false;
                            }
                            m_impl->AddWork(candidate);
                        }
                        demand = TextureDemandHandle(*this, m_impl->DemandId(demandIndex), slot.residency);
                        return true;
                    }
                    candidate = slot.nextSamePath;
                }
            }
            if (m_impl->residencyCount >= m_impl->config.maximumResidencyRecords || m_impl->demandCount >= m_impl->config.maximumDemands)
                return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture residency or demand capacity is exhausted");
        }

        u64 initialTargetGpuBytes = 0;
        TextureUploadFailure estimateFailure;
        if (!EstimateCandidateGpuBytes(metadata, initialFirstResidentMip, initialTargetGpuBytes, estimateFailure))
            return FailUploader(failure, estimateFailure, "texture initial residency memory preflight failed");

        GpuTextureResidencyHandle residency;
        TextureResidencyFailure residencyFailure;
        if (!m_residency.Allocate(residency, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency identity allocation failed");
        TextureUploadRequestId uploadRequest;
        TextureUploadFailure uploadFailure;
        if (!m_uploader.RequestMipTransition(resource, m_residency, residency, initialFirstResidentMip, uploadRequest, ToAsyncPriority(desc.priority), &uploadFailure))
        {
            static_cast<void>(m_residency.Retire(residency));
            return FailUploader(failure, uploadFailure, "texture initial residency request failed");
        }

        // Duplicate the registry owner before taking the runtime lock. Publication below only transfers an
        // already-owned handle into an empty slot and therefore cannot enter ResourceRegistry.
        resources::ResourceHandle retainedResource(resource);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const u32 residencyIndex = m_impl->AllocateResidency();
        Impl::ResidencySlot& slot = m_impl->residencies[residencyIndex];
        Impl::TransferResourceOwnershipToEmpty(slot.resource, retainedResource);
        slot.residency = residency;
        slot.uploadRequest = uploadRequest;
        slot.mipCount = metadata.GetMipCount();
        slot.mipTailFirstLevel = metadata.GetMipTailFirstLevel();
        slot.initialTargetFirstResidentMip = initialFirstResidentMip;
        slot.initialTargetGpuBytes = initialTargetGpuBytes;
        slot.desiredFirstResidentMip = slot.mipTailFirstLevel;
        slot.desiredChangedTick = m_impl->tickSerial;
        slot.demandCount = 1;
        m_impl->AddDemandBookkeeping(slot, requestedFirstResidentMip, desc.priority);
        u32 previousHead = InvalidRuntimeRecordIndex;
        static_cast<void>(m_impl->residencyByPath.Find(resource.GetPath().Id(), previousHead));
        slot.nextSamePath = previousHead;
        slot.state = TextureRuntimeState::MipTailLoading;
        slot.active = true;
        const bool pathIndexed = previousHead == InvalidRuntimeRecordIndex ? m_impl->residencyByPath.Insert(resource.GetPath().Id(), residencyIndex).IsSuccessful()
                                                                           : m_impl->residencyByPath.Set(resource.GetPath().Id(), residencyIndex).IsSuccessful();
        const bool gpuIndexed = m_impl->residencyByGpuIndex.Insert(residency.index, residencyIndex).IsSuccessful();
        VG_ASSERT_MSG(pathIndexed && gpuIndexed, "reserved texture runtime lookup capacity must accept a fresh record");
        m_impl->AddWork(residencyIndex);
        const u32 demandIndex = m_impl->AllocateDemand();
        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
        demandSlot.residencyIndex = residencyIndex;
        demandSlot.residencyGeneration = slot.generation;
        demandSlot.requestedFirstResidentMip = requestedFirstResidentMip;
        demandSlot.priority = desc.priority;
        demandSlot.active = true;
        ++m_impl->residencyCount;
        ++m_impl->demandCount;
        ++m_impl->demandsIssued;
        demand = TextureDemandHandle(*this, m_impl->DemandId(demandIndex), residency);
        return true;
    }

    bool TextureResidencyRuntime::UpdateDemand(TextureDemandHandle& demand, const TextureDemandDesc& desc, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency demand updates must run on the main thread");
        if (demand.m_runtime != this || !demand.m_demand.IsValid() || !IsValidDemandPriority(desc.priority))
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand update requires a live demand owned by this runtime");

        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::DemandSlot* const demandSlot = m_impl->FindDemand(demand.m_demand);
        if (demandSlot == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::StaleDemand, "texture demand handle is stale");
        Impl::ResidencySlot* const slot = m_impl->FindRecord(demandSlot->residencyIndex, demandSlot->residencyGeneration);
        if (slot == nullptr || slot->demandCount == 0)
            return Fail(failure, TextureResidencyRuntimeFailureCode::StaleDemand, "texture demand references a stale residency record");

        const u32 requestedFirstResidentMip = NormalizeDemandFirstMip(desc.firstResidentMip);
        const bool unchanged = demandSlot->requestedFirstResidentMip == requestedFirstResidentMip && demandSlot->priority == desc.priority;
        if (unchanged)
        {
            if (slot->qualityState == TextureQualityState::Failed)
            {
                slot->qualityState = TextureQualityState::Stable;
                slot->qualityFailure = {};
                slot->qualityRetryRequired = false;
                slot->desiredChangedTick = m_impl->tickSerial;
                m_impl->AddWork(demandSlot->residencyIndex);
            }
            return true;
        }

        const u32 previousDesired = slot->desiredFirstResidentMip;
        m_impl->RemoveDemandBookkeeping(*slot, demandSlot->requestedFirstResidentMip, demandSlot->priority);
        demandSlot->requestedFirstResidentMip = requestedFirstResidentMip;
        demandSlot->priority = desc.priority;
        m_impl->AddDemandBookkeeping(*slot, requestedFirstResidentMip, desc.priority);
        if (slot->desiredFirstResidentMip != previousDesired)
        {
            slot->desiredChangedTick = m_impl->tickSerial;
            if (slot->qualityState == TextureQualityState::Failed)
            {
                slot->qualityState = TextureQualityState::Stable;
                slot->qualityFailure = {};
                slot->qualityRetryRequired = false;
            }
            m_impl->AddWork(demandSlot->residencyIndex);
        }
        return true;
    }

    bool TextureResidencyRuntime::SetDemandFirstResidentMip(TextureDemandHandle& demand, const u32 firstResidentMip,
                                                           TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency demand updates must run on the main thread");
        if (demand.m_runtime != this || !demand.m_demand.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand update requires a live demand owned by this runtime");

        TextureDemandPriority priority = TextureDemandPriority::Streaming;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            Impl::DemandSlot* const demandSlot = m_impl->FindDemand(demand.m_demand);
            if (demandSlot == nullptr)
                return Fail(failure, TextureResidencyRuntimeFailureCode::StaleDemand, "texture demand handle is stale");
            priority = demandSlot->priority;
        }
        return UpdateDemand(demand, TextureDemandDesc{firstResidentMip, priority}, failure);
    }

    bool TextureResidencyRuntime::ReplaceTexture(const resources::ResourceHandle& resource, const GpuTextureResidencyHandle residency,
                                                 TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture generation replacement must be issued on the main thread");
        if (!resource.IsValid() || resource.GetType() != textures::TextureResourceType || !residency.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture replacement requires a live texture resource and residency identity");
        auto* const texture = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen() || texture->GetMetadata().GetMipCount() == 0)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture replacement references closed or empty texture metadata");
        const textures::TextureFile& metadata = texture->GetMetadata();
        const crypto::Digest256 contentFingerprint = metadata.GetContentFingerprint();

        u32 recordIndex = InvalidRuntimeRecordIndex;
        TextureUploadRequestId qualityRequest;
        TextureQualityState qualityState = TextureQualityState::Stable;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (!m_impl->residencyByGpuIndex.Find(residency.index, recordIndex) || recordIndex >= m_impl->residencies.Size())
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture replacement references an unknown residency identity");
            Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
            if (!slot.active || slot.residency != residency || slot.resource.GetPath() != resource.GetPath() ||
                slot.state != TextureRuntimeState::BindlessReady)
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture replacement requires the bindless-ready current residency for the same path");
            if (slot.resource.GetGeneration() == resource.GetGeneration())
                return true;
            if (slot.replacementState == TextureReplacementState::Uploading ||
                slot.replacementState == TextureReplacementState::InstallationPending)
            {
                if (slot.replacementResource.GetGeneration() == resource.GetGeneration())
                    return true;
                return Fail(failure, TextureResidencyRuntimeFailureCode::ReplacementBusy, "texture residency already has a different replacement in flight");
            }
            if (slot.replacementState == TextureReplacementState::Failed &&
                slot.replacementResource.GetGeneration() == resource.GetGeneration())
                return Fail(failure, TextureResidencyRuntimeFailureCode::ReplacementRetryRequired,
                            "texture replacement failed and requires explicit retry");

            qualityState = slot.qualityState;
            qualityRequest = slot.qualityUploadRequest;
        }

        if (qualityState == TextureQualityState::Cancelling || qualityState == TextureQualityState::InstallationPending)
            return Fail(failure, TextureResidencyRuntimeFailureCode::ReplacementBusy, "texture replacement is waiting for the active mip transition to release residency ownership");
        if (qualityState == TextureQualityState::Uploading)
        {
            const TextureUploadState uploadState = m_uploader.GetState(qualityRequest);
            if (uploadState == TextureUploadState::Invalid)
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = recordIndex < m_impl->residencies.Size() ? &m_impl->residencies[recordIndex] : nullptr;
                if (slot != nullptr && slot->active && slot->residency == residency && slot->qualityUploadRequest == qualityRequest)
                {
                    slot->qualityUploadRequest = {};
                    slot->qualityState = TextureQualityState::InstallationPending;
                }
                return Fail(failure, TextureResidencyRuntimeFailureCode::ReplacementBusy, "texture replacement is waiting for the admitted mip transition to publish");
            }

            TextureUploadFailure cancelFailure;
            if (!m_uploader.Cancel(qualityRequest, &cancelFailure))
                return FailUploader(failure, cancelFailure, "texture mip transition cancellation before generation replacement failed");
            const bool cancellationComplete = m_uploader.GetState(qualityRequest) == TextureUploadState::Invalid;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = recordIndex < m_impl->residencies.Size() ? &m_impl->residencies[recordIndex] : nullptr;
                if (slot == nullptr || !slot->active || slot->residency != residency)
                    return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency changed while cancelling quality work for replacement");
                if (slot->qualityUploadRequest == qualityRequest)
                {
                    m_impl->ReleaseQualityPromotionReservation(*slot);
                    slot->qualityUploadRequest = cancellationComplete ? TextureUploadRequestId{} : qualityRequest;
                    slot->qualityState = cancellationComplete ? TextureQualityState::Stable : TextureQualityState::Cancelling;
                    if (cancellationComplete)
                    {
                        slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                        slot->qualityTargetGpuBytes = 0;
                        slot->qualityFailure = {};
                        slot->qualityRetryRequired = false;
                    }
                }
            }
            if (!cancellationComplete)
                return Fail(failure, TextureResidencyRuntimeFailureCode::ReplacementBusy, "texture replacement is waiting for submitted mip work to be discarded");
        }

        u32 replacementTargetFirstResidentMip = metadata.GetMipTailFirstLevel();
        TextureDemandPriority priority = TextureDemandPriority::Streaming;
        {
            resources::ResourceHandle supersededReplacement;
            SubmittedTextureCandidate supersededCandidate;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = recordIndex < m_impl->residencies.Size() ? &m_impl->residencies[recordIndex] : nullptr;
                if (slot == nullptr || !slot->active || slot->residency != residency || slot->resource.GetPath() != resource.GetPath() || slot->state != TextureRuntimeState::BindlessReady)
                    return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency changed before replacement admission");

                // A genuinely newer requested generation supersedes a failed retained candidate. Ownership is
                // detached while locked, but its ResourceRegistry/RHI release happens after the guard leaves scope.
                m_impl->DetachReplacementCandidate(*slot, supersededCandidate);
                Impl::TransferResourceOwnershipToEmpty(supersededReplacement, slot->replacementResource);
                slot->replacementUploadRequest = {};
                slot->replacementFailure = {};
                slot->replacementContentFingerprint = {};
                slot->replacementTargetFirstResidentMip = InvalidFirstResidentMip;
                slot->replacementMipCount = 0;
                slot->replacementMipTailFirstLevel = 0;
                slot->replacementTargetGpuBytes = 0;
                slot->replacementState = TextureReplacementState::None;
                slot->replacementRetryRequired = false;
                replacementTargetFirstResidentMip = Impl::RecomputeDesiredFirstResidentMipForTail(*slot, metadata.GetMipTailFirstLevel());
                priority = slot->aggregatePriority;
            }
        }

        TextureUploadRequestId uploadRequest;
        TextureUploadFailure uploadFailure;
        u64 replacementTargetGpuBytes = 0;
        const bool preflighted = EstimateCandidateGpuBytes(metadata, replacementTargetFirstResidentMip, replacementTargetGpuBytes, uploadFailure);
        const bool requested = preflighted &&
                               m_uploader.RequestGenerationReplacement(resource, residency, replacementTargetFirstResidentMip, uploadRequest, ToAsyncPriority(priority), &uploadFailure);
        resources::ResourceHandle retainedReplacement(resource);
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            Impl::ResidencySlot* const slot = recordIndex < m_impl->residencies.Size() ? &m_impl->residencies[recordIndex] : nullptr;
            if (slot != nullptr && slot->active && slot->residency == residency && slot->resource.GetPath() == resource.GetPath())
            {
                Impl::TransferResourceOwnershipToEmpty(slot->replacementResource, retainedReplacement);
                slot->replacementContentFingerprint = contentFingerprint;
                slot->replacementTargetFirstResidentMip = replacementTargetFirstResidentMip;
                slot->replacementMipCount = metadata.GetMipCount();
                slot->replacementMipTailFirstLevel = metadata.GetMipTailFirstLevel();
                slot->replacementTargetGpuBytes = replacementTargetGpuBytes;
                slot->replacementUploadRequest = requested ? uploadRequest : TextureUploadRequestId{};
                slot->replacementFailure = uploadFailure;
                slot->replacementState = requested ? TextureReplacementState::Uploading : TextureReplacementState::Failed;
                slot->replacementRetryRequired = !requested;
                if (requested)
                    m_impl->AddWork(recordIndex);
                if (!requested)
                    return FailUploader(failure, uploadFailure, "texture generation replacement upload admission failed");
                return true;
            }
        }
        if (requested && uploadRequest.IsValid())
            static_cast<void>(m_uploader.Cancel(uploadRequest));
        return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency changed before replacement admission completed");
    }

    bool TextureResidencyRuntime::RetryTextureReplacement(const GpuTextureResidencyHandle residency,
                                                          TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture replacement retry must be issued on the main thread");

        u32 recordIndex = InvalidRuntimeRecordIndex;
        const resources::ResourceHandle* replacementOwner = nullptr;
        const SubmittedTextureCandidate* retainedCandidate = nullptr;
        bool hasCandidate = false;
        u32 replacementTargetFirstResidentMip = InvalidFirstResidentMip;
        TextureDemandPriority priority = TextureDemandPriority::Streaming;
        u64 replacementTargetGpuBytes = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (!residency.IsValid() || !m_impl->residencyByGpuIndex.Find(residency.index, recordIndex) || recordIndex >= m_impl->residencies.Size())
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture replacement retry references an unknown residency identity");
            Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
            if (!slot.active || slot.residency != residency || slot.replacementState != TextureReplacementState::Failed ||
                !slot.replacementRetryRequired || slot.replacementResource.GetGeneration() == 0)
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency has no failed replacement to retry");
            replacementOwner = &slot.replacementResource;
            hasCandidate = slot.replacementCandidate.IsValid();
            retainedCandidate = hasCandidate ? &slot.replacementCandidate : nullptr;
            replacementTargetFirstResidentMip = slot.replacementTargetFirstResidentMip;
            replacementTargetGpuBytes = slot.replacementTargetGpuBytes;
            priority = slot.aggregatePriority;
        }

        // The runtime record owns this handle for the whole main-thread retry. Duplicate it only after releasing
        // the runtime spinlock so ResourceRegistry::AddStrong never participates in lock ordering.
        resources::ResourceHandle replacement(*replacementOwner);
        if (hasCandidate)
        {
            if (retainedCandidate == nullptr || !retainedCandidate->IsValid())
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "retained texture replacement candidate changed before retry");
            TextureInstallationDesc installation;
            installation.texture = retainedCandidate->texture.GetRef();
            installation.firstResidentMip = retainedCandidate->firstResidentMip;
            installation.residentMipCount = retainedCandidate->residentMipCount;
            installation.source = retainedCandidate->resource;
            installation.contentFingerprint = retainedCandidate->contentFingerprint;
            TextureInstallationTicket ticket;
            TextureResidencyFailure residencyFailure;
            if (!m_residency.Install(residency, installation, ticket, &residencyFailure))
            {
                TextureUploadFailure retryFailure;
                retryFailure.code = TextureUploadFailureCode::ResidencyFailure;
                retryFailure.message = "retained texture replacement candidate could not re-enter residency installation";
                retryFailure.residencyFailure = residencyFailure;
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
                slot.replacementFailure = retryFailure;
                if (failure != nullptr)
                    failure->uploadFailure = retryFailure;
                return FailUploader(failure, retryFailure, retryFailure.message);
            }
            SubmittedTextureCandidate releasedCandidate;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
                m_impl->DetachReplacementCandidate(slot, releasedCandidate);
                slot.replacementFailure = {};
                slot.replacementState = TextureReplacementState::InstallationPending;
                slot.replacementRetryRequired = false;
                m_impl->AddWork(recordIndex);
            }
            return true;
        }

        TextureUploadRequestId uploadRequest;
        TextureUploadFailure uploadFailure;
        auto* const replacementTexture = replacement.IsValid() && replacement.GetType() == textures::TextureResourceType ? static_cast<textures::TextureResourceObject*>(replacement.Get()) : nullptr;
        if (replacementTexture == nullptr || !replacementTexture->IsOpen() ||
            !EstimateCandidateGpuBytes(replacementTexture->GetMetadata(), replacementTargetFirstResidentMip, replacementTargetGpuBytes, uploadFailure) ||
            !m_uploader.RequestGenerationReplacement(replacement, residency, replacementTargetFirstResidentMip, uploadRequest, ToAsyncPriority(priority), &uploadFailure))
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
            slot.replacementFailure = uploadFailure;
            return FailUploader(failure, uploadFailure, "texture generation replacement retry could not restart upload");
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
        slot.replacementUploadRequest = uploadRequest;
        slot.replacementTargetGpuBytes = replacementTargetGpuBytes;
        slot.replacementFailure = {};
        slot.replacementState = TextureReplacementState::Uploading;
        slot.replacementRetryRequired = false;
        m_impl->AddWork(recordIndex);
        return true;
    }

    bool TextureResidencyRuntime::CancelDemand(TextureDemandHandle& demand, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (demand.m_runtime != this || !demand.m_demand.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand does not belong to this residency runtime");
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->FindDemand(demand.m_demand) == nullptr)
                return Fail(failure, TextureResidencyRuntimeFailureCode::StaleDemand, "texture demand handle is stale");
        }
        demand.Reset();
        return true;
    }

    void TextureResidencyRuntime::ReleaseDemand(const TextureDemandId demand) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::DemandSlot* const demandSlot = m_impl->FindDemand(demand);
        if (demandSlot == nullptr)
            return;
        const u32 residencyIndex = demandSlot->residencyIndex;
        const u32 residencyGeneration = demandSlot->residencyGeneration;
        const u32 requestedFirstResidentMip = demandSlot->requestedFirstResidentMip;
        const TextureDemandPriority priority = demandSlot->priority;
        demandSlot->residencyIndex = InvalidRuntimeRecordIndex;
        demandSlot->residencyGeneration = 0;
        demandSlot->requestedFirstResidentMip = InvalidFirstResidentMip;
        demandSlot->priority = TextureDemandPriority::Streaming;
        demandSlot->active = false;
        demandSlot->generation = NextGeneration(demandSlot->generation);
        m_impl->recycledDemands.PushBack(demand.index);
        --m_impl->demandCount;
        ++m_impl->demandsReleased;
        Impl::ResidencySlot* const slot = m_impl->FindRecord(residencyIndex, residencyGeneration);
        if (slot == nullptr || slot->demandCount == 0)
            return;
        const u32 previousDesired = slot->desiredFirstResidentMip;
        m_impl->RemoveDemandBookkeeping(*slot, requestedFirstResidentMip, priority);
        --slot->demandCount;
        if (slot->desiredFirstResidentMip != previousDesired)
            slot->desiredChangedTick = m_impl->tickSerial;
        if (slot->demandCount != 0 && slot->desiredFirstResidentMip != previousDesired && slot->qualityState == TextureQualityState::Failed)
        {
            slot->qualityState = TextureQualityState::Stable;
            slot->qualityFailure = {};
            slot->qualityRetryRequired = false;
        }
        if (slot->demandCount == 0)
            m_impl->AddWork(residencyIndex);
        else if (slot->desiredFirstResidentMip != previousDesired)
            m_impl->AddWork(residencyIndex);
    }

    bool TextureResidencyRuntime::Tick(const rhi::ResidencyFenceSet& prerequisiteFences, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime progress must run on the main thread");

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            ++m_impl->tickSerial;
            if (m_impl->tickSerial == 0)
                ++m_impl->tickSerial;
        }
        u32 mipTransitionsStarted = 0;
        u32 stateChecksRemaining = m_impl->config.maximumStateChecksPerTick;

        TextureResidencyFailure residencyFailure;
        static_cast<void>(m_residency.CollectRetirements(&residencyFailure));
        if (residencyFailure.code != TextureResidencyFailureCode::None)
            return FailResidency(failure, residencyFailure, "texture residency retirement collection failed");

        const auto snapshotWork = [this, &stateChecksRemaining]() noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            m_impl->BuildWorkScratch(stateChecksRemaining);
            stateChecksRemaining -= m_impl->workScratch.Size();
        };

        const auto retireRecord = [this, failure](const Impl::WorkRef work) noexcept -> bool
        {
            GpuTextureResidencyHandle residency;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                if (slot == nullptr || slot->demandCount != 0)
                    return true;
                residency = slot->residency;
            }
            TextureResidencyFailure retireFailure;
            if (!m_residency.Retire(residency, &retireFailure))
            {
                if (retireFailure.code == TextureResidencyFailureCode::Busy)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                        slot->state = TextureRuntimeState::Cancelling;
                    return true;
                }
                return FailResidency(failure, retireFailure, "texture residency retirement failed");
            }
            TextureResidencyInfo lowerInfo;
            const bool stillLive = m_residency.GetInfo(residency, lowerInfo, &retireFailure);
            Impl::DeferredResidencyOwnership deferred;
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
            if (slot == nullptr || slot->demandCount != 0)
                return true;
            slot->uploadRequest = {};
            if (!stillLive && retireFailure.code == TextureResidencyFailureCode::StaleHandle)
                m_impl->RemoveResidency(work.index, deferred);
            else
                slot->state = TextureRuntimeState::Retiring;
            return true;
        };

        const auto processWork = [this, failure, &retireRecord, &mipTransitionsStarted]() noexcept -> bool
        {
            for (const Impl::WorkRef work : m_impl->workScratch)
            {
                TextureRuntimeState state = TextureRuntimeState::Invalid;
                TextureUploadRequestId request;
                TextureUploadRequestId qualityRequest;
                TextureUploadRequestId replacementRequest;
                GpuTextureResidencyHandle residency;
                const resources::ResourceHandle* resourceOwner = nullptr;
                u32 demandCount = 0;
                u32 desiredFirstResidentMip = InvalidFirstResidentMip;
                u32 initialTargetFirstResidentMip = InvalidFirstResidentMip;
                u32 currentFirstResidentMip = InvalidFirstResidentMip;
                u32 qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                u64 desiredChangedTick = 0;
                TextureDemandPriority aggregatePriority = TextureDemandPriority::Streaming;
                TextureQualityState qualityState = TextureQualityState::Stable;
                TextureReplacementState replacementState = TextureReplacementState::None;
                u32 replacementGeneration = 0;
                crypto::Digest256 replacementFingerprint;
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    const Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot == nullptr)
                        continue;
                    state = slot->state;
                    request = slot->uploadRequest;
                    qualityRequest = slot->qualityUploadRequest;
                    replacementRequest = slot->replacementUploadRequest;
                    residency = slot->residency;
                    resourceOwner = &slot->resource;
                    demandCount = slot->demandCount;
                    desiredFirstResidentMip = slot->desiredFirstResidentMip;
                    initialTargetFirstResidentMip = slot->initialTargetFirstResidentMip;
                    currentFirstResidentMip = slot->currentFirstResidentMip;
                    qualityTargetFirstResidentMip = slot->qualityTargetFirstResidentMip;
                    desiredChangedTick = slot->desiredChangedTick;
                    aggregatePriority = slot->aggregatePriority;
                    qualityState = slot->qualityState;
                    replacementState = slot->replacementState;
                    replacementGeneration = slot->replacementResource.GetGeneration();
                    replacementFingerprint = slot->replacementContentFingerprint;
                }
                // ResidencySlot storage is reserved to the runtime's hard record cap and only the main thread
                // replaces/removes its resource owner. Duplicate the stable slot owner after unlocking so the
                // ResourceRegistry reference operation cannot invert with the runtime spinlock.
                resources::ResourceHandle resource(*resourceOwner);

                if (state == TextureRuntimeState::Retiring)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure) && queryFailure.code == TextureResidencyFailureCode::StaleHandle)
                    {
                        Impl::DeferredResidencyOwnership deferred;
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr && slot->demandCount == 0)
                            m_impl->RemoveResidency(work.index, deferred);
                    }
                    continue;
                }

                if (demandCount == 0)
                {
                    if (replacementState == TextureReplacementState::Uploading && replacementRequest.IsValid())
                    {
                        const TextureUploadState replacementUploadState = m_uploader.GetState(replacementRequest);
                        if (replacementUploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(replacementRequest, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture replacement cancellation failed");
                        }
                        Impl::DeferredResidencyOwnership deferred;
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->replacementUploadRequest = {};
                            Impl::TransferResourceOwnershipToEmpty(deferred.replacementResource, slot->replacementResource);
                            m_impl->DetachReplacementCandidate(*slot, deferred.replacementCandidate);
                            slot->replacementFailure = {};
                            slot->replacementContentFingerprint = {};
                            slot->replacementTargetFirstResidentMip = InvalidFirstResidentMip;
                            slot->replacementMipCount = 0;
                            slot->replacementMipTailFirstLevel = 0;
                            slot->replacementTargetGpuBytes = 0;
                            slot->replacementState = TextureReplacementState::None;
                            slot->replacementRetryRequired = false;
                        }
                        replacementState = TextureReplacementState::None;
                    }
                    else if (replacementState == TextureReplacementState::Failed)
                    {
                        Impl::DeferredResidencyOwnership deferred;
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            Impl::TransferResourceOwnershipToEmpty(deferred.replacementResource, slot->replacementResource);
                            m_impl->DetachReplacementCandidate(*slot, deferred.replacementCandidate);
                            slot->replacementFailure = {};
                            slot->replacementContentFingerprint = {};
                            slot->replacementTargetFirstResidentMip = InvalidFirstResidentMip;
                            slot->replacementMipCount = 0;
                            slot->replacementMipTailFirstLevel = 0;
                            slot->replacementTargetGpuBytes = 0;
                            slot->replacementState = TextureReplacementState::None;
                            slot->replacementRetryRequired = false;
                        }
                        replacementState = TextureReplacementState::None;
                    }

                    if ((qualityState == TextureQualityState::Uploading || qualityState == TextureQualityState::Cancelling) && qualityRequest.IsValid())
                    {
                        const TextureUploadState uploadState = m_uploader.GetState(qualityRequest);
                        if (qualityState == TextureQualityState::Uploading && uploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(qualityRequest, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture quality cancellation failed");
                        }
                        if (m_uploader.GetState(qualityRequest) != TextureUploadState::Invalid)
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                m_impl->ReleaseQualityPromotionReservation(*slot);
                                slot->qualityState = TextureQualityState::Cancelling;
                            }
                            continue;
                        }
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            m_impl->ReleaseQualityPromotionReservation(*slot);
                            slot->qualityUploadRequest = {};
                            slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                            slot->qualityTargetGpuBytes = 0;
                            slot->qualityState = TextureQualityState::Stable;
                            slot->qualityFailure = {};
                            slot->qualityRetryRequired = false;
                        }
                    }
                    if (request.IsValid())
                    {
                        const TextureUploadState uploadState = m_uploader.GetState(request);
                        if (state != TextureRuntimeState::Cancelling && uploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(request, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture mip-tail cancellation failed");
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                                slot->state = TextureRuntimeState::Cancelling;
                        }
                        if (m_uploader.GetState(request) != TextureUploadState::Invalid)
                            continue;
                    }
                    if (!retireRecord(work))
                        return false;
                    continue;
                }

                if (replacementState == TextureReplacementState::Uploading && replacementRequest.IsValid())
                {
                    const TextureUploadState replacementUploadState = m_uploader.GetState(replacementRequest);
                    if (replacementUploadState == TextureUploadState::Failed)
                    {
                        TextureUploadFailure requestFailure;
                        static_cast<void>(m_uploader.GetRequestFailure(replacementRequest, requestFailure));
                        TextureUploadFailure cancelFailure;
                        if (!m_uploader.Cancel(replacementRequest, &cancelFailure))
                            return FailUploader(failure, cancelFailure, "failed texture replacement upload could not be released");
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr && slot->replacementResource.GetGeneration() == replacementGeneration)
                        {
                            slot->replacementUploadRequest = {};
                            slot->replacementFailure = requestFailure;
                            slot->replacementState = TextureReplacementState::Failed;
                            slot->replacementRetryRequired = true;
                            if (slot->state == TextureRuntimeState::BindlessReady)
                                m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                        }
                        continue;
                    }
                    if (replacementUploadState == TextureUploadState::ReadyToInstall)
                    {
                        SubmittedTextureCandidate candidate;
                        TextureUploadFailure takeFailure;
                        if (!m_uploader.TakeSubmitted(replacementRequest, candidate, &takeFailure))
                            return FailUploader(failure, takeFailure, "completed texture replacement candidate could not be retained");

                        TextureInstallationDesc installation;
                        installation.texture = candidate.texture.GetRef();
                        installation.firstResidentMip = candidate.firstResidentMip;
                        installation.residentMipCount = candidate.residentMipCount;
                        installation.source = candidate.resource;
                        installation.contentFingerprint = candidate.contentFingerprint;
                        TextureInstallationTicket ticket;
                        TextureResidencyFailure installFailure;
                        if (!m_residency.Install(residency, installation, ticket, &installFailure))
                        {
                            TextureUploadFailure retainedFailure;
                            retainedFailure.code = TextureUploadFailureCode::ResidencyFailure;
                            retainedFailure.message = "completed texture replacement candidate could not enter residency installation";
                            retainedFailure.residencyFailure = installFailure;
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr && slot->replacementResource.GetGeneration() == replacementGeneration)
                            {
                                slot->replacementUploadRequest = {};
                                if (!m_impl->RetainReplacementCandidate(*slot, static_cast<SubmittedTextureCandidate&&>(candidate)))
                                {
                                    retainedFailure.code = TextureUploadFailureCode::CapacityExceeded;
                                    retainedFailure.message = "failed texture replacement candidate exceeds the runtime retained GPU-byte budget";
                                    retainedFailure.residencyFailure = {};
                                }
                                slot->replacementFailure = retainedFailure;
                                slot->replacementState = TextureReplacementState::Failed;
                                slot->replacementRetryRequired = true;
                                if (slot->state == TextureRuntimeState::BindlessReady)
                                    m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                            }
                            continue;
                        }
                        candidate.Reset();
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr && slot->replacementResource.GetGeneration() == replacementGeneration)
                        {
                            slot->replacementUploadRequest = {};
                            slot->replacementFailure = {};
                            slot->replacementState = TextureReplacementState::InstallationPending;
                            slot->replacementRetryRequired = false;
                        }
                        continue;
                    }
                }

                if (replacementState == TextureReplacementState::InstallationPending)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "pending texture replacement residency query failed");
                    const bool accepted = lowerInfo.state == TextureResidencyState::BindlessReady && lowerInfo.source.IsValid() &&
                                          lowerInfo.source.GetGeneration() == replacementGeneration &&
                                          lowerInfo.contentFingerprint == replacementFingerprint;
                    if (accepted)
                    {
                        Impl::DeferredResidencyOwnership deferred;
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr && slot->replacementResource.GetGeneration() == replacementGeneration)
                            {
                                VG_ASSERT_MSG(slot->replacementMipCount != 0,
                                              "accepted texture replacement must retain plain metadata for lock-safe cutover");
                                Impl::TransferResourceOwnershipToEmpty(deferred.resource, slot->resource);
                                Impl::TransferResourceOwnershipToEmpty(slot->resource, slot->replacementResource);
                                slot->mipCount = slot->replacementMipCount;
                                slot->mipTailFirstLevel = slot->replacementMipTailFirstLevel;
                                const u32 previousDesired = slot->desiredFirstResidentMip;
                                slot->desiredFirstResidentMip = Impl::RecomputeDesiredFirstResidentMip(*slot);
                                if (slot->desiredFirstResidentMip != previousDesired)
                                    slot->desiredChangedTick = m_impl->tickSerial;
                                slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                                m_impl->SetCommittedGpuBytes(*slot, slot->replacementTargetGpuBytes);
                                slot->replacementUploadRequest = {};
                                m_impl->DetachReplacementCandidate(*slot, deferred.replacementCandidate);
                                slot->replacementFailure = {};
                                slot->replacementContentFingerprint = {};
                                slot->replacementTargetFirstResidentMip = InvalidFirstResidentMip;
                                slot->replacementMipCount = 0;
                                slot->replacementMipTailFirstLevel = 0;
                                slot->replacementTargetGpuBytes = 0;
                                slot->replacementState = TextureReplacementState::None;
                                slot->replacementRetryRequired = false;
                                slot->qualityUploadRequest = {};
                                slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                                slot->qualityTargetGpuBytes = 0;
                                slot->qualityFailure = {};
                                slot->qualityState = TextureQualityState::Stable;
                                slot->qualityRetryRequired = false;
                                if (slot->state == TextureRuntimeState::BindlessReady && slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                                    m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                                else
                                    m_impl->AddWork(work.index);
                            }
                        }
                    }
                    continue;
                }

                if (replacementState == TextureReplacementState::Failed)
                    continue;

                if (state == TextureRuntimeState::InstallationPending)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "pending texture residency query failed");
                    if (lowerInfo.state == TextureResidencyState::BindlessReady)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->state = TextureRuntimeState::BindlessReady;
                            slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                            m_impl->SetCommittedGpuBytes(*slot, slot->initialTargetGpuBytes);
                            slot->uploadRequest = {};
                            if (slot->replacementState == TextureReplacementState::None && slot->qualityState == TextureQualityState::Stable &&
                                slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                                m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                            else
                                m_impl->AddWork(work.index);
                        }
                    }
                    continue;
                }

                if (state == TextureRuntimeState::MipTailLoading || state == TextureRuntimeState::Cancelling)
                {
                    TextureUploadState uploadState = request.IsValid() ? m_uploader.GetState(request) : TextureUploadState::Invalid;
                    if (state == TextureRuntimeState::MipTailLoading && initialTargetFirstResidentMip == InvalidFirstResidentMip && request.IsValid())
                    {
                        if (uploadState != TextureUploadState::Invalid)
                            continue;
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                                slot->uploadRequest = {};
                        }
                        request = {};
                        uploadState = TextureUploadState::Invalid;
                    }
                    if (state == TextureRuntimeState::MipTailLoading && initialTargetFirstResidentMip != InvalidFirstResidentMip &&
                        desiredFirstResidentMip != initialTargetFirstResidentMip)
                    {
                        if (request.IsValid() && uploadState == TextureUploadState::Invalid)
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                slot->uploadRequest = {};
                                slot->state = TextureRuntimeState::InstallationPending;
                            }
                            continue;
                        }
                        if (request.IsValid() && uploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(request, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture initial-quality retarget cancellation failed");
                            if (m_uploader.GetState(request) != TextureUploadState::Invalid)
                            {
                                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                                Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                                if (slot != nullptr)
                                {
                                    slot->initialTargetFirstResidentMip = InvalidFirstResidentMip;
                                    slot->initialTargetGpuBytes = 0;
                                }
                                continue;
                            }
                        }
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                slot->uploadRequest = {};
                                slot->initialTargetFirstResidentMip = InvalidFirstResidentMip;
                                slot->initialTargetGpuBytes = 0;
                            }
                        }
                        request = {};
                        uploadState = TextureUploadState::Invalid;
                        initialTargetFirstResidentMip = InvalidFirstResidentMip;
                    }

                    if (request.IsValid())
                    {
                        uploadState = m_uploader.GetState(request);
                        if (uploadState == TextureUploadState::Failed)
                        {
                            TextureUploadFailure requestFailure;
                            static_cast<void>(m_uploader.GetRequestFailure(request, requestFailure));
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(request, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "failed texture initial residency request could not be released");
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                slot->uploadRequest = {};
                                if (slot->demandCount != 0 && slot->desiredFirstResidentMip != slot->initialTargetFirstResidentMip)
                                {
                                    slot->uploadFailure = {};
                                    slot->initialTargetFirstResidentMip = InvalidFirstResidentMip;
                                    slot->initialTargetGpuBytes = 0;
                                    slot->state = TextureRuntimeState::MipTailLoading;
                                    m_impl->AddWork(work.index);
                                }
                                else
                                {
                                    slot->uploadFailure = requestFailure;
                                    slot->state = TextureRuntimeState::Failed;
                                    m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                                    ++m_impl->failedResidencies;
                                }
                            }
                            continue;
                        }
                        if (uploadState == TextureUploadState::Invalid)
                        {
                            TextureResidencyInfo lowerInfo;
                            TextureResidencyFailure queryFailure;
                            if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                                return FailResidency(failure, queryFailure, "installed texture residency query failed");
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                slot->uploadRequest = {};
                                slot->state = lowerInfo.state == TextureResidencyState::BindlessReady ? TextureRuntimeState::BindlessReady : TextureRuntimeState::InstallationPending;
                                if (slot->state == TextureRuntimeState::BindlessReady)
                                {
                                    slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                                    m_impl->SetCommittedGpuBytes(*slot, slot->initialTargetGpuBytes);
                                    if (slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                                        m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                                }
                            }
                            continue;
                        }
                        continue;
                    }

                    if (state == TextureRuntimeState::Cancelling)
                        continue;
                    if (initialTargetFirstResidentMip != InvalidFirstResidentMip)
                    {
                        TextureResidencyInfo lowerInfo;
                        TextureResidencyFailure queryFailure;
                        if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                            return FailResidency(failure, queryFailure, "pending initial texture residency query failed");
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->state = lowerInfo.state == TextureResidencyState::BindlessReady ? TextureRuntimeState::BindlessReady : TextureRuntimeState::InstallationPending;
                            if (slot->state == TextureRuntimeState::BindlessReady)
                            {
                                slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                                m_impl->SetCommittedGpuBytes(*slot, slot->initialTargetGpuBytes);
                                if (slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                                    m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                            }
                        }
                        continue;
                    }

                    if (mipTransitionsStarted >= m_impl->config.maximumMipTransitionsPerTick)
                        continue;
                    TextureUploadRequestId retargetRequest;
                    TextureUploadFailure retargetFailure;
                    u64 retargetGpuBytes = 0;
                    auto* const retargetTexture = resource.IsValid() && resource.GetType() == textures::TextureResourceType ? static_cast<textures::TextureResourceObject*>(resource.Get()) : nullptr;
                    const bool preflighted = retargetTexture != nullptr && retargetTexture->IsOpen() &&
                                             EstimateCandidateGpuBytes(retargetTexture->GetMetadata(), desiredFirstResidentMip, retargetGpuBytes, retargetFailure);
                    const bool requested = preflighted &&
                                           m_uploader.RequestMipTransition(resource, m_residency, residency, desiredFirstResidentMip, retargetRequest,
                                                                          ToAsyncPriority(aggregatePriority), &retargetFailure);
                    if (!requested)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            if (slot->demandCount != 0 && slot->desiredFirstResidentMip != desiredFirstResidentMip)
                            {
                                slot->uploadFailure = {};
                                m_impl->AddWork(work.index);
                            }
                            else if (IsTransientTransitionAdmissionFailure(retargetFailure))
                                ++m_impl->qualityTransitionAdmissionDeferrals;
                            else
                            {
                                slot->uploadFailure = retargetFailure;
                                slot->state = TextureRuntimeState::Failed;
                                m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                                ++m_impl->failedResidencies;
                            }
                        }
                        continue;
                    }
                    if (!retargetRequest.IsValid())
                    {
                        TextureResidencyInfo lowerInfo;
                        TextureResidencyFailure queryFailure;
                        if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                            return FailResidency(failure, queryFailure, "texture initial retarget no-op query failed");
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->state = TextureRuntimeState::BindlessReady;
                            slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                            slot->initialTargetGpuBytes = retargetGpuBytes;
                            m_impl->SetCommittedGpuBytes(*slot, retargetGpuBytes);
                            slot->initialTargetFirstResidentMip = lowerInfo.firstResidentMip;
                            if (slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                                m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                        }
                        continue;
                    }
                    ++mipTransitionsStarted;
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        slot->uploadRequest = retargetRequest;
                        slot->initialTargetFirstResidentMip = desiredFirstResidentMip;
                        slot->initialTargetGpuBytes = retargetGpuBytes;
                    }
                    continue;
                }

                if (state != TextureRuntimeState::BindlessReady || replacementState != TextureReplacementState::None)
                    continue;

                if (qualityState == TextureQualityState::Uploading)
                {
                    const TextureUploadState uploadState = qualityRequest.IsValid() ? m_uploader.GetState(qualityRequest) : TextureUploadState::Invalid;
                    if (desiredFirstResidentMip != qualityTargetFirstResidentMip)
                    {
                        if (qualityRequest.IsValid() && uploadState == TextureUploadState::Invalid)
                        {
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                            {
                                slot->qualityUploadRequest = {};
                                slot->qualityState = TextureQualityState::InstallationPending;
                            }
                            continue;
                        }
                        if (qualityRequest.IsValid() && uploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(qualityRequest, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture quality retarget cancellation failed");
                            if (m_uploader.GetState(qualityRequest) != TextureUploadState::Invalid)
                            {
                                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                                Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                                if (slot != nullptr)
                                {
                                    m_impl->ReleaseQualityPromotionReservation(*slot);
                                    slot->qualityState = TextureQualityState::Cancelling;
                                }
                                continue;
                            }
                        }
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            m_impl->ReleaseQualityPromotionReservation(*slot);
                            slot->qualityUploadRequest = {};
                            slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                            slot->qualityTargetGpuBytes = 0;
                            slot->qualityState = TextureQualityState::Stable;
                        }
                        qualityState = TextureQualityState::Stable;
                        qualityRequest = {};
                        qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                    }
                    else if (uploadState == TextureUploadState::Failed)
                    {
                        TextureUploadFailure requestFailure;
                        static_cast<void>(m_uploader.GetRequestFailure(qualityRequest, requestFailure));
                        TextureUploadFailure cancelFailure;
                        if (!m_uploader.Cancel(qualityRequest, &cancelFailure))
                            return FailUploader(failure, cancelFailure, "failed texture quality request could not be released");
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            m_impl->ReleaseQualityPromotionReservation(*slot);
                            slot->qualityUploadRequest = {};
                            if (slot->demandCount != 0 && slot->desiredFirstResidentMip != slot->qualityTargetFirstResidentMip)
                            {
                                slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                                slot->qualityTargetGpuBytes = 0;
                                slot->qualityFailure = {};
                                slot->qualityState = TextureQualityState::Stable;
                                slot->qualityRetryRequired = false;
                                m_impl->AddWork(work.index);
                            }
                            else
                            {
                                slot->qualityFailure = requestFailure;
                                slot->qualityState = TextureQualityState::Failed;
                                slot->qualityRetryRequired = true;
                                ++m_impl->qualityTransitionFailures;
                                m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                            }
                        }
                        continue;
                    }
                    else if (uploadState == TextureUploadState::Invalid)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->qualityUploadRequest = {};
                            slot->qualityState = TextureQualityState::InstallationPending;
                        }
                        continue;
                    }
                    else
                        continue;
                }

                if (qualityState == TextureQualityState::Cancelling)
                {
                    if (qualityRequest.IsValid() && m_uploader.GetState(qualityRequest) != TextureUploadState::Invalid)
                        continue;
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        m_impl->ReleaseQualityPromotionReservation(*slot);
                        slot->qualityUploadRequest = {};
                        slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                        slot->qualityTargetGpuBytes = 0;
                        slot->qualityState = TextureQualityState::Stable;
                    }
                    qualityState = TextureQualityState::Stable;
                    qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                }

                if (qualityState == TextureQualityState::InstallationPending)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "pending texture quality residency query failed");
                    const bool accepted = lowerInfo.state == TextureResidencyState::BindlessReady && lowerInfo.source.IsValid() && resource.IsValid() &&
                                          lowerInfo.source.GetPath() == resource.GetPath() && lowerInfo.source.GetGeneration() == resource.GetGeneration() &&
                                          lowerInfo.firstResidentMip == qualityTargetFirstResidentMip;
                    if (!accepted)
                        continue;
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        slot->currentFirstResidentMip = lowerInfo.firstResidentMip;
                        m_impl->SetCommittedGpuBytes(*slot, slot->qualityTargetGpuBytes);
                        m_impl->ReleaseQualityPromotionReservation(*slot);
                        slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                        slot->qualityTargetGpuBytes = 0;
                        slot->qualityFailure = {};
                        slot->qualityState = TextureQualityState::Stable;
                        slot->qualityRetryRequired = false;
                        currentFirstResidentMip = slot->currentFirstResidentMip;
                        desiredFirstResidentMip = slot->desiredFirstResidentMip;
                        desiredChangedTick = slot->desiredChangedTick;
                        aggregatePriority = slot->aggregatePriority;
                        if (slot->currentFirstResidentMip == slot->desiredFirstResidentMip)
                            m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                    }
                    if (currentFirstResidentMip == desiredFirstResidentMip)
                        continue;
                    qualityState = TextureQualityState::Stable;
                }

                if (qualityState == TextureQualityState::Failed)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                        m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                    continue;
                }

                if (currentFirstResidentMip == InvalidFirstResidentMip)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "bindless texture quality query failed");
                    currentFirstResidentMip = lowerInfo.firstResidentMip;
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                        slot->currentFirstResidentMip = currentFirstResidentMip;
                }
                if (currentFirstResidentMip == desiredFirstResidentMip)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr && slot->qualityState == TextureQualityState::Stable && slot->replacementState == TextureReplacementState::None)
                        m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                    continue;
                }

                const bool demotion = desiredFirstResidentMip > currentFirstResidentMip;
                u64 tickSerial = 0;
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    tickSerial = m_impl->tickSerial;
                }
                if (demotion && tickSerial - desiredChangedTick < m_impl->config.mipDemotionHysteresisTicks)
                    continue;
                if (mipTransitionsStarted >= m_impl->config.maximumMipTransitionsPerTick)
                    continue;

                TextureUploadRequestId transitionRequest;
                TextureUploadFailure transitionFailure;
                u64 transitionGpuBytes = 0;
                auto* const transitionTexture = resource.IsValid() && resource.GetType() == textures::TextureResourceType ? static_cast<textures::TextureResourceObject*>(resource.Get()) : nullptr;
                if (transitionTexture == nullptr || !transitionTexture->IsOpen() ||
                    !EstimateCandidateGpuBytes(transitionTexture->GetMetadata(), desiredFirstResidentMip, transitionGpuBytes, transitionFailure))
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        if (slot->demandCount == 0 || slot->desiredFirstResidentMip != desiredFirstResidentMip)
                            m_impl->AddWork(work.index);
                        else
                        {
                            slot->qualityTargetFirstResidentMip = desiredFirstResidentMip;
                            slot->qualityTargetGpuBytes = transitionGpuBytes;
                            slot->qualityFailure = transitionFailure;
                            slot->qualityState = TextureQualityState::Failed;
                            slot->qualityRetryRequired = true;
                            ++m_impl->qualityTransitionFailures;
                            m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                        }
                    }
                    continue;
                }

                if (!demotion)
                {
                    bool overBudget = false;
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            overBudget = !m_impl->TryReserveQualityPromotion(*slot, transitionGpuBytes);
                            if (overBudget)
                                ++m_impl->qualityPromotionBudgetDeferrals;
                        }
                    }
                    if (overBudget)
                        continue;
                }

                const bool requested = m_uploader.RequestMipTransition(resource, m_residency, residency, desiredFirstResidentMip, transitionRequest,
                                                                       ToAsyncPriority(aggregatePriority), &transitionFailure);
                if (!requested)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        m_impl->ReleaseQualityPromotionReservation(*slot);
                        if (slot->demandCount == 0 || slot->desiredFirstResidentMip != desiredFirstResidentMip)
                            m_impl->AddWork(work.index);
                        else if (IsTransientTransitionAdmissionFailure(transitionFailure))
                            ++m_impl->qualityTransitionAdmissionDeferrals;
                        else
                        {
                            slot->qualityTargetFirstResidentMip = desiredFirstResidentMip;
                            slot->qualityTargetGpuBytes = transitionGpuBytes;
                            slot->qualityFailure = transitionFailure;
                            slot->qualityState = TextureQualityState::Failed;
                            slot->qualityRetryRequired = true;
                            ++m_impl->qualityTransitionFailures;
                            m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                        }
                    }
                    continue;
                }
                if (!transitionRequest.IsValid())
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        slot->currentFirstResidentMip = desiredFirstResidentMip;
                        m_impl->SetCommittedGpuBytes(*slot, transitionGpuBytes);
                        m_impl->ReleaseQualityPromotionReservation(*slot);
                        slot->qualityTargetFirstResidentMip = InvalidFirstResidentMip;
                        slot->qualityTargetGpuBytes = 0;
                        slot->qualityFailure = {};
                        slot->qualityState = TextureQualityState::Stable;
                        slot->qualityRetryRequired = false;
                        m_impl->RemoveWorkUnlessCleanupRequired(work.index);
                    }
                    continue;
                }

                ++mipTransitionsStarted;
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                if (slot != nullptr)
                {
                    slot->qualityUploadRequest = transitionRequest;
                    slot->qualityTargetFirstResidentMip = desiredFirstResidentMip;
                    slot->qualityTargetGpuBytes = transitionGpuBytes;
                    slot->qualityFailure = {};
                    slot->qualityState = TextureQualityState::Uploading;
                    slot->qualityRetryRequired = false;
                    ++m_impl->qualityTransitionsStarted;
                }
            }
            return true;
        };

        snapshotWork();
        if (!processWork())
            return false;

        TextureUploadFailure uploadFailure;
        if (!m_uploader.Tick(prerequisiteFences, &uploadFailure))
            return FailUploader(failure, uploadFailure, "texture uploader progress failed");
        u32 installed = 0;
        if (!m_uploader.InstallReadyCandidates(m_residency, m_impl->config.maximumInstallationsPerTick, installed, &uploadFailure))
        {
            const bool requestFailed = uploadFailure.request.IsValid() && m_uploader.GetState(uploadFailure.request) == TextureUploadState::Failed;
            if (!IsTransientTransitionAdmissionFailure(uploadFailure) && !requestFailed)
                return FailUploader(failure, uploadFailure, "completed textures could not enter residency installation");
            if (!requestFailed)
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                ++m_impl->qualityTransitionAdmissionDeferrals;
            }
        }

        snapshotWork();
        return processWork();
    }

    bool TextureResidencyRuntime::StageGpuSceneContribution(GpuSceneRuntime& gpuScene, bool& staged, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        staged = false;
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture GPU Scene contribution must stage on the main thread");
        if (!gpuScene.IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidDependency, "texture GPU Scene contribution requires an initialized GPU Scene runtime");
        if (m_residency.GetStats().pendingInstallations == 0)
            return true;

        TextureResidencyBatch batch;
        TextureResidencyFailure residencyFailure;
        if (!m_residency.PrepareBatch(m_impl->config.maximumTableInstallationsPerFrame, batch, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture GPU Scene contribution preparation failed");
        if (batch.installationCount == 0)
        {
            m_residency.AcceptSubmittedBatch(batch, {});
            return true;
        }
        const containers::ArraySpan<GpuSceneUploadRequest> requests{m_impl->contributionRequests.TypedData(), batch.installationCount};
        if (!m_residency.BuildUploadRequests(batch, requests, &residencyFailure))
        {
            static_cast<void>(m_residency.RetryBatch(batch));
            return FailResidency(failure, residencyFailure, "texture GPU Scene upload request planning failed");
        }

        GpuSceneContributionDesc contribution;
        contribution.owner = this;
        contribution.token = EncodeTextureBatch(batch);
        contribution.requests = {requests.Data(), requests.Size()};
        contribution.write = WriteTextureContribution;
        contribution.accept = AcceptTextureContribution;
        contribution.retry = RetryTextureContribution;
        GpuSceneRuntimeFailure gpuSceneFailure;
        if (!gpuScene.StageContribution(contribution, &gpuSceneFailure))
        {
            static_cast<void>(m_residency.RetryBatch(batch));
            return FailGpuScene(failure, gpuSceneFailure, "texture GPU Scene contribution staging failed");
        }
        staged = true;
        return true;
    }

    bool TextureResidencyRuntime::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        TextureResidencyFailure residencyFailure;
        return m_residency.SealRetirements(safeAfter, &residencyFailure) ? true : FailResidency(failure, residencyFailure, "texture residency retirement sealing failed");
    }

    u32 TextureResidencyRuntime::CollectRetirements(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
        {
            static_cast<void>(Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized"));
            return 0;
        }
        TextureResidencyFailure residencyFailure;
        const u32 collected = m_residency.CollectRetirements(&residencyFailure);
        if (residencyFailure.code != TextureResidencyFailureCode::None)
        {
            static_cast<void>(FailResidency(failure, residencyFailure, "texture residency retirement collection failed"));
            return 0;
        }
        return collected;
    }

    TextureResidentLookupResult TextureResidencyRuntime::FindResidentResource(const resources::ResourcePath path, TextureResidentResource& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !path.IsValid())
            return TextureResidentLookupResult::NotFound;

        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 candidate = InvalidRuntimeRecordIndex;
        if (!m_impl->residencyByPath.Find(path.Id(), candidate))
            return TextureResidentLookupResult::NotFound;

        while (candidate != InvalidRuntimeRecordIndex)
        {
            const Impl::ResidencySlot& slot = m_impl->residencies[candidate];
            const u32 next = slot.nextSamePath;
            if (slot.active && slot.demandCount != 0u && slot.state == TextureRuntimeState::BindlessReady)
            {
                if (output.residency.IsValid())
                {
                    output = {};
                    return TextureResidentLookupResult::Ambiguous;
                }
                output.residency = slot.residency;
                output.resourceGeneration = slot.resource.GetGeneration();
            }
            candidate = next;
        }
        return output.residency.IsValid() ? TextureResidentLookupResult::Found : TextureResidentLookupResult::NotFound;
    }

    TextureResidentLookupResult TextureResidencyRuntime::FindDemandedResource(const resources::ResourcePath path, TextureRuntimeInfo& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !path.IsValid())
            return TextureResidentLookupResult::NotFound;

        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 candidate = InvalidRuntimeRecordIndex;
        if (!m_impl->residencyByPath.Find(path.Id(), candidate))
            return TextureResidentLookupResult::NotFound;

        bool found = false;
        while (candidate != InvalidRuntimeRecordIndex)
        {
            const Impl::ResidencySlot& slot = m_impl->residencies[candidate];
            const u32 next = slot.nextSamePath;
            if (slot.active && slot.demandCount != 0u)
            {
                if (found)
                {
                    output = {};
                    return TextureResidentLookupResult::Ambiguous;
                }
                found = true;
                output.residency = slot.residency;
                output.resourcePath = slot.resource.GetPath();
                output.resourceGeneration = slot.resource.GetGeneration();
                output.demandCount = slot.demandCount;
                output.state = slot.state;
                output.uploadFailure = slot.uploadFailure;
                output.currentFirstResidentMip = slot.currentFirstResidentMip;
                output.desiredFirstResidentMip = slot.desiredFirstResidentMip;
                output.qualityTargetFirstResidentMip = slot.qualityTargetFirstResidentMip;
                output.demandPriority = slot.aggregatePriority;
                output.qualityState = slot.qualityState;
                output.qualitySatisfied = slot.state == TextureRuntimeState::BindlessReady && slot.qualityState == TextureQualityState::Stable &&
                                          slot.currentFirstResidentMip == slot.desiredFirstResidentMip;
                output.qualityRetryRequired = slot.qualityRetryRequired;
                output.qualityFailure = slot.qualityFailure;
                output.committedGpuBytes = slot.committedGpuBytes;
                output.replacementResourceGeneration = slot.replacementResource.GetGeneration();
                output.replacementState = slot.replacementState;
                output.replacementRetryRequired = slot.replacementRetryRequired;
                output.replacementFailure = slot.replacementFailure;
            }
            candidate = next;
        }
        return found ? TextureResidentLookupResult::Found : TextureResidentLookupResult::NotFound;
    }

    bool TextureResidencyRuntime::GetResidencyInfo(const GpuTextureResidencyHandle residency, TextureResidencyInfo& info,
                                                   TextureResidencyRuntimeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");

        u32 recordIndex = InvalidRuntimeRecordIndex;
        u32 recordGeneration = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (!residency.IsValid() || !m_impl->residencyByGpuIndex.Find(residency.index, recordIndex) || recordIndex >= m_impl->residencies.Size())
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency observation references an unknown runtime identity");
            const Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
            if (!slot.active || slot.residency != residency)
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency observation references a stale runtime identity");
            recordGeneration = slot.generation;
        }

        TextureResidencyFailure residencyFailure;
        if (!m_residency.GetInfo(residency, info, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency physical observation failed");

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            const Impl::ResidencySlot* const slot = m_impl->FindRecord(recordIndex, recordGeneration);
            if (slot == nullptr || slot->residency != residency)
            {
                info = {};
                return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture residency changed during physical observation");
            }
        }
        return true;
    }

    bool TextureResidencyRuntime::GetInfo(const GpuTextureResidencyHandle residency, TextureRuntimeInfo& info, TextureResidencyRuntimeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 recordIndex = InvalidRuntimeRecordIndex;
        if (!residency.IsValid() || !m_impl->residencyByGpuIndex.Find(residency.index, recordIndex) || recordIndex >= m_impl->residencies.Size())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture runtime query references an unknown residency identity");
        const Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
        if (!slot.active || slot.residency != residency)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture runtime query references a stale residency identity");
        info.residency = residency;
        info.resourcePath = slot.resource.GetPath();
        info.resourceGeneration = slot.resource.GetGeneration();
        info.demandCount = slot.demandCount;
        info.state = slot.state;
        info.uploadFailure = slot.uploadFailure;
        info.currentFirstResidentMip = slot.currentFirstResidentMip;
        info.desiredFirstResidentMip = slot.desiredFirstResidentMip;
        info.qualityTargetFirstResidentMip = slot.qualityTargetFirstResidentMip;
        info.demandPriority = slot.aggregatePriority;
        info.qualityState = slot.qualityState;
        info.qualitySatisfied = slot.state == TextureRuntimeState::BindlessReady && slot.qualityState == TextureQualityState::Stable &&
                                slot.currentFirstResidentMip == slot.desiredFirstResidentMip;
        info.qualityRetryRequired = slot.qualityRetryRequired;
        info.qualityFailure = slot.qualityFailure;
        info.committedGpuBytes = slot.committedGpuBytes;
        info.replacementResourceGeneration = slot.replacementResource.GetGeneration();
        info.replacementState = slot.replacementState;
        info.replacementRetryRequired = slot.replacementRetryRequired;
        info.replacementFailure = slot.replacementFailure;
        return true;
    }

    TextureResidencyRuntimeStats TextureResidencyRuntime::GetStats() const noexcept
    {
        TextureResidencyRuntimeStats stats;
        stats.residency = m_residency.GetStats();
        stats.uploader = m_uploader.GetStats();
        if (m_impl == nullptr)
            return stats;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        stats.residencyRecords = m_impl->residencyCount;
        stats.liveDemands = m_impl->demandCount;
        stats.demandsIssued = m_impl->demandsIssued;
        stats.demandsCoalesced = m_impl->demandsCoalesced;
        stats.demandsReleased = m_impl->demandsReleased;
        stats.failedResidencies = m_impl->failedResidencies;
        stats.qualityTransitionsStarted = m_impl->qualityTransitionsStarted;
        stats.qualityTransitionFailures = m_impl->qualityTransitionFailures;
        stats.qualityTransitionAdmissionDeferrals = m_impl->qualityTransitionAdmissionDeferrals;
        stats.qualityPromotionBudgetDeferrals = m_impl->qualityPromotionBudgetDeferrals;
        stats.committedTextureGpuBytes = m_impl->committedTextureGpuBytes;
        stats.reservedQualityPromotionGpuBytes = m_impl->reservedQualityPromotionGpuBytes;
        stats.peakCommittedTextureGpuBytes = m_impl->peakCommittedTextureGpuBytes;
        stats.retainedReplacementCandidateGpuBytes = m_impl->retainedReplacementCandidateGpuBytes;
        return stats;
    }

    TextureResidencyManager& TextureResidencyRuntime::GetResidencyManager() noexcept
    {
        return m_residency;
    }

    bool TextureResidencyRuntime::IsDemandValid(const TextureDemandId demand) const noexcept
    {
        if (m_impl == nullptr || !demand.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        return demand.session == m_impl->session && demand.index < m_impl->demands.Size() && m_impl->demands[demand.index].active && m_impl->demands[demand.index].generation == demand.generation;
    }

    const TextureResidencyManager& TextureResidencyRuntime::GetResidencyManager() const noexcept
    {
        return m_residency;
    }
} // namespace vanguard::rendering
