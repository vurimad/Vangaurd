#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rendering/geometry_allocator.hpp>
#include <vanguard/rendering/geometry_upload.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/material_resource_resolver.hpp>
#include <vanguard/rendering/mesh_geometry_upload.hpp>
#include <vanguard/rendering/mesh_lod_geometry_uploader.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_node_graph_factory.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>
#include <vanguard/rendering/texture_uploader.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include "../private/vanguard/rendering/render_flow_resource_placed.hpp"
#include "../private/vanguard/rendering/render_flow_resource_pool.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace vanguard::rendering::tests
{
    [[nodiscard]] bool RunMeshDrawLayoutProof() noexcept;
    [[nodiscard]] bool RunMaterialResourceResolverProof(MaterialResourceResolver& resolver, TextureResidencyRuntime& textures, GpuSceneRuntime& runtime, RenderCommandSystem& commands,
                                                        RenderSceneManager& scenes, const resources::ResourceHandle& mesh, rhi::DescriptorDomainRef resourceDescriptors,
                                                        rhi::DescriptorDomainRef samplerDescriptors) noexcept;
}

namespace
{
    namespace gpu = vanguard::rhi;
    namespace jobs = vanguard::jobs;
    namespace meshes = vanguard::meshes;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace shaders = vanguard::shaders;
    namespace textures = vanguard::textures;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    inline constexpr resources::ResourceTypeId TestMaterialType = vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T');

    [[nodiscard]] bool RunDedicatedResourceProviderSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize({}, &failure))
            return false;

        const auto runFrame = [&allocator, &failure](const vanguard::u64 frameSerial, gpu::BufferRef& resolvedBuffer) noexcept
        {
            rendering::ResourcePlanningWriter writer;
            constexpr rendering::RenderFlowNodeId node{1};
            constexpr rendering::GpuFlowGroupId flow{1};
            constexpr rendering::CommandScopeId scope{1};
            constexpr rendering::FlowSpaceId space{1};
            rendering::FrameBufferDesc desc;
            desc.active.size = 4096;
            desc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            desc.active.initialState = gpu::ResourceState::Common;
            desc.maximumSize = desc.active.size;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            rendering::ResourceUseId secondUse;
            rendering::BufferUseDesc useDesc;
            useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
            useDesc.access = rendering::LogicalAccessIntent::Write;
            useDesc.content = rendering::ResourceContentIntent::Discard;
            if (!allocator.BeginFrame(frameSerial, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                !writer.DeclareBuffer({space, "dedicated.provider.smoke"}, desc, resource, &failure) || !writer.BeginBufferUse(resource, useDesc, use, &failure) || !writer.EndUse(use, &failure) ||
                !writer.BeginBufferUse(resource, useDesc, secondUse, &failure) || !writer.EndUse(secondUse, &failure) || !writer.Close(&failure) ||
                !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                return false;
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::ResolvedBufferUse resolved;
            rendering::ResolvedBufferUse secondResolved;
            gpu::Failure rhiFailure;
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246414255464645ull, &rhiFailure);
            if (!commands)
                return false;
            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !allocator.PacketFor(node, packet, &failure) ||
                packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch ||
                !gpu::BindCommandList(commands, &rhiFailure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || !cursor.BeginBufferUse(use, resolved, &failure) ||
                !resolved.GetBuffer().IsValid())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            resolvedBuffer = resolved.GetBuffer();
            if (!cursor.EndUse(use, &failure) || !cursor.BeginBufferUse(secondUse, secondResolved, &failure) || secondResolved.GetBuffer() != resolvedBuffer || !cursor.EndUse(secondUse, &failure) ||
                !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[] = {commands};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("resource-flow dedicated-buffer transitions", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
                return false;
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            return allocator.Finish(terminal, &failure) && gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure);
        };

        gpu::BufferRef first;
        gpu::BufferRef second;
        if (!runFrame(1, first) || !runFrame(2, second) || first != second)
            return false;

        const auto runTextureFrame = [&allocator, &failure](const vanguard::u64 frameSerial, const gpu::Extent3D maximumExtent, const vanguard::u16 maximumMipCount, gpu::TextureRef& resolvedTexture) noexcept
        {
            rendering::ResourcePlanningWriter writer;
            constexpr rendering::RenderFlowNodeId node{2};
            constexpr rendering::GpuFlowGroupId flow{2};
            constexpr rendering::CommandScopeId scope{2};
            constexpr rendering::FlowSpaceId space{2};
            rendering::FrameTextureDesc desc;
            desc.active.extent = {8, 8, 1};
            desc.active.dimension = gpu::TextureDimension::Texture2D;
            desc.active.format = gpu::Format::R8G8B8A8UNorm;
            desc.active.mipCount = 2;
            desc.active.arraySize = 1;
            desc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
            desc.active.initialState = gpu::ResourceState::Common;
            desc.maximumExtent = maximumExtent;
            desc.maximumMipCount = maximumMipCount;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            rendering::TextureUseDesc useDesc;
            useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
            useDesc.subresources = {0, 1, 0, 1};
            useDesc.access = rendering::LogicalAccessIntent::Write;
            useDesc.content = rendering::ResourceContentIntent::Discard;
            if (!allocator.BeginFrame(frameSerial, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                !writer.DeclareTexture({space, "dedicated.provider.texture.shape"}, desc, resource, &failure) || !writer.BeginTextureUse(resource, useDesc, use, &failure) || !writer.EndUse(use, &failure) ||
                !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                return false;
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::ResolvedTextureUse resolved;
            gpu::Failure rhiFailure;
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246415445585452ull, &rhiFailure);
            if (!commands || !gpu::BindCommandList(commands, &rhiFailure))
                return false;
            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !allocator.PacketFor(node, packet, &failure) ||
                !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || !cursor.BeginTextureUse(use, resolved, &failure) ||
                !resolved.GetTexture().IsValid())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            resolvedTexture = resolved.GetTexture();
            gpu::TextureDesc physicalDesc;
            if (!gpu::GetTextureDesc(resolvedTexture, physicalDesc, &rhiFailure) || physicalDesc.extent.width != desc.active.extent.width || physicalDesc.extent.height != desc.active.extent.height ||
                physicalDesc.extent.depth != desc.active.extent.depth || physicalDesc.mipCount != desc.active.mipCount)
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            if (!cursor.EndUse(use, &failure) || !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[] = {commands};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("resource-flow dedicated-texture transitions", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
                return false;
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            return allocator.Finish(terminal, &failure) && gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure);
        };

        gpu::TextureRef firstTexture;
        gpu::TextureRef secondTexture;
        if (!runTextureFrame(3, {16, 16, 1}, 4, firstTexture) || !runTextureFrame(4, {32, 32, 1}, 5, secondTexture) || firstTexture != secondTexture)
            return false;
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        gpu::Failure clearFailure;
        if (stats.dedicatedResourcePoolMisses != 2 || stats.dedicatedResourcePoolHits != 2 || stats.chargedTextureBytes == 0 || stats.chargedBufferBytes == 0 ||
            stats.chargedNativeBytes != stats.chargedTextureBytes + stats.chargedBufferBytes || !allocator.ClearPersistentCaches(&failure))
            return false;
        const rendering::RenderFlowResourceAllocatorStats clearing = allocator.GetStats();
        if (clearing.chargedNativeBytes != stats.chargedNativeBytes || clearing.pendingNativeDestructionResources != 2 || !gpu::FlushRetiredResources(&clearFailure))
            return false;
        const rendering::RenderFlowResourceAllocatorStats cleared = allocator.GetStats();
        return cleared.chargedNativeBytes == 0 && cleared.pendingNativeDestructionResources == 0 && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunDedicatedResourceLifetimeReuseTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{8};
        constexpr rendering::GpuFlowGroupId flow{8};
        constexpr rendering::CommandScopeId scope{8};
        constexpr rendering::FlowSpaceId space{8};
        rendering::FrameBufferDesc desc;
        desc.active.size = 4096;
        desc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.maximumSize = desc.active.size;
        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::LogicalResourceId resources[3];
        rendering::ResourceUseId uses[3];
        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(8, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "dedicated.lifetime.a"}, desc, resources[0], &failure) || !writer.DeclareBuffer({space, "dedicated.lifetime.b"}, desc, resources[1], &failure) ||
            !writer.DeclareBuffer({space, "dedicated.lifetime.c"}, desc, resources[2], &failure) || !writer.BeginBufferUse(resources[0], write, uses[0], &failure) ||
            !writer.BeginBufferUse(resources[1], write, uses[1], &failure) || !writer.EndUse(uses[0], &failure) || !writer.EndUse(uses[1], &failure) ||
            !writer.BeginBufferUse(resources[2], write, uses[2], &failure) || !writer.EndUse(uses[2], &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return false;

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolved[3];
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246414C49464554ull, &rhiFailure);
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) ||
            !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || !cursor.BeginBufferUse(uses[0], resolved[0], &failure) ||
            !cursor.BeginBufferUse(uses[1], resolved[1], &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return false;
        }
        const gpu::BufferRef first = resolved[0].GetBuffer();
        const gpu::BufferRef overlapping = resolved[1].GetBuffer();
        if (!first.IsValid() || !overlapping.IsValid() || first == overlapping || !cursor.EndUse(uses[0], &failure) || !cursor.EndUse(uses[1], &failure) ||
            !cursor.BeginBufferUse(uses[2], resolved[2], &failure) || resolved[2].GetBuffer() != first || !cursor.EndUse(uses[2], &failure) || !cursor.FinalizePacket(&failure) ||
            !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return false;
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow same-frame dedicated reuse", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return false;
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
            return false;
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        return stats.dedicatedResourcePoolMisses == 2 && stats.chargedBufferBytes != 0 && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunPlacedResourcePoolContractTest() noexcept
    {
        const gpu::Capabilities::PlacedResourceClass& placedClass = gpu::GetCapabilities().placedResources.buffers;
        if (!placedClass.IsSupported())
            return true;

        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-placed-provider] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        gpu::BufferDesc virtualDesc = bufferDesc.active;
        virtualDesc.virtualResource = true;
        gpu::MemoryRequirements requirements;
        if (!gpu::GetMemoryRequirements(virtualDesc, requirements, &rhiFailure) || requirements.size == 0 || requirements.alignment == 0)
            return fail("virtual buffer requirements");

        rendering::detail::PlacedRangePlannerConfig plannerConfig;
        plannerConfig.minimumHeapBytes = placedClass.maximumHeapAlignment;
        plannerConfig.heapAlignment = placedClass.maximumHeapAlignment;
        const auto request = [&requirements](const vanguard::u32 allocation, const gpu::QueueType queue, const vanguard::u32 first, const vanguard::u32 last) noexcept
        {
            rendering::detail::PlacedRangeRequest value;
            value.allocation = allocation;
            value.kind = rendering::FrameResourceKind::Buffer;
            value.queue = queue;
            value.firstAcquire = {{first + 1u}, first};
            value.lastRelease = {{last + 1u}, last};
            value.requirements = requirements;
            return value;
        };

        // Sequential logical buffers alias one native range, but must remain
        // distinct RHI objects while both objects belong to the acquired batch.
        rendering::detail::PlacedRangePlan plan;
        const rendering::detail::PlacedRangeRequest requests[] = {
            request(0, gpu::QueueType::Graphics, 0, 1),
            request(1, gpu::QueueType::Graphics, 2, 3),
        };
        if (!rendering::detail::BuildPlacedRangePlan(requests, plannerConfig, plan, &failure) || plan.heaps.Size() != 1 || plan.assignments.Size() != 2 ||
            plan.assignments[0].offset != plan.assignments[1].offset)
            return fail("single-heap alias plan");
        const rendering::detail::PlacedObjectRequest objects[] = {
            {0, rendering::FrameResourceDesc::Buffer(bufferDesc)},
            {1, rendering::FrameResourceDesc::Buffer(bufferDesc)},
        };
        rendering::RenderFlowResourceAllocatorConfig config;
        config.hardNativeByteLimit = plan.heaps[0].capacity;
        rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
        rendering::detail::PlacedResourcePool pool(config, ledger);
        rendering::detail::PlacedResourceBatch first;
        if (!pool.Acquire(plan, objects, 1, first, &failure) || !first.IsValid() || first.assignments.Size() != 2 || first.assignments[0].buffer == first.assignments[1].buffer ||
            first.assignments[0].placement.heap != first.assignments[1].placement.heap || first.assignments[0].placement.offset != first.assignments[1].placement.offset ||
            ledger.GetStats().chargedBytes != plan.heaps[0].capacity)
            return fail("initial placed acquisition");
        const gpu::BufferRef firstObject = first.assignments[0].buffer;
        const gpu::BufferRef secondObject = first.assignments[1].buffer;
        pool.Rollback(first);

        rendering::detail::PlacedResourceBatch reused;
        if (!pool.Acquire(plan, objects, 2, reused, &failure) || reused.assignments[0].buffer != firstObject || reused.assignments[1].buffer != secondObject)
            return fail("exact heap and object cache reuse");
        const rendering::detail::PlacedResourcePoolStats reusedStats = pool.GetStats();
        if (reusedStats.heapMisses != 1 || reusedStats.heapHits != 1 || reusedStats.objectMisses != 2 || reusedStats.objectHits != 2)
            return fail("placed cache accounting");
        pool.Rollback(reused);

        // Dedicated and placed providers charge one allocator-wide ledger.
        rendering::detail::DedicatedResourcePool dedicated(config, ledger);
        rendering::detail::DedicatedResourceAssignment blockedDedicated;
        if (dedicated.Acquire(rendering::FrameResourceDesc::Buffer(bufferDesc), 3, blockedDedicated, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
            return fail("shared provider hard budget");

        rendering::detail::PlacedResourceBatch pending;
        if (!pool.Acquire(plan, objects, 4, pending, &failure))
            return fail("pending-retirement acquisition");
        gpu::ResidencyFenceSet incomplete;
        incomplete.graphics = ~vanguard::u64{0};
        pool.Retire(pending, incomplete);
        rendering::detail::PlacedResourceBatch pendingBlocked;
        if (pool.Acquire(plan, objects, 5, pendingBlocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded || pool.GetStats().pendingRetirementHeaps != 1 ||
            ledger.GetStats().chargedBytes != config.hardNativeByteLimit)
            return fail("placed pending-retirement guard");
        pool.DeviceLost();
        if (ledger.GetStats().chargedBytes != 0 || pool.GetStats().pendingRetirementHeaps != 0)
            return fail("placed device-loss cleanup");

        // A later heap failure must remove every heap/object created earlier
        // in the same call and restore hit/miss statistics.
        rendering::detail::PlacedRangePlan twoHeapPlan;
        const rendering::detail::PlacedRangeRequest twoHeapRequests[] = {
            request(10, gpu::QueueType::Graphics, 0, 1),
            request(11, gpu::QueueType::Compute, 0, 1),
        };
        if (!rendering::detail::BuildPlacedRangePlan(twoHeapRequests, plannerConfig, twoHeapPlan, &failure) || twoHeapPlan.heaps.Size() != 2 || twoHeapPlan.heaps[0].capacity != twoHeapPlan.heaps[1].capacity)
            return fail("transactional two-heap plan");
        const rendering::detail::PlacedObjectRequest twoHeapObjects[] = {
            {10, rendering::FrameResourceDesc::Buffer(bufferDesc)},
            {11, rendering::FrameResourceDesc::Buffer(bufferDesc)},
        };
        rendering::RenderFlowResourceAllocatorConfig transactionalConfig;
        transactionalConfig.hardNativeByteLimit = twoHeapPlan.heaps[0].capacity;
        rendering::detail::AllocatorNativeByteLedger transactionalLedger(transactionalConfig.hardNativeByteLimit);
        rendering::detail::PlacedResourcePool transactionalPool(transactionalConfig, transactionalLedger);
        rendering::detail::PlacedResourceBatch failedBatch;
        if (transactionalPool.Acquire(twoHeapPlan, twoHeapObjects, 6, failedBatch, &failure) || failedBatch.IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded ||
            transactionalLedger.GetStats().chargedBytes != transactionalConfig.hardNativeByteLimit || transactionalPool.GetStats().pendingNativeDestructionHeaps != 1)
            return fail("transactional placed rollback");
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("transactional placed native release");
        transactionalPool.Poll();
        if (transactionalLedger.GetStats().chargedBytes != 0 || transactionalPool.GetStats().pendingNativeDestructionHeaps != 0)
            return fail("transactional placed observed release");
        const rendering::detail::PlacedResourcePoolStats failedStats = transactionalPool.GetStats();
        if (failedStats.heapHits != 0 || failedStats.heapMisses != 0 || failedStats.objectHits != 0 || failedStats.objectMisses != 0)
            return fail("transactional statistics");
        for (vanguard::u32 cycle = 0; cycle < 16; ++cycle)
        {
            rendering::detail::PlacedResourceBatch batch;
            if (!pool.Acquire(plan, objects, 10 + cycle, batch, &failure) || batch.heapEntries[0] != 0 || batch.objectEntries[0] != 0)
                return fail("released metadata recycling");
            pool.Rollback(batch);
            if (!pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("recycling native release");
            pool.Poll();
            if (ledger.GetStats().chargedBytes != 0)
                return fail("recycling ledger");
        }
        rendering::RenderFlowResourceAllocatorConfig liveConfig = config;
        liveConfig.hardNativeByteLimit *= 2;
        rendering::detail::AllocatorNativeByteLedger liveLedger(liveConfig.hardNativeByteLimit);
        rendering::detail::PlacedResourcePool livePool(liveConfig, liveLedger);
        rendering::detail::PlacedResourceBatch retiring;
        rendering::detail::PlacedResourceBatch held;
        if (!livePool.Acquire(plan, objects, 40, retiring, &failure) || !livePool.Acquire(plan, objects, 40, held, &failure))
            return fail("live batch recycling setup");
        const gpu::BufferRef heldObject = held.assignments[0].buffer;
        livePool.Retire(retiring, {}, false);
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("live batch native release");
        livePool.Poll();
        livePool.Rollback(held);
        livePool.Poll();
        rendering::detail::PlacedResourceBatch compacted;
        if (!livePool.Acquire(plan, objects, 41, compacted, &failure) || compacted.heapEntries[0] != 0 || compacted.assignments[0].buffer != heldObject)
            return fail("live batch indices survive deferred compaction");
        livePool.Rollback(compacted);
        return true;
    }

    class GroupOrderingNode final : public rendering::RenderNodeImpl
    {
    public:
        GroupOrderingNode(vanguard::u32* order, bool* valid, vanguard::u32 expected, bool deferred, rendering::RenderNodeCommandListUsage usage) noexcept
            : m_order(order), m_valid(valid), m_expected(expected), m_deferred(deferred), m_usage(usage) {}
        bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            return context.RTDecision(true);
        }
        void Execute(const rendering::RenderNodeImplContext& context, jobs::Builder* builder) const override
        {
            if (!m_deferred)
            {
                Consume(context);
                return;
            }
            static jobs::JobName name{"GroupOrdering/Child"};
            jobs::Task task = jobs::Task::Create([this, &context](const jobs::JobContext&) noexcept { Consume(context); });
            if (builder == nullptr || !builder->Dispatch(name, std::move(task)))
                *m_valid = false;
        }
        bool GetJobBuilderUsage() const noexcept override { return m_deferred; }
        rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return m_usage; }
    private:
        void Consume(const rendering::RenderNodeImplContext& context) const
        {
            *m_valid = *m_valid && context.RTDecision(false) && *m_order == m_expected;
            ++*m_order;
        }
        vanguard::u32* m_order;
        bool* m_valid;
        vanguard::u32 m_expected;
        bool m_deferred;
        rendering::RenderNodeCommandListUsage m_usage;
    };

    [[nodiscard]] bool RunMixedCommandListGroupTest() noexcept
    {
        vanguard::u32 order = 0;
        bool valid = true;
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            factory.BeginCommandListGroup(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), "MixedGroup");
            for (vanguard::u32 index = 0; index < 4; ++index)
                static_cast<void>(factory.Create<GroupOrderingNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), &order, &valid, index, index == 1,
                                                                   index == 2 ? rendering::RenderNodeCommandListUsage::Require : rendering::RenderNodeCommandListUsage::None));
            factory.EndCommandListGroup();
        }
        graph.BuildRenderFlowGroups();
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures preparationFailures;
        rendering::RenderFrameCommandLists lists;
        lists.PrepareForFrame(graph.GetNumNodes() + static_cast<vanguard::u32>(rendering::ReservedFrameCommandList::Count));
        rendering::RenderFrameInfo frame;
        rendering::RenderNodeImplContext context;
        rendering::RenderNodeImplContext::InitData init(0);
        init.frame = &frame;
        init.frameCommandLists = &lists;
        context.Init(init);
        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(1, {}, &failure) || !graph.PrepareResourceBindings(bindings, preparationFailures))
            return false;
        jobs::Builder declarations({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(context, allocator, bindings, preparationFailures, declarations);
        if (!declarations.WaitForCompletion() || preparationFailures.HasFailure() || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !graph.PrepareExecutionPackets(allocator, lists, bindings, &failure))
            return false;
        jobs::Builder kickoffBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        jobs::Counter kickoff = kickoffBuilder.ExtractCounter();
        if (!kickoff.IsValid())
            return false;
        bool completed = false;
        bool scheduled = false;
        jobs::Builder setup({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        static jobs::JobName name{"MixedGroup/Setup"};
        jobs::Task task = jobs::Task::Create([&](const jobs::JobContext& jobContext) noexcept
        {
            jobs::Task completion = jobs::Task::Create([&](const jobs::JobContext&) noexcept { completed = true; });
            scheduled = rendering::RenderNodeJob::RunRenderNodeJobs(graph, context, bindings, kickoff, jobContext, std::move(completion), &failure);
        });
        if (!setup.Dispatch(name, std::move(task)) || !setup.WaitForCompletion())
            return false;
        rendering::CompiledExecutionPacketView packet;
        if (!allocator.PacketFor(rendering::RenderFlowNodeId{graph.GetNode(0)}, packet, &failure))
            return false;
        lists.Reset();
        const rendering::CommandScopeExecutionReceipt scope{packet.GetCommandScope(), rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
        rendering::TerminalExecutionReceipt receipt;
        receipt.generation = allocator.GetExecutionGeneration();
        receipt.join = rendering::TerminalJoinToken::CompletedSynchronously(receipt.generation);
        receipt.completion = rendering::TerminalExecutionCompletionKind::Aborted;
        receipt.commandScopes = {&scope, 1};
        return allocator.Finish(receipt, &failure) && scheduled && completed && valid && order == 4;
    }

    [[nodiscard]] bool RunPlacedResourceExecutionSmokeTest() noexcept
    {
        if (!gpu::GetCapabilities().placedResources.buffers.IsSupported() || !gpu::GetCapabilities().placedResources.sameQueueGraphics)
            return true;

        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-placed-execution] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        rendering::FrameBufferDesc firstDesc;
        firstDesc.active.size = 4096;
        firstDesc.active.usage = gpu::BufferUsage::UnorderedAccess | gpu::BufferUsage::CopySource;
        firstDesc.active.initialState = gpu::ResourceState::Common;
        firstDesc.maximumSize = firstDesc.active.size;
        firstDesc.initialization = rendering::FrameResourceInitialization::Clear;
        firstDesc.clearValue = rendering::BufferClearValue::Uint(0x11111111u);
        rendering::FrameBufferDesc secondDesc = firstDesc;
        secondDesc.clearValue = rendering::BufferClearValue::Uint(0x22222222u);

        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::BufferUseDesc read;
        read.requiredState = gpu::ResourceState::CopySource;
        read.access = rendering::LogicalAccessIntent::Read;
        read.content = rendering::ResourceContentIntent::Preserve;

        rendering::RenderFlowResourceAllocator allocator;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{79};
        constexpr rendering::GpuFlowGroupId flow{79};
        constexpr rendering::CommandScopeId scope{79};
        constexpr rendering::FlowSpaceId space{79};
        rendering::LogicalResourceId firstResource;
        rendering::LogicalResourceId secondResource;
        rendering::ResourceUseId firstWrite;
        rendering::ResourceUseId secondWrite;
        rendering::ResourceUseId secondRead;
        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(79, {true}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "placed.execution.first"}, firstDesc, firstResource, &failure) || !writer.DeclareBuffer({space, "placed.execution.second"}, secondDesc, secondResource, &failure) ||
            !writer.BeginBufferUse(firstResource, write, firstWrite, &failure) || !writer.EndUse(firstWrite, &failure) || !writer.BeginBufferUse(secondResource, write, secondWrite, &failure) ||
            !writer.EndUse(secondWrite, &failure) || !writer.BeginBufferUse(secondResource, read, secondRead, &failure) || !writer.EndUse(secondRead, &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::RenderFlowResourceAllocatorStats resolvedStats = allocator.GetStats();
        if (resolvedStats.placedHeapPoolMisses != 1 || resolvedStats.placedObjectPoolMisses != 2 || resolvedStats.dedicatedResourcePoolMisses != 0 || resolvedStats.compiledResourceActions != 7 ||
            !allocator.BeginExecution(&failure))
            return fail("placed selection");

        gpu::BufferDesc readbackDesc;
        readbackDesc.size = firstDesc.active.size;
        readbackDesc.usage = gpu::BufferUsage::CopyDestination;
        readbackDesc.initialState = gpu::ResourceState::CopyDestination;
        readbackDesc.memoryType = gpu::MemoryType::Readback;
        gpu::Buffer readback(gpu::AdoptReference, gpu::CreateBuffer(readbackDesc, {}, &rhiFailure));
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x504c41434544584eull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        if (!readback.IsValid() || !commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution setup");
        }

        rendering::ResolvedBufferUse resolvedFirst;
        rendering::ResolvedBufferUse resolvedSecondWrite;
        rendering::ResolvedBufferUse resolvedSecondRead;
        if (!cursor.BeginBufferUse(firstWrite, resolvedFirst, &failure) || !resolvedFirst.GetBuffer().IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("first owner begin");
        }
        const gpu::BufferRef firstNative = resolvedFirst.GetBuffer();
        if (!cursor.EndUse(firstWrite, &failure) || !cursor.BeginBufferUse(secondWrite, resolvedSecondWrite, &failure) || !resolvedSecondWrite.GetBuffer().IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("second owner activation");
        }
        const gpu::BufferRef secondNative = resolvedSecondWrite.GetBuffer();
        if (secondNative == firstNative || !cursor.EndUse(secondWrite, &failure) || !cursor.BeginBufferUse(secondRead, resolvedSecondRead, &failure) || resolvedSecondRead.GetBuffer() != secondNative ||
            !gpu::CopyBuffer(readback.GetRef(), 0, resolvedSecondRead.GetBuffer(), 0, firstDesc.active.size, &rhiFailure) || !cursor.EndUse(secondRead, &failure) || !cursor.FinalizePacket(&failure) ||
            !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("alias execution");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow placed alias execution", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
            return fail("terminal retirement");
        const vanguard::u32* const mapped = static_cast<const vanguard::u32*>(gpu::LockBuffer(readback.GetRef(), 0, firstDesc.active.size, &rhiFailure));
        if (mapped == nullptr)
            return fail("readback lock");
        bool contentsMatch = true;
        for (vanguard::u32 index = 0; index < firstDesc.active.size / sizeof(vanguard::u32); ++index)
            contentsMatch = contentsMatch && mapped[index] == 0x22222222u;
        gpu::UnlockBuffer(readback.GetRef());
        if (!contentsMatch || allocator.GetStats().pendingRetirementPlacedHeaps != 0)
            return fail("alias readback");

        // Copy-queue lifetimes are deliberately outside the V1 placed profile
        // and must continue through the mandatory dedicated provider.
        rendering::FrameBufferDesc copyDesc = firstDesc;
        copyDesc.active.usage = gpu::BufferUsage::CopyDestination;
        copyDesc.initialization = rendering::FrameResourceInitialization::Undefined;
        copyDesc.clearValue = {};
        rendering::BufferUseDesc copyWrite;
        copyWrite.requiredState = gpu::ResourceState::CopyDestination;
        copyWrite.access = rendering::LogicalAccessIntent::Write;
        copyWrite.content = rendering::ResourceContentIntent::Discard;
        rendering::ResourcePlanningWriter copyWriter;
        rendering::LogicalResourceId copyResource;
        rendering::ResourceUseId copyUse;
        constexpr rendering::RenderFlowNodeId copyNode{80};
        constexpr rendering::GpuFlowGroupId copyFlow{80};
        constexpr rendering::CommandScopeId copyScope{80};
        if (!allocator.BeginFrame(80, {true}, &failure) || !allocator.RequestBeginQueue(gpu::QueueType::Copy, copyFlow, &failure) || !allocator.RequestEndQueue(copyFlow, &failure) ||
            !allocator.CreatePlanningWriter(copyNode, copyFlow, copyScope, copyWriter, &failure) ||
            !copyWriter.DeclareBuffer({space, "placed.execution.copy-fallback"}, copyDesc, copyResource, &failure) || !copyWriter.BeginBufferUse(copyResource, copyWrite, copyUse, &failure) ||
            !copyWriter.EndUse(copyUse, &failure) || !copyWriter.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("copy fallback planning");
        if (!allocator.Resolve(nullptr, &failure) ||
            allocator.GetStats().dedicatedResourcePoolMisses != 1 || allocator.GetStats().placedHeapPoolMisses != 1)
            return fail("copy dedicated fallback");
        const rendering::ExecutionGenerationId copyGeneration = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt copyReceipts[] = {{copyScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Copy, {}}};
        const rendering::TerminalExecutionReceipt copyTerminal{copyGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(copyReceipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(copyGeneration)};
        if (!allocator.Finish(copyTerminal, &failure))
            return fail("copy fallback retirement");
        if (!allocator.ClearPersistentCaches(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("persistent cache clear");
        const rendering::RenderFlowResourceAllocatorStats clearedStats = allocator.GetStats();
        return clearedStats.chargedNativeBytes == 0 && clearedStats.pendingNativeDestructionPlacedHeaps == 0 && clearedStats.pendingNativeDestructionResources == 0 && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunDedicatedResourceFailureAndBudgetTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-provider-failure] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        const rendering::FrameResourceDesc physicalDesc = rendering::FrameResourceDesc::Buffer(bufferDesc);
        gpu::BufferDesc queriedDesc = bufferDesc.active;
        queriedDesc.size = bufferDesc.maximumSize;
        gpu::MemoryRequirements requirements;
        if (!gpu::GetMemoryRequirements(queriedDesc, requirements, &rhiFailure) || requirements.size == 0)
            return fail("memory requirements");

        constexpr rendering::detail::DedicatedResourceProviderFailurePoint failurePoints[] = {
            rendering::detail::DedicatedResourceProviderFailurePoint::RequirementQuery,
            rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation,
            rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeDescriptor,
            rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeRequirements,
            rendering::detail::DedicatedResourceProviderFailurePoint::PoolCommit,
        };
        for (const rendering::detail::DedicatedResourceProviderFailurePoint point : failurePoints)
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment assignment;
            pool.SetProviderFailureInjection({point, 0,
                                              point == rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation ? rendering::RenderFlowResourceFailureCode::NativeOutOfMemory
                                                                                                                                : rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure});
            const bool acquired = pool.Acquire(physicalDesc, 1, assignment, &failure);
            pool.ClearProviderFailureInjection();
            const rendering::RenderFlowResourceFailureCode expected = point == rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation
                                                                          ? rendering::RenderFlowResourceFailureCode::NativeOutOfMemory
                                                                          : rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
            const bool createdBeforeFailure = point == rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeDescriptor ||
                                              point == rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeRequirements ||
                                              point == rendering::detail::DedicatedResourceProviderFailurePoint::PoolCommit;
            const rendering::detail::DedicatedResourcePoolStats failedStats = pool.GetStats();
            if (acquired || assignment.IsValid() || failure.code != expected || failedStats.chargedBytes != (createdBeforeFailure ? requirements.size : 0) ||
                failedStats.pendingNativeDestruction != (createdBeforeFailure ? 1 : 0))
                return fail("private boundary injection");
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("injected-resource release");
            pool.Poll();
            if (pool.GetStats().chargedBytes != 0)
                return fail("post-create failure ledger release");
        }

        // A reusable entry is eligible only while the pool is its sole owner.
        // A leaked strong reference blocks both compatible reuse and eviction.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(physicalDesc, 10, first, &failure))
                return fail("hard-budget first acquire");
            gpu::Buffer retained(first.buffer);
            pool.Rollback(first.entry);

            rendering::detail::DedicatedResourceAssignment compatibleBlocked;
            if (pool.Acquire(physicalDesc, 11, compatibleBlocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("strong-owner compatible reuse guard");

            rendering::FrameBufferDesc incompatibleDesc = bufferDesc;
            incompatibleDesc.active.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination;
            rendering::detail::DedicatedResourceAssignment blocked;
            if (pool.Acquire(rendering::FrameResourceDesc::Buffer(incompatibleDesc), 12, blocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("strong-owner eviction guard");
            const rendering::detail::DedicatedResourcePoolStats pending = pool.GetStats();
            if (pending.chargedBytes != requirements.size || pending.pendingNativeDestruction != 0)
                return fail("strong-owner charge accounting");
            retained.Reset();
            if (!pool.Acquire(physicalDesc, 13, compatibleBlocked, &failure) || compatibleBlocked.entry != first.entry)
                return fail("post-owner-release compatible reuse");
            pool.Rollback(compatibleBlocked.entry);
        }

        // Soft trimming uses the same observation-backed ledger as pressure
        // eviction; trimming alone is never evidence of native destruction.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.bufferSoftTargetBytes = 0;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(physicalDesc, 20, assignment, &failure))
                return fail("soft-trim acquire");
            gpu::Buffer retained(assignment.buffer);
            pool.Rollback(assignment.entry);
            if (!pool.TrimToSoftTargets(&failure))
                return fail("soft-trim request");
            const rendering::detail::DedicatedResourcePoolStats pinned = pool.GetStats();
            if (pinned.chargedBytes != requirements.size || pinned.pendingNativeDestruction != 0)
                return fail("soft-trim strong-owner guard");
            retained.Reset();
            if (!pool.TrimToSoftTargets(&failure))
                return fail("soft-trim post-owner request");
            const rendering::detail::DedicatedResourcePoolStats pending = pool.GetStats();
            if (pending.chargedBytes != requirements.size || pending.pendingNativeDestruction != 1)
                return fail("soft-trim pending accounting");
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("soft-trim native release");
            pool.Poll();
            if (pool.GetStats().chargedBytes != 0)
                return fail("soft-trim ledger release");
        }

        // Transfer is legal only with the pool owner plus the one prepared
        // publication owner. A third owner has escaped the scoped use domain.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(physicalDesc, 24, assignment, &failure))
                return fail("transfer ownership acquire");
            gpu::Buffer publication(assignment.buffer);
            gpu::Buffer escaped(assignment.buffer);
            const vanguard::u32 entries[] = {assignment.entry};
            if (pool.TransferOwnership({entries, 1}, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation || pool.GetStats().chargedBytes != requirements.size)
                return fail("transfer escaped-owner guard");
            escaped.Reset();
            if (!pool.TransferOwnership({entries, 1}, &failure) || pool.GetStats().chargedBytes != 0 || !publication.IsValid())
                return fail("transfer exact publication ownership");
        }

        // A failure to obtain native-release evidence is a provider failure,
        // not budget exhaustion. The reusable entry remains owned and charged.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(physicalDesc, 24, assignment, &failure))
                return fail("observation failure acquire");
            pool.Rollback(assignment.entry);
            rendering::FrameBufferDesc incompatibleDesc = bufferDesc;
            incompatibleDesc.active.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination;
            pool.SetProviderFailureInjection({rendering::detail::DedicatedResourceProviderFailurePoint::NativeReleaseObservation, 0, rendering::RenderFlowResourceFailureCode::BackendContractViolation});
            rendering::detail::DedicatedResourceAssignment blocked;
            const bool acquired = pool.Acquire(rendering::FrameResourceDesc::Buffer(incompatibleDesc), 25, blocked, &failure);
            pool.ClearProviderFailureInjection();
            if (acquired || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation || pool.GetStats().chargedBytes != requirements.size ||
                pool.GetStats().pendingNativeDestruction != 0)
                return fail("observation failure fidelity");
            if (!pool.Acquire(physicalDesc, 26, assignment, &failure) || assignment.entry == rendering::detail::InvalidDedicatedResourceEntry)
                return fail("observation failure preserves reusable ownership");
            pool.Rollback(assignment.entry);
        }

        // The end-to-end provider smoke test retires from a real submission
        // receipt. This deterministic pool check holds one fence incomplete so
        // neither reuse nor budget admission can cross PendingRetirement.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(physicalDesc, 25, assignment, &failure))
                return fail("pending-retirement acquire");
            gpu::ResidencyFenceSet incomplete;
            incomplete.graphics = ~vanguard::u64{0};
            pool.Retire(assignment.entry, incomplete);
            rendering::detail::DedicatedResourceAssignment blocked;
            if (pool.GetStats().pendingRetirement != 1 || pool.Acquire(physicalDesc, 26, blocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded ||
                pool.GetStats().chargedBytes != requirements.size)
                return fail("pending-retirement reuse guard");
            pool.DeviceLost();
            if (pool.GetStats().chargedBytes != 0 || pool.GetStats().pendingRetirement != 0)
                return fail("pending-retirement device-loss cleanup");
        }

        const auto resolveBuffers = [&bufferDesc](rendering::RenderFlowResourceAllocator& allocator, const vanguard::u64 frameSerial, const vanguard::u32 count,
                                                  rendering::RenderFlowResourceFailure& outputFailure) noexcept
        {
            constexpr rendering::RenderFlowNodeId node{70};
            constexpr rendering::GpuFlowGroupId flow{70};
            constexpr rendering::CommandScopeId scope{70};
            constexpr rendering::FlowSpaceId space{70};
            constexpr const char* names[] = {"provider.failure.a", "provider.failure.b"};
            rendering::ResourcePlanningWriter writer;
            if (!allocator.BeginFrame(frameSerial, {}, &outputFailure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &outputFailure))
                return false;
            rendering::BufferUseDesc useDesc;
            useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
            useDesc.access = rendering::LogicalAccessIntent::Write;
            useDesc.content = rendering::ResourceContentIntent::Discard;
            rendering::LogicalResourceId resources[2];
            rendering::ResourceUseId uses[2];
            for (vanguard::u32 index = 0; index < count; ++index)
                if (!writer.DeclareBuffer({space, names[index]}, bufferDesc, resources[index], &outputFailure))
                    return false;
            // These fixtures must force distinct acquisitions. Sequential uses
            // are now expected to share one dedicated resource within the frame.
            for (vanguard::u32 index = 0; index < count; ++index)
                if (!writer.BeginBufferUse(resources[index], useDesc, uses[index], &outputFailure))
                    return false;
            for (vanguard::u32 index = 0; index < count; ++index)
                if (!writer.EndUse(uses[index], &outputFailure))
                    return false;
            if (!writer.Close(&outputFailure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &outputFailure))
                return false;
            return allocator.Resolve(nullptr, &outputFailure);
        };

        const auto abortResolved = [](rendering::RenderFlowResourceAllocator& allocator, rendering::RenderFlowResourceFailure& outputFailure) noexcept
        {
            constexpr rendering::CommandScopeId scope{70};
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt scopes[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt receipt{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                              vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(scopes),
                                                              rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            return allocator.Finish(receipt, &outputFailure);
        };

        // Stop the second acquisition at every provider boundary. Resolve must
        // publish no generation, return the first assignment to Reusable, and
        // keep any already-created failed native object charged until release.
        for (const rendering::detail::DedicatedResourceProviderFailurePoint point : failurePoints)
        {
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure))
                return fail("rollback allocator init");
            rendering::detail::DedicatedResourceProviderTestAccess::Set(allocator, {point, 1, rendering::RenderFlowResourceFailureCode::NativeOutOfMemory});
            if (resolveBuffers(allocator, 30, 2, failure) || allocator.GetExecutionGeneration().IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::NativeOutOfMemory)
                return fail("scratch publication injection");
            rendering::detail::DedicatedResourceProviderTestAccess::Clear(allocator);
            const bool createdBeforeFailure = point == rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeDescriptor ||
                                              point == rendering::detail::DedicatedResourceProviderFailurePoint::AuthoritativeRequirements ||
                                              point == rendering::detail::DedicatedResourceProviderFailurePoint::PoolCommit;
            const rendering::RenderFlowResourceAllocatorStats rolledBack = allocator.GetStats();
            if (rolledBack.state != rendering::RenderFlowResourceSessionState::Idle || rolledBack.chargedNativeBytes != requirements.size * (createdBeforeFailure ? 2u : 1u) ||
                rolledBack.pendingNativeDestructionResources != (createdBeforeFailure ? 1u : 0u) || rolledBack.dedicatedResourcePoolMisses != 1)
                return fail("scratch rollback accounting");
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("scratch rollback failed-native release");
            const rendering::RenderFlowResourceAllocatorStats released = allocator.GetStats();
            if (released.chargedNativeBytes != requirements.size || released.pendingNativeDestructionResources != 0)
                return fail("scratch rollback failed-native ledger release");
            if (!resolveBuffers(allocator, 31, 1, failure) || allocator.GetStats().dedicatedResourcePoolHits != 1 || !abortResolved(allocator, failure) ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("rolled-back assignment reuse");
        }

        // The same rollback path must preserve an admitted first object when a
        // second assignment cannot fit the one-object hard budget.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize(config, &failure))
                return fail("budget allocator init");
            if (resolveBuffers(allocator, 40, 2, failure) || allocator.GetExecutionGeneration().IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("hard-budget resolve rejection");
            const rendering::RenderFlowResourceAllocatorStats rejected = allocator.GetStats();
            if (rejected.state != rendering::RenderFlowResourceSessionState::Idle || rejected.chargedNativeBytes != requirements.size || rejected.dedicatedResourcePoolMisses != 1)
                return fail("hard-budget rollback accounting");
            if (!resolveBuffers(allocator, 41, 1, failure) || allocator.GetStats().dedicatedResourcePoolHits != 1 || !abortResolved(allocator, failure) ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("hard-budget rollback reuse");
        }

        // Device loss is a distinct terminal path: no poisoned-fence wait,
        // every pool charge is invalidated once, and the allocator stays
        // unavailable until it is recreated.
        {
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure) || !resolveBuffers(allocator, 50, 1, failure))
                return fail("device-loss setup");
            constexpr rendering::CommandScopeId scope{70};
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt scopes[] = {{scope, rendering::CommandScopeCompletionKind::UnknownDueToDeviceLoss, gpu::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt receipt{generation, rendering::TerminalExecutionCompletionKind::DeviceLost,
                                                              vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(scopes),
                                                              rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            if (!allocator.Finish(receipt, &failure))
                return fail("device-loss finish");
            const rendering::RenderFlowResourceAllocatorStats lost = allocator.GetStats();
            if (lost.state != rendering::RenderFlowResourceSessionState::DeviceUnavailable || lost.chargedNativeBytes != 0 || lost.abortedFrames != 1 || !allocator.Shutdown(&failure) ||
                !gpu::FlushRetiredResources(&rhiFailure))
                return fail("device-loss cleanup");
        }

        return true;
    }

    [[nodiscard]] bool RunTypedClearSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-clear] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        rendering::RenderFlowResourceAllocator allocator;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{2};
        constexpr rendering::GpuFlowGroupId flow{2};
        constexpr rendering::CommandScopeId scope{2};
        constexpr rendering::FlowSpaceId space{2};
        rendering::LogicalResourceId resources[5];
        rendering::ResourceUseId uses[5];

        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        bufferDesc.initialization = rendering::FrameResourceInitialization::Clear;
        bufferDesc.clearValue = rendering::BufferClearValue::Uint(0x10203040u);
        rendering::BufferUseDesc bufferUse;
        bufferUse.requiredState = gpu::ResourceState::UnorderedAccess;
        bufferUse.access = rendering::LogicalAccessIntent::Write;
        bufferUse.content = rendering::ResourceContentIntent::Discard;

        rendering::FrameTextureDesc floatDesc;
        floatDesc.active.extent = {8, 8, 1};
        floatDesc.active.dimension = gpu::TextureDimension::Texture2D;
        floatDesc.active.format = gpu::Format::R8G8B8A8UNorm;
        floatDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        floatDesc.active.initialState = gpu::ResourceState::Common;
        floatDesc.maximumExtent = floatDesc.active.extent;
        floatDesc.maximumMipCount = 1;
        rendering::TextureUseDesc floatUse;
        floatUse.requiredState = gpu::ResourceState::UnorderedAccess;
        floatUse.subresources = {0, 1, 0, 1};
        floatUse.access = rendering::LogicalAccessIntent::Write;
        floatUse.content = rendering::ResourceContentIntent::Clear;
        floatUse.clearValue = rendering::TextureClearValue::Color({0.25f, 0.5f, 0.75f, 1.0f});

        rendering::FrameTextureDesc uintDesc = floatDesc;
        uintDesc.active.format = gpu::Format::R32UInt;
        rendering::TextureUseDesc uintUse = floatUse;
        uintUse.clearValue = rendering::TextureClearValue::Uint(0x50607080u);

        rendering::FrameTextureDesc colorDesc = floatDesc;
        colorDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::RenderTarget;
        colorDesc.initialization = rendering::FrameResourceInitialization::Clear;
        colorDesc.clearValue = rendering::TextureClearValue::Color({0.1f, 0.2f, 0.3f, 1.0f});
        rendering::TextureUseDesc colorUse;
        colorUse.requiredState = gpu::ResourceState::RenderTarget;
        colorUse.subresources = {0, 1, 0, 1};
        colorUse.access = rendering::LogicalAccessIntent::Write;
        colorUse.content = rendering::ResourceContentIntent::Discard;

        rendering::FrameTextureDesc depthDesc = floatDesc;
        depthDesc.active.format = gpu::Format::D32FloatS8UInt;
        depthDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::DepthStencil;
        rendering::TextureUseDesc depthUse;
        depthUse.requiredState = gpu::ResourceState::DepthWrite;
        depthUse.subresources = {0, 1, 0, 1};
        depthUse.access = rendering::LogicalAccessIntent::Write;
        depthUse.content = rendering::ResourceContentIntent::Clear;
        depthUse.clearValue = rendering::TextureClearValue::DepthStencil(0.0f, 5);

        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(10, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "native.clear.buffer"}, bufferDesc, resources[0], &failure) || !writer.BeginBufferUse(resources[0], bufferUse, uses[0], &failure) ||
            !writer.EndUse(uses[0], &failure) || !writer.DeclareTexture({space, "native.clear.float"}, floatDesc, resources[1], &failure) || !writer.BeginTextureUse(resources[1], floatUse, uses[1], &failure) ||
            !writer.EndUse(uses[1], &failure) || !writer.DeclareTexture({space, "native.clear.uint"}, uintDesc, resources[2], &failure) || !writer.BeginTextureUse(resources[2], uintUse, uses[2], &failure) ||
            !writer.EndUse(uses[2], &failure) || !writer.DeclareTexture({space, "native.clear.color-target"}, colorDesc, resources[3], &failure) ||
            !writer.BeginTextureUse(resources[3], colorUse, uses[3], &failure) || !writer.EndUse(uses[3], &failure) ||
            !writer.DeclareTexture({space, "native.clear.depth-stencil"}, depthDesc, resources[4], &failure) || !writer.BeginTextureUse(resources[4], depthUse, uses[4], &failure) ||
            !writer.EndUse(uses[4], &failure) || !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().compiledResourceActions != 10 || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246434c45415253ull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        if (!commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution setup");
        }
        rendering::ResolvedBufferUse resolvedBuffer;
        if (!cursor.BeginBufferUse(uses[0], resolvedBuffer, &failure) || !resolvedBuffer.GetBuffer().IsValid() || !cursor.EndUse(uses[0], &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("buffer clear recording");
        }
        for (vanguard::u32 index = 1; index < 5; ++index)
        {
            rendering::ResolvedTextureUse resolvedTexture;
            if (!cursor.BeginTextureUse(uses[index], resolvedTexture, &failure) || !resolvedTexture.GetTexture().IsValid() || !cursor.EndUse(uses[index], &failure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return fail("texture clear recording");
            }
        }
        if (!cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("packet finalization");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow typed clears", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        return allocator.Finish(terminal, &failure) && gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunRetainedImportSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        gpu::BufferDesc bufferDesc;
        bufferDesc.size = 2048;
        bufferDesc.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::CopySource;
        bufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer externalBuffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &rhiFailure));
        gpu::TextureDesc textureDesc;
        textureDesc.extent = {4, 4, 1};
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource;
        textureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture externalTexture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {}, &rhiFailure));
        if (!externalBuffer.IsValid() || !externalTexture.IsValid())
        {
            std::fprintf(stderr, "[retained-import] external creation failed: %s\n", rhiFailure.message[0] != '\0' ? rhiFailure.message : "unknown RHI failure");
            return false;
        }
        const vanguard::i32 initialBufferRefs = gpu::GetRefCount(externalBuffer.GetRef());
        const vanguard::i32 initialTextureRefs = gpu::GetRefCount(externalTexture.GetRef());
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[retained-import] %s failed (flow=%u: %s, rhi=%u: %s, refs=%d/%d expected=%d/%d)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none",
                         gpu::GetRefCount(externalBuffer.GetRef()), gpu::GetRefCount(externalTexture.GetRef()), initialBufferRefs, initialTextureRefs);
            return false;
        };

        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(20, {}, &failure))
            return fail("allocator setup");

        rendering::RetainedBufferImportDesc bufferImport;
        bufferImport.token = {0x425546464552494dull};
        bufferImport.buffer = externalBuffer.GetRef();
        bufferImport.expected = bufferDesc;
        bufferImport.initialState = gpu::ResourceState::Common;
        bufferImport.terminalState = gpu::ResourceState::Common;
        rendering::RetainedTextureImportDesc textureImport;
        textureImport.token = {0x544558545552494dull};
        textureImport.texture = externalTexture.GetRef();
        textureImport.expected = textureDesc;
        textureImport.initialState = gpu::ResourceState::Common;
        textureImport.terminalState = gpu::ResourceState::Common;

        rendering::ImportedResourceId importedBuffer;
        rendering::ImportedResourceId importedBufferAgain;
        rendering::ImportedResourceId importedTexture;
        rendering::ImportedResourceId rejectedImport;
        rendering::RetainedBufferImportDesc mismatchedBuffer = bufferImport;
        ++mismatchedBuffer.expected.size;
        rendering::RetainedBufferImportDesc explicitWaitBuffer = bufferImport;
        explicitWaitBuffer.readiness = rendering::ImportReadinessKind::ExplicitFenceWait;
        rendering::RetainedBufferImportDesc transferBuffer = bufferImport;
        transferBuffer.terminalQueue = gpu::QueueType::Copy;
        if (allocator.RegisterImport(mismatchedBuffer, importedBuffer, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict ||
            allocator.RegisterImport(explicitWaitBuffer, rejectedImport, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::UnsupportedCapability ||
            allocator.RegisterImport(transferBuffer, rejectedImport, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch ||
            !allocator.RegisterImport(bufferImport, importedBuffer, &failure) || !allocator.RegisterImport(bufferImport, importedBufferAgain, &failure) || importedBuffer != importedBufferAgain ||
            !allocator.RegisterImport(textureImport, importedTexture, &failure) || gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs + 1 ||
            gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs + 1)
            return fail("registration");
        rendering::RetainedBufferImportDesc duplicatePhysicalBuffer = bufferImport;
        duplicatePhysicalBuffer.token.value ^= 1u;
        if (allocator.RegisterImport(duplicatePhysicalBuffer, rejectedImport, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict)
            return fail("duplicate physical registration");

        constexpr rendering::RenderFlowNodeId node{3};
        constexpr rendering::GpuFlowGroupId flow{3};
        constexpr rendering::CommandScopeId scope{3};
        constexpr rendering::FlowSpaceId space{3};
        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId logicalBuffer;
        rendering::LogicalResourceId logicalTexture;
        rendering::ResourceUseId bufferUse;
        rendering::ResourceUseId textureUse;
        rendering::BufferUseDesc bufferRead;
        bufferRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        bufferRead.access = rendering::LogicalAccessIntent::Read;
        bufferRead.content = rendering::ResourceContentIntent::Preserve;
        rendering::TextureUseDesc textureRead;
        textureRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        textureRead.access = rendering::LogicalAccessIntent::Read;
        textureRead.content = rendering::ResourceContentIntent::Preserve;
        if (!allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) || !writer.ImportBuffer({space, "retained.import.buffer"}, importedBuffer, logicalBuffer, &failure) ||
            !writer.BeginBufferUse(logicalBuffer, bufferRead, bufferUse, &failure) || !writer.EndUse(bufferUse, &failure) ||
            !writer.ImportTexture({space, "retained.import.texture"}, importedTexture, logicalTexture, &failure) || !writer.BeginTextureUse(logicalTexture, textureRead, textureUse, &failure) ||
            !writer.EndUse(textureUse, &failure) || !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().retainedImports != 2 || gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs + 1 ||
            gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs + 1 || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolvedBuffer;
        rendering::ResolvedTextureUse resolvedTexture;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246494d504f5254ull, &rhiFailure);
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) ||
            !cursor.BeginBufferUse(bufferUse, resolvedBuffer, &failure) || resolvedBuffer.GetBuffer() != externalBuffer.GetRef() || !cursor.EndUse(bufferUse, &failure) ||
            !cursor.BeginTextureUse(textureUse, resolvedTexture, &failure) || resolvedTexture.GetTexture() != externalTexture.GetRef() || !cursor.EndUse(textureUse, &failure) ||
            !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("packet execution");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow retained imports", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
            return fail("terminal completion");

        const vanguard::i32 bufferRefsBeforeGenerationRelease = gpu::GetRefCount(externalBuffer.GetRef());
        const vanguard::i32 textureRefsBeforeGenerationRelease = gpu::GetRefCount(externalTexture.GetRef());
        packet = {};
        cursor = {};
        // Submitted command-list tracking may continue to retain the resources
        // after its fence completes. Prove only the ownership that belongs to
        // the compiled execution generation: releasing its retained views drops exactly one.
        if (gpu::GetRefCount(externalBuffer.GetRef()) != bufferRefsBeforeGenerationRelease - 1 || gpu::GetRefCount(externalTexture.GetRef()) != textureRefsBeforeGenerationRelease - 1)
            return fail("retained-owner release");

        // A concrete Common before-state assertion proves the execution generation restored
        // the registered terminal contract before returning same-queue access.
        gpu::CommandListRef verify = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246494d56455249ull, &rhiFailure);
        if (!verify || !gpu::BindCommandList(verify, &rhiFailure) || !gpu::TransitionBuffer(externalBuffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, &rhiFailure) ||
            !gpu::TransitionTexture(externalTexture.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, {}, &rhiFailure) ||
            !gpu::TransitionBuffer(externalBuffer.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, &rhiFailure) ||
            !gpu::TransitionTexture(externalTexture.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, {}, &rhiFailure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(verify);
            return fail("terminal-state verification recording");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef verifySubmissions[] = {verify};
        gpu::GpuFence verifyCompletion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow retained import terminal state", {verifySubmissions, 1}, gpu::CommandListSyncType::None, verifyCompletion, &rhiFailure) ||
            !gpu::WaitForGpuFence(verifyCompletion, 5'000'000'000ull, &rhiFailure) || allocator.GetStats().chargedNativeBytes != 0)
            return fail("terminal-state verification submission");

        // A declared import with no executable use cannot silently promise a
        // different terminal state: there is no legal recorder on which to
        // place that transition.
        const vanguard::i32 bufferRefsBeforeRejectedFrame = gpu::GetRefCount(externalBuffer.GetRef());
        rendering::RetainedBufferImportDesc noUseTransition = bufferImport;
        noUseTransition.terminalState = gpu::ResourceState::CopySource;
        rendering::ImportedResourceId rejectedNoUseImport;
        rendering::ResourcePlanningWriter rejectedWriter;
        rendering::LogicalResourceId rejectedLogical;
        constexpr rendering::RenderFlowNodeId rejectedNode{4};
        constexpr rendering::GpuFlowGroupId rejectedFlow{4};
        constexpr rendering::CommandScopeId rejectedScope{4};
        if (!allocator.BeginFrame(21, {}, &failure) || !allocator.RegisterImport(noUseTransition, rejectedNoUseImport, &failure) ||
            !allocator.CreatePlanningWriter(rejectedNode, rejectedFlow, rejectedScope, rejectedWriter, &failure) ||
            !rejectedWriter.ImportBuffer({space, "retained.import.no-use"}, rejectedNoUseImport, rejectedLogical, &failure) || !rejectedWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || allocator.Resolve(nullptr, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::IncompleteExecution || allocator.GetExecutionGeneration().IsValid() ||
            gpu::GetRefCount(externalBuffer.GetRef()) != bufferRefsBeforeRejectedFrame)
            return fail("unused import terminal contract");

        return allocator.GetStats().chargedNativeBytes == 0 && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunTerminalExportSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[terminal-export] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        gpu::BufferDesc externalDesc;
        externalDesc.size = 1024;
        externalDesc.usage = gpu::BufferUsage::ShaderResource;
        externalDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer external(gpu::AdoptReference, gpu::CreateBuffer(externalDesc, {}, &rhiFailure));
        if (!external.IsValid())
            return fail("external creation");

        rendering::RenderFlowResourceAllocator allocator;
        rendering::ExportSlotId bufferSlot;
        rendering::ExportSlotId textureSlot;
        rendering::ExportSlotId importedSlot;
        if (!allocator.Initialize({}, &failure) || !allocator.BeginFrame(30, {}, &failure) || !allocator.ReserveExportSlot(bufferSlot, &failure) ||
            !allocator.ReserveExportSlot(textureSlot, &failure) || !allocator.ReserveExportSlot(importedSlot, &failure))
            return fail("allocator and export-slot setup");

        rendering::RetainedBufferImportDesc importDesc;
        importDesc.token = {0x455850494d504f52ull};
        importDesc.buffer = external.GetRef();
        importDesc.expected = externalDesc;
        importDesc.initialState = gpu::ResourceState::Common;
        importDesc.terminalState = gpu::ResourceState::Common;
        rendering::ImportedResourceId imported;
        if (!allocator.RegisterImport(importDesc, imported, &failure))
            return fail("import registration");

        constexpr rendering::RenderFlowNodeId node{5};
        constexpr rendering::GpuFlowGroupId flow{5};
        constexpr rendering::CommandScopeId scope{5};
        constexpr rendering::FlowSpaceId space{5};
        rendering::ResourcePlanningWriter writer;
        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::CopySource;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        rendering::FrameTextureDesc textureDesc;
        textureDesc.active.extent = {8, 8, 1};
        textureDesc.active.dimension = gpu::TextureDimension::Texture2D;
        textureDesc.active.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.active.usage = gpu::TextureUsage::CopyDestination | gpu::TextureUsage::CopySource;
        textureDesc.active.initialState = gpu::ResourceState::Common;
        textureDesc.maximumExtent = textureDesc.active.extent;
        textureDesc.maximumMipCount = 1;
        rendering::LogicalResourceId buffer;
        rendering::LogicalResourceId texture;
        rendering::LogicalResourceId importedBuffer;
        rendering::ResourceUseId bufferUse;
        rendering::ResourceUseId textureUse;
        rendering::ResourceUseId importedUse;
        rendering::BufferUseDesc bufferWrite;
        bufferWrite.requiredState = gpu::ResourceState::CopyDestination;
        bufferWrite.access = rendering::LogicalAccessIntent::Write;
        bufferWrite.content = rendering::ResourceContentIntent::Discard;
        rendering::TextureUseDesc textureWrite;
        textureWrite.requiredState = gpu::ResourceState::CopyDestination;
        textureWrite.access = rendering::LogicalAccessIntent::Write;
        textureWrite.content = rendering::ResourceContentIntent::Discard;
        rendering::BufferUseDesc importedRead;
        importedRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        importedRead.access = rendering::LogicalAccessIntent::Read;
        importedRead.content = rendering::ResourceContentIntent::Preserve;
        rendering::TerminalResourceExportDesc terminal;
        terminal.terminalState = gpu::ResourceState::Common;
        terminal.terminalQueue = gpu::QueueType::Graphics;
        terminal.readiness = rendering::ExportReadinessKind::ExplicitFenceSignal;
        if (!allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) || !writer.DeclareBuffer({space, "terminal.export.buffer"}, bufferDesc, buffer, &failure) ||
            !writer.BeginBufferUse(buffer, bufferWrite, bufferUse, &failure) || !writer.EndUse(bufferUse, &failure) || !writer.RequestExport(buffer, bufferSlot, terminal, &failure) ||
            !writer.DeclareTexture({space, "terminal.export.texture"}, textureDesc, texture, &failure) || !writer.BeginTextureUse(texture, textureWrite, textureUse, &failure) ||
            !writer.EndUse(textureUse, &failure) || !writer.RequestExport(texture, textureSlot, terminal, &failure) ||
            !writer.ImportBuffer({space, "terminal.export.imported"}, imported, importedBuffer, &failure) || !writer.BeginBufferUse(importedBuffer, importedRead, importedUse, &failure) ||
            !writer.EndUse(importedUse, &failure) || !writer.RequestExport(importedBuffer, importedSlot, terminal, &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().chargedNativeBytes == 0 || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        rendering::PublishedResourceExport tooEarly;
        if (allocator.TakeExport(bufferSlot, tooEarly, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidPhase)
            return fail("pre-terminal publication guard");

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolvedBuffer;
        rendering::ResolvedTextureUse resolvedTexture;
        rendering::ResolvedBufferUse resolvedImported;
        gpu::BufferRef physicalBuffer;
        gpu::TextureRef physicalTexture;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x4558504f5254534dull, &rhiFailure);
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) ||
            !cursor.BeginBufferUse(bufferUse, resolvedBuffer, &failure))
            return fail("execution setup");
        physicalBuffer = resolvedBuffer.GetBuffer();
        if (!physicalBuffer.IsValid() || !cursor.EndUse(bufferUse, &failure) || !cursor.BeginTextureUse(textureUse, resolvedTexture, &failure))
            return fail("buffer execution");
        physicalTexture = resolvedTexture.GetTexture();
        if (!physicalTexture.IsValid() || !cursor.EndUse(textureUse, &failure) || !cursor.BeginBufferUse(importedUse, resolvedImported, &failure) || resolvedImported.GetBuffer() != external.GetRef() ||
            !cursor.EndUse(importedUse, &failure) || !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            return fail("texture/import execution");
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow terminal exports", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt receipt{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(receipt, &failure) || allocator.GetStats().chargedNativeBytes != 0 || allocator.GetStats().outstandingPublishedExports != 3)
            return fail("atomic publication");

        rendering::PublishedResourceExport exportedBuffer;
        rendering::PublishedResourceExport exportedTexture;
        rendering::PublishedResourceExport exportedImported;
        if (!allocator.TakeExport(bufferSlot, exportedBuffer, &failure) || !allocator.TakeExport(textureSlot, exportedTexture, &failure) || !allocator.TakeExport(importedSlot, exportedImported, &failure) ||
            exportedBuffer.GetBuffer() != physicalBuffer || exportedTexture.GetTexture() != physicalTexture || exportedImported.GetBuffer() != external.GetRef() ||
            exportedBuffer.GetTerminalState() != gpu::ResourceState::Common || exportedTexture.GetTerminalQueue() != gpu::QueueType::Graphics ||
            exportedBuffer.GetReadiness() != rendering::ExportReadinessKind::ExplicitFenceSignal || exportedBuffer.GetReadyFence() != completion || exportedTexture.GetReadyFence() != completion ||
            exportedImported.GetReadyFence() != completion || exportedBuffer.GetBufferDesc().size != bufferDesc.active.size || exportedTexture.GetTextureDesc().extent.width != textureDesc.active.extent.width ||
            allocator.GetStats().outstandingPublishedExports != 0)
            return fail("published owners");
        rendering::PublishedResourceExport duplicateTake;
        if (allocator.TakeExport(bufferSlot, duplicateTake, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity)
            return fail("single-consumer publication");

        // Aborted execution must retire its allocator-owned resource normally
        // and publish no owner.
        rendering::ExportSlotId abortedSlot;
        rendering::ResourcePlanningWriter abortedWriter;
        rendering::LogicalResourceId abortedBuffer;
        rendering::ResourceUseId abortedUse;
        constexpr rendering::RenderFlowNodeId abortedNode{6};
        constexpr rendering::GpuFlowGroupId abortedFlow{6};
        constexpr rendering::CommandScopeId abortedScope{6};
        if (!allocator.BeginFrame(31, {}, &failure) || !allocator.ReserveExportSlot(abortedSlot, &failure) ||
            !allocator.CreatePlanningWriter(abortedNode, abortedFlow, abortedScope, abortedWriter, &failure) ||
            !abortedWriter.DeclareBuffer({space, "terminal.export.abort"}, bufferDesc, abortedBuffer, &failure) || !abortedWriter.BeginBufferUse(abortedBuffer, bufferWrite, abortedUse, &failure) ||
            !abortedWriter.EndUse(abortedUse, &failure) || !abortedWriter.RequestExport(abortedBuffer, abortedSlot, terminal, &failure) || !abortedWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("abort setup");
        const rendering::ExecutionGenerationId abortedGeneration = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt abortedReceipts[] = {{abortedScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt abortedReceipt{abortedGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                 vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(abortedReceipts),
                                                                 rendering::TerminalJoinToken::CompletedSynchronously(abortedGeneration)};
        if (!allocator.Finish(abortedReceipt, &failure) || allocator.GetStats().outstandingPublishedExports != 0 || allocator.TakeExport(abortedSlot, duplicateTake, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity || allocator.GetStats().chargedNativeBytes == 0)
            return fail("abort publication guard");

        rendering::ExportSlotId conflictSlotA;
        rendering::ExportSlotId conflictSlotB;
        rendering::ResourcePlanningWriter conflictWriter;
        rendering::LogicalResourceId conflictBuffer;
        rendering::ResourceUseId conflictUse;
        if (!allocator.BeginFrame(32, {}, &failure) || !allocator.ReserveExportSlot(conflictSlotA, &failure) || !allocator.ReserveExportSlot(conflictSlotB, &failure) ||
            !allocator.CreatePlanningWriter(abortedNode, abortedFlow, abortedScope, conflictWriter, &failure) ||
            !conflictWriter.DeclareBuffer({space, "terminal.export.conflict"}, bufferDesc, conflictBuffer, &failure) || !conflictWriter.BeginBufferUse(conflictBuffer, bufferWrite, conflictUse, &failure) ||
            !conflictWriter.EndUse(conflictUse, &failure) || !conflictWriter.RequestExport(conflictBuffer, conflictSlotA, terminal, &failure) ||
            !conflictWriter.RequestExport(conflictBuffer, conflictSlotB, terminal, &failure) || !conflictWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || allocator.Resolve(nullptr, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidUseOrScope || allocator.GetExecutionGeneration().IsValid())
            return fail("duplicate ownership conflict");

        return gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) && allocator.Shutdown(&failure);
    }

    [[nodiscard]] bool RunCrossQueueForkJoinSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-fork-join] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        rendering::RenderFlowResourceAllocator allocator;
        rendering::ResourcePlanningWriter writers[3];
        constexpr rendering::RenderFlowNodeId nodes[] = {{40}, {41}, {42}};
        constexpr rendering::GpuFlowGroupId flows[] = {{40}, {42}, {44}};
        constexpr rendering::GpuFlowGroupId forkFlow{41};
        constexpr rendering::GpuFlowGroupId joinFlow{43};
        constexpr rendering::CommandScopeId scopes[] = {{40}, {41}, {42}};
        constexpr rendering::FlowSpaceId space{40};
        rendering::LogicalResourceId resources[2];
        rendering::ResourceUseId uses[2];
        rendering::FrameBufferDesc desc;
        desc.active.size = 4096;
        desc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.maximumSize = desc.active.size;
        rendering::BufferUseDesc useDescs[2];
        useDescs[0].requiredState = gpu::ResourceState::UnorderedAccess;
        useDescs[0].access = rendering::LogicalAccessIntent::Write;
        useDescs[0].content = rendering::ResourceContentIntent::Discard;
        useDescs[1].requiredState = gpu::ResourceState::ShaderResourceCompute;
        useDescs[1].access = rendering::LogicalAccessIntent::Read;
        useDescs[1].content = rendering::ResourceContentIntent::Preserve;
        gpu::Buffer external(gpu::AdoptReference, gpu::CreateBuffer(desc.active, {}, &rhiFailure));
        rendering::RetainedBufferImportDesc importDesc;
        importDesc.token = {0x43524f5353515545ull};
        importDesc.buffer = external.GetRef();
        importDesc.expected = desc.active;
        importDesc.initialState = gpu::ResourceState::Common;
        importDesc.terminalState = gpu::ResourceState::Common;
        importDesc.initialQueue = gpu::QueueType::Graphics;
        importDesc.terminalQueue = gpu::QueueType::Compute;
        rendering::ImportedResourceId imported;

        if (!external.IsValid() || !allocator.Initialize({}, &failure) || !allocator.BeginFrame(40, {}, &failure) || !allocator.RegisterImport(importDesc, imported, &failure) ||
            !allocator.RequestQueueSync(forkFlow, gpu::CommandListSyncType::ForkAsyncCompute, &failure) || !allocator.RequestBeginQueue(gpu::QueueType::Compute, flows[1], &failure) ||
            !allocator.RequestEndQueue(flows[1], &failure) || !allocator.RequestQueueSync(joinFlow, gpu::CommandListSyncType::JoinAsyncCompute, &failure) ||
            !allocator.CreatePlanningWriter(nodes[0], flows[0], scopes[0], writers[0], &failure) || !allocator.CreatePlanningWriter(nodes[1], flows[1], scopes[1], writers[1], &failure) ||
            !allocator.CreatePlanningWriter(nodes[2], flows[2], scopes[2], writers[2], &failure) || !writers[0].ImportBuffer({space, "native.cross.queue"}, imported, resources[0], &failure) ||
            !writers[0].BeginBufferUse(resources[0], useDescs[0], uses[0], &failure) || !writers[0].EndUse(uses[0], &failure) ||
            !writers[1].ReferenceResource({space, "native.cross.queue"}, resources[1], &failure) || !writers[1].BeginBufferUse(resources[1], useDescs[1], uses[1], &failure) ||
            !writers[1].EndUse(uses[1], &failure) || !writers[0].Close(&failure) || !writers[1].Close(&failure) || !writers[2].Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        gpu::CommandListRef commandLists[3];
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            const gpu::CommandListType type = index == 1 ? gpu::CommandListType::Compute : gpu::CommandListType::Default;
            commandLists[index] = gpu::CreateCommandList(type, 0x5246464a00000000ull + index, &rhiFailure);
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            const gpu::QueueType queue = index == 1 ? gpu::QueueType::Compute : gpu::QueueType::Graphics;
            if (!commandLists[index].IsValid() || !gpu::BindCommandList(commandLists[index], &rhiFailure) || !allocator.PacketFor(nodes[index], packet, &failure) ||
                !packet.OpenCursor(scopes[index], queue, cursor, &failure))
            {
                gpu::UnbindCommandList();
                return fail("packet recording");
            }
            if (index < 2)
            {
                rendering::ResolvedBufferUse resolved;
                if (!cursor.BeginBufferUse(uses[index], resolved, &failure) || resolved.GetBuffer() != external.GetRef() || !cursor.EndUse(uses[index], &failure))
                {
                    gpu::UnbindCommandList();
                    return fail("imported use recording");
                }
            }
            if (!cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                return fail("packet finalization");
            }
            gpu::UnbindCommandList();
        }

        gpu::SubmissionReceipt submissions[3];
        const gpu::CommandListRef graphicsFork[] = {commandLists[0]};
        const gpu::CommandListRef compute[] = {commandLists[1]};
        const gpu::CommandListRef graphicsJoin[] = {commandLists[2]};
        if (!gpu::CloseAndSubmitCommandLists("resource-flow graphics fork", {graphicsFork, 1}, gpu::CommandListSyncType::ForkAsyncCompute, submissions[0], &rhiFailure) ||
            submissions[0].residency.graphics == 0 || submissions[0].residency.compute == 0 ||
            !gpu::CloseAndSubmitCommandLists("resource-flow async compute", {compute, 1}, gpu::CommandListSyncType::None, submissions[1], &rhiFailure) ||
            submissions[1].completion.queue != gpu::QueueType::Compute ||
            !gpu::CloseAndSubmitCommandLists("resource-flow graphics join", {graphicsJoin, 1}, gpu::CommandListSyncType::JoinAsyncCompute, submissions[2], &rhiFailure) ||
            submissions[2].completion.queue != gpu::QueueType::Graphics || submissions[2].residency.graphics == 0)
            return fail("fork/join submission");

        const rendering::CommandScopeExecutionReceipt receipts[] = {
            {scopes[0], rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, {gpu::QueueType::Graphics, submissions[0].residency.graphics}},
            {scopes[1], rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Compute, submissions[1].completion},
            {scopes[2], rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, submissions[2].completion}};
        const rendering::QueueDependencyExecutionReceipt dependencyReceipts[] = {{scopes[0], scopes[1], gpu::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::Submitted},
                                                                                 {scopes[1], scopes[2], gpu::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::Submitted}};
        const rendering::TerminalExecutionReceipt terminal{
            generation, rendering::TerminalExecutionCompletionKind::Completed, vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
            rendering::TerminalJoinToken::CompletedSynchronously(generation), vanguard::containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(dependencyReceipts)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(submissions[2].completion, 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("terminal join");
        return true;
    }

    class TestMaterialResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return TestMaterialType;
        }
    };

    template <typename T> [[nodiscard]] T* AllocateResource() noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(vanguard::memory::PoolId::Resources, sizeof(T), alignof(T));
        return block ? ::new (block.address) T() : nullptr;
    }

    template <typename T> void DeleteResource(T* const resource) noexcept
    {
        if (resource == nullptr)
            return;
        resource->~T();
        vanguard::memory::MemoryBlock block{resource, sizeof(T), vanguard::memory::PoolId::Resources};
        vanguard::memory::Free(block);
    }

    void BeginMaterialLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        if (!registry.BeginLoading(request))
            return;
        TestMaterialResource* const material = AllocateResource<TestMaterialResource>();
        if (material != nullptr && registry.Publish(request, material))
            return;
        DeleteResource(material);
        static_cast<void>(registry.Fail(request, resources::Failure::OutOfMemory));
    }

    void BeginDeferredMeshLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        static_cast<void>(registry.BeginLoading(request));
    }

    void BeginDeferredTextureLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void*) noexcept
    {
        static_cast<void>(registry.BeginLoading(request));
    }

    void DestroyMaterial(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<TestMaterialResource*>(resource));
    }

    void DestroyMesh(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<meshes::MeshResourceObject*>(resource));
    }

    void DestroyTexture(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteResource(static_cast<textures::TextureResourceObject*>(resource));
    }

    [[nodiscard]] bool WriteFile(const vanguard::filesystem::AbsolutePath& path, const ByteArray& bytes) noexcept
    {
        auto writer = vanguard::filesystem::GetManager().CreateFileWriter(path, vanguard::filesystem::FOF_Buffered);
        if (!writer)
            return false;
        writer->Serialize(const_cast<vanguard::u8*>(bytes.TypedData()), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }

    [[nodiscard]] bool BuildMeshUploadFixture(ByteArray& document, meshes::MeshFile& mesh) noexcept
    {
        std::array<vanguard::u8, 24> positions0{};
        std::array<vanguard::u8, 24> positions1{};
        std::array<vanguard::u8, 16> texcoords0{};
        std::array<vanguard::u8, 16> texcoords1{};
        std::array<vanguard::u8, 12> indices{};
        for (vanguard::u32 index = 0; index < positions0.size(); ++index)
        {
            positions0[index] = static_cast<vanguard::u8>(1u + index);
            positions1[index] = static_cast<vanguard::u8>(41u + index);
        }
        for (vanguard::u32 index = 0; index < texcoords0.size(); ++index)
        {
            texcoords0[index] = static_cast<vanguard::u8>(81u + index);
            texcoords1[index] = static_cast<vanguard::u8>(111u + index);
        }
        for (vanguard::u32 index = 0; index < indices.size(); ++index)
            indices[index] = static_cast<vanguard::u8>(151u + index);

        const std::array<meshes::BufferBuildRecord, 3> buffers{{{10, meshes::BufferKind::Vertex, 12, 48}, {11, meshes::BufferKind::Vertex, 8, 32}, {20, meshes::BufferKind::Index, 2, 12}}};
        const meshes::PageFlags pageFlags = meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload;
        const std::array<meshes::PageBuildRecord, 5> pages{{{10, 0, positions0.data(), positions0.size(), 4, pageFlags},
                                                            {10, 24, positions1.data(), positions1.size(), 4, pageFlags},
                                                            {11, 0, texcoords0.data(), texcoords0.size(), 4, pageFlags},
                                                            {11, 16, texcoords1.data(), texcoords1.size(), 4, pageFlags},
                                                            {20, 0, indices.data(), indices.size(), 4, pageFlags}}};
        const std::array<meshes::VertexLayoutBuildRecord, 1> layouts{{{1}}};
        const std::array<meshes::VertexStreamBuildRecord, 2> streams{
            {{1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 10, 0, 12}, {1, meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, 1, 11, 0, 8}}};
        const std::array<meshes::MaterialSlotBuildRecord, 1> materials{
            {{1, 0x4d4154455249414cull,
              vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("materials/geometry_test.vmat"), vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T'))}}};
        const std::array<meshes::LodBuildRecord, 1> lods{{{0, 1.0f}}};
        meshes::Bounds bounds;
        bounds.minimum[0] = bounds.minimum[1] = bounds.minimum[2] = -1.0f;
        bounds.maximum[0] = bounds.maximum[1] = bounds.maximum[2] = 1.0f;
        bounds.sphereRadius = 1.75f;
        const std::array<meshes::SubmeshBuildRecord, 1> submeshes{
            {{0x1000, 0x2000, 0, 1, 1, 20, meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::CastsShadow, 1, 2, 1, 3, bounds}}};

        meshes::BuildDescription description;
        description.kind = meshes::MeshKind::Static;
        description.name = 0x47454f4d45545259ull;
        description.bounds = bounds;
        description.sourceFingerprint = vanguard::crypto::Sha256("geometry-upload-fixture", 23);
        description.buffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
        description.pages = {pages.data(), static_cast<vanguard::u32>(pages.size())};
        description.vertexLayouts = {layouts.data(), static_cast<vanguard::u32>(layouts.size())};
        description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
        description.materialSlots = {materials.data(), static_cast<vanguard::u32>(materials.size())};
        description.lods = {lods.data(), static_cast<vanguard::u32>(lods.size())};
        description.submeshes = {submeshes.data(), static_cast<vanguard::u32>(submeshes.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        if (meshes::WriteMesh(writer, description) != meshes::Result::Success)
            return false;
        vanguard::filesystem::MemoryFileReader reader(document, 0);
        return mesh.Open(reader) == meshes::Result::Success;
    }

    [[nodiscard]] bool BuildTextureUploadFixture(ByteArray& document) noexcept
    {
        std::array<vanguard::u8, 64> pixels{};
        for (vanguard::u32 index = 0; index < pixels.size(); ++index)
            pixels[index] = static_cast<vanguard::u8>(index * 17u + 3u);

        const std::array<textures::SubresourceBuildRecord, 1> subresources{{
            {0, 0, 0, pixels.data(), pixels.size(), 16, 64},
        }};
        textures::BuildDescription description;
        description.dimension = textures::TextureDimension::Texture2D;
        description.format = textures::PixelFormat::R8G8B8A8UNorm;
        description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
        description.width = 4;
        description.height = 4;
        description.depth = 1;
        description.arrayLayers = 1;
        description.mipCount = 1;
        description.mipTailFirstLevel = 0;
        description.sourceFingerprint = vanguard::crypto::Sha256("texture-uploader-fixture", 24);
        description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        return textures::WriteTexture(writer, description) == textures::Result::Success;
    }

    [[nodiscard]] bool BuildTextureTransitionFixture(ByteArray& document) noexcept
    {
        std::array<vanguard::u8, 256> mip0{};
        std::array<vanguard::u8, 64> mip1{};
        std::array<vanguard::u8, 16> mip2{};
        std::array<vanguard::u8, 4> mip3{};
        for (vanguard::u32 index = 0; index < mip0.size(); ++index)
            mip0[index] = static_cast<vanguard::u8>(index * 13u + 1u);
        for (vanguard::u32 index = 0; index < mip1.size(); ++index)
            mip1[index] = static_cast<vanguard::u8>(index * 11u + 2u);
        for (vanguard::u32 index = 0; index < mip2.size(); ++index)
            mip2[index] = static_cast<vanguard::u8>(index * 7u + 3u);
        for (vanguard::u32 index = 0; index < mip3.size(); ++index)
            mip3[index] = static_cast<vanguard::u8>(index * 5u + 4u);
        const std::array<textures::SubresourceBuildRecord, 4> subresources{{
            {0, 0, 0, mip0.data(), mip0.size(), 32, 256},
            {1, 0, 0, mip1.data(), mip1.size(), 16, 64},
            {2, 0, 0, mip2.data(), mip2.size(), 8, 16},
            {3, 0, 0, mip3.data(), mip3.size(), 4, 4},
        }};
        textures::BuildDescription description;
        description.dimension = textures::TextureDimension::Texture2D;
        description.format = textures::PixelFormat::R8G8B8A8UNorm;
        description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
        description.width = 8;
        description.height = 8;
        description.depth = 1;
        description.arrayLayers = 1;
        description.mipCount = 4;
        description.mipTailFirstLevel = 2;
        description.sourceFingerprint = vanguard::crypto::Sha256("texture-transition-fixture", 26);
        description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        document.Clear();
        vanguard::filesystem::MemoryFileWriter writer(document);
        return textures::WriteTexture(writer, description) == textures::Result::Success;
    }

    [[nodiscard]] bool PumpTextureUpload(rendering::TextureUploader& uploader, const rendering::TextureUploadRequestId request, rendering::TextureUploadFailure& failure) noexcept
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const rendering::TextureUploadState state = uploader.GetState(request);
            if (state == rendering::TextureUploadState::Submitted || state == rendering::TextureUploadState::Failed)
                return true;
            if (!uploader.Tick(&failure))
                return false;
            std::this_thread::yield();
        }
        return false;
    }

    [[nodiscard]] bool RunTextureUploaderRegressionTests(const resources::ResourceHandle& texture, const resources::ResourceHandle& multiWindowTexture, gpu::Failure& rhiFailure) noexcept
    {
        const auto VerifyPersistentShaderRead = [&rhiFailure](const gpu::TextureRef candidate) noexcept
        {
            const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
            for (vanguard::u32 pass = 0; pass < 2; ++pass)
            {
                const gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::CopySync, 0x5458535441544500ull + pass, &rhiFailure);
                if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !gpu::TransitionTexture(candidate, shaderRead, gpu::ResourceState::CopySource, {}, &rhiFailure))
                    return false;
                gpu::UnbindCommandList();
                const gpu::CommandListRef submissions[] = {commands};
                gpu::GpuFence completion;
                if (!gpu::CloseAndSubmitCommandLists("texture persistent shader-read state", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure) ||
                    !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
                    return false;
            }
            return true;
        };

        rendering::TextureUploadFailure failure;
        const auto RegressionFailure = [&failure](const unsigned line) noexcept
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture uploader regression check failed at line %u: code=%u message=%s\n", line, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "");
            return false;
        };
        rendering::TextureUploaderConfig invalidConfig;
        invalidConfig.maximumAcquisitionStartsPerTick = 0;
        rendering::TextureUploader invalidUploader;
        if (invalidUploader.Initialize(invalidConfig, &failure) || failure.code != rendering::TextureUploadFailureCode::InvalidConfiguration)
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig throttledConfig;
        throttledConfig.maximumRequests = 2;
        throttledConfig.maximumAcquisitionStartsPerTick = 1;
        throttledConfig.maximumCandidatesPerBatch = 2;
        throttledConfig.maximumWritesPerBatch = 2;
        throttledConfig.maximumBytesPerBatch = 128;
        throttledConfig.maximumPendingCandidateBytes = 128;
        throttledConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader throttledUploader;
        rendering::TextureUploadRequestId firstThrottleRequest;
        rendering::TextureUploadRequestId secondThrottleRequest;
        if (!throttledUploader.Initialize(throttledConfig, &failure) || !throttledUploader.RequestMipTail(texture, {11, 1}, firstThrottleRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !throttledUploader.RequestMipTail(texture, {12, 1}, secondThrottleRequest, vanguard::io::eAsyncPriority_Streaming, &failure) || !throttledUploader.Tick(&failure) ||
            throttledUploader.GetStats().acquisitionWindowsStarted != 1 || !throttledUploader.Tick(&failure) || throttledUploader.GetStats().acquisitionWindowsStarted != 2)
            return RegressionFailure(__LINE__);
        if (!PumpTextureUpload(throttledUploader, firstThrottleRequest, failure) || !PumpTextureUpload(throttledUploader, secondThrottleRequest, failure) ||
            throttledUploader.GetState(firstThrottleRequest) != rendering::TextureUploadState::Submitted || throttledUploader.GetState(secondThrottleRequest) != rendering::TextureUploadState::Submitted)
            return RegressionFailure(__LINE__);
        rendering::SubmittedTextureCandidate firstThrottleCandidate;
        rendering::SubmittedTextureCandidate secondThrottleCandidate;
        if (!throttledUploader.TakeSubmitted(firstThrottleRequest, firstThrottleCandidate, &failure) || !throttledUploader.TakeSubmitted(secondThrottleRequest, secondThrottleCandidate, &failure) ||
            !gpu::WaitForGpuFence(firstThrottleCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure) || !gpu::WaitForGpuFence(secondThrottleCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return RegressionFailure(__LINE__);
        firstThrottleCandidate.Reset();
        secondThrottleCandidate.Reset();
        if (!throttledUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig normalConfig;
        normalConfig.maximumRequests = 1;
        normalConfig.maximumAcquisitionStartsPerTick = 1;
        normalConfig.maximumCandidatesPerBatch = 1;
        normalConfig.maximumWritesPerBatch = 1;
        normalConfig.maximumBytesPerBatch = 64;
        normalConfig.maximumPendingCandidateBytes = 64;
        normalConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader normalUploader;
        rendering::TextureUploadRequestId request;
        if (!normalUploader.Initialize(normalConfig, &failure) || !normalUploader.RequestMipTail(texture, {21, 1}, request, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(normalUploader, request, failure) || normalUploader.GetState(request) != rendering::TextureUploadState::Submitted)
            return RegressionFailure(__LINE__);

        rendering::TextureUploadRequestId coalesced;
        if (!normalUploader.RequestMipTail(texture, {21, 1}, coalesced, vanguard::io::eAsyncPriority_Streaming, &failure) || coalesced != request || normalUploader.GetStats().requestsCoalesced != 1 ||
            normalUploader.GetStats().liveRequests != 1 || normalUploader.GetStats().batchesSubmitted != 1)
            return RegressionFailure(__LINE__);
        rendering::SubmittedTextureCandidate submitted;
        if (!normalUploader.TakeSubmitted(request, submitted, &failure) || !submitted.IsValid() || !gpu::WaitForGpuFence(submitted.copyCompletion, 5'000'000'000ull, &rhiFailure) ||
            !VerifyPersistentShaderRead(submitted.texture.GetRef()))
            return RegressionFailure(__LINE__);
        submitted.Reset();
        if (!normalUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig multiWindowConfig = normalConfig;
        multiWindowConfig.maximumAcquisitionWindowSubresources = 1;
        rendering::TextureUploader multiWindowUploader;
        rendering::TextureUploadRequestId multiWindowRequest;
        if (!multiWindowUploader.Initialize(multiWindowConfig, &failure) ||
            !multiWindowUploader.RequestMipTail(multiWindowTexture, {23, 1}, multiWindowRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(multiWindowUploader, multiWindowRequest, failure) || multiWindowUploader.GetState(multiWindowRequest) != rendering::TextureUploadState::Submitted)
            return RegressionFailure(__LINE__);
        const rendering::TextureUploaderStats multiWindowStats = multiWindowUploader.GetStats();
        rendering::SubmittedTextureCandidate multiWindowCandidate;
        if (multiWindowStats.acquisitionWindowsStarted != 2 || multiWindowStats.batchesSubmitted != 2 || multiWindowStats.writesSubmitted != 2 || multiWindowStats.bytesSubmitted != 20 ||
            !multiWindowUploader.TakeSubmitted(multiWindowRequest, multiWindowCandidate, &failure) || !multiWindowCandidate.IsValid() || multiWindowCandidate.firstResidentMip != 2 ||
            multiWindowCandidate.residentMipCount != 2 || multiWindowCandidate.subresourceCount != 2 || multiWindowCandidate.sourceBytesUploaded != 20 ||
            !gpu::WaitForGpuFence(multiWindowCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return RegressionFailure(__LINE__);
        multiWindowCandidate.Reset();
        if (!multiWindowUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        // Source-byte admission and retained physical-memory admission are separate. Two multi-window
        // requests may reserve their small cooked tails together, but an exact one-texture GPU budget must
        // retain only one candidate until ownership of that candidate leaves the uploader.
        gpu::TextureDesc budgetProbeDesc;
        budgetProbeDesc.extent = {2, 2, 1};
        budgetProbeDesc.format = gpu::Format::R8G8B8A8UNorm;
        budgetProbeDesc.mipCount = 2;
        budgetProbeDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        budgetProbeDesc.initialState = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        gpu::Texture budgetProbe(gpu::AdoptReference, gpu::CreateTexture(budgetProbeDesc, {}, &rhiFailure));
        if (!budgetProbe.IsValid())
            return RegressionFailure(__LINE__);
        const vanguard::u64 probedCandidateBytes = gpu::GetMemoryRequirements(budgetProbe.GetRef()).size;
        if (probedCandidateBytes <= 1)
            return RegressionFailure(__LINE__);
        const vanguard::u64 exactCandidateBytes = probedCandidateBytes;
        budgetProbe.Reset();

        rendering::TextureUploaderConfig physicalBudgetConfig = multiWindowConfig;
        physicalBudgetConfig.maximumRequests = 2;
        physicalBudgetConfig.maximumAcquisitionStartsPerTick = 2;
        physicalBudgetConfig.maximumCandidatesPerBatch = 2;
        physicalBudgetConfig.maximumWritesPerBatch = 2;
        physicalBudgetConfig.maximumCandidateGpuBytesPerBatch = exactCandidateBytes * 2;
        physicalBudgetConfig.maximumPendingCandidateGpuBytes = exactCandidateBytes;
        rendering::TextureUploader physicalBudgetUploader;
        rendering::TextureUploadRequestId firstPhysicalRequest;
        rendering::TextureUploadRequestId secondPhysicalRequest;
        if (!physicalBudgetUploader.Initialize(physicalBudgetConfig, &failure) ||
            !physicalBudgetUploader.RequestMipTail(multiWindowTexture, {41, 1}, firstPhysicalRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !physicalBudgetUploader.RequestMipTail(multiWindowTexture, {42, 1}, secondPhysicalRequest, vanguard::io::eAsyncPriority_Streaming, &failure))
            return RegressionFailure(__LINE__);
        const auto IsRetainedPhysicalCandidate = [&physicalBudgetUploader](const rendering::TextureUploadRequestId request) noexcept
        {
            const rendering::TextureUploadState state = physicalBudgetUploader.GetState(request);
            return state == rendering::TextureUploadState::Submitted || state == rendering::TextureUploadState::ReadyToInstall;
        };
        bool observedSingleRetainedCandidate = false;
        const auto physicalBudgetDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < physicalBudgetDeadline)
        {
            if (!physicalBudgetUploader.Tick(&failure))
                return RegressionFailure(__LINE__);
            const bool firstRetained = IsRetainedPhysicalCandidate(firstPhysicalRequest);
            const bool secondRetained = IsRetainedPhysicalCandidate(secondPhysicalRequest);
            if (firstRetained != secondRetained && physicalBudgetUploader.GetStats().pendingCandidateBytes == 40)
            {
                observedSingleRetainedCandidate = true;
                break;
            }
            std::this_thread::yield();
        }
        const bool firstRetained = IsRetainedPhysicalCandidate(firstPhysicalRequest);
        const bool secondRetained = IsRetainedPhysicalCandidate(secondPhysicalRequest);
        const rendering::TextureUploadRequestId retainedPhysicalRequest = firstRetained ? firstPhysicalRequest : secondPhysicalRequest;
        const rendering::TextureUploadRequestId blockedPhysicalRequest = firstRetained ? secondPhysicalRequest : firstPhysicalRequest;
        const rendering::TextureUploaderStats oneRetainedStats = physicalBudgetUploader.GetStats();
        if (!observedSingleRetainedCandidate || firstRetained == secondRetained || physicalBudgetUploader.GetState(blockedPhysicalRequest) != rendering::TextureUploadState::Acquiring ||
            oneRetainedStats.pendingCandidateBytes != 40 || oneRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes || oneRetainedStats.candidateGpuBytesSubmitted != exactCandidateBytes)
            return RegressionFailure(__LINE__);
        for (vanguard::u32 tick = 0; tick < 4; ++tick)
            if (!physicalBudgetUploader.Tick(&failure))
                return RegressionFailure(__LINE__);
        const rendering::TextureUploaderStats stillOneRetainedStats = physicalBudgetUploader.GetStats();
        if (physicalBudgetUploader.GetState(blockedPhysicalRequest) != rendering::TextureUploadState::Acquiring || stillOneRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes ||
            stillOneRetainedStats.candidateGpuBytesSubmitted != exactCandidateBytes)
            return RegressionFailure(__LINE__);

        rendering::SubmittedTextureCandidate retainedPhysicalCandidate;
        const bool retainedTaken = physicalBudgetUploader.TakeSubmitted(retainedPhysicalRequest, retainedPhysicalCandidate, &failure);
        const rendering::TextureUploaderStats afterRetainedTakeStats = physicalBudgetUploader.GetStats();
        const bool blockedPumped = retainedTaken && PumpTextureUpload(physicalBudgetUploader, blockedPhysicalRequest, failure);
        if (!retainedTaken || afterRetainedTakeStats.pendingCandidateBytes != 20 || afterRetainedTakeStats.pendingCandidateGpuBytes != 0 || !blockedPumped ||
            physicalBudgetUploader.GetState(blockedPhysicalRequest) != rendering::TextureUploadState::Submitted)
            return RegressionFailure(__LINE__);
        const rendering::TextureUploaderStats secondRetainedStats = physicalBudgetUploader.GetStats();
        rendering::SubmittedTextureCandidate blockedPhysicalCandidate;
        if (secondRetainedStats.pendingCandidateBytes != 20 || secondRetainedStats.pendingCandidateGpuBytes != exactCandidateBytes ||
            !physicalBudgetUploader.TakeSubmitted(blockedPhysicalRequest, blockedPhysicalCandidate, &failure) || !gpu::WaitForGpuFence(retainedPhysicalCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure) ||
            !gpu::WaitForGpuFence(blockedPhysicalCandidate.copyCompletion, 5'000'000'000ull, &rhiFailure))
            return RegressionFailure(__LINE__);
        retainedPhysicalCandidate.Reset();
        blockedPhysicalCandidate.Reset();
        if (physicalBudgetUploader.GetStats().pendingCandidateBytes != 0 || physicalBudgetUploader.GetStats().pendingCandidateGpuBytes != 0 || !physicalBudgetUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig impossiblePhysicalBudgetConfig = multiWindowConfig;
        impossiblePhysicalBudgetConfig.maximumPendingCandidateGpuBytes = exactCandidateBytes - 1;
        rendering::TextureUploader impossiblePhysicalBudgetUploader;
        rendering::TextureUploadRequestId impossiblePhysicalRequest;
        if (!impossiblePhysicalBudgetUploader.Initialize(impossiblePhysicalBudgetConfig, &failure) ||
            !impossiblePhysicalBudgetUploader.RequestMipTail(multiWindowTexture, {43, 1}, impossiblePhysicalRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(impossiblePhysicalBudgetUploader, impossiblePhysicalRequest, failure) ||
            impossiblePhysicalBudgetUploader.GetState(impossiblePhysicalRequest) != rendering::TextureUploadState::Failed)
            return RegressionFailure(__LINE__);
        rendering::TextureUploadFailure impossiblePhysicalFailure;
        if (!impossiblePhysicalBudgetUploader.GetRequestFailure(impossiblePhysicalRequest, impossiblePhysicalFailure) ||
            impossiblePhysicalFailure.code != rendering::TextureUploadFailureCode::CapacityExceeded || impossiblePhysicalBudgetUploader.GetStats().pendingCandidateGpuBytes != 0 ||
            !impossiblePhysicalBudgetUploader.Cancel(impossiblePhysicalRequest, &failure) || !impossiblePhysicalBudgetUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig cancelledConfig = normalConfig;
        cancelledConfig.maximumReadyCandidates = 1;
        rendering::TextureUploader cancelledUploader;
        rendering::TextureUploadRequestId cancelledRequest;
        if (!cancelledUploader.Initialize(cancelledConfig, &failure) || !cancelledUploader.RequestMipTail(texture, {22, 1}, cancelledRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(cancelledUploader, cancelledRequest, failure) || cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Submitted ||
            !cancelledUploader.Cancel(cancelledRequest, &failure) || cancelledUploader.GetStats().liveRequests != 1)
            return RegressionFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000 && cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Invalid; ++poll)
        {
            if (!cancelledUploader.Tick(&failure))
                return RegressionFailure(__LINE__);
            std::this_thread::yield();
        }
        if (cancelledUploader.GetState(cancelledRequest) != rendering::TextureUploadState::Invalid || !cancelledUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);

        rendering::TextureUploaderConfig hardCapConfig = normalConfig;
        hardCapConfig.maximumBytesPerBatch = 63;
        rendering::TextureUploader hardCapUploader;
        rendering::TextureUploadRequestId hardCapRequest;
        if (!hardCapUploader.Initialize(hardCapConfig, &failure) || !hardCapUploader.RequestMipTail(texture, {31, 1}, hardCapRequest, vanguard::io::eAsyncPriority_Streaming, &failure) ||
            !PumpTextureUpload(hardCapUploader, hardCapRequest, failure) || hardCapUploader.GetState(hardCapRequest) != rendering::TextureUploadState::Failed)
            return RegressionFailure(__LINE__);
        rendering::TextureUploadFailure requestFailure;
        const rendering::TextureUploaderStats hardCapStats = hardCapUploader.GetStats();
        if (!hardCapUploader.GetRequestFailure(hardCapRequest, requestFailure) || requestFailure.code != rendering::TextureUploadFailureCode::CapacityExceeded || hardCapStats.batchesSubmitted != 0 ||
            hardCapStats.bytesSubmitted != 0 || !hardCapUploader.Cancel(hardCapRequest, &failure) || !hardCapUploader.Shutdown(&failure))
            return RegressionFailure(__LINE__);
        return true;
    }

    [[nodiscard]] bool SubmitFences(const vanguard::u64 name, gpu::ResidencyFenceSet& fences, gpu::Failure& failure) noexcept
    {
        fences = {};
        const gpu::CommandListType types[] = {gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            gpu::CommandListRef list = gpu::CreateCommandList(types[index], name + index, &failure);
            if (!list || !gpu::BindCommandList(list, &failure))
                return false;
            gpu::UnbindCommandList();
            const gpu::CommandListRef lists[] = {list};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("geometry allocator retirement", {lists, 1}, gpu::CommandListSyncType::None, completion, &failure))
                return false;
            fences.Include(completion);
        }
        return true;
    }

    [[nodiscard]] bool WaitFences(const gpu::ResidencyFenceSet& fences, gpu::Failure& failure) noexcept
    {
        return gpu::WaitForGpuFence({gpu::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) && gpu::WaitForGpuFence({gpu::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
               gpu::WaitForGpuFence({gpu::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
    }

    struct TextureRuntimeGpuSceneExecution
    {
        rendering::GpuSceneRuntime* runtime = nullptr;
    };

    rendering::RenderFrameExecutionStatus ExecuteTextureRuntimeGpuSceneTick(rendering::RenderFrameTickContext& context, void* const userData) noexcept
    {
        auto* const execution = static_cast<TextureRuntimeGpuSceneExecution*>(userData);
        rendering::GpuSceneRuntimeFailure failure;
        return execution != nullptr && execution->runtime != nullptr && execution->runtime->Publish(context, &failure)
                   ? rendering::RenderFrameExecutionStatus::Success()
                   : rendering::RenderFrameExecutionStatus::Failure(failure.message != nullptr ? failure.message : "texture runtime GPU Scene contribution failed");
    }

    rendering::RenderFrameExecutionStatus ExecuteTextureRuntimeGpuSceneFrame(rendering::RenderFrameContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    [[nodiscard]] bool RunTests(gpu::Failure& failure) noexcept
    {
        if (!RunDedicatedResourceProviderSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow dedicated-resource provider smoke test passed.\n");
        if (!RunDedicatedResourceLifetimeReuseTest())
            return false;
        std::printf("Vanguard Resource Flow same-frame lifetime reuse test passed.\n");
        if (!RunDedicatedResourceFailureAndBudgetTest())
            return false;
        std::printf("Vanguard Resource Flow provider rollback and hard-budget test passed.\n");
        if (!RunPlacedResourcePoolContractTest())
            return false;
        std::printf("Vanguard Resource Flow placed-resource provider contract test passed.\n");
        if (!RunMixedCommandListGroupTest())
            return false;
        std::printf("Vanguard mixed CPU/GPU command-list group continuation test passed.\n");
        if (!RunPlacedResourceExecutionSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow placed-resource execution test passed.\n");
        if (!RunTypedClearSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow typed clear smoke test passed.\n");
        if (!RunRetainedImportSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow retained import smoke test passed.\n");
        if (!RunTerminalExportSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow terminal export smoke test passed.\n");
        if (!RunCrossQueueForkJoinSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow graphics/compute fork-join smoke test passed.\n");
        rendering::GeometryAllocator allocator;
        rendering::GeometryAllocatorFailure allocatorFailure;
        rendering::GeometryAllocatorConfig config;
        config.verticesPerArena = 64;
        config.indexBytesPerArena = 128;
        config.maximumIndexArenaBytes = 4096;
        config.maximumVertexArenas = 8;
        config.maximumIndexArenas = 8;
        config.maximumAllocations = 32;
        config.retirementEpochCount = 4;
        config.initialRetirementsPerEpoch = 8;
        if (!allocator.Initialize(config, &allocatorFailure))
            return false;
        rendering::GeometryUploader uploader;
        rendering::GeometryUploadFailure uploadFailure;
        rendering::GeometryUploadConfig uploadConfig;
        uploadConfig.bytesPerSegment = 1024;
        uploadConfig.maximumOverflowBytes = 4096;
        uploadConfig.maximumGeometriesPerBatch = 8;
        uploadConfig.maximumCopiesPerBatch = 32;
        if (!uploader.Initialize(allocator, uploadConfig, &uploadFailure))
            return false;

        const gpu::VertexBindingDesc bindings[] = {{0, 12, gpu::VertexInputRate::PerVertex, 1}, {1, 20, gpu::VertexInputRate::PerVertex, 1}};
        vanguard::crypto::Digest256 fingerprint;
        fingerprint.bytes[0] = 1;
        rendering::GeometryAllocationRequest request;
        request.vertexLayout = {fingerprint, {bindings, 2}};
        request.vertexCount = 10;
        request.indexCount = 6;

        rendering::GeometryReservation first;
        rendering::GeometryReservation second;
        if (!allocator.Reserve(request, first, &allocatorFailure))
            return false;
        request.vertexCount = 8;
        request.indexCount = 8;
        if (!allocator.Reserve(request, second, &allocatorFailure) || first.vertex.arena != second.vertex.arena || first.index.arena != second.index.arena || second.vertex.firstVertex != 12 ||
            second.index.firstIndex != 8)
            return false;

        rendering::GeometryVertexArenaView vertexArena;
        rendering::GeometryIndexArenaView indexArena;
        if (!allocator.GetVertexArena(first.vertex.arena, vertexArena) || vertexArena.bindingCount != 2 || vertexArena.vertexCapacity != 64 || !vertexArena.buffers[0] || !vertexArena.buffers[1] ||
            vertexArena.dedicated || !allocator.GetIndexArena(first.index.arena, indexArena) || indexArena.format != gpu::IndexFormat::UInt16 || indexArena.indexCapacity != 64 || !indexArena.buffer ||
            indexArena.dedicated)
            return false;

        rendering::GeometryReservation reused;
        if (!allocator.Cancel(second, &allocatorFailure) || !allocator.Reserve(request, reused, &allocatorFailure) || reused.vertex.firstVertex != second.vertex.firstVertex ||
            reused.index.firstIndex != second.index.firstIndex || !allocator.Cancel(reused, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest otherLayout = request;
        otherLayout.vertexLayout.fingerprint.bytes[0] = 2;
        rendering::GeometryReservation otherLayoutReservation;
        if (!allocator.Reserve(otherLayout, otherLayoutReservation, &allocatorFailure) || otherLayoutReservation.vertex.arena == first.vertex.arena ||
            !allocator.Cancel(otherLayoutReservation, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest uint32Request = request;
        uint32Request.indexFormat = gpu::IndexFormat::UInt32;
        rendering::GeometryReservation uint32Reservation;
        if (!allocator.Reserve(uint32Request, uint32Reservation, &allocatorFailure) || uint32Reservation.index.arena == first.index.arena || !allocator.Cancel(uint32Reservation, &allocatorFailure))
            return false;

        rendering::GeometryAllocationRequest batchRequests[] = {request, request};
        batchRequests[1].vertexLayout.fingerprint = {};
        rendering::GeometryReservation batchReservations[2];
        const vanguard::u32 reservedBeforeBatch = allocator.GetStats().reservedAllocations;
        if (allocator.ReserveBatch({batchRequests, 2}, {batchReservations, 2}, &allocatorFailure) || batchReservations[0].IsValid() || batchReservations[1].IsValid() ||
            allocator.GetStats().reservedAllocations != reservedBeforeBatch)
            return false;

        rendering::GeometryAllocationRequest dedicatedRequest = request;
        dedicatedRequest.vertexCount = 65;
        dedicatedRequest.indexCount = 65;
        rendering::GeometryReservation dedicated;
        if (!allocator.Reserve(dedicatedRequest, dedicated, &allocatorFailure) || !allocator.GetVertexArena(dedicated.vertex.arena, vertexArena) || !vertexArena.dedicated ||
            !allocator.GetIndexArena(dedicated.index.arena, indexArena) || !indexArena.dedicated)
            return false;
        const rendering::GeometryUploadRequest oversizedUpload[] = {{dedicated}};
        rendering::GeometryUploadReservation oversizedStaging[1];
        if (!uploader.Begin({oversizedUpload, 1}, {oversizedStaging, 1}, &uploadFailure) || !oversizedStaging[0].IsValid() || uploader.GetStats().overflowBatches != 1 || !uploader.Cancel(&uploadFailure))
            return false;
        const rendering::VertexArenaSetId oldVertexArena = dedicated.vertex.arena;
        const rendering::IndexArenaId oldIndexArena = dedicated.index.arena;
        if (!allocator.Cancel(dedicated, &allocatorFailure) || allocator.GetVertexArena(oldVertexArena, vertexArena) || allocator.GetIndexArena(oldIndexArena, indexArena))
            return false;

        rendering::GeometryReservation recycledDedicated;
        if (!allocator.Reserve(dedicatedRequest, recycledDedicated, &allocatorFailure) || recycledDedicated.vertex.arena.index != oldVertexArena.index ||
            recycledDedicated.vertex.arena.generation == oldVertexArena.generation || recycledDedicated.index.arena.index != oldIndexArena.index ||
            recycledDedicated.index.arena.generation == oldIndexArena.generation || !allocator.Cancel(recycledDedicated, &allocatorFailure))
            return false;

        rendering::GeometryReservation forged = first;
        ++forged.vertex.vertexCount;
        const rendering::GeometryUploadRequest forgedRequest[] = {{forged}};
        rendering::GeometryUploadReservation staging[1];
        if (uploader.Begin({forgedRequest, 1}, {staging, 1}, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::InvalidAllocationState)
            return false;

        const rendering::GeometryUploadRequest uploadRequest[] = {{first}};
        if (!uploader.Begin({uploadRequest, 1}, {staging, 1}, &uploadFailure) || staging[0].bindingCount != 2 || staging[0].vertexStreams[0].size != 120 || staging[0].vertexStreams[1].size != 200 ||
            staging[0].indices.size != 12)
            return false;
        const rendering::GeometryUploadReservation cancelledStaging = staging[0];
        if (!uploader.Cancel(&uploadFailure) || uploader.Complete(cancelledStaging, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::NoOpenBatch ||
            !allocator.ValidateReservation(first) || !uploader.Begin({uploadRequest, 1}, {staging, 1}, &uploadFailure))
            return false;
        for (vanguard::u32 binding = 0; binding < staging[0].bindingCount; ++binding)
            for (vanguard::u64 byte = 0; byte < staging[0].vertexStreams[binding].size; ++byte)
                static_cast<vanguard::u8*>(staging[0].vertexStreams[binding].destination)[byte] = static_cast<vanguard::u8>(17u + binding * 53u + byte);
        for (vanguard::u64 byte = 0; byte < staging[0].indices.size; ++byte)
            static_cast<vanguard::u8*>(staging[0].indices.destination)[byte] = static_cast<vanguard::u8>(201u + byte);

        rendering::GeometryPlacement placements[1];
        rendering::GeometryUploadResult uploadResult;
        if (uploader.Submit({placements, 1}, uploadResult, &uploadFailure) || uploadFailure.code != rendering::GeometryUploadFailureCode::BatchNotReady)
            return false;
        rendering::GeometryUploadReservation forgedStaging = staging[0];
        ++forgedStaging.vertexStreams[0].size;
        if (uploader.Complete(forgedStaging, &uploadFailure) || !uploader.Complete(staging[0], &uploadFailure) || !uploader.Submit({placements, 1}, uploadResult, &uploadFailure) ||
            !uploadResult.completion.IsValid() || uploadResult.geometryCount != 1 || uploadResult.copyCount != 3 || uploadResult.destinationBufferCount != 3 || uploadResult.uploadedBytes != 332 ||
            !gpu::WaitForGpuFence(uploadResult.completion, 5'000'000'000ull, &failure) || allocator.GetState(first.allocation) != rendering::GeometryAllocationState::Active)
            return false;
        const rendering::GeometryUploadStats uploadStats = uploader.GetStats();
        if (uploadStats.batchesSubmitted != 1 || uploadStats.geometriesUploaded != 1 || uploadStats.copiesRecorded != 3 || uploadStats.bytesUploaded != 332 || uploadStats.stagingBytesCommitted != 3072)
            return false;
        const rendering::GeometryPlacement placement = placements[0];
        if (!allocator.Retire(placement, &allocatorFailure))
            return false;

        gpu::ResidencyFenceSet missingQueues;
        missingQueues.graphics = 1;
        if (allocator.SealRetirements(missingQueues, &allocatorFailure) || allocatorFailure.code != rendering::GeometryAllocatorFailureCode::MissingRetirementFence)
            return false;

        gpu::ResidencyFenceSet submitted;
        if (!SubmitFences(0x47454f4d45545200ull, submitted, failure) || !WaitFences(submitted, failure))
            return false;
        const gpu::ResidencyFenceSet future{submitted.graphics + 1u, submitted.compute + 1u, submitted.copy + 1u};
        if (!allocator.SealRetirements(future, &allocatorFailure) || allocator.Collect(&allocatorFailure) != 0)
            return false;
        gpu::ResidencyFenceSet completed;
        if (!SubmitFences(0x47454f4d45545300ull, completed, failure) || completed.graphics < future.graphics || completed.compute < future.compute || completed.copy < future.copy ||
            !WaitFences(completed, failure) || allocator.Collect(&allocatorFailure) != 1 || allocator.GetState(first.allocation) != rendering::GeometryAllocationState::Invalid)
            return false;

        rendering::GeometryReservation reclaimed;
        if (!allocator.Reserve(request, reclaimed, &allocatorFailure) || reclaimed.vertex.firstVertex != first.vertex.firstVertex || reclaimed.index.firstIndex != first.index.firstIndex ||
            !allocator.Cancel(reclaimed, &allocatorFailure))
            return false;

        ByteArray meshDocument{vanguard::memory::pools::Rendering::GetInstance()};
        ByteArray textureDocument{vanguard::memory::pools::Rendering::GetInstance()};
        ByteArray transitionTextureDocument{vanguard::memory::pools::Rendering::GetInstance()};
        meshes::MeshFile mesh;
        if (!vanguard::rendering::tests::RunMeshDrawLayoutProof() || !BuildMeshUploadFixture(meshDocument, mesh) || !BuildTextureUploadFixture(textureDocument) || !BuildTextureTransitionFixture(transitionTextureDocument))
            return false;
        std::array<ByteArray, 5> pageBytes{{ByteArray(vanguard::memory::pools::Rendering::GetInstance()), ByteArray(vanguard::memory::pools::Rendering::GetInstance()),
                                            ByteArray(vanguard::memory::pools::Rendering::GetInstance()), ByteArray(vanguard::memory::pools::Rendering::GetInstance()),
                                            ByteArray(vanguard::memory::pools::Rendering::GetInstance())}};
        rendering::VerifiedMeshPagePayload pagePayloads[5];
        vanguard::filesystem::MemoryFileReader pageReader(meshDocument, 0);
        for (vanguard::u32 page = 0; page < 5; ++page)
        {
            pageBytes[page].Resize(static_cast<vanguard::u32>(mesh.GetPages()[page].byteSize));
            if (mesh.ReadPage(pageReader, page, pageBytes[page].TypedData(), pageBytes[page].Size()) != meshes::Result::Success)
                return false;
            pagePayloads[page] = {page, {pageBytes[page].TypedData(), pageBytes[page].Size()}};
        }

        rendering::PreparedMeshLodUpload prepared;
        rendering::MeshGeometryUploadFailure meshUploadFailure;
        rendering::VerifiedMeshPagePayload duplicatePages[5] = {pagePayloads[0], pagePayloads[0], pagePayloads[2], pagePayloads[3], pagePayloads[4]};
        const vanguard::u32 reservedBeforeInvalidPages = allocator.GetStats().reservedAllocations;
        if (rendering::PrepareMeshLodGeometryUpload(mesh, 0, {duplicatePages, 5}, allocator, uploader, prepared, &meshUploadFailure) ||
            meshUploadFailure.code != rendering::MeshGeometryUploadFailureCode::InvalidPageSet || prepared.IsValid() || allocator.GetStats().reservedAllocations != reservedBeforeInvalidPages)
            return false;

        rendering::GeometryReservation blockingReservation;
        if (!allocator.Reserve(request, blockingReservation, &allocatorFailure))
            return false;
        const rendering::GeometryUploadRequest blockingRequest[] = {{blockingReservation}};
        rendering::GeometryUploadReservation blockingUpload[1];
        if (!uploader.Begin({blockingRequest, 1}, {blockingUpload, 1}, &uploadFailure) ||
            rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure) ||
            meshUploadFailure.code != rendering::MeshGeometryUploadFailureCode::UploadFailure || prepared.IsValid() || allocator.GetStats().reservedAllocations != 1 || !uploader.Cancel(&uploadFailure) ||
            !allocator.Cancel(blockingReservation, &allocatorFailure))
            return false;

        if (!rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure) || !prepared.IsValid() || prepared.pages.Size() != 5 ||
            prepared.geometries.Size() != 1 || prepared.submeshes.Size() != 1 || prepared.vertexBytes != 40 || prepared.indexBytes != 6 ||
            !rendering::CancelPreparedMeshLodUpload(allocator, uploader, prepared, &meshUploadFailure) || prepared.IsValid() || allocator.GetStats().reservedAllocations != 0)
            return false;

        if (!rendering::PrepareMeshLodGeometryUpload(mesh, 0, {pagePayloads, 5}, allocator, uploader, prepared, &meshUploadFailure))
            return false;
        if (!rendering::FillPreparedMeshLodUpload(uploader, prepared, &meshUploadFailure))
            return false;
        rendering::GeometryPlacement meshPlacement[1];
        rendering::GeometryUploadResult meshUploadResult;
        if (!uploader.Submit({meshPlacement, 1}, meshUploadResult, &uploadFailure) || !meshPlacement[0].IsValid() || meshUploadResult.geometryCount != 1 || meshUploadResult.copyCount != 3 ||
            meshUploadResult.uploadedBytes != 46 || !gpu::WaitForGpuFence(meshUploadResult.completion, 5'000'000'000ull, &failure))
            return false;
        prepared.Reset();
        if (!allocator.Retire(meshPlacement[0], &allocatorFailure))
            return false;
        gpu::ResidencyFenceSet meshRetirementFences;
        if (!SubmitFences(0x4d45534847454f00ull, meshRetirementFences, failure) || !WaitFences(meshRetirementFences, failure) || !allocator.SealRetirements(meshRetirementFences, &allocatorFailure) ||
            allocator.Collect(&allocatorFailure) != 1)
            return false;

        const vanguard::filesystem::AbsolutePath testDirectory = vanguard::filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("vanguard_mesh_lod_geometryUploader_tests");
        const vanguard::filesystem::AbsolutePath meshPath = testDirectory.AddFilePath("geometry.vmesh");
        const vanguard::filesystem::AbsolutePath texturePath = testDirectory.AddFilePath("upload.vtex");
        const vanguard::filesystem::AbsolutePath transitionTexturePath = testDirectory.AddFilePath("transition.vtex");
        vanguard::filesystem::Manager& fileManager = vanguard::filesystem::GetManager();
        static_cast<void>(fileManager.DeleteFile(meshPath));
        static_cast<void>(fileManager.DeleteFile(texturePath));
        static_cast<void>(fileManager.DeleteFile(transitionTexturePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
        if (!fileManager.CreatePath(testDirectory) || !WriteFile(meshPath, meshDocument) || !WriteFile(texturePath, textureDocument) || !WriteFile(transitionTexturePath, transitionTextureDocument))
            return false;

        resources::ResourceRegistry registry;
        if (!registry.Initialize() || !registry.RegisterLoader({TestMaterialType, "geometry geometryUploader material", BeginMaterialLoad, DestroyMaterial, nullptr}) ||
            !registry.RegisterLoader({meshes::MeshResourceType, "geometry geometryUploader mesh", BeginDeferredMeshLoad, DestroyMesh, nullptr}) ||
            !registry.RegisterLoader({textures::TextureResourceType, "texture uploader regression texture", BeginDeferredTextureLoad, DestroyTexture, nullptr}))
            return false;
        const resources::ResourceReference materialReference(resources::ResourcePath::FromString("materials/geometry_test.vmat"), TestMaterialType);
        resources::ResourceRequest materialRequest = registry.Request(materialReference);
        materialRequest.Wait();
        resources::ResourceHandle materialHandle = materialRequest.Acquire();
        if (!materialHandle.IsValid())
            return false;

        const resources::ResourceReference meshReference(resources::ResourcePath::FromString("meshes/geometry_geometryUploader.vmesh"), meshes::MeshResourceType);
        resources::ResourceRequest meshRequest = registry.Request(meshReference);
        meshes::MeshPageSource pageSource;
        meshes::MeshResourceObject* meshResource = AllocateResource<meshes::MeshResourceObject>();
        if (meshResource == nullptr || pageSource.OpenLoose(meshPath) != meshes::Result::Success || meshResource->Open(std::move(pageSource), {&materialHandle, 1}) != meshes::Result::Success ||
            !registry.Publish(meshRequest, meshResource))
        {
            DeleteResource(meshResource);
            return false;
        }
        meshRequest.Wait();
        resources::ResourceHandle meshHandle = meshRequest.Acquire();
        if (!meshHandle.IsValid())
            return false;

        const resources::ResourceReference textureReference(resources::ResourcePath::FromString("textures/texture_uploader_test.vtex"), textures::TextureResourceType);
        resources::ResourceRequest textureRequest = registry.Request(textureReference);
        textures::TextureSubresourceSource textureSource;
        textures::TextureFile textureMetadata;
        vanguard::filesystem::MemoryFileReader textureReader(textureDocument, 0);
        textures::TextureResourceObject* textureResource = AllocateResource<textures::TextureResourceObject>();
        if (textureResource == nullptr || textureSource.OpenLoose(texturePath) != textures::Result::Success || textureMetadata.Open(textureReader) != textures::Result::Success ||
            textureResource->OpenPrepared(std::move(textureSource), std::move(textureMetadata)) != textures::Result::Success || !registry.Publish(textureRequest, textureResource))
        {
            DeleteResource(textureResource);
            return false;
        }
        textureRequest.Wait();
        resources::ResourceHandle textureHandle = textureRequest.Acquire();
        if (!textureHandle.IsValid())
            return false;
        const resources::ResourceReference transitionTextureReference(resources::ResourcePath::FromString("textures/texture_transition_test.vtex"), textures::TextureResourceType);
        resources::ResourceRequest transitionTextureRequest = registry.Request(transitionTextureReference);
        textures::TextureSubresourceSource transitionTextureSource;
        textures::TextureFile transitionTextureMetadata;
        vanguard::filesystem::MemoryFileReader transitionTextureReader(transitionTextureDocument, 0);
        textures::TextureResourceObject* transitionTextureResource = AllocateResource<textures::TextureResourceObject>();
        if (transitionTextureResource == nullptr || transitionTextureSource.OpenLoose(transitionTexturePath) != textures::Result::Success ||
            transitionTextureMetadata.Open(transitionTextureReader) != textures::Result::Success ||
            transitionTextureResource->OpenPrepared(std::move(transitionTextureSource), std::move(transitionTextureMetadata)) != textures::Result::Success ||
            !registry.Publish(transitionTextureRequest, transitionTextureResource))
        {
            DeleteResource(transitionTextureResource);
            return false;
        }
        transitionTextureRequest.Wait();
        resources::ResourceHandle transitionTextureHandle = transitionTextureRequest.Acquire();
        if (!transitionTextureHandle.IsValid())
            return false;
        if (!RunTextureUploaderRegressionTests(textureHandle, transitionTextureHandle, failure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture uploader regression failed: rhi=%u message=%s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain residencyDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        rendering::GpuSceneTables residencyTables;
        rendering::GpuSceneTablesFailure residencyTableFailure;
        rendering::GpuSceneLifetime residencyLifetime;
        rendering::GpuSceneLifetimeFailure residencyLifetimeFailure;
        rendering::GpuSceneUploader residencySceneUploader;
        rendering::GpuSceneUploadFailure residencySceneUploadFailure;
        rendering::GpuSceneDefinitions residencyDefinitions;
        rendering::GpuSceneDefinitionFailure residencyDefinitionFailure;
        rendering::GpuSceneUploadConfig residencySceneUploadConfig;
        residencySceneUploadConfig.bytesPerSegment = 2u * 1024u * 1024u;
        residencySceneUploadConfig.maximumUpdatesPerBatch = 64;
        residencySceneUploadConfig.maximumCopiesPerBatch = 128;
        rendering::GpuSceneDefinitionsConfig residencyDefinitionConfig;
        residencyDefinitionConfig.maximumGeometries = 16;
        residencyDefinitionConfig.maximumMaterials = 16;
        residencyDefinitionConfig.maximumRenderables = 16;
        residencyDefinitionConfig.maximumDefinitionsPerBatch = 16;
        residencyDefinitionConfig.maximumAllocationsPerBatch = 64;
        if (!residencyDescriptors || !residencyTables.Initialize({residencyDescriptors, 2}, &residencyTableFailure) || !residencyLifetime.Initialize(residencyTables, {}, &residencyLifetimeFailure) ||
            !residencySceneUploader.Initialize(residencyTables, residencyLifetime, residencySceneUploadConfig, &residencySceneUploadFailure) ||
            !residencyDefinitions.Initialize(residencyLifetime, residencySceneUploader, residencyDefinitionConfig, &residencyDefinitionFailure))
        {
            std::printf("[geometryAllocatorTests] residency GPU Scene setup failed: table=%u lifetime=%u upload=%u definitions=%u\n", static_cast<unsigned>(residencyTableFailure.code),
                        static_cast<unsigned>(residencyLifetimeFailure.code), static_cast<unsigned>(residencySceneUploadFailure.code), static_cast<unsigned>(residencyDefinitionFailure.code));
            return false;
        }

        gpu::DescriptorDomain textureDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 3, 0, visibility}, &failure));
        rendering::TextureResidencyManager textureResidency;
        rendering::TextureResidencyFailure textureResidencyFailure;
        rendering::TextureResidencyConfig textureResidencyConfig;
        textureResidencyConfig.maximumTextures = 3;
        textureResidencyConfig.maximumPendingInstallations = 2;
        textureResidencyConfig.maximumPendingRetirements = 4;
        rendering::GpuTextureResidencyHandle firstTextureHandle;
        rendering::GpuTextureResidencyHandle secondTextureHandle;
        const auto TextureTestFailure = [](const unsigned line) noexcept
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture residency check failed at line %u\n", line);
            return false;
        };
        const rendering::GpuSceneUploadStats uploadStatsBeforeTextureAllocation = residencySceneUploader.GetStats();
        if (!textureDescriptors || !textureResidency.Initialize(residencyLifetime, textureDescriptors, textureResidencyConfig, &textureResidencyFailure) ||
            !textureResidency.Allocate(firstTextureHandle, &textureResidencyFailure) || !textureResidency.Allocate(secondTextureHandle, &textureResidencyFailure) || !firstTextureHandle.IsValid() ||
            !secondTextureHandle.IsValid() || residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureAllocation.batchesSubmitted)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle capacityHandle;
        rendering::GpuTextureResidencyHandle overflowHandle;
        if (!textureResidency.Allocate(capacityHandle, &textureResidencyFailure) || textureResidency.Allocate(overflowHandle, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::CapacityExceeded)
            return TextureTestFailure(__LINE__);

        rendering::GpuSceneAllocation mixedProducerAllocation;
        if (!residencyLifetime.Allocate<rendering::GpuInstance>(1, mixedProducerAllocation, &residencyLifetimeFailure))
            return TextureTestFailure(__LINE__);

        auto SubmitTextureBatch = [&](gpu::GpuFence& completion, const vanguard::u32 maximumTextureInstallations, const rendering::GpuSceneAllocation extraAllocation = {}) noexcept
        {
            completion = {};
            rendering::TextureResidencyBatch batch;
            if (!textureResidency.PrepareBatch(maximumTextureInstallations, batch, &textureResidencyFailure) || !batch.IsValid() || batch.installationCount == 0)
                return TextureTestFailure(__LINE__);
            vanguard::containers::DynamicArray<rendering::GpuSceneUploadRequest> requests(vanguard::memory::pools::Rendering::GetInstance());
            vanguard::containers::DynamicArray<rendering::GpuSceneUploadReservation> reservations(vanguard::memory::pools::Rendering::GetInstance());
            const vanguard::u32 requestCount = batch.installationCount + (extraAllocation.IsValid() ? 1u : 0u);
            requests.Resize(requestCount);
            reservations.Resize(requestCount);
            if (!textureResidency.BuildUploadRequests(batch, {requests.TypedData(), batch.installationCount}, &textureResidencyFailure))
            {
                static_cast<void>(textureResidency.DiscardBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (extraAllocation.IsValid())
                requests[batch.installationCount] = {extraAllocation, 0, 1};
            if (!residencySceneUploader.Begin({requests.TypedData(), requests.Size()}, {reservations.TypedData(), reservations.Size()}, &residencySceneUploadFailure))
            {
                static_cast<void>(textureResidency.RetryBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (!textureResidency.WriteBatch(batch, {reservations.TypedData(), batch.installationCount}, &textureResidencyFailure))
            {
                static_cast<void>(residencySceneUploader.Cancel());
                static_cast<void>(textureResidency.DiscardBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            if (extraAllocation.IsValid())
            {
                const rendering::GpuSceneUploadReservation& reservation = reservations[batch.installationCount];
                if (!reservation.IsValid() || reservation.size != sizeof(rendering::GpuInstance))
                {
                    static_cast<void>(residencySceneUploader.Cancel());
                    static_cast<void>(textureResidency.DiscardBatch(batch));
                    return TextureTestFailure(__LINE__);
                }
                const rendering::GpuInstance value{};
                std::memcpy(reservation.destination, &value, sizeof(value));
            }
            for (const rendering::GpuSceneUploadReservation reservation : reservations)
                if (!residencySceneUploader.Complete(reservation, &residencySceneUploadFailure))
                {
                    static_cast<void>(residencySceneUploader.Cancel());
                    static_cast<void>(textureResidency.DiscardBatch(batch));
                    return TextureTestFailure(__LINE__);
                }
            rendering::GpuSceneUploadResult result;
            if (!residencySceneUploader.Submit(result, &residencySceneUploadFailure))
            {
                static_cast<void>(textureResidency.RetryBatch(batch));
                return TextureTestFailure(__LINE__);
            }
            textureResidency.AcceptSubmittedBatch(batch, result.completion);
            completion = result.completion;
            return true;
        };

        gpu::TextureDesc residentTextureDesc;
        residentTextureDesc.extent = {4, 4, 1};
        residentTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        residentTextureDesc.mipCount = 1;
        residentTextureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        gpu::Texture firstTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture secondTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture thirdTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        gpu::Texture fourthTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
        rendering::TextureInstallationTicket firstTextureTicket;
        rendering::TextureInstallationTicket secondTextureTicket;
        rendering::TextureResidencyInfo firstTextureInfo;
        rendering::TextureResidencyInfo secondTextureInfo;
        const rendering::GpuSceneUploadStats uploadStatsBeforeTextureInstall = residencySceneUploader.GetStats();
        if (!firstTexture || !secondTexture || !thirdTexture || !fourthTexture || !textureResidency.Install(firstTextureHandle, {firstTexture, 4, 1}, firstTextureTicket, &textureResidencyFailure) ||
            !textureResidency.Install(secondTextureHandle, {secondTexture, 2, 1}, secondTextureTicket, &textureResidencyFailure) || firstTextureTicket.revision != 1 || secondTextureTicket.revision != 1 ||
            residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted || !textureResidency.GetInfo(firstTextureHandle, firstTextureInfo, &textureResidencyFailure) ||
            firstTextureInfo.state != rendering::TextureResidencyState::Allocated || firstTextureInfo.texture.IsValid())
            return TextureTestFailure(__LINE__);

        rendering::TextureResidencyBatch retriedTextureBatch;
        if (!textureResidency.PrepareBatch(1, retriedTextureBatch, &textureResidencyFailure) || retriedTextureBatch.installationCount != 1 ||
            !textureResidency.RetryBatch(retriedTextureBatch, &textureResidencyFailure) || textureResidency.GetStats().pendingInstallations != 2 || textureResidency.GetStats().frozenInstallations != 0)
            return TextureTestFailure(__LINE__);

        gpu::GpuFence firstTextureBatchCompletion;
        if (!SubmitTextureBatch(firstTextureBatchCompletion, 1, mixedProducerAllocation) || !firstTextureBatchCompletion.IsValid() ||
            residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted + 1 ||
            residencyLifetime.GetState(mixedProducerAllocation) != rendering::GpuSceneAllocationState::Active || !textureResidency.GetInfo(firstTextureHandle, firstTextureInfo, &textureResidencyFailure) ||
            firstTextureInfo.texture != firstTexture.GetRef() || !textureResidency.GetInfo(secondTextureHandle, secondTextureInfo, &textureResidencyFailure) ||
            secondTextureInfo.state != rendering::TextureResidencyState::Allocated || secondTextureInfo.texture.IsValid() || textureResidency.GetStats().pendingInstallations != 1)
            return TextureTestFailure(__LINE__);
        gpu::GpuFence secondTextureBatchCompletion;
        if (!SubmitTextureBatch(secondTextureBatchCompletion, textureResidencyConfig.maximumPendingInstallations) || !secondTextureBatchCompletion.IsValid() ||
            residencySceneUploader.GetStats().batchesSubmitted != uploadStatsBeforeTextureInstall.batchesSubmitted + 2 ||
            !textureResidency.GetInfo(secondTextureHandle, secondTextureInfo, &textureResidencyFailure) || secondTextureInfo.texture != secondTexture.GetRef() ||
            firstTextureInfo.descriptor.GpuIndex() == secondTextureInfo.descriptor.GpuIndex() || textureResidency.GetStats().pendingInstallations != 0)
            return TextureTestFailure(__LINE__);

        rendering::TextureResidencyInfo textureInfo;
        rendering::TextureInstallationTicket thirdTextureTicket;
        rendering::TextureInstallationTicket failedTextureTicket;
        if (!textureResidency.Install(firstTextureHandle, {thirdTexture, 0, 1}, thirdTextureTicket, &textureResidencyFailure) || thirdTextureTicket.revision != 2 ||
            textureResidency.Install(capacityHandle, {fourthTexture, 0, 1}, failedTextureTicket, &textureResidencyFailure) ||
            textureResidencyFailure.code != rendering::TextureResidencyFailureCode::DescriptorFailure || !textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.texture != firstTexture.GetRef() || textureInfo.firstResidentMip != 4 || textureInfo.installationRevision != 1 || textureResidency.GetStats().pendingInstallations != 1 ||
            !textureResidency.GetInfo(capacityHandle, textureInfo, &textureResidencyFailure) || textureInfo.state != rendering::TextureResidencyState::Allocated || textureInfo.texture.IsValid())
            return TextureTestFailure(__LINE__);

        gpu::GpuFence replacementCompletion;
        if (!SubmitTextureBatch(replacementCompletion, textureResidencyConfig.maximumPendingInstallations) || !replacementCompletion.IsValid() ||
            !textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) || textureInfo.texture != thirdTexture.GetRef() || textureInfo.firstResidentMip != 0 ||
            textureInfo.installationRevision != 2 || textureResidency.GetStats().pendingDescriptorRetirements != 1)
            return TextureTestFailure(__LINE__);

        if (textureResidency.SealRetirements({}, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::MissingRetirementFence)
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet textureReplacementFences;
        if (!SubmitFences(0x5445585245504c00ull, textureReplacementFences, failure) || !textureResidency.SealRetirements(textureReplacementFences, &textureResidencyFailure) ||
            !WaitFences(textureReplacementFences, failure) || !gpu::RetireResources(&failure))
            return TextureTestFailure(__LINE__);

        rendering::TextureInstallationTicket fourthTextureTicket;
        rendering::TextureResidencyBatch cancelledTextureBatch;
        if (!textureResidency.Install(secondTextureHandle, {fourthTexture, 0, 1}, fourthTextureTicket, &textureResidencyFailure) || fourthTextureTicket.revision != 2 ||
            !textureResidency.PrepareBatch(1, cancelledTextureBatch, &textureResidencyFailure) || cancelledTextureBatch.installationCount != 1 ||
            !textureResidency.DiscardBatch(cancelledTextureBatch, &textureResidencyFailure) || !textureResidency.GetInfo(secondTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.texture != secondTexture.GetRef() || textureInfo.installationRevision != 1)
            return TextureTestFailure(__LINE__);

        if (!textureResidency.Install(secondTextureHandle, {fourthTexture, 0, 1}, fourthTextureTicket, &textureResidencyFailure) ||
            !SubmitTextureBatch(replacementCompletion, textureResidencyConfig.maximumPendingInstallations) || !textureResidency.GetInfo(secondTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.texture != fourthTexture.GetRef() || textureInfo.installationRevision != 2 || !textureResidency.Retire(capacityHandle, &textureResidencyFailure) ||
            textureResidency.GetInfo(capacityHandle, textureInfo, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::StaleHandle ||
            !textureResidency.Retire(firstTextureHandle, &textureResidencyFailure) || !textureResidency.Retire(secondTextureHandle, &textureResidencyFailure) ||
            !residencyLifetime.Retire(mixedProducerAllocation, &residencyLifetimeFailure))
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet textureRetirementFences;
        if (!SubmitFences(0x5445585245545200ull, textureRetirementFences, failure) || !textureResidency.SealRetirements(textureRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(textureRetirementFences, &residencyLifetimeFailure) || !WaitFences(textureRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 3 || textureResidency.CollectRetirements(&textureResidencyFailure) != 2 ||
            textureResidency.GetInfo(firstTextureHandle, textureInfo, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::StaleHandle)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle streamedTextureHandle;
        rendering::TextureUploaderConfig streamedUploaderConfig;
        streamedUploaderConfig.maximumRequests = 1;
        streamedUploaderConfig.maximumAcquisitionStartsPerTick = 1;
        streamedUploaderConfig.maximumCandidatesPerBatch = 1;
        streamedUploaderConfig.maximumCompletionPollsPerTick = 1;
        streamedUploaderConfig.maximumReadyCandidates = 1;
        streamedUploaderConfig.maximumWritesPerBatch = 1;
        streamedUploaderConfig.maximumBytesPerBatch = 64;
        streamedUploaderConfig.maximumPendingCandidateBytes = 64;
        streamedUploaderConfig.maximumAcquisitionWindowBytes = 64;
        rendering::TextureUploader streamedUploader;
        rendering::TextureUploadFailure streamedUploadFailure;
        rendering::TextureUploadRequestId streamedRequest;
        if (!textureResidency.Allocate(streamedTextureHandle, &textureResidencyFailure) || !streamedUploader.Initialize(streamedUploaderConfig, &streamedUploadFailure) ||
            !streamedUploader.RequestMipTail(textureHandle, textureResidency, streamedTextureHandle, streamedRequest, vanguard::io::eAsyncPriority_Streaming, &streamedUploadFailure))
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            const rendering::TextureUploadState state = streamedUploader.GetState(streamedRequest);
            if (state == rendering::TextureUploadState::ReadyToInstall || state == rendering::TextureUploadState::Failed)
                break;
            if (!streamedUploader.Tick(&streamedUploadFailure))
                return TextureTestFailure(__LINE__);
            std::this_thread::yield();
        }
        vanguard::u32 installedCandidates = 0;
        if (streamedUploader.GetState(streamedRequest) != rendering::TextureUploadState::ReadyToInstall ||
            !streamedUploader.InstallReadyCandidates(textureResidency, 1, installedCandidates, &streamedUploadFailure) || installedCandidates != 1 ||
            streamedUploader.GetState(streamedRequest) != rendering::TextureUploadState::Invalid || !textureResidency.GetInfo(streamedTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.state != rendering::TextureResidencyState::Allocated || textureInfo.texture.IsValid() || !streamedUploader.Shutdown(&streamedUploadFailure))
            return TextureTestFailure(__LINE__);

        gpu::GpuFence streamedTableCompletion;
        if (!SubmitTextureBatch(streamedTableCompletion, 1) || !streamedTableCompletion.IsValid() || !textureResidency.GetInfo(streamedTextureHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.state != rendering::TextureResidencyState::BindlessReady || !textureInfo.texture.IsValid() || textureInfo.firstResidentMip != 0 || textureInfo.residentMipCount != 1 ||
            !textureInfo.source.IsValid() || textureInfo.source.GetGeneration() != textureHandle.GetGeneration() ||
            !(textureInfo.contentFingerprint == static_cast<textures::TextureResourceObject*>(textureHandle.Get())->GetMetadata().GetContentFingerprint()) ||
            !textureResidency.Retire(streamedTextureHandle, &textureResidencyFailure))
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet streamedRetirementFences;
        if (!SubmitFences(0x545853545245414dull, streamedRetirementFences, failure) || !textureResidency.SealRetirements(streamedRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(streamedRetirementFences, &residencyLifetimeFailure) || !WaitFences(streamedRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1)
            return TextureTestFailure(__LINE__);

        rendering::GpuTextureResidencyHandle transitionHandle;
        rendering::TextureUploaderConfig transitionUploaderConfig;
        transitionUploaderConfig.maximumRequests = 1;
        transitionUploaderConfig.maximumAcquisitionStartsPerTick = 1;
        transitionUploaderConfig.maximumCandidatesPerBatch = 1;
        transitionUploaderConfig.maximumCompletionPollsPerTick = 1;
        transitionUploaderConfig.maximumReadyCandidates = 1;
        transitionUploaderConfig.maximumWritesPerBatch = 4;
        transitionUploaderConfig.maximumCopiesPerBatch = 4;
        transitionUploaderConfig.maximumAcquisitionWindowSubresources = 4;
        transitionUploaderConfig.maximumBytesPerBatch = 512;
        transitionUploaderConfig.maximumCopyBytesPerBatch = 512;
        transitionUploaderConfig.maximumCandidateGpuBytesPerBatch = 1024ull * 1024ull;
        transitionUploaderConfig.maximumPendingCandidateBytes = 512;
        transitionUploaderConfig.maximumAcquisitionWindowBytes = 512;
        rendering::TextureUploader transitionUploader;
        rendering::TextureUploadFailure transitionFailure;
        const auto PumpTransition = [&](const rendering::TextureUploadRequestId transitionRequest, const gpu::ResidencyFenceSet& prerequisites) noexcept
        {
            for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
            {
                const rendering::TextureUploadState state = transitionUploader.GetState(transitionRequest);
                if (state == rendering::TextureUploadState::ReadyToInstall || state == rendering::TextureUploadState::Failed)
                    return true;
                if (!transitionUploader.Tick(prerequisites, &transitionFailure))
                    return false;
                std::this_thread::yield();
            }
            return false;
        };
        const auto InstallTransition = [&](gpu::GpuFence& tableCompletion) noexcept
        {
            vanguard::u32 installed = 0;
            if (!transitionUploader.InstallReadyCandidates(textureResidency, 1, installed, &transitionFailure) || installed != 1)
                return false;
            // Installation admission transfers transition ownership to residency. The identity must remain
            // immutable until the shared GPU Scene batch either accepts or discards the candidate.
            if (textureResidency.Retire(transitionHandle, &textureResidencyFailure) || textureResidencyFailure.code != rendering::TextureResidencyFailureCode::Busy)
                return false;
            return SubmitTextureBatch(tableCompletion, 1);
        };

        rendering::TextureUploadRequestId transitionRequest;
        if (!textureResidency.Allocate(transitionHandle, &textureResidencyFailure) || !transitionUploader.Initialize(transitionUploaderConfig, &transitionFailure) ||
            !transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 2, transitionRequest, vanguard::io::eAsyncPriority_Streaming, &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, {}))
            return TextureTestFailure(__LINE__);
        {
            rendering::TextureInstallationTicket forgedTransitionTicket;
            rendering::TextureInstallationDesc forgedTransitionInstallation;
            forgedTransitionInstallation.texture = fourthTexture.GetRef();
            forgedTransitionInstallation.firstResidentMip = 2;
            forgedTransitionInstallation.residentMipCount = 1;
            forgedTransitionInstallation.source = transitionTextureHandle.ToWeak();
            forgedTransitionInstallation.contentFingerprint = static_cast<textures::TextureResourceObject*>(transitionTextureHandle.Get())->GetMetadata().GetContentFingerprint();
            if (textureResidency.Install(transitionHandle, forgedTransitionInstallation, forgedTransitionTicket, &textureResidencyFailure) ||
                textureResidencyFailure.code != rendering::TextureResidencyFailureCode::Busy)
                return TextureTestFailure(__LINE__);
        }
        gpu::GpuFence tailTableCompletion;
        if (!InstallTransition(tailTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 2 ||
            textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);

        const rendering::TextureUploaderStats tailStats = transitionUploader.GetStats();
        gpu::ResidencyFenceSet promotionPrerequisites;
        promotionPrerequisites.Include(tailTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming, &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, promotionPrerequisites))
            return TextureTestFailure(__LINE__);
        gpu::GpuFence promotionTableCompletion;
        if (!InstallTransition(promotionTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 0 ||
            textureInfo.residentMipCount != 4)
            return TextureTestFailure(__LINE__);
        const rendering::TextureUploaderStats promotionStats = transitionUploader.GetStats();
        if (promotionStats.bytesSubmitted - tailStats.bytesSubmitted != 320 || promotionStats.copyBytesSubmitted - tailStats.copyBytesSubmitted != 20 ||
            promotionStats.copiesSubmitted - tailStats.copiesSubmitted != 2)
            return TextureTestFailure(__LINE__);

        gpu::ResidencyFenceSet demotionPrerequisites;
        demotionPrerequisites.Include(promotionTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 2, transitionRequest, vanguard::io::eAsyncPriority_Streaming, &transitionFailure) ||
            !transitionRequest.IsValid() || !PumpTransition(transitionRequest, demotionPrerequisites))
            return TextureTestFailure(__LINE__);
        gpu::GpuFence demotionTableCompletion;
        if (!InstallTransition(demotionTableCompletion) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) || textureInfo.firstResidentMip != 2 ||
            textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);
        const rendering::TextureUploaderStats demotionStats = transitionUploader.GetStats();
        if (demotionStats.bytesSubmitted != promotionStats.bytesSubmitted || demotionStats.copyBytesSubmitted - promotionStats.copyBytesSubmitted != 20 ||
            demotionStats.copiesSubmitted - promotionStats.copiesSubmitted != 2)
            return TextureTestFailure(__LINE__);

        gpu::ResidencyFenceSet completedTransitionCutover;
        if (!SubmitFences(0x5458544355544f56ull, completedTransitionCutover, failure) || !textureResidency.SealRetirements(completedTransitionCutover, &textureResidencyFailure) ||
            !WaitFences(completedTransitionCutover, failure) || !gpu::RetireResources(&failure))
            return TextureTestFailure(__LINE__);

        // A candidate discarded before shared table submission must leave the old installation authoritative,
        // release the manager-owned transition, and allow a later request to start normally.
        gpu::ResidencyFenceSet discardPrerequisites;
        discardPrerequisites.Include(demotionTableCompletion);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming, &transitionFailure) ||
            !transitionRequest.IsValid())
        {
            std::fprintf(stderr, "[geometryAllocatorTests] discard transition request failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!PumpTransition(transitionRequest, discardPrerequisites))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] discard transition pump failed: %s (state %u)\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message",
                         static_cast<unsigned>(transitionUploader.GetState(transitionRequest)));
            return TextureTestFailure(__LINE__);
        }
        vanguard::u32 discardedInstalled = 0;
        rendering::TextureResidencyBatch discardedTransitionBatch;
        if (!transitionUploader.InstallReadyCandidates(textureResidency, 1, discardedInstalled, &transitionFailure) || discardedInstalled != 1 ||
            !textureResidency.PrepareBatch(1, discardedTransitionBatch, &textureResidencyFailure) || discardedTransitionBatch.installationCount != 1 ||
            !textureResidency.DiscardBatch(discardedTransitionBatch, &textureResidencyFailure) || !textureResidency.GetInfo(transitionHandle, textureInfo, &textureResidencyFailure) ||
            textureInfo.firstResidentMip != 2 || textureInfo.residentMipCount != 2)
            return TextureTestFailure(__LINE__);
        if (!transitionUploader.RequestMipTransition(transitionTextureHandle, textureResidency, transitionHandle, 0, transitionRequest, vanguard::io::eAsyncPriority_Streaming, &transitionFailure) ||
            !transitionRequest.IsValid())
            return TextureTestFailure(__LINE__);
        if (!transitionUploader.Cancel(transitionRequest, &transitionFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] cancelled replacement failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!transitionUploader.Shutdown(&transitionFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] transition uploader shutdown failed: %s\n", transitionFailure.message != nullptr ? transitionFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }
        if (!textureResidency.Retire(transitionHandle, &textureResidencyFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] transition residency retirement failed: %s\n", textureResidencyFailure.message != nullptr ? textureResidencyFailure.message : "no message");
            return TextureTestFailure(__LINE__);
        }

        gpu::ResidencyFenceSet transitionRetirementFences;
        if (!SubmitFences(0x5458545245544952ull, transitionRetirementFences, failure) || !textureResidency.SealRetirements(transitionRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(transitionRetirementFences, &residencyLifetimeFailure) || !WaitFences(transitionRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1 || !textureResidency.Shutdown(&textureResidencyFailure))
            return TextureTestFailure(__LINE__);
        firstTexture.Reset();
        secondTexture.Reset();
        thirdTexture.Reset();
        fourthTexture.Reset();
        textureDescriptors.Reset();
        std::printf("Vanguard D3D12 texture upload and residency tests passed.\n");

        gpu::DescriptorDomain runtimeTextureDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        gpu::DescriptorDomain runtimeSamplerDescriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 16, 0, visibility}, &failure));
        rendering::RenderSceneManager runtimeScenes;
        rendering::RenderSceneFailure runtimeSceneFailure;
        rendering::GpuSceneRuntimeConfig gpuSceneRuntimeConfig;
        gpuSceneRuntimeConfig.tables.resourceDescriptors = runtimeTextureDescriptors;
        gpuSceneRuntimeConfig.tables.maximumPagesPerTable = 2;
        gpuSceneRuntimeConfig.upload.bytesPerSegment = 2u * 1024u * 1024u;
        gpuSceneRuntimeConfig.upload.maximumUpdatesPerBatch = 64;
        gpuSceneRuntimeConfig.upload.maximumCopiesPerBatch = 128;
        gpuSceneRuntimeConfig.definitions.maximumGeometries = 2;
        gpuSceneRuntimeConfig.definitions.maximumMaterials = 4;
        gpuSceneRuntimeConfig.definitions.maximumRenderables = 2;
        gpuSceneRuntimeConfig.definitions.maximumDefinitionsPerBatch = 6;
        gpuSceneRuntimeConfig.definitions.maximumAllocationsPerBatch = 16;
        gpuSceneRuntimeConfig.maximumExternalContributions = 2;
        rendering::GpuSceneRuntime gpuSceneRuntime;
        rendering::GpuSceneRuntimeFailure gpuSceneRuntimeFailure;
        rendering::RenderCameraStorage runtimeCameras;
        rendering::RenderCameraFailure runtimeCameraFailure;
        TextureRuntimeGpuSceneExecution gpuSceneExecution{&gpuSceneRuntime};
        rendering::RenderCommandSystem runtimeCommands;
        rendering::RenderCommandSystemConfig runtimeCommandConfig;
        runtimeCommandConfig.executeFrameTick = &ExecuteTextureRuntimeGpuSceneTick;
        runtimeCommandConfig.executeFrame = &ExecuteTextureRuntimeGpuSceneFrame;
        runtimeCommandConfig.userData = &gpuSceneExecution;
        rendering::RenderCommandFailure runtimeCommandFailure;
        rendering::TextureResidencyRuntimeConfig textureRuntimeConfig;
        textureRuntimeConfig.residency.maximumTextures = 2;
        textureRuntimeConfig.residency.maximumPendingInstallations = 2;
        textureRuntimeConfig.residency.maximumPendingRetirements = 2;
        textureRuntimeConfig.uploader.maximumRequests = 2;
        textureRuntimeConfig.uploader.maximumAcquisitionStartsPerTick = 2;
        textureRuntimeConfig.uploader.maximumCandidatesPerBatch = 2;
        textureRuntimeConfig.uploader.maximumCompletionPollsPerTick = 2;
        textureRuntimeConfig.uploader.maximumReadyCandidates = 2;
        textureRuntimeConfig.uploader.maximumWritesPerBatch = 8;
        textureRuntimeConfig.uploader.maximumCopiesPerBatch = 8;
        textureRuntimeConfig.maximumResidencyRecords = 2;
        textureRuntimeConfig.maximumDemands = 4;
        textureRuntimeConfig.maximumInstallationsPerTick = 2;
        textureRuntimeConfig.maximumTableInstallationsPerFrame = 2;
        textureRuntimeConfig.maximumStateChecksPerTick = 2;
        rendering::TextureResidencyRuntime textureRuntime;
        rendering::TextureResidencyRuntimeFailure textureRuntimeFailure;
        rendering::TextureDemandHandle firstTextureDemand;
        rendering::TextureDemandHandle secondTextureDemand;
        if (!runtimeTextureDescriptors || !runtimeSamplerDescriptors || !runtimeScenes.Initialize({}, &runtimeSceneFailure) ||
            !gpuSceneRuntime.Initialize(runtimeScenes, gpuSceneRuntimeConfig, &gpuSceneRuntimeFailure) || !runtimeCameras.Initialize(runtimeScenes, {}, &runtimeCameraFailure) ||
            !runtimeCommands.Initialize(runtimeScenes, runtimeCameras, runtimeCommandConfig, &runtimeCommandFailure) ||
            !textureRuntime.Initialize(gpuSceneRuntime.GetLifetime(), runtimeTextureDescriptors, textureRuntimeConfig, &textureRuntimeFailure) ||
            !textureRuntime.RequestTexture(textureHandle, firstTextureDemand, &textureRuntimeFailure) || !textureRuntime.RequestTexture(textureHandle, secondTextureDemand, &textureRuntimeFailure) ||
            firstTextureDemand.GetResidency() != secondTextureDemand.GetResidency())
            return TextureTestFailure(__LINE__);
        const rendering::GpuTextureResidencyHandle runtimeTexture = firstTextureDemand.GetResidency();
        rendering::TextureRuntimeInfo runtimeTextureInfo;
        if (!textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) || runtimeTextureInfo.state != rendering::TextureRuntimeState::MipTailLoading ||
            runtimeTextureInfo.demandCount != 2 || textureRuntime.GetStats().demandsCoalesced != 1)
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            if (!textureRuntime.Tick(&textureRuntimeFailure) || !textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure))
                return TextureTestFailure(__LINE__);
            if (runtimeTextureInfo.state == rendering::TextureRuntimeState::InstallationPending || runtimeTextureInfo.state == rendering::TextureRuntimeState::Failed)
                break;
            std::this_thread::yield();
        }
        if (runtimeTextureInfo.state != rendering::TextureRuntimeState::InstallationPending || textureRuntime.GetStats().residency.pendingInstallations != 1)
            return TextureTestFailure(__LINE__);
        rendering::MaterialResourceResolver materialResources;
        rendering::MaterialResourceResolverFailure materialResourceFailure;
        rendering::MaterialResourceResolveRequest textureRoleRequest;
        textureRoleRequest.role.name = 0x746578747572655full;
        textureRoleRequest.role.kind = shaders::MaterialResourceKind::Texture;
        textureRoleRequest.role.flags = shaders::MaterialResourceFlags::Required;
        textureRoleRequest.role.typeFingerprint.bytes[0] = 1;
        textureRoleRequest.role.shape.access = shaders::MaterialResourceAccess::Read;
        textureRoleRequest.role.shape.textureDimension = shaders::MaterialTextureDimension::D2;
        textureRoleRequest.role.shape.componentCount = 4;
        textureRoleRequest.expectedAssetType = textures::TextureResourceType;
        textureRoleRequest.dependency = resources::DependencyKind::Required;
        textureRoleRequest.resource = textureHandle;
        rendering::MaterialResourceResolveTicket textureRoleTicket;
        rendering::MaterialResourceReference textureRoleReference;
        rendering::MaterialResourceResolverConfig materialResourceConfig;
        materialResourceConfig.maximumProviders = 8;
        materialResourceConfig.maximumFallbacks = 8;
        materialResourceConfig.maximumOperations = 16;
        materialResourceConfig.maximumDescriptorCacheEntries = 2;
        if (!materialResources.Initialize(textureRuntime, runtimeTextureDescriptors, runtimeSamplerDescriptors, materialResourceConfig, &materialResourceFailure) ||
            !materialResources.Begin(textureRoleRequest, textureRoleTicket, &materialResourceFailure) ||
            materialResources.Poll(textureRoleTicket, textureRoleReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Pending || !textureRoleTicket.IsValid() ||
            textureRoleReference.IsValid())
            return TextureTestFailure(__LINE__);
        rendering::RenderCommandFrameTickResult runtimeTickResult;
        if (!textureRuntime.StageGpuSceneContribution(gpuSceneRuntime, &textureRuntimeFailure) ||
            !runtimeCommands.FrameTick(runtimeScenes.GetFramePipelineScenes(), 1, runtimeTickResult, &runtimeCommandFailure) || !runtimeCommands.FlushPreviousFrameProcessing(&runtimeCommandFailure) ||
            !gpuSceneRuntime.ResolveContributions(&gpuSceneRuntimeFailure) || runtimeCommands.ConsumeExecutionFailure(runtimeCommandFailure) ||
            gpuSceneRuntime.ConsumePublicationFailure(gpuSceneRuntimeFailure) || !textureRuntime.Tick(&textureRuntimeFailure) ||
            !textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) || runtimeTextureInfo.state != rendering::TextureRuntimeState::BindlessReady)
            return TextureTestFailure(__LINE__);

        if (materialResources.Poll(textureRoleTicket, textureRoleReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready || !textureRoleReference.IsValid() ||
            textureRoleReference.GetIdentity().index != runtimeTexture.index || textureRoleReference.GetIdentity().generation != runtimeTexture.generation ||
            textureRoleReference.GetGpuResource().resource != runtimeTexture.index || textureRoleReference.GetGpuResource().type != rendering::GpuMaterialResourceType::Texture)
            return TextureTestFailure(__LINE__);

        rendering::TextureDemandHandle closureTextureDemand;
        if (!textureRuntime.RequestTexture(transitionTextureHandle, closureTextureDemand, &textureRuntimeFailure))
            return TextureTestFailure(__LINE__);
        rendering::TextureRuntimeInfo closureTextureInfo;
        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            if (!textureRuntime.Tick(&textureRuntimeFailure) || !textureRuntime.GetInfo(closureTextureDemand.GetResidency(), closureTextureInfo, &textureRuntimeFailure))
                return TextureTestFailure(__LINE__);
            if (closureTextureInfo.state == rendering::TextureRuntimeState::InstallationPending || closureTextureInfo.state == rendering::TextureRuntimeState::Failed)
                break;
            std::this_thread::yield();
        }
        if (closureTextureInfo.state != rendering::TextureRuntimeState::InstallationPending ||
            !vanguard::rendering::tests::RunMaterialResourceResolverProof(materialResources, textureRuntime, gpuSceneRuntime, runtimeCommands, runtimeScenes, meshHandle, runtimeTextureDescriptors,
                                                                          runtimeSamplerDescriptors) ||
            !textureRuntime.GetInfo(closureTextureDemand.GetResidency(), closureTextureInfo, &textureRuntimeFailure) || closureTextureInfo.state != rendering::TextureRuntimeState::BindlessReady)
            return TextureTestFailure(__LINE__);
        const rendering::GpuSceneRuntimeStats gpuSceneRuntimeStats = gpuSceneRuntime.GetStats();
        if (gpuSceneRuntimeStats.publicationTicks != 19 || gpuSceneRuntimeStats.submittedBatches != 14 || gpuSceneRuntimeStats.stagedContributions != 8 || gpuSceneRuntimeStats.acceptedContributions != 8 ||
            gpuSceneRuntimeStats.retriedContributions != 0)
        {
            std::fprintf(stderr, "[geometryAllocatorTests] GPU Scene totals: ticks=%llu batches=%llu staged=%llu accepted=%llu retried=%llu\n", gpuSceneRuntimeStats.publicationTicks,
                         gpuSceneRuntimeStats.submittedBatches, gpuSceneRuntimeStats.stagedContributions, gpuSceneRuntimeStats.acceptedContributions, gpuSceneRuntimeStats.retriedContributions);
            return TextureTestFailure(__LINE__);
        }
        firstTextureDemand.Reset();
        if (!textureRuntime.GetInfo(runtimeTexture, runtimeTextureInfo, &textureRuntimeFailure) || runtimeTextureInfo.demandCount != 2)
            return TextureTestFailure(__LINE__);
        secondTextureDemand.Reset();
        closureTextureDemand.Reset();
        textureRoleReference.Reset();
        textureRoleRequest.resource.Reset();
        if (!materialResources.Shutdown(&materialResourceFailure))
            return TextureTestFailure(__LINE__);
        if (!textureRuntime.Tick(&textureRuntimeFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture runtime retirement tick failed: code=%u message=%s lower=%u upload=%u\n", static_cast<unsigned>(textureRuntimeFailure.code),
                         textureRuntimeFailure.message != nullptr ? textureRuntimeFailure.message : "", static_cast<unsigned>(textureRuntimeFailure.residencyFailure.code),
                         static_cast<unsigned>(textureRuntimeFailure.uploadFailure.code));
            return TextureTestFailure(__LINE__);
        }
        gpu::ResidencyFenceSet runtimeRetirementFences;
        rendering::GpuSceneLifetimeFailure runtimeLifetimeFailure;
        if (!SubmitFences(0x54585452554e3542ull, runtimeRetirementFences, failure) || !textureRuntime.SealRetirements(runtimeRetirementFences, &textureRuntimeFailure) ||
            !gpuSceneRuntime.GetLifetime().SealRetirements(runtimeRetirementFences, &runtimeLifetimeFailure) || !WaitFences(runtimeRetirementFences, failure) || !gpu::RetireResources(&failure))
            return TextureTestFailure(__LINE__);
        const vanguard::u32 collectedSceneAllocations = gpuSceneRuntime.GetLifetime().Collect(&runtimeLifetimeFailure);
        const vanguard::u32 collectedTextures = textureRuntime.CollectRetirements(&textureRuntimeFailure);
        if (collectedSceneAllocations != 20 || collectedTextures != 2 || !textureRuntime.Tick(&textureRuntimeFailure))
        {
            std::fprintf(stderr, "[material3C2] retirement collection scene=%u textures=%u\n", collectedSceneAllocations, collectedTextures);
            return TextureTestFailure(__LINE__);
        }
        const rendering::TextureResidencyRuntimeStats textureRuntimeStats = textureRuntime.GetStats();
        if (textureRuntimeStats.residencyRecords != 0 || textureRuntimeStats.liveDemands != 0 || !textureRuntime.Shutdown(&textureRuntimeFailure) || !runtimeCommands.Shutdown(&runtimeCommandFailure) ||
            !gpuSceneRuntime.Shutdown({}, &gpuSceneRuntimeFailure))
        {
            std::fprintf(stderr, "[geometryAllocatorTests] texture runtime cleanup failed: records=%u demands=%u manager=%u requests=%u code=%u message=%s\n", textureRuntimeStats.residencyRecords,
                         textureRuntimeStats.liveDemands, textureRuntimeStats.residency.liveResidencies, textureRuntimeStats.uploader.liveRequests, static_cast<unsigned>(textureRuntimeFailure.code),
                         textureRuntimeFailure.message != nullptr ? textureRuntimeFailure.message : "");
            return TextureTestFailure(__LINE__);
        }

        rendering::GpuSceneRuntime abandonedGpuScene;
        rendering::TextureResidencyRuntime abandonedTextureRuntime;
        rendering::TextureDemandHandle abandonedTextureDemand;
        if (!abandonedGpuScene.Initialize(runtimeScenes, gpuSceneRuntimeConfig, &gpuSceneRuntimeFailure) ||
            !abandonedTextureRuntime.Initialize(abandonedGpuScene.GetLifetime(), runtimeTextureDescriptors, textureRuntimeConfig, &textureRuntimeFailure) ||
            !abandonedTextureRuntime.RequestTexture(transitionTextureHandle, abandonedTextureDemand, &textureRuntimeFailure) || !abandonedTextureDemand.IsValid() ||
            !abandonedTextureRuntime.AbandonDevice(&textureRuntimeFailure) || abandonedTextureDemand.IsValid() || !abandonedGpuScene.AbandonDevice(&gpuSceneRuntimeFailure))
            return TextureTestFailure(__LINE__);
        abandonedTextureDemand.Reset();
        if (!runtimeCameras.Shutdown(&runtimeCameraFailure) || !runtimeScenes.Shutdown(&runtimeSceneFailure))
            return TextureTestFailure(__LINE__);
        runtimeTextureDescriptors.Reset();
        runtimeSamplerDescriptors.Reset();
        std::printf("Vanguard texture runtime ownership, demand, and GPU Scene installation tests passed.\n");

        rendering::MeshResidencyConfig residencyConfig;
        residencyConfig.geometryAllocator.verticesPerArena = 64;
        residencyConfig.geometryAllocator.indexBytesPerArena = 1024;
        residencyConfig.geometryAllocator.maximumCommittedVertexBytes = 1024 * 1024;
        residencyConfig.geometryAllocator.maximumCommittedIndexBytes = 1024 * 1024;
        residencyConfig.geometryAllocator.maximumVertexArenas = 4;
        residencyConfig.geometryAllocator.maximumIndexArenas = 4;
        residencyConfig.geometryAllocator.maximumAllocations = 16;
        residencyConfig.geometryUpload.bytesPerSegment = 1024;
        residencyConfig.geometryUpload.maximumOverflowBytes = 4096;
        residencyConfig.geometryUpload.segmentCount = 3;
        residencyConfig.geometryUpload.maximumGeometriesPerBatch = 8;
        residencyConfig.geometryUpload.maximumCopiesPerBatch = 32;
        residencyConfig.lodUpload.maximumRequests = 4;
        residencyConfig.lodUpload.maximumLodsPerTick = 1;
        residencyConfig.lodUpload.maximumUploadBytesPerTick = 4096;
        residencyConfig.lodUpload.maximumPendingUploadBytes = 4096;
        residencyConfig.lodUpload.maximumPreparationDeferrals = 4;
        residencyConfig.maximumResidencyRecords = 2;
        residencyConfig.maximumDemands = 8;
        rendering::MeshResidencyManager residency;
        rendering::MeshResidencyFailure residencyFailure;
        rendering::MeshDemandHandle firstDemand;
        rendering::MeshDemandHandle secondDemand;
        if (!residency.Initialize(residencyDefinitions, residencyConfig, &residencyFailure) || !residency.RequestMesh(meshHandle, firstDemand, &residencyFailure) ||
            !residency.RequestMesh(meshHandle, secondDemand, &residencyFailure) || firstDemand.GetResidency() != secondDemand.GetResidency())
        {
            std::printf("[geometryAllocatorTests] residency demand setup failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        rendering::MeshResidencyInfo residencyInfo;
        const rendering::MeshResidencyHandle residencyHandle = firstDemand.GetResidency();
        if (!residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MeshResidencyState::MetadataRetained ||
            residencyInfo.resourceGeneration != meshHandle.GetGeneration() || residencyInfo.anchorLod != 0 || residencyInfo.demandCount != 2 || residency.GetStats().demandsCoalesced != 1 ||
            residency.Shutdown(&residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::LiveResidencyRemains || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure))
        {
            std::printf("[geometryAllocatorTests] residency coalescing/lifetime checks failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }

        std::array<rendering::MeshDemandHandle, 4> concurrentDemands;
        bool concurrentResults[4]{};
        std::thread demandThreads[4];
        for (vanguard::u32 index = 0; index < 4; ++index)
            demandThreads[index] =
                std::thread([&residency, &meshHandle, &concurrentDemands, &concurrentResults, index]() noexcept { concurrentResults[index] = residency.RequestMesh(meshHandle, concurrentDemands[index]); });
        for (std::thread& thread : demandThreads)
            thread.join();
        if (!concurrentResults[0] || !concurrentResults[1] || !concurrentResults[2] || !concurrentResults[3] || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) ||
            residencyInfo.demandCount != 6 || residency.GetStats().demandsCoalesced != 5)
            return TextureTestFailure(__LINE__);
        for (rendering::MeshDemandHandle& concurrentDemand : concurrentDemands)
            concurrentDemand.Reset();
        if (!residency.CancelDemand(firstDemand, &residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.demandCount != 1)
            return TextureTestFailure(__LINE__);

        for (vanguard::u32 poll = 0; poll < 100'000; ++poll)
        {
            if (!residency.Tick(&residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure))
                return TextureTestFailure(__LINE__);
            if (residencyInfo.state == rendering::MeshResidencyState::AnchorLodDefinitionsSubmitted || residencyInfo.state == rendering::MeshResidencyState::Failed)
                break;
            std::this_thread::yield();
        }
        if (residencyInfo.state != rendering::MeshResidencyState::AnchorLodDefinitionsSubmitted || residencyInfo.geometryCount != 1 || residencyInfo.vertexBytes != 40 || residencyInfo.indexBytes != 6)
            return TextureTestFailure(__LINE__);
        const rendering::GpuSceneDefinitionsStats residencyDefinitionStats = residencyDefinitions.GetStats();
        if (residencyDefinitionStats.geometries != 1 || residencyDefinitionStats.references != 1)
            return TextureTestFailure(__LINE__);

        secondDemand.Reset();
        if (!residency.Tick(&residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MeshResidencyState::Retiring)
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet residencyRetirementFences;
        if (!SubmitFences(0x4d45534852455300ull, residencyRetirementFences, failure) || !residency.SealRetirements(residencyRetirementFences, &residencyFailure) ||
            !residencyLifetime.SealRetirements(residencyRetirementFences, &residencyLifetimeFailure) || !WaitFences(residencyRetirementFences, failure) || residency.CollectRetirements(&residencyFailure) != 1 ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 2)
            return TextureTestFailure(__LINE__);
        if (residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::InvalidArgument || residency.GetStats().residencyRecords != 0 ||
            residency.GetStats().liveDemands != 0 || !residency.Shutdown(&residencyFailure))
            return TextureTestFailure(__LINE__);
        rendering::MeshResidencyManager abandonedMeshResidency;
        rendering::MeshDemandHandle abandonedMeshDemand;
        if (!abandonedMeshResidency.Initialize(residencyDefinitions, residencyConfig, &residencyFailure) || !abandonedMeshResidency.RequestMesh(meshHandle, abandonedMeshDemand, &residencyFailure) ||
            !abandonedMeshDemand.IsValid() || !abandonedMeshResidency.AbandonDevice(&residencyFailure) || abandonedMeshDemand.IsValid())
            return TextureTestFailure(__LINE__);
        abandonedMeshDemand.Reset();
        if (!residencyDefinitions.Shutdown(&residencyDefinitionFailure) || !residencySceneUploader.Shutdown(&residencySceneUploadFailure) || !residencyLifetime.Shutdown(&residencyLifetimeFailure) ||
            !residencyTables.Shutdown({}, &residencyTableFailure))
        {
            std::printf("[geometryAllocatorTests] residency teardown checks failed: code=%u message=%s\n", static_cast<unsigned>(residencyFailure.code),
                        residencyFailure.message != nullptr ? residencyFailure.message : "");
            return false;
        }
        residencyDescriptors.Reset();

        rendering::MeshLodGeometryUploader geometryUploader;
        rendering::MeshLodGeometryUploadFailure geometryUploadFailure;
        rendering::MeshLodGeometryUploaderConfig uploaderConfig;
        uploaderConfig.maximumRequests = 4;
        uploaderConfig.maximumLodsPerTick = 1;
        uploaderConfig.maximumUploadBytesPerTick = 1024;
        uploaderConfig.maximumPendingUploadBytes = 4096;
        uploaderConfig.maximumPreparationDeferrals = 8;
        rendering::MeshLodGeometryUploadRequestId geometryUploadRequest;
        rendering::MeshLodGeometryUploadRequestId coalescedRequest;
        if (!geometryUploader.Initialize(allocator, uploader, uploaderConfig, &geometryUploadFailure) ||
            !geometryUploader.Request(meshHandle, 0, geometryUploadRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure) ||
            !geometryUploader.Request(meshHandle, 0, coalescedRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure) || geometryUploadRequest != coalescedRequest ||
            geometryUploader.GetStats().requestsCoalesced != 1)
            return TextureTestFailure(__LINE__);
        if (!geometryUploader.Cancel(coalescedRequest, &geometryUploadFailure) || geometryUploader.GetState(geometryUploadRequest) == rendering::MeshLodGeometryUploadState::Invalid)
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000 && geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Complete &&
                                     geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Failed;
             ++poll)
        {
            if (!geometryUploader.Tick(&geometryUploadFailure))
                return TextureTestFailure(__LINE__);
            std::this_thread::yield();
        }
        if (geometryUploader.GetState(geometryUploadRequest) != rendering::MeshLodGeometryUploadState::Complete)
            return TextureTestFailure(__LINE__);

        rendering::PendingMeshLodGeometry pendingGeometry;
        if (!geometryUploader.TakeCompleted(geometryUploadRequest, pendingGeometry, &geometryUploadFailure) || !pendingGeometry.IsValid() || pendingGeometry.pages.Size() != 5 ||
            pendingGeometry.geometries.Size() != 1 || pendingGeometry.submeshes.Size() != 1 || pendingGeometry.vertexBytes != 40 || pendingGeometry.indexBytes != 6 ||
            geometryUploader.GetStats().liveRequests != 0 || !gpu::WaitForGpuFence(pendingGeometry.copyCompletion, 5'000'000'000ull, &failure))
            return TextureTestFailure(__LINE__);
        if (!rendering::AbortPendingMeshLodGeometry(pendingGeometry, allocator, &allocatorFailure))
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet uploadRetirementFences;
        if (!SubmitFences(0x4d455348494e5300ull, uploadRetirementFences, failure) || !WaitFences(uploadRetirementFences, failure) || !allocator.SealRetirements(uploadRetirementFences, &allocatorFailure) ||
            allocator.Collect(&allocatorFailure) != 1)
            return TextureTestFailure(__LINE__);

        rendering::MeshLodGeometryUploadRequestId staleRequest;
        if (!geometryUploader.Request(meshHandle, 0, staleRequest, vanguard::io::eAsyncPriority_Streaming, &geometryUploadFailure))
            return TextureTestFailure(__LINE__);
        meshHandle.Reset();
        if (!registry.Evict(meshReference.GetPath()))
            return TextureTestFailure(__LINE__);
        for (vanguard::u32 poll = 0; poll < 100'000 && geometryUploader.GetState(staleRequest) != rendering::MeshLodGeometryUploadState::Failed; ++poll)
        {
            if (!geometryUploader.Tick(&geometryUploadFailure))
                return TextureTestFailure(__LINE__);
            std::this_thread::yield();
        }
        rendering::MeshLodGeometryUploadFailure staleFailure;
        if (geometryUploader.GetState(staleRequest) != rendering::MeshLodGeometryUploadState::Failed || !geometryUploader.GetRequestFailure(staleRequest, staleFailure) ||
            staleFailure.code != rendering::MeshLodGeometryUploadFailureCode::StaleResourceGeneration || !geometryUploader.Cancel(staleRequest, &geometryUploadFailure))
            return TextureTestFailure(__LINE__);
        if (!geometryUploader.Shutdown(&geometryUploadFailure))
            return TextureTestFailure(__LINE__);

        firstTextureInfo = {};
        secondTextureInfo = {};
        textureInfo = {};
        materialHandle.Reset();
        textureHandle.Reset();
        transitionTextureHandle.Reset();
        meshRequest.Reset();
        textureRequest.Reset();
        transitionTextureRequest.Reset();
        materialRequest.Reset();
        if (!registry.Evict(textureReference.GetPath()) || !registry.Evict(transitionTextureReference.GetPath()) || !registry.Evict(materialReference.GetPath()) || !registry.Shutdown())
            return TextureTestFailure(__LINE__);
        static_cast<void>(fileManager.DeleteFile(meshPath));
        static_cast<void>(fileManager.DeleteFile(texturePath));
        static_cast<void>(fileManager.DeleteFile(transitionTexturePath));
        static_cast<void>(fileManager.DeletePath(testDirectory));
        mesh.Close();

        const rendering::GeometryAllocatorStats stats = allocator.GetStats();
        if (stats.reservedAllocations != 0 || stats.activeAllocations != 0 || stats.retiringAllocations != 0 || stats.retirements != 3 || stats.reclaimed != 3 || !uploader.Shutdown(&uploadFailure) ||
            !allocator.Shutdown(&allocatorFailure))
            return TextureTestFailure(__LINE__);
        return gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
    }
} // namespace

int main()
{
    if (!vanguard::memory::Initialize() || !vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "geometryAllocatorTests") || !vanguard::containers::Initialize() ||
        !vanguard::io::Initialize())
        return 1;
    const vanguard::filesystem::AbsolutePath root = vanguard::filesystem::paths::GetCurrentWorkingDirectory();
    if (!vanguard::filesystem::Initialize({root, root, root}) || !vanguard::jobs::Initialize(vanguard::jobs::ToolConfig()))
        return 1;

    vanguard::rhi::d3d12::Backend backend;
    vanguard::rhi::Failure failure;
    vanguard::rhi::DeviceParams params;
#if VG_ENABLE_ASSERTS
    params.enableValidation = true;
#endif
    if (!vanguard::rhi::Initialize(backend, params, &failure))
    {
        const bool unsupported = failure.code == vanguard::rhi::FailureCode::Unsupported;
        static_cast<void>(vanguard::jobs::Shutdown());
        vanguard::filesystem::Shutdown();
        vanguard::io::Shutdown();
        vanguard::diagnostics::Shutdown();
        return unsupported ? 0 : 1;
    }

    const bool passed = RunTests(failure);
    if (!passed)
        std::printf("[geometryAllocatorTests] failed: %s\n", failure.message);
    static_cast<void>(vanguard::rhi::WaitIdle());
    static_cast<void>(vanguard::rhi::RetireResources());
    const bool shutdown = vanguard::rhi::Shutdown(&failure) && vanguard::jobs::Shutdown();
    vanguard::filesystem::Shutdown();
    vanguard::io::Shutdown();
    vanguard::diagnostics::Shutdown();
    return passed && shutdown ? 0 : 1;
}
