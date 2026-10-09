#pragma once

#include <vanguard/rendering/viewport.hpp>

namespace vanguard::rendering::detail
{
    // Pure receipt-transition rules shared by every ViewportManager receipt path.
    // The owning RenderSlot remains the synchronization authority and must hold its
    // outputReceiptLock across each production transition/query that uses these helpers.
    [[nodiscard]] inline bool MatchesTextureOutputReceiptEpoch(const RenderOutputReceipt& receipt, const RenderViewportHandle viewport,
                                                               const u64 outputRevision, const u64 producerSerial) noexcept
    {
        return receipt.IsValid() && receipt.viewport == viewport && receipt.outputRevision == outputRevision &&
               receipt.producerSerial == producerSerial;
    }

    [[nodiscard]] inline bool MatchesPendingTextureOutputReceiptEpoch(const RenderOutputReceipt& receipt, const RenderViewportHandle viewport,
                                                                      const u64 outputRevision, const u64 producerSerial) noexcept
    {
        return MatchesTextureOutputReceiptEpoch(receipt, viewport, outputRevision, producerSerial) &&
               receipt.state == RenderOutputReceiptState::Pending;
    }

    inline void PrearmTextureOutputReceipt(RenderOutputReceipt& receipt, const RenderViewportHandle viewport, const u64 outputRevision,
                                           const u64 producerSerial, const RenderTextureOutputContract& contract) noexcept
    {
        receipt.viewport = viewport;
        receipt.outputRevision = outputRevision;
        receipt.producerSerial = producerSerial;
        receipt.state = RenderOutputReceiptState::Pending;
        receipt.terminalState = contract.restingState;
        receipt.terminalQueue = contract.restingQueue;
        receipt.readiness = contract.publicationReadiness;
        receipt.readyFence = {};
    }

    [[nodiscard]] inline bool RestorePendingTextureOutputReceipt(RenderOutputReceipt& receipt, const RenderViewportHandle viewport,
                                                                 const u64 outputRevision, const u64 producerSerial,
                                                                 const RenderOutputReceipt& previous) noexcept
    {
        if (!MatchesPendingTextureOutputReceiptEpoch(receipt, viewport, outputRevision, producerSerial))
            return false;
        receipt = previous;
        return true;
    }

    [[nodiscard]] inline bool FailPendingTextureOutputReceipt(RenderOutputReceipt& receipt, const RenderViewportHandle viewport,
                                                              const u64 outputRevision) noexcept
    {
        if (!receipt.IsValid() || receipt.viewport != viewport || receipt.outputRevision != outputRevision ||
            receipt.state != RenderOutputReceiptState::Pending)
            return false;
        receipt.state = RenderOutputReceiptState::Failed;
        receipt.readyFence = {};
        return true;
    }

    [[nodiscard]] inline bool MatchesTextureOutputPublication(const RenderTextureOutputContract& contract,
                                                              const rhi::ResourceState terminalState, const rhi::QueueType terminalQueue,
                                                              const RenderOutputPublicationReadinessKind readiness,
                                                              const rhi::GpuFence readyFence) noexcept
    {
        return terminalState == contract.restingState && terminalQueue == contract.restingQueue &&
               readiness == contract.publicationReadiness && readyFence.IsValid() && readyFence.queue == terminalQueue;
    }

    inline void PublishReadyTextureOutputReceipt(RenderOutputReceipt& receipt, const rhi::ResourceState terminalState,
                                                 const rhi::QueueType terminalQueue,
                                                 const RenderOutputPublicationReadinessKind readiness,
                                                 const rhi::GpuFence readyFence) noexcept
    {
        // The caller has already matched the exact Pending epoch and publication contract.
        receipt.terminalState = terminalState;
        receipt.terminalQueue = terminalQueue;
        receipt.readiness = readiness;
        receipt.readyFence = readyFence;
        receipt.state = RenderOutputReceiptState::Ready;
    }
} // namespace vanguard::rendering::detail
