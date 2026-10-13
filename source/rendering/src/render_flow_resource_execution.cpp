#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ResetActiveCompiledRange(detail::PacketExecutionRuntime& runtime) noexcept
        {
            runtime.activeRangeEpoch = 0;
            runtime.activeRangeBeginStep = InvalidRenderFlowResourceIndex;
            runtime.activeRangeEndStep = InvalidRenderFlowResourceIndex;
            runtime.activeRangeEndCount = 0;
        }

        void ReleaseGenerationRuntimeOwnersInternal(ExecutionGenerationRef::Impl& generation) noexcept
        {
            for (detail::CompiledPacket& packet : generation.packets)
            {
                if (packet.runtime.liveness != nullptr)
                {
                    packet.runtime.liveness->ResetCompiledRange();
                    for (u8& active : packet.runtime.liveness->useActive)
                        active = 0;
                    detail::ReleasePacketUseLiveness(packet.runtime.liveness);
                    packet.runtime.liveness = nullptr;
                }
                packet.runtime.activeUseCount = 0;
                ResetActiveCompiledRange(packet.runtime);
            }
            generation.retainedImports.Clear();
            generation.presentationAcquisition = {};
            generation.presentationCommandScope = {};
            generation.pendingExports.Clear();
            generation.placedBatch.Clear();
            generation.physicalBindings.Clear();
        }
    } // namespace

    void detail::ReleaseGenerationRuntimeOwners(ExecutionGenerationRef::Impl* const generation) noexcept
    {
        if (generation != nullptr)
            ReleaseGenerationRuntimeOwnersInternal(*generation);
    }

    namespace
    {
        [[nodiscard]] bool PacketFailure(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message, const detail::CompiledPacket* const packet = nullptr,
                                         const ResourceUseId use = {}) noexcept
        {
            return detail::Fail(failure, code, RenderFlowResourceSessionState::Executing, message, packet != nullptr ? packet->node : RenderFlowNodeId{}, {}, use);
        }

        [[nodiscard]] detail::CompiledPacket* GetPacket(ExecutionGenerationRef::Impl* const generation, const u32 packetIndex) noexcept
        {
            return generation != nullptr && packetIndex < generation->packets.Size() ? &generation->packets[packetIndex] : nullptr;
        }

        [[nodiscard]] const detail::CompiledPacket* GetPacket(const ExecutionGenerationRef::Impl* const generation, const u32 packetIndex) noexcept
        {
            return generation != nullptr && packetIndex < generation->packets.Size() ? &generation->packets[packetIndex] : nullptr;
        }

        [[nodiscard]] bool IsActiveUse(const detail::CompiledPacket& packet, const u32 runtimeUseSlot) noexcept
        {
            return packet.runtime.liveness != nullptr && runtimeUseSlot < packet.runtime.liveness->useActive.Size() && packet.runtime.liveness->useActive[runtimeUseSlot] != 0;
        }

        [[nodiscard]] constexpr bool ValidScopeCompletion(const CommandScopeCompletionKind completion) noexcept
        {
            return completion == CommandScopeCompletionKind::Submitted || completion == CommandScopeCompletionKind::DiscardedBeforeSubmission || completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss;
        }

        [[nodiscard]] constexpr bool ValidTerminalCompletion(const TerminalExecutionCompletionKind completion) noexcept
        {
            return completion == TerminalExecutionCompletionKind::Completed || completion == TerminalExecutionCompletionKind::Aborted || completion == TerminalExecutionCompletionKind::DeviceLost;
        }

        [[nodiscard]] constexpr bool ValidQueueDependencyCompletion(const QueueDependencyCompletionKind completion) noexcept
        {
            return completion == QueueDependencyCompletionKind::Submitted || completion == QueueDependencyCompletionKind::DiscardedBeforeSubmission ||
                   completion == QueueDependencyCompletionKind::UnknownDueToDeviceLoss;
        }

        [[nodiscard]] constexpr u64 ExportSlotKey(const ExportSlotId slot) noexcept
        {
            return (static_cast<u64>(slot.generation) << 32u) | slot.index;
        }

        [[nodiscard]] bool SubmittedQueueOrderProved(const ExecutionGenerationRef::Impl& generation, const u32 producerScopeIndex, const u32 consumerScopeIndex,
                                                     const containers::DynamicArray<u32>& submittedProducerScopes) noexcept
        {
            if (producerScopeIndex >= generation.commandScopes.Size() || consumerScopeIndex >= generation.commandScopes.Size() || consumerScopeIndex >= submittedProducerScopes.Size())
                return false;
            const CompiledCommandScope& producer = generation.commandScopes[producerScopeIndex];
            const CompiledCommandScope& consumer = generation.commandScopes[consumerScopeIndex];
            const rhi::CommandListSyncType required = detail::RequiredQueueSync(producer.queue, consumer.queue);
            if (required == rhi::CommandListSyncType::None)
                return false;
            const u32 admittedProducerScopeIndex = submittedProducerScopes[consumerScopeIndex];
            return admittedProducerScopeIndex != InvalidRenderFlowResourceIndex && producerScopeIndex <= admittedProducerScopeIndex;
        }

        void FailPacketExecution(detail::CompiledPacket& packet) noexcept
        {
            packet.runtime.state = detail::PacketRuntimeState::Failed;
            packet.runtime.activeUseCount = 0;
            ResetActiveCompiledRange(packet.runtime);
            if (packet.runtime.liveness != nullptr)
            {
                packet.runtime.liveness->ResetCompiledRange();
                for (u8& active : packet.runtime.liveness->useActive)
                    active = 0;
            }
        }

        [[nodiscard]] bool RecorderMatches(const ExecutionGenerationRef::Impl& generation, const rhi::CommandListRef commandList) noexcept
        {
            return !generation.hasNativeResourceBindings || (commandList.IsValid() && rhi::GetBoundCommandList() == commandList);
        }

        [[nodiscard]] bool ResolvePacketResourceBinding(const ExecutionGenerationRef::Impl& generation, const detail::CompiledPacket& packet,
                                                        const u32 slot, const PhysicalResourceId expectedPhysical, rhi::ResourceRef& resource,
                                                        RenderFlowResourceFailure* const failure, const ResourceUseId use = {}) noexcept
        {
            resource = {};
            if (packet.resourceBindings.Size() != packet.resourceBindingPhysicals.Size() ||
                packet.resourceBindings.Size() != packet.resourceBindingShaderResources.Size() ||
                packet.resourceBindings.Size() != packet.resourceBindingUnorderedAccess.Size() || slot >= packet.resourceBindings.Size() ||
                !expectedPhysical.IsValid() || expectedPhysical.generation != generation.id.generation ||
                packet.resourceBindingPhysicals[slot] != expectedPhysical)
                return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                     "compiled packet resource binding is stale or inconsistent", &packet, use);
            resource = packet.resourceBindings[slot];
            if (!resource.IsValid() || (resource.GetKind() != rhi::ResourceKind::Texture && resource.GetKind() != rhi::ResourceKind::Buffer))
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                     "compiled packet resource binding has an invalid native kind", &packet, use);
            return true;
        }

        struct PendingCompiledBarrierRun
        {
            void Reset() noexcept
            {
                offset = InvalidRenderFlowResourceIndex;
                count = 0;
                batch = InvalidRenderFlowResourceIndex;
            }

            u32 offset = InvalidRenderFlowResourceIndex;
            u32 count = 0;
            u32 batch = InvalidRenderFlowResourceIndex;
        };

        [[nodiscard]] bool ReplayCompiledBarrier(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                 const rhi::CompiledBarrier& barrier, const ResourceUseId use,
                                                 RenderFlowResourceFailure* const failure) noexcept
        {
            if (barrier.resourceBinding >= packet.resourceBindingPhysicals.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                     "RF17 generic barrier escaped its packet binding table", &packet, use);
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(generation, packet, barrier.resourceBinding,
                                              packet.resourceBindingPhysicals[barrier.resourceBinding], resource, failure, use))
                return false;

            rhi::Failure rhiFailure;
            bool succeeded = false;
            if (barrier.kind == rhi::CompiledBarrierKind::TextureTransition && resource.GetKind() == rhi::ResourceKind::Texture)
            {
                succeeded = rhi::TransitionTexture({resource.Index(), resource.GetGeneration()}, barrier.before, barrier.after,
                                                   barrier.textureSubresources, &rhiFailure);
            }
            else if (barrier.kind == rhi::CompiledBarrierKind::BufferTransition && resource.GetKind() == rhi::ResourceKind::Buffer)
            {
                succeeded = rhi::TransitionBuffer({resource.Index(), resource.GetGeneration()}, barrier.before, barrier.after, &rhiFailure);
            }
            else if (barrier.kind == rhi::CompiledBarrierKind::TextureUav && resource.GetKind() == rhi::ResourceKind::Texture)
            {
                // Preserve RF16's exact UAV precondition on the generic path.
                // BarrierTextureUav would silently transition from a wrong state.
                // Replay every cell explicitly: once NVRHI has expanded texture
                // tracking, a whole-resource same-state request intentionally
                // emits only one UAV ordering barrier, while RF16 orders the full
                // resource. Per-cell replay preserves that stronger contract.
                const PhysicalResourceId physical = packet.resourceBindingPhysicals[barrier.resourceBinding];
                if (!physical.IsValid() || physical.generation != generation.id.generation || physical.index >= generation.physicalBindings.Size())
                    return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                         "RF17 texture UAV fallback lost its physical state descriptor", &packet, use);
                const detail::PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                if (binding.texture != rhi::TextureRef{resource.Index(), resource.GetGeneration()} || binding.buffer.IsValid() ||
                    binding.textureMipCount == 0 || binding.textureArraySize == 0)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                         "RF17 texture UAV fallback disagrees with its physical binding", &packet, use);
                succeeded = true;
                for (u32 slice = 0; succeeded && slice < binding.textureArraySize; ++slice)
                    for (u32 mip = 0; succeeded && mip < binding.textureMipCount; ++mip)
                        succeeded = rhi::TransitionTexture({resource.Index(), resource.GetGeneration()}, rhi::ResourceState::UnorderedAccess,
                                                           rhi::ResourceState::UnorderedAccess,
                                                           {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1}, &rhiFailure);
            }
            else if (barrier.kind == rhi::CompiledBarrierKind::BufferUav && resource.GetKind() == rhi::ResourceKind::Buffer)
            {
                succeeded = rhi::TransitionBuffer({resource.Index(), resource.GetGeneration()}, rhi::ResourceState::UnorderedAccess,
                                                  rhi::ResourceState::UnorderedAccess, &rhiFailure);
            }
            else
            {
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                     "RF17 generic barrier kind disagrees with its packet binding", &packet, use);
            }

            return succeeded || PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                              rhiFailure.message[0] != '\0' ? rhiFailure.message : "RF17 generic barrier replay failed", &packet, use);
        }

        [[nodiscard]] bool FlushCompiledBarrierRun(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                   PendingCompiledBarrierRun& pending, const ResourceUseId use,
                                                   RenderFlowResourceFailure* const failure) noexcept
        {
            if (pending.count == 0)
                return true;
            if (pending.offset >= packet.compiledBarriers.Size() || pending.count > packet.compiledBarriers.Size() - pending.offset ||
                pending.batch == InvalidRenderFlowResourceIndex)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                     "RF17 compiled barrier run is malformed", &packet, use);

            rhi::Failure rhiFailure;
            const containers::ArraySpan<const rhi::CompiledBarrier> barriers(&packet.compiledBarriers[pending.offset], pending.count);
            if (rhi::ExecuteCompiledBarriers(barriers, &rhiFailure))
            {
                pending.Reset();
                return true;
            }
            if (rhiFailure.code != rhi::FailureCode::Unsupported)
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     rhiFailure.message[0] != '\0' ? rhiFailure.message : "RF17 compiled barrier batch failed", &packet, use);

            // Unsupported is the only RF16 failure guaranteed non-mutating. Replay
            // the exact same independent run through the tracked generic path.
            for (u32 index = pending.offset; index < pending.offset + pending.count; ++index)
                if (!ReplayCompiledBarrier(generation, packet, packet.compiledBarriers[index], use, failure))
                    return false;
            pending.Reset();
            return true;
        }

        [[nodiscard]] bool ExecuteCompiledBarrierSpan(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                      const u32 offset, const u32 count, PendingCompiledBarrierRun& pending,
                                                      const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
        {
            if (count == 0)
                return true;
            if (packet.compiledBarriers.Size() != packet.compiledBarrierBatches.Size() || offset > packet.compiledBarriers.Size() ||
                count > packet.compiledBarriers.Size() - offset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                     "RF17 compiled barrier span is outside its packet program", &packet, use);

            for (u32 index = offset; index < offset + count; ++index)
            {
                const rhi::CompiledBarrier& barrier = packet.compiledBarriers[index];
                const u32 batch = packet.compiledBarrierBatches[index];
                if (barrier.resourceBinding >= packet.resourceBindings.Size())
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                         "RF17 compiled barrier binding is outside its packet table", &packet, use);
                if (batch == InvalidRenderFlowResourceIndex)
                {
                    if (!FlushCompiledBarrierRun(generation, packet, pending, use, failure) ||
                        !ReplayCompiledBarrier(generation, packet, barrier, use, failure))
                        return false;
                    continue;
                }
                if (pending.count != 0 && (pending.batch != batch || pending.offset + pending.count != index))
                    if (!FlushCompiledBarrierRun(generation, packet, pending, use, failure))
                        return false;
                if (pending.count == 0)
                {
                    pending.offset = index;
                    pending.count = 1;
                    pending.batch = batch;
                }
                else
                {
                    ++pending.count;
                }
            }
            return true;
        }

        [[nodiscard]] bool ExecuteActions(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const u32 offset, const u32 count,
                                          const ResourceUseId use, PendingCompiledBarrierRun& pending,
                                          RenderFlowResourceFailure* const failure, const bool scopeExit = false) noexcept
        {
            const auto& actions = scopeExit ? packet.exitActions : packet.actions;
            if (offset > actions.Size() || count > actions.Size() - offset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled resource-action range is invalid", &packet, use);
            if (!generation.hasNativeResourceBindings)
                return true;
            for (u32 index = offset; index < offset + count; ++index)
            {
                const detail::CompiledResourceAction& action = actions[index];
                const bool barrierAction = action.kind == detail::CompiledResourceActionKind::TextureTransition ||
                                           action.kind == detail::CompiledResourceActionKind::BufferTransition ||
                                           action.kind == detail::CompiledResourceActionKind::TextureUavBarrier ||
                                           action.kind == detail::CompiledResourceActionKind::BufferUavBarrier;
                if (barrierAction)
                {
                    if ((action.kind == detail::CompiledResourceActionKind::TextureUavBarrier || action.kind == detail::CompiledResourceActionKind::BufferUavBarrier) &&
                        action.compiledBarrierCount == 0)
                        return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                             "RF17 UAV action has no compiled barrier", &packet, use);
                    if (!ExecuteCompiledBarrierSpan(generation, packet, action.compiledBarrierOffset, action.compiledBarrierCount, pending, use, failure))
                        return false;
                    continue;
                }

                if (action.compiledBarrierCount != 0)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                         "RF17 non-barrier action owns compiled barrier metadata", &packet, use);
                if (!FlushCompiledBarrierRun(generation, packet, pending, use, failure))
                    return false;
                rhi::ResourceRef resource;
                if (!ResolvePacketResourceBinding(generation, packet, action.resourceBinding, action.physical, resource, failure, use))
                    return false;
                const bool texture = resource.GetKind() == rhi::ResourceKind::Texture;
                const rhi::TextureRef textureRef = texture ? rhi::TextureRef{resource.Index(), resource.GetGeneration()} : rhi::TextureRef{};
                const rhi::BufferRef bufferRef = !texture ? rhi::BufferRef{resource.Index(), resource.GetGeneration()} : rhi::BufferRef{};
                rhi::Failure rhiFailure;
                bool succeeded = false;
                if (action.kind == detail::CompiledResourceActionKind::TextureColorTargetClear && texture)
                    succeeded = rhi::ClearColorTarget(textureRef, action.textureClearValue.color, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureDepthClear && texture)
                    succeeded = rhi::ClearDepthTarget(textureRef, action.textureClearValue.depth, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureStencilClear && texture)
                    succeeded = rhi::ClearStencilTarget(textureRef, action.textureClearValue.stencil, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureDepthStencilClear && texture)
                    succeeded = rhi::ClearDepthStencilTarget(textureRef, action.textureClearValue.depth, action.textureClearValue.stencil, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureUavFloatClear && texture)
                    succeeded = rhi::ClearTextureUav(textureRef, action.textureClearValue.color, action.textureSubresources, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureUavUintClear && texture)
                    succeeded = rhi::ClearTextureUav(textureRef, action.textureClearValue.uintValue, action.textureSubresources, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::BufferUavUintClear && !texture)
                    succeeded = rhi::ClearBufferUav(bufferRef, action.bufferClearValue.value, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::SwapChainPresentTransition && texture && action.physical.index < generation.physicalBindings.Size())
                {
                    const detail::PhysicalBindingRecord& binding = generation.physicalBindings[action.physical.index];
                    if (binding.kind != detail::PhysicalBindingKind::RetainedImport || binding.texture != textureRef || binding.buffer.IsValid() ||
                        binding.retainedImport >= generation.retainedImports.Size())
                        return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "compiled presentation action disagrees with its retained physical binding", &packet, use);
                    const detail::RetainedImportRecord& imported = generation.retainedImports[binding.retainedImport];
                    if (!imported.presentationAcquisition.IsValid() || imported.presentationAcquisition.texture != binding.texture ||
                        imported.presentationAcquisition.swapChain != generation.presentationAcquisition.swapChain ||
                        imported.presentationAcquisition.serial != generation.presentationAcquisition.serial)
                        return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "compiled presentation action disagrees with its retained acquisition", &packet, use);
                    succeeded = rhi::TransitionSwapChainPresent(imported.presentationAcquisition, &rhiFailure);
                }
                else
                    return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled resource action kind disagrees with its physical assignment", &packet, use);
                if (!succeeded)
                    return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "compiled resource action failed", &packet, use);
            }
            return true;
        }

        [[nodiscard]] bool ExecuteAliasActivation(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const detail::CompiledStep& step, const ResourceUseId use,
                                                  RenderFlowResourceFailure* const failure) noexcept
        {
            if (step.aliasPredecessorCount == 0)
                return true;
            if (step.aliasPredecessorOffset > packet.aliasPredecessors.Size() || step.aliasPredecessorCount > packet.aliasPredecessors.Size() - step.aliasPredecessorOffset ||
                packet.aliasPredecessorBindingSlots.Size() != packet.aliasPredecessors.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled alias-activation range is invalid", &packet, use);
            rhi::ResourceRef destination;
            if (!ResolvePacketResourceBinding(generation, packet, step.resourceBinding, step.physical, destination, failure, use))
                return false;
            for (u32 index = 0; index < step.aliasPredecessorCount; ++index)
            {
                const u32 aliasIndex = step.aliasPredecessorOffset + index;
                const u32 slot = packet.aliasPredecessorBindingSlots[aliasIndex];
                if (slot >= packet.resourceBindings.Size() || packet.resourceBindings[slot] != packet.aliasPredecessors[aliasIndex])
                    return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled alias activation contains an invalid predecessor", &packet, use);
            }
            rhi::Failure rhiFailure;
            if (!rhi::ActivateAliasedResource(destination, containers::ArraySpan<const rhi::ResourceRef>(&packet.aliasPredecessors[step.aliasPredecessorOffset], step.aliasPredecessorCount), &rhiFailure))
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     "placed alias activation failed", &packet, use);
            return true;
        }

        [[nodiscard]] bool FinalizeAliasPredecessor(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const detail::CompiledStep& step, const ResourceUseId use,
                                                    RenderFlowResourceFailure* const failure) noexcept
        {
            if (!step.finalizeForAlias)
                return true;
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(generation, packet, step.resourceBinding, step.physical, resource, failure, use))
                return false;
            rhi::Failure rhiFailure;
            const bool finalized = resource.GetKind() == rhi::ResourceKind::Texture
                                       ? rhi::MakeStateSafeToRetire(rhi::TextureRef{resource.Index(), resource.GetGeneration()}, &rhiFailure)
                                       : rhi::MakeStateSafeToRetire(rhi::BufferRef{resource.Index(), resource.GetGeneration()}, &rhiFailure);
            if (!finalized)
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     "placed alias predecessor finalization failed", &packet, use);
            return true;
        }

        [[nodiscard]] bool ValidateCompiledExecutionRange(const detail::CompiledPacket& packet, const CompiledExecutionRange& range, u32& afterEndStep,
                                                          RenderFlowResourceFailure* const failure) noexcept
        {
            afterEndStep = InvalidRenderFlowResourceIndex;
            if (range.beginStepCount != range.endStepCount || range.beginStepOffset > packet.steps.Size() ||
                range.beginStepCount > packet.steps.Size() - range.beginStepOffset || range.endStepOffset > packet.steps.Size() ||
                range.endStepCount > packet.steps.Size() - range.endStepOffset || range.endStepOffset != range.beginStepOffset + range.beginStepCount)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled execution range has invalid step boundaries", &packet);

            afterEndStep = range.endStepOffset + range.endStepCount;
            if (range.beginActions.offset > packet.actionTape.Size() || range.beginActions.count > packet.actionTape.Size() - range.beginActions.offset ||
                range.endActions.offset > packet.actionTape.Size() || range.endActions.count > packet.actionTape.Size() - range.endActions.offset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled execution range has invalid action-tape boundaries", &packet);

            const auto tapeBoundary = [&](const u32 stepIndex, u32& offset) noexcept -> bool
            {
                if (stepIndex == packet.steps.Size())
                {
                    offset = packet.exitActionTapeOffset;
                    return offset <= packet.actionTape.Size();
                }
                if (stepIndex > packet.steps.Size())
                    return false;
                offset = packet.steps[stepIndex].actionTapeOffset;
                return offset <= packet.actionTape.Size();
            };

            u32 beginActionOffset = 0;
            u32 endActionOffset = 0;
            u32 afterEndActionOffset = 0;
            if (!tapeBoundary(range.beginStepOffset, beginActionOffset) || !tapeBoundary(range.endStepOffset, endActionOffset) ||
                !tapeBoundary(afterEndStep, afterEndActionOffset) || endActionOffset < beginActionOffset || afterEndActionOffset < endActionOffset ||
                range.beginActions.offset != beginActionOffset || range.beginActions.count != endActionOffset - beginActionOffset ||
                range.endActions.offset != endActionOffset || range.endActions.count != afterEndActionOffset - endActionOffset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled execution range disagrees with packet action-tape topology", &packet);

            if (range.beginStepCount == 0)
                return true;

            const detail::CompiledStep& firstBegin = packet.steps[range.beginStepOffset];
            const detail::CompiledStep& lastBegin = packet.steps[range.endStepOffset - 1u];
            const detail::CompiledStep& firstEnd = packet.steps[range.endStepOffset];
            const detail::CompiledStep& lastEnd = packet.steps[afterEndStep - 1u];
            const bool firstBeginKind = firstBegin.kind == CompiledResourceStepKind::TextureUseBegin || firstBegin.kind == CompiledResourceStepKind::BufferUseBegin;
            const bool lastBeginKind = lastBegin.kind == CompiledResourceStepKind::TextureUseBegin || lastBegin.kind == CompiledResourceStepKind::BufferUseBegin;
            if (!firstBeginKind || !lastBeginKind || firstEnd.kind != CompiledResourceStepKind::UseEnd || lastEnd.kind != CompiledResourceStepKind::UseEnd ||
                !firstBegin.use.IsValid() || firstBegin.use.flowGroup != packet.flowGroup || lastBegin.use.flowGroup != packet.flowGroup ||
                firstBegin.use.generation != lastBegin.use.generation ||
                static_cast<u64>(lastBegin.use.ordinal) != static_cast<u64>(firstBegin.use.ordinal) + range.beginStepCount - 1u ||
                firstEnd.use != lastBegin.use || lastEnd.use != firstBegin.use)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled execution range has invalid dense begin/end identity boundaries", &packet,
                                     firstBegin.use);
            return true;
        }

        [[nodiscard]] bool ExecuteCompiledRangeTape(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                    const CompiledExecutionActionSegment segment, const detail::CompiledActionTapePhase phase,
                                                    const u32 stepOffset, const u32 stepCount, RenderFlowResourceFailure* const failure) noexcept
        {
            if (segment.offset > packet.actionTape.Size() || segment.count > packet.actionTape.Size() - segment.offset ||
                stepOffset > packet.steps.Size() || stepCount > packet.steps.Size() - stepOffset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled range action-tape segment is invalid", &packet);
            PendingCompiledBarrierRun pending;
            for (u32 tapeIndex = segment.offset; tapeIndex < segment.offset + segment.count; ++tapeIndex)
            {
                const detail::CompiledActionTapeEntry& entry = packet.actionTape[tapeIndex];
                if (entry.phase != phase || entry.stepIndex < stepOffset || entry.stepIndex >= stepOffset + stepCount)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled range action-tape entry is outside its child step span", &packet);
                const detail::CompiledStep& step = packet.steps[entry.stepIndex];
                if (step.actionTapeCount != 1 || step.actionTapeOffset != tapeIndex)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled range action-tape entry disagrees with its step", &packet, step.use);

                if (phase == detail::CompiledActionTapePhase::Begin)
                {
                    if ((step.kind != CompiledResourceStepKind::TextureUseBegin && step.kind != CompiledResourceStepKind::BufferUseBegin) ||
                        entry.actionOffset != step.beforeActionOffset || entry.actionCount != step.beforeActionCount)
                        return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                             "compiled begin action-tape entry disagrees with its step semantics", &packet, step.use);
                    if ((step.aliasPredecessorCount != 0 && !FlushCompiledBarrierRun(generation, packet, pending, step.use, failure)) ||
                        !ExecuteAliasActivation(generation, packet, step, step.use, failure) ||
                        !ExecuteCompiledBarrierSpan(generation, packet, step.compiledBarrierOffset, step.compiledBarrierCount, pending, step.use, failure) ||
                        !ExecuteActions(generation, packet, entry.actionOffset, entry.actionCount, step.use, pending, failure))
                        return false;
                }
                else if (phase == detail::CompiledActionTapePhase::End)
                {
                    if (step.kind != CompiledResourceStepKind::UseEnd || entry.actionOffset != step.afterActionOffset || entry.actionCount != step.afterActionCount)
                        return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                             "compiled end action-tape entry disagrees with its step semantics", &packet, step.use);
                    if (!ExecuteActions(generation, packet, entry.actionOffset, entry.actionCount, step.use, pending, failure) ||
                        (step.finalizeForAlias && !FlushCompiledBarrierRun(generation, packet, pending, step.use, failure)) ||
                        !FinalizeAliasPredecessor(generation, packet, step, step.use, failure))
                        return false;
                }
                else
                {
                    return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled child range contains a packet-exit action-tape entry", &packet,
                                         step.use);
                }
            }
            return FlushCompiledBarrierRun(generation, packet, pending, {}, failure);
        }

        [[nodiscard]] bool ExecuteCompiledRangeBegins(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                      const u32 stepOffset, const u32 stepCount,
                                                      RenderFlowResourceFailure* const failure) noexcept
        {
            if (stepOffset > packet.steps.Size() || stepCount > packet.steps.Size() - stepOffset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                     "compiled range begin span is invalid", &packet);
            PendingCompiledBarrierRun pending;
            for (u32 stepIndex = stepOffset; stepIndex < stepOffset + stepCount; ++stepIndex)
            {
                const detail::CompiledStep& step = packet.steps[stepIndex];
                if (step.kind != CompiledResourceStepKind::TextureUseBegin && step.kind != CompiledResourceStepKind::BufferUseBegin)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                         "compiled range begin span contains a non-begin step", &packet, step.use);
                if ((step.aliasPredecessorCount != 0 && !FlushCompiledBarrierRun(generation, packet, pending, step.use, failure)) ||
                    !ExecuteAliasActivation(generation, packet, step, step.use, failure) ||
                    !ExecuteCompiledBarrierSpan(generation, packet, step.compiledBarrierOffset, step.compiledBarrierCount, pending, step.use, failure) ||
                    !ExecuteActions(generation, packet, step.beforeActionOffset, step.beforeActionCount, step.use, pending, failure))
                    return false;
            }
            return FlushCompiledBarrierRun(generation, packet, pending, {}, failure);
        }

        [[nodiscard]] bool ExecutePacketExitTape(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                 RenderFlowResourceFailure* const failure) noexcept
        {
            if (packet.exitActionTapeOffset > packet.actionTape.Size() || packet.exitActionTapeCount > packet.actionTape.Size() - packet.exitActionTapeOffset ||
                packet.exitActionTapeOffset + packet.exitActionTapeCount != packet.actionTape.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled packet-exit action tape is malformed", &packet);
            if (packet.exitActions.Empty())
            {
                if (packet.exitActionTapeCount != 0)
                    return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "empty packet exit unexpectedly owns an action-tape entry", &packet);
                return true;
            }
            if (packet.exitActionTapeCount != 1)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "packet exit must own exactly one semantic action-tape entry", &packet);
            const detail::CompiledActionTapeEntry& entry = packet.actionTape[packet.exitActionTapeOffset];
            if (entry.phase != detail::CompiledActionTapePhase::Exit || entry.stepIndex != InvalidRenderFlowResourceIndex || entry.actionOffset != 0 ||
                entry.actionCount != packet.exitActions.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled packet-exit action-tape entry disagrees with exit actions", &packet);
            PendingCompiledBarrierRun pending;
            if (!ExecuteActions(generation, packet, entry.actionOffset, entry.actionCount, {}, pending, failure, true))
                return false;
            return FlushCompiledBarrierRun(generation, packet, pending, {}, failure);
        }

        [[nodiscard]] bool ExecuteCompiledUseBegin(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                   const detail::CompiledStep& step, const ResourceUseId use,
                                                   RenderFlowResourceFailure* const failure) noexcept
        {
            PendingCompiledBarrierRun pending;
            if (!ExecuteAliasActivation(generation, packet, step, use, failure) ||
                !ExecuteCompiledBarrierSpan(generation, packet, step.compiledBarrierOffset, step.compiledBarrierCount, pending, use, failure) ||
                !ExecuteActions(generation, packet, step.beforeActionOffset, step.beforeActionCount, use, pending, failure))
                return false;
            return FlushCompiledBarrierRun(generation, packet, pending, use, failure);
        }

        [[nodiscard]] bool ExecuteCompiledUseEnd(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet,
                                                 const detail::CompiledStep& step, const ResourceUseId use,
                                                 RenderFlowResourceFailure* const failure) noexcept
        {
            PendingCompiledBarrierRun pending;
            if (!ExecuteActions(generation, packet, step.afterActionOffset, step.afterActionCount, use, pending, failure) ||
                (step.finalizeForAlias && !FlushCompiledBarrierRun(generation, packet, pending, use, failure)) ||
                !FinalizeAliasPredecessor(generation, packet, step, use, failure))
                return false;
            return FlushCompiledBarrierRun(generation, packet, pending, use, failure);
        }

    } // namespace

    namespace detail
    {
        PacketUseLiveness* AllocatePacketUseLiveness() noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(PacketUseLiveness), alignof(PacketUseLiveness));
            return block ? new (block.address) PacketUseLiveness() : nullptr;
        }

        void RetainPacketUseLiveness(PacketUseLiveness* const liveness) noexcept
        {
            if (liveness != nullptr)
                static_cast<void>(liveness->references.Increment());
        }

        void ReleasePacketUseLiveness(PacketUseLiveness* const liveness) noexcept
        {
            if (liveness == nullptr || liveness->references.Decrement() != 0)
                return;
            liveness->~PacketUseLiveness();
            memory::MemoryBlock block{liveness, sizeof(PacketUseLiveness), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void RetainGeneration(ExecutionGenerationRef::Impl* const generation) noexcept
        {
            if (generation != nullptr)
                static_cast<void>(generation->references.Increment());
        }

        void ReleaseGeneration(ExecutionGenerationRef::Impl* const generation) noexcept
        {
            if (generation == nullptr || generation->references.Decrement() != 0)
                return;
            generation->~Impl();
            memory::MemoryBlock block{generation, sizeof(ExecutionGenerationRef::Impl), memory::PoolId::Rendering};
            memory::Free(block);
        }
    } // namespace detail

    bool PublishedResourceExport::IsValid() const noexcept
    {
        return m_slot.IsValid() && (m_texture.IsValid() != m_buffer.IsValid());
    }

    FrameResourceKind PublishedResourceExport::GetKind() const noexcept
    {
        return m_kind;
    }

    ExportSlotId PublishedResourceExport::GetSlot() const noexcept
    {
        return m_slot;
    }

    rhi::TextureRef PublishedResourceExport::GetTexture() const noexcept
    {
        return IsValid() && m_kind == FrameResourceKind::Texture ? m_texture.GetRef() : rhi::TextureRef{};
    }

    rhi::BufferRef PublishedResourceExport::GetBuffer() const noexcept
    {
        return IsValid() && m_kind == FrameResourceKind::Buffer ? m_buffer.GetRef() : rhi::BufferRef{};
    }

    const rhi::TextureDesc& PublishedResourceExport::GetTextureDesc() const noexcept
    {
        return m_textureDesc;
    }

    const rhi::BufferDesc& PublishedResourceExport::GetBufferDesc() const noexcept
    {
        return m_bufferDesc;
    }

    rhi::ResourceState PublishedResourceExport::GetTerminalState() const noexcept
    {
        return m_terminalState;
    }

    rhi::QueueType PublishedResourceExport::GetTerminalQueue() const noexcept
    {
        return m_terminalQueue;
    }

    ExportReadinessKind PublishedResourceExport::GetReadiness() const noexcept
    {
        return m_readiness;
    }

    rhi::GpuFence PublishedResourceExport::GetReadyFence() const noexcept
    {
        return m_readyFence;
    }

    void PublishedResourceExport::Reset() noexcept
    {
        m_slot = {};
        m_texture.Reset();
        m_buffer.Reset();
        m_textureDesc = {};
        m_bufferDesc = {};
        m_terminalState = rhi::ResourceState::Unknown;
        m_terminalQueue = rhi::QueueType::Graphics;
        m_readiness = ExportReadinessKind::SameQueueContinuation;
        m_readyFence = {};
    }

    bool TerminalJoinToken::FromReadyCounter(const jobs::Counter& counter, const ExecutionGenerationId generation, TerminalJoinToken& token) noexcept
    {
        token = TerminalJoinToken({}, false);
        if (!generation.IsValid() || !counter.IsValid() || !counter.IsReady())
            return false;
        token = TerminalJoinToken(generation, true);
        return true;
    }

    ExecutionGenerationRef::ExecutionGenerationRef(Impl* const impl) noexcept : m_impl(impl)
    {
        detail::RetainGeneration(m_impl);
    }

    ExecutionGenerationRef::~ExecutionGenerationRef()
    {
        Reset();
    }

    ExecutionGenerationRef::ExecutionGenerationRef(const ExecutionGenerationRef& other) noexcept : m_impl(other.m_impl)
    {
        detail::RetainGeneration(m_impl);
    }

    ExecutionGenerationRef& ExecutionGenerationRef::operator=(const ExecutionGenerationRef& other) noexcept
    {
        if (this == &other)
            return *this;
        Impl* const replacement = other.m_impl;
        detail::RetainGeneration(replacement);
        detail::ReleaseGeneration(m_impl);
        m_impl = replacement;
        return *this;
    }

    ExecutionGenerationRef::ExecutionGenerationRef(ExecutionGenerationRef&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    ExecutionGenerationRef& ExecutionGenerationRef::operator=(ExecutionGenerationRef&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleaseGeneration(m_impl);
        m_impl = other.m_impl;
        other.m_impl = nullptr;
        return *this;
    }

    bool ExecutionGenerationRef::IsValid() const noexcept
    {
        return m_impl != nullptr && m_impl->id.IsValid();
    }

    ExecutionGenerationId ExecutionGenerationRef::GetId() const noexcept
    {
        return m_impl != nullptr ? m_impl->id : ExecutionGenerationId{};
    }

    void ExecutionGenerationRef::Reset() noexcept
    {
        Impl* const value = m_impl;
        m_impl = nullptr;
        detail::ReleaseGeneration(value);
    }

    bool CompiledExecutionPacketView::IsValid() const noexcept
    {
        return m_generation.IsValid() && m_packetIndex < m_generation.m_impl->packets.Size();
    }

    ExecutionGenerationId CompiledExecutionPacketView::GetGeneration() const noexcept
    {
        return m_generation.GetId();
    }

    RenderFlowNodeId CompiledExecutionPacketView::GetNode() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->node : RenderFlowNodeId{};
    }

    GpuFlowGroupId CompiledExecutionPacketView::GetFlowGroup() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->flowGroup : GpuFlowGroupId{};
    }

    CommandScopeId CompiledExecutionPacketView::GetCommandScope() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->commandScope : CommandScopeId{};
    }

    rhi::QueueType CompiledExecutionPacketView::GetQueue() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->queue : rhi::QueueType::Graphics;
    }

    u32 CompiledExecutionPacketView::GetStepCount() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->steps.Size() : 0;
    }

    u32 CompiledExecutionPacketView::GetUseCount() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->beginStepByUseOrdinal.Size() : 0;
    }

    u32 CompiledExecutionPacketView::GetDecisionCount() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->decisions.Size() : 0;
    }

    CompiledExecutionActionSegment CompiledExecutionPacketView::GetExitActionSegment() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? CompiledExecutionActionSegment{packet->exitActionTapeOffset, packet->exitActionTapeCount}
                                 : CompiledExecutionActionSegment{};
    }

    bool CompiledExecutionPacketView::ResolveExecutionRange(const u32 firstUseOrdinal, const u32 useCount, CompiledExecutionRange& range,
                                                            RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        range = {};
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution range packet is invalid");

        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        const detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        range.generation = generation->id.generation;
        if (generation->terminal.GetValue())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution range packet generation is terminal", &packet);
        if (packet.beginStepByUseOrdinal.Size() != packet.endStepByUseOrdinal.Size() ||
            firstUseOrdinal > packet.beginStepByUseOrdinal.Size() || useCount > packet.beginStepByUseOrdinal.Size() - firstUseOrdinal)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution range use ordinals are outside the compiled packet", &packet);

        const auto tapeBoundary = [&](const u32 stepIndex, u32& offset) noexcept -> bool
        {
            if (stepIndex == packet.steps.Size())
            {
                offset = packet.exitActionTapeOffset;
                return offset <= packet.actionTape.Size();
            }
            if (stepIndex >= packet.steps.Size())
                return false;
            offset = packet.steps[stepIndex].actionTapeOffset;
            return offset <= packet.actionTape.Size();
        };

        if (useCount == 0)
        {
            const u32 stepBoundary = firstUseOrdinal < packet.beginStepByUseOrdinal.Size()
                                       ? packet.beginStepByUseOrdinal[firstUseOrdinal]
                                       : packet.steps.Size();
            u32 actionBoundary = 0;
            if (!tapeBoundary(stepBoundary, actionBoundary))
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "empty execution range has an invalid packet boundary", &packet);
            range.beginStepOffset = stepBoundary;
            range.endStepOffset = stepBoundary;
            range.beginActions.offset = actionBoundary;
            range.endActions.offset = actionBoundary;
            return true;
        }

        const u32 firstBeginStep = packet.beginStepByUseOrdinal[firstUseOrdinal];
        const u32 lastUseOrdinal = firstUseOrdinal + useCount - 1u;
        const u32 lastBeginStep = packet.beginStepByUseOrdinal[lastUseOrdinal];
        const u32 firstEndStep = packet.endStepByUseOrdinal[lastUseOrdinal];
        const u32 lastEndStep = packet.endStepByUseOrdinal[firstUseOrdinal];
        if (firstBeginStep >= packet.steps.Size() || firstEndStep != firstBeginStep + useCount ||
            useCount > packet.steps.Size() - firstEndStep || lastBeginStep != firstBeginStep + useCount - 1u ||
            lastEndStep != firstEndStep + useCount - 1u)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution range step spans are not contiguous", &packet);
        const detail::CompiledStep& firstBegin = packet.steps[firstBeginStep];
        const detail::CompiledStep& lastBegin = packet.steps[lastBeginStep];
        const detail::CompiledStep& firstEnd = packet.steps[firstEndStep];
        const detail::CompiledStep& lastEnd = packet.steps[lastEndStep];
        const ResourceUseId firstUse = firstBegin.use;
        if (!firstUse.IsValid() || firstUse.flowGroup != packet.flowGroup || firstUse.ordinal != firstUseOrdinal ||
            (firstBegin.kind != CompiledResourceStepKind::TextureUseBegin && firstBegin.kind != CompiledResourceStepKind::BufferUseBegin))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution range first step has an invalid planning-use identity", &packet, firstUse);
        // Dense ordinal topology is sealed while the packet steps are created (or
        // copied from the structural plan), so range publication only proves the
        // two immutable boundaries instead of replaying every use. ResourceUseId is
        // a planning/session identity and intentionally keeps that generation even
        // when the published ExecutionGenerationId differs after a rejected Resolve.
        const ResourceUseId lastUse{packet.flowGroup, lastUseOrdinal, firstUse.generation};
        if ((lastBegin.kind != CompiledResourceStepKind::TextureUseBegin && lastBegin.kind != CompiledResourceStepKind::BufferUseBegin) ||
            firstEnd.kind != CompiledResourceStepKind::UseEnd || lastEnd.kind != CompiledResourceStepKind::UseEnd ||
            lastBegin.use != lastUse || firstEnd.use != lastUse || lastEnd.use != firstUse)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                 "execution range boundaries disagree with the compiled dense use topology", &packet, lastUse);

        const u32 afterEndStep = firstEndStep + useCount;
        if ((firstUseOrdinal + useCount < packet.beginStepByUseOrdinal.Size() &&
             packet.beginStepByUseOrdinal[firstUseOrdinal + useCount] != afterEndStep) ||
            (firstUseOrdinal + useCount == packet.beginStepByUseOrdinal.Size() && afterEndStep != packet.steps.Size()))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution range does not end at the next dense packet boundary", &packet);

        u32 beginActionOffset = 0;
        u32 endActionOffset = 0;
        u32 afterEndActionOffset = 0;
        if (!tapeBoundary(firstBeginStep, beginActionOffset) || !tapeBoundary(firstEndStep, endActionOffset) ||
            !tapeBoundary(afterEndStep, afterEndActionOffset) ||
            endActionOffset < beginActionOffset || afterEndActionOffset < endActionOffset)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution range action tape boundaries are invalid", &packet);

        range.beginStepOffset = firstBeginStep;
        range.beginStepCount = useCount;
        range.endStepOffset = firstEndStep;
        range.endStepCount = useCount;
        range.beginActions = {beginActionOffset, endActionOffset - beginActionOffset};
        range.endActions = {endActionOffset, afterEndActionOffset - endActionOffset};
        return true;
    }

    bool CompiledExecutionPacketView::ResolveTextureUseBinding(const ResourceUseId use, const u32 rangeEpoch, ResolvedTextureUse& resolved,
                                                               RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || rangeEpoch == 0 || !use.IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture execution-image binding identity is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (generation->terminal.GetValue() || use.flowGroup != packet.flowGroup || use.ordinal >= packet.beginStepByUseOrdinal.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture use does not belong to this execution packet", &packet, use);
        const u32 stepIndex = packet.beginStepByUseOrdinal[use.ordinal];
        if (stepIndex >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use has no compiled begin step", &packet, use);
        const detail::CompiledStep& step = packet.steps[stepIndex];
        if (step.kind != CompiledResourceStepKind::TextureUseBegin || step.use != use || !step.physical.IsValid() ||
            step.physical.generation != generation->id.generation || packet.runtime.liveness == nullptr ||
            packet.runtime.liveness->generation != generation->id || packet.runtime.liveness->packetIndex != m_packetIndex)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture execution-image binding disagrees with the compiled packet", &packet, use);

        ResolvedTextureUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_use = use;
        value.m_physical = step.physical;
        value.m_desc = step.texture;
        value.m_viewDesc = step.textureView;
        value.m_generation = generation->id;
        value.m_packetIndex = m_packetIndex;
        value.m_rangeEpoch = rangeEpoch;
        value.m_hasExplicitView = step.hasExplicitView;
        if (generation->hasNativeResourceBindings)
        {
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(*generation, packet, step.resourceBinding, step.physical, resource, failure, use))
                return false;
            if (resource.GetKind() != rhi::ResourceKind::Texture)
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "texture execution-image packet binding has the wrong kind", &packet, use);
            value.m_texture = {resource.Index(), resource.GetGeneration()};
            value.m_shaderResource = packet.resourceBindingShaderResources[step.resourceBinding];
            value.m_unorderedAccess = packet.resourceBindingUnorderedAccess[step.resourceBinding];
        }
        resolved = static_cast<ResolvedTextureUse&&>(value);
        return true;
    }

    bool CompiledExecutionPacketView::ResolveBufferUseBinding(const ResourceUseId use, const u32 rangeEpoch, ResolvedBufferUse& resolved,
                                                              RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || rangeEpoch == 0 || !use.IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer execution-image binding identity is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (generation->terminal.GetValue() || use.flowGroup != packet.flowGroup || use.ordinal >= packet.beginStepByUseOrdinal.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer use does not belong to this execution packet", &packet, use);
        const u32 stepIndex = packet.beginStepByUseOrdinal[use.ordinal];
        if (stepIndex >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use has no compiled begin step", &packet, use);
        const detail::CompiledStep& step = packet.steps[stepIndex];
        if (step.kind != CompiledResourceStepKind::BufferUseBegin || step.use != use || !step.physical.IsValid() ||
            step.physical.generation != generation->id.generation || packet.runtime.liveness == nullptr ||
            packet.runtime.liveness->generation != generation->id || packet.runtime.liveness->packetIndex != m_packetIndex)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer execution-image binding disagrees with the compiled packet", &packet, use);

        ResolvedBufferUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_use = use;
        value.m_physical = step.physical;
        value.m_desc = step.buffer;
        value.m_viewDesc = step.bufferView;
        value.m_generation = generation->id;
        value.m_packetIndex = m_packetIndex;
        value.m_rangeEpoch = rangeEpoch;
        value.m_hasExplicitView = step.hasExplicitView;
        if (generation->hasNativeResourceBindings)
        {
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(*generation, packet, step.resourceBinding, step.physical, resource, failure, use))
                return false;
            if (resource.GetKind() != rhi::ResourceKind::Buffer)
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "buffer execution-image packet binding has the wrong kind", &packet, use);
            value.m_buffer = {resource.Index(), resource.GetGeneration()};
            value.m_shaderResource = packet.resourceBindingShaderResources[step.resourceBinding];
            value.m_unorderedAccess = packet.resourceBindingUnorderedAccess[step.resourceBinding];
        }
        resolved = static_cast<ResolvedBufferUse&&>(value);
        return true;
    }

    bool CompiledExecutionPacketView::OpenCursor(const CommandScopeId activeScope, const rhi::QueueType activeQueue, ExecutionPacketCursor& cursor, RenderFlowResourceFailure* const failure) const noexcept
    {
        return OpenCursorInternal(activeScope, activeQueue, InvalidRenderFlowResourceIndex, false, cursor, failure);
    }

    bool CompiledExecutionPacketView::OpenCompiledCursor(const CommandScopeId activeScope, const rhi::QueueType activeQueue, const u32 expectedRangeCount,
                                                         ExecutionPacketCursor& cursor, RenderFlowResourceFailure* const failure) const noexcept
    {
        return OpenCursorInternal(activeScope, activeQueue, expectedRangeCount, true, cursor, failure);
    }

    bool CompiledExecutionPacketView::OpenCursorInternal(const CommandScopeId activeScope, const rhi::QueueType activeQueue, const u32 expectedRangeCount,
                                                         const bool compiledRanges, ExecutionPacketCursor& cursor, RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || cursor.IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution packet or output cursor is invalid");
        if (compiledRanges && expectedRangeCount == InvalidRenderFlowResourceIndex)
            return PacketFailure(failure, RenderFlowResourceFailureCode::CapacityExceeded, "compiled execution range count exceeds addressable capacity");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        // The scheduled occurrence owns this packet exclusively. Teardown follows
        // the complete recording join, so command-list seeding needs no lifecycle lock.
        if (packet.commandScope != activeScope || packet.queue != activeQueue)
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active recorder queue or command scope does not match the compiled packet", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Unclaimed)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution packet has already been claimed or generation is terminal", &packet);
        rhi::CommandListRef commandList;
        if (generation->hasNativeResourceBindings)
        {
            commandList = rhi::GetBoundCommandList();
            if (!commandList.IsValid() || rhi::GetQueueType(rhi::GetBoundCommandListType()) != activeQueue)
                return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "physical execution requires a bound command list on the packet's compiled queue", &packet);
            rhi::Failure rhiFailure;
            for (u32 producer = 0; producer < 3; ++producer)
            {
                if (packet.incomingWaits[producer] == 0)
                    continue;
                const bool waitRecorded = rhi::AddCommandListWait({static_cast<rhi::QueueType>(producer), packet.incomingWaits[producer]}, &rhiFailure);
                if (!waitRecorded)
                {
                    FailPacketExecution(packet);
                    return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction), "incoming queue wait recording failed; discard the command list", &packet);
                }
            }
            const bool resourcesAdmitted = rhi::AdmitCommandListResources(containers::ArraySpan<const rhi::ResourceRef>(packet.resourceBindings), &rhiFailure);
            if (!resourcesAdmitted)
            {
                FailPacketExecution(packet);
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     "command-scope resource-table admission failed; discard the command list", &packet);
            }
            const bool statesSeeded = rhi::SeedCommandListStates(containers::ArraySpan<const rhi::CommandListEntryState>(packet.entryStates), &rhiFailure);
            if (!statesSeeded)
            {
                FailPacketExecution(packet);
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction), "command-scope entry-state seeding failed; discard the command list", &packet);
            }
        }
        packet.runtime.state = detail::PacketRuntimeState::Executing;
        packet.runtime.mode = compiledRanges ? detail::PacketExecutionMode::CompiledRanges : detail::PacketExecutionMode::Undecided;
        packet.runtime.nextStep = 0;
        packet.runtime.activeUseCount = 0;
        packet.runtime.lastCompletedRangeEpoch = 0;
        packet.runtime.expectedRangeCount = compiledRanges ? expectedRangeCount : InvalidRenderFlowResourceIndex;
        ResetActiveCompiledRange(packet.runtime);
        if (packet.runtime.liveness != nullptr)
            packet.runtime.liveness->ResetCompiledRange();
        cursor.m_generation = m_generation;
        cursor.m_packetIndex = m_packetIndex;
        cursor.m_commandList = commandList;
        cursor.m_open = true;
        return true;
    }

    ResolvedTextureUse::~ResolvedTextureUse()
    {
        detail::ReleasePacketUseLiveness(m_liveness);
    }

    ResolvedTextureUse::ResolvedTextureUse(ResolvedTextureUse&& other) noexcept
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_texture(other.m_texture), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc),
          m_runtimeUseSlot(other.m_runtimeUseSlot), m_generation(other.m_generation), m_packetIndex(other.m_packetIndex), m_rangeEpoch(other.m_rangeEpoch),
          m_hasExplicitView(other.m_hasExplicitView), m_shaderResource(other.m_shaderResource), m_unorderedAccess(other.m_unorderedAccess)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_texture = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_generation = {};
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_rangeEpoch = 0;
        other.m_hasExplicitView = false;
    }

    ResolvedTextureUse& ResolvedTextureUse::operator=(ResolvedTextureUse&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleasePacketUseLiveness(m_liveness);
        m_liveness = other.m_liveness;
        m_use = other.m_use;
        m_physical = other.m_physical;
        m_texture = other.m_texture;
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_generation = other.m_generation;
        m_packetIndex = other.m_packetIndex;
        m_rangeEpoch = other.m_rangeEpoch;
        m_hasExplicitView = other.m_hasExplicitView;
        m_shaderResource = other.m_shaderResource;
        m_unorderedAccess = other.m_unorderedAccess;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_texture = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_generation = {};
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_rangeEpoch = 0;
        other.m_hasExplicitView = false;
        return *this;
    }

    bool ResolvedTextureUse::IsValid() const noexcept
    {
        if (m_liveness == nullptr || !m_physical.IsValid())
            return false;
        if (m_rangeEpoch != 0)
            return m_liveness->generation == m_generation && m_liveness->packetIndex == m_packetIndex &&
                   m_liveness->ContainsCompiledUse(m_rangeEpoch, m_use.ordinal);
        return m_runtimeUseSlot < m_liveness->useActive.Size() && m_liveness->useActive[m_runtimeUseSlot] != 0;
    }

    ResourceUseId ResolvedTextureUse::GetUse() const noexcept
    {
        return m_use;
    }
    PhysicalResourceId ResolvedTextureUse::GetPhysicalResource() const noexcept
    {
        return m_physical;
    }
    rhi::TextureRef ResolvedTextureUse::GetTexture() const noexcept
    {
        return IsValid() ? m_texture : rhi::TextureRef{};
    }
    const TextureUseDesc& ResolvedTextureUse::GetDesc() const noexcept
    {
        return m_desc;
    }

    rhi::DescriptorHandle ResolvedTextureUse::GetShaderResourceDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_shaderResource : rhi::DescriptorHandle{};
    }
    rhi::DescriptorHandle ResolvedTextureUse::GetUnorderedAccessDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_unorderedAccess : rhi::DescriptorHandle{};
    }

    bool ResolvedTextureUse::HasExplicitView() const noexcept
    {
        return m_hasExplicitView;
    }

    const rhi::TextureViewDesc& ResolvedTextureUse::GetViewDesc() const noexcept
    {
        return m_viewDesc;
    }

    ResolvedBufferUse::~ResolvedBufferUse()
    {
        detail::ReleasePacketUseLiveness(m_liveness);
    }

    ResolvedBufferUse::ResolvedBufferUse(ResolvedBufferUse&& other) noexcept
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_buffer(other.m_buffer), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc), m_runtimeUseSlot(other.m_runtimeUseSlot),
          m_generation(other.m_generation), m_packetIndex(other.m_packetIndex), m_rangeEpoch(other.m_rangeEpoch), m_hasExplicitView(other.m_hasExplicitView),
          m_shaderResource(other.m_shaderResource), m_unorderedAccess(other.m_unorderedAccess)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_buffer = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_generation = {};
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_rangeEpoch = 0;
        other.m_hasExplicitView = false;
    }

    ResolvedBufferUse& ResolvedBufferUse::operator=(ResolvedBufferUse&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleasePacketUseLiveness(m_liveness);
        m_liveness = other.m_liveness;
        m_use = other.m_use;
        m_physical = other.m_physical;
        m_buffer = other.m_buffer;
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_generation = other.m_generation;
        m_packetIndex = other.m_packetIndex;
        m_rangeEpoch = other.m_rangeEpoch;
        m_hasExplicitView = other.m_hasExplicitView;
        m_shaderResource = other.m_shaderResource;
        m_unorderedAccess = other.m_unorderedAccess;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_buffer = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_generation = {};
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_rangeEpoch = 0;
        other.m_hasExplicitView = false;
        return *this;
    }

    bool ResolvedBufferUse::IsValid() const noexcept
    {
        if (m_liveness == nullptr || !m_physical.IsValid())
            return false;
        if (m_rangeEpoch != 0)
            return m_liveness->generation == m_generation && m_liveness->packetIndex == m_packetIndex &&
                   m_liveness->ContainsCompiledUse(m_rangeEpoch, m_use.ordinal);
        return m_runtimeUseSlot < m_liveness->useActive.Size() && m_liveness->useActive[m_runtimeUseSlot] != 0;
    }

    ResourceUseId ResolvedBufferUse::GetUse() const noexcept
    {
        return m_use;
    }
    PhysicalResourceId ResolvedBufferUse::GetPhysicalResource() const noexcept
    {
        return m_physical;
    }
    rhi::BufferRef ResolvedBufferUse::GetBuffer() const noexcept
    {
        return IsValid() ? m_buffer : rhi::BufferRef{};
    }
    const BufferUseDesc& ResolvedBufferUse::GetDesc() const noexcept
    {
        return m_desc;
    }

    rhi::DescriptorHandle ResolvedBufferUse::GetShaderResourceDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_shaderResource : rhi::DescriptorHandle{};
    }
    rhi::DescriptorHandle ResolvedBufferUse::GetUnorderedAccessDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_unorderedAccess : rhi::DescriptorHandle{};
    }

    bool ResolvedBufferUse::HasExplicitView() const noexcept
    {
        return m_hasExplicitView;
    }

    const rhi::BufferViewDesc& ResolvedBufferUse::GetViewDesc() const noexcept
    {
        return m_viewDesc;
    }

    ExecutionPacketCursor::~ExecutionPacketCursor()
    {
        if (!m_open || m_generation.m_impl == nullptr)
            return;
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket* const packet = GetPacket(generation, m_packetIndex);
        if (packet != nullptr)
        {
            if (packet->runtime.state == detail::PacketRuntimeState::Executing)
                FailPacketExecution(*packet);
        }
        m_open = false;
        m_commandList = {};
    }

    ExecutionPacketCursor::ExecutionPacketCursor(ExecutionPacketCursor&& other) noexcept
        : m_generation(static_cast<ExecutionGenerationRef&&>(other.m_generation)), m_packetIndex(other.m_packetIndex), m_commandList(other.m_commandList), m_open(other.m_open)
    {
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_commandList = {};
        other.m_open = false;
    }

    ExecutionPacketCursor& ExecutionPacketCursor::operator=(ExecutionPacketCursor&& other) noexcept
    {
        if (this == &other)
            return *this;
        if (m_open && m_generation.m_impl != nullptr)
        {
            ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
            detail::CompiledPacket* const packet = GetPacket(generation, m_packetIndex);
            if (packet != nullptr)
            {
                if (packet->runtime.state == detail::PacketRuntimeState::Executing)
                    FailPacketExecution(*packet);
            }
            m_open = false;
        }
        m_generation = static_cast<ExecutionGenerationRef&&>(other.m_generation);
        m_packetIndex = other.m_packetIndex;
        m_commandList = other.m_commandList;
        m_open = other.m_open;
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_commandList = {};
        other.m_open = false;
        return *this;
    }

    bool ExecutionPacketCursor::IsValid() const noexcept
    {
        return m_open && m_generation.IsValid() && m_packetIndex < m_generation.m_impl->packets.Size();
    }

    bool ExecutionPacketCursor::BeginTextureUse(const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during texture use", &packet, use);
        if (packet.runtime.mode == detail::PacketExecutionMode::CompiledRanges)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "legacy texture-use replay cannot follow compiled-range execution", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::TextureUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use does not match the next compiled packet step", &packet, use);
        packet.runtime.mode = detail::PacketExecutionMode::LegacyUseReplay;
        if (!ExecuteCompiledUseBegin(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        return true;
    }

    bool ExecutionPacketCursor::BeginBufferUse(const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during buffer use", &packet, use);
        if (packet.runtime.mode == detail::PacketExecutionMode::CompiledRanges)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "legacy buffer-use replay cannot follow compiled-range execution", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::BufferUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use does not match the next compiled packet step", &packet, use);
        packet.runtime.mode = detail::PacketExecutionMode::LegacyUseReplay;
        if (!ExecuteCompiledUseBegin(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        return true;
    }

    bool ExecutionPacketCursor::BeginTextureUse(const ResourceUseId use, ResolvedTextureUse& resolved, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during texture use", &packet, use);
        if (packet.runtime.mode == detail::PacketExecutionMode::CompiledRanges)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "legacy texture-use replay cannot follow compiled-range execution", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::TextureUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use does not match the next compiled packet step", &packet, use);
        packet.runtime.mode = detail::PacketExecutionMode::LegacyUseReplay;
        if (!ExecuteCompiledUseBegin(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedTextureUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
        if (generation->hasNativeResourceBindings)
        {
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(*generation, packet, step.resourceBinding, step.physical, resource, failure, use))
                return false;
            if (resource.GetKind() != rhi::ResourceKind::Texture)
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "texture use packet binding has the wrong kind", &packet, use);
            value.m_texture = {resource.Index(), resource.GetGeneration()};
            value.m_shaderResource = packet.resourceBindingShaderResources[step.resourceBinding];
            value.m_unorderedAccess = packet.resourceBindingUnorderedAccess[step.resourceBinding];
        }
        value.m_desc = step.texture;
        value.m_viewDesc = step.textureView;
        value.m_hasExplicitView = step.hasExplicitView;
        resolved = static_cast<ResolvedTextureUse&&>(value);
        return true;
    }

    bool ExecutionPacketCursor::BeginBufferUse(const ResourceUseId use, ResolvedBufferUse& resolved, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during buffer use", &packet, use);
        if (packet.runtime.mode == detail::PacketExecutionMode::CompiledRanges)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "legacy buffer-use replay cannot follow compiled-range execution", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::BufferUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use does not match the next compiled packet step", &packet, use);
        packet.runtime.mode = detail::PacketExecutionMode::LegacyUseReplay;
        if (!ExecuteCompiledUseBegin(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedBufferUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
        if (generation->hasNativeResourceBindings)
        {
            rhi::ResourceRef resource;
            if (!ResolvePacketResourceBinding(*generation, packet, step.resourceBinding, step.physical, resource, failure, use))
                return false;
            if (resource.GetKind() != rhi::ResourceKind::Buffer)
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "buffer use packet binding has the wrong kind", &packet, use);
            value.m_buffer = {resource.Index(), resource.GetGeneration()};
            value.m_shaderResource = packet.resourceBindingShaderResources[step.resourceBinding];
            value.m_unorderedAccess = packet.resourceBindingUnorderedAccess[step.resourceBinding];
        }
        value.m_desc = step.buffer;
        value.m_viewDesc = step.bufferView;
        value.m_hasExplicitView = step.hasExplicitView;
        resolved = static_cast<ResolvedBufferUse&&>(value);
        return true;
    }

    bool ExecutionPacketCursor::EndUse(const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before resource-use end", &packet, use);
        if (packet.runtime.mode != detail::PacketExecutionMode::LegacyUseReplay)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "legacy resource-use completion is not active for this packet", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use cannot end at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::UseEnd || step.use != use || !IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use end does not match the next compiled packet step", &packet, use);
        if (!ExecuteCompiledUseEnd(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 0;
        --packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        return true;
    }

    bool ExecutionPacketCursor::BeginRange(const CompiledExecutionRange& range, const u32 rangeEpoch, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || rangeEpoch == 0)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled execution range identity is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (range.generation == 0 || range.generation != generation->id.generation)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                 "compiled execution range belongs to another execution generation", &packet);
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before compiled range begin", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "compiled range cannot begin outside packet execution", &packet);
        if (packet.runtime.mode != detail::PacketExecutionMode::CompiledRanges || packet.runtime.expectedRangeCount == InvalidRenderFlowResourceIndex)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "compiled range requires a compiled-range cursor", &packet);
        if (packet.runtime.activeRangeEpoch != 0 || packet.runtime.activeUseCount != 0)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "a compiled child range is already active", &packet);
        if (packet.runtime.lastCompletedRangeEpoch == ~u32{0} || rangeEpoch != packet.runtime.lastCompletedRangeEpoch + 1u ||
            rangeEpoch > packet.runtime.expectedRangeCount)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child ranges were not entered in declaration order", &packet);

        u32 afterEndStep = InvalidRenderFlowResourceIndex;
        if (!ValidateCompiledExecutionRange(packet, range, afterEndStep, failure))
            return false;
        if (packet.runtime.nextStep != range.beginStepOffset)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range does not begin at the packet execution cursor", &packet);
        if (packet.runtime.liveness != nullptr)
        {
            if (packet.runtime.liveness->generation != generation->id || packet.runtime.liveness->packetIndex != m_packetIndex ||
                packet.runtime.liveness->activeRangeEpoch.GetValue() != 0)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range liveness belongs to another or already-active range", &packet);
        }
        else if (range.beginStepCount != 0)
        {
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range has resource uses but no range-liveness owner", &packet);
        }

        if (!ExecuteCompiledRangeBegins(*generation, packet, range.beginStepOffset, range.beginStepCount, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }

        packet.runtime.nextStep = range.endStepOffset;
        packet.runtime.activeUseCount = range.beginStepCount;
        packet.runtime.activeRangeEpoch = rangeEpoch;
        packet.runtime.activeRangeBeginStep = range.beginStepOffset;
        packet.runtime.activeRangeEndStep = range.endStepOffset;
        packet.runtime.activeRangeEndCount = range.endStepCount;
        if (packet.runtime.liveness != nullptr)
        {
            const u32 firstUseOrdinal = range.beginStepCount != 0 ? packet.steps[range.beginStepOffset].use.ordinal : InvalidRenderFlowResourceIndex;
            const u32 endUseOrdinal = range.beginStepCount != 0 ? firstUseOrdinal + range.beginStepCount : InvalidRenderFlowResourceIndex;
            packet.runtime.liveness->PublishCompiledRange(firstUseOrdinal, endUseOrdinal, rangeEpoch);
        }
        return true;
    }

    bool ExecutionPacketCursor::EndRange(const CompiledExecutionRange& range, const u32 rangeEpoch, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || rangeEpoch == 0)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled execution range identity is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (range.generation == 0 || range.generation != generation->id.generation)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                 "compiled execution range belongs to another execution generation", &packet);
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before compiled range end", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing ||
            packet.runtime.mode != detail::PacketExecutionMode::CompiledRanges)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "compiled range cannot end outside compiled-range packet execution", &packet);
        if (packet.runtime.activeRangeEpoch != rangeEpoch || packet.runtime.activeRangeBeginStep != range.beginStepOffset ||
            packet.runtime.activeRangeEndStep != range.endStepOffset || packet.runtime.activeRangeEndCount != range.endStepCount)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range end does not match the active range", &packet);
        if (packet.runtime.nextStep != range.endStepOffset || packet.runtime.activeUseCount != range.endStepCount)
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range runtime span is incomplete", &packet);

        u32 afterEndStep = InvalidRenderFlowResourceIndex;
        if (!ValidateCompiledExecutionRange(packet, range, afterEndStep, failure))
            return false;
        if (packet.runtime.liveness != nullptr)
        {
            if (packet.runtime.liveness->generation != generation->id || packet.runtime.liveness->packetIndex != m_packetIndex ||
                packet.runtime.liveness->activeRangeEpoch.GetValue() != rangeEpoch)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range liveness does not match the active range", &packet);
        }
        else if (range.endStepCount != 0)
        {
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled child range has resource completions but no range-liveness owner", &packet);
        }

        if (!ExecuteCompiledRangeTape(*generation, packet, range.endActions, detail::CompiledActionTapePhase::End,
                                      range.endStepOffset, range.endStepCount, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }

        packet.runtime.nextStep = afterEndStep;
        packet.runtime.activeUseCount = 0;
        if (packet.runtime.liveness != nullptr)
            packet.runtime.liveness->ResetCompiledRange();
        packet.runtime.lastCompletedRangeEpoch = rangeEpoch;
        ResetActiveCompiledRange(packet.runtime);
        return true;
    }

    bool ExecutionPacketCursor::CapturedDecision(const DecisionId decision, bool& value, RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        const detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution packet is not active", &packet);
        if (decision.ordinal < packet.decisions.Size() && packet.decisions[decision.ordinal].id == decision)
        {
            value = packet.decisions[decision.ordinal].value;
            return true;
        }
        return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "decision does not belong to this execution packet", &packet);
    }

    bool ExecutionPacketCursor::FinalizePacket(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before packet finalization", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.activeUseCount != 0 ||
            packet.runtime.activeRangeEpoch != 0 || packet.runtime.nextStep != packet.steps.Size() ||
            (packet.runtime.liveness != nullptr && packet.runtime.liveness->activeRangeEpoch.GetValue() != 0))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution packet still has an active or unconsumed step", &packet);
        if (packet.runtime.mode == detail::PacketExecutionMode::CompiledRanges)
        {
            if (packet.runtime.expectedRangeCount == InvalidRenderFlowResourceIndex ||
                packet.runtime.lastCompletedRangeEpoch != packet.runtime.expectedRangeCount)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution,
                                     "compiled-range packet did not complete every declared range epoch", &packet);
        }
        if (!ExecutePacketExitTape(*generation, packet, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.state = detail::PacketRuntimeState::Complete;
        ResetActiveCompiledRange(packet.runtime);
        if (packet.runtime.liveness != nullptr)
            packet.runtime.liveness->ResetCompiledRange();
        m_open = false;
        m_commandList = {};
        return true;
    }

    void ExecutionPacketCursor::CancelRemaining() noexcept
    {
        if (!IsValid())
            return;
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (packet.runtime.state == detail::PacketRuntimeState::Executing)
        {
            packet.runtime.state = detail::PacketRuntimeState::Canceled;
            packet.runtime.activeUseCount = 0;
            ResetActiveCompiledRange(packet.runtime);
            if (packet.runtime.liveness != nullptr)
            {
                packet.runtime.liveness->ResetCompiledRange();
                for (u8& active : packet.runtime.liveness->useActive)
                    active = 0;
            }
        }
        m_open = false;
        m_commandList = {};
    }

    bool RenderFlowResourceAllocator::TakeExport(const ExportSlotId slot, PublishedResourceExport& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (m_impl->state != RenderFlowResourceSessionState::Idle)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "terminal exports may be taken only between frame sessions");
        if (!slot.IsValid() || resource.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, m_impl->state, "terminal export slot or output owner is invalid");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->writerLock);
        const u64 key = ExportSlotKey(slot);
        u32 index = InvalidRenderFlowResourceIndex;
        if (!m_impl->publishedExportIndices.Find(key, index) || index >= m_impl->publishedExports.Size() || m_impl->publishedExports[index].slot != slot)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, m_impl->state, "terminal export slot has not been published or was already taken");

        const u32 lastIndex = m_impl->publishedExports.Size() - 1u;
        u64 movedKey = 0;
        if (index != lastIndex)
        {
            movedKey = ExportSlotKey(m_impl->publishedExports.Back().slot);
            u32 indexedLast = InvalidRenderFlowResourceIndex;
            if (!m_impl->publishedExportIndices.Find(movedKey, indexedLast) || indexedLast != lastIndex)
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, m_impl->state, "persistent export index lost its swap-removal tail");
        }
        if (!m_impl->publishedExportIndices.Erase(key))
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, m_impl->state, "persistent export index could not erase a published slot");
        if (index != lastIndex)
        {
            if (!m_impl->publishedExportIndices.Erase(movedKey))
            {
                static_cast<void>(m_impl->publishedExportIndices.Insert(key, index));
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, m_impl->state, "persistent export index could not relocate its swap-removal tail");
            }
            if (!m_impl->publishedExportIndices.Insert(movedKey, index))
            {
                static_cast<void>(m_impl->publishedExportIndices.Insert(key, index));
                static_cast<void>(m_impl->publishedExportIndices.Insert(movedKey, lastIndex));
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, m_impl->state, "persistent export index rejected its relocated swap-removal tail");
            }
        }

        detail::PublishedExportRecord& published = m_impl->publishedExports[index];
        resource.m_slot = published.slot;
        resource.m_kind = published.kind;
        resource.m_texture = static_cast<rhi::Texture&&>(published.texture);
        resource.m_buffer = static_cast<rhi::Buffer&&>(published.buffer);
        resource.m_textureDesc = published.textureDesc;
        resource.m_bufferDesc = published.bufferDesc;
        resource.m_terminalState = published.terminalState;
        resource.m_terminalQueue = published.terminalQueue;
        resource.m_readiness = published.readiness;
        resource.m_readyFence = published.readyFence;
        if (index != lastIndex)
        {
            detail::PublishedExportRecord& moved = m_impl->publishedExports.Back();
            published = static_cast<detail::PublishedExportRecord&&>(moved);
        }
        static_cast<void>(m_impl->publishedExports.PopBack());
        return true;
    }

    bool RenderFlowResourceAllocator::BeginExecution(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Ready)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator has no ready execution generation");
        owner->state = RenderFlowResourceSessionState::Executing;
        owner->stats.state = owner->state;
        return true;
    }

    bool RenderFlowResourceAllocator::PacketFor(const RenderFlowNodeId node, CompiledExecutionPacketView& packet, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        packet = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized", node);
        if (owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator has no published execution generation", node);
        u32 packetIndex = InvalidRenderFlowResourceIndex;
        if (!node.IsValid() || !owner->publishedGeneration->packetByNode.Find(node.value, packetIndex) || packetIndex >= owner->publishedGeneration->packets.Size() ||
            owner->publishedGeneration->packets[packetIndex].node != node)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "render node has no compiled execution packet", node);
        packet.m_generation = ExecutionGenerationRef(owner->publishedGeneration);
        packet.m_packetIndex = packetIndex;
        return true;
    }

    containers::ArraySpan<const CompiledCommandScope> RenderFlowResourceAllocator::GetExecutionCommandScopes() const noexcept
    {
        return m_impl != nullptr && m_impl->publishedGeneration != nullptr ? containers::ArraySpan<const CompiledCommandScope>(m_impl->publishedGeneration->commandScopes) :
                                                                            containers::ArraySpan<const CompiledCommandScope>{};
    }

    containers::ArraySpan<const CompiledQueueDependency> RenderFlowResourceAllocator::GetExecutionQueueDependencies() const noexcept
    {
        return m_impl != nullptr && m_impl->publishedGeneration != nullptr ? containers::ArraySpan<const CompiledQueueDependency>(m_impl->publishedGeneration->queueDependencies) :
                                                                            containers::ArraySpan<const CompiledQueueDependency>{};
    }

    bool RenderFlowResourceAllocator::Finish(const TerminalExecutionReceipt& receipt, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->publishedGeneration == nullptr || (owner->state != RenderFlowResourceSessionState::Ready && owner->state != RenderFlowResourceSessionState::Executing))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not ready for terminal completion");
        ExecutionGenerationRef::Impl* const generation = owner->publishedGeneration;
        if (!ValidTerminalCompletion(receipt.completion) || !receipt.join.IsReadyFor(generation->id) || receipt.generation != generation->id ||
            receipt.commandScopes.Size() != generation->commandScopes.Size() || receipt.queueDependencies.Size() != generation->queueDependencies.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal receipt does not prove the complete execution generation joined");
        if (receipt.completion == TerminalExecutionCompletionKind::Completed && owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires an execution generation that was explicitly started");

        // Terminal receipts are unordered public evidence. Build one dense
        // generation-local index, then consume it everywhere below instead of
        // repeatedly cross-matching S scopes and D dependencies. The backing is
        // ResolveScratch memory whose Resolve-phase contents are dead once the
        // execution generation is published.
        detail::ResolveScratch& terminalScratch = owner->resolveScratch;
        detail::ResolveScratchHashMap<u64, u32>& scopeIndices = terminalScratch.openScopeIndices;
        containers::DynamicArray<u32>& scopeReceiptIndices = terminalScratch.batchResourceSlots;
        containers::DynamicArray<u32>& dependencyByConsumer = terminalScratch.terminalDependencyByConsumer;
        containers::DynamicArray<u32>& dependencyReceiptIndices = terminalScratch.physicalAssignmentOrder;
        containers::DynamicArray<u32>& submittedProducerScopes = terminalScratch.crossQueueProducerScopes;
        containers::DynamicArray<u32>& dependencyProducerScopeIndices = terminalScratch.terminalDependencyProducerScopes;
        constexpr u32 DuplicateTerminalReceiptIndex = InvalidRenderFlowResourceIndex - 1u;

        scopeIndices.Reset();
        scopeIndices.Reserve(generation->commandScopes.Size());
        scopeReceiptIndices.Clear();
        scopeReceiptIndices.Resize(generation->commandScopes.Size(), InvalidRenderFlowResourceIndex);
        for (u32 scopeIndex = 0; scopeIndex < generation->commandScopes.Size(); ++scopeIndex)
        {
            const CompiledCommandScope& expected = generation->commandScopes[scopeIndex];
            if (!expected.scope.IsValid() || !scopeIndices.Insert(static_cast<u64>(expected.scope.value), scopeIndex))
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "compiled command-scope index is invalid or duplicated");
        }
        for (u32 receiptIndex = 0; receiptIndex < receipt.commandScopes.Size(); ++receiptIndex)
        {
            const CommandScopeExecutionReceipt& candidate = receipt.commandScopes[receiptIndex];
            u32 scopeIndex = InvalidRenderFlowResourceIndex;
            if (!scopeIndices.Find(static_cast<u64>(candidate.scope.value), scopeIndex) || scopeIndex >= scopeReceiptIndices.Size())
                continue;
            u32& indexedReceipt = scopeReceiptIndices[scopeIndex];
            indexedReceipt = indexedReceipt == InvalidRenderFlowResourceIndex ? receiptIndex : DuplicateTerminalReceiptIndex;
        }

        for (u32 scopeIndex = 0; scopeIndex < generation->commandScopes.Size(); ++scopeIndex)
        {
            const CompiledCommandScope& expected = generation->commandScopes[scopeIndex];
            const u32 indexedReceipt = scopeReceiptIndices[scopeIndex];
            if (indexedReceipt == DuplicateTerminalReceiptIndex)
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt duplicates a command scope");
            const CommandScopeExecutionReceipt* const found = indexedReceipt < receipt.commandScopes.Size() ? &receipt.commandScopes[indexedReceipt] : nullptr;
            if (found == nullptr || !detail::ValidQueue(found->queue) || found->queue != expected.queue || !ValidScopeCompletion(found->completion))
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt is missing a command scope or reports the wrong queue");
            if (generation->hasNativeResourceBindings && receipt.completion == TerminalExecutionCompletionKind::Completed && found->completion != CommandScopeCompletionKind::Submitted)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires every command scope to be submitted");
            if (found->completion == CommandScopeCompletionKind::Submitted && (!found->fence.IsValid() || found->fence.queue != found->queue))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "submitted command scope requires a matching real queue fence");
            if (found->completion == CommandScopeCompletionKind::DiscardedBeforeSubmission && found->fence.IsValid())
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "discarded command scope cannot report a submission fence");
            if (found->completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss && (receipt.completion != TerminalExecutionCompletionKind::DeviceLost || found->fence.IsValid()))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "unknown command completion is legal only after device loss");
        }

        if (generation->presentationAcquisition.IsValid())
        {
            if (!generation->presentationCommandScope.IsValid())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state,
                                    "presentation acquisition has no compiled terminal Graphics scope");
            u32 presentationScopeIndex = InvalidRenderFlowResourceIndex;
            const CommandScopeExecutionReceipt* presentationReceipt = nullptr;
            if (scopeIndices.Find(static_cast<u64>(generation->presentationCommandScope.value), presentationScopeIndex) && presentationScopeIndex < scopeReceiptIndices.Size())
            {
                const u32 receiptIndex = scopeReceiptIndices[presentationScopeIndex];
                if (receiptIndex < receipt.commandScopes.Size())
                    presentationReceipt = &receipt.commandScopes[receiptIndex];
            }
            if (receipt.completion == TerminalExecutionCompletionKind::Completed &&
                (presentationReceipt == nullptr || presentationReceipt->completion != CommandScopeCompletionKind::Submitted ||
                 presentationReceipt->queue != rhi::QueueType::Graphics || !presentationReceipt->fence.IsValid() ||
                 presentationReceipt->fence.queue != rhi::QueueType::Graphics))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state,
                                    "successful presentation requires its terminal Graphics scope submission receipt");
        }

        dependencyByConsumer.Clear();
        dependencyByConsumer.Resize(generation->commandScopes.Size(), InvalidRenderFlowResourceIndex);
        dependencyReceiptIndices.Clear();
        dependencyReceiptIndices.Resize(generation->queueDependencies.Size(), InvalidRenderFlowResourceIndex);
        dependencyProducerScopeIndices.Clear();
        dependencyProducerScopeIndices.Resize(generation->queueDependencies.Size(), InvalidRenderFlowResourceIndex);
        for (u32 dependencyIndex = 0; dependencyIndex < generation->queueDependencies.Size(); ++dependencyIndex)
        {
            const CompiledQueueDependency& expected = generation->queueDependencies[dependencyIndex];
            u32 producerScopeIndex = InvalidRenderFlowResourceIndex;
            u32 consumerScopeIndex = InvalidRenderFlowResourceIndex;
            if (!scopeIndices.Find(static_cast<u64>(expected.producerScope.value), producerScopeIndex) ||
                !scopeIndices.Find(static_cast<u64>(expected.consumerScope.value), consumerScopeIndex) ||
                producerScopeIndex >= generation->commandScopes.Size() || consumerScopeIndex >= dependencyByConsumer.Size() ||
                dependencyByConsumer[consumerScopeIndex] != InvalidRenderFlowResourceIndex)
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "compiled queue dependency has invalid or duplicate dense scope indices");
            dependencyByConsumer[consumerScopeIndex] = dependencyIndex;
            dependencyProducerScopeIndices[dependencyIndex] = producerScopeIndex;
        }
        for (u32 receiptIndex = 0; receiptIndex < receipt.queueDependencies.Size(); ++receiptIndex)
        {
            const QueueDependencyExecutionReceipt& candidate = receipt.queueDependencies[receiptIndex];
            u32 consumerScopeIndex = InvalidRenderFlowResourceIndex;
            if (!scopeIndices.Find(static_cast<u64>(candidate.consumerScope.value), consumerScopeIndex) || consumerScopeIndex >= dependencyByConsumer.Size())
                continue;
            const u32 dependencyIndex = dependencyByConsumer[consumerScopeIndex];
            if (dependencyIndex >= generation->queueDependencies.Size() || generation->queueDependencies[dependencyIndex].producerScope != candidate.producerScope)
                continue;
            u32& indexedReceipt = dependencyReceiptIndices[dependencyIndex];
            indexedReceipt = indexedReceipt == InvalidRenderFlowResourceIndex ? receiptIndex : DuplicateTerminalReceiptIndex;
        }

        for (u32 dependencyIndex = 0; dependencyIndex < generation->queueDependencies.Size(); ++dependencyIndex)
        {
            const CompiledQueueDependency& expected = generation->queueDependencies[dependencyIndex];
            const u32 indexedReceipt = dependencyReceiptIndices[dependencyIndex];
            if (indexedReceipt == DuplicateTerminalReceiptIndex)
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt duplicates a compiled queue dependency");
            const QueueDependencyExecutionReceipt* const found = indexedReceipt < receipt.queueDependencies.Size() ? &receipt.queueDependencies[indexedReceipt] : nullptr;
            if (found == nullptr || found->sync != expected.sync || !ValidQueueDependencyCompletion(found->completion))
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt is missing or disagrees with a compiled queue dependency");
            if (generation->hasNativeResourceBindings && receipt.completion == TerminalExecutionCompletionKind::Completed && found->completion != QueueDependencyCompletionKind::Submitted)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires every queue dependency to be submitted");
            if (found->completion == QueueDependencyCompletionKind::UnknownDueToDeviceLoss && receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "unknown queue-dependency completion is legal only after device loss");
            const u32 producerScopeIndex = dependencyProducerScopeIndices[dependencyIndex];
            if (producerScopeIndex >= scopeReceiptIndices.Size())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "compiled queue dependency producer is absent from the dense command-scope index");
            const u32 loweringReceiptIndex = scopeReceiptIndices[producerScopeIndex];
            const CommandScopeExecutionReceipt* const loweringReceipt = loweringReceiptIndex < receipt.commandScopes.Size() ? &receipt.commandScopes[loweringReceiptIndex] : nullptr;
            const CommandScopeCompletionKind requiredLowering = found->completion == QueueDependencyCompletionKind::Submitted                   ? CommandScopeCompletionKind::Submitted
                                                                : found->completion == QueueDependencyCompletionKind::DiscardedBeforeSubmission ? CommandScopeCompletionKind::DiscardedBeforeSubmission
                                                                                                                                                : CommandScopeCompletionKind::UnknownDueToDeviceLoss;
            if (loweringReceipt == nullptr || loweringReceipt->completion != requiredLowering)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "queue dependency completion disagrees with its lowering command scope");
        }

        submittedProducerScopes.Clear();
        if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost && !generation->queueDependencies.Empty())
        {
            submittedProducerScopes.Resize(generation->commandScopes.Size(), InvalidRenderFlowResourceIndex);
            u32 forkProducerScopeIndex = InvalidRenderFlowResourceIndex;
            u32 joinProducerScopeIndex = InvalidRenderFlowResourceIndex;
            for (u32 consumerScopeIndex = 0; consumerScopeIndex < generation->commandScopes.Size(); ++consumerScopeIndex)
            {
                const u32 dependencyIndex = dependencyByConsumer[consumerScopeIndex];
                if (dependencyIndex < generation->queueDependencies.Size())
                {
                    const u32 dependencyReceiptIndex = dependencyReceiptIndices[dependencyIndex];
                    if (dependencyReceiptIndex < receipt.queueDependencies.Size() &&
                        receipt.queueDependencies[dependencyReceiptIndex].completion == QueueDependencyCompletionKind::Submitted)
                    {
                        const CompiledQueueDependency& dependency = generation->queueDependencies[dependencyIndex];
                        const u32 producerScopeIndex = dependencyProducerScopeIndices[dependencyIndex];
                        if (producerScopeIndex >= consumerScopeIndex)
                            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "submitted queue dependency is not forward ordered in the dense command-scope stream");
                        if (dependency.sync == rhi::CommandListSyncType::ForkAsyncCompute)
                            forkProducerScopeIndex = producerScopeIndex;
                        else if (dependency.sync == rhi::CommandListSyncType::JoinAsyncCompute)
                            joinProducerScopeIndex = producerScopeIndex;
                    }
                }
                const rhi::QueueType queue = generation->commandScopes[consumerScopeIndex].queue;
                submittedProducerScopes[consumerScopeIndex] = queue == rhi::QueueType::Compute ? forkProducerScopeIndex
                                                               : queue == rhi::QueueType::Graphics ? joinProducerScopeIndex
                                                                                                    : InvalidRenderFlowResourceIndex;
            }
        }

        const bool hasPendingExports = !generation->pendingExports.Empty();
        rhi::ResidencyFenceSet submittedFences;
        for (const CommandScopeExecutionReceipt& commandScope : receipt.commandScopes)
            if (commandScope.completion == CommandScopeCompletionKind::Submitted)
                submittedFences.Include(commandScope.fence);
        {
            if (generation->terminal.GetValue())
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "execution generation is already terminal");

            // Recording and its continuations have joined before this coordinator access.
            // Validate first. An invalid terminal receipt must not partially cancel packets.
            for (detail::CompiledPacket& compiled : generation->packets)
            {
                if (receipt.completion == TerminalExecutionCompletionKind::Completed && compiled.runtime.state != detail::PacketRuntimeState::Complete)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful frame completion requires every packet to be exhausted", compiled.node);
                if (receipt.completion != TerminalExecutionCompletionKind::Completed && compiled.runtime.state == detail::PacketRuntimeState::Executing)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal abort cannot join while a packet cursor is still executing", compiled.node);
            }

            if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
                for (u32 packetIndex = 0; packetIndex < generation->packets.Size(); ++packetIndex)
                {
                    const detail::CompiledPacket& compiled = generation->packets[packetIndex];
                    if (packetIndex >= generation->commandScopes.Size() || packetIndex >= scopeReceiptIndices.Size() ||
                        generation->commandScopes[packetIndex].scope != compiled.commandScope)
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state,
                                            "compiled packet and dense command-scope indices diverged before terminal validation", compiled.node);
                    const u32 consumerReceiptIndex = scopeReceiptIndices[packetIndex];
                    const CommandScopeExecutionReceipt* const consumer = consumerReceiptIndex < receipt.commandScopes.Size() ? &receipt.commandScopes[consumerReceiptIndex] : nullptr;
                    if (consumer == nullptr || consumer->completion != CommandScopeCompletionKind::Submitted)
                        continue;
                    for (const u32 predecessorIndex : compiled.statePredecessorPackets)
                    {
                        if (predecessorIndex >= generation->packets.Size() || predecessorIndex >= generation->commandScopes.Size() || predecessorIndex >= scopeReceiptIndices.Size())
                            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "explicit state predecessor is stale", compiled.node);
                        const detail::CompiledPacket& predecessor = generation->packets[predecessorIndex];
                        if (generation->commandScopes[predecessorIndex].scope != predecessor.commandScope)
                            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state,
                                                "explicit state predecessor lost its dense command-scope identity", compiled.node);
                        const u32 producerReceiptIndex = scopeReceiptIndices[predecessorIndex];
                        const CommandScopeExecutionReceipt* const producer = producerReceiptIndex < receipt.commandScopes.Size() ? &receipt.commandScopes[producerReceiptIndex] : nullptr;
                        const bool ordered = producer != nullptr && producer->completion == CommandScopeCompletionKind::Submitted &&
                                             (producer->queue == consumer->queue ? producer->fence.value <= consumer->fence.value
                                                                                : SubmittedQueueOrderProved(*generation, predecessorIndex, packetIndex, submittedProducerScopes));
                        if (predecessor.runtime.state != detail::PacketRuntimeState::Complete || !ordered)
                            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "submitted explicit-state consumer requires its complete, ordered submitted producer", compiled.node);
                    }
                }

            if (receipt.completion == TerminalExecutionCompletionKind::Completed && hasPendingExports)
            {
                containers::DynamicArray<detail::PublishedExportRecord> publications{memory::pools::Rendering::GetInstance()};
                containers::DynamicArray<detail::DedicatedResourceTransfer> poolTransfers{memory::pools::Rendering::GetInstance()};
                publications.Reserve(generation->pendingExports.Size());
                poolTransfers.Reserve(generation->pendingExports.Size());
                // Dependency receipt indexing is dead after submitted-order
                // validation. Reuse the same u32 backing as physical -> export
                // so retirement can distinguish transferred resources in O(1).
                dependencyReceiptIndices.Clear();
                dependencyReceiptIndices.Resize(generation->physicalBindings.Size(), InvalidRenderFlowResourceIndex);
                for (u32 pendingIndex = 0; pendingIndex < generation->pendingExports.Size(); ++pendingIndex)
                {
                    const detail::PendingExportRecord& pending = generation->pendingExports[pendingIndex];
                    if (!pending.slot.IsValid() || !pending.physical.IsValid() || pending.physical.generation != generation->id.generation || pending.physical.index >= generation->physicalBindings.Size() ||
                        pending.terminalPacket >= generation->packets.Size() || pending.terminalPacket >= scopeReceiptIndices.Size())
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "compiled terminal export contains an invalid physical or command-scope assignment");
                    u32& physicalExport = dependencyReceiptIndices[pending.physical.index];
                    if (physicalExport != InvalidRenderFlowResourceIndex)
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "multiple terminal exports reference one physical binding");
                    physicalExport = pendingIndex;
                    const u32 terminalReceiptIndex = scopeReceiptIndices[pending.terminalPacket];
                    const CommandScopeExecutionReceipt* const terminalReceipt = terminalReceiptIndex < receipt.commandScopes.Size() ? &receipt.commandScopes[terminalReceiptIndex] : nullptr;
                    if (terminalReceipt == nullptr || terminalReceipt->completion != CommandScopeCompletionKind::Submitted || terminalReceipt->queue != pending.terminalQueue ||
                        !terminalReceipt->fence.IsValid())
                        return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal export requires the submitted receipt of its final command scope");

                    const detail::PhysicalBindingRecord& binding = generation->physicalBindings[pending.physical.index];
                    if (binding.kind == detail::PhysicalBindingKind::PlacedPool)
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "terminal exports cannot transfer allocator-owned placed resources");
                    detail::PublishedExportRecord publication;
                    publication.slot = pending.slot;
                    publication.kind = pending.kind;
                    publication.textureDesc = pending.textureDesc;
                    publication.bufferDesc = pending.bufferDesc;
                    publication.terminalState = pending.terminalState;
                    publication.terminalQueue = pending.terminalQueue;
                    publication.readiness = pending.readiness;
                    publication.readyFence = terminalReceipt->fence;
                    if (pending.kind == FrameResourceKind::Texture && binding.texture.IsValid() && !binding.buffer.IsValid())
                        publication.texture = rhi::Texture(binding.texture);
                    else if (pending.kind == FrameResourceKind::Buffer && binding.buffer.IsValid() && !binding.texture.IsValid())
                        publication.buffer = rhi::Buffer(binding.buffer);
                    else
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "terminal export kind disagrees with its physical resource");
                    if (binding.kind == detail::PhysicalBindingKind::DedicatedPool)
                        poolTransfers.PushBack({binding.poolEntry});
                    publications.PushBack(static_cast<detail::PublishedExportRecord&&>(publication));
                }
                const u32 publicationBaseIndex = owner->publishedExports.Size();
                u32 indexedPublicationCount = 0;
                for (u32 publicationOffset = 0; publicationOffset < publications.Size(); ++publicationOffset)
                {
                    const detail::PublishedExportRecord& publication = publications[publicationOffset];
                    const u64 publicationKey = ExportSlotKey(publication.slot);
                    u32 existingPublication = InvalidRenderFlowResourceIndex;
                    if (owner->publishedExportIndices.Find(publicationKey, existingPublication))
                    {
                        for (u32 rollback = 0; rollback < indexedPublicationCount; ++rollback)
                            static_cast<void>(owner->publishedExportIndices.Erase(ExportSlotKey(publications[rollback].slot)));
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "terminal export slot is already present in the persistent publication index");
                    }
                    if (!owner->publishedExportIndices.Insert(publicationKey, publicationBaseIndex + publicationOffset))
                    {
                        for (u32 rollback = 0; rollback < indexedPublicationCount; ++rollback)
                            static_cast<void>(owner->publishedExportIndices.Erase(ExportSlotKey(publications[rollback].slot)));
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "persistent publication index rejected a pre-reserved terminal export slot");
                    }
                    ++indexedPublicationCount;
                }
                if (!owner->dedicatedPool.TransferOwnership(containers::ArraySpan<const detail::DedicatedResourceTransfer>(poolTransfers), submittedFences, failure))
                {
                    for (u32 rollback = 0; rollback < indexedPublicationCount; ++rollback)
                        static_cast<void>(owner->publishedExportIndices.Erase(ExportSlotKey(publications[rollback].slot)));
                    return false;
                }
                {
                    concurrency::ScopedLock<concurrency::SpinLock> publicationGuard(owner->writerLock);
                    for (detail::PublishedExportRecord& publication : publications)
                        owner->publishedExports.PushBack(static_cast<detail::PublishedExportRecord&&>(publication));
                }
            }
            else if (receipt.completion != TerminalExecutionCompletionKind::Completed)
            {
                for (detail::CompiledPacket& compiled : generation->packets)
                {
                    if (compiled.runtime.state == detail::PacketRuntimeState::Unclaimed)
                        compiled.runtime.state = detail::PacketRuntimeState::Canceled;
                }
            }
            generation->terminal.SetValue(true);
        }

        if (generation->hasNativeResourceBindings)
        {
            if (receipt.completion == TerminalExecutionCompletionKind::DeviceLost)
            {
                detail::ReleasePersistentDedicatedPlan(*owner, false);
                owner->dedicatedPool.DeviceLost(submittedFences);
                owner->placedPool.DeviceLost(submittedFences);
                concurrency::ScopedLock<concurrency::SpinLock> publicationGuard(owner->writerLock);
                owner->publishedExports.Clear();
                owner->publishedExportIndices.Reset();
            }
            else
            {
                // Receipt/dependency validation is complete here. Reuse its dead
                // u32 scratch so terminal retirement does not allocate a fresh
                // vector every frame just to batch pool entries.
                containers::DynamicArray<u32>& reusableDedicated = dependencyByConsumer;
                containers::DynamicArray<u32>& quarantinedDedicated = dependencyProducerScopeIndices;
                reusableDedicated.Clear();
                quarantinedDedicated.Clear();
                for (u32 bindingIndex = 0; bindingIndex < generation->physicalBindings.Size(); ++bindingIndex)
                {
                    const detail::PhysicalBindingRecord& binding = generation->physicalBindings[bindingIndex];
                    const bool exported = receipt.completion == TerminalExecutionCompletionKind::Completed && hasPendingExports && bindingIndex < dependencyReceiptIndices.Size() &&
                                          dependencyReceiptIndices[bindingIndex] != InvalidRenderFlowResourceIndex;
                    if (binding.kind == detail::PhysicalBindingKind::DedicatedPool && !exported)
                    {
                        const bool reusable = !binding.explicitState || receipt.completion == TerminalExecutionCompletionKind::Completed;
                        (reusable ? reusableDedicated : quarantinedDedicated).PushBack(binding.poolEntry);
                    }
                }
                owner->dedicatedPool.Retire(reusableDedicated, quarantinedDedicated, submittedFences);
                bool placedReusable = true;
                // A placed batch is one heap-ownership unit. Any explicit object with uncertain abort state makes the complete batch unsafe to cache.
                if (receipt.completion != TerminalExecutionCompletionKind::Completed)
                    for (const detail::PhysicalBindingRecord& binding : generation->physicalBindings)
                        if (binding.kind == detail::PhysicalBindingKind::PlacedPool && binding.explicitState)
                        {
                            placedReusable = false;
                            break;
                        }
                owner->placedPool.Retire(generation->placedBatch, submittedFences, placedReusable);
            }
            const bool deviceLost = receipt.completion == TerminalExecutionCompletionKind::DeviceLost;
            if (!detail::ReleaseRetainedImportDescriptorBorrows(*owner, *generation, submittedFences, deviceLost, failure))
                return false;
            if (deviceLost)
                detail::RetainedImportDescriptorCacheDeviceLost(*owner, submittedFences);
        }

        // The terminal receipt is the ownership boundary. Packet views may keep
        // immutable compiled metadata alive after Finish, but they must not pin
        // descriptor slots, imported resources, presentation acquisitions, or
        // allocator physical bindings after all execution owners have joined.
        detail::ReleaseGenerationRuntimeOwners(generation);

        owner->state = receipt.completion == TerminalExecutionCompletionKind::DeviceLost ? RenderFlowResourceSessionState::DeviceUnavailable : RenderFlowResourceSessionState::TerminalJoined;
        owner->stats.state = owner->state;
        if (receipt.completion == TerminalExecutionCompletionKind::Completed)
            ++owner->stats.completedFrames;
        else
            ++owner->stats.abortedFrames;

        owner->publishedGeneration = nullptr;
        owner->activePolicy = {};
        detail::ReleaseGeneration(generation);
        if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
        {
            owner->state = RenderFlowResourceSessionState::Idle;
            owner->stats.state = owner->state;
        }
        return true;
    }
} // namespace vanguard::rendering
