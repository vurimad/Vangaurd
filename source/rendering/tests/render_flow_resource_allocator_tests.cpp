#include <vanguard/rendering/render_flow_resource_execution.hpp>

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
        if (!allocator.Initialize({}, &failure))
            return false;

        rendering::FrameResourceSession session;
        if (!allocator.BeginFrame(reverseWriterClose ? 2 : 1, {}, session, &failure))
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
        if (!session.CreatePlanningWriter(nodeA, flowA, scopeA, writerA, &failure) || !session.CreatePlanningWriter(nodeB, flowB, scopeB, writerB, &failure) ||
            !session.CreatePlanningWriter(nodeC, flowC, scopeC, writerC, &failure))
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
        if (!closed || !session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return false;

        const rendering::CompiledCommandScope scopes[] = {{scopeC, rhi::QueueType::Graphics, 2}, {scopeA, rhi::QueueType::Graphics, 0}, {scopeB, rhi::QueueType::Graphics, 1}};
        const rendering::CompiledQueueSchedule schedule{containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)};
        const rendering::SurvivingGraphOverlay surviving{{}, true};
        rendering::ExecutionGenerationRef generation;
        if (!session.Resolve(surviving, schedule, generation, nullptr, &failure))
            return false;
        session.CancelBeforePublication();
        check(session.GetState() == rendering::RenderFlowResourceSessionState::Ready && generation.IsValid(), "pre-publication cancellation cannot detach a published execution generation");

        rendering::CompiledExecutionPacketView packetA;
        rendering::CompiledExecutionPacketView packetB;
        rendering::CompiledExecutionPacketView packetC;
        check(!session.PacketFor(nodeA, packetA, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "packet lookup requires an explicit execution transition");
        if (!session.BeginExecution(&failure))
            return false;
        check(!session.BeginExecution(&failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidPhase,
              "execution transition is coordinator-owned and occurs exactly once");
        if (!session.PacketFor(nodeA, packetA, &failure) || !session.PacketFor(nodeB, packetB, &failure) || !session.PacketFor(nodeC, packetC, &failure))
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
        check(!cursorB.BeginTextureUse(readB, wrongKind, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
              "execution cursor rejects the wrong resource kind");
        check(!cursorB.BeginBufferUse(writeB, resolvedWriteB, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution,
              "execution cursor rejects reordered resource use");
        check(!cursorA.FinalizePacket(&failure), "execution cursor rejects an incomplete packet");
        if (!cursorA.BeginBufferUse(writeA, resolvedA, &failure))
            return false;
        result.firstAllocation = resolvedA.GetPhysicalResource().index;
        check(resolvedA.IsValid(), "resolved buffer use is valid only inside its compiled scope");
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
        const rendering::ExecutionGenerationId wrongGeneration{generation.GetId().index, generation.GetId().generation + 1u};
        const rendering::TerminalExecutionReceipt wrongTerminal{generation.GetId(), rendering::TerminalExecutionCompletionKind::Completed,
                                                                containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                rendering::TerminalJoinToken::CompletedSynchronously(wrongGeneration)};
        check(!session.Finish(wrongTerminal, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompleteExecution &&
                  session.GetState() == rendering::RenderFlowResourceSessionState::Executing,
              "terminal join proof is generation-bound and invalid proof does not mutate execution state");
        const rendering::TerminalExecutionReceipt terminal{generation.GetId(), rendering::TerminalExecutionCompletionKind::Completed,
                                                           containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation.GetId())};
        if (!session.Finish(terminal, &failure))
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
        generation.Reset();
        resolvedReadB = {};
        resolvedWriteB = {};
        resolvedSecondWriteB = {};
        resolvedC = {};
        check(!resolvedA.IsValid(), "expired resolved use retains only a packet-local liveness witness for a memory-safe stale check");
        resolvedA = {};
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        check(stats.state == rendering::RenderFlowResourceSessionState::Idle && stats.completedFrames == 1 && stats.compiledPackets == 3,
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

    void TestCulledScopeFailure(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::FrameResourceSession session;
        check(allocator.Initialize({}, &failure) && allocator.BeginFrame(10, {}, session, &failure), "broken-scope test starts a planning session");
        rendering::ResourcePlanningWriter openWriter;
        rendering::ResourcePlanningWriter closeWriter;
        const rendering::RenderFlowNodeId openNode{20};
        const rendering::RenderFlowNodeId closeNode{21};
        check(session.CreatePlanningWriter(openNode, {20}, {20}, openWriter, &failure) && session.CreatePlanningWriter(closeNode, {21}, {21}, closeWriter, &failure),
              "broken-scope test creates writers");
        rendering::LogicalResourceId resource;
        const rendering::ResourceScopeId scope{77};
        check(openWriter.DeclareBuffer({{1}, "culled.resource"}, MakeBufferDesc(), resource, &failure) && openWriter.OpenResourceScope(resource, scope, &failure) &&
                  closeWriter.CloseResourceScope(scope, &failure) && openWriter.Close(&failure) && closeWriter.Close(&failure) &&
                  session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "broken-scope candidates seal before culling");
        const rendering::RenderFlowNodeId survivors[] = {closeNode};
        const rendering::CompiledCommandScope scopes[] = {{{21}, rhi::QueueType::Graphics, 0}};
        rendering::ExecutionGenerationRef generation;
        const bool resolved = session.Resolve({containers::ArraySpan<const rendering::RenderFlowNodeId>(survivors), false},
                                              {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure);
        check(!resolved && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope && allocator.GetStats().state == rendering::RenderFlowResourceSessionState::Idle &&
                  !generation.IsValid(),
              "culling one cross-node scope endpoint aborts scratch Resolve without publication");
        check(allocator.Shutdown(&failure), "failed Resolve leaves allocator shutdown-safe");
    }

    void TestPoisonedWriterBlocksSeal(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::FrameResourceSession session;
        check(allocator.Initialize({}, &failure) && allocator.BeginFrame(11, {}, session, &failure), "writer-poison test starts a planning session");
        rendering::ResourcePlanningWriter writerA;
        rendering::ResourcePlanningWriter writerB;
        check(session.CreatePlanningWriter({30}, {30}, {30}, writerA, &failure) && session.CreatePlanningWriter({31}, {31}, {31}, writerB, &failure), "writer-poison test creates writers");
        rendering::LogicalResourceId resourceA;
        rendering::LogicalResourceId resourceB;
        rendering::ResourceUseId forgedUse;
        check(writerA.DeclareBuffer({{2}, "owned.resource"}, MakeBufferDesc(), resourceA, &failure), "first declaration is accepted");
        check(!writerB.BeginBufferUse(resourceA, ReadUse(), forgedUse, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
              "writer-local logical ids cannot be used by another flow group");
        check(!writerB.DeclareBuffer({{2}, "later.resource"}, MakeBufferDesc(), resourceB, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::IncompletePlanning,
              "a rejected operation poisons its writer instead of publishing a partial tape");
        check(writerA.Close(&failure) && !writerB.Close(&failure) && !session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::IncompletePlanning,
              "a poisoned writer prevents candidate publication");
        session.CancelBeforePublication();
        check(allocator.GetStats().rejectedOperations >= 2 && allocator.Shutdown(&failure), "writer rejection accounting is merged safely at writer close");
    }

    void TestDuplicateDeclaration(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::FrameResourceSession session;
        check(allocator.Initialize({}, &failure) && allocator.BeginFrame(12, {}, session, &failure), "duplicate-declaration test starts a planning session");
        rendering::ResourcePlanningWriter writerA;
        rendering::ResourcePlanningWriter writerB;
        check(session.CreatePlanningWriter({30}, {30}, {30}, writerA, &failure) && session.CreatePlanningWriter({31}, {31}, {31}, writerB, &failure),
              "duplicate-declaration test creates writers");
        const rendering::LogicalResourceKey key{{2}, "duplicate.resource"};
        rendering::LogicalResourceId resourceA;
        rendering::LogicalResourceId resourceB;
        check(writerA.DeclareBuffer(key, MakeBufferDesc(), resourceA, &failure) && writerB.DeclareBuffer(key, MakeBufferDesc(), resourceB, &failure) && writerA.Close(&failure) &&
                  writerB.Close(&failure) && session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "duplicate declaration reaches deterministic Resolve validation");
        const rendering::CompiledCommandScope scopes[] = {{{30}, rhi::QueueType::Graphics, 0}, {{31}, rhi::QueueType::Graphics, 1}};
        rendering::ExecutionGenerationRef generation;
        check(!session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict && !generation.IsValid(),
              "duplicate named declaration fails atomically even when descriptors match");
        check(allocator.Shutdown(&failure), "duplicate declaration failure leaves allocator shutdown-safe");
    }

    void TestAggregatePlanningCaps(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.maximumLogicalResources = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter first;
            rendering::ResourcePlanningWriter second;
            rendering::LogicalResourceId firstResource;
            rendering::LogicalResourceId secondResource;
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(13, {}, session, &failure) && session.CreatePlanningWriter({50}, {50}, {50}, first, &failure) &&
                      session.CreatePlanningWriter({51}, {51}, {51}, second, &failure) && first.ReferenceResource({{5}, "first"}, firstResource, &failure) &&
                      second.ReferenceResource({{5}, "second"}, secondResource, &failure) && first.Close(&failure) && second.Close(&failure) &&
                      !session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
                  "logical-resource capacity is an aggregate frame limit independent of writer close order");
            session.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "aggregate resource-cap failure remains cancellation-safe");
        }
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.maximumLogicalResources = 2;
            config.maximumViews = 1;
            rendering::RenderFlowResourceAllocator allocator;
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter first;
            rendering::ResourcePlanningWriter second;
            rendering::LogicalResourceId firstResource;
            rendering::LogicalResourceId secondResource;
            rendering::LogicalBufferViewId firstView;
            rendering::LogicalBufferViewId secondView;
            const rendering::FrameBufferDesc desc = MakeBufferDesc();
            check(allocator.Initialize(config, &failure) && allocator.BeginFrame(14, {}, session, &failure) && session.CreatePlanningWriter({52}, {52}, {52}, first, &failure) &&
                      session.CreatePlanningWriter({53}, {53}, {53}, second, &failure) && first.DeclareBuffer({{5}, "first.view"}, desc, firstResource, &failure) &&
                      first.CreateBufferView(firstResource, {}, firstView, &failure) && second.DeclareBuffer({{5}, "second.view"}, desc, secondResource, &failure) &&
                      second.CreateBufferView(secondResource, {}, secondView, &failure) && first.Close(&failure) && second.Close(&failure) &&
                      !session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) && failure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded,
                  "texture and buffer views share one deterministic aggregate frame capacity");
            session.CancelBeforePublication();
            check(allocator.Shutdown(&failure), "aggregate view-cap failure remains cancellation-safe");
        }
    }

    void TestDuplicateScheduleOrder(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::FrameResourceSession session;
        rendering::ResourcePlanningWriter writer;
        check(allocator.Initialize({}, &failure) && allocator.BeginFrame(15, {}, session, &failure) && session.CreatePlanningWriter({60}, {60}, {60}, writer, &failure) &&
                  writer.Close(&failure) && session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
              "duplicate schedule-order test seals a minimal plan");
        const rendering::CompiledCommandScope scopes[] = {{{60}, rhi::QueueType::Graphics, 4}, {{61}, rhi::QueueType::Compute, 4}};
        rendering::ExecutionGenerationRef generation;
        check(!session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure) &&
                  failure.code == rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch && !generation.IsValid(),
              "queue schedule stable-order positions are authoritative and unique");
        check(allocator.Shutdown(&failure), "invalid queue schedule fails before generation publication");
    }

    void TestRangeAndSubresourceValidation(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId texture;
            rendering::ResourceUseId writeMip;
            rendering::ResourceUseId readOtherMip;
            check(allocator.Initialize({}, &failure) && allocator.BeginFrame(17, {}, session, &failure) && session.CreatePlanningWriter({70}, {70}, {70}, writer, &failure) &&
                      writer.DeclareTexture({{7}, "mipped.texture"}, MakeTextureDesc(), texture, &failure) && writer.BeginTextureUse(texture, TextureWriteUse(0), writeMip, &failure) &&
                      writer.EndUse(writeMip, &failure) && writer.BeginTextureUse(texture, TextureReadUse(1), readOtherMip, &failure) && writer.EndUse(readOtherMip, &failure) &&
                      writer.Close(&failure) && session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "subresource-content test seals its candidate tape");
            const rendering::CompiledCommandScope scopes[] = {{{70}, rhi::QueueType::Graphics, 0}};
            rendering::ExecutionGenerationRef generation;
            check(!session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope && !generation.IsValid(),
                  "writing one mip does not make a different mip's contents defined");
            check(allocator.Shutdown(&failure), "subresource-content failure leaves no published generation");
        }
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId texture;
            rendering::LogicalTextureViewId view;
            rhi::TextureViewDesc invalidView;
            invalidView.subresources = {2, 1, 0, 1};
            check(allocator.Initialize({}, &failure) && allocator.BeginFrame(18, {}, session, &failure) && session.CreatePlanningWriter({71}, {71}, {71}, writer, &failure) &&
                      writer.DeclareTexture({{7}, "invalid.view.texture"}, MakeTextureDesc(), texture, &failure) && writer.CreateTextureView(texture, invalidView, view, &failure) &&
                      writer.Close(&failure) && session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "invalid texture-view test reaches Resolve validation");
            const rendering::CompiledCommandScope scopes[] = {{{71}, rhi::QueueType::Graphics, 0}};
            rendering::ExecutionGenerationRef generation;
            check(!session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict,
                  "out-of-range texture views fail before execution generation publication");
            check(allocator.Shutdown(&failure), "invalid texture-view failure is cleanup-safe");
        }
        {
            rendering::RenderFlowResourceAllocator allocator;
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId buffer;
            rendering::LogicalBufferViewId view;
            const rendering::FrameBufferDesc desc = MakeBufferDesc();
            check(allocator.Initialize({}, &failure) && allocator.BeginFrame(19, {}, session, &failure) && session.CreatePlanningWriter({72}, {72}, {72}, writer, &failure) &&
                      writer.DeclareBuffer({{7}, "invalid.view.buffer"}, desc, buffer, &failure) &&
                      writer.CreateBufferView(buffer, {rhi::Format::Unknown, desc.active.size - 8, 32, 0}, view, &failure) && writer.Close(&failure) &&
                      session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure),
                  "invalid buffer-view test reaches Resolve validation");
            const rendering::CompiledCommandScope scopes[] = {{{72}, rhi::QueueType::Graphics, 0}};
            rendering::ExecutionGenerationRef generation;
            check(!session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure) &&
                      failure.code == rendering::RenderFlowResourceFailureCode::DescriptorConflict,
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
            rendering::FrameResourceSession session;
            rendering::ResourcePlanningWriter writer;
            constexpr rendering::RenderFlowNodeId node{80};
            constexpr rendering::GpuFlowGroupId flow{80};
            constexpr rendering::CommandScopeId scope{80};
            const bool planned = allocator.Initialize({}, &failure) && allocator.BeginFrame(frameSerial, {}, session, &failure) &&
                                 session.CreatePlanningWriter(node, flow, scope, writer, &failure) && record(writer, failure) && writer.Close(&failure) &&
                                 session.SealCandidates(rendering::PlanningJoinToken::CompletedSynchronously(), &failure);
            check(planned, message);
            if (!planned)
            {
                writer.Abandon();
                session.CancelBeforePublication();
                check(allocator.Shutdown(&failure), "failed state-validation setup remains shutdown-safe");
                return;
            }

            const rendering::CompiledCommandScope scopes[] = {{scope, queue, 0}};
            rendering::ExecutionGenerationRef generation;
            const bool resolved = session.Resolve({{}, true}, {containers::ArraySpan<const rendering::CompiledCommandScope>(scopes)}, generation, nullptr, &failure);
            check(expectSuccess ? resolved : !resolved && failure.code == rendering::RenderFlowResourceFailureCode::InvalidUseOrScope, message);
            if (resolved)
            {
                const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, queue, {}}};
                const rendering::TerminalExecutionReceipt terminal{generation.GetId(), rendering::TerminalExecutionCompletionKind::Aborted,
                                                                   containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                                   rendering::TerminalJoinToken::CompletedSynchronously(generation.GetId())};
                check(session.Finish(terminal, &failure), "valid logical state plan can be abandoned before command submission");
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
    }

    void TestPrePublicationAbort(CheckFunction check) noexcept
    {
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::FrameResourceSession session;
        check(allocator.Initialize({}, &failure) && allocator.BeginFrame(16, {}, session, &failure), "pre-publication abort test starts a session");
        rendering::ResourcePlanningWriter writer;
        check(session.CreatePlanningWriter({40}, {40}, {40}, writer, &failure), "pre-publication abort test creates a writer");
        writer.Abandon();
        session.CancelBeforePublication();
        const rendering::RenderFlowResourceAllocatorStats once = allocator.GetStats();
        session.CancelBeforePublication();
        const rendering::RenderFlowResourceAllocatorStats twice = allocator.GetStats();
        check(once.state == rendering::RenderFlowResourceSessionState::Idle && once.abortedFrames == 1 && twice.abortedFrames == once.abortedFrames,
              "pre-publication abort returns to Idle exactly once");
        check(allocator.Shutdown(&failure), "pre-publication abort leaves allocator shutdown-safe");
    }
} // namespace

void RunRenderFlowResourceAllocatorTests(void (*check)(bool condition, const char* message) noexcept) noexcept
{
    TestDeterministicMergeAndExecution(check);
    TestCulledScopeFailure(check);
    TestPoisonedWriterBlocksSeal(check);
    TestDuplicateDeclaration(check);
    TestAggregatePlanningCaps(check);
    TestDuplicateScheduleOrder(check);
    TestRangeAndSubresourceValidation(check);
    TestStateAndActiveUseValidation(check);
    TestPrePublicationAbort(check);
}
