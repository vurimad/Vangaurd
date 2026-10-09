#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rhi/rhi.hpp>

#include "../private/vanguard/rendering/render_flow_resource_placed.hpp"


namespace
{
    namespace containers = vanguard::containers;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;
    using vanguard::u16;
    using vanguard::u32;

    using CheckFunction = void (*)(bool condition, const char* message) noexcept;

    struct FrameResult
    {
        u32 firstAllocation = rendering::InvalidRenderFlowResourceIndex;
        u32 temporaryAllocation = rendering::InvalidRenderFlowResourceIndex;
        u32 postSwapAllocation = rendering::InvalidRenderFlowResourceIndex;
    };

    [[nodiscard]] rendering::RenderFlowResourceAllocatorConfig MakeLogicalOnlyConfig() noexcept
    {
        rendering::RenderFlowResourceAllocatorConfig result;
        result.allowLogicalOnlyValidation = true;
        return result;
    }

    [[nodiscard]] rendering::FrameBufferDesc MakeBufferDesc(const vanguard::u64 size = 4096) noexcept
    {
        rendering::FrameBufferDesc result;
        result.active.size = size;
        result.active.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess;
        result.active.initialState = rhi::ResourceState::Common;
        result.maximumSize = size;
        result.initialization = rendering::FrameResourceInitialization::Undefined;
        return result;
    }

    [[nodiscard]] rendering::BufferUseDesc WriteUse() noexcept
    {
        rendering::BufferUseDesc result;
        result.requiredState = rhi::ResourceState::UnorderedAccess;
        result.access = rendering::LogicalAccessIntent::Write;
        result.content = rendering::ResourceContentIntent::Discard;
        return result;
    }

    [[nodiscard]] rendering::BufferUseDesc ReadUse() noexcept
    {
        rendering::BufferUseDesc result;
        result.requiredState = rhi::ResourceState::ShaderResourceGraphics;
        result.access = rendering::LogicalAccessIntent::Read;
        result.content = rendering::ResourceContentIntent::Preserve;
        return result;
    }

    [[nodiscard]] rendering::FrameTextureDesc MakeTextureDesc() noexcept
    {
        rendering::FrameTextureDesc result;
        result.active.extent = {8, 8, 1};
        result.active.dimension = rhi::TextureDimension::Texture2D;
        result.active.format = rhi::Format::R8G8B8A8UNorm;
        result.active.mipCount = 2;
        result.active.arraySize = 1;
        result.active.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
        result.active.initialState = rhi::ResourceState::Common;
        result.maximumExtent = result.active.extent;
        result.maximumMipCount = result.active.mipCount;
        result.initialization = rendering::FrameResourceInitialization::Undefined;
        return result;
    }

    [[nodiscard]] rendering::TextureUseDesc TextureWriteUse(const u16 mip) noexcept
    {
        rendering::TextureUseDesc result;
        result.requiredState = rhi::ResourceState::UnorderedAccess;
        result.subresources = {mip, 1, 0, 1};
        result.access = rendering::LogicalAccessIntent::Write;
        result.content = rendering::ResourceContentIntent::Discard;
        return result;
    }

    [[nodiscard]] rendering::TextureUseDesc TextureReadUse(const u16 mip) noexcept
    {
        rendering::TextureUseDesc result;
        result.requiredState = rhi::ResourceState::ShaderResourceGraphics;
        result.subresources = {mip, 1, 0, 1};
        result.access = rendering::LogicalAccessIntent::Read;
        result.content = rendering::ResourceContentIntent::Preserve;
        return result;
    }

    [[nodiscard]] bool BuildAndExecuteFrame(const bool reverseWriterClose, FrameResult& result, CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize(MakeLogicalOnlyConfig(), &failure))
            return false;

        if (!allocator.BeginFrame(reverseWriterClose ? 2 : 1, {}, &failure))
            return false;

        constexpr rendering::RenderFlowNodeId nodeA{1};
        constexpr rendering::RenderFlowNodeId nodeB{2};
        constexpr rendering::RenderFlowNodeId nodeC{3};
        constexpr rendering::GpuFlowGroupId flowA{1};
        constexpr rendering::GpuFlowGroupId flowB{2};
        constexpr rendering::GpuFlowGroupId flowC{3};
        constexpr rendering::CommandScopeId scopeA{11};
        constexpr rendering::CommandScopeId scopeB{12};
        constexpr rendering::CommandScopeId scopeC{13};
        constexpr rendering::FlowSpaceId cameraSpace{7};
        constexpr rendering::ResourceScopeId lifetimeScope{0x1234};

        rendering::ResourcePlanningWriter writerA;
        rendering::ResourcePlanningWriter writerB;
        rendering::ResourcePlanningWriter writerC;
        if (!allocator.CreatePlanningWriter(nodeA, flowA, scopeA, writerA, &failure) || !allocator.CreatePlanningWriter(nodeB, flowB, scopeB, writerB, &failure) ||
            !allocator.CreatePlanningWriter(nodeC, flowC, scopeC, writerC, &failure))
            return false;

        const rendering::LogicalResourceKey colorKey{cameraSpace, "frame.color"};
        rendering::LogicalResourceId colorA;
        rendering::LogicalResourceId colorB;
        rendering::LogicalResourceId colorC;
        rendering::LogicalResourceId temporaryB;
        rendering::LogicalResourceId secondTemporaryB;
        rendering::LogicalBufferViewId temporaryViewB;
        rendering::ResourceUseId writeA;
        rendering::ResourceUseId readB;
        rendering::ResourceUseId writeB;
        rendering::ResourceUseId secondWriteB;
        rendering::ResourceUseId readC;
        rendering::DecisionId decisionB;
        const rendering::FrameBufferDesc desc = MakeBufferDesc();

        if (!writerA.DeclareBuffer(colorKey, desc, colorA, &failure) || !writerA.BeginBufferUse(colorA, WriteUse(), writeA, &failure) || !writerA.EndUse(writeA, &failure) ||
            !writerA.OpenResourceScope(colorA, lifetimeScope, &failure) || !writerB.ReferenceResource(colorKey, colorB, &failure) ||
            !writerB.DeclareTemporaryBuffer("post-process scratch", desc, temporaryB, &failure) ||
            !writerB.CreateBufferView(temporaryB, {rhi::Format::Unknown, 0, 0, 0}, temporaryViewB, &failure) || !writerB.BeginBufferUse(colorB, ReadUse(), readB, &failure) ||
            !writerB.BeginBufferViewUse(temporaryViewB, WriteUse(), writeB, &failure) || !writerB.EndUse(readB, &failure) || !writerB.EndUse(writeB, &failure) ||
            !writerB.DeclareTemporaryBuffer("post-process scratch", desc, secondTemporaryB, &failure) || !writerB.BeginBufferUse(secondTemporaryB, WriteUse(), secondWriteB, &failure) ||
            !writerB.EndUse(secondWriteB, &failure) || !writerB.SwapLogicalMappings(colorB, temporaryB, &failure) || !writerB.CaptureDecision(true, decisionB, &failure) ||
            !writerC.ReferenceResource(colorKey, colorC, &failure) || !writerC.BeginBufferUse(colorC, ReadUse(), readC, &failure) || !writerC.EndUse(readC, &failure) ||
            !writerC.CloseResourceScope(lifetimeScope, &failure))
            return false;

        const bool closed = reverseWriterClose ? writerC.Close(&failure) && writerB.Close(&failure) && writerA.Close(&failure)
                                               : writerA.Close(&failure) && writerB.Close(&failure) && writerC.Close(&failure);
        if (!closed || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return false;

        if (!allocator.Resolve(nullptr, &failure))
            return false;
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        allocator.CancelBeforePublication();
        check(allocator.GetState() == rendering::RenderFlowResourceSessionState::Ready && generation.IsValid(), "pre-publication cancellation cannot detach a published execution generation");

        rendering::CompiledExecutionPacketView packetA;
        rendering::CompiledExecutionPacketView packetB;
        rendering::CompiledExecutionPacketView packetC;
        rendering::CompiledExecutionRange legacyRangeA;
        check(!allocator.PacketFor(nodeA, packetA, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "packet lookup requires an explicit execution transition");
        if (!allocator.BeginExecution(&failure))
            return false;
        check(!allocator.BeginExecution(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "execution transition is coordinator-owned and occurs exactly once");
        if (!allocator.PacketFor(nodeA, packetA, &failure) || !allocator.PacketFor(nodeB, packetB, &failure) || !allocator.PacketFor(nodeC, packetC, &failure) ||
            !packetA.ResolveExecutionRange(0, 1, legacyRangeA, &failure))
            return false;

        rendering::ExecutionPacketCursor cursorA;
        rendering::ExecutionPacketCursor cursorB;
        rendering::ExecutionPacketCursor cursorC;
        check(!packetA.OpenCursor(scopeA, rhi::QueueType::Compute, cursorA, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
              "execution packet rejects the wrong active queue");
        if (!packetA.OpenCursor(scopeA, rhi::QueueType::Graphics, cursorA, &failure) || !packetB.OpenCursor(scopeB, rhi::QueueType::Graphics, cursorB, &failure) ||
            !packetC.OpenCursor(scopeC, rhi::QueueType::Graphics, cursorC, &failure))
            return false;

        rendering::ResolvedBufferUse resolvedA;
        rendering::ResolvedBufferUse resolvedReadB;
        rendering::ResolvedBufferUse resolvedWriteB;
        rendering::ResolvedBufferUse resolvedSecondWriteB;
        rendering::ResolvedBufferUse resolvedC;
        rendering::ResolvedTextureUse wrongKind;
        rendering::ExecutionPacketCursor duplicateCursor;
        check(!packetA.OpenCursor(scopeA, rhi::QueueType::Graphics, duplicateCursor, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase && !duplicateCursor.IsValid() && cursorA.IsValid(),
              "sequential duplicate packet claim fails without disturbing its recording owner");
        check(!cursorB.BeginTextureUse(readB, wrongKind, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
              "execution cursor rejects the wrong resource kind");
        check(!cursorB.BeginBufferUse(writeB, resolvedWriteB, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
              "execution cursor rejects reordered resource use");
        check(!cursorA.FinalizePacket(&failure), "execution cursor rejects an incomplete packet");
        if (!cursorA.BeginBufferUse(writeA, resolvedA, &failure))
            return false;
        check(!cursorA.BeginRange(legacyRangeA, 1, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "compiled-range execution cannot follow a packet that already entered legacy per-use replay");
        result.firstAllocation = resolvedA.GetPhysicalResource().index;
        check(resolvedA.IsValid(), "resolved buffer use is valid only inside its compiled scope");
        rendering::ExecutionPacketCursor movedCursorA(static_cast<rendering::ExecutionPacketCursor&&>(cursorA));
        check(!cursorA.IsValid() && movedCursorA.IsValid() && resolvedA.IsValid(), "cursor move construction transfers its active use without invalidation");
        cursorA = static_cast<rendering::ExecutionPacketCursor&&>(movedCursorA);
        check(cursorA.IsValid() && !movedCursorA.IsValid() && resolvedA.IsValid(), "cursor move assignment to an empty owner preserves packet execution");
        if (!cursorA.EndUse(writeA, &failure))
            return false;
        check(!resolvedA.IsValid(), "resolved buffer use expires at matching EndUse");
        if (!cursorA.FinalizePacket(&failure))
            return false;

        bool decisionValue = false;
        if (!cursorB.CapturedDecision(decisionB, decisionValue, &failure) || !decisionValue || !cursorB.BeginBufferUse(readB, resolvedReadB, &failure) ||
            !cursorB.BeginBufferUse(writeB, resolvedWriteB, &failure))
            return false;
        check(resolvedReadB.GetPhysicalResource().index == result.firstAllocation, "named logical resource keeps its physical assignment before swap");
        check(resolvedReadB.IsValid() && resolvedWriteB.IsValid(), "one packet can hold several independently tracked resource uses at once");
        if (!cursorB.EndUse(readB, &failure))
            return false;
        check(!resolvedReadB.IsValid() && resolvedWriteB.IsValid(), "ending one overlapping use does not invalidate another active use");
        result.temporaryAllocation = resolvedWriteB.GetPhysicalResource().index;
        check(resolvedWriteB.HasExplicitView() && resolvedWriteB.GetViewDesc().size == desc.active.size, "compiled buffer view survives planning as a typed execution binding");
        if (!cursorB.EndUse(writeB, &failure) || !cursorB.BeginBufferUse(secondWriteB, resolvedSecondWriteB, &failure))
            return false;
        check(resolvedSecondWriteB.GetPhysicalResource().index != result.temporaryAllocation, "temporary identity comes from declaration position rather than display name");
        if (!cursorB.EndUse(secondWriteB, &failure) || !cursorB.FinalizePacket(&failure))
            return false;

        if (!cursorC.BeginBufferUse(readC, resolvedC, &failure))
            return false;
        result.postSwapAllocation = resolvedC.GetPhysicalResource().index;
        if (!cursorC.EndUse(readC, &failure) || !cursorC.FinalizePacket(&failure))
            return false;

        const rendering::CommandScopeExecutionReceipt receipts[] = {{scopeB, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                                                                    {scopeC, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                                                                    {scopeA, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
        const rendering::ExecutionGenerationId wrongGeneration{generation.index, generation.generation + 1u};
        const rendering::TerminalExecutionReceipt wrongTerminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                                 containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                 rendering::TerminalJoinToken::CompletedSynchronously(wrongGeneration)};
        check(!allocator.Finish(wrongTerminal, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
                  allocator.GetState() == rendering::RenderFlowResourceSessionState::Executing,
              "terminal join proof is generation-bound and invalid proof does not mutate execution state");
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                            containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                            rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure))
            return false;
        rendering::ExecutionPacketCursor staleCursor;
        check(!packetA.OpenCursor(scopeA, rhi::QueueType::Graphics, staleCursor, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "terminal execution generation rejects stale packet replay");
        cursorA = {};
        cursorB = {};
        cursorC = {};
        packetA = {};
        packetB = {};
        packetC = {};
        resolvedReadB = {};
        resolvedWriteB = {};
        resolvedSecondWriteB = {};
        resolvedC = {};
        check(!resolvedA.IsValid(), "expired resolved use retains only a packet-local liveness witness for a memory-safe stale check");
        resolvedA = {};
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        check(stats.state == rendering::RenderFlowResourceSessionState::Idle && stats.completedFrames == 1 && stats.compiledPackets == 3 && stats.compiledResourceActions != 0,
              "successful terminal receipt returns the allocator to Idle");
        return allocator.Shutdown(&failure);
    }

    void TestDeterministicMergeAndExecution(CheckFunction check) noexcept
    {
        FrameResult forward;
        FrameResult reverse;
        const bool forwardOk = BuildAndExecuteFrame(false, forward, check);
        const bool reverseOk = BuildAndExecuteFrame(true, reverse, check);
        check(forwardOk && reverseOk, "logical resource frames compile and execute without an RHI device");
        if (!forwardOk || !reverseOk)
            return;
        check(forward.firstAllocation != forward.temporaryAllocation && forward.postSwapAllocation == forward.temporaryAllocation, "logical swap changes only later resource uses");
        check(forward.firstAllocation == reverse.firstAllocation && forward.temporaryAllocation == reverse.temporaryAllocation && forward.postSwapAllocation == reverse.postSwapAllocation,
              "planning writer close order does not affect deterministic physical assignment");
    }

    void TestActiveUseSwapCaptureAndScopeMapping(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        constexpr rendering::RenderFlowNodeId node{8};
        constexpr rendering::GpuFlowGroupId flow{8};
        constexpr rendering::CommandScopeId commandScope{8};
        constexpr rendering::FlowSpaceId space{8};
        constexpr rendering::ResourceScopeId explicitScope{0x8a11u};

        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId logicalA;
        rendering::LogicalResourceId logicalB;
        rendering::ResourceUseId beforeSwap;
        rendering::ResourceUseId afterSwap;
        const rendering::FrameBufferDesc desc = MakeBufferDesc();
        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(150, {}, &failure) &&
                             allocator.CreatePlanningWriter(node, flow, commandScope, writer, &failure) &&
                             writer.DeclareBuffer({space, "swap.capture.a"}, desc, logicalA, &failure) &&
                             writer.DeclareTemporaryBuffer("swap capture b", desc, logicalB, &failure) &&
                             writer.OpenResourceScope(logicalA, &failure) && writer.OpenResourceScope(logicalA, explicitScope, &failure) &&
                             writer.BeginBufferUse(logicalA, WriteUse(), beforeSwap, &failure) && writer.SwapLogicalMappings(logicalA, logicalB, &failure) &&
                             writer.BeginBufferUse(logicalA, WriteUse(), afterSwap, &failure) &&
                             // Named-scope close resolves the current slot mapping: after the
                             // swap, logicalB names the allocation on which logicalA opened it.
                             writer.CloseResourceScope(logicalB, &failure) && writer.CloseResourceScope(explicitScope, &failure) &&
                             writer.EndUse(beforeSwap, &failure) && writer.EndUse(afterSwap, &failure) && writer.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && allocator.Resolve(nullptr, &failure);
        check(planned, "active-use swap capture with named/current and explicit/captured scope semantics resolves successfully");
        if (!planned)
        {
            writer.Abandon();
            allocator.CancelBeforePublication();
            static_cast<void>(allocator.Shutdown(&failure));
            return;
        }

        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolvedBefore;
        rendering::ResolvedBufferUse resolvedAfter;
        const bool opened = allocator.BeginExecution(&failure) && allocator.PacketFor(node, packet, &failure) &&
                            packet.OpenCursor(commandScope, rhi::QueueType::Graphics, cursor, &failure) &&
                            cursor.BeginBufferUse(beforeSwap, resolvedBefore, &failure) && cursor.BeginBufferUse(afterSwap, resolvedAfter, &failure);
        check(opened && resolvedBefore.GetPhysicalResource().IsValid() && resolvedAfter.GetPhysicalResource().IsValid() &&
                  resolvedBefore.GetPhysicalResource() != resolvedAfter.GetPhysicalResource(),
              "BeginUse keeps the pre-swap allocation while a later BeginUse observes the post-swap mapping");
        const bool executed = opened && cursor.EndUse(beforeSwap, &failure) && cursor.EndUse(afterSwap, &failure) && cursor.FinalizePacket(&failure);
        const rendering::CommandScopeExecutionReceipt receipts[] = {
            {commandScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        check(executed && allocator.Finish(terminal, &failure) && allocator.Shutdown(&failure),
              "active-use swap capture and scope mapping execute and retire cleanly");
    }

    void TestDenseResourceUseIdentity(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        constexpr rendering::RenderFlowNodeId node{9};
        constexpr rendering::GpuFlowGroupId flow{9};
        constexpr rendering::CommandScopeId scope{9};
        constexpr rendering::FlowSpaceId space{9};

        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId first;
        rendering::ResourceUseId second;
        rendering::ResourceUseId third;
        rendering::DecisionId decision;
        const bool recorded = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(151, {}, &failure) &&
                              allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
                              writer.DeclareBuffer({space, "dense.use.identity"}, MakeBufferDesc(), resource, &failure) &&
                              writer.BeginBufferUse(resource, WriteUse(), first, &failure) && writer.CaptureDecision(true, decision, &failure) && writer.EndUse(first, &failure) &&
                              writer.OpenResourceScope(resource, &failure) && writer.BeginBufferUse(resource, ReadUse(), second, &failure) && writer.CloseResourceScope(resource, &failure) &&
                              writer.EndUse(second, &failure) && writer.BeginBufferUse(resource, ReadUse(), third, &failure);
        check(recorded && first.ordinal == 0 && second.ordinal == 1 && third.ordinal == 2 && first.flowGroup == flow && second.flowGroup == flow && third.flowGroup == flow &&
                  first.generation == second.generation && second.generation == third.generation,
              "ResourceUseId ordinal is dense over uses and is unaffected by interleaved non-use semantic events");

        rendering::ResourcePlanningWriter moved(static_cast<rendering::ResourcePlanningWriter&&>(writer));
        check(!writer.IsValid() && moved.IsValid() && moved.EndUse(third, &failure) && moved.Close(&failure) &&
                  allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "a live dense use survives planning-writer move ownership and seals through its moved writer");

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolved[3];
        const bool resolvedPlan = allocator.Resolve(nullptr, &failure);
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::RenderFlowResourceAllocatorStats resolvedStats = allocator.GetStats();
        check(resolvedPlan && resolvedStats.compiledOperations == 10,
              "dense EndUse endpoints preserve semantic-operation accounting without occupying generic operation records");
        bool executed = resolvedPlan && allocator.BeginExecution(&failure) && allocator.PacketFor(node, packet, &failure) &&
                        packet.OpenCursor(scope, rhi::QueueType::Graphics, cursor, &failure) &&
                        cursor.BeginBufferUse(first, resolved[0], &failure) && cursor.EndUse(first, &failure) &&
                        cursor.BeginBufferUse(second, resolved[1], &failure) && cursor.EndUse(second, &failure) &&
                        cursor.BeginBufferUse(third, resolved[2], &failure) && cursor.EndUse(third, &failure) && cursor.FinalizePacket(&failure);
        const rendering::CommandScopeExecutionReceipt receipts[] = {
            {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        check(executed && allocator.Finish(terminal, &failure), "dense resource-use identities execute in their authored begin/end order");

        rendering::ResourcePlanningWriter staleWriter;
        rendering::LogicalResourceId staleResource;
        rendering::ResourceUseId current;
        check(allocator.BeginFrame(152, {}, &failure) && allocator.CreatePlanningWriter(node, flow, scope, staleWriter, &failure) &&
                  staleWriter.DeclareBuffer({space, "dense.use.stale"}, MakeBufferDesc(), staleResource, &failure) && staleWriter.BeginBufferUse(staleResource, WriteUse(), current, &failure) &&
                  !staleWriter.EndUse(first, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope && !staleWriter.Close(&failure),
              "a prior-generation dense ResourceUseId cannot end a current-frame use even when its ordinal and flow group match");
        allocator.CancelBeforePublication();

        rendering::ResourcePlanningWriter unendedWriter;
        rendering::LogicalResourceId unendedResource;
        rendering::ResourceUseId unended;
        const bool unendedPlanned = allocator.BeginFrame(153, {}, &failure) && allocator.CreatePlanningWriter(node, flow, scope, unendedWriter, &failure) &&
                                    unendedWriter.DeclareBuffer({space, "dense.use.unended"}, MakeBufferDesc(), unendedResource, &failure) &&
                                    unendedWriter.BeginBufferUse(unendedResource, WriteUse(), unended, &failure) && unendedWriter.Close(&failure) &&
                                    allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(unendedPlanned && !allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope,
              "a dense use without an EndUse endpoint fails Resolve atomically");
        check(allocator.Shutdown(&failure), "dense resource-use identity tests leave the allocator cleanup-safe");
    }

    void TestCompiledExecutionRangeTopology(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        constexpr rendering::RenderFlowNodeId node{91};
        constexpr rendering::GpuFlowGroupId flow{91};
        constexpr rendering::CommandScopeId scope{91};
        constexpr rendering::FlowSpaceId space{91};

        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId resources[3];
        rendering::ResourceUseId uses[3];
        const bool planned =
            allocator.Initialize(MakeLogicalOnlyConfig(), &failure) &&
            allocator.BeginFrame(915, {}, &failure) &&
            allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
            writer.DeclareBuffer({space, "execution.range.a"}, MakeBufferDesc(), resources[0], &failure) &&
            writer.DeclareBuffer({space, "execution.range.b"}, MakeBufferDesc(), resources[1], &failure) &&
            writer.DeclareBuffer({space, "execution.range.c"}, MakeBufferDesc(), resources[2], &failure) &&
            writer.BeginBufferUse(resources[0], WriteUse(), uses[0], &failure) &&
            writer.BeginBufferUse(resources[1], WriteUse(), uses[1], &failure) &&
            writer.EndUse(uses[1], &failure) &&
            writer.EndUse(uses[0], &failure) &&
            writer.BeginBufferUse(resources[2], WriteUse(), uses[2], &failure) &&
            writer.EndUse(uses[2], &failure) &&
            writer.Close(&failure) &&
            allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
            allocator.Resolve(nullptr, &failure) &&
            allocator.BeginExecution(&failure);
        check(planned, "compiled execution-range topology frame plans and resolves");
        if (!planned)
        {
            allocator.CancelBeforePublication();
            static_cast<void>(allocator.Shutdown(&failure));
            return;
        }

        rendering::CompiledExecutionPacketView packet;
        rendering::CompiledExecutionRange firstRange;
        rendering::CompiledExecutionRange secondRange;
        rendering::CompiledExecutionRange emptyBeforeSecond;
        rendering::CompiledExecutionRange emptyAtEnd;
        rendering::CompiledExecutionRange malformed;
        const bool published =
            allocator.PacketFor(node, packet, &failure) &&
            packet.GetUseCount() == 3 &&
            packet.GetStepCount() == 6 &&
            packet.ResolveExecutionRange(0, 2, firstRange, &failure) &&
            packet.ResolveExecutionRange(2, 1, secondRange, &failure) &&
            packet.ResolveExecutionRange(2, 0, emptyBeforeSecond, &failure) &&
            packet.ResolveExecutionRange(3, 0, emptyAtEnd, &failure);
        check(published &&
                  firstRange.beginStepOffset == 0 && firstRange.beginStepCount == 2 &&
                  firstRange.endStepOffset == 2 && firstRange.endStepCount == 2 &&
                  firstRange.beginActions.count == 2 && firstRange.endActions.Empty() &&
                  secondRange.beginStepOffset == 4 && secondRange.beginStepCount == 1 &&
                  secondRange.endStepOffset == 5 && secondRange.endStepCount == 1 &&
                  secondRange.beginActions.count == 1 && secondRange.endActions.Empty() &&
                  emptyBeforeSecond.beginStepOffset == 4 && emptyBeforeSecond.endStepOffset == 4 &&
                  emptyBeforeSecond.beginStepCount == 0 && emptyBeforeSecond.endStepCount == 0 &&
                  emptyBeforeSecond.beginActions.Empty() && emptyBeforeSecond.endActions.Empty() &&
                  emptyAtEnd.beginStepOffset == 6 && emptyAtEnd.endStepOffset == 6 &&
                  emptyAtEnd.beginStepCount == 0 && emptyAtEnd.endStepCount == 0 &&
                  emptyAtEnd.beginActions.Empty() && emptyAtEnd.endActions.Empty() &&
                  packet.GetExitActionSegment().Empty(),
              "compiled execution ranges preserve exact nested begin/reverse-end topology, semantic begin-entry cardinality, and zero-use boundaries");

        check(!packet.ResolveExecutionRange(0, 3, malformed, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
              "compiled execution-range publication rejects a declaration grouping that crosses a real packet child boundary");

        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse direct[3];
        rendering::ResolvedBufferUse wrongEpochDirect;
        rendering::ResolvedBufferUse forbiddenLegacy;
        rendering::CompiledExecutionRange corruptedFirstRange = firstRange;
        ++corruptedFirstRange.endStepOffset;
        const bool directPublished =
            packet.ResolveBufferUseBinding(uses[0], 1, direct[0], &failure) &&
            packet.ResolveBufferUseBinding(uses[1], 1, direct[1], &failure) &&
            packet.ResolveBufferUseBinding(uses[2], 3, direct[2], &failure) &&
            packet.ResolveBufferUseBinding(uses[0], 3, wrongEpochDirect, &failure);
        check(directPublished && !direct[0].IsValid() && !direct[1].IsValid() && !direct[2].IsValid() && !wrongEpochDirect.IsValid(),
              "compiled-range direct witnesses remain dormant until their exact child epoch begins");

        const bool executed =
            directPublished && packet.OpenCompiledCursor(scope, rhi::QueueType::Graphics, 4, cursor, &failure) &&
            !cursor.BeginRange(corruptedFirstRange, 1, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            !cursor.BeginRange(emptyBeforeSecond, 2, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            cursor.BeginRange(firstRange, 1, &failure) && direct[0].IsValid() && direct[1].IsValid() && !direct[2].IsValid() && !wrongEpochDirect.IsValid() &&
            !cursor.BeginRange(emptyBeforeSecond, 2, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            !cursor.FinalizePacket(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            direct[0].IsValid() && direct[1].IsValid() &&
            !cursor.BeginBufferUse(uses[0], forbiddenLegacy, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase &&
            !cursor.EndRange(firstRange, 2, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            direct[0].IsValid() && direct[1].IsValid() &&
            cursor.EndRange(firstRange, 1, &failure) && !direct[0].IsValid() && !direct[1].IsValid() &&
            cursor.BeginRange(emptyBeforeSecond, 2, &failure) && cursor.EndRange(emptyBeforeSecond, 2, &failure) &&
            cursor.BeginRange(secondRange, 3, &failure) && direct[2].IsValid() && !wrongEpochDirect.IsValid() &&
            cursor.EndRange(secondRange, 3, &failure) && !direct[2].IsValid() &&
            !cursor.FinalizePacket(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
            cursor.BeginRange(emptyAtEnd, 4, &failure) && cursor.EndRange(emptyAtEnd, 4, &failure) &&
            cursor.FinalizePacket(&failure);
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {
            {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        check(executed && allocator.Finish(terminal, &failure) && allocator.Shutdown(&failure),
              "compiled execution ranges execute directly, reject skipped or mismatched epochs without corrupting the active range, authenticate direct-witness range membership, preserve ordered zero-use epochs, reject legacy mixing, and expire direct witnesses at child end");

        rendering::RenderFlowResourceAllocator zeroAllocator;
        rendering::ResourcePlanningWriter zeroWriter;
        rendering::ResourcePlanningWriter zeroNoRangeWriter;
        constexpr rendering::RenderFlowNodeId zeroNode{93};
        constexpr rendering::RenderFlowNodeId zeroNoRangeNode{94};
        constexpr rendering::GpuFlowGroupId zeroFlow{93};
        constexpr rendering::GpuFlowGroupId zeroNoRangeFlow{94};
        constexpr rendering::CommandScopeId zeroScope{93};
        constexpr rendering::CommandScopeId zeroNoRangeScope{94};
        const bool zeroPlanned =
            zeroAllocator.Initialize(MakeLogicalOnlyConfig(), &failure) &&
            zeroAllocator.BeginFrame(917, {}, &failure) &&
            zeroAllocator.CreatePlanningWriter(zeroNode, zeroFlow, zeroScope, zeroWriter, &failure) &&
            zeroAllocator.CreatePlanningWriter(zeroNoRangeNode, zeroNoRangeFlow, zeroNoRangeScope, zeroNoRangeWriter, &failure) &&
            zeroWriter.Close(&failure) && zeroNoRangeWriter.Close(&failure) &&
            zeroAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
            zeroAllocator.Resolve(nullptr, &failure) && zeroAllocator.BeginExecution(&failure);
        check(zeroPlanned, "all-zero compiled-range frame plans and resolves");
        if (zeroPlanned)
        {
            rendering::CompiledExecutionPacketView zeroPacket;
            rendering::CompiledExecutionRange zeroRange;
            rendering::ExecutionPacketCursor zeroCursor;
            rendering::CompiledExecutionPacketView zeroNoRangePacket;
            rendering::ExecutionPacketCursor zeroNoRangeCursor;
            const bool zeroOpened =
                zeroAllocator.PacketFor(zeroNode, zeroPacket, &failure) &&
                zeroPacket.ResolveExecutionRange(0, 0, zeroRange, &failure) &&
                zeroPacket.OpenCompiledCursor(zeroScope, rhi::QueueType::Graphics, 2, zeroCursor, &failure);
            const bool zeroNoRangeExecuted =
                zeroAllocator.PacketFor(zeroNoRangeNode, zeroNoRangePacket, &failure) &&
                zeroNoRangePacket.OpenCompiledCursor(zeroNoRangeScope, rhi::QueueType::Graphics, 0, zeroNoRangeCursor, &failure) &&
                zeroNoRangeCursor.FinalizePacket(&failure);
            const bool zeroExecuted =
                zeroOpened &&
                !zeroCursor.FinalizePacket(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
                zeroCursor.BeginRange(zeroRange, 1, &failure) && zeroCursor.EndRange(zeroRange, 1, &failure) &&
                !zeroCursor.FinalizePacket(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
                zeroCursor.BeginRange(zeroRange, 2, &failure) && zeroCursor.EndRange(zeroRange, 2, &failure) &&
                zeroCursor.FinalizePacket(&failure);
            const rendering::ExecutionGenerationId zeroGeneration = zeroAllocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt zeroReceipts[] = {
                {zeroScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                {zeroNoRangeScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt zeroTerminal{zeroGeneration, rendering::TerminalExecutionCompletionKind::Completed,
                                                                   containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(zeroReceipts),
                                                                   rendering::TerminalJoinToken::CompletedSynchronously(zeroGeneration)};
            check(zeroExecuted && zeroNoRangeExecuted && zeroAllocator.Finish(zeroTerminal, &failure) && zeroAllocator.Shutdown(&failure),
                  "compiled cursor finalization requires every declared all-zero child epoch while an explicit zero-range packet finalizes directly");
        }
        else
        {
            zeroWriter.Abandon();
            zeroNoRangeWriter.Abandon();
            zeroAllocator.CancelBeforePublication();
            static_cast<void>(zeroAllocator.Shutdown(&failure));
        }

        rendering::RenderFlowResourceAllocator cancelAllocator;
        rendering::ResourcePlanningWriter cancelWriter;
        rendering::LogicalResourceId cancelResource;
        rendering::ResourceUseId cancelUse;
        constexpr rendering::RenderFlowNodeId cancelNode{92};
        constexpr rendering::GpuFlowGroupId cancelFlow{92};
        constexpr rendering::CommandScopeId cancelScope{92};
        constexpr rendering::FlowSpaceId cancelSpace{92};
        const bool cancelPlanned =
            cancelAllocator.Initialize(MakeLogicalOnlyConfig(), &failure) &&
            cancelAllocator.BeginFrame(916, {}, &failure) &&
            cancelAllocator.CreatePlanningWriter(cancelNode, cancelFlow, cancelScope, cancelWriter, &failure) &&
            cancelWriter.DeclareBuffer({cancelSpace, "execution.range.cancel"}, MakeBufferDesc(), cancelResource, &failure) &&
            cancelWriter.BeginBufferUse(cancelResource, WriteUse(), cancelUse, &failure) &&
            cancelWriter.EndUse(cancelUse, &failure) && cancelWriter.Close(&failure) &&
            cancelAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
            cancelAllocator.Resolve(nullptr, &failure) && cancelAllocator.BeginExecution(&failure);
        check(cancelPlanned, "compiled-range cancellation frame plans and resolves");
        if (cancelPlanned)
        {
            rendering::CompiledExecutionPacketView cancelPacket;
            rendering::CompiledExecutionRange cancelRange;
            rendering::ResolvedBufferUse cancelDirect;
            rendering::ExecutionPacketCursor cancelCursor;
            const bool cancelOpened =
                cancelAllocator.PacketFor(cancelNode, cancelPacket, &failure) &&
                cancelPacket.ResolveExecutionRange(0, 1, cancelRange, &failure) &&
                cancelPacket.ResolveBufferUseBinding(cancelUse, 1, cancelDirect, &failure) &&
                cancelPacket.OpenCompiledCursor(cancelScope, rhi::QueueType::Graphics, 1, cancelCursor, &failure) &&
                cancelCursor.BeginRange(cancelRange, 1, &failure) && cancelDirect.IsValid();
            check(cancelOpened, "compiled-range cancellation activates its direct witness before cancellation");
            if (cancelOpened)
                cancelCursor.CancelRemaining();
            check(cancelOpened && !cancelCursor.IsValid() && !cancelDirect.IsValid(),
                  "CancelRemaining closes compiled-range ownership and expires every active direct witness");

            const rendering::ExecutionGenerationId cancelGeneration = cancelAllocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt cancelReceipts[] = {
                {cancelScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt cancelTerminal{cancelGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                     containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(cancelReceipts),
                                                                     rendering::TerminalJoinToken::CompletedSynchronously(cancelGeneration)};
            check(cancelOpened && cancelAllocator.Finish(cancelTerminal, &failure) && cancelAllocator.Shutdown(&failure),
                  "a canceled compiled range joins and retires cleanly through an aborted terminal receipt");
        }
        else
        {
            cancelWriter.Abandon();
            cancelAllocator.CancelBeforePublication();
            static_cast<void>(cancelAllocator.Shutdown(&failure));
        }
    }

    void TestDedicatedResourceLifetimePlanning(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{10};
        constexpr rendering::GpuFlowGroupId flow{10};
        constexpr rendering::CommandScopeId scope{10};
        constexpr rendering::FlowSpaceId space{10};
        rendering::LogicalResourceId resources[3];
        rendering::ResourceUseId uses[3];
        const rendering::FrameBufferDesc desc = MakeBufferDesc();
        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(3, {}, &failure) &&
                             allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
                             writer.DeclareBuffer({space, "lifetime.a"}, desc, resources[0], &failure) &&
                             writer.DeclareBuffer({space, "lifetime.b"}, desc, resources[1], &failure) &&
                             writer.DeclareBuffer({space, "lifetime.c"}, desc, resources[2], &failure) &&
                             writer.BeginBufferUse(resources[0], WriteUse(), uses[0], &failure) &&
                             writer.BeginBufferUse(resources[1], WriteUse(), uses[1], &failure) &&
                             writer.EndUse(uses[0], &failure) && writer.EndUse(uses[1], &failure) &&
                             writer.BeginBufferUse(resources[2], WriteUse(), uses[2], &failure) && writer.EndUse(uses[2], &failure) &&
                             writer.Close(&failure) && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "dedicated-resource lifetime test seals overlapping and sequential logical uses");
        if (planned)
        {
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::ResolvedBufferUse resolved[3];
            const bool resolvedPlan = allocator.Resolve(nullptr, &failure);
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const bool opened = resolvedPlan && allocator.BeginExecution(&failure) && allocator.PacketFor(node, packet, &failure) &&
                                packet.OpenCursor(scope, rhi::QueueType::Graphics, cursor, &failure);
            bool executed = opened && cursor.BeginBufferUse(uses[0], resolved[0], &failure) &&
                            cursor.BeginBufferUse(uses[1], resolved[1], &failure);
            if (executed)
            {
                const rendering::PhysicalResourceId first = resolved[0].GetPhysicalResource();
                const rendering::PhysicalResourceId overlapping = resolved[1].GetPhysicalResource();
                executed = cursor.EndUse(uses[0], &failure) && cursor.EndUse(uses[1], &failure) &&
                           cursor.BeginBufferUse(uses[2], resolved[2], &failure);
                const rendering::PhysicalResourceId sequential = executed ? resolved[2].GetPhysicalResource() : rendering::PhysicalResourceId{};
                check(first.IsValid() && overlapping.IsValid() && sequential.IsValid() && first != overlapping && sequential == first,
                      "overlapping lifetimes stay distinct while a strictly later compatible lifetime reuses the first dedicated resource");
                executed = executed && cursor.EndUse(uses[2], &failure) && cursor.FinalizePacket(&failure);
            }
            const rendering::CommandScopeExecutionReceipt receipts[] = {
                {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                               containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            check(executed && allocator.Finish(terminal, &failure), "dedicated-resource lifetime plan executes and joins exactly once");
        }
        check(allocator.Shutdown(&failure), "dedicated-resource lifetime planning is cleanup-safe");
    }

    void TestFirstTouchPhysicalAssignmentOrder(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{11};
        constexpr rendering::GpuFlowGroupId flow{11};
        constexpr rendering::CommandScopeId scope{11};
        constexpr rendering::FlowSpaceId space{11};
        rendering::LogicalResourceId declaredFirst;
        rendering::LogicalResourceId touchedFirst;
        rendering::ResourceUseId touchedFirstUse;
        rendering::ResourceUseId declaredFirstUse;
        const rendering::FrameBufferDesc desc = MakeBufferDesc();
        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(4, {}, &failure) &&
                             allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
                             writer.DeclareBuffer({space, "first.touch.declared.first"}, desc, declaredFirst, &failure) &&
                             writer.DeclareBuffer({space, "first.touch.used.first"}, desc, touchedFirst, &failure) &&
                             writer.BeginBufferUse(touchedFirst, WriteUse(), touchedFirstUse, &failure) &&
                             writer.BeginBufferUse(declaredFirst, WriteUse(), declaredFirstUse, &failure) &&
                             writer.EndUse(declaredFirstUse, &failure) && writer.EndUse(touchedFirstUse, &failure) && writer.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "first-touch assignment test seals resources declared in the opposite order from their first use");
        if (planned)
        {
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::ResolvedBufferUse touchedResolved;
            rendering::ResolvedBufferUse declaredResolved;
            const bool resolvedPlan = allocator.Resolve(nullptr, &failure);
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            bool executed = resolvedPlan && allocator.BeginExecution(&failure) && allocator.PacketFor(node, packet, &failure) &&
                            packet.OpenCursor(scope, rhi::QueueType::Graphics, cursor, &failure) &&
                            cursor.BeginBufferUse(touchedFirstUse, touchedResolved, &failure) && cursor.BeginBufferUse(declaredFirstUse, declaredResolved, &failure);
            if (executed)
            {
                const rendering::PhysicalResourceId firstTouchedPhysical = touchedResolved.GetPhysicalResource();
                const rendering::PhysicalResourceId declaredFirstPhysical = declaredResolved.GetPhysicalResource();
                check(firstTouchedPhysical.IsValid() && declaredFirstPhysical.IsValid() && firstTouchedPhysical.index < declaredFirstPhysical.index,
                      "physical assignment follows authoritative first-touch order rather than declaration order");
                executed = cursor.EndUse(declaredFirstUse, &failure) && cursor.EndUse(touchedFirstUse, &failure) && cursor.FinalizePacket(&failure);
            }
            const rendering::CommandScopeExecutionReceipt receipts[] = {
                {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                               containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            check(executed && allocator.Finish(terminal, &failure), "first-touch assignment order executes and joins exactly once");
        }
        check(allocator.Shutdown(&failure), "first-touch physical assignment test is cleanup-safe");
    }

    void TestMissingScopeBeginFailure(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(10, {}, &failure), "broken-scope test starts allocator planning");
        rendering::ResourcePlanningWriter closeWriter;
        const rendering::RenderFlowNodeId closeNode{21};
        check(allocator.CreatePlanningWriter(closeNode, {21}, {21}, closeWriter, &failure), "broken-scope test creates the surviving node writer");
        const rendering::ResourceScopeId scope{77};
        check(closeWriter.CloseResourceScope(scope, &failure) && closeWriter.Close(&failure) &&
                  allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "broken-scope declaration stream seals with only the surviving node");
        const bool resolved = allocator.Resolve(nullptr, &failure);
        check(!resolved && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope && allocator.GetStats().state == rendering::RenderFlowResourceSessionState::Idle &&
                  !allocator.GetExecutionGeneration().IsValid(),
              "a surviving scope close without its culled begin aborts Resolve without publication");
        check(allocator.Shutdown(&failure), "failed Resolve leaves allocator shutdown-safe");
    }

    void TestPoisonedWriterBlocksSeal(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(11, {}, &failure), "writer-poison test starts allocator planning");
        rendering::ResourcePlanningWriter writerA;
        rendering::ResourcePlanningWriter writerB;
        check(allocator.CreatePlanningWriter({30}, {30}, {30}, writerA, &failure) && allocator.CreatePlanningWriter({31}, {31}, {31}, writerB, &failure), "writer-poison test creates writers");
        rendering::LogicalResourceId resourceA;
        rendering::LogicalResourceId resourceB;
        rendering::ResourceUseId forgedUse;
        check(writerA.DeclareBuffer({{2}, "owned.resource"}, MakeBufferDesc(), resourceA, &failure), "first declaration is accepted");
        check(!writerB.BeginBufferUse(resourceA, ReadUse(), forgedUse, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
              "writer-local logical ids cannot be used by another flow group");
        check(!writerB.DeclareBuffer({{2}, "later.resource"}, MakeBufferDesc(), resourceB, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompletePlanning,
              "a rejected operation poisons its writer instead of publishing a partial tape");
        check(writerA.Close(&failure) && !writerB.Close(&failure) && !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::IncompletePlanning,
              "a poisoned writer prevents candidate publication");
        allocator.CancelBeforePublication();
        check(allocator.GetStats().rejectedOperations >= 2 && allocator.Shutdown(&failure), "writer rejection accounting is merged safely at writer close");
    }

    void TestDuplicateDeclaration(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(12, {}, &failure), "duplicate-declaration test starts allocator planning");
        rendering::ResourcePlanningWriter writerA;
        rendering::ResourcePlanningWriter writerB;
        check(allocator.CreatePlanningWriter({30}, {30}, {30}, writerA, &failure) && allocator.CreatePlanningWriter({31}, {31}, {31}, writerB, &failure),
              "duplicate-declaration test creates writers");
        const rendering::LogicalResourceKey key{{2}, "duplicate.resource"};
        rendering::LogicalResourceId resourceA;
        rendering::LogicalResourceId resourceB;
        check(writerA.DeclareBuffer(key, MakeBufferDesc(), resourceA, &failure) && writerB.DeclareBuffer(key, MakeBufferDesc(), resourceB, &failure) && writerA.Close(&failure) &&
                   writerB.Close(&failure) && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "duplicate declaration reaches deterministic Resolve validation");
        check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict &&
                  !allocator.GetExecutionGeneration().IsValid(),
              "duplicate named declaration fails atomically even when descriptors match");
        check(allocator.Shutdown(&failure), "duplicate declaration failure leaves allocator shutdown-safe");
    }

    void TestAggregatePlanningCaps(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.allowLogicalOnlyValidation = true;
            config.maximumLogicalResources = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter first;
            rendering::ResourcePlanningWriter second;
            rendering::LogicalResourceId firstResource;
            rendering::LogicalResourceId secondResource;
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(13, {}, &failure) && allocator.CreatePlanningWriter({50}, {50}, {50}, first, &failure) &&
                      allocator.CreatePlanningWriter({51}, {51}, {51}, second, &failure) && first.ReferenceResource({{5}, "first"}, firstResource, &failure) &&
                      second.ReferenceResource({{5}, "second"}, secondResource, &failure) && first.Close(&failure) && second.Close(&failure) &&
                      !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
                  "logical-resource capacity is an aggregate frame limit independent of writer close order");
            allocator.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "aggregate resource-cap failure remains cancellation-safe");
        }
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.allowLogicalOnlyValidation = true;
            config.maximumLogicalResources = 2;
            config.maximumViews = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter first;
            rendering::ResourcePlanningWriter second;
            rendering::LogicalResourceId firstResource;
            rendering::LogicalResourceId secondResource;
            rendering::LogicalBufferViewId firstView;
            rendering::LogicalBufferViewId secondView;
            const rendering::FrameBufferDesc desc = MakeBufferDesc();
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(14, {}, &failure) && allocator.CreatePlanningWriter({52}, {52}, {52}, first, &failure) &&
                      allocator.CreatePlanningWriter({53}, {53}, {53}, second, &failure) && first.DeclareBuffer({{5}, "first.view"}, desc, firstResource, &failure) &&
                      first.CreateBufferView(firstResource, {}, firstView, &failure) && second.DeclareBuffer({{5}, "second.view"}, desc, secondResource, &failure) &&
                      second.CreateBufferView(secondResource, {}, secondView, &failure) && first.Close(&failure) && second.Close(&failure) &&
                      !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
                  "texture and buffer views share one deterministic aggregate frame capacity");
            allocator.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "aggregate view-cap failure remains cancellation-safe");
        }
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.allowLogicalOnlyValidation = true;
            config.maximumOperations = 8;
            config.maximumOperationsPerPlanningWriter = 2;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId buffer;
            rendering::LogicalResourceId texture;
            rendering::DecisionId decision;
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(15, {}, &failure) && allocator.CreatePlanningWriter({54}, {54}, {54}, writer, &failure) &&
                      writer.DeclareBuffer({{5}, "writer.cap.buffer"}, MakeBufferDesc(), buffer, &failure) && writer.CaptureDecision(true, decision, &failure) &&
                      !writer.DeclareTexture({{5}, "writer.cap.texture"}, MakeTextureDesc(), texture, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded &&
                      !texture.IsValid(),
                  "per-writer operation capacity is a hard local bound below the aggregate frame limit");
            allocator.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "per-writer capacity failure rolls back cold payload state and remains cancellation-safe");
        }
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.allowLogicalOnlyValidation = true;
            config.maximumOperations = 2;
            config.maximumOperationsPerPlanningWriter = 2;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::DecisionId decision;
            constexpr rendering::GpuFlowGroupId flow{55};
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(16, {}, &failure) &&
                      allocator.RequestBeginQueue(rhi::QueueType::Graphics, flow, &failure) && allocator.RequestEndQueue(flow, &failure) &&
                      allocator.CreatePlanningWriter({55}, flow, {55}, writer, &failure) && writer.CaptureDecision(true, decision, &failure) && writer.Close(&failure) &&
                      !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
                  "recorded queue requests and writer operations share the aggregate operation capacity");
            allocator.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "aggregate queue-request capacity failure remains cancellation-safe");
        }
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.allowLogicalOnlyValidation = true;
            config.maximumOperations = 2;
            config.maximumOperationsPerPlanningWriter = 2;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(17, {}, &failure) && allocator.CreatePlanningWriter({56}, {56}, {56}, writer, &failure) &&
                      writer.DeclareBuffer({{5}, "dense.use.cap"}, MakeBufferDesc(), resource, &failure) && writer.BeginBufferUse(resource, WriteUse(), use, &failure) &&
                      !writer.EndUse(use, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded && !writer.Close(&failure),
                  "dense EndUse rejects directly at the configured semantic operation budget and poisons the writer transactionally");
            allocator.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "dense EndUse capacity rejection remains cancellation-safe");
        }
    }

    void TestRangeAndSubresourceValidation(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId texture;
            rendering::ResourceUseId writeMip;
            rendering::ResourceUseId readOtherMip;
            check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(17, {}, &failure) && allocator.CreatePlanningWriter({70}, {70}, {70}, writer, &failure) &&
                      writer.DeclareTexture({{7}, "mipped.texture"}, MakeTextureDesc(), texture, &failure) && writer.BeginTextureUse(texture, TextureWriteUse(0), writeMip, &failure) &&
                      writer.EndUse(writeMip, &failure) && writer.BeginTextureUse(texture, TextureReadUse(1), readOtherMip, &failure) && writer.EndUse(readOtherMip, &failure) &&
                      writer.Close(&failure) && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "subresource-content test seals its candidate tape");
            check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope &&
                      !allocator.GetExecutionGeneration().IsValid(),
                  "writing one mip does not make a different mip's contents defined");
            check(allocator.Shutdown(&failure), "subresource-content failure leaves no published generation");
        }
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId texture;
            rendering::LogicalTextureViewId view;
            rhi::TextureViewDesc invalidView;
            invalidView.subresources = {2, 1, 0, 1};
            check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(18, {}, &failure) && allocator.CreatePlanningWriter({71}, {71}, {71}, writer, &failure) &&
                      writer.DeclareTexture({{7}, "invalid.view.texture"}, MakeTextureDesc(), texture, &failure) && writer.CreateTextureView(texture, invalidView, view, &failure) &&
                      writer.Close(&failure) && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "invalid texture-view test reaches Resolve validation");
            check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict &&
                      !allocator.GetExecutionGeneration().IsValid(),
                  "out-of-range texture views fail before execution generation publication");
            check(allocator.Shutdown(&failure), "invalid texture-view failure is cleanup-safe");
        }
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId buffer;
            rendering::LogicalBufferViewId view;
            const rendering::FrameBufferDesc desc = MakeBufferDesc();
            check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(19, {}, &failure) && allocator.CreatePlanningWriter({72}, {72}, {72}, writer, &failure) &&
                      writer.DeclareBuffer({{7}, "invalid.view.buffer"}, desc, buffer, &failure) &&
                      writer.CreateBufferView(buffer, {rhi::Format::Unknown, desc.active.size - 8, 32, 0}, view, &failure) && writer.Close(&failure) &&
                      allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "invalid buffer-view test reaches Resolve validation");
            check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict &&
                      !allocator.GetExecutionGeneration().IsValid(),
                  "overflowing buffer views fail before execution generation publication");
            check(allocator.Shutdown(&failure), "invalid buffer-view failure is cleanup-safe");
        }
    }

    void TestStateAndActiveUseValidation(CheckFunction check) noexcept
    {
        const auto runCase = [check](const vanguard::u64 frameSerial, const rhi::QueueType queue, const bool expectSuccess, const char* const message, auto&& record) noexcept
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::RenderFlowResourceFailure failure;
            rendering::ResourcePlanningWriter writer;
            constexpr rendering::RenderFlowNodeId node{80};
            constexpr rendering::GpuFlowGroupId flow{80};
            constexpr rendering::CommandScopeId scope{80};
            bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(frameSerial, {}, &failure);
            if (planned && queue != rhi::QueueType::Graphics)
                planned = allocator.RequestBeginQueue(queue, flow, &failure) && allocator.RequestEndQueue(flow, &failure);
            planned = planned && allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) && record(writer, failure) && writer.Close(&failure) &&
                      allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
            check(planned, message);
            if (!planned)
            {
                writer.Abandon();
                allocator.CancelBeforePublication();
                check(allocator.Shutdown(&failure), "failed state-validation setup remains shutdown-safe");
                return;
            }

            const bool resolved = allocator.Resolve(nullptr, &failure);
            check(expectSuccess ? resolved : !resolved && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope, message);
            if (resolved)
            {
                const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, queue, {}}};
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                   containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                   rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                check(allocator.Finish(terminal, &failure), "valid logical state plan can be abandoned before command submission");
            }
            check(allocator.Shutdown(&failure), "state-validation case leaves the allocator shutdown-safe");
        };

        runCase(20, rhi::QueueType::Graphics, false, "write access cannot masquerade as a shader-resource state",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    rendering::BufferUseDesc invalid = WriteUse();
                    invalid.requiredState = rhi::ResourceState::ShaderResourceGraphics;
                    return writer.DeclareBuffer({{8}, "invalid.access.state"}, MakeBufferDesc(), buffer, &failure) && writer.BeginBufferUse(buffer, invalid, use, &failure) &&
                           writer.EndUse(use, &failure);
                });

        runCase(21, rhi::QueueType::Copy, false, "copy queue rejects UAV resource use",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    return writer.DeclareBuffer({{8}, "copy.queue.uav"}, MakeBufferDesc(), buffer, &failure) && writer.BeginBufferUse(buffer, WriteUse(), use, &failure) &&
                           writer.EndUse(use, &failure);
                });

        runCase(22, rhi::QueueType::Graphics, false, "texture write and shader-read states cannot form one active texture layout",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::FrameTextureDesc desc = MakeTextureDesc();
                    desc.active.usage = desc.active.usage | rhi::TextureUsage::CopyDestination;
                    rendering::TextureUseDesc invalid = TextureWriteUse(0);
                    invalid.requiredState = rhi::ResourceState::CopyDestination | rhi::ResourceState::ShaderResourceGraphics;
                    invalid.access = rendering::LogicalAccessIntent::ReadWrite;
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId use;
                    return writer.DeclareTexture({{8}, "invalid.texture.mask"}, desc, texture, &failure) && writer.BeginTextureUse(texture, invalid, use, &failure) &&
                           writer.EndUse(use, &failure);
                });

        runCase(23, rhi::QueueType::Graphics, false, "whole-buffer read and write uses cannot remain active together",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId read;
                    rendering::ResourceUseId write;
                    return writer.DeclareBuffer({{8}, "overlapping.buffer"}, MakeBufferDesc(), buffer, &failure) && writer.BeginBufferUse(buffer, WriteUse(), initialize, &failure) &&
                           writer.EndUse(initialize, &failure) && writer.BeginBufferUse(buffer, ReadUse(), read, &failure) && writer.BeginBufferUse(buffer, WriteUse(), write, &failure) &&
                           writer.EndUse(read, &failure) && writer.EndUse(write, &failure);
                });

        runCase(24, rhi::QueueType::Graphics, false, "overlapping texture subresources reject simultaneous read and write uses",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId read;
                    rendering::ResourceUseId write;
                    return writer.DeclareTexture({{8}, "overlapping.texture"}, MakeTextureDesc(), texture, &failure) &&
                           writer.BeginTextureUse(texture, TextureWriteUse(0), initialize, &failure) && writer.EndUse(initialize, &failure) &&
                           writer.BeginTextureUse(texture, TextureReadUse(0), read, &failure) && writer.BeginTextureUse(texture, TextureWriteUse(0), write, &failure) &&
                           writer.EndUse(read, &failure) && writer.EndUse(write, &failure);
                });

        runCase(25, rhi::QueueType::Compute, false, "compute queue rejects graphics-only shader state",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId read;
                    return writer.DeclareBuffer({{8}, "compute.graphics.state"}, MakeBufferDesc(), buffer, &failure) && writer.BeginBufferUse(buffer, WriteUse(), initialize, &failure) &&
                           writer.EndUse(initialize, &failure) && writer.BeginBufferUse(buffer, ReadUse(), read, &failure) && writer.EndUse(read, &failure);
                });

        runCase(26, rhi::QueueType::Compute, true, "compute buffer accepts UAV read-write and identical combined read-state overlaps",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::FrameBufferDesc desc = MakeBufferDesc();
                    desc.active.usage = desc.active.usage | rhi::BufferUsage::IndirectArguments;
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId readWrite;
                    rendering::ResourceUseId firstRead;
                    rendering::ResourceUseId secondRead;
                    rendering::BufferUseDesc readWriteDesc = WriteUse();
                    readWriteDesc.access = rendering::LogicalAccessIntent::ReadWrite;
                    readWriteDesc.content = rendering::ResourceContentIntent::Preserve;
                    rendering::BufferUseDesc combinedRead = ReadUse();
                    combinedRead.requiredState = rhi::ResourceState::ShaderResourceCompute | rhi::ResourceState::IndirectArgument;
                    return writer.DeclareBuffer({{8}, "combined.buffer.read"}, desc, buffer, &failure) && writer.BeginBufferUse(buffer, WriteUse(), initialize, &failure) &&
                           writer.EndUse(initialize, &failure) && writer.BeginBufferUse(buffer, readWriteDesc, readWrite, &failure) && writer.EndUse(readWrite, &failure) &&
                           writer.BeginBufferUse(buffer, combinedRead, firstRead, &failure) && writer.BeginBufferUse(buffer, combinedRead, secondRead, &failure) &&
                           writer.EndUse(firstRead, &failure) && writer.EndUse(secondRead, &failure);
                });

        runCase(27, rhi::QueueType::Graphics, true, "disjoint texture mips may remain active in different compatible states",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId initialize0;
                    rendering::ResourceUseId initialize1;
                    rendering::ResourceUseId firstRead;
                    rendering::ResourceUseId secondRead;
                    rendering::ResourceUseId nestedRead;
                    rendering::TextureUseDesc combinedRead = TextureReadUse(0);
                    combinedRead.requiredState = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
                    rendering::TextureUseDesc uavRead = TextureReadUse(1);
                    uavRead.requiredState = rhi::ResourceState::UnorderedAccess;
                    return writer.DeclareTexture({{8}, "disjoint.texture.states"}, MakeTextureDesc(), texture, &failure) &&
                           writer.BeginTextureUse(texture, TextureWriteUse(0), initialize0, &failure) && writer.EndUse(initialize0, &failure) &&
                           writer.BeginTextureUse(texture, TextureWriteUse(1), initialize1, &failure) && writer.EndUse(initialize1, &failure) &&
                           writer.BeginTextureUse(texture, combinedRead, firstRead, &failure) && writer.BeginTextureUse(texture, combinedRead, nestedRead, &failure) &&
                           writer.BeginTextureUse(texture, uavRead, secondRead, &failure) && writer.EndUse(firstRead, &failure) && writer.EndUse(nestedRead, &failure) &&
                           writer.EndUse(secondRead, &failure);
                });

        runCase(28, rhi::QueueType::Graphics, true, "large same-state buffer read fanout remains a valid simultaneous-use set",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId reads[128]{};
                    if (!writer.DeclareBuffer({{8}, "buffer.read.fanout"}, MakeBufferDesc(), buffer, &failure) ||
                        !writer.BeginBufferUse(buffer, WriteUse(), initialize, &failure) || !writer.EndUse(initialize, &failure))
                        return false;
                    for (u32 index = 0; index < 128; ++index)
                        if (!writer.BeginBufferUse(buffer, ReadUse(), reads[index], &failure))
                            return false;
                    for (u32 index = 0; index < 128; ++index)
                        if (!writer.EndUse(reads[index], &failure))
                            return false;
                    return true;
                });

        runCase(128, rhi::QueueType::Graphics, true, "texture read fanout survives non-LIFO ends before a following write",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId initialize;
                    rendering::ResourceUseId reads[64]{};
                    rendering::ResourceUseId write;
                    if (!writer.DeclareTexture({{8}, "texture.read.fanout"}, MakeTextureDesc(), texture, &failure) ||
                        !writer.BeginTextureUse(texture, TextureWriteUse(0), initialize, &failure) || !writer.EndUse(initialize, &failure))
                        return false;
                    for (u32 index = 0; index < 64; ++index)
                        if (!writer.BeginTextureUse(texture, TextureReadUse(0), reads[index], &failure))
                            return false;
                    for (u32 index = 0; index < 64; index += 2)
                        if (!writer.EndUse(reads[index], &failure))
                            return false;
                    for (u32 index = 1; index < 64; index += 2)
                        if (!writer.EndUse(reads[index], &failure))
                            return false;
                    return writer.BeginTextureUse(texture, TextureWriteUse(0), write, &failure) && writer.EndUse(write, &failure);
                });

        runCase(29, rhi::QueueType::Graphics, false, "clear intent without a typed clear value is rejected",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    rendering::BufferUseDesc clear = WriteUse();
                    clear.content = rendering::ResourceContentIntent::Clear;
                    return writer.DeclareBuffer({{8}, "missing.clear.value"}, MakeBufferDesc(), buffer, &failure) &&
                           writer.BeginBufferUse(buffer, clear, use, &failure) && writer.EndUse(use, &failure);
                });

        runCase(30, rhi::QueueType::Graphics, false, "clear values cannot be attached to non-clear uses",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    rendering::BufferUseDesc write = WriteUse();
                    write.clearValue = rendering::BufferClearValue::Uint(1);
                    return writer.DeclareBuffer({{8}, "stray.clear.value"}, MakeBufferDesc(), buffer, &failure) &&
                           writer.BeginBufferUse(buffer, write, use, &failure) && writer.EndUse(use, &failure);
                });

        runCase(31, rhi::QueueType::Graphics, false, "buffer clear requires unordered-access state",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    rendering::BufferUseDesc clear = WriteUse();
                    clear.requiredState = rhi::ResourceState::CopyDestination;
                    clear.content = rendering::ResourceContentIntent::Clear;
                    clear.clearValue = rendering::BufferClearValue::Uint(2);
                    rendering::FrameBufferDesc desc = MakeBufferDesc();
                    desc.active.usage = desc.active.usage | rhi::BufferUsage::CopyDestination;
                    return writer.DeclareBuffer({{8}, "clear.wrong.state"}, desc, buffer, &failure) &&
                           writer.BeginBufferUse(buffer, clear, use, &failure) && writer.EndUse(use, &failure);
                });

        runCase(32, rhi::QueueType::Graphics, false, "unsigned texture clear requires an unsigned-integer format",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId use;
                    rendering::TextureUseDesc clear = TextureWriteUse(0);
                    clear.content = rendering::ResourceContentIntent::Clear;
                    clear.clearValue = rendering::TextureClearValue::Uint(3);
                    return writer.DeclareTexture({{8}, "clear.wrong.format"}, MakeTextureDesc(), texture, &failure) &&
                           writer.BeginTextureUse(texture, clear, use, &failure) && writer.EndUse(use, &failure);
                });

        runCase(33, rhi::QueueType::Graphics, false, "declaration-time texture clear requires a full-resource first use",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::FrameTextureDesc desc = MakeTextureDesc();
                    desc.initialization = rendering::FrameResourceInitialization::Clear;
                    desc.clearValue = rendering::TextureClearValue::Color({0.0f, 0.0f, 0.0f, 0.0f});
                    rendering::LogicalResourceId texture;
                    rendering::ResourceUseId use;
                    return writer.DeclareTexture({{8}, "partial.initial.clear"}, desc, texture, &failure) &&
                           writer.BeginTextureUse(texture, TextureWriteUse(0), use, &failure) && writer.EndUse(use, &failure);
                });

        runCase(34, rhi::QueueType::Graphics, false, "declaration and first-use clear values must agree",
                [](rendering::ResourcePlanningWriter& writer, rendering::RenderFlowResourceFailure& failure) noexcept
                {
                    rendering::FrameBufferDesc desc = MakeBufferDesc();
                    desc.initialization = rendering::FrameResourceInitialization::Clear;
                    desc.clearValue = rendering::BufferClearValue::Uint(4);
                    rendering::BufferUseDesc clear = WriteUse();
                    clear.content = rendering::ResourceContentIntent::Clear;
                    clear.clearValue = rendering::BufferClearValue::Uint(5);
                    rendering::LogicalResourceId buffer;
                    rendering::ResourceUseId use;
                    return writer.DeclareBuffer({{8}, "conflicting.clear.values"}, desc, buffer, &failure) &&
                           writer.BeginBufferUse(buffer, clear, use, &failure) && writer.EndUse(use, &failure);
                });
    }

    void TestTypedClearActions(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{93};
        constexpr rendering::GpuFlowGroupId flow{93};
        constexpr rendering::CommandScopeId scope{93};
        rendering::LogicalResourceId buffer;
        rendering::LogicalResourceId floatTexture;
        rendering::LogicalResourceId uintTexture;
        rendering::LogicalResourceId colorTarget;
        rendering::LogicalResourceId depthTarget;
        rendering::ResourceUseId uses[7];

        rendering::FrameBufferDesc bufferDesc = MakeBufferDesc();
        bufferDesc.initialization = rendering::FrameResourceInitialization::Clear;
        bufferDesc.clearValue = rendering::BufferClearValue::Uint(0x11223344u);

        rendering::TextureUseDesc floatClear = TextureWriteUse(0);
        floatClear.content = rendering::ResourceContentIntent::Clear;
        floatClear.clearValue = rendering::TextureClearValue::Color({0.25f, 0.5f, 0.75f, 1.0f});

        rendering::FrameTextureDesc uintDesc = MakeTextureDesc();
        uintDesc.active.format = rhi::Format::R32UInt;
        uintDesc.active.mipCount = 1;
        uintDesc.maximumMipCount = 1;
        rendering::TextureUseDesc uintClear = TextureWriteUse(0);
        uintClear.content = rendering::ResourceContentIntent::Clear;
        uintClear.clearValue = rendering::TextureClearValue::Uint(0xa5a5a5a5u);

        rendering::FrameTextureDesc colorDesc = MakeTextureDesc();
        colorDesc.active.mipCount = 1;
        colorDesc.maximumMipCount = 1;
        colorDesc.active.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        colorDesc.initialization = rendering::FrameResourceInitialization::Clear;
        colorDesc.clearValue = rendering::TextureClearValue::Color({0.1f, 0.2f, 0.3f, 1.0f});
        rendering::TextureUseDesc colorWrite;
        colorWrite.requiredState = rhi::ResourceState::RenderTarget;
        colorWrite.subresources = {0, 1, 0, 1};
        colorWrite.access = rendering::LogicalAccessIntent::Write;
        colorWrite.content = rendering::ResourceContentIntent::Discard;

        rendering::FrameTextureDesc depthDesc = MakeTextureDesc();
        depthDesc.active.format = rhi::Format::D32FloatS8UInt;
        depthDesc.active.mipCount = 1;
        depthDesc.maximumMipCount = 1;
        depthDesc.active.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::ShaderResource;
        rendering::TextureUseDesc depthClear;
        depthClear.requiredState = rhi::ResourceState::DepthWrite;
        depthClear.subresources = {0, 1, 0, 1};
        depthClear.access = rendering::LogicalAccessIntent::Write;
        depthClear.content = rendering::ResourceContentIntent::Clear;
        depthClear.clearValue = rendering::TextureClearValue::DepthStencil(0.0f, 7);

        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(35, {}, &failure) &&
                             allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
                             writer.DeclareBuffer({{9}, "typed.clear.buffer"}, bufferDesc, buffer, &failure) &&
                             writer.BeginBufferUse(buffer, WriteUse(), uses[0], &failure) && writer.EndUse(uses[0], &failure) &&
                             writer.BeginBufferUse(buffer, ReadUse(), uses[1], &failure) && writer.EndUse(uses[1], &failure) &&
                             writer.DeclareTexture({{9}, "typed.clear.float"}, MakeTextureDesc(), floatTexture, &failure) &&
                             writer.BeginTextureUse(floatTexture, floatClear, uses[2], &failure) && writer.EndUse(uses[2], &failure) &&
                             writer.BeginTextureUse(floatTexture, TextureReadUse(0), uses[3], &failure) && writer.EndUse(uses[3], &failure) &&
                             writer.DeclareTexture({{9}, "typed.clear.uint"}, uintDesc, uintTexture, &failure) &&
                             writer.BeginTextureUse(uintTexture, uintClear, uses[4], &failure) && writer.EndUse(uses[4], &failure) &&
                             writer.DeclareTexture({{9}, "typed.clear.target"}, colorDesc, colorTarget, &failure) &&
                             writer.BeginTextureUse(colorTarget, colorWrite, uses[5], &failure) && writer.EndUse(uses[5], &failure) &&
                             writer.DeclareTexture({{9}, "typed.clear.depth"}, depthDesc, depthTarget, &failure) &&
                             writer.BeginTextureUse(depthTarget, depthClear, uses[6], &failure) && writer.EndUse(uses[6], &failure) &&
                             writer.Close(&failure) && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "typed clear test seals declaration-time and per-use clear intents");
        if (planned)
        {
            const bool resolved = allocator.Resolve(nullptr, &failure);
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            check(resolved && generation.IsValid() && allocator.GetStats().compiledResourceActions == 12,
                  "typed clears compile after their transitions and participate in the bounded action ledger");
            if (resolved)
            {
                bool executed = allocator.BeginExecution(&failure);
                rendering::CompiledExecutionPacketView packet;
                rendering::ExecutionPacketCursor cursor;
                executed = executed && allocator.PacketFor(node, packet, &failure) && packet.GetStepCount() == 14 &&
                           packet.OpenCursor(scope, rhi::QueueType::Graphics, cursor, &failure);
                for (u32 index = 0; index < 7 && executed; ++index)
                {
                    if (index < 2)
                    {
                        rendering::ResolvedBufferUse resolvedUse;
                        executed = cursor.BeginBufferUse(uses[index], resolvedUse, &failure) && cursor.EndUse(uses[index], &failure) && !resolvedUse.IsValid();
                    }
                    else
                    {
                        rendering::ResolvedTextureUse resolvedUse;
                        executed = cursor.BeginTextureUse(uses[index], resolvedUse, &failure) && cursor.EndUse(uses[index], &failure) && !resolvedUse.IsValid();
                    }
                }
                executed = executed && cursor.FinalizePacket(&failure);
                check(executed, "typed clear packet exhausts in exact compiled use order");
                const rendering::CommandScopeExecutionReceipt receipts[] = {
                    {scope, rendering::CommandScopeCompletionKind::Submitted, rhi::QueueType::Graphics, {rhi::QueueType::Graphics, 1}}};
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                                   containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                   rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                check(executed && allocator.Finish(terminal, &failure), "typed clear generation reaches terminal completion");
            }
        }
        check(allocator.Shutdown(&failure), "typed clear execution is cleanup-safe");
    }

    void TestCrossQueueWaitProofRequired(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter graphicsWriter;
        rendering::ResourcePlanningWriter computeWriter;
        constexpr rendering::RenderFlowNodeId graphicsNode{90};
        constexpr rendering::RenderFlowNodeId computeNode{91};
        constexpr rendering::GpuFlowGroupId graphicsFlow{90};
        constexpr rendering::GpuFlowGroupId computeFlow{91};
        constexpr rendering::CommandScopeId graphicsScope{90};
        constexpr rendering::CommandScopeId computeScope{91};
        constexpr rendering::LogicalResourceKey key{{9}, "cross.queue.requires.wait"};
        rendering::LogicalResourceId graphicsResource;
        rendering::LogicalResourceId computeResource;
        rendering::ResourceUseId write;
        rendering::ResourceUseId read;
        rendering::BufferUseDesc computeRead = ReadUse();
        computeRead.requiredState = rhi::ResourceState::ShaderResourceCompute;
        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(30, {}, &failure) &&
                             allocator.RequestBeginQueue(rhi::QueueType::Compute, computeFlow, &failure) && allocator.RequestEndQueue(computeFlow, &failure) &&
                             allocator.CreatePlanningWriter(graphicsNode, graphicsFlow, graphicsScope, graphicsWriter, &failure) &&
                             allocator.CreatePlanningWriter(computeNode, computeFlow, computeScope, computeWriter, &failure) &&
                             graphicsWriter.DeclareBuffer(key, MakeBufferDesc(), graphicsResource, &failure) &&
                             graphicsWriter.BeginBufferUse(graphicsResource, WriteUse(), write, &failure) && graphicsWriter.EndUse(write, &failure) &&
                             computeWriter.ReferenceResource(key, computeResource, &failure) && computeWriter.BeginBufferUse(computeResource, computeRead, read, &failure) &&
                             computeWriter.EndUse(read, &failure) && graphicsWriter.Close(&failure) && computeWriter.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "cross-queue wait-proof test seals its candidate plan");
        if (planned)
        {
            check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch &&
                      !allocator.GetExecutionGeneration().IsValid(),
                  "cross-queue use is rejected while the compiled schedule has no real wait dependency");
        }
        check(allocator.Shutdown(&failure), "cross-queue wait-proof rejection is cleanup-safe");
    }

    void TestDuplicateCommandScopeFallback(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter firstWriter;
        rendering::ResourcePlanningWriter secondWriter;
        constexpr rendering::RenderFlowNodeId firstNode{94};
        constexpr rendering::RenderFlowNodeId secondNode{95};
        constexpr rendering::GpuFlowGroupId firstFlow{94};
        constexpr rendering::GpuFlowGroupId secondFlow{95};
        constexpr rendering::CommandScopeId duplicateScope{94};

        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(31, {}, &failure) &&
                             allocator.CreatePlanningWriter(firstNode, firstFlow, duplicateScope, firstWriter, &failure) &&
                             allocator.CreatePlanningWriter(secondNode, secondFlow, duplicateScope, secondWriter, &failure) && firstWriter.Close(&failure) && secondWriter.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "duplicate command-scope fallback test seals distinct-node writers");
        if (planned)
            check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch &&
                      !allocator.GetExecutionGeneration().IsValid(),
                  "a noncanonical command scope activates fallback uniqueness indexing and rejects a prior canonical scope collision");
        check(allocator.Shutdown(&failure), "duplicate command-scope fallback rejection is cleanup-safe");
    }

    void TestSealIdentityScratchReuse(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(32, {}, &failure), "seal identity scratch test starts its duplicate-node frame");

        rendering::ResourcePlanningWriter firstWriter;
        rendering::ResourcePlanningWriter secondWriter;
        constexpr rendering::RenderFlowNodeId duplicateNode{96};
        check(allocator.CreatePlanningWriter(duplicateNode, {96}, {96}, firstWriter, &failure) &&
                  allocator.CreatePlanningWriter(duplicateNode, {97}, {97}, secondWriter, &failure) && firstWriter.Close(&failure) && secondWriter.Close(&failure) &&
                  !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
              "single-pass sealing rejects one render-flow node published by multiple writer groups");
        allocator.CancelBeforePublication();

        check(allocator.BeginFrame(33, {}, &failure), "seal identity scratch test restarts after rejected sealing");
        rendering::ResourcePlanningWriter overlapWriter;
        rendering::ResourcePlanningWriter laterWriter;
        constexpr rendering::RenderFlowNodeId nodes[] = {{96}, {97}};
        constexpr rendering::GpuFlowGroupId flows[] = {{96}, {97}};
        // The first noncanonical command scope intentionally equals the second
        // node ID. Seal node identities must be reset before Resolve reuses the
        // same generation-stamped set for command-scope uniqueness.
        constexpr rendering::CommandScopeId scopes[] = {{97}, {98}};
        const bool planned = allocator.CreatePlanningWriter(nodes[0], flows[0], scopes[0], overlapWriter, &failure) &&
                             allocator.CreatePlanningWriter(nodes[1], flows[1], scopes[1], laterWriter, &failure) && overlapWriter.Close(&failure) && laterWriter.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned && allocator.Resolve(nullptr, &failure), "Resolve resets seal node-identity scratch before noncanonical command-scope indexing");
        if (allocator.GetExecutionGeneration().IsValid())
        {
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt receipts[] = {
                {scopes[0], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                {scopes[1], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                               containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            check(allocator.Finish(terminal, &failure), "seal identity scratch reuse proof aborts its resolved generation cleanly");
        }
        check(allocator.Shutdown(&failure), "seal identity scratch reuse proof is cleanup-safe");
    }

    void TestCrossQueueForkJoinSchedule(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter graphicsProducer;
        rendering::ResourcePlanningWriter computeConsumer;
        rendering::ResourcePlanningWriter graphicsJoin;
        constexpr rendering::RenderFlowNodeId nodes[] = {{100}, {101}, {102}};
        constexpr rendering::GpuFlowGroupId flows[] = {{100}, {102}, {104}};
        constexpr rendering::GpuFlowGroupId forkFlow{101};
        constexpr rendering::GpuFlowGroupId joinFlow{103};
        constexpr rendering::CommandScopeId scopes[] = {{100}, {101}, {102}};
        constexpr rendering::LogicalResourceKey key{{10}, "cross.queue.fork.join"};
        rendering::LogicalResourceId resources[3];
        rendering::ResourceUseId uses[3];
        rendering::LogicalResourceId scratchResources[3];
        rendering::ResourceUseId scratchUses[3];
        rendering::BufferUseDesc computeRead = ReadUse();
        computeRead.requiredState = rhi::ResourceState::ShaderResourceCompute;

        const bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(32, {}, &failure) &&
                             allocator.RequestQueueSync(forkFlow, rhi::CommandListSyncType::ForkAsyncCompute, &failure) &&
                             allocator.RequestBeginQueue(rhi::QueueType::Compute, flows[1], &failure) && allocator.RequestEndQueue(flows[1], &failure) &&
                             allocator.RequestQueueSync(joinFlow, rhi::CommandListSyncType::JoinAsyncCompute, &failure) &&
                             allocator.CreatePlanningWriter(nodes[0], flows[0], scopes[0], graphicsProducer, &failure) &&
                             allocator.CreatePlanningWriter(nodes[1], flows[1], scopes[1], computeConsumer, &failure) &&
                             allocator.CreatePlanningWriter(nodes[2], flows[2], scopes[2], graphicsJoin, &failure) &&
                             graphicsProducer.DeclareBuffer(key, MakeBufferDesc(), resources[0], &failure) &&
                             graphicsProducer.BeginBufferUse(resources[0], WriteUse(), uses[0], &failure) && graphicsProducer.EndUse(uses[0], &failure) &&
                             graphicsProducer.DeclareTemporaryBuffer("fork scratch", MakeBufferDesc(8192), scratchResources[0], &failure) &&
                             graphicsProducer.BeginBufferUse(scratchResources[0], WriteUse(), scratchUses[0], &failure) && graphicsProducer.EndUse(scratchUses[0], &failure) &&
                             computeConsumer.ReferenceResource(key, resources[1], &failure) &&
                             computeConsumer.BeginBufferUse(resources[1], computeRead, uses[1], &failure) && computeConsumer.EndUse(uses[1], &failure) &&
                             computeConsumer.DeclareTemporaryBuffer("compute scratch", MakeBufferDesc(8192), scratchResources[1], &failure) &&
                             computeConsumer.BeginBufferUse(scratchResources[1], WriteUse(), scratchUses[1], &failure) && computeConsumer.EndUse(scratchUses[1], &failure) &&
                             graphicsJoin.ReferenceResource(key, resources[2], &failure) &&
                             graphicsJoin.BeginBufferUse(resources[2], ReadUse(), uses[2], &failure) && graphicsJoin.EndUse(uses[2], &failure) &&
                             graphicsJoin.DeclareTemporaryBuffer("join scratch", MakeBufferDesc(8192), scratchResources[2], &failure) &&
                             graphicsJoin.BeginBufferUse(scratchResources[2], WriteUse(), scratchUses[2], &failure) && graphicsJoin.EndUse(scratchUses[2], &failure) &&
                             graphicsProducer.Close(&failure) && computeConsumer.Close(&failure) && graphicsJoin.Close(&failure) &&
                             allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "cross-queue fork/join test seals its candidate plan");
        if (planned)
        {
            const bool resolved = allocator.Resolve(nullptr, &failure);
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            check(resolved && generation.IsValid(), "balanced Graphics/Compute requests publish an execution generation");
            if (resolved)
            {
                check(allocator.BeginExecution(&failure), "cross-queue fork/join generation enters execution");
                bool executed = true;
                rendering::PhysicalResourceId scratchPhysical;
                for (u32 index = 0; index < 3; ++index)
                {
                    rendering::CompiledExecutionPacketView packet;
                    rendering::ExecutionPacketCursor cursor;
                    rendering::ResolvedBufferUse resolvedUse;
                    rendering::ResolvedBufferUse resolvedScratch;
                    const rhi::QueueType queue = index == 1 ? rhi::QueueType::Compute : rhi::QueueType::Graphics;
                    executed = executed && allocator.PacketFor(nodes[index], packet, &failure) && packet.OpenCursor(scopes[index], queue, cursor, &failure) &&
                               cursor.BeginBufferUse(uses[index], resolvedUse, &failure) && cursor.EndUse(uses[index], &failure) && !resolvedUse.IsValid() &&
                               cursor.BeginBufferUse(scratchUses[index], resolvedScratch, &failure);
                    if (executed)
                    {
                        const rendering::PhysicalResourceId current = resolvedScratch.GetPhysicalResource();
                        executed = current.IsValid() && (index == 0 || current == scratchPhysical);
                        scratchPhysical = index == 0 ? current : scratchPhysical;
                    }
                    executed = executed && cursor.EndUse(scratchUses[index], &failure) && !resolvedScratch.IsValid() &&
                               cursor.FinalizePacket(&failure);
                }
                check(executed, "cross-queue fork/join packets exhaust in their compiled queue scopes");
                check(executed && scratchPhysical.IsValid(),
                      "distinct dedicated-resource lifetimes reuse one object across exact Graphics/Compute fork and join dependencies");
                const rendering::CommandScopeExecutionReceipt receipts[] = {
                    {scopes[0], rendering::CommandScopeCompletionKind::Submitted, rhi::QueueType::Graphics, {rhi::QueueType::Graphics, 1}},
                    {scopes[1], rendering::CommandScopeCompletionKind::Submitted, rhi::QueueType::Compute, {rhi::QueueType::Compute, 1}},
                    {scopes[2], rendering::CommandScopeCompletionKind::Submitted, rhi::QueueType::Graphics, {rhi::QueueType::Graphics, 2}}};
                const rendering::QueueDependencyExecutionReceipt dependencyReceipts[] = {
                    {scopes[1], scopes[2], rhi::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::Submitted},
                    {scopes[0], scopes[1], rhi::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::Submitted}};
                const rendering::TerminalExecutionReceipt missingDependencies{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                                               containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                check(executed && !allocator.Finish(missingDependencies, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
                      "terminal completion rejects omitted fork/join execution evidence without mutating the generation");
                const rendering::CommandScopeExecutionReceipt abortedScopes[] = {
                    {scopes[0], rendering::CommandScopeCompletionKind::Submitted, rhi::QueueType::Graphics, {rhi::QueueType::Graphics, 1}},
                    {scopes[1], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Compute, {}},
                    {scopes[2], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
                const rendering::QueueDependencyExecutionReceipt contradictoryAbortDependencies[] = {
                    {scopes[0], scopes[1], rhi::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission},
                    {scopes[1], scopes[2], rhi::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission}};
                const rendering::TerminalExecutionReceipt contradictoryAbort{
                    generation, rendering::TerminalExecutionCompletionKind::Aborted,
                    containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(abortedScopes),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation),
                    containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(contradictoryAbortDependencies)};
                check(executed && !allocator.Finish(contradictoryAbort, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
                      "terminal abort rejects a discarded dependency whose lowering scope was submitted");
                const rendering::CommandScopeExecutionReceipt duplicateScopeReceipts[] = {
                    receipts[0], receipts[1], receipts[1]};
                const rendering::TerminalExecutionReceipt duplicateScopeTerminal{
                    generation, rendering::TerminalExecutionCompletionKind::Completed,
                    containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(duplicateScopeReceipts),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation),
                    containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(dependencyReceipts)};
                check(executed && !allocator.Finish(duplicateScopeTerminal, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                      "terminal receipt rejects a duplicate command scope even when receipt order is not canonical");
                const rendering::QueueDependencyExecutionReceipt duplicateDependencyReceipts[] = {
                    dependencyReceipts[0], dependencyReceipts[0]};
                const rendering::TerminalExecutionReceipt duplicateDependencyTerminal{
                    generation, rendering::TerminalExecutionCompletionKind::Completed,
                    containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation),
                    containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(duplicateDependencyReceipts)};
                check(executed && !allocator.Finish(duplicateDependencyTerminal, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                      "terminal receipt rejects a duplicate queue dependency without assuming receipt order");
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                                     containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                     rendering::TerminalJoinToken::CompletedSynchronously(generation),
                                                                     containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(dependencyReceipts)};
                check(executed && allocator.Finish(terminal, &failure), "cross-queue fork/join generation reaches one terminal join");
            }
        }
        check(allocator.Shutdown(&failure), "cross-queue fork/join execution is cleanup-safe");
    }

    void TestRepeatedCrossQueueOrderFacts(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter writers[5];
        constexpr rendering::RenderFlowNodeId nodes[] = {{200}, {201}, {202}, {203}, {204}};
        constexpr rendering::GpuFlowGroupId flows[] = {{200}, {202}, {204}, {206}, {208}};
        constexpr rendering::GpuFlowGroupId syncFlows[] = {{201}, {203}, {205}, {207}};
        constexpr rendering::CommandScopeId scopes[] = {{200}, {201}, {202}, {203}, {204}};
        constexpr rendering::LogicalResourceKey key{{11}, "cross.queue.repeated.watermark"};
        rendering::LogicalResourceId resources[5];
        rendering::ResourceUseId uses[5];
        rendering::BufferUseDesc computeRead = ReadUse();
        computeRead.requiredState = rhi::ResourceState::ShaderResourceCompute;

        bool planned = allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(33, {}, &failure) &&
                       allocator.RequestQueueSync(syncFlows[0], rhi::CommandListSyncType::ForkAsyncCompute, &failure) &&
                       allocator.RequestBeginQueue(rhi::QueueType::Compute, flows[1], &failure) && allocator.RequestEndQueue(flows[1], &failure) &&
                       allocator.RequestQueueSync(syncFlows[1], rhi::CommandListSyncType::JoinAsyncCompute, &failure) &&
                       allocator.RequestQueueSync(syncFlows[2], rhi::CommandListSyncType::ForkAsyncCompute, &failure) &&
                       allocator.RequestBeginQueue(rhi::QueueType::Compute, flows[3], &failure) && allocator.RequestEndQueue(flows[3], &failure) &&
                       allocator.RequestQueueSync(syncFlows[3], rhi::CommandListSyncType::JoinAsyncCompute, &failure);
        for (u32 index = 0; planned && index < 5; ++index)
            planned = allocator.CreatePlanningWriter(nodes[index], flows[index], scopes[index], writers[index], &failure);
        planned = planned && writers[0].DeclareBuffer(key, MakeBufferDesc(), resources[0], &failure) &&
                  writers[0].BeginBufferUse(resources[0], WriteUse(), uses[0], &failure) && writers[0].EndUse(uses[0], &failure);
        for (u32 index = 1; planned && index < 5; ++index)
        {
            const rendering::BufferUseDesc use = index == 1 || index == 3 ? computeRead : index == 2 ? WriteUse() : ReadUse();
            planned = writers[index].ReferenceResource(key, resources[index], &failure) && writers[index].BeginBufferUse(resources[index], use, uses[index], &failure) &&
                      writers[index].EndUse(uses[index], &failure);
        }
        for (u32 index = 0; planned && index < 5; ++index)
            planned = writers[index].Close(&failure);
        planned = planned && allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
        check(planned, "repeated fork/join order-fact test seals two async-compute regions");
        if (planned)
        {
            const bool resolved = allocator.Resolve(nullptr, &failure);
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            check(resolved && generation.IsValid(), "dense queue-order facts advance across repeated fork/join regions");
            if (resolved)
            {
                const rendering::CommandScopeExecutionReceipt scopeReceipts[] = {
                    {scopes[0], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                    {scopes[1], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Compute, {}},
                    {scopes[2], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}},
                    {scopes[3], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Compute, {}},
                    {scopes[4], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
                const rendering::QueueDependencyExecutionReceipt dependencyReceipts[] = {
                    {scopes[0], scopes[1], rhi::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission},
                    {scopes[1], scopes[2], rhi::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission},
                    {scopes[2], scopes[3], rhi::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission},
                    {scopes[3], scopes[4], rhi::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission}};
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                     containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(scopeReceipts),
                                                                     rendering::TerminalJoinToken::CompletedSynchronously(generation),
                                                                     containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(dependencyReceipts)};
                check(allocator.Finish(terminal, &failure), "repeated fork/join order-fact generation aborts cleanly after Resolve proof");
            }
        }
        check(allocator.Shutdown(&failure), "repeated fork/join order-fact proof is cleanup-safe");
    }

    void TestCompiledActionCapacity(CheckFunction check) noexcept
    {
        {
            rendering::RenderFlowResourceAllocatorConfig config = MakeLogicalOnlyConfig();
            config.maximumCompiledResourceActions = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::RenderFlowResourceFailure failure;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId texture;
            rendering::ResourceUseId use;
            rendering::FrameTextureDesc textureDesc = MakeTextureDesc();
            textureDesc.active.arraySize = 2;
            rendering::TextureUseDesc write = TextureWriteUse(0);
            write.subresources = {0, 2, 0, 2};
            constexpr rendering::CommandScopeId scope{92};
            const bool planned = allocator.Initialize(config, &failure) && allocator.BeginFrame(31, {}, &failure) &&
                                 allocator.CreatePlanningWriter({92}, {92}, scope, writer, &failure) &&
                                 writer.DeclareTexture({{9}, "action.coalesced.capacity"}, textureDesc, texture, &failure) &&
                                 writer.BeginTextureUse(texture, write, use, &failure) && writer.EndUse(use, &failure) && writer.Close(&failure) &&
                                 allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
            check(planned, "compiled-action coalescing test seals its candidate plan");
            if (planned)
            {
                const bool resolved = allocator.Resolve(nullptr, &failure);
                check(resolved && allocator.GetExecutionGeneration().IsValid(), "a two-mip by two-slice transition rectangle coalesces under a one-action capacity");
                if (resolved)
                {
                    const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
                    const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
                    const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                       containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                       rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                    check(allocator.Finish(terminal, &failure), "coalesced one-action generation aborts cleanly");
                }
            }
            check(allocator.Shutdown(&failure), "compiled-action coalescing proof is cleanup-safe");
        }

        {
            rendering::RenderFlowResourceAllocatorConfig config = MakeLogicalOnlyConfig();
            config.maximumCompiledResourceActions = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::RenderFlowResourceFailure failure;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId firstTexture;
            rendering::LogicalResourceId secondTexture;
            rendering::ResourceUseId firstUse;
            rendering::ResourceUseId secondUse;
            const bool planned = allocator.Initialize(config, &failure) && allocator.BeginFrame(32, {}, &failure) &&
                                 allocator.CreatePlanningWriter({93}, {93}, {93}, writer, &failure) &&
                                 writer.DeclareTexture({{9}, "action.capacity.first"}, MakeTextureDesc(), firstTexture, &failure) &&
                                 writer.DeclareTexture({{9}, "action.capacity.second"}, MakeTextureDesc(), secondTexture, &failure) &&
                                 writer.BeginTextureUse(firstTexture, TextureWriteUse(0), firstUse, &failure) && writer.EndUse(firstUse, &failure) &&
                                 writer.BeginTextureUse(secondTexture, TextureWriteUse(0), secondUse, &failure) && writer.EndUse(secondUse, &failure) && writer.Close(&failure) &&
                                 allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
            check(planned, "compiled-action hard-cap test seals two independent transition resources");
            if (planned)
                check(!allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded &&
                          !allocator.GetExecutionGeneration().IsValid(),
                      "independent texture transitions cannot coalesce across resource identity and still honor the one-action capacity");
            check(allocator.Shutdown(&failure), "compiled-action hard-cap failure is cleanup-safe");
        }
    }

    void TestPrePublicationAbort(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(16, {}, &failure), "pre-publication abort test starts allocator planning");
        rendering::ResourcePlanningWriter writer;
        check(allocator.CreatePlanningWriter({40}, {40}, {40}, writer, &failure), "pre-publication abort test creates a writer");
        writer.Abandon();
        allocator.CancelBeforePublication();
        const rendering::RenderFlowResourceAllocatorStats once = allocator.GetStats();
        allocator.CancelBeforePublication();
        const rendering::RenderFlowResourceAllocatorStats twice = allocator.GetStats();
        check(once.state == rendering::RenderFlowResourceSessionState::Idle && once.abortedFrames == 1 && twice.abortedFrames == once.abortedFrames,
              "pre-publication abort returns to Idle exactly once");
        check(allocator.Shutdown(&failure), "pre-publication abort leaves allocator shutdown-safe");
    }

    void TestStalePlanningWriterIsolation(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocatorConfig config = MakeLogicalOnlyConfig();
        config.maximumPlanningWriters = 1;

        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter staleWriter;
        check(allocator.Initialize(config, &failure) && allocator.BeginFrame(143, {}, &failure) &&
                  allocator.CreatePlanningWriter({143}, {0}, {143}, staleWriter, &failure),
              "stale-writer isolation test occupies the only pooled writer lease");

        allocator.CancelBeforePublication();
        check(!staleWriter.IsValid() && allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle,
              "cancellation invalidates an outstanding writer without requiring its handle to die");

        rendering::ResourcePlanningWriter currentWriter;
        check(allocator.BeginFrame(144, {}, &failure) && allocator.CreatePlanningWriter({144}, {0}, {144}, currentWriter, &failure),
              "next frame reuses the same flow group while the stale pooled lease is still retained");

        rendering::LogicalResourceId staleResource;
        check(!staleWriter.DeclareBuffer({{14}, "stale.writer"}, MakeBufferDesc(), staleResource, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "old-generation writer cannot mutate the reused persistent group storage");

        rendering::LogicalResourceId currentResource;
        check(currentWriter.DeclareBuffer({{14}, "current.writer"}, MakeBufferDesc(), currentResource, &failure) && currentWriter.Close(&failure),
              "current writer remains usable after the stale handle is rejected");

        staleWriter.Abandon();
        allocator.CancelBeforePublication();
        check(allocator.Shutdown(&failure), "stale-writer isolation leaves allocator shutdown-safe");
    }

    void TestPersistentCandidateResourceReuse(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocatorConfig config = MakeLogicalOnlyConfig();
        config.maximumPlanningWriters = 1;

        constexpr const char* longName0 = "persistent.resource.name.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        constexpr const char* longName1 = "persistent.resource.name.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        constexpr const char* longName2 = "persistent.resource.name.cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
        constexpr const char* shrinkName = "persistent.resource.name.shrink.dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize(config, &failure), "persistent resource-slot reuse initializes allocator");

        rendering::ResourcePlanningWriter firstWriter;
        rendering::LogicalResourceId first[3];
        const bool firstFrame = allocator.BeginFrame(145, {}, &failure) && allocator.CreatePlanningWriter({145}, {0}, {145}, firstWriter, &failure) &&
                                firstWriter.DeclareBuffer({{15}, longName0}, MakeBufferDesc(), first[0], &failure) &&
                                firstWriter.DeclareBuffer({{15}, longName1}, MakeBufferDesc(), first[1], &failure) &&
                                firstWriter.DeclareBuffer({{15}, longName2}, MakeBufferDesc(), first[2], &failure) && firstWriter.Close(&failure);
        check(firstFrame && first[0].index == 0 && first[1].index == 1 && first[2].index == 2,
              "persistent resource-slot reuse establishes a three-slot high-water mark");
        allocator.CancelBeforePublication();

        rendering::ResourcePlanningWriter shrinkWriter;
        rendering::LogicalResourceId shrink;
        const bool shrinkFrame = allocator.BeginFrame(146, {}, &failure) && allocator.CreatePlanningWriter({146}, {0}, {146}, shrinkWriter, &failure) &&
                                 shrinkWriter.DeclareBuffer({{15}, shrinkName}, MakeBufferDesc(), shrink, &failure) && shrinkWriter.Close(&failure);
        check(shrinkFrame && shrink.index == 0, "persistent resource-slot reuse exposes only the current compact logical range after shrink");
        allocator.CancelBeforePublication();

        rendering::ResourcePlanningWriter regrowWriter;
        rendering::LogicalResourceId regrown[3];
        const bool regrowFrame = allocator.BeginFrame(147, {}, &failure) && allocator.CreatePlanningWriter({147}, {0}, {147}, regrowWriter, &failure) &&
                                 regrowWriter.DeclareBuffer({{15}, longName2}, MakeBufferDesc(), regrown[0], &failure) &&
                                 regrowWriter.DeclareBuffer({{15}, longName0}, MakeBufferDesc(), regrown[1], &failure) &&
                                 regrowWriter.DeclareBuffer({{15}, longName1}, MakeBufferDesc(), regrown[2], &failure) && regrowWriter.Close(&failure);
        check(regrowFrame && regrown[0].index == 0 && regrown[1].index == 1 && regrown[2].index == 2,
              "persistent resource-slot reuse regrows compactly without stale high-water names becoming visible");
        allocator.CancelBeforePublication();
        check(allocator.Shutdown(&failure), "persistent resource-slot reuse leaves allocator shutdown-safe");
    }

    void TestAdaptiveNamedResourceIndexAndCapacity(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocatorConfig config = MakeLogicalOnlyConfig();
        config.maximumPlanningWriters = 1;
        config.maximumOperationsPerPlanningWriter = 64;
        config.maximumLogicalResources = 260;

        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::ResourcePlanningWriter writer;
        check(allocator.Initialize(config, &failure) && allocator.BeginFrame(148, {}, &failure) &&
                  allocator.CreatePlanningWriter({148}, {0}, {148}, writer, &failure),
              "adaptive named-resource index test creates one bounded writer");

        rendering::LogicalResourceId resources[260];
        bool recorded = true;
        for (u32 index = 0; index < 260; ++index)
        {
            char name[64]{};
            std::snprintf(name, sizeof(name), "adaptive.named.resource.%03u", index);
            recorded = recorded && writer.ReferenceResource({{18}, name}, resources[index], &failure) && resources[index].index == index;
        }
        check(recorded, "adaptive named-resource lookup crosses repeated index-resize thresholds without changing dense logical ids");

        constexpr u32 duplicateIndices[] = {27, 70, 130, 259};
        bool duplicatesPreserved = true;
        for (const u32 duplicateIndex : duplicateIndices)
        {
            char duplicateName[64]{};
            std::snprintf(duplicateName, sizeof(duplicateName), "adaptive.named.resource.%03u", duplicateIndex);
            rendering::LogicalResourceId duplicate;
            duplicatesPreserved = duplicatesPreserved && writer.ReferenceResource({{18}, duplicateName}, duplicate, &failure) && duplicate == resources[duplicateIndex];
        }
        check(duplicatesPreserved, "indexed named-resource lookup preserves full logical identity across repeated table growth");

        rendering::LogicalResourceId overflow;
        check(!writer.ReferenceResource({{18}, "adaptive.named.resource.overflow"}, overflow, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
              "reference-only recording obeys the logical-resource capacity without coupling it to the operation limit");
        allocator.CancelBeforePublication();

        rendering::ResourcePlanningWriter smallWriter;
        rendering::LogicalResourceId smallA;
        rendering::LogicalResourceId smallB;
        check(allocator.BeginFrame(149, {}, &failure) && allocator.CreatePlanningWriter({149}, {0}, {149}, smallWriter, &failure) &&
                  smallWriter.ReferenceResource({{18}, "adaptive.small.a"}, smallA, &failure) && smallWriter.ReferenceResource({{18}, "adaptive.small.b"}, smallB, &failure) &&
                  smallA.index == 0 && smallB.index == 1,
              "historically large adaptive writer records a compact small frame");
        allocator.CancelBeforePublication();

        rendering::ResourcePlanningWriter regrowWriter;
        rendering::LogicalResourceId regrowA;
        rendering::LogicalResourceId regrowB;
        check(allocator.BeginFrame(150, {}, &failure) && allocator.CreatePlanningWriter({150}, {0}, {150}, regrowWriter, &failure) &&
                  regrowWriter.ReferenceResource({{18}, "adaptive.regrow.a"}, regrowA, &failure) && regrowWriter.ReferenceResource({{18}, "adaptive.regrow.b"}, regrowB, &failure) &&
                  regrowA.index == 0 && regrowB.index == 1,
              "adaptive writer remains valid after auxiliary high-water decay returns it to the small path");
        allocator.CancelBeforePublication();
        check(allocator.Shutdown(&failure), "adaptive named-resource bound failure leaves allocator shutdown-safe");

    }

    void TestTerminalLifecycleRecovery(CheckFunction check) noexcept
    {
        const auto publish = [](rendering::RenderFlowResourceAllocator& allocator, rendering::RenderFlowResourceFailure& failure, const vanguard::u64 serial) noexcept
        {
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            constexpr rendering::RenderFlowNodeId node{140};
            constexpr rendering::GpuFlowGroupId flow{140};
            constexpr rendering::CommandScopeId scope{140};
            return allocator.BeginFrame(serial, {}, &failure) && allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) &&
                   writer.DeclareBuffer({{14}, "lifecycle.recovery"}, MakeBufferDesc(), resource, &failure) &&
                   writer.BeginBufferUse(resource, WriteUse(), use, &failure) && writer.EndUse(use, &failure) && writer.Close(&failure) &&
                   allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && allocator.Resolve(nullptr, &failure);
        };

        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::RenderFlowResourceFailure failure;
            check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && publish(allocator, failure, 140), "terminal lifecycle test publishes a generation");
            check(!allocator.Shutdown(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase &&
                      allocator.GetState() == rendering::RenderFlowResourceSessionState::Ready,
                  "explicit shutdown refuses to discard a published execution generation");
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            constexpr rendering::CommandScopeId scope{140};
            const rendering::CommandScopeExecutionReceipt receipts[] = {
                {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, rhi::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                               containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            check(allocator.Finish(terminal, &failure) && allocator.Shutdown(&failure), "published execution joins before explicit allocator shutdown");
        }

        {
            rendering::CompiledExecutionPacketView packet;
            rendering::RenderFlowResourceFailure failure;
            {
                rendering::RenderFlowResourceAllocator allocator;
                check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && publish(allocator, failure, 141) && allocator.BeginExecution(&failure) &&
                          allocator.PacketFor({140}, packet, &failure),
                      "allocator-destruction test retains a published packet view");
            }
            rendering::ExecutionPacketCursor cursor;
            check(packet.IsValid() && !packet.OpenCursor({140}, rhi::QueueType::Graphics, cursor, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
                  "allocator destruction makes a retained execution generation terminal before releasing its last reference");
        }

        {
            rendering::ResourcePlanningWriter writer;
            rendering::RenderFlowResourceFailure failure;
            {
                rendering::RenderFlowResourceAllocator allocator;
                check(allocator.Initialize(MakeLogicalOnlyConfig(), &failure) && allocator.BeginFrame(142, {}, &failure) &&
                          allocator.CreatePlanningWriter({142}, {142}, {142}, writer, &failure),
                      "writer-first lifetime test creates an outstanding writer");
            }
            check(!writer.IsValid(), "outstanding writer observes allocator abandonment without dereferencing freed control state");
            writer.Abandon();
        }
    }

    void TestPhysicalProviderRequirement(CheckFunction check) noexcept
    {
        if (rhi::IsInitialized())
            return;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        check(allocator.Initialize({}, &failure), "native-provider requirement test initializes allocator metadata without an RHI");
        check(!allocator.BeginFrame(28, {}, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::NotInitialized &&
                  allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle && !allocator.GetExecutionGeneration().IsValid(),
              "production allocator mode rejects a frame when physical assignment has no initialized RHI");
        check(allocator.Shutdown(&failure), "native-provider requirement failure leaves allocator shutdown-safe");
    }

    void TestPlacedRangePlanner(CheckFunction check) noexcept
    {
        const auto request = [](const u32 allocation, const rendering::FrameResourceKind kind, const rhi::QueueType queue,
                                const u32 first, const u32 last, const vanguard::u64 size, const vanguard::u64 alignment,
                                const vanguard::u64 compatibilityClass = 1u) noexcept
        {
            rendering::detail::PlacedRangeRequest value;
            value.allocation = allocation;
            value.kind = kind;
            value.queue = queue;
            value.firstAcquire = {{first + 1u}, first};
            value.lastRelease = {{last + 1u}, last};
            value.requirements.size = size;
            value.requirements.alignment = alignment;
            value.requirements.compatibilityClass = compatibilityClass;
            value.requirements.memoryType = rhi::MemoryType::DeviceLocal;
            value.requirements.heapCategory = kind == rendering::FrameResourceKind::Texture
                                                    ? rhi::PlacedHeapCategory::Texture
                                                    : rhi::PlacedHeapCategory::Buffer;
            return value;
        };

        rendering::RenderFlowResourceFailure failure;
        rendering::detail::PlacedRangePlan plan;
        rendering::detail::PlacedRangePlannerScratch scratch;
        rendering::detail::PlacedRangePlannerConfig config;
        config.minimumHeapBytes = 128;
        config.heapAlignment = 64;
        const auto find = [&plan](const u32 allocation) noexcept -> const rendering::detail::PlacedRangeAssignment* {
            for (const rendering::detail::PlacedRangeAssignment& assignment : plan.assignments)
                if (assignment.allocation == allocation)
                    return &assignment;
            return nullptr;
        };

        {
            const rendering::detail::PlacedRangeRequest requests[] = {
                request(0, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 128, 64),
                request(1, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 128, 64)};
            const bool built = rendering::detail::BuildPlacedRangePlan(requests, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* first = find(0);
            const rendering::detail::PlacedRangeAssignment* second = find(1);
            check(built && plan.heaps.Size() == 1 && first != nullptr && second != nullptr && first->offset == 0 && second->offset == 0 &&
                      second->predecessorCount == 1 && plan.predecessors[second->predecessorOffset].allocation == 0 &&
                      plan.predecessors[second->predecessorOffset].offset == 0 && plan.predecessors[second->predecessorOffset].size == 128,
                  "placed planner reuses an exact-fit range with exact predecessor provenance");
        }

        {
            const rendering::detail::PlacedRangeRequest requests[] = {
                request(10, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 2, 32, 32),
                request(11, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 2, 96, 64)};
            const bool built = rendering::detail::BuildPlacedRangePlan(requests, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* small = find(10);
            const rendering::detail::PlacedRangeAssignment* large = find(11);
            check(built && plan.heaps.Size() == 1 && small != nullptr && large != nullptr && large->offset == 0 && small->offset == 96,
                  "same-event placed acquisitions use descending bytes and alignment before stable allocation id");
        }

        {
            const rendering::detail::PlacedRangeRequest requests[] = {
                request(20, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 64),
                request(21, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 64),
                request(22, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 128, 64)};
            const bool built = rendering::detail::BuildPlacedRangePlan(requests, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* combined = find(22);
            const bool predecessors = combined != nullptr && combined->predecessorCount == 2 &&
                                      plan.predecessors[combined->predecessorOffset].allocation == 20 &&
                                      plan.predecessors[combined->predecessorOffset].offset == 0 &&
                                      plan.predecessors[combined->predecessorOffset + 1u].allocation == 21 &&
                                      plan.predecessors[combined->predecessorOffset + 1u].offset == 64;
            check(built && plan.heaps.Size() == 1 && combined != nullptr && combined->offset == 0 && predecessors,
                  "adjacent released ranges coalesce for first-fit without losing multi-owner provenance");
        }

        {
            const rendering::detail::PlacedRangeRequest touching[] = {
                request(30, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 128, 64),
                request(31, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 1, 2, 128, 64)};
            check(rendering::detail::BuildPlacedRangePlan(touching, config, plan, scratch, &failure) && plan.heaps.Size() == 2,
                  "equal-position release and acquisition remain overlapping and cannot alias");

            const rendering::detail::PlacedRangeRequest partiallyVirgin[] = {
                request(32, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 64),
                request(33, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 128, 64)};
            check(rendering::detail::BuildPlacedRangePlan(partiallyVirgin, config, plan, scratch, &failure) && plan.heaps.Size() == 2,
                  "a range mixing predecessor-owned and virgin bytes is not emitted as an incomplete alias activation");
        }

        {
            rendering::detail::PlacedRangePlannerConfig fragmentedConfig = config;
            fragmentedConfig.minimumHeapBytes = 192;
            const rendering::detail::PlacedRangeRequest fragmented[] = {
                request(40, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 3, 64, 64),
                request(41, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 64),
                request(42, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 3, 64, 64),
                request(43, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 4, 128, 64)};
            check(rendering::detail::BuildPlacedRangePlan(fragmented, fragmentedConfig, plan, scratch, &failure) && plan.heaps.Size() == 2,
                  "fragmentation grows a stable new heap instead of overlapping live neighbors");
        }

        {
            const rendering::detail::PlacedRangeRequest alignedReuse[] = {
                request(44, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 3, 48, 16),
                request(45, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 32, 16),
                request(46, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 4, 16, 64),
            };
            const bool built = rendering::detail::BuildPlacedRangePlan(alignedReuse, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* reused = find(46);
            const bool exactProvenance = reused != nullptr && reused->predecessorCount == 1 &&
                                         plan.predecessors[reused->predecessorOffset].allocation == 45 &&
                                         plan.predecessors[reused->predecessorOffset].offset == 64 &&
                                         plan.predecessors[reused->predecessorOffset].size == 16;
            check(built && plan.heaps.Size() == 1 && reused != nullptr && reused->offset == 64 && exactProvenance,
                  "alignment-aware free-run lookup chooses the earliest exact aligned subrange and clips predecessor provenance");

            const bool compactAlignmentSummaries = scratch.alignmentLevelCount == 2 && scratch.activeHeapCount == 1 && scratch.activeGroupCount == 1 &&
                                                   scratch.heaps[0].alignmentLevelCount == 2 &&
                                                   scratch.heaps[0].freeRunFits.Size() == scratch.heaps[0].freeRuns.Size() * 2u &&
                                                   scratch.groups[0].alignmentLevelCount == 2 &&
                                                   scratch.groups[0].fitTree.Size() == scratch.groups[0].treeCapacity * 4u;
            check(compactAlignmentSummaries,
                  "placed planner summaries allocate only the two observed 16-byte and 64-byte alignment classes instead of a fixed 64-level table");

            const rendering::detail::PlacedRangeRequest oneAlignment[] = {
                request(450, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 32),
                request(451, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 64, 32),
            };
            const bool oneAlignmentBuilt = rendering::detail::BuildPlacedRangePlan(oneAlignment, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* oneAlignmentReuse = find(451);
            const bool oneAlignmentStorage = oneAlignmentBuilt && scratch.alignmentLevelCount == 1 && scratch.activeHeapCount == 1 && scratch.activeGroupCount == 1 &&
                                             scratch.heaps[0].alignmentLevelCount == 1 && scratch.heaps[0].freeRunFits.Size() == scratch.heaps[0].freeRuns.Size() &&
                                             scratch.groups[0].alignmentLevelCount == 1 && oneAlignmentReuse != nullptr && oneAlignmentReuse->offset == 0;
            check(oneAlignmentStorage,
                  "placed planner scratch safely restrides from two observed alignment classes to one on the next build");

            const bool twoAlignmentRebuilt = rendering::detail::BuildPlacedRangePlan(alignedReuse, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* restridedReuse = find(46);
            check(twoAlignmentRebuilt && scratch.alignmentLevelCount == 2 && scratch.heaps[0].alignmentLevelCount == 2 &&
                      scratch.heaps[0].freeRunFits.Size() == scratch.heaps[0].freeRuns.Size() * 2u && restridedReuse != nullptr && restridedReuse->offset == 64,
                  "placed planner scratch safely restrides back to two alignment classes without stale fit summaries");
        }

        {
            rendering::detail::PlacedRangeRequest chain[16]{};
            for (u32 index = 0; index < 8; ++index)
                chain[index] = request(200u + index, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 16, 16);
            for (u32 index = 0; index < 8; ++index)
                chain[8u + index] = request(300u + index, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 16, 16);
            const bool built = rendering::detail::BuildPlacedRangePlan(chain, config, plan, scratch, &failure);
            bool exactDrain = built && plan.heaps.Size() == 1;
            for (u32 index = 0; index < 8 && exactDrain; ++index)
            {
                const rendering::detail::PlacedRangeAssignment* reused = find(300u + index);
                exactDrain = reused != nullptr && reused->offset == static_cast<vanguard::u64>(index) * 16u && reused->predecessorCount == 1 &&
                             plan.predecessors[reused->predecessorOffset].allocation == 200u + index &&
                             plan.predecessors[reused->predecessorOffset].offset == static_cast<vanguard::u64>(index) * 16u &&
                             plan.predecessors[reused->predecessorOffset].size == 16;
            }
            check(exactDrain, "long coalesced provenance chains drain in address order without losing exact predecessor coverage");
        }

        {
            rendering::detail::PlacedRangePlannerConfig splitConfig = config;
            splitConfig.minimumHeapBytes = 128;
            const rendering::detail::PlacedRangeRequest split[] = {
                request(400, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 7, 16, 16),
                request(401, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 1, 2, 96, 16),
                request(402, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 3, 6, 16, 64),
                request(403, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 4, 5, 48, 16),
                request(404, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 4, 5, 32, 16),
            };
            const bool built = rendering::detail::BuildPlacedRangePlan(split, splitConfig, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* middle = find(402);
            const rendering::detail::PlacedRangeAssignment* left = find(403);
            const rendering::detail::PlacedRangeAssignment* right = find(404);
            const bool middleExact = middle != nullptr && middle->offset == 64 && middle->predecessorCount == 1 &&
                                     plan.predecessors[middle->predecessorOffset].allocation == 401 && plan.predecessors[middle->predecessorOffset].offset == 64 &&
                                     plan.predecessors[middle->predecessorOffset].size == 16;
            const bool leftExact = left != nullptr && left->offset == 16 && left->predecessorCount == 1 &&
                                   plan.predecessors[left->predecessorOffset].allocation == 401 && plan.predecessors[left->predecessorOffset].offset == 16 &&
                                   plan.predecessors[left->predecessorOffset].size == 48;
            const bool rightExact = right != nullptr && right->offset == 80 && right->predecessorCount == 1 &&
                                    plan.predecessors[right->predecessorOffset].allocation == 401 && plan.predecessors[right->predecessorOffset].offset == 80 &&
                                    plan.predecessors[right->predecessorOffset].size == 32;
            check(built && plan.heaps.Size() == 1 && middleExact && leftExact && rightExact,
                  "two-sided provenance split preserves one predecessor across the middle consume and both residual ranges");
        }

        {
            rendering::detail::PlacedRangePlannerConfig middleConfig = config;
            middleConfig.minimumHeapBytes = 128;
            const rendering::detail::PlacedRangeRequest middleSplit[] = {
                request(409, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 8, 16, 16),
                request(410, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 1, 4, 32, 16),
                request(411, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 4, 32, 16),
                request(412, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 3, 4, 32, 16),
                request(413, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 5, 7, 32, 64),
            };
            const bool built = rendering::detail::BuildPlacedRangePlan(middleSplit, middleConfig, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* middle = find(413);
            const bool exact = middle != nullptr && middle->offset == 64 && middle->predecessorCount == 2 &&
                               plan.predecessors[middle->predecessorOffset].allocation == 411 && plan.predecessors[middle->predecessorOffset].offset == 64 &&
                               plan.predecessors[middle->predecessorOffset].size == 16 &&
                               plan.predecessors[middle->predecessorOffset + 1u].allocation == 412 && plan.predecessors[middle->predecessorOffset + 1u].offset == 80 &&
                               plan.predecessors[middle->predecessorOffset + 1u].size == 16;
            check(built && plan.heaps.Size() == 1 && exact,
                  "middle consume crosses provenance boundaries while preserving clipped predecessor fragments exactly");
        }

        {
            const rendering::detail::PlacedRangeRequest earliestHeap[] = {
                request(47, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 128, 64),
                request(48, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 128, 64),
                request(49, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 2, 3, 64, 64),
            };
            const bool built = rendering::detail::BuildPlacedRangePlan(earliestHeap, config, plan, scratch, &failure);
            const rendering::detail::PlacedRangeAssignment* reused = find(49);
            check(built && plan.heaps.Size() == 2 && reused != nullptr && reused->heap == 0 && reused->offset == 0,
                  "compatibility fit indexing preserves deterministic earliest-heap first-fit after global release promotion");
        }

        {
            rendering::detail::PlacedRangeRequest spike[64]{};
            for (u32 index = 0; index < 64; ++index)
                spike[index] = request(100u + index, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 2, 128, 64, static_cast<vanguard::u64>(index) + 1u);
            const bool spikeBuilt = rendering::detail::BuildPlacedRangePlan(spike, config, plan, scratch, &failure);
            const bool spikeIndexed = scratch.heaps.Size() == 64 && scratch.groups.Size() == 64 && scratch.seenAllocations.keys.Size() >= 128 && scratch.compatibleHeaps.entries.Size() >= 128;

            for (u32 frame = 0; frame < 32; ++frame)
            {
                scratch.Reset();
                scratch.DecayWithoutUse();
            }
            bool dormantBackingReleased = scratch.heaps.Size() == 64 && scratch.groups.Size() == 64;
            for (u32 index = 1; index < 64 && dormantBackingReleased; ++index)
                dormantBackingReleased = scratch.heaps[index].freeRuns.Capacity() == 0 && scratch.heaps[index].provenance.Capacity() == 0 &&
                                         scratch.groups[index].heapIndices.Capacity() == 0 && scratch.groups[index].fitTree.Capacity() == 0;
            const bool transientBackingDecayed = scratch.seenAllocations.keys.Size() <= 8 && scratch.compatibleHeaps.entries.Size() <= 8 &&
                                                 scratch.order.Capacity() <= 128 && scratch.releaseEvents.Capacity() <= 128 && scratch.candidateFragments.Capacity() <= 128;
            check(spikeBuilt && spikeIndexed && dormantBackingReleased && transientBackingDecayed,
                  "placed planner high-water scratch decays across zero-use frames with bounded dormant-slot work and without capacity-wide reset scans");
        }

        {
            const rendering::detail::PlacedRangeRequest classes[] = {
                request(50, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 2, 64, 64),
                request(51, rendering::FrameResourceKind::Buffer, rhi::QueueType::Compute, 0, 2, 64, 64),
                request(52, rendering::FrameResourceKind::Texture, rhi::QueueType::Graphics, 0, 2, 64, 64)};
            check(rendering::detail::BuildPlacedRangePlan(classes, config, plan, scratch, &failure) && plan.heaps.Size() == 3,
                  "placed heaps remain separated by resource kind and command queue");

            rendering::detail::PlacedRangeRequest copy = request(53, rendering::FrameResourceKind::Buffer, rhi::QueueType::Copy, 0, 1, 64, 64);
            check(!rendering::detail::BuildPlacedRangePlan({&copy, 1}, config, plan, scratch, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::UnsupportedCapability && plan.heaps.Empty() && plan.assignments.Empty(),
                  "initial placed planning rejects copy-queue ownership without publishing a partial plan");
        }

        {
            rendering::detail::PlacedRangePlannerConfig overflow = config;
            overflow.minimumHeapBytes = ~vanguard::u64{0} - 31u;
            rendering::detail::PlacedRangeRequest value = request(60, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 1, 64, 64);
            check(!rendering::detail::BuildPlacedRangePlan({&value, 1}, overflow, plan, scratch, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::ArithmeticOverflow && plan.heaps.Empty(),
                  "placed heap growth rejects checked u64 alignment overflow transactionally");

            const rendering::detail::PlacedRangeRequest lateOverflow[] = {
                request(61, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 0, 3, 64, 64),
                request(62, rendering::FrameResourceKind::Buffer, rhi::QueueType::Graphics, 1, 2, ~vanguard::u64{0} - 31u, 64)};
            check(!rendering::detail::BuildPlacedRangePlan(lateOverflow, config, plan, scratch, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::ArithmeticOverflow && plan.heaps.Empty() &&
                      plan.assignments.Empty() && plan.predecessors.Empty(),
                  "a late placed-planning failure discards every earlier scratch assignment");
        }
    }
} // namespace

void RunRenderFlowResourceAllocatorTests(void (*check)(bool condition, const char* message) noexcept) noexcept
{
    TestDeterministicMergeAndExecution(check);
    TestActiveUseSwapCaptureAndScopeMapping(check);
    TestDenseResourceUseIdentity(check);
    TestCompiledExecutionRangeTopology(check);
    TestDedicatedResourceLifetimePlanning(check);
    TestFirstTouchPhysicalAssignmentOrder(check);
    TestMissingScopeBeginFailure(check);
    TestPoisonedWriterBlocksSeal(check);
    TestDuplicateDeclaration(check);
    TestAggregatePlanningCaps(check);
    TestRangeAndSubresourceValidation(check);
    TestStateAndActiveUseValidation(check);
    TestTypedClearActions(check);
    TestCrossQueueWaitProofRequired(check);
    TestDuplicateCommandScopeFallback(check);
    TestSealIdentityScratchReuse(check);
    TestCrossQueueForkJoinSchedule(check);
    TestRepeatedCrossQueueOrderFacts(check);
    TestCompiledActionCapacity(check);
    TestPrePublicationAbort(check);
    TestStalePlanningWriterIsolation(check);
    TestPersistentCandidateResourceReuse(check);
    TestAdaptiveNamedResourceIndexAndCapacity(check);
    TestTerminalLifecycleRecovery(check);
    TestPhysicalProviderRequirement(check);
    TestPlacedRangePlanner(check);
}
