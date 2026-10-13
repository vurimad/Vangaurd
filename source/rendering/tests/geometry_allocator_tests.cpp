#include "../../../tests/support/scratch_directory.hpp"
#include <vanguard/concurrency/atomic.hpp>
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
#include <vanguard/rendering/render_graph_cache.hpp>
#include <vanguard/rendering/render_node_graph_factory.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>
#include <vanguard/rendering/texture_uploader.hpp>
#include <vanguard/rendering/viewport.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include "../private/vanguard/rendering/render_flow_resource_placed.hpp"
#include "../private/vanguard/rendering/render_flow_resource_pool.hpp"
#include "../private/vanguard/rendering/texture_output_receipt_state.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

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

    [[noreturn]] void RunRhiUninitializedShutdownDestructorProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptorOwner(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
        if (!descriptorOwner.IsValid())
            std::_Exit(2);

        auto* const allocator = new rendering::RenderFlowResourceAllocator();
        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = descriptorOwner.GetRef();
        config.descriptorCacheTarget = 4;
        config.descriptorCacheMaximum = 4;
        if (!allocator->Initialize(config, &failure))
            std::_Exit(3);
        descriptorOwner.Reset();

        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{91};
        constexpr rendering::GpuFlowGroupId flow{91};
        constexpr rendering::CommandScopeId scope{91};
        constexpr rendering::FlowSpaceId space{91};
        rendering::FrameBufferDesc desc;
        desc.active.size = 4096;
        desc.active.structureStride = 16;
        desc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.maximumSize = desc.active.size;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId use;
        rendering::BufferUseDesc useDesc;
        useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
        useDesc.access = rendering::LogicalAccessIntent::Write;
        useDesc.content = rendering::ResourceContentIntent::Discard;
        if (!allocator->BeginFrame(1, {}, &failure) || !allocator->CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "rf2.rhi-uninitialized.shutdown"}, desc, resource, &failure) || !writer.BeginBufferUse(resource, useDesc, use, &failure) ||
            !writer.EndUse(use, &failure) || !writer.Close(&failure) || !allocator->SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator->Resolve(nullptr, &failure))
            std::_Exit(4);
        const rendering::ExecutionGenerationId generation = allocator->GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator->Finish(terminal, &failure))
            std::_Exit(5);
        const rendering::RenderFlowResourceAllocatorStats before = allocator->GetStats();
        if (before.state != rendering::RenderFlowResourceSessionState::Idle || before.canonicalDescriptorLiveSlots != 2 || before.chargedNativeBytes == 0)
            std::_Exit(6);

        if (!gpu::AbandonDevice(&rhiFailure) || gpu::IsInitialized())
            std::_Exit(7);
        if (allocator->Shutdown(&failure) || failure.code != rendering::RenderFlowResourceFailureCode::NotInitialized || !allocator->IsInitialized())
            std::_Exit(8);
        const rendering::RenderFlowResourceAllocatorStats after = allocator->GetStats();
        if (after.canonicalDescriptorLiveSlots != before.canonicalDescriptorLiveSlots || after.chargedNativeBytes != before.chargedNativeBytes ||
            after.dedicatedResourcePoolHits != before.dedicatedResourcePoolHits || after.dedicatedResourcePoolMisses != before.dedicatedResourcePoolMisses)
            std::_Exit(9);

        // The backend is gone while allocator-owned RHI refs remain. The
        // destructor must strand its implementation and terminate rather than
        // destructing those stale refs. The parent expects VG_FATAL's fast-fail.
        delete allocator;
        std::_Exit(10);
    }

    [[nodiscard]] bool RunRhiUninitializedShutdownProofSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;
        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" --rf2-rhi-uninitialized-shutdown-proof", executable);
        if (commandLength <= 0 || static_cast<vanguard::usize>(commandLength) >= sizeof(commandLine))
            return false;
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0xc0000602u;
#else
        return true;
#endif
    }

    // The stale allocator deliberately outlives the abandoned RHI. Run in a
    // subprocess so its still-owned old-incarnation RHI refs are never released
    // through the replacement backend by a C++ destructor.
    [[noreturn]] void RunRF11RetainedImportIncarnationProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                          gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 1, 0, visibility}, &rhiFailure));
        gpu::BufferDesc desc;
        desc.size = 1024;
        desc.structureStride = 16;
        desc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        desc.initialState = gpu::ResourceState::Common;
        gpu::Buffer native(gpu::AdoptReference, gpu::CreateBuffer(desc, {}, &rhiFailure));
        auto* allocator = new rendering::RenderFlowResourceAllocator();
        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = descriptors.GetRef();
        config.descriptorCacheTarget = 1;
        config.descriptorCacheMaximum = 1;
        rendering::RetainedBufferImportDesc importDesc;
        importDesc.token = {0x5246313141424141ull};
        importDesc.buffer = native.GetRef();
        importDesc.expected = desc;
        importDesc.initialState = gpu::ResourceState::Common;
        importDesc.terminalState = gpu::ResourceState::Common;
        rendering::ImportedResourceId imported;
        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId logical;
        rendering::ResourceUseId use;
        rendering::BufferUseDesc read;
        read.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        read.access = rendering::LogicalAccessIntent::Read;
        read.content = rendering::ResourceContentIntent::Preserve;
        constexpr rendering::RenderFlowNodeId node{0xA11};
        constexpr rendering::GpuFlowGroupId flow{0xA11};
        constexpr rendering::CommandScopeId scope{0xA11};
        constexpr rendering::FlowSpaceId space{0xA11};
        if (!descriptors.IsValid() || !native.IsValid() || !allocator->Initialize(config, &failure) || !allocator->BeginFrame(1011, {}, &failure) ||
            !allocator->RegisterImport(importDesc, imported, &failure) || !allocator->CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.ImportBuffer({space, "rf11.restart.retained"}, imported, logical, &failure) ||
            !writer.BeginBufferUse(logical, read, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
            !allocator->SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator->Resolve(nullptr, &failure) ||
            !allocator->BeginExecution(&failure))
            std::_Exit(20);
        const rendering::ExecutionGenerationId generation = allocator->GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator->Finish(terminal, &failure) || allocator->GetStats().canonicalDescriptorLiveSlots != 1)
            std::_Exit(21);
        const vanguard::u64 oldIncarnation = gpu::GetDeviceIncarnation();
        if (oldIncarnation == 0 || !gpu::AbandonDevice(&rhiFailure))
            std::_Exit(22);
        gpu::d3d12::Backend replacement;
        gpu::DeviceParams params;
#if VG_ENABLE_ASSERTS
        params.enableValidation = true;
#endif
        if (!gpu::Initialize(replacement, params, &rhiFailure) || gpu::GetDeviceIncarnation() == oldIncarnation)
            std::_Exit(23);
        // Even if a newly created descriptor domain/native buffer reuses the
        // old slot indices (ABA), it cannot authorize the persistent old owner.
        gpu::DescriptorDomain replacementDomain(gpu::AdoptReference,
                                                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 1, 0, visibility}, &rhiFailure));
        gpu::Buffer replacementNative(gpu::AdoptReference, gpu::CreateBuffer(desc, {}, &rhiFailure));
        if (!replacementDomain.IsValid() || !replacementNative.IsValid())
            std::_Exit(24);
        if (allocator->ClearPersistentCaches(&failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity ||
            allocator->Shutdown(&failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity)
            std::_Exit(25);
        // The allocator intentionally retains old-incarnation refs. Never run
        // its destructor against the replacement device.
        std::_Exit(0);
    }

    [[nodiscard]] bool RunRF11RetainedImportIncarnationProofSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;
        char commandLine[MAX_PATH + 128]{};
        const int length = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" --rf11-retained-incarnation-proof", executable);
        if (length <= 0 || static_cast<vanguard::usize>(length) >= sizeof(commandLine))
            return false;
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0;
#else
        return true;
#endif
    }

    // Compile one real, descriptor-domain-scoped semantic contract in the
    // caller's already-open frame. No GPU work is submitted, so an aborted
    // execution with a discarded command scope is the precise terminal proof.
    [[nodiscard]] bool CompileAndFinishRF12ScopeContract(rendering::RenderFlowResourceAllocator& allocator,
                                                         rendering::RenderFlowResourceContract& contract,
                                                         rendering::RenderNodeGraph& graph,
                                                         rendering::RenderNodeResourceBindings& bindings,
                                                         rendering::RenderFlowResourceFailure& failure) noexcept
    {
        constexpr rendering::RenderFlowNodeId node{0xC12};
        constexpr rendering::GpuFlowGroupId flow{0xC12};
        constexpr rendering::CommandScopeId scope{0xC12};
        constexpr rendering::FlowSpaceId space{0xC12};
        rendering::FrameBufferDesc desc;
        desc.active.size = 1024;
        desc.active.structureStride = 16;
        desc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.maximumSize = desc.active.size;
        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId use;
        if (!allocator.BeginResourceContractCompilation(contract, &failure) ||
            !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "rf12.scope.epoch.buffer"}, desc, resource, &failure) ||
            !writer.BeginBufferUse(resource, write, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
            !graph.CaptureResourceContract(contract, bindings, allocator, &failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return false;
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {
            {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        return contract.IsValid() && contract.GetPublicationSerial() != 0 && allocator.Finish(terminal, &failure) &&
               allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle;
    }

    // The prior RF11 child specifically covers stale retained descriptor owners.
    // Keep that proof intact; this separate child tests the immutable RF12
    // resource contract across a real RHI incarnation change.
    [[noreturn]] void RunRF12ContractIncarnationProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) |
                                                gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain oldDomain(gpu::AdoptReference,
                                        gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
        auto* const oldAllocator = new rendering::RenderFlowResourceAllocator();
        rendering::RenderFlowResourceAllocatorConfig oldConfig;
        oldConfig.resourceDescriptors = oldDomain.GetRef();
        oldConfig.descriptorCacheTarget = 4;
        oldConfig.descriptorCacheMaximum = 4;
        rendering::RenderFlowResourceContract contract;
        rendering::RenderNodeGraph graph;
        graph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings bindings;
        if (!oldDomain.IsValid() || !oldAllocator->Initialize(oldConfig, &failure) ||
            !oldAllocator->BeginFrame(0xC120, {}, &failure) ||
            !CompileAndFinishRF12ScopeContract(*oldAllocator, contract, graph, bindings, failure) ||
            oldAllocator->GetStats().structuralPlanBuilds != 1)
            std::_Exit(40);
        const vanguard::u64 oldPublication = contract.GetPublicationSerial();
        const vanguard::u64 oldIncarnation = gpu::GetDeviceIncarnation();
        if (oldPublication == 0 || oldIncarnation == 0 || !gpu::AbandonDevice(&rhiFailure))
            std::_Exit(41);

        gpu::d3d12::Backend replacementBackend;
        gpu::DeviceParams params;
#if VG_ENABLE_ASSERTS
        params.enableValidation = true;
#endif
        if (!gpu::Initialize(replacementBackend, params, &rhiFailure) || gpu::GetDeviceIncarnation() == oldIncarnation)
            std::_Exit(42);
        {
            gpu::DescriptorDomain newDomain(gpu::AdoptReference,
                                            gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            rendering::RenderFlowResourceAllocator newAllocator;
            rendering::RenderFlowResourceAllocatorConfig newConfig;
            newConfig.resourceDescriptors = newDomain.GetRef();
            newConfig.descriptorCacheTarget = 4;
            newConfig.descriptorCacheMaximum = 4;
            if (!newDomain.IsValid() || !newAllocator.Initialize(newConfig, &failure) ||
                !newAllocator.BeginFrame(0xC121, {}, &failure))
                std::_Exit(43);
            if (newAllocator.BindResourceContract(contract, &failure) ||
                failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity ||
                newAllocator.GetState() != rendering::RenderFlowResourceSessionState::Planning ||
                !contract.IsValid() || contract.GetPublicationSerial() != oldPublication)
                std::_Exit(44);
            if (!CompileAndFinishRF12ScopeContract(newAllocator, contract, graph, bindings, failure) ||
                contract.GetPublicationSerial() <= oldPublication || newAllocator.GetStats().structuralPlanBuilds != 1 ||
                !newAllocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                std::_Exit(45);
        }
        // Old allocator/pool/domain still own old-incarnation RHI references.
        // Never destroy or Reset them through the replacement backend.
        std::_Exit(0);
    }

    [[nodiscard]] bool RunRF12ContractIncarnationProofSubprocess() noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;
        char commandLine[MAX_PATH + 128]{};
        const int length = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" --rf12-contract-incarnation-proof", executable);
        if (length <= 0 || static_cast<vanguard::usize>(length) >= sizeof(commandLine))
            return false;
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0;
#else
        return true;
#endif
    }

    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    inline constexpr resources::ResourceTypeId TestMaterialType = vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T');

    struct FailingContributionRetry final
    {
        vanguard::u32 retryCalls = 0;
    };

    [[nodiscard]] bool IgnoreContributionWrite(void*, rendering::GpuSceneContributionToken,
                                               vanguard::containers::ArraySpan<const rendering::GpuSceneUploadReservation>,
                                               const char*& failureMessage) noexcept
    {
        failureMessage = nullptr;
        return true;
    }

    void IgnoreContributionAccept(void*, rendering::GpuSceneContributionToken, gpu::GpuFence) noexcept
    {
    }

    [[nodiscard]] bool FailContributionRetry(void* const owner, rendering::GpuSceneContributionToken,
                                             const char*& failureMessage) noexcept
    {
        auto* const state = static_cast<FailingContributionRetry*>(owner);
        if (state != nullptr)
            ++state->retryCalls;
        failureMessage = "forced contribution rollback failure";
        return false;
    }

    [[nodiscard]] bool RunDedicatedResourceProviderSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[dedicated-provider] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        rendering::FrameBufferDesc smallerCapacity;
        smallerCapacity.active.size = 1024;
        smallerCapacity.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        smallerCapacity.active.initialState = gpu::ResourceState::Common;
        smallerCapacity.maximumSize = 2048;
        rendering::FrameBufferDesc largerCapacity = smallerCapacity;
        largerCapacity.active.size = 1536;
        largerCapacity.maximumSize = 4096;
        const rendering::detail::DedicatedResourceRequest smallerRequest =
            rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(smallerCapacity));
        const rendering::detail::DedicatedResourceRequest largerRequest =
            rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(largerCapacity));
        rendering::detail::DedicatedResourceClassKey forcedCollision = smallerRequest.compatibility;
        forcedCollision.usageBits ^= static_cast<vanguard::u32>(gpu::BufferUsage::CopySource);
        forcedCollision.hash = smallerRequest.compatibility.hash;
        if (smallerRequest.capacity != smallerCapacity.maximumSize || largerRequest.capacity != largerCapacity.maximumSize ||
            smallerRequest.physicalDesc.buffer.active.size != smallerCapacity.maximumSize || largerRequest.physicalDesc.buffer.active.size != largerCapacity.maximumSize ||
            !rendering::detail::DedicatedResourceSameCompatibilityClass(smallerRequest.compatibility, largerRequest.compatibility) ||
            !rendering::detail::DedicatedResourceCanSatisfy(largerRequest.compatibility, largerRequest.capacity, smallerRequest.compatibility, smallerRequest.capacity) ||
            rendering::detail::DedicatedResourceCanSatisfy(smallerRequest.compatibility, smallerRequest.capacity, largerRequest.compatibility, largerRequest.capacity) ||
            rendering::detail::DedicatedResourceSameCompatibilityClass(smallerRequest.compatibility, forcedCollision))
            return false;

        const gpu::ShaderStageMask descriptorVisibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptorOwner(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, descriptorVisibility}, &rhiFailure));
        const gpu::DescriptorDomainRef descriptorDomain = descriptorOwner.GetRef();
        rendering::RenderFlowResourceAllocatorConfig allocatorConfig;
        allocatorConfig.resourceDescriptors = descriptorDomain;
        allocatorConfig.descriptorCacheTarget = 0;
        allocatorConfig.descriptorCacheMaximum = 4;
        rendering::RenderFlowResourceAllocator allocator;
        if (!descriptorOwner.IsValid() || !allocator.Initialize(allocatorConfig, &failure))
            return false;
        // RF12 semantic-plan ownership lives outside the allocator. This focused
        // provider smoke keeps one cache-style contract alive while frame-local
        // native imports/export slots are rebound below.
        rendering::RenderFlowResourceContract resourceContract;
        rendering::RenderNodeGraph contractCaptureGraph;
        contractCaptureGraph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings contractCaptureBindings;
        constexpr rendering::ResourceContractPatchId dynamicImportPatch{0x5246313254455351ull};
        constexpr rendering::ResourceContractPatchId anchorImportPatch{0x5246313254455353ull};
        constexpr rendering::ResourceContractPatchId dynamicExportPatch{0x5246313254455352ull};
        // The allocator must be the strong owner for the entire persistent pool
        // lifetime. Keep only the raw identity as a test witness.
        descriptorOwner.Reset();
        gpu::BufferDesc importedNativeDesc;
        importedNativeDesc.size = 512;
        importedNativeDesc.structureStride = 16;
        importedNativeDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        importedNativeDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer importedFirst(gpu::AdoptReference, gpu::CreateBuffer(importedNativeDesc, {}, &rhiFailure));
        gpu::Buffer importedAnchor(gpu::AdoptReference, gpu::CreateBuffer(importedNativeDesc, {}, &rhiFailure));
        // Cold and warm dynamic imports straddle this stationary import in
        // native ResourceRef order. The cached RF17 program must remap its
        // symbolic binding slots when the backend admission sort flips.
        gpu::Buffer importedSecond(gpu::AdoptReference, gpu::CreateBuffer(importedNativeDesc, {}, &rhiFailure));
        gpu::BufferDesc incompatibleNativeDesc = importedNativeDesc;
        incompatibleNativeDesc.size *= 2;
        gpu::Buffer importedIncompatible(gpu::AdoptReference, gpu::CreateBuffer(incompatibleNativeDesc, {}, &rhiFailure));
        if (!importedFirst.IsValid() || !importedSecond.IsValid() || !importedAnchor.IsValid() || !importedIncompatible.IsValid() || importedFirst.GetRef() == importedSecond.GetRef() ||
            importedFirst.GetRef() == importedAnchor.GetRef() || importedSecond.GetRef() == importedAnchor.GetRef())
            return false;
        const vanguard::u64 firstNativeSlot = gpu::ResourceRef(importedFirst.GetRef()).value;
        const vanguard::u64 anchorNativeSlot = gpu::ResourceRef(importedAnchor.GetRef()).value;
        const vanguard::u64 secondNativeSlot = gpu::ResourceRef(importedSecond.GetRef()).value;
        if (!((firstNativeSlot < anchorNativeSlot && anchorNativeSlot < secondNativeSlot) ||
              (secondNativeSlot < anchorNativeSlot && anchorNativeSlot < firstNativeSlot)))
            return fail("native binding permutation fixture");

        const auto runFrame = [&allocator, &failure, &importedNativeDesc, &importedAnchor, &resourceContract, &contractCaptureGraph, &contractCaptureBindings,
                               dynamicImportPatch, anchorImportPatch, dynamicExportPatch](
                                                     const vanguard::u64 frameSerial, const gpu::BufferRef importedNative,
                                                     gpu::BufferRef& resolvedBuffer,
                                                     gpu::DescriptorHandle& shaderResource, gpu::DescriptorHandle& unorderedAccess,
                                                     rendering::ExportSlotId& resolvedExportSlot, vanguard::u64& executionTapeSignature) noexcept
        {
            rendering::ResourcePlanningWriter writer;
            constexpr rendering::RenderFlowNodeId node{1};
            constexpr rendering::GpuFlowGroupId flow{1};
            constexpr rendering::CommandScopeId scope{1};
            constexpr rendering::FlowSpaceId space{1};
            rendering::FrameBufferDesc desc;
            desc.active.size = 4096;
            desc.active.structureStride = 16;
            desc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            desc.active.initialState = gpu::ResourceState::Common;
            desc.maximumSize = desc.active.size;
            rendering::LogicalResourceId resource;
            rendering::LogicalResourceId importedLogical;
            rendering::LogicalResourceId anchorLogical;
            rendering::ResourceUseId use;
            rendering::ResourceUseId secondUse;
            rendering::ResourceUseId importedUse;
            rendering::ResourceUseId anchorUse;
            constexpr rendering::ResourceScopeId structuralScope{0x5246313253434f50ull};
            rendering::ImportedResourceId imported;
            rendering::ImportedResourceId anchorImported;
            rendering::ExportSlotId exportSlot;
            rendering::RetainedBufferImportDesc importDesc;
            importDesc.token = {gpu::ResourceRef(importedNative).value};
            importDesc.contractPatch = dynamicImportPatch;
            importDesc.buffer = importedNative;
            importDesc.expected = importedNativeDesc;
            importDesc.initialState = gpu::ResourceState::Common;
            importDesc.terminalState = gpu::ResourceState::Common;
            rendering::RetainedBufferImportDesc anchorDesc = importDesc;
            anchorDesc.token = {gpu::ResourceRef(importedAnchor.GetRef()).value};
            anchorDesc.contractPatch = anchorImportPatch;
            anchorDesc.buffer = importedAnchor.GetRef();
            rendering::BufferUseDesc useDesc;
            useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
            useDesc.access = rendering::LogicalAccessIntent::Write;
            useDesc.content = rendering::ResourceContentIntent::Discard;
            rendering::BufferUseDesc importedRead;
            importedRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
            importedRead.access = rendering::LogicalAccessIntent::Read;
            importedRead.content = rendering::ResourceContentIntent::Preserve;
            rendering::TerminalResourceExportDesc exportDesc;
            exportDesc.terminalState = gpu::ResourceState::Common;
            exportDesc.terminalQueue = gpu::QueueType::Graphics;
            exportDesc.readiness = rendering::ExportReadinessKind::ExplicitFenceSignal;
            if (!allocator.BeginFrame(frameSerial, {false, false}, &failure))
                return false;
            const bool coldCompile = !resourceContract.IsValid();
            // Registration order is deliberately reversed after the cold compile.
            // Patch identity, not positional import index, owns the warm mapping.
            if ((coldCompile && (!allocator.RegisterImport(importDesc, imported, &failure) || !allocator.RegisterImport(anchorDesc, anchorImported, &failure))) ||
                (!coldCompile && (!allocator.RegisterImport(anchorDesc, anchorImported, &failure) || !allocator.RegisterImport(importDesc, imported, &failure))) ||
                !allocator.ReserveExportSlot(dynamicExportPatch, exportSlot, &failure))
                return false;
            if (coldCompile)
            {
                if (!allocator.BeginResourceContractCompilation(resourceContract, &failure) ||
                    !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.DeclareBuffer({space, "dedicated.provider.smoke"}, desc, resource, &failure) || !writer.OpenResourceScope(resource, structuralScope, &failure) ||
                    !writer.BeginBufferUse(resource, useDesc, use, &failure) || !writer.EndUse(use, &failure) ||
                    !writer.BeginBufferUse(resource, useDesc, secondUse, &failure) || !writer.EndUse(secondUse, &failure) ||
                    !writer.CloseResourceScope(structuralScope, &failure) ||
                    !writer.ImportBuffer({space, "dedicated.provider.dynamic-import"}, imported, importedLogical, &failure) ||
                    !writer.BeginBufferUse(importedLogical, importedRead, importedUse, &failure) || !writer.EndUse(importedUse, &failure) ||
                    !writer.ImportBuffer({space, "dedicated.provider.anchor-import"}, anchorImported, anchorLogical, &failure) ||
                    !writer.BeginBufferUse(anchorLogical, importedRead, anchorUse, &failure) || !writer.EndUse(anchorUse, &failure) ||
                    !writer.RequestExport(importedLogical, exportSlot, exportDesc, &failure) || !writer.Close(&failure) ||
                    !contractCaptureGraph.CaptureResourceContract(resourceContract, contractCaptureBindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                    return false;
            }
            else
            {
                // The cached plan owns dense ordinals. Stamp only the current
                // session generation, exactly as the renderer's node-template
                // instantiation does for a warm graph hit.
                use = {flow, 0, exportSlot.generation};
                secondUse = {flow, 1, exportSlot.generation};
                importedUse = {flow, 2, exportSlot.generation};
                anchorUse = {flow, 3, exportSlot.generation};
                if (!allocator.BindResourceContract(resourceContract, &failure))
                    return false;
            }
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::ResolvedBufferUse resolved;
            rendering::ResolvedBufferUse secondResolved;
            rendering::ResolvedBufferUse importedResolved;
            rendering::ResolvedBufferUse anchorResolved;
            rendering::CompiledExecutionRange ranges[4];
            gpu::Failure rhiFailure;
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246414255464645ull, &rhiFailure);
            if (!commands)
                return false;
            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !allocator.PacketFor(node, packet, &failure) ||
                !packet.ResolveExecutionRange(0, 1, ranges[0], &failure) ||
                !packet.ResolveExecutionRange(1, 1, ranges[1], &failure) ||
                !packet.ResolveExecutionRange(2, 1, ranges[2], &failure) ||
                !packet.ResolveExecutionRange(3, 1, ranges[3], &failure) ||
                !packet.ResolveBufferUseBinding(use, 1, resolved, &failure) ||
                !packet.ResolveBufferUseBinding(secondUse, 2, secondResolved, &failure) ||
                !packet.ResolveBufferUseBinding(importedUse, 3, importedResolved, &failure) ||
                !packet.ResolveBufferUseBinding(anchorUse, 4, anchorResolved, &failure) ||
                packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 4, cursor, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch ||
                !gpu::BindCommandList(commands, &rhiFailure) || !packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 4, cursor, &failure) ||
                resolved.IsValid() || secondResolved.IsValid() || importedResolved.IsValid() || anchorResolved.IsValid() ||
                !cursor.BeginRange(ranges[0], 1, &failure) || !resolved.IsValid() || !resolved.GetBuffer().IsValid())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            const rendering::CompiledExecutionActionSegment exitSegment = packet.GetExitActionSegment();
            executionTapeSignature = 14695981039346656037ull;
            const auto mixSignature = [&executionTapeSignature](const vanguard::u32 value) noexcept
            {
                executionTapeSignature = (executionTapeSignature ^ value) * 1099511628211ull;
            };
            for (const rendering::CompiledExecutionRange& range : ranges)
            {
                mixSignature(range.beginActions.count);
                mixSignature(range.endActions.count);
            }
            mixSignature(exitSegment.count);
            resolvedBuffer = resolved.GetBuffer();
            shaderResource = resolved.GetShaderResourceDescriptor();
            unorderedAccess = resolved.GetUnorderedAccessDescriptor();
            if (!shaderResource.IsValid() || !unorderedAccess.IsValid())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            if (!cursor.EndRange(ranges[0], 1, &failure) || resolved.IsValid() ||
                !cursor.BeginRange(ranges[1], 2, &failure) || !secondResolved.IsValid() || secondResolved.GetBuffer() != resolvedBuffer ||
                !cursor.EndRange(ranges[1], 2, &failure) || secondResolved.IsValid() ||
                !cursor.BeginRange(ranges[2], 3, &failure) || !importedResolved.IsValid() || importedResolved.GetBuffer() != importedNative ||
                !cursor.EndRange(ranges[2], 3, &failure) || importedResolved.IsValid() ||
                !cursor.BeginRange(ranges[3], 4, &failure) || !anchorResolved.IsValid() || anchorResolved.GetBuffer() != importedAnchor.GetRef() ||
                !cursor.EndRange(ranges[3], 4, &failure) || anchorResolved.IsValid() ||
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
            rendering::PublishedResourceExport exported;
            if (!allocator.Finish(terminal, &failure) || !allocator.TakeExport(exportSlot, exported, &failure) || exported.GetSlot() != exportSlot ||
                exported.GetBuffer() != importedNative || exported.GetReadyFence() != completion ||
                !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
                return false;
            resolvedExportSlot = exportSlot;
            exported.Reset();
            return true;
        };

        gpu::BufferRef first;
        gpu::BufferRef second;
        gpu::DescriptorHandle firstShaderResource;
        gpu::DescriptorHandle firstUnorderedAccess;
        gpu::DescriptorHandle secondShaderResource;
        gpu::DescriptorHandle secondUnorderedAccess;
        rendering::ExportSlotId firstExportSlot;
        rendering::ExportSlotId secondExportSlot;
        vanguard::u64 coldExecutionTapeSignature = 0;
        vanguard::u64 warmExecutionTapeSignature = 0;
        if (!runFrame(1, importedFirst.GetRef(), first, firstShaderResource, firstUnorderedAccess, firstExportSlot, coldExecutionTapeSignature))
            return fail("cold resource-contract frame");
        const gpu::DescriptorDomainStats coldBufferDescriptors = gpu::GetDescriptorDomainStats(descriptorDomain);
        const rendering::RenderFlowResourceAllocatorStats coldBufferStats = allocator.GetStats();
        if (coldBufferStats.structuralPlanBuilds != 1 || coldBufferStats.structuralPlanHits != 0 || coldBufferStats.structuralPlanMisses != 1 ||
            coldBufferStats.persistentStateProgramBuilds != 1 || coldBufferStats.persistentStateProgramHits != 0)
            return false;
        if (coldBufferDescriptors.allocated != 4 || coldBufferDescriptors.populated != 4)
        {
            std::fprintf(stderr, "[dedicated-provider] cold descriptor stats allocated=%u populated=%u pending=%u live=%u reusable=%u\n",
                         coldBufferDescriptors.allocated, coldBufferDescriptors.populated, coldBufferDescriptors.pendingRetirement,
                         coldBufferStats.canonicalDescriptorLiveSlots, coldBufferStats.canonicalDescriptorReusableSlots);
            return fail("cold resource-contract descriptor publication");
        }
        if (!runFrame(2, importedSecond.GetRef(), second, secondShaderResource, secondUnorderedAccess, secondExportSlot, warmExecutionTapeSignature))
            return fail("warm resource-contract frame");
        if (first != second ||
            firstExportSlot.index != secondExportSlot.index || firstExportSlot.generation == secondExportSlot.generation ||
            firstShaderResource != secondShaderResource || firstUnorderedAccess != secondUnorderedAccess ||
            coldExecutionTapeSignature == 0 || warmExecutionTapeSignature != coldExecutionTapeSignature)
            return fail("warm resource-contract equivalence");
        const rendering::RenderFlowResourceAllocatorStats persistentBufferStats = allocator.GetStats();
        if (persistentBufferStats.persistentDedicatedPlanBuilds != 1 || persistentBufferStats.persistentDedicatedPlanHits != 1 ||
            persistentBufferStats.persistentDedicatedLeaseHits != 1 || persistentBufferStats.persistentDedicatedLeaseFallbacks != 0 ||
            persistentBufferStats.persistentStateProgramBuilds != 1 || persistentBufferStats.persistentStateProgramHits != 1 ||
            persistentBufferStats.persistentStateProgramMisses != 0 ||
            persistentBufferStats.structuralPlanBuilds != 1 || persistentBufferStats.structuralPlanHits != 1 || persistentBufferStats.structuralPlanMisses != 1 ||
            persistentBufferStats.compiledOperations != coldBufferStats.compiledOperations || persistentBufferStats.planningWriters != 0)
            return false;
        const gpu::DescriptorDomainStats warmBufferDescriptors = gpu::GetDescriptorDomainStats(descriptorDomain);
        if (warmBufferDescriptors.allocated != 4 || warmBufferDescriptors.populated != 4 || warmBufferDescriptors.allocationFailures != coldBufferDescriptors.allocationFailures)
            return false;

        // Native buffer identity and incoming fence values are legitimate
        // per-frame patches; imported initial/terminal state and queue policy
        // are NOT. Reject all four changed contracts before a cached state
        // program can be admitted, without disturbing the published plan.
        for (vanguard::u32 variant = 0; variant < 4; ++variant)
        {
            rendering::RetainedBufferImportDesc altered;
            altered.token = {gpu::ResourceRef(importedFirst.GetRef()).value};
            altered.contractPatch = dynamicImportPatch;
            altered.buffer = importedFirst.GetRef();
            altered.expected = importedNativeDesc;
            altered.initialState = gpu::ResourceState::Common;
            altered.terminalState = gpu::ResourceState::Common;
            if (variant == 0)
                altered.initialState = gpu::ResourceState::ShaderResourceGraphics;
            else if (variant == 1)
                altered.terminalState = gpu::ResourceState::ShaderResourceGraphics;
            else if (variant == 2)
                altered.initialQueue = gpu::QueueType::Compute;
            else
                altered.terminalQueue = gpu::QueueType::Compute;
            rendering::RetainedBufferImportDesc anchor;
            anchor.token = {gpu::ResourceRef(importedAnchor.GetRef()).value};
            anchor.contractPatch = anchorImportPatch;
            anchor.buffer = importedAnchor.GetRef();
            anchor.expected = importedNativeDesc;
            anchor.initialState = gpu::ResourceState::Common;
            anchor.terminalState = gpu::ResourceState::Common;
            rendering::ImportedResourceId changedId;
            rendering::ImportedResourceId anchorId;
            rendering::ExportSlotId slot;
            if (!allocator.BeginFrame(3 + variant, {false, false}, &failure) ||
                !allocator.RegisterImport(altered, changedId, &failure) ||
                !allocator.RegisterImport(anchor, anchorId, &failure) ||
                !allocator.ReserveExportSlot(dynamicExportPatch, slot, &failure) ||
                allocator.BindResourceContract(resourceContract, &failure) ||
                failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict ||
                !resourceContract.IsValid())
                return fail("changed warm import state or queue policy");
            allocator.CancelBeforePublication();
        }

        // A valid current import with the same semantic patch but a different
        // descriptor shape is a structural miss, not a warm patch. Rejection
        // must leave the published contract available for the cold fallback.
        {
            const vanguard::u64 publicationSerial = resourceContract.GetPublicationSerial();
            rendering::RetainedBufferImportDesc incompatibleImport;
            incompatibleImport.token = {gpu::ResourceRef(importedIncompatible.GetRef()).value};
            incompatibleImport.contractPatch = dynamicImportPatch;
            incompatibleImport.buffer = importedIncompatible.GetRef();
            incompatibleImport.expected = incompatibleNativeDesc;
            incompatibleImport.initialState = gpu::ResourceState::Common;
            incompatibleImport.terminalState = gpu::ResourceState::Common;
            rendering::RetainedBufferImportDesc anchorImport;
            anchorImport.token = {gpu::ResourceRef(importedAnchor.GetRef()).value};
            anchorImport.contractPatch = anchorImportPatch;
            anchorImport.buffer = importedAnchor.GetRef();
            anchorImport.expected = importedNativeDesc;
            anchorImport.initialState = gpu::ResourceState::Common;
            anchorImport.terminalState = gpu::ResourceState::Common;
            rendering::ImportedResourceId incompatibleImported;
            rendering::ImportedResourceId anchorImported;
            rendering::ExportSlotId exportSlot;
            if (!allocator.BeginFrame(7, {false, false}, &failure) || !allocator.RegisterImport(anchorImport, anchorImported, &failure) ||
                !allocator.RegisterImport(incompatibleImport, incompatibleImported, &failure) ||
                !allocator.ReserveExportSlot(dynamicExportPatch, exportSlot, &failure) ||
                allocator.BindResourceContract(resourceContract, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict ||
                !resourceContract.IsValid() || resourceContract.GetPublicationSerial() != publicationSerial)
                return fail("incompatible warm import shape");
            allocator.CancelBeforePublication();
        }

        // A structural miss may compile a candidate and then fail in physical
        // assignment. That failed candidate must never replace the last proven
        // structural plan: the original shape must still hit immediately after
        // the rejected frame.
        {
            rendering::ResourcePlanningWriter rejectedWriter;
            constexpr rendering::RenderFlowNodeId rejectedNode{3};
            constexpr rendering::GpuFlowGroupId rejectedFlow{3};
            constexpr rendering::CommandScopeId rejectedScope{3};
            constexpr rendering::FlowSpaceId rejectedSpace{3};
            rendering::FrameBufferDesc rejectedDesc;
            rejectedDesc.active.size = 8192;
            rejectedDesc.active.usage = gpu::BufferUsage::UnorderedAccess;
            rejectedDesc.active.initialState = gpu::ResourceState::Common;
            rejectedDesc.maximumSize = rejectedDesc.active.size;
            rendering::LogicalResourceId rejectedResource;
            rendering::ResourceUseId rejectedUse;
            rendering::BufferUseDesc rejectedWrite;
            rejectedWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            rejectedWrite.access = rendering::LogicalAccessIntent::Write;
            rejectedWrite.content = rendering::ResourceContentIntent::Discard;
            if (!allocator.BeginFrame(8, {false, false}, &failure) || !allocator.BeginResourceContractCompilation(resourceContract, &failure) ||
                !allocator.CreatePlanningWriter(rejectedNode, rejectedFlow, rejectedScope, rejectedWriter, &failure) ||
                !rejectedWriter.DeclareBuffer({rejectedSpace, "dedicated.provider.rejected-shape"}, rejectedDesc, rejectedResource, &failure) ||
                !rejectedWriter.BeginBufferUse(rejectedResource, rejectedWrite, rejectedUse, &failure) || !rejectedWriter.EndUse(rejectedUse, &failure) ||
                !rejectedWriter.Close(&failure) || !contractCaptureGraph.CaptureResourceContract(resourceContract, contractCaptureBindings, allocator, &failure) ||
                !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                return fail("failed-miss planning");
            rendering::detail::DedicatedResourceProviderTestAccess::Set(
                allocator, {rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation, 0,
                            rendering::RenderFlowResourceFailureCode::NativeOutOfMemory});
            const bool rejected = !allocator.Resolve(nullptr, &failure) && failure.code == rendering::RenderFlowResourceFailureCode::NativeOutOfMemory &&
                                  !allocator.GetExecutionGeneration().IsValid() && allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle;
            rendering::detail::DedicatedResourceProviderTestAccess::Clear(allocator);
            const rendering::RenderFlowResourceAllocatorStats rejectedStats = allocator.GetStats();
            if (!rejected || rejectedStats.structuralPlanBuilds != 1 || rejectedStats.structuralPlanHits != 1 || rejectedStats.structuralPlanMisses != 2)
                return fail("failed-miss transaction");
        }

        gpu::BufferRef recoveredBuffer;
        gpu::DescriptorHandle recoveredShaderResource;
        gpu::DescriptorHandle recoveredUnorderedAccess;
        rendering::ExportSlotId recoveredExportSlot;
        vanguard::u64 recoveredExecutionTapeSignature = 0;
        if (!runFrame(9, importedFirst.GetRef(), recoveredBuffer, recoveredShaderResource, recoveredUnorderedAccess, recoveredExportSlot, recoveredExecutionTapeSignature) ||
            recoveredBuffer != first || recoveredShaderResource != firstShaderResource || recoveredUnorderedAccess != firstUnorderedAccess)
            return fail("failed-miss old-plan recovery");
        const rendering::RenderFlowResourceAllocatorStats recoveredPlanStats = allocator.GetStats();
        if (recoveredPlanStats.structuralPlanBuilds != 1 || recoveredPlanStats.structuralPlanHits != 2 || recoveredPlanStats.structuralPlanMisses != 2 ||
            recoveredPlanStats.compiledOperations != coldBufferStats.compiledOperations)
            return fail("failed-miss structural stats");

        const auto runTextureFrame = [&allocator, &failure](const vanguard::u64 frameSerial, const gpu::Extent3D maximumExtent, const vanguard::u16 maximumMipCount,
                                                            gpu::TextureRef& resolvedTexture, gpu::DescriptorHandle& shaderResource,
                                                            gpu::DescriptorHandle& unorderedAccess) noexcept
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
            if (!allocator.BeginFrame(frameSerial, {false, false}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
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
            shaderResource = resolved.GetShaderResourceDescriptor();
            unorderedAccess = resolved.GetUnorderedAccessDescriptor();
            if (!shaderResource.IsValid() || !unorderedAccess.IsValid())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
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
        gpu::DescriptorHandle firstTextureShaderResource;
        gpu::DescriptorHandle firstTextureUnorderedAccess;
        gpu::DescriptorHandle secondTextureShaderResource;
        gpu::DescriptorHandle secondTextureUnorderedAccess;
        if (!runTextureFrame(10, {16, 16, 1}, 4, firstTexture, firstTextureShaderResource, firstTextureUnorderedAccess))
            return fail("cold texture frame");
        if (!runTextureFrame(11, {32, 32, 1}, 5, secondTexture, secondTextureShaderResource, secondTextureUnorderedAccess) || firstTexture != secondTexture ||
            firstTextureShaderResource != secondTextureShaderResource || firstTextureUnorderedAccess != secondTextureUnorderedAccess)
            return fail("warm texture frame");
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        gpu::Failure clearFailure;
        // RF11 retained-import canonical descriptors share the same bounded
        // ledger. Rebinding import A -> B and the first texture publication
        // deliberately exercises global descriptor-pressure eviction/retry;
        // that retry reclaims the newly created texture as a pool hit. Earlier
        // canonical bundles are allowed to leave the cache under that pressure;
        // only the current warm texture bundle must remain resident here.
        if (stats.dedicatedResourcePoolMisses != 2 || stats.dedicatedResourcePoolHits != 4 || stats.chargedTextureBytes == 0 || stats.chargedBufferBytes == 0 ||
            stats.chargedNativeBytes != stats.chargedTextureBytes + stats.chargedBufferBytes || stats.canonicalDescriptorLiveSlots != 2 ||
            stats.canonicalDescriptorPublicationMisses != 8 || stats.canonicalDescriptorPublicationHits != 3 || stats.canonicalDescriptorPressureEvictions == 0 ||
            stats.canonicalDescriptorMaintenanceEvictions != 0 ||
            stats.structuralPlanBuilds != 1 || stats.structuralPlanHits != 2 || stats.structuralPlanMisses != 2 ||
            gpu::GetDescriptorDomainStats(descriptorDomain).allocated != 2 || !allocator.ClearPersistentCaches(&failure))
        {
            std::fprintf(stderr, "[dedicated-provider] aggregate stats misses=%llu hits=%llu descriptorMisses=%llu descriptorHits=%llu structural=%llu/%llu/%llu\n",
                         static_cast<unsigned long long>(stats.dedicatedResourcePoolMisses), static_cast<unsigned long long>(stats.dedicatedResourcePoolHits),
                         static_cast<unsigned long long>(stats.canonicalDescriptorPublicationMisses), static_cast<unsigned long long>(stats.canonicalDescriptorPublicationHits),
                         static_cast<unsigned long long>(stats.structuralPlanBuilds), static_cast<unsigned long long>(stats.structuralPlanHits),
                         static_cast<unsigned long long>(stats.structuralPlanMisses));
            std::fprintf(stderr, "[dedicated-provider] charged=%llu texture=%llu buffer=%llu live=%llu pressure=%llu maintenance=%llu domainAllocated=%u\n",
                         static_cast<unsigned long long>(stats.chargedNativeBytes), static_cast<unsigned long long>(stats.chargedTextureBytes),
                         static_cast<unsigned long long>(stats.chargedBufferBytes), static_cast<unsigned long long>(stats.canonicalDescriptorLiveSlots),
                         static_cast<unsigned long long>(stats.canonicalDescriptorPressureEvictions), static_cast<unsigned long long>(stats.canonicalDescriptorMaintenanceEvictions),
                         gpu::GetDescriptorDomainStats(descriptorDomain).allocated);
            return fail("aggregate stats");
        }
        const rendering::RenderFlowResourceAllocatorStats clearing = allocator.GetStats();
        if (clearing.chargedNativeBytes != stats.chargedNativeBytes || clearing.pendingNativeDestructionResources != 2 || clearing.canonicalDescriptorLiveSlots != 0 ||
            gpu::GetDescriptorDomainStats(descriptorDomain).allocated != 0 || !gpu::FlushRetiredResources(&clearFailure))
            return false;
        const rendering::RenderFlowResourceAllocatorStats cleared = allocator.GetStats();
        if (cleared.chargedNativeBytes != 0 || cleared.pendingNativeDestructionResources != 0)
            return false;
        gpu::TextureRef rebuiltTexture;
        gpu::DescriptorHandle rebuiltShaderResource;
        gpu::DescriptorHandle rebuiltUnorderedAccess;
        if (!runTextureFrame(12, {32, 32, 1}, 5, rebuiltTexture, rebuiltShaderResource, rebuiltUnorderedAccess))
            return false;
        const rendering::RenderFlowResourceAllocatorStats rebuilt = allocator.GetStats();
        if (rebuilt.structuralPlanBuilds != 1 || rebuilt.structuralPlanHits != 2 || rebuilt.structuralPlanMisses != 2 ||
            gpu::GetDescriptorDomainStats(descriptorDomain).allocated != 2 || !allocator.ClearPersistentCaches(&failure) ||
            !gpu::FlushRetiredResources(&clearFailure) || !allocator.Shutdown(&failure))
            return false;
        if (!gpu::FlushRetiredResources(&clearFailure))
            return false;
        return gpu::GetDescriptorDomainStats(descriptorDomain).capacity == 0;
    }

    [[nodiscard]] bool RunRF13PhysicalProgramCacheProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderFlowResourceAllocator allocator;
        const auto fail = [&](const char* const stage) noexcept
        {
            const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
            std::fprintf(stderr, "[rf13-physical-program] %s flow=%u message=%s builds=%llu hits=%llu leaseHits=%llu leaseFallbacks=%llu structural=%llu/%llu/%llu\n",
                         stage, static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned long long>(stats.persistentDedicatedPlanBuilds), static_cast<unsigned long long>(stats.persistentDedicatedPlanHits),
                         static_cast<unsigned long long>(stats.persistentDedicatedLeaseHits), static_cast<unsigned long long>(stats.persistentDedicatedLeaseFallbacks),
                         static_cast<unsigned long long>(stats.structuralPlanBuilds), static_cast<unsigned long long>(stats.structuralPlanHits),
                         static_cast<unsigned long long>(stats.structuralPlanMisses));
            return false;
        };
        if (!allocator.Initialize({}, &failure))
            return fail("initialize");

        rendering::RenderNodeGraph captureGraph;
        captureGraph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings captureBindings;
        constexpr vanguard::u32 PhysicalProgramCacheEntries = 4;
        rendering::RenderFlowResourceContract contracts[PhysicalProgramCacheEntries + 1u];
        rendering::CompiledExecutionPacketView heldEvictedPacket;

        const auto run = [&](rendering::RenderFlowResourceContract& contract, const vanguard::u64 frameSerial) noexcept
        {
            constexpr rendering::RenderFlowNodeId node{0x52463133u};
            constexpr rendering::GpuFlowGroupId flow{1};
            constexpr rendering::CommandScopeId scope{0x52463133u};
            constexpr rendering::FlowSpaceId space{0x52463133u};
            rendering::FrameBufferDesc desc;
            desc.active.size = 4096;
            desc.active.usage = gpu::BufferUsage::UnorderedAccess;
            desc.active.initialState = gpu::ResourceState::Common;
            desc.maximumSize = desc.active.size;
            rendering::BufferUseDesc useDesc;
            useDesc.requiredState = gpu::ResourceState::UnorderedAccess;
            useDesc.access = rendering::LogicalAccessIntent::Write;
            useDesc.content = rendering::ResourceContentIntent::Discard;

            if (!allocator.BeginFrame(frameSerial, {false, false}, &failure))
                return fail("begin frame");
            if (!contract.IsValid())
            {
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId resource;
                rendering::ResourceUseId use;
                if (!allocator.BeginResourceContractCompilation(contract, &failure) ||
                    !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.DeclareBuffer({space, "rf13.physical-program"}, desc, resource, &failure) ||
                    !writer.BeginBufferUse(resource, useDesc, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
                    !captureGraph.CaptureResourceContract(contract, captureBindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                {
                    allocator.CancelBeforePublication();
                    return fail("cold planning");
                }
            }
            else if (!allocator.BindResourceContract(contract, &failure))
            {
                allocator.CancelBeforePublication();
                return fail("warm bind");
            }

            rendering::CompiledExecutionPacketView packet;
            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !allocator.PacketFor(node, packet, &failure))
            {
                allocator.CancelBeforePublication();
                return fail("resolve/begin/packet");
            }
            if (&contract == &contracts[0] && !heldEvictedPacket.IsValid())
                heldEvictedPacket = packet;
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt receipt{packet.GetCommandScope(), rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission,
                                                                   gpu::QueueType::Graphics, {}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                                                                rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            return allocator.Finish(terminal, &failure) || fail("finish");
        };

        vanguard::u64 frameSerial = 0x524631330000ull;
        for (rendering::RenderFlowResourceContract& contract : contracts)
            if (!run(contract, ++frameSerial))
                return fail("cold contract sequence");

        // The fifth cold contract evicts only the oldest physical companion from
        // the four-entry RF13 cache; every immutable structural contract survives.
        const auto afterCold = allocator.GetStats();
        if (afterCold.persistentDedicatedPlanBuilds != 5 || afterCold.persistentDedicatedPlanHits != 0 ||
            afterCold.structuralPlanBuilds != 5 || afterCold.structuralPlanHits != 0)
            return fail("cold cache counters");

        // B/C/D remain resident and must check out their exact strong leases with
        // no static physical-plan rebuild.
        for (vanguard::u32 index = 1; index < 4; ++index)
            if (!run(contracts[index], ++frameSerial))
                return fail("warm survivor sequence");
        const auto afterWarmSurvivors = allocator.GetStats();
        if (afterWarmSurvivors.persistentDedicatedPlanBuilds != 5 || afterWarmSurvivors.persistentDedicatedPlanHits != 3 ||
            afterWarmSurvivors.persistentDedicatedLeaseHits != 3 || afterWarmSurvivors.persistentDedicatedLeaseFallbacks != 0 ||
            afterWarmSurvivors.structuralPlanHits != 3)
            return fail("warm survivor counters");

        // A still warm-hits its semantic contract after physical eviction. Only
        // its physical program is rebuilt, proving the two cache layers are not
        // accidentally coupled by graph-entry lifetime or storage address.
        if (!run(contracts[0], ++frameSerial))
            return fail("evicted physical rebuild");
        const auto afterPhysicalRebuild = allocator.GetStats();
        if (afterPhysicalRebuild.persistentDedicatedPlanBuilds != 6 || afterPhysicalRebuild.persistentDedicatedPlanHits != 3 ||
            afterPhysicalRebuild.structuralPlanBuilds != 5 || afterPhysicalRebuild.structuralPlanHits != 4)
            return fail("physical rebuild counters");

        if (!heldEvictedPacket.IsValid() || heldEvictedPacket.GetStepCount() != 2 || heldEvictedPacket.GetUseCount() != 1 ||
            !allocator.ClearPersistentCaches(&failure) || !heldEvictedPacket.IsValid() ||
            heldEvictedPacket.GetStepCount() != 2 || heldEvictedPacket.GetUseCount() != 1)
            return fail("held execution packet survived physical cache eviction");
        rendering::CompiledExecutionRange staleRange;
        if (heldEvictedPacket.ResolveExecutionRange(0, 1, staleRange, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidPhase)
            return fail("terminal packet must reject execution after cache eviction");
        heldEvictedPacket = {};
        return allocator.Shutdown(&failure) || fail("cleanup");
    }

    // RF12 dynamic bindings: one immutable semantic contract survives native
    // import replacement, a newer incoming fence, reversed export reservations,
    // and a warm Resolve failure without publishing a failed candidate.
    [[nodiscard]] bool RunResourceContractDynamicPatchProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rf12-dynamic-patch] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        constexpr rendering::ResourceContractPatchId importPatch{0x5246313244594e49ull};
        constexpr rendering::ResourceContractPatchId exportImportPatch{0x5246313245585041ull};
        constexpr rendering::ResourceContractPatchId exportNativePatch{0x5246313245585042ull};
        constexpr rendering::RenderFlowNodeId node{1};
        constexpr rendering::GpuFlowGroupId flow{1};
        constexpr rendering::CommandScopeId scope{1};
        constexpr rendering::FlowSpaceId space{1};
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceContract contract;
        rendering::RenderNodeGraph graph;
        graph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings bindings;
        if (!allocator.Initialize({}, &failure))
            return fail("allocator initialize");
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("pre-ABA resource retirement drain");

        gpu::BufferDesc externalDesc;
        externalDesc.size = 1024;
        externalDesc.structureStride = 16;
        externalDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        externalDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer original(gpu::AdoptReference, gpu::CreateBuffer(externalDesc, {}, &rhiFailure));
        if (!original.IsValid())
            return fail("initial native import");

        // Within one frame an external token is a unique native-resource
        // identity. Stable cross-frame rebinding is expressed by contractPatch,
        // never by silently changing the resource behind the same token.
        gpu::Buffer tokenConflict(gpu::AdoptReference, gpu::CreateBuffer(externalDesc, {}, &rhiFailure));
        if (!tokenConflict.IsValid())
            return fail("same-token conflict native resource");
        {
            rendering::RetainedBufferImportDesc first;
            first.token = {0x52463132544f4b4eull};
            first.contractPatch = importPatch;
            first.buffer = original.GetRef();
            first.expected = externalDesc;
            first.initialState = gpu::ResourceState::Common;
            first.terminalState = gpu::ResourceState::Common;
            rendering::ImportedResourceId accepted;
            rendering::ImportedResourceId rejected;
            if (!allocator.BeginFrame(1211, {}, &failure) || !allocator.RegisterImport(first, accepted, &failure))
                return fail("same-token baseline registration");
            rendering::RetainedBufferImportDesc conflicting = first;
            conflicting.buffer = tokenConflict.GetRef();
            if (allocator.RegisterImport(conflicting, rejected, &failure) ||
                failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict)
                return fail("same token must reject a different native resource");
            allocator.CancelBeforePublication();
        }
        tokenConflict.Reset();
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("same-token conflict cleanup");

        // Public submission receipts provide real, strictly ordered graphics
        // waits rather than manufactured fence values.
        const auto submitFence = [&](const gpu::CommandListType type, gpu::GpuFence& fence) noexcept
        {
            gpu::CommandListRef list = gpu::CreateCommandList(type, 0x5246313246454e43ull, &rhiFailure);
            if (!list.IsValid() || !gpu::BindCommandList(list, &rhiFailure))
            {
                if (list.IsValid()) gpu::DiscardCommandList(list);
                return false;
            }
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[] = {list};
            return gpu::CloseAndSubmitCommandLists("RF12 dynamic incoming fence", {submissions, 1}, gpu::CommandListSyncType::None, fence, &rhiFailure);
        };
        gpu::GpuFence coldWait;
        gpu::GpuFence warmWait;
        gpu::GpuFence copyWait;
        if (!submitFence(gpu::CommandListType::Default, coldWait) || !coldWait.IsValid())
            return fail("real cold graphics fence");

        rendering::FrameBufferDesc nativeDesc;
        nativeDesc.active.size = 4096;
        nativeDesc.active.usage = gpu::BufferUsage::UnorderedAccess;
        nativeDesc.active.initialState = gpu::ResourceState::Common;
        nativeDesc.maximumSize = nativeDesc.active.size;
        rendering::BufferUseDesc importedRead;
        importedRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        importedRead.access = rendering::LogicalAccessIntent::Read;
        importedRead.content = rendering::ResourceContentIntent::Preserve;
        rendering::BufferUseDesc nativeWrite;
        nativeWrite.requiredState = gpu::ResourceState::UnorderedAccess;
        nativeWrite.access = rendering::LogicalAccessIntent::Write;
        nativeWrite.content = rendering::ResourceContentIntent::Discard;
        rendering::TerminalResourceExportDesc exported;
        exported.terminalState = gpu::ResourceState::Common;
        exported.terminalQueue = gpu::QueueType::Graphics;
        exported.readiness = rendering::ExportReadinessKind::ExplicitFenceSignal;
        // Hold all exported physical buffers through the injected failure so
        // the provider must attempt native creation rather than reuse a pool hit.
        gpu::Buffer heldNative[4];
        vanguard::u32 heldCount = 0;
        rendering::ExportSlotId coldImportSlot;
        rendering::ExportSlotId coldNativeSlot;
        rendering::ExportSlotId warmImportSlot;
        rendering::ExportSlotId warmNativeSlot;
        rendering::ImportedResourceId registeredImport;

        const auto registerPatches = [&](const vanguard::u64 serial, const gpu::BufferRef current, const gpu::GpuFence incoming,
                                         const bool reverse, rendering::ExportSlotId& importSlot, rendering::ExportSlotId& nativeSlot) noexcept
        {
            rendering::RetainedBufferImportDesc desc;
            desc.token = {gpu::ResourceRef(current).value};
            desc.contractPatch = importPatch;
            desc.buffer = current;
            desc.expected = externalDesc;
            desc.initialState = gpu::ResourceState::Common;
            desc.terminalState = gpu::ResourceState::Common;
            desc.initialQueue = incoming.queue;
            desc.terminalQueue = gpu::QueueType::Graphics;
            desc.readiness = rendering::ImportReadinessKind::ExplicitFenceWait;
            desc.incomingWait = incoming;
            if (!allocator.BeginFrame(serial, {}, &failure) || !allocator.RegisterImport(desc, registeredImport, &failure))
                return false;
            if (reverse)
                return allocator.ReserveExportSlot(exportNativePatch, nativeSlot, &failure) &&
                       allocator.ReserveExportSlot(exportImportPatch, importSlot, &failure);
            return allocator.ReserveExportSlot(exportImportPatch, importSlot, &failure) &&
                   allocator.ReserveExportSlot(exportNativePatch, nativeSlot, &failure);
        };

        const auto runFrame = [&](const vanguard::u64 serial, const gpu::BufferRef current, const gpu::GpuFence incoming,
                                  const bool cold, const bool abort, const bool injectNativeFailure,
                                  rendering::ExportSlotId& importSlot, rendering::ExportSlotId& nativeSlot) noexcept
        {
            if (!registerPatches(serial, current, incoming, !cold, importSlot, nativeSlot))
                return false;
            rendering::ResourceUseId importedUse;
            rendering::ResourceUseId nativeUse;
            if (cold)
            {
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId logicalImport;
                rendering::LogicalResourceId logicalNative;
                if (!allocator.BeginResourceContractCompilation(contract, &failure) ||
                    !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.ImportBuffer({space, "rf12.dynamic.import"}, registeredImport, logicalImport, &failure) ||
                    !writer.BeginBufferUse(logicalImport, importedRead, importedUse, &failure) || !writer.EndUse(importedUse, &failure) ||
                    !writer.DeclareBuffer({space, "rf12.dynamic.native"}, nativeDesc, logicalNative, &failure) ||
                    !writer.BeginBufferUse(logicalNative, nativeWrite, nativeUse, &failure) || !writer.EndUse(nativeUse, &failure) ||
                    !writer.RequestExport(logicalImport, importSlot, exported, &failure) ||
                    !writer.RequestExport(logicalNative, nativeSlot, exported, &failure) || !writer.Close(&failure) ||
                    !graph.CaptureResourceContract(contract, bindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                    return false;
            }
            else
            {
                importedUse = {flow, 0, importSlot.generation};
                nativeUse = {flow, 1, importSlot.generation};
                if (!allocator.BindResourceContract(contract, &failure))
                    return false;
            }

            if (injectNativeFailure)
            {
                const vanguard::u64 publishedSerial = contract.GetPublicationSerial();
                rendering::detail::DedicatedResourceProviderTestAccess::Set(
                    allocator, {rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation, 0,
                                rendering::RenderFlowResourceFailureCode::NativeOutOfMemory});
                const bool rejected = !allocator.Resolve(nullptr, &failure) &&
                                      failure.code == rendering::RenderFlowResourceFailureCode::NativeOutOfMemory &&
                                      !allocator.GetExecutionGeneration().IsValid() &&
                                      allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle &&
                                      contract.GetPublicationSerial() == publishedSerial;
                rendering::detail::DedicatedResourceProviderTestAccess::Clear(allocator);
                return rejected;
            }

            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
                return false;
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            if (abort)
            {
                const rendering::CommandScopeExecutionReceipt receipts[] = {
                    {scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
                const rendering::TerminalExecutionReceipt terminal{
                    generation, rendering::TerminalExecutionCompletionKind::Aborted,
                    vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                rendering::PublishedResourceExport none;
                return allocator.Finish(terminal, &failure) &&
                       !allocator.TakeExport(importSlot, none, &failure) &&
                       failure.code == rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity &&
                       !allocator.TakeExport(nativeSlot, none, &failure) &&
                       failure.code == rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity;
            }

            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::CompiledExecutionRange ranges[2];
            rendering::ResolvedBufferUse resolvedImport;
            rendering::ResolvedBufferUse resolvedNative;
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246313244594e50ull, &rhiFailure);
            bool valid = commands.IsValid() && gpu::BindCommandList(commands, &rhiFailure) &&
                         allocator.PacketFor(node, packet, &failure) &&
                         packet.ResolveExecutionRange(0, 1, ranges[0], &failure) &&
                         packet.ResolveExecutionRange(1, 1, ranges[1], &failure) &&
                         packet.ResolveBufferUseBinding(importedUse, 1, resolvedImport, &failure) &&
                         packet.ResolveBufferUseBinding(nativeUse, 2, resolvedNative, &failure) &&
                         packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 2, cursor, &failure);
            // OpenCompiledCursor records the current incomingWait using
            // AddCommandListWait; no public getter for the recorded fence exists.
            valid = valid && cursor.BeginRange(ranges[0], 1, &failure) &&
                    resolvedImport.IsValid() && resolvedImport.GetBuffer() == current &&
                    cursor.EndRange(ranges[0], 1, &failure) && !resolvedImport.IsValid() &&
                    cursor.BeginRange(ranges[1], 2, &failure) &&
                    resolvedNative.IsValid() && resolvedNative.GetBuffer() != current &&
                    cursor.EndRange(ranges[1], 2, &failure) && !resolvedNative.IsValid() &&
                    cursor.FinalizePacket(&failure) && gpu::FlushPendingBarriers(&rhiFailure);
            if (gpu::GetBoundCommandList().IsValid())
                gpu::UnbindCommandList();
            if (!valid)
            {
                if (commands.IsValid()) gpu::DiscardCommandList(commands);
                return false;
            }
            const gpu::CommandListRef submissions[] = {commands};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("RF12 dynamic patched resources", {submissions, 1},
                                                 gpu::CommandListSyncType::None, completion, &rhiFailure))
                return false;
            const rendering::CommandScopeExecutionReceipt receipts[] = {
                {scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
            const rendering::TerminalExecutionReceipt terminal{
                generation, rendering::TerminalExecutionCompletionKind::Completed,
                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            rendering::PublishedResourceExport currentImport;
            rendering::PublishedResourceExport currentNative;
            if (!allocator.Finish(terminal, &failure) ||
                !allocator.TakeExport(importSlot, currentImport, &failure) ||
                !allocator.TakeExport(nativeSlot, currentNative, &failure) ||
                currentImport.GetSlot() != importSlot || currentNative.GetSlot() != nativeSlot ||
                currentImport.GetBuffer() != current || !currentNative.GetBuffer().IsValid() ||
                currentNative.GetBuffer() == current || currentImport.GetReadyFence() != completion ||
                currentNative.GetReadyFence() != completion || heldCount >= 4)
                return false;
            heldNative[heldCount++] = gpu::Buffer(currentNative.GetBuffer());
            currentImport.Reset();
            currentNative.Reset();
            return gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure);
        };

        rendering::ExportSlotId firstImportSlot;
        rendering::ExportSlotId firstNativeSlot;
        if (!runFrame(1212, original.GetRef(), coldWait, true, false, false, firstImportSlot, firstNativeSlot) ||
            !contract.IsValid() || contract.GetPublicationSerial() == 0)
            return fail("cold semantic contract / paired exports");
        const vanguard::u64 publicationSerial = contract.GetPublicationSerial();
        const rendering::RenderFlowResourceAllocatorStats coldStats = allocator.GetStats();
        if (coldStats.structuralPlanBuilds != 1 || coldStats.structuralPlanHits != 0 ||
            coldStats.persistentStateProgramBuilds != 1 || coldStats.persistentStateProgramHits != 0)
            return fail("cold structural witness");

        const gpu::BufferRef stale = original.GetRef();
        original.Reset();
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("old import released");
        gpu::BufferDesc staleDescription;
        if (gpu::GetBufferDesc(stale, staleDescription, &rhiFailure) || rhiFailure.code != gpu::FailureCode::InvalidReference)
            return fail("old import generation must be rejected");
        gpu::Buffer replacement(gpu::AdoptReference, gpu::CreateBuffer(externalDesc, {}, &rhiFailure));
        if (!replacement.IsValid() || replacement.GetRef() == stale ||
            replacement.GetRef().index != stale.index || replacement.GetRef().generation == stale.generation ||
            gpu::GetBufferDesc(stale, staleDescription, &rhiFailure))
            return fail("native replacement must recycle the stale index with an advanced generation");

        if (!submitFence(gpu::CommandListType::Default, warmWait) ||
            warmWait.queue != coldWait.queue || warmWait.value <= coldWait.value ||
            !runFrame(1213, replacement.GetRef(), warmWait, false, false, false, warmImportSlot, warmNativeSlot) ||
            warmImportSlot.index == firstImportSlot.index || warmNativeSlot.index == firstNativeSlot.index ||
            warmImportSlot.generation == firstImportSlot.generation ||
            contract.GetPublicationSerial() != publicationSerial)
            return fail("warm current native/wait and reverse export patches");
        const rendering::RenderFlowResourceAllocatorStats warmStats = allocator.GetStats();
        if (warmStats.structuralPlanBuilds != 1 || warmStats.structuralPlanHits != 1 || warmStats.planningWriters != 0 ||
            warmStats.persistentStateProgramBuilds != 1 || warmStats.persistentStateProgramHits != 1 ||
            warmStats.persistentStateProgramMisses != 0)
            return fail("warm semantic plan hit without declaration replay");

        // The queue is semantic topology; the current fence value is not.
        if (!submitFence(gpu::CommandListType::CopyAsync, copyWait) || copyWait.queue != gpu::QueueType::Copy)
            return fail("real Copy queue wait witness");
        rendering::ExportSlotId rejectedImportSlot;
        rendering::ExportSlotId rejectedNativeSlot;
        if (!registerPatches(1214, replacement.GetRef(), copyWait, true, rejectedImportSlot, rejectedNativeSlot) ||
            allocator.BindResourceContract(contract, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict ||
            contract.GetPublicationSerial() != publicationSerial)
            return fail("incoming wait queue semantic miss");
        allocator.CancelBeforePublication();

        rendering::ExportSlotId failedImportSlot;
        rendering::ExportSlotId failedNativeSlot;
        if (!runFrame(1215, replacement.GetRef(), warmWait, false, false, true, failedImportSlot, failedNativeSlot) ||
            contract.GetPublicationSerial() != publicationSerial)
            return fail("transactional warm Resolve native-creation failure");
        rendering::ExportSlotId recoveredImportSlot;
        rendering::ExportSlotId recoveredNativeSlot;
        if (!runFrame(1216, replacement.GetRef(), warmWait, false, false, false, recoveredImportSlot, recoveredNativeSlot) ||
            contract.GetPublicationSerial() != publicationSerial ||
            allocator.GetStats().structuralPlanBuilds != 1 ||
            allocator.GetStats().structuralPlanHits < 2)
            return fail("old published contract warm recovery");
        rendering::ExportSlotId abortedImportSlot;
        rendering::ExportSlotId abortedNativeSlot;
        if (!runFrame(1217, replacement.GetRef(), warmWait, false, true, false, abortedImportSlot, abortedNativeSlot) ||
            contract.GetPublicationSerial() != publicationSerial ||
            allocator.GetStats().outstandingPublishedExports != 0)
            return fail("aborted dynamic export must not publish");

        for (gpu::Buffer& held : heldNative) held.Reset();
        replacement.Reset();
        if (!allocator.ClearPersistentCaches(&failure) || !allocator.Shutdown(&failure) ||
            !gpu::FlushRetiredResources(&rhiFailure))
            return fail("dynamic patch cleanup");
        return true;
    }

    // RF12 cache-entry revision proof on the real D3D12 path.  Logical-only
    // validation intentionally never publishes persistent structural plans, so
    // the non-native allocator tests cover declaration/topology semantics while
    // this test proves the actual cache-entry-owned contract cold-recompiles.
    [[nodiscard]] bool RunRF12RevisionTopologyContractProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rf12-revision-topology] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        const auto makeKey = [](const vanguard::u64 rendererRevision) noexcept
        {
            rendering::RenderGraphKey key;
            key.renderExtent = {1, 1};
            key.outputExtent = {1, 1};
            key.rendererRevision = rendererRevision;
            key.RebuildHash();
            return key;
        };
        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;

        // Changed semantic swap order/count must select another cache entry and
        // publish a second cold structural contract with the changed mapping.
        {
            rendering::RenderGraphCache cache;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure))
                return fail("swap allocator initialize");
            bool needsRebuild = false;
            const rendering::RenderGraphKey baselineKey = makeKey(0x5100);
            rendering::RenderGraphCache::CacheEntry* const baseline = cache.GetGraph(0x5100, baselineKey, 0, needsRebuild);
            if (baseline == nullptr || !needsRebuild)
                return fail("swap baseline cache miss");
            constexpr rendering::RenderFlowNodeId node{0x510};
            constexpr rendering::GpuFlowGroupId flow{0x510};
            constexpr rendering::CommandScopeId scope{0x510};
            constexpr rendering::FlowSpaceId space{0x510};
            constexpr rendering::ResourceScopeId lifetimeScopes[] = {{0x5101}, {0x5102}, {0x5103}};
            const auto compileSwap = [&](rendering::RenderGraphCache::CacheEntry& entry, const vanguard::u64 frameSerial,
                                         const bool secondSwap) noexcept
            {
                entry.graph.BuildRenderFlowGroups();
                rendering::RenderNodeResourceBindings bindings;
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId resources[3];
                rendering::ResourceUseId originalUses[3];
                rendering::ResourceUseId finalUse;
                if (!allocator.BeginFrame(frameSerial, {}, &failure) ||
                    !allocator.BeginResourceContractCompilation(entry.resourceContract, &failure) ||
                    !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.DeclareBuffer({space, "rf12.revision.swap.a"}, bufferDesc, resources[0], &failure) ||
                    !writer.DeclareBuffer({space, "rf12.revision.swap.b"}, bufferDesc, resources[1], &failure) ||
                    !writer.DeclareBuffer({space, "rf12.revision.swap.c"}, bufferDesc, resources[2], &failure))
                    return false;
                for (vanguard::u32 index = 0; index < 3; ++index)
                    if (!writer.OpenResourceScope(resources[index], lifetimeScopes[index], &failure) ||
                        !writer.BeginBufferUse(resources[index], write, originalUses[index], &failure) || !writer.EndUse(originalUses[index], &failure))
                        return false;
                if (!writer.SwapLogicalMappings(resources[0], resources[1], &failure) ||
                    (secondSwap && !writer.SwapLogicalMappings(resources[0], resources[2], &failure)) ||
                    !writer.BeginBufferUse(resources[0], write, finalUse, &failure) || !writer.EndUse(finalUse, &failure))
                    return false;
                for (vanguard::u32 index = 3; index-- > 0;)
                    if (!writer.CloseResourceScope(lifetimeScopes[index], &failure))
                        return false;
                if (!writer.Close(&failure) || !entry.graph.CaptureResourceContract(entry.resourceContract, bindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) ||
                    !allocator.BeginExecution(&failure))
                    return false;

                rendering::CompiledExecutionPacketView packet;
                rendering::ResolvedBufferUse originals[3];
                rendering::ResolvedBufferUse finalResolved;
                bool resolved = allocator.PacketFor(node, packet, &failure);
                for (vanguard::u32 index = 0; resolved && index < 3; ++index)
                    resolved = packet.ResolveBufferUseBinding(originalUses[index], 1, originals[index], &failure);
                resolved = resolved && packet.ResolveBufferUseBinding(finalUse, 1, finalResolved, &failure);
                const rendering::PhysicalResourceId expected = resolved ? originals[secondSwap ? 2u : 1u].GetPhysicalResource() : rendering::PhysicalResourceId{};
                const rendering::PhysicalResourceId actual = resolved ? finalResolved.GetPhysicalResource() : rendering::PhysicalResourceId{};
                const bool mapped = expected.IsValid() && actual == expected &&
                                    originals[0].GetPhysicalResource() != originals[1].GetPhysicalResource() &&
                                    originals[0].GetPhysicalResource() != originals[2].GetPhysicalResource() &&
                                    originals[1].GetPhysicalResource() != originals[2].GetPhysicalResource();
                for (rendering::ResolvedBufferUse& original : originals)
                    original = {};
                finalResolved = {};
                const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt receipt{
                    scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
                const rendering::TerminalExecutionReceipt terminal{
                    generation, rendering::TerminalExecutionCompletionKind::Aborted,
                    vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                return mapped && allocator.Finish(terminal, &failure) && entry.resourceContract.IsValid() &&
                       entry.resourceContract.GetPublicationSerial() != 0;
            };
            if (!compileSwap(*baseline, 0x5100, false))
                return fail("baseline swap contract compile");
            baseline->PostBuildClear();
            bool warmNeedsRebuild = true;
            if (cache.GetGraph(0x5101, baselineKey, 0, warmNeedsRebuild) != baseline || warmNeedsRebuild)
                return fail("unchanged swap revision cache hit");
            rendering::RenderGraphKey revisedKey = baselineKey;
            ++revisedKey.rendererRevision;
            revisedKey.RebuildHash();
            rendering::RenderGraphCache::CacheEntry* const revised = cache.GetGraph(0x5102, revisedKey, 0, needsRebuild);
            if (revised == nullptr || revised == baseline || !needsRebuild || revised->resourceContract.IsValid() ||
                !compileSwap(*revised, 0x5102, true))
                return fail("changed swap revision cold contract compile");
            revised->PostBuildClear();
            const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
            if (stats.structuralPlanBuilds != 2 || stats.structuralPlanMisses != 2 || stats.structuralPlanHits != 0 ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("swap revision structural accounting/cleanup");
        }

        // The same cache-revision boundary must cold-compile a changed queue
        // topology rather than reusing the Graphics-only packet schedule.
        {
            rendering::RenderGraphCache cache;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure))
                return fail("queue allocator initialize");
            bool needsRebuild = false;
            const rendering::RenderGraphKey baselineKey = makeKey(0x5200);
            rendering::RenderGraphCache::CacheEntry* const baseline = cache.GetGraph(0x5200, baselineKey, 0, needsRebuild);
            if (baseline == nullptr || !needsRebuild)
                return fail("queue baseline cache miss");
            constexpr rendering::RenderFlowNodeId nodes[] = {{0x520}, {0x521}, {0x522}};
            constexpr rendering::GpuFlowGroupId flows[] = {{0x520}, {0x522}, {0x524}};
            constexpr rendering::GpuFlowGroupId forkFlow{0x521};
            constexpr rendering::GpuFlowGroupId joinFlow{0x523};
            constexpr rendering::CommandScopeId scopes[] = {{0x520}, {0x521}, {0x522}};
            constexpr rendering::LogicalResourceKey resourceKey{{0x520}, "rf12.revision.queue"};
            const auto compileQueue = [&](rendering::RenderGraphCache::CacheEntry& entry, const vanguard::u64 frameSerial,
                                          const bool asyncCompute) noexcept
            {
                entry.graph.BuildRenderFlowGroups();
                rendering::RenderNodeResourceBindings bindings;
                rendering::ResourcePlanningWriter writers[3];
                rendering::LogicalResourceId resources[3];
                rendering::ResourceUseId uses[3];
                if (!allocator.BeginFrame(frameSerial, {}, &failure) ||
                    !allocator.BeginResourceContractCompilation(entry.resourceContract, &failure))
                    return false;
                if (asyncCompute && (!allocator.RequestQueueSync(forkFlow, gpu::CommandListSyncType::ForkAsyncCompute, &failure) ||
                                     !allocator.RequestBeginQueue(gpu::QueueType::Compute, flows[1], &failure) || !allocator.RequestEndQueue(flows[1], &failure) ||
                                     !allocator.RequestQueueSync(joinFlow, gpu::CommandListSyncType::JoinAsyncCompute, &failure)))
                    return false;
                for (vanguard::u32 index = 0; index < 3; ++index)
                    if (!allocator.CreatePlanningWriter(nodes[index], flows[index], scopes[index], writers[index], &failure))
                        return false;
                bool planned = writers[0].DeclareBuffer(resourceKey, bufferDesc, resources[0], &failure) &&
                               writers[0].BeginBufferUse(resources[0], write, uses[0], &failure) && writers[0].EndUse(uses[0], &failure);
                for (vanguard::u32 index = 1; planned && index < 3; ++index)
                    planned = writers[index].ReferenceResource(resourceKey, resources[index], &failure) &&
                              writers[index].BeginBufferUse(resources[index], write, uses[index], &failure) && writers[index].EndUse(uses[index], &failure);
                for (vanguard::u32 index = 0; planned && index < 3; ++index)
                    planned = writers[index].Close(&failure);
                if (!planned || !entry.graph.CaptureResourceContract(entry.resourceContract, bindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) ||
                    !allocator.BeginExecution(&failure))
                    return false;
                rendering::CompiledExecutionPacketView packets[3];
                for (vanguard::u32 index = 0; index < 3; ++index)
                {
                    const gpu::QueueType expectedQueue = asyncCompute && index == 1 ? gpu::QueueType::Compute : gpu::QueueType::Graphics;
                    if (!allocator.PacketFor(nodes[index], packets[index], &failure) || packets[index].GetQueue() != expectedQueue)
                        return false;
                }
                const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt receipts[] = {
                    {scopes[0], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}},
                    {scopes[1], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission,
                     asyncCompute ? gpu::QueueType::Compute : gpu::QueueType::Graphics, {}},
                    {scopes[2], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
                const rendering::QueueDependencyExecutionReceipt dependencies[] = {
                    {scopes[0], scopes[1], gpu::CommandListSyncType::ForkAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission},
                    {scopes[1], scopes[2], gpu::CommandListSyncType::JoinAsyncCompute, rendering::QueueDependencyCompletionKind::DiscardedBeforeSubmission}};
                const rendering::TerminalExecutionReceipt terminal{
                    generation, rendering::TerminalExecutionCompletionKind::Aborted,
                    vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                    rendering::TerminalJoinToken::CompletedSynchronously(generation),
                    asyncCompute ? vanguard::containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>(dependencies)
                                 : vanguard::containers::ArraySpan<const rendering::QueueDependencyExecutionReceipt>{}};
                return allocator.Finish(terminal, &failure) && entry.resourceContract.IsValid() &&
                       entry.resourceContract.GetPublicationSerial() != 0;
            };
            if (!compileQueue(*baseline, 0x5200, false))
                return fail("baseline Graphics queue contract compile");
            baseline->PostBuildClear();
            bool warmNeedsRebuild = true;
            if (cache.GetGraph(0x5201, baselineKey, 0, warmNeedsRebuild) != baseline || warmNeedsRebuild)
                return fail("unchanged queue revision cache hit");
            rendering::RenderGraphKey revisedKey = baselineKey;
            ++revisedKey.rendererRevision;
            revisedKey.RebuildHash();
            rendering::RenderGraphCache::CacheEntry* const revised = cache.GetGraph(0x5202, revisedKey, 0, needsRebuild);
            if (revised == nullptr || revised == baseline || !needsRebuild || revised->resourceContract.IsValid() ||
                !compileQueue(*revised, 0x5202, true))
                return fail("changed queue revision cold contract compile");
            revised->PostBuildClear();
            const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
            if (stats.structuralPlanBuilds != 2 || stats.structuralPlanMisses != 2 || stats.structuralPlanHits != 0 ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("queue revision structural accounting/cleanup");
        }
        return true;
    }

    // RF12: the normalized contract is bound to its descriptor-domain owner,
    // not to the caller's semantic declarations alone. A fresh allocator on
    // the same device must reject the old domain before it may cold-republish.
    [[nodiscard]] bool RunRF12ContractDomainEpochProof() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rf12-contract-domain] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) |
                                                gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        rendering::RenderFlowResourceContract contract;
        rendering::RenderNodeGraph graph;
        graph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings bindings;
        const vanguard::u64 deviceIncarnation = gpu::GetDeviceIncarnation();
        if (deviceIncarnation == 0)
            return fail("initial device incarnation");

        vanguard::u64 publicationSerial = 0;
        gpu::DescriptorDomainRef oldDomainRef;
        {
            gpu::DescriptorDomain domain(gpu::AdoptReference,
                                         gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            rendering::RenderFlowResourceAllocator allocator;
            rendering::RenderFlowResourceAllocatorConfig config;
            config.resourceDescriptors = domain.GetRef();
            config.descriptorCacheTarget = 4;
            config.descriptorCacheMaximum = 4;
            if (!domain.IsValid() || !allocator.Initialize(config, &failure) || !allocator.BeginFrame(0xC120, {}, &failure) ||
                !CompileAndFinishRF12ScopeContract(allocator, contract, graph, bindings, failure))
                return fail("first domain cold contract publication");
            publicationSerial = contract.GetPublicationSerial();
            oldDomainRef = domain.GetRef();
            const rendering::RenderFlowResourceAllocatorStats cold = allocator.GetStats();
            if (publicationSerial == 0 || cold.structuralPlanBuilds != 1 || cold.structuralPlanMisses != 1 ||
                cold.structuralPlanHits != 0 || !allocator.Shutdown(&failure))
                return fail("first domain finish and shutdown");
        }
        if (!gpu::FlushRetiredResources(&rhiFailure) || gpu::GetDeviceIncarnation() != deviceIncarnation)
            return fail("same-device domain retirement");

        gpu::DescriptorDomain replacement(gpu::AdoptReference,
                                          gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = replacement.GetRef();
        config.descriptorCacheTarget = 4;
        config.descriptorCacheMaximum = 4;
        if (!replacement.IsValid() || replacement.GetRef() == oldDomainRef || !allocator.Initialize(config, &failure) ||
            !allocator.BeginFrame(0xC121, {}, &failure))
            return fail("replacement domain initialization");
        if (allocator.BindResourceContract(contract, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity ||
            allocator.GetState() != rendering::RenderFlowResourceSessionState::Planning ||
            !contract.IsValid() || contract.GetPublicationSerial() != publicationSerial ||
            gpu::GetDeviceIncarnation() != deviceIncarnation)
            return fail("old contract must reject replacement descriptor domain");

        if (!CompileAndFinishRF12ScopeContract(allocator, contract, graph, bindings, failure) ||
            contract.GetPublicationSerial() <= publicationSerial)
            return fail("replacement domain cold contract recovery");
        const rendering::RenderFlowResourceAllocatorStats recovered = allocator.GetStats();
        if (recovered.structuralPlanBuilds != 1 || recovered.structuralPlanMisses != 1 || recovered.structuralPlanHits != 0 ||
            !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("replacement domain clean shutdown");
        return true;
    }

    [[nodiscard]] bool RunExplicitBufferNoOpTransitionBoundaryTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        const auto fail = [&failure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[no-op-transition] %s failed (flow=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none");
            return false;
        };
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize({}, &failure))
            return fail("initialize");

        constexpr rendering::RenderFlowNodeId node{2};
        constexpr rendering::GpuFlowGroupId flow{2};
        constexpr rendering::CommandScopeId scope{2};
        constexpr rendering::FlowSpaceId space{2};
        rendering::ResourcePlanningWriter writer;
        rendering::FrameBufferDesc desc;
        desc.active.size = 256;
        desc.active.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::ShaderResource;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.active.keepInitialState = false;
        desc.maximumSize = desc.active.size;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId uploadUse;
        rendering::ResourceUseId computeReadUse;
        rendering::ResourceUseId repeatedReadUse;
        rendering::BufferUseDesc upload;
        upload.requiredState = gpu::ResourceState::CopyDestination;
        upload.access = rendering::LogicalAccessIntent::Write;
        upload.content = rendering::ResourceContentIntent::Discard;
        rendering::BufferUseDesc computeRead;
        computeRead.requiredState = gpu::ResourceState::ShaderResourceCompute;
        computeRead.access = rendering::LogicalAccessIntent::Read;
        computeRead.content = rendering::ResourceContentIntent::Preserve;
        if (!allocator.BeginFrame(3, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "explicit.buffer.noop.transition"}, desc, resource, &failure) ||
            !writer.BeginBufferUse(resource, upload, uploadUse, &failure) || !writer.EndUse(uploadUse, &failure) ||
            !writer.BeginBufferUse(resource, computeRead, computeReadUse, &failure) || !writer.EndUse(computeReadUse, &failure) ||
            !writer.BeginBufferUse(resource, computeRead, repeatedReadUse, &failure) || !writer.EndUse(repeatedReadUse, &failure) ||
            !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("plan/resolve");

        gpu::Failure rhiFailure;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x4e4f4f505452414eull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolvedUpload;
        rendering::ResolvedBufferUse resolvedRead;
        rendering::ResolvedBufferUse resolvedRepeatedRead;
        rendering::CompiledExecutionRange uploadRange;
        rendering::CompiledExecutionRange readRange;
        rendering::CompiledExecutionRange repeatedReadRange;
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.ResolveExecutionRange(0, 1, uploadRange, &failure) ||
            !packet.ResolveExecutionRange(1, 1, readRange, &failure) ||
            !packet.ResolveExecutionRange(2, 1, repeatedReadRange, &failure) ||
            !packet.ResolveBufferUseBinding(uploadUse, 1, resolvedUpload, &failure) ||
            !packet.ResolveBufferUseBinding(computeReadUse, 2, resolvedRead, &failure) ||
            !packet.ResolveBufferUseBinding(repeatedReadUse, 3, resolvedRepeatedRead, &failure) ||
            uploadRange.beginStepCount != 1 || uploadRange.endStepCount != 1 ||
            !uploadRange.beginActions.Empty() || !uploadRange.endActions.Empty() ||
            readRange.beginStepCount != 1 || readRange.endStepCount != 1 ||
            !readRange.beginActions.Empty() || !readRange.endActions.Empty() ||
            repeatedReadRange.beginStepCount != 1 || repeatedReadRange.endStepCount != 1 ||
            !repeatedReadRange.beginActions.Empty() || !repeatedReadRange.endActions.Empty() ||
            packet.GetExitActionSegment().count != 1 ||
            !packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 3, cursor, &failure) ||
            resolvedUpload.IsValid() || resolvedRead.IsValid() || resolvedRepeatedRead.IsValid() ||
            !cursor.BeginRange(uploadRange, 1, &failure) || !resolvedUpload.IsValid() || !resolvedUpload.GetBuffer().IsValid())
        {
            std::fprintf(stderr, "[no-op-transition] execution failed (flow=%u: %s, rhi=%u: %s)\n", static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return false;
        }
        const gpu::BufferRef physical = resolvedUpload.GetBuffer();
        if (!cursor.EndRange(uploadRange, 1, &failure) || resolvedUpload.IsValid() ||
            !cursor.BeginRange(readRange, 2, &failure) || !resolvedRead.IsValid() || resolvedRead.GetBuffer() != physical ||
            !cursor.EndRange(readRange, 2, &failure) || resolvedRead.IsValid() ||
            !cursor.BeginRange(repeatedReadRange, 3, &failure) || !resolvedRepeatedRead.IsValid() || resolvedRepeatedRead.GetBuffer() != physical ||
            !cursor.EndRange(repeatedReadRange, 3, &failure) || resolvedRepeatedRead.IsValid() || !cursor.FinalizePacket(&failure))
        {
            std::fprintf(stderr, "[no-op-transition] transition sequence failed (flow=%u: %s, rhi=%u: %s)\n", static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return false;
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow no-op transition boundary", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
        {
            std::fprintf(stderr, "[no-op-transition] submission failed (rhi=%u: %s)\n", static_cast<unsigned>(rhiFailure.code), rhiFailure.message);
            return false;
        }
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("finish/shutdown");
        return true;
    }

    [[nodiscard]] bool RunExplicitTextureSubresourceStateTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        const auto fail = [&failure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[explicit-texture-state] %s failed (flow=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none");
            return false;
        };
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize({}, &failure))
            return fail("initialize");

        constexpr rendering::RenderFlowNodeId node{3};
        constexpr rendering::GpuFlowGroupId flow{3};
        constexpr rendering::CommandScopeId scope{3};
        constexpr rendering::FlowSpaceId space{3};
        rendering::ResourcePlanningWriter writer;
        rendering::FrameTextureDesc desc;
        desc.active.extent = {8, 8, 1};
        desc.active.dimension = gpu::TextureDimension::Texture2D;
        desc.active.format = gpu::Format::R8G8B8A8UNorm;
        desc.active.mipCount = 3;
        desc.active.arraySize = 2;
        desc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.active.keepInitialState = false;
        desc.maximumExtent = desc.active.extent;
        desc.maximumMipCount = desc.active.mipCount;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId use;
        rendering::TextureUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.subresources = {2, 1, 1, 1};
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        if (!allocator.BeginFrame(4, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareTexture({space, "explicit.texture.subresource.state"}, desc, resource, &failure) ||
            !writer.BeginTextureUse(resource, write, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) ||
            !allocator.BeginExecution(&failure))
            return fail("plan/resolve");
        if (allocator.GetStats().compiledBarriers == 0)
            return fail("RF17 exact barrier program");

        gpu::Failure rhiFailure;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x4558505445585354ull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedTextureUse resolved;
        if (!commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || !cursor.BeginTextureUse(use, resolved, &failure) || !resolved.GetTexture().IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution");
        }
        const gpu::TextureRef physical = resolved.GetTexture();
        if (!cursor.EndUse(use, &failure) || !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("transition sequence");
        }
        gpu::TextureDesc physicalDesc;
        if (!gpu::GetTextureDesc(physical, physicalDesc, &rhiFailure) || physicalDesc.mipCount != desc.active.mipCount ||
            physicalDesc.arraySize != desc.active.arraySize || physicalDesc.initialState != desc.active.initialState || physicalDesc.keepInitialState)
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("physical descriptor");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow explicit texture state", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("finish/shutdown");
        return true;
    }

    [[nodiscard]] bool RunPartialTextureUavBarrierRejectionTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        const auto fail = [&failure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[partial-texture-uav-barrier] %s failed (flow=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none");
            return false;
        };

        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize({}, &failure))
            return fail("initialize");

        constexpr rendering::RenderFlowNodeId node{33};
        constexpr rendering::GpuFlowGroupId flow{33};
        constexpr rendering::CommandScopeId scope{33};
        constexpr rendering::FlowSpaceId space{33};
        rendering::ResourcePlanningWriter writer;
        rendering::FrameTextureDesc desc;
        desc.active.extent = {8, 8, 1};
        desc.active.dimension = gpu::TextureDimension::Texture2D;
        desc.active.format = gpu::Format::R8G8B8A8UNorm;
        desc.active.mipCount = 2;
        desc.active.arraySize = 1;
        desc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.active.keepInitialState = false;
        desc.maximumExtent = desc.active.extent;
        desc.maximumMipCount = desc.active.mipCount;

        rendering::TextureUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.subresources = {0, 1, 0, 1};
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::TextureUseDesc rewrite = write;
        rewrite.content = rendering::ResourceContentIntent::Preserve;
        rendering::LogicalResourceId resource;
        rendering::ResourceUseId uses[2];
        if (!allocator.BeginFrame(33, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareTexture({space, "partial.texture.uav.barrier"}, desc, resource, &failure) ||
            !writer.BeginTextureUse(resource, write, uses[0], &failure) || !writer.EndUse(uses[0], &failure) ||
            !writer.BeginTextureUse(resource, rewrite, uses[1], &failure) || !writer.EndUse(uses[1], &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("partial UAV resolve");

        // RF11 makes the backend's whole-resource UAV barrier legal for a
        // partial texture range by normalizing untouched subresources around
        // the barrier and restoring their exact prior state afterwards.
        gpu::Failure rhiFailure;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5054554156424152ull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedTextureUse resolved;
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) ||
            !cursor.BeginTextureUse(uses[0], resolved, &failure) || !resolved.IsValid() || !cursor.EndUse(uses[0], &failure) || resolved.IsValid() ||
            !cursor.BeginTextureUse(uses[1], resolved, &failure) || !resolved.IsValid() || !cursor.EndUse(uses[1], &failure) || resolved.IsValid() ||
            !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("partial UAV execution");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow partial texture UAV barrier", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("partial UAV submission");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("partial UAV finish/shutdown");
        return true;
    }

    [[nodiscard]] bool RunExplicitTextureCoalescedTransitionStateTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&failure, &rhiFailure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[explicit-texture-coalesce] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message);
            return false;
        };

        rendering::RenderFlowResourceAllocatorConfig config;
        // Exact native actions: two predecessor transitions, two heterogeneous
        // consumer transitions, and one coalesced final Common restoration.
        config.maximumCompiledResourceActions = 5;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize(config, &failure))
            return fail("initialize");

        constexpr rendering::RenderFlowNodeId nodes[] = {{4}, {5}};
        constexpr rendering::GpuFlowGroupId flows[] = {{4}, {5}};
        constexpr rendering::CommandScopeId scopes[] = {{4}, {5}};
        constexpr rendering::FlowSpaceId space{4};
        rendering::ResourcePlanningWriter writers[2];
        rendering::FrameTextureDesc desc;
        desc.active.extent = {8, 8, 1};
        desc.active.dimension = gpu::TextureDimension::Texture2D;
        desc.active.format = gpu::Format::R8G8B8A8UNorm;
        desc.active.mipCount = 2;
        desc.active.arraySize = 2;
        desc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess | gpu::TextureUsage::CopyDestination;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.active.keepInitialState = false;
        desc.maximumExtent = desc.active.extent;
        desc.maximumMipCount = desc.active.mipCount;

        rendering::LogicalResourceId resources[2];
        rendering::ResourceUseId uses[3];
        rendering::TextureUseDesc firstWrite;
        firstWrite.requiredState = gpu::ResourceState::UnorderedAccess;
        firstWrite.subresources = {0, 1, 0, 2};
        firstWrite.access = rendering::LogicalAccessIntent::Write;
        firstWrite.content = rendering::ResourceContentIntent::Discard;
        rendering::TextureUseDesc secondWrite = firstWrite;
        secondWrite.requiredState = gpu::ResourceState::CopyDestination;
        secondWrite.subresources = {1, 1, 0, 2};
        rendering::TextureUseDesc combinedRead;
        combinedRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        combinedRead.subresources = {0, 2, 0, 2};
        combinedRead.access = rendering::LogicalAccessIntent::Read;
        combinedRead.content = rendering::ResourceContentIntent::Preserve;

        constexpr rendering::LogicalResourceKey key{space, "explicit.texture.coalesced.transition.state"};
        if (!allocator.BeginFrame(5, {}, &failure) || !allocator.CreatePlanningWriter(nodes[0], flows[0], scopes[0], writers[0], &failure) ||
            !allocator.CreatePlanningWriter(nodes[1], flows[1], scopes[1], writers[1], &failure) || !writers[0].DeclareTexture(key, desc, resources[0], &failure) ||
            !writers[0].BeginTextureUse(resources[0], firstWrite, uses[0], &failure) || !writers[0].EndUse(uses[0], &failure) ||
            !writers[0].BeginTextureUse(resources[0], secondWrite, uses[1], &failure) || !writers[0].EndUse(uses[1], &failure) ||
            !writers[1].ReferenceResource(key, resources[1], &failure) || !writers[1].BeginTextureUse(resources[1], combinedRead, uses[2], &failure) ||
            !writers[1].EndUse(uses[2], &failure) || !writers[0].Close(&failure) || !writers[1].Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("plan/resolve");

        gpu::CommandListRef commandLists[2];
        gpu::TextureRef physical;
        for (vanguard::u32 packetIndex = 0; packetIndex < 2; ++packetIndex)
        {
            commandLists[packetIndex] = gpu::CreateCommandList(gpu::CommandListType::Default, 0x434f414c45534300ull + packetIndex, &rhiFailure);
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            if (!commandLists[packetIndex].IsValid() || !gpu::BindCommandList(commandLists[packetIndex], &rhiFailure) || !allocator.PacketFor(nodes[packetIndex], packet, &failure) ||
                !packet.OpenCursor(scopes[packetIndex], gpu::QueueType::Graphics, cursor, &failure))
            {
                gpu::UnbindCommandList();
                return fail("packet recording");
            }
            if (packetIndex == 0)
            {
                rendering::ResolvedTextureUse resolved[2];
                if (!cursor.BeginTextureUse(uses[0], resolved[0], &failure) || !resolved[0].GetTexture().IsValid())
                {
                    gpu::UnbindCommandList();
                    return fail("heterogeneous predecessor recording");
                }
                physical = resolved[0].GetTexture();
                if (!cursor.EndUse(uses[0], &failure) || !cursor.BeginTextureUse(uses[1], resolved[1], &failure) || resolved[1].GetTexture() != physical || !cursor.EndUse(uses[1], &failure))
                {
                    gpu::UnbindCommandList();
                    return fail("heterogeneous predecessor recording");
                }
            }
            else
            {
                rendering::ResolvedTextureUse resolved;
                if (!cursor.BeginTextureUse(uses[2], resolved, &failure) || resolved.GetTexture() != physical || !cursor.EndUse(uses[2], &failure))
                {
                    gpu::UnbindCommandList();
                    return fail("coalesced consumer recording");
                }
            }
            if (!cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                return fail("packet finalization");
            }
            gpu::UnbindCommandList();
        }

        gpu::GpuFence completions[2];
        for (vanguard::u32 index = 0; index < 2; ++index)
        {
            const gpu::CommandListRef submission[] = {commandLists[index]};
            if (!gpu::CloseAndSubmitCommandLists(index == 0 ? "resource-flow heterogeneous texture predecessor" : "resource-flow coalesced texture consumer", {submission, 1},
                                                 gpu::CommandListSyncType::None, completions[index], &rhiFailure))
                return fail("submission");
        }
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scopes[0], rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completions[0]},
                                                                    {scopes[1], rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completions[1]}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completions[1], 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("finish/shutdown");
        return true;
    }

    [[nodiscard]] bool RunDedicatedResourceLifetimeReuseTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-lifetime-reuse] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage,
                         static_cast<unsigned>(failure.code), failure.message != nullptr ? failure.message : "none",
                         static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        const gpu::ShaderStageMask descriptorVisibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                          gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, descriptorVisibility}, &rhiFailure));
        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = descriptors.GetRef();
        config.descriptorCacheTarget = 4;
        config.descriptorCacheMaximum = 4;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::ResourcePlanningWriter writer;
        constexpr rendering::RenderFlowNodeId node{8};
        constexpr rendering::GpuFlowGroupId flow{8};
        constexpr rendering::CommandScopeId scope{8};
        constexpr rendering::FlowSpaceId space{8};
        rendering::FrameBufferDesc desc;
        desc.active.size = 4096;
        desc.active.structureStride = 16;
        desc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.maximumSize = desc.active.size;
        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::BufferUseDesc clear = write;
        clear.content = rendering::ResourceContentIntent::Clear;
        clear.clearValue = rendering::BufferClearValue::Uint(0xdecafbad);
        rendering::LogicalResourceId resources[4];
        rendering::LogicalBufferViewId firstExplicitView;
        rendering::ResourceUseId uses[4];
        if (!descriptors.IsValid() || !allocator.Initialize(config, &failure) || !allocator.BeginFrame(8, {}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "dedicated.lifetime.a"}, desc, resources[0], &failure) || !writer.DeclareBuffer({space, "dedicated.lifetime.b"}, desc, resources[1], &failure) ||
            !writer.DeclareBuffer({space, "dedicated.lifetime.c"}, desc, resources[2], &failure) || !writer.DeclareBuffer({space, "dedicated.lifetime.d"}, desc, resources[3], &failure) ||
            !writer.CreateBufferView(resources[0], {}, firstExplicitView, &failure) ||
            !writer.BeginBufferViewUse(firstExplicitView, write, uses[0], &failure) ||
            !writer.BeginBufferUse(resources[1], write, uses[1], &failure) || !writer.EndUse(uses[0], &failure) || !writer.EndUse(uses[1], &failure) ||
            !writer.BeginBufferUse(resources[2], write, uses[2], &failure) || !writer.EndUse(uses[2], &failure) ||
            !writer.BeginBufferUse(resources[3], clear, uses[3], &failure) || !writer.EndUse(uses[3], &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolved[4];
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246414C49464554ull, &rhiFailure);
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) ||
            !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) || !cursor.BeginBufferUse(uses[0], resolved[0], &failure) ||
            !cursor.BeginBufferUse(uses[1], resolved[1], &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("resolve/execution setup");
        }
        const gpu::BufferRef first = resolved[0].GetBuffer();
        const gpu::BufferRef overlapping = resolved[1].GetBuffer();
        if (!first.IsValid() || !overlapping.IsValid() || first == overlapping || !resolved[0].HasExplicitView() || resolved[0].GetShaderResourceDescriptor().IsValid() ||
            resolved[0].GetUnorderedAccessDescriptor().IsValid() || !resolved[1].GetShaderResourceDescriptor().IsValid() || !resolved[1].GetUnorderedAccessDescriptor().IsValid() ||
            !cursor.EndUse(uses[0], &failure) || !cursor.EndUse(uses[1], &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("overlap/explicit-view semantics");
        }
        if (!cursor.BeginBufferUse(uses[2], resolved[2], &failure) || resolved[2].GetBuffer() != first ||
            !resolved[2].GetShaderResourceDescriptor().IsValid() || !resolved[2].GetUnorderedAccessDescriptor().IsValid())
        {
            std::fprintf(stderr, "[resource-flow-lifetime-reuse] late reuse first=%u:%u later=%u:%u srv=%u:%u uav=%u:%u\n",
                         first.index, first.generation, resolved[2].GetBuffer().index, resolved[2].GetBuffer().generation,
                         resolved[2].GetShaderResourceDescriptor().index, resolved[2].GetShaderResourceDescriptor().generation,
                         resolved[2].GetUnorderedAccessDescriptor().index, resolved[2].GetUnorderedAccessDescriptor().generation);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("same-frame late canonical demand");
        }
        if (!cursor.EndUse(uses[2], &failure) || !cursor.BeginBufferUse(uses[3], resolved[3], &failure) || resolved[3].GetBuffer() != first ||
            !cursor.EndUse(uses[3], &failure) || !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("late-use finalization");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow same-frame dedicated reuse", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
            return fail("finish/wait");
        const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
        const gpu::DescriptorDomainStats descriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (stats.dedicatedResourcePoolMisses != 2 || stats.chargedBufferBytes == 0 || stats.canonicalDescriptorLiveSlots != 4 || descriptorStats.allocated != 4 || descriptorStats.populated != 4)
        {
            std::fprintf(stderr, "[resource-flow-lifetime-reuse] stats misses=%llu charged=%llu live=%u allocated=%u populated=%u\n",
                         static_cast<unsigned long long>(stats.dedicatedResourcePoolMisses), static_cast<unsigned long long>(stats.chargedBufferBytes),
                         stats.canonicalDescriptorLiveSlots, descriptorStats.allocated, descriptorStats.populated);
            return fail("post-finish descriptor accounting");
        }
        if (!allocator.Shutdown(&failure))
            return fail("shutdown");
        const gpu::DescriptorDomainStats shutdownStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        return shutdownStats.allocated == 0 && shutdownStats.populated == 0 && shutdownStats.pendingRetirement == 0;
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

        // RF6 topology invalidation must release native budget, not merely turn
        // PlanReady into generic dedicated reuse. A placed frame cannot consume
        // a dedicated reusable object, so a shared hard ledger would otherwise
        // remain blocked forever when soft targets are disabled/unbounded.
        {
            rendering::RenderFlowResourceAllocatorConfig transitionConfig;
            transitionConfig.hardNativeByteLimit = requirements.size > plan.heaps[0].capacity ? requirements.size : plan.heaps[0].capacity;
            rendering::detail::AllocatorNativeByteLedger transitionLedger(transitionConfig.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool transitionDedicated(transitionConfig, transitionLedger);
            rendering::detail::PlacedResourcePool transitionPlaced(transitionConfig, transitionLedger);
            rendering::detail::DedicatedResourceAssignment dedicatedAssignment;
            if (!transitionDedicated.Acquire(rendering::FrameResourceDesc::Buffer(bufferDesc), 5, dedicatedAssignment, &failure))
                return fail("RF6 placed-transition dedicated acquire");
            rendering::detail::DedicatedResourceLease dedicatedLease;
            if (!transitionDedicated.ReserveLease(dedicatedAssignment.entry, dedicatedLease))
                return fail("RF6 placed-transition lease reserve");
            transitionDedicated.Rollback(dedicatedAssignment.entry);
            transitionDedicated.ReleaseLease(dedicatedLease);

            rendering::detail::PlacedResourceBatch budgetBlocked;
            if (transitionPlaced.Acquire(plan, objects, 6, budgetBlocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("RF6 placed-transition reproduces shared-budget block");

            transitionDedicated.EvictLease(dedicatedLease);
            transitionDedicated.Poll();
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("RF6 placed-transition native release");
            transitionDedicated.Poll();
            rendering::detail::DedicatedResourceAssignment staleLease;
            if (transitionDedicated.TryAcquireLease(dedicatedLease, rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(bufferDesc)), 7,
                                                    staleLease, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Stale ||
                transitionLedger.GetStats().chargedBytes != 0)
                return fail("RF6 placed-transition dedicated budget reclamation");

            rendering::detail::PlacedResourceBatch recovered;
            if (!transitionPlaced.Acquire(plan, objects, 7, recovered, &failure))
                return fail("RF6 placed-transition placed retry");
            transitionPlaced.Rollback(recovered);
            if (!transitionPlaced.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("RF6 placed-transition cleanup");
            transitionPlaced.Poll();
            if (transitionLedger.GetStats().chargedBytes != 0)
                return fail("RF6 placed-transition final ledger cleanup");
        }

        // Normal frame progression polls once at BeginFrame. A direct provider
        // acquisition does not rescan retirement state unless native-byte
        // pressure requires it; then one forced Poll+retry may recover a now-safe
        // exact heap instead of failing the hard budget.
        rendering::RenderFlowResourceAllocatorConfig pressureConfig = config;
        pressureConfig.hardNativeByteLimit = plan.heaps[0].capacity;
        rendering::detail::AllocatorNativeByteLedger pressureLedger(pressureConfig.hardNativeByteLimit);
        rendering::detail::PlacedResourcePool pressurePool(pressureConfig, pressureLedger);
        rendering::detail::PlacedResourceBatch pressureFirst;
        if (!pressurePool.Acquire(plan, objects, 5, pressureFirst, &failure))
            return fail("placed pressure-poll setup acquisition");
        const vanguard::u32 pressureHeap = pressureFirst.heapEntries[0];
        const gpu::BufferRef pressureObject = pressureFirst.assignments[0].buffer;
        pressurePool.Retire(pressureFirst, {});
        rendering::detail::PlacedResourceBatch pressureReused;
        if (!pressurePool.Acquire(plan, objects, 6, pressureReused, &failure) || pressureReused.heapEntries[0] != pressureHeap || pressureReused.assignments[0].buffer != pressureObject ||
            pressurePool.GetStats().heapHits != 1)
            return fail("placed hard-budget pressure performs one retirement Poll and exact reuse retry");
        pressurePool.Rollback(pressureReused);
        if (!pressurePool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("placed pressure-poll cleanup");
        pressurePool.Poll();
        if (pressureLedger.GetStats().chargedBytes != 0)
            return fail("placed pressure-poll final ledger cleanup");

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
        livePool.Poll();
        if (!gpu::FlushRetiredResources(&rhiFailure))
            return fail("live batch native release");
        livePool.Poll();
        livePool.Rollback(held);
        livePool.Poll();
        rendering::detail::PlacedResourceBatch compacted;
        if (!livePool.Acquire(plan, objects, 41, compacted, &failure) || compacted.heapEntries[0] != 0 || compacted.assignments[0].buffer != heldObject)
            return fail("live batch indices survive deferred compaction");
        livePool.Rollback(compacted);

        // A placed object with an external owner keeps its whole heap out of
        // the reusable/trim indices. Reuse may resume only after pool-only
        // ownership is restored, otherwise another placed object could bind
        // the same native heap range while the old object is still live.
        rendering::RenderFlowResourceAllocatorConfig heldConfig = config;
        heldConfig.hardNativeByteLimit = plan.heaps[0].capacity * 2u;
        heldConfig.bufferSoftTargetBytes = 0;
        rendering::detail::AllocatorNativeByteLedger heldLedger(heldConfig.hardNativeByteLimit);
        rendering::detail::PlacedResourcePool heldPool(heldConfig, heldLedger);
        rendering::detail::PlacedResourceBatch heldFirst;
        rendering::detail::PlacedResourceBatch heldSecond;
        if (!heldPool.Acquire(plan, objects, 50, heldFirst, &failure) || !heldPool.Acquire(plan, objects, 51, heldSecond, &failure))
            return fail("held placed heap setup");
        const vanguard::u32 heldFirstHeap = heldFirst.heapEntries[0];
        const vanguard::u32 heldSecondHeap = heldSecond.heapEntries[0];
        if (heldFirstHeap == heldSecondHeap)
            return fail("held placed heap setup isolation");
        const gpu::BufferRef latePinnedRef = heldSecond.assignments[0].buffer;
        const gpu::BufferRef latePinnedSecondRef = heldSecond.assignments[1].buffer;
        heldPool.Rollback(heldFirst);
        heldPool.Rollback(heldSecond);
        gpu::Buffer pinned(latePinnedRef);
        gpu::Buffer pinnedSecond(latePinnedSecondRef);

        rendering::detail::PlacedResourceBatch heldThird;
        if (!heldPool.Acquire(plan, objects, 52, heldThird, &failure) || heldThird.heapEntries[0] != heldFirstHeap)
            return fail("late external owner demotes published placed heap before reuse");
        heldPool.Rollback(heldThird);

        rendering::detail::ResourcePoolTrimState heldTrim;
        heldTrim.projectedBufferBytes = heldLedger.GetStats().bufferBytes;
        if (!heldPool.TrimToSoftTargets(heldTrim, &failure) || heldTrim.projectedBufferBytes != plan.heaps[0].capacity ||
            !gpu::FlushRetiredResources(&rhiFailure))
            return fail("held placed heap trim isolation");
        heldPool.Poll();
        if (heldLedger.GetStats().bufferBytes != plan.heaps[0].capacity)
            return fail("held placed heap remains charged while externally owned");
        if (heldPool.ClearPersistentCache(&failure) || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation)
            return fail("held placed heap cache-clear ownership guard");

        pinned.Reset();
        heldPool.Poll();
        if (heldPool.ClearPersistentCache(&failure) || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation)
            return fail("held placed heap advances its owner witness without publishing while a second external owner remains");
        pinnedSecond.Reset();
        heldPool.Poll();
        if (!heldPool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("held placed heap promotion and release");
        heldPool.Poll();
        if (heldLedger.GetStats().chargedBytes != 0)
            return fail("held placed heap final ledger release");

        // Texture and buffer soft targets are independent. Keep an older texture
        // heap reusable while only buffers are over target; trim must select the
        // buffer directly rather than churn the other kind through a global heap.
        const gpu::Capabilities::PlacedResourceClass& texturePlacedClass = gpu::GetCapabilities().placedResources.textures;
        if (texturePlacedClass.IsSupported())
        {
            rendering::FrameTextureDesc textureDesc;
            textureDesc.active.extent = {8, 8, 1};
            textureDesc.active.dimension = gpu::TextureDimension::Texture2D;
            textureDesc.active.format = gpu::Format::R8G8B8A8UNorm;
            textureDesc.active.mipCount = 1;
            textureDesc.active.arraySize = 1;
            textureDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
            textureDesc.active.initialState = gpu::ResourceState::Common;
            textureDesc.maximumExtent = textureDesc.active.extent;
            textureDesc.maximumMipCount = 1;
            gpu::TextureDesc virtualTextureDesc = textureDesc.active;
            virtualTextureDesc.virtualResource = true;
            gpu::MemoryRequirements textureRequirements;
            if (!gpu::GetMemoryRequirements(virtualTextureDesc, textureRequirements, &rhiFailure) || textureRequirements.size == 0 || textureRequirements.alignment == 0)
                return fail("virtual texture requirements");

            rendering::detail::PlacedRangePlannerConfig texturePlannerConfig;
            texturePlannerConfig.minimumHeapBytes = texturePlacedClass.maximumHeapAlignment;
            texturePlannerConfig.heapAlignment = texturePlacedClass.maximumHeapAlignment;
            rendering::detail::PlacedRangeRequest textureRange;
            textureRange.allocation = 90;
            textureRange.kind = rendering::FrameResourceKind::Texture;
            textureRange.queue = gpu::QueueType::Graphics;
            textureRange.firstAcquire = {{1}, 0};
            textureRange.lastRelease = {{2}, 1};
            textureRange.requirements = textureRequirements;
            rendering::detail::PlacedRangePlan texturePlan;
            if (!rendering::detail::BuildPlacedRangePlan({&textureRange, 1}, texturePlannerConfig, texturePlan, &failure) || texturePlan.heaps.Size() != 1)
                return fail("texture trim-isolation plan");
            const rendering::detail::PlacedObjectRequest textureObject[] = {
                {90, rendering::FrameResourceDesc::Texture(textureDesc)},
            };

            rendering::RenderFlowResourceAllocatorConfig splitTrimConfig;
            splitTrimConfig.hardNativeByteLimit = texturePlan.heaps[0].capacity + plan.heaps[0].capacity;
            splitTrimConfig.textureSoftTargetBytes = texturePlan.heaps[0].capacity;
            splitTrimConfig.bufferSoftTargetBytes = 0;
            rendering::detail::AllocatorNativeByteLedger splitTrimLedger(splitTrimConfig.hardNativeByteLimit);
            rendering::detail::PlacedResourcePool splitTrimPool(splitTrimConfig, splitTrimLedger);
            rendering::detail::PlacedResourceBatch textureBatch;
            rendering::detail::PlacedResourceBatch bufferBatch;
            if (!splitTrimPool.Acquire(texturePlan, textureObject, 60, textureBatch, &failure))
                return fail("split trim older texture acquisition");
            splitTrimPool.Rollback(textureBatch);
            if (!splitTrimPool.Acquire(plan, objects, 61, bufferBatch, &failure))
                return fail("split trim newer buffer acquisition");
            splitTrimPool.Rollback(bufferBatch);

            rendering::detail::ResourcePoolTrimState splitTrim;
            const rendering::detail::AllocatorNativeByteLedgerStats splitCharged = splitTrimLedger.GetStats();
            splitTrim.projectedTextureBytes = splitCharged.textureBytes;
            splitTrim.projectedBufferBytes = splitCharged.bufferBytes;
            if (!splitTrimPool.TrimToSoftTargets(splitTrim, &failure) || splitTrim.projectedTextureBytes != texturePlan.heaps[0].capacity || splitTrim.projectedBufferBytes != 0)
                return fail("split placed trim heaps isolate the over-target resource kind");
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("split placed trim native release");
            splitTrimPool.Poll();
            const rendering::detail::AllocatorNativeByteLedgerStats afterSplitTrim = splitTrimLedger.GetStats();
            if (afterSplitTrim.textureBytes != texturePlan.heaps[0].capacity || afterSplitTrim.bufferBytes != 0)
                return fail("split placed trim preserves the non-over-target texture heap");
            if (!splitTrimPool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("split placed trim cleanup");
            splitTrimPool.Poll();
            if (splitTrimLedger.GetStats().chargedBytes != 0)
                return fail("split placed trim final ledger cleanup");
        }

        // RF3: canonical descriptors belong to the placed native object, not to
        // the generation. Aliased objects sharing one heap range must keep
        // distinct bundles, warm hits must preserve their GPU indices, and the
        // descriptor ownership baseline must be exactly 1 + descriptorCount.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 16, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 placed canonical descriptor domain");

            rendering::FrameBufferDesc canonicalBufferDesc = bufferDesc;
            canonicalBufferDesc.active.structureStride = 16;
            canonicalBufferDesc.active.usage = canonicalBufferDesc.active.usage | gpu::BufferUsage::Structured;
            gpu::BufferDesc canonicalVirtualDesc = canonicalBufferDesc.active;
            canonicalVirtualDesc.virtualResource = true;
            gpu::MemoryRequirements canonicalRequirements;
            if (!gpu::GetMemoryRequirements(canonicalVirtualDesc, canonicalRequirements, &rhiFailure) || canonicalRequirements.size == 0 || canonicalRequirements.alignment == 0)
                return fail("RF3 placed canonical requirements");
            const rendering::detail::PlacedRangeRequest canonicalRequests[] = {
                {0, rendering::FrameResourceKind::Buffer, gpu::QueueType::Graphics, {{1}, 0}, {{2}, 1}, rendering::InvalidRenderFlowResourceIndex,
                 rendering::InvalidRenderFlowResourceIndex, canonicalRequirements},
                {1, rendering::FrameResourceKind::Buffer, gpu::QueueType::Graphics, {{3}, 2}, {{4}, 3}, rendering::InvalidRenderFlowResourceIndex,
                 rendering::InvalidRenderFlowResourceIndex, canonicalRequirements},
            };
            rendering::detail::PlacedRangePlan canonicalPlan;
            if (!rendering::detail::BuildPlacedRangePlan(canonicalRequests, plannerConfig, canonicalPlan, &failure) || canonicalPlan.heaps.Size() != 1 || canonicalPlan.assignments.Size() != 2 ||
                canonicalPlan.assignments[0].offset != canonicalPlan.assignments[1].offset)
                return fail("RF3 placed canonical alias plan");

            rendering::RenderFlowResourceAllocatorConfig descriptorConfig = config;
            descriptorConfig.hardNativeByteLimit = canonicalPlan.heaps[0].capacity;
            descriptorConfig.descriptorCacheTarget = 8;
            descriptorConfig.descriptorCacheMaximum = 8;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(descriptorConfig.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(descriptorConfig.descriptorCacheTarget, descriptorConfig.descriptorCacheMaximum);
            rendering::detail::PlacedResourcePool descriptorPool(descriptorConfig, nativeLedger, descriptorLedger, descriptors.GetRef());
            const rendering::detail::PlacedObjectRequest descriptorObjects[] = {
                {0, rendering::FrameResourceDesc::Buffer(canonicalBufferDesc), true},
                {1, rendering::FrameResourceDesc::Buffer(canonicalBufferDesc), true},
            };

            rendering::detail::PlacedResourceBatch cold;
            if (!descriptorPool.Acquire(canonicalPlan, descriptorObjects, 100, cold, &failure) || cold.assignments.Size() != 2 ||
                !cold.assignments[0].shaderResource.IsValid() || !cold.assignments[0].unorderedAccess.IsValid() ||
                !cold.assignments[1].shaderResource.IsValid() || !cold.assignments[1].unorderedAccess.IsValid() ||
                cold.assignments[0].shaderResource == cold.assignments[1].shaderResource ||
                cold.assignments[0].unorderedAccess == cold.assignments[1].unorderedAccess)
                return fail("RF3 placed cold canonical publication and alias separation");
            const gpu::BufferRef firstAlias = cold.assignments[0].buffer;
            const gpu::BufferRef secondAlias = cold.assignments[1].buffer;
            const vanguard::u32 firstSrvIndex = cold.assignments[0].shaderResource.GpuIndex();
            const vanguard::u32 firstUavIndex = cold.assignments[0].unorderedAccess.GpuIndex();
            const vanguard::u32 secondSrvIndex = cold.assignments[1].shaderResource.GpuIndex();
            const vanguard::u32 secondUavIndex = cold.assignments[1].unorderedAccess.GpuIndex();
            descriptorPool.Rollback(cold);
            const rendering::detail::AllocatorDescriptorLedgerStats coldStats = descriptorLedger.GetStats();
            if (coldStats.liveSlots != 4 || coldStats.reusableSlots != 4 || coldStats.publicationMisses != 2 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 4)
                return fail("RF3 placed cold descriptor ledger accounting");

            rendering::detail::PlacedResourceBatch warm;
            if (!descriptorPool.Acquire(canonicalPlan, descriptorObjects, 101, warm, &failure) || warm.assignments[0].buffer != firstAlias || warm.assignments[1].buffer != secondAlias ||
                warm.assignments[0].shaderResource.GpuIndex() != firstSrvIndex || warm.assignments[0].unorderedAccess.GpuIndex() != firstUavIndex ||
                warm.assignments[1].shaderResource.GpuIndex() != secondSrvIndex || warm.assignments[1].unorderedAccess.GpuIndex() != secondUavIndex)
                return fail("RF3 placed warm canonical reuse");
            const vanguard::u32 firstObjectEntry = warm.assignments[0].objectEntry;
            descriptorPool.Rollback(warm);
            if (descriptorLedger.GetStats().publicationHits != 2 || descriptorLedger.GetStats().liveSlots != 4 || descriptorLedger.GetStats().reusableSlots != 4)
                return fail("RF3 placed warm hit accounting");

            // One escaped owner raises the exact expected refcount above
            // 1+descriptorCount. Pressure must demote that object's heap instead
            // of evicting it; once the owner disappears, the held proof restores
            // descriptor eligibility without a frame-wide object scan.
            gpu::Buffer escaped(firstAlias);
            rendering::detail::CanonicalDescriptorEvictionCandidate heldCandidate;
            if (!descriptorPool.GetColdestCanonicalDescriptorCandidate(heldCandidate) || heldCandidate.entry != firstObjectEntry ||
                descriptorPool.EvictCanonicalDescriptorCandidate(heldCandidate, true, &failure) != rendering::detail::CanonicalDescriptorEvictionResult::Skipped ||
                descriptorLedger.GetStats().liveSlots != 4 || descriptorLedger.GetStats().reusableSlots != 2)
                return fail("RF3 placed escaped-owner descriptor guard");
            escaped.Reset();
            descriptorPool.Poll();
            if (descriptorLedger.GetStats().reusableSlots != 4)
                return fail("RF3 placed held-owner descriptor recovery");

            rendering::detail::CanonicalDescriptorEvictionCandidate coldCandidate;
            if (!descriptorPool.GetColdestCanonicalDescriptorCandidate(coldCandidate) ||
                descriptorPool.EvictCanonicalDescriptorCandidate(coldCandidate, true, &failure) != rendering::detail::CanonicalDescriptorEvictionResult::Evicted ||
                descriptorLedger.GetStats().liveSlots != 2 || descriptorLedger.GetStats().reusableSlots != 2 || descriptorLedger.GetStats().pressureEvictions != 1)
                return fail("RF3 placed descriptor-only pressure eviction");
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("RF3 placed descriptor eviction drain");
            rendering::detail::PlacedResourceBatch republished;
            if (!descriptorPool.Acquire(canonicalPlan, descriptorObjects, 102, republished, &failure) || republished.assignments[0].buffer != firstAlias || republished.assignments[1].buffer != secondAlias ||
                descriptorLedger.GetStats().publicationMisses != 3 || descriptorLedger.GetStats().liveSlots != 4)
                return fail("RF3 placed descriptor-evicted native reuse and republish");
            descriptorPool.Rollback(republished);
            if (!descriptorPool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("RF3 placed canonical cleanup");
            descriptorPool.Poll();
            if (descriptorLedger.GetStats().liveSlots != 0 || descriptorLedger.GetStats().reusableSlots != 0 || nativeLedger.GetStats().chargedBytes != 0 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("RF3 placed canonical final accounting");
        }
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

    // All 64 repeated uses exercise a heap-backed cold name (>20 bytes) and
    // must become allocation-free borrowed names on the warm contract path.
    constexpr rendering::LogicalResourceName RFEX1DuplicateName = VG_RENDER_FLOW_NAME("RFEX1.Duplicate.Warm.LongName");
    constexpr rendering::LogicalResourceName RFEX1CollisionNameA = VG_RENDER_FLOW_NAME("rfex1.2l2jjq6yf6");
    constexpr rendering::LogicalResourceName RFEX1CollisionNameB = VG_RENDER_FLOW_NAME("rfex1.j971n4e0yg");
    // Deliberately exceeds containers::String's 20-byte inline capacity.
    // Cold declaration owns the bytes; a warm contract must use the exact
    // immutable cached name without constructing a heap-backed copy per frame.
    constexpr rendering::LogicalResourceName RFEX1LongName = VG_RENDER_FLOW_NAME("RFEX1.Immutable.Warm.Contract.LongName");
    constexpr vanguard::u32 RFEX1DuplicateUseCount = 64;
    static_assert(RFEX1CollisionNameA.GetHash() == RFEX1CollisionNameB.GetHash());

    struct RFEX1BindingState
    {
        bool valid = true;
        bool decisionValue = true;
        bool observedDecision = false;
        const rendering::ResolvedBufferUse* escaped = nullptr;
        rendering::ResourceUseId firstDuplicate;
        rendering::ResourceUseId secondDuplicate;
        vanguard::u32 declarationCalls = 0;
        vanguard::u32 patchCalls = 0;
    };

    class RFEX1BindingNode final : public rendering::RenderNodeImpl
    {
    public:
        enum class Stage : vanguard::u8
        {
            Initialize,
            Lookup,
            Stale
        };

        RFEX1BindingNode(const Stage stage, RFEX1BindingState* const state) noexcept : m_stage(stage), m_state(state) {}

        bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            ++m_state->declarationCalls;
            rendering::BufferUseDesc use;
            if (m_stage == Stage::Initialize)
            {
                rendering::FrameBufferDesc desc;
                desc.active.size = 256;
                desc.active.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::CopySource;
                desc.active.initialState = gpu::ResourceState::Common;
                desc.maximumSize = desc.active.size;
                use.requiredState = gpu::ResourceState::CopyDestination;
                use.access = rendering::LogicalAccessIntent::Write;
                use.content = rendering::ResourceContentIntent::Discard;
                const rendering::RenderFlowNameTag duplicate = context.RTSharedAlloc(RFEX1DuplicateName, desc);
                const rendering::RenderFlowNameTag collisionA = context.RTSharedAlloc(RFEX1CollisionNameA, desc);
                const rendering::RenderFlowNameTag collisionB = context.RTSharedAlloc(RFEX1CollisionNameB, desc);
                const rendering::RenderFlowNameTag longName = context.RTSharedAlloc(RFEX1LongName, desc);
                context.RTUseBegin(duplicate, use);
                context.RTUseEnd(duplicate);
                context.RTUseBegin(collisionA, use);
                context.RTUseEnd(collisionA);
                context.RTUseBegin(collisionB, use);
                context.RTUseEnd(collisionB);
                context.RTUseBegin(longName, use);
                context.RTUseEnd(longName);
                return true;
            }

            use.requiredState = gpu::ResourceState::CopySource;
            use.access = rendering::LogicalAccessIntent::Read;
            use.content = rendering::ResourceContentIntent::Preserve;
            const rendering::RenderFlowNameTag duplicate = rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1DuplicateName);
            if (m_stage == Stage::Lookup)
            {
                for (vanguard::u32 index = 0; index < RFEX1DuplicateUseCount; ++index)
                {
                    context.RTUseBegin(duplicate, use);
                    context.RTUseEnd(duplicate);
                }
                const rendering::RenderFlowNameTag collisionA = rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1CollisionNameA);
                const rendering::RenderFlowNameTag collisionB = rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1CollisionNameB);
                context.RTUseBegin(collisionA, use);
                context.RTUseEnd(collisionA);
                context.RTUseBegin(collisionB, use);
                context.RTUseEnd(collisionB);
                const rendering::RenderFlowNameTag longName = rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1LongName);
                context.RTUseBegin(longName, use);
                context.RTUseEnd(longName);
                static_cast<void>(context.RTDecision(m_state->decisionValue));

                rendering::FrameBufferDesc temporaryDesc;
                temporaryDesc.active.size = 256;
                temporaryDesc.active.usage = gpu::BufferUsage::CopyDestination;
                temporaryDesc.active.initialState = gpu::ResourceState::Common;
                temporaryDesc.maximumSize = temporaryDesc.active.size;
                const rendering::RenderFlowNameTag temporary = context.RTTempAlloc("RFEX1.Temp", temporaryDesc);
                rendering::BufferUseDesc temporaryUse;
                temporaryUse.requiredState = gpu::ResourceState::CopyDestination;
                temporaryUse.access = rendering::LogicalAccessIntent::Write;
                temporaryUse.content = rendering::ResourceContentIntent::Discard;
                context.RTUseBegin(temporary, temporaryUse);
                context.RTUseEnd(temporary);
            }
            else
            {
                context.RTUseBegin(duplicate, use);
                context.RTUseEnd(duplicate);
            }
            return true;
        }

        bool PatchResourceContract(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            ++m_state->patchCalls;
            if (m_stage == Stage::Lookup)
                static_cast<void>(context.RTDecision(m_state->decisionValue));
            return true;
        }

        void Execute(const rendering::RenderNodeImplContext& context, jobs::Builder*) const override
        {
            if (m_stage == Stage::Initialize)
                return;
            if (m_stage == Stage::Lookup)
            {
                m_state->observedDecision = context.RTDecision(!m_state->decisionValue);
                m_state->valid = m_state->valid && m_state->observedDecision == m_state->decisionValue;
                const rendering::RenderFlowNameTag duplicate = rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1DuplicateName);
                const rendering::ResolvedBufferUse* first = nullptr;
                rendering::ResourceUseId previous;
                for (vanguard::u32 index = 0; index < RFEX1DuplicateUseCount; ++index)
                {
                    const rendering::ResolvedBufferUse& resolved = context.RTBuffer(duplicate);
                    m_state->valid = m_state->valid && resolved.IsValid() && resolved.GetBuffer().IsValid();
                    if (index == 0)
                    {
                        first = &resolved;
                        m_state->firstDuplicate = resolved.GetUse();
                    }
                    else
                    {
                        m_state->valid = m_state->valid && resolved.GetUse().ordinal == previous.ordinal + 1u;
                        if (index == 1)
                            m_state->secondDuplicate = resolved.GetUse();
                    }
                    previous = resolved.GetUse();
                }
                const rendering::ResolvedBufferUse& fallback = context.RTBuffer(duplicate);
                const rendering::ResolvedBufferUse& collisionA =
                    context.RTBuffer(rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1CollisionNameA));
                const rendering::ResolvedBufferUse& collisionB =
                    context.RTBuffer(rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1CollisionNameB));
                const rendering::ResolvedBufferUse& longName =
                    context.RTBuffer(rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1LongName));
                const rendering::ResolvedBufferUse& temporary = context.RTBuffer(context.RTNameTag(VG_RENDER_FLOW_NAME("RFEX1.Temp")));
                m_state->valid = m_state->valid && first != nullptr && fallback.IsValid() && collisionA.IsValid() && collisionB.IsValid() &&
                                 longName.IsValid() && temporary.IsValid() &&
                                 fallback.GetBuffer().IsValid() && collisionA.GetBuffer().IsValid() && collisionB.GetBuffer().IsValid() &&
                                 longName.GetBuffer().IsValid() && temporary.GetBuffer().IsValid() &&
                                 fallback.GetUse() == m_state->firstDuplicate && collisionA.GetUse() != collisionB.GetUse() &&
                                 longName.GetUse() != collisionA.GetUse() && longName.GetUse() != collisionB.GetUse();
                m_state->escaped = first;
                return;
            }

            const rendering::ResolvedBufferUse& current =
                context.RTBuffer(rendering::RenderNodeImplContext::RTSharedNameTag(RFEX1DuplicateName));
            m_state->valid = m_state->valid && m_state->escaped != nullptr && !m_state->escaped->IsValid() && !m_state->escaped->GetBuffer().IsValid() &&
                             current.IsValid() && current.GetBuffer().IsValid() &&
                             current.GetUse() != m_state->firstDuplicate && current.GetUse() != m_state->secondDuplicate;
        }

        rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override
        {
            return rendering::RenderNodeCommandListUsage::None;
        }

    private:
        Stage m_stage;
        RFEX1BindingState* m_state = nullptr;
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

    [[nodiscard]] bool RunRFEX1DirectBindingTest() noexcept
    {
        RFEX1BindingState state;
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            factory.BeginCommandListGroup(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), "RFEX1Bindings");
            static_cast<void>(factory.Create<RFEX1BindingNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0),
                                                              RFEX1BindingNode::Stage::Initialize, &state));
            static_cast<void>(factory.Create<RFEX1BindingNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0),
                                                              RFEX1BindingNode::Stage::Lookup, &state));
            static_cast<void>(factory.Create<RFEX1BindingNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0),
                                                              RFEX1BindingNode::Stage::Stale, &state));
            factory.EndCommandListGroup();
        }
        graph.BuildRenderFlowGroups();

        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        if (!allocator.Initialize({}, &failure))
            return false;
        rendering::RenderFlowResourceContract resourceContract;

        const auto runFrame = [&](const vanguard::u64 frameSerial) noexcept
        {
            state.valid = true;
            state.observedDecision = !state.decisionValue;
            state.escaped = nullptr;
            state.firstDuplicate = {};
            state.secondDuplicate = {};
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
            if (!allocator.BeginFrame(frameSerial, {}, &failure) || !graph.PrepareResourceBindings(bindings, preparationFailures))
                return false;
            if (!resourceContract.IsValid())
            {
                if (!allocator.BeginResourceContractCompilation(resourceContract, &failure))
                    return false;
                jobs::Builder declarations({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
                graph.PrepareResourcesParallel(context, allocator, bindings, preparationFailures, declarations);
                if (!declarations.WaitForCompletion() || preparationFailures.HasFailure() ||
                    !graph.CaptureResourceContract(resourceContract, bindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                    return false;
            }
            else if (!graph.InstantiateResourceContract(resourceContract, bindings, allocator, &failure) ||
                     !graph.PatchResourceContract(context, allocator, bindings, &failure) ||
                     !allocator.BindResourceContract(resourceContract, &failure))
                return false;
            if (!allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !graph.PrepareExecutionPackets(allocator, lists, bindings, &failure))
                return false;

            jobs::Builder kickoffBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
            jobs::Counter kickoff = kickoffBuilder.ExtractCounter();
            if (!kickoff.IsValid())
                return false;
            bool completed = false;
            bool scheduled = false;
            jobs::Builder setup({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
            static jobs::JobName name{"RFEX1Bindings/Setup"};
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
            const rendering::CommandScopeExecutionReceipt scope{packet.GetCommandScope(), rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission,
                                                                 gpu::QueueType::Graphics, {}};
            rendering::TerminalExecutionReceipt receipt;
            receipt.generation = allocator.GetExecutionGeneration();
            receipt.join = rendering::TerminalJoinToken::CompletedSynchronously(receipt.generation);
            receipt.completion = rendering::TerminalExecutionCompletionKind::Aborted;
            receipt.commandScopes = {&scope, 1};
            return allocator.Finish(receipt, &failure) && scheduled && completed && state.valid && state.escaped != nullptr && !state.escaped->IsValid();
        };

        if (!runFrame(2))
            return false;
        const rendering::ResourceUseId coldFirstDuplicate = state.firstDuplicate;
        const vanguard::u64 publicationSerial = resourceContract.GetPublicationSerial();
        const rendering::RenderFlowResourceAllocatorStats cold = allocator.GetStats();
        if (!resourceContract.IsValid() || publicationSerial == 0 || state.declarationCalls != 3 || state.patchCalls != 0 || !state.observedDecision ||
            cold.structuralPlanBuilds != 1 || cold.structuralPlanMisses != 1 || cold.structuralPlanHits != 0 || cold.compiledBarriers == 0)
            return false;

        // Missing the decision-only patch must reject warm admission without
        // replacing or invalidating the last proven contract.
        {
            rendering::RenderNodeResourceBindings bindings;
            rendering::RenderNodeResourcePreparationFailures preparationFailures;
            if (!allocator.BeginFrame(3, {}, &failure) || !graph.PrepareResourceBindings(bindings, preparationFailures) ||
                !graph.InstantiateResourceContract(resourceContract, bindings, allocator, &failure) ||
                allocator.BindResourceContract(resourceContract, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DescriptorConflict ||
                !resourceContract.IsValid() || resourceContract.GetPublicationSerial() != publicationSerial)
                return false;
            allocator.CancelBeforePublication();
        }

        state.decisionValue = false;
        if (!runFrame(4))
            return false;
        const rendering::RenderFlowResourceAllocatorStats warm = allocator.GetStats();
        if (state.declarationCalls != 3 || state.patchCalls != 3 || state.observedDecision || !state.firstDuplicate.IsValid() ||
            state.firstDuplicate.generation == coldFirstDuplicate.generation || warm.planningWriters != 0 ||
            warm.structuralPlanBuilds != 1 || warm.structuralPlanMisses != 1 || warm.structuralPlanHits != 1 ||
            warm.persistentStateProgramHits != 0 || warm.persistentStateProgramMisses != 1 || warm.persistentStateProgramBuilds != 2 ||
            resourceContract.GetPublicationSerial() != publicationSerial)
            return false;
        // A changed decision must compile exactly one new state variant, not
        // trigger a cold barrier compiler on every subsequent stable frame.
        if (!runFrame(5))
            return false;
        const rendering::RenderFlowResourceAllocatorStats stable = allocator.GetStats();
        return state.declarationCalls == 3 && state.patchCalls == 6 && !state.observedDecision &&
               stable.structuralPlanBuilds == 1 && stable.structuralPlanHits == 2 &&
               stable.persistentStateProgramBuilds == 2 && stable.persistentStateProgramMisses == 1 &&
               stable.persistentStateProgramHits == 1 &&
               resourceContract.GetPublicationSerial() == publicationSerial && allocator.Shutdown(&failure);
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

        // Automatic-state objects close back to descriptor.initialState. The
        // current placed alias contract finalizes predecessors to Common, so a
        // non-Common creation state must fall back to dedicated ownership rather
        // than being admitted and failing later during RF8 alias lowering.
        {
            rendering::FrameBufferDesc nonCommonDesc;
            nonCommonDesc.active.size = 4096;
            nonCommonDesc.active.usage = gpu::BufferUsage::UnorderedAccess;
            nonCommonDesc.active.initialState = gpu::ResourceState::UnorderedAccess;
            nonCommonDesc.active.keepInitialState = true;
            nonCommonDesc.maximumSize = nonCommonDesc.active.size;
            rendering::BufferUseDesc nonCommonWrite;
            nonCommonWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            nonCommonWrite.access = rendering::LogicalAccessIntent::Write;
            nonCommonWrite.content = rendering::ResourceContentIntent::Discard;
            rendering::RenderFlowResourceAllocator fallbackAllocator;
            rendering::ResourcePlanningWriter fallbackWriters[2];
            rendering::LogicalResourceId fallbackResources[2];
            rendering::ResourceUseId fallbackUses[2];
            constexpr rendering::RenderFlowNodeId fallbackNodes[] = {{76}, {77}};
            constexpr rendering::GpuFlowGroupId fallbackFlows[] = {{76}, {77}};
            constexpr rendering::CommandScopeId fallbackScopes[] = {{76}, {77}};
            constexpr rendering::FlowSpaceId fallbackSpace{76};
            if (!fallbackAllocator.Initialize({}, &failure) || !fallbackAllocator.BeginFrame(76, {true, false}, &failure) ||
                !fallbackAllocator.CreatePlanningWriter(fallbackNodes[0], fallbackFlows[0], fallbackScopes[0], fallbackWriters[0], &failure) ||
                !fallbackAllocator.CreatePlanningWriter(fallbackNodes[1], fallbackFlows[1], fallbackScopes[1], fallbackWriters[1], &failure) ||
                !fallbackWriters[0].DeclareBuffer({fallbackSpace, "placed.noncommon.first"}, nonCommonDesc, fallbackResources[0], &failure) ||
                !fallbackWriters[0].BeginBufferUse(fallbackResources[0], nonCommonWrite, fallbackUses[0], &failure) ||
                !fallbackWriters[0].EndUse(fallbackUses[0], &failure) || !fallbackWriters[0].Close(&failure) ||
                !fallbackWriters[1].DeclareBuffer({fallbackSpace, "placed.noncommon.second"}, nonCommonDesc, fallbackResources[1], &failure) ||
                !fallbackWriters[1].BeginBufferUse(fallbackResources[1], nonCommonWrite, fallbackUses[1], &failure) ||
                !fallbackWriters[1].EndUse(fallbackUses[1], &failure) || !fallbackWriters[1].Close(&failure) ||
                !fallbackAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !fallbackAllocator.Resolve(nullptr, &failure))
                return fail("non-Common automatic-state placed fallback resolve");
            const rendering::RenderFlowResourceAllocatorStats fallbackStats = fallbackAllocator.GetStats();
            if (fallbackStats.placedHeapPoolMisses != 0 || fallbackStats.placedObjectPoolMisses != 0 || fallbackStats.dedicatedResourcePoolMisses == 0 ||
                !fallbackAllocator.BeginExecution(&failure))
                return fail("non-Common automatic-state placed fallback selection");
            const rendering::ExecutionGenerationId fallbackGeneration = fallbackAllocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt fallbackReceipts[] = {
                {fallbackScopes[0], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}},
                {fallbackScopes[1], rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}}};
            const rendering::TerminalExecutionReceipt fallbackTerminal{
                fallbackGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(fallbackReceipts),
                rendering::TerminalJoinToken::CompletedSynchronously(fallbackGeneration)};
            if (!fallbackAllocator.Finish(fallbackTerminal, &failure) || !fallbackAllocator.Shutdown(&failure))
                return fail("non-Common automatic-state placed fallback cleanup");
        }

        {
            rendering::RenderFlowResourceAllocatorConfig capacityConfig;
            // RF11 sparse lowering reduced this frame to four native actions
            // (the stats counter also includes the earlier one-action seed).
            // Keep the per-frame cap one below that exact current program so
            // the placed-allocation rollback path is still exercised.
            capacityConfig.maximumCompiledResourceActions = 3;
            rendering::RenderFlowResourceAllocator capacityAllocator;
            rendering::ResourcePlanningWriter seedWriter;
            rendering::LogicalResourceId seedResource;
            rendering::ResourceUseId seedUse;
            constexpr rendering::RenderFlowNodeId seedNode{77};
            constexpr rendering::GpuFlowGroupId seedFlow{77};
            constexpr rendering::CommandScopeId seedScope{77};
            constexpr rendering::FlowSpaceId seedSpace{77};
            if (!capacityAllocator.Initialize(capacityConfig, &failure) || !capacityAllocator.BeginFrame(77, {false, false}, &failure) ||
                !capacityAllocator.CreatePlanningWriter(seedNode, seedFlow, seedScope, seedWriter, &failure) ||
                !seedWriter.DeclareBuffer({seedSpace, "placed.capacity.rf6-seed"}, firstDesc, seedResource, &failure) ||
                !seedWriter.BeginBufferUse(seedResource, write, seedUse, &failure) || !seedWriter.EndUse(seedUse, &failure) || !seedWriter.Close(&failure) ||
                !capacityAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !capacityAllocator.Resolve(nullptr, &failure) ||
                !capacityAllocator.BeginExecution(&failure))
                return fail("placed action-cap RF6 seed");
            const rendering::ExecutionGenerationId seedGeneration = capacityAllocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt seedReceipt{seedScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
            const rendering::TerminalExecutionReceipt seedTerminal{seedGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                   vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&seedReceipt, 1),
                                                                   rendering::TerminalJoinToken::CompletedSynchronously(seedGeneration)};
            if (!capacityAllocator.Finish(seedTerminal, &failure) || capacityAllocator.GetStats().persistentDedicatedPlanBuilds != 1)
                return fail("placed action-cap RF6 seed finish");

            rendering::ResourcePlanningWriter capacityWriter;
            rendering::LogicalResourceId capacityFirst;
            rendering::LogicalResourceId capacitySecond;
            rendering::ResourceUseId capacityFirstWrite;
            rendering::ResourceUseId capacitySecondWrite;
            rendering::ResourceUseId capacitySecondRead;
            constexpr rendering::RenderFlowNodeId capacityNode{78};
            constexpr rendering::GpuFlowGroupId capacityFlow{78};
            constexpr rendering::CommandScopeId capacityScope{78};
            constexpr rendering::FlowSpaceId capacitySpace{78};
            if (!capacityAllocator.BeginFrame(78, {true}, &failure) ||
                !capacityAllocator.CreatePlanningWriter(capacityNode, capacityFlow, capacityScope, capacityWriter, &failure) ||
                !capacityWriter.DeclareBuffer({capacitySpace, "placed.capacity.first"}, firstDesc, capacityFirst, &failure) ||
                !capacityWriter.DeclareBuffer({capacitySpace, "placed.capacity.second"}, secondDesc, capacitySecond, &failure) ||
                !capacityWriter.OpenResourceScope(capacityFirst, &failure) || !capacityWriter.BeginBufferUse(capacityFirst, write, capacityFirstWrite, &failure) ||
                !capacityWriter.EndUse(capacityFirstWrite, &failure) || !capacityWriter.CloseResourceScope(capacityFirst, &failure) ||
                !capacityWriter.OpenResourceScope(capacitySecond, &failure) || !capacityWriter.BeginBufferUse(capacitySecond, write, capacitySecondWrite, &failure) ||
                !capacityWriter.EndUse(capacitySecondWrite, &failure) || !capacityWriter.BeginBufferUse(capacitySecond, read, capacitySecondRead, &failure) ||
                !capacityWriter.EndUse(capacitySecondRead, &failure) || !capacityWriter.CloseResourceScope(capacitySecond, &failure) || !capacityWriter.Close(&failure) ||
                !capacityAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                return fail("placed action-cap planning");
            if (capacityAllocator.Resolve(nullptr, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::CapacityExceeded || capacityAllocator.GetExecutionGeneration().IsValid() ||
                capacityAllocator.GetStats().pendingNativeDestructionResources == 0)
                return fail("placed action-cap enforcement");
            if (!capacityAllocator.Shutdown(&failure))
                return fail("placed action-cap shutdown");
        }

        // RF3 global pressure is allocator-wide rather than pool-local. Force a
        // two-slot cap, publish one dedicated canonical pair, then require a
        // placed pair and finally the original dedicated pair again. Each switch
        // must evict only the colder descriptors while retaining/reusing the
        // underlying native object/heap.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 8, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 global-pressure descriptor domain");
            rendering::RenderFlowResourceAllocatorConfig pressureConfig;
            pressureConfig.resourceDescriptors = descriptors.GetRef();
            pressureConfig.descriptorCacheTarget = 0;
            pressureConfig.descriptorCacheMaximum = 2;
            rendering::RenderFlowResourceAllocator pressureAllocator;
            if (!pressureAllocator.Initialize(pressureConfig, &failure))
                return fail("RF3 global-pressure allocator init");

            rendering::FrameBufferDesc descriptorDesc;
            descriptorDesc.active.size = 4096;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            descriptorDesc.active.initialState = gpu::ResourceState::Common;
            descriptorDesc.maximumSize = descriptorDesc.active.size;
            rendering::BufferUseDesc descriptorWrite;
            descriptorWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            descriptorWrite.access = rendering::LogicalAccessIntent::Write;
            descriptorWrite.content = rendering::ResourceContentIntent::Discard;

            const auto runPressureFrame = [&](const vanguard::u64 serial, const bool placed, const char* const name,
                                              rendering::RenderFlowResourceAllocatorStats& stats) noexcept
            {
                const rendering::RenderFlowNodeId pressureNode{static_cast<vanguard::u32>(180 + serial)};
                const rendering::GpuFlowGroupId pressureFlow{static_cast<vanguard::u32>(180 + serial)};
                const rendering::CommandScopeId pressureScope{static_cast<vanguard::u32>(180 + serial)};
                const rendering::FlowSpaceId pressureSpace{180};
                rendering::ResourcePlanningWriter pressureWriter;
                rendering::LogicalResourceId resource;
                rendering::ResourceUseId use;
                if (!pressureAllocator.BeginFrame(serial, {placed, false}, &failure) ||
                    !pressureAllocator.CreatePlanningWriter(pressureNode, pressureFlow, pressureScope, pressureWriter, &failure) ||
                    !pressureWriter.DeclareBuffer({pressureSpace, name}, descriptorDesc, resource, &failure) ||
                    !pressureWriter.BeginBufferUse(resource, descriptorWrite, use, &failure) || !pressureWriter.EndUse(use, &failure) || !pressureWriter.Close(&failure) ||
                    !pressureAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !pressureAllocator.Resolve(nullptr, &failure))
                    return false;
                stats = pressureAllocator.GetStats();
                const rendering::ExecutionGenerationId pressureGeneration = pressureAllocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt pressureReceipt{pressureScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
                const rendering::TerminalExecutionReceipt pressureTerminal{pressureGeneration, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                          vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&pressureReceipt, 1),
                                                                          rendering::TerminalJoinToken::CompletedSynchronously(pressureGeneration)};
                return pressureAllocator.Finish(pressureTerminal, &failure);
            };

            rendering::RenderFlowResourceAllocatorStats dedicatedCold;
            rendering::RenderFlowResourceAllocatorStats placedPressure;
            rendering::RenderFlowResourceAllocatorStats dedicatedRepublish;
            if (!runPressureFrame(1, false, "rf3.pressure.dedicated", dedicatedCold) || dedicatedCold.dedicatedResourcePoolMisses != 1 ||
                dedicatedCold.canonicalDescriptorLiveSlots != 2 || dedicatedCold.canonicalDescriptorPressureEvictions != 0)
                return fail("RF3 global-pressure dedicated cold setup");
            if (!runPressureFrame(2, true, "rf3.pressure.placed", placedPressure) || placedPressure.placedObjectPoolMisses != 1 ||
                placedPressure.canonicalDescriptorLiveSlots != 2 || placedPressure.canonicalDescriptorPressureEvictions != 1)
                return fail("RF3 global-pressure dedicated-to-placed eviction");
            if (!runPressureFrame(3, false, "rf3.pressure.dedicated", dedicatedRepublish) || dedicatedRepublish.dedicatedResourcePoolHits != 1 ||
                dedicatedRepublish.canonicalDescriptorLiveSlots != 2 || dedicatedRepublish.canonicalDescriptorPressureEvictions != 2 ||
                dedicatedRepublish.canonicalDescriptorPublicationMisses != 3)
                return fail("RF3 global-pressure placed-to-dedicated republish");
            if (!pressureAllocator.ClearPersistentCaches(&failure) || !gpu::FlushRetiredResources(&rhiFailure) || !pressureAllocator.Shutdown(&failure))
                return fail("RF3 global-pressure cleanup");
        }

        // BeginFrame descriptor maintenance uses the same global cold order.
        // The older dedicated bundle must be trimmed before the newer placed
        // bundle, while the native dedicated object remains reusable.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 8, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 global-maintenance descriptor domain");
            rendering::RenderFlowResourceAllocatorConfig maintenanceConfig;
            maintenanceConfig.resourceDescriptors = descriptors.GetRef();
            maintenanceConfig.descriptorCacheTarget = 2;
            maintenanceConfig.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator maintenanceAllocator;
            if (!maintenanceAllocator.Initialize(maintenanceConfig, &failure))
                return fail("RF3 global-maintenance allocator init");

            rendering::FrameBufferDesc descriptorDesc;
            descriptorDesc.active.size = 4096;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            descriptorDesc.active.initialState = gpu::ResourceState::Common;
            descriptorDesc.maximumSize = descriptorDesc.active.size;
            rendering::BufferUseDesc descriptorWrite;
            descriptorWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            descriptorWrite.access = rendering::LogicalAccessIntent::Write;
            descriptorWrite.content = rendering::ResourceContentIntent::Discard;

            const auto runMaintenanceFrame = [&](const vanguard::u64 serial, const bool placed, const bool processEviction, const char* const name,
                                                 rendering::RenderFlowResourceAllocatorStats& stats) noexcept
            {
                const rendering::RenderFlowNodeId node{static_cast<vanguard::u32>(200 + serial)};
                const rendering::GpuFlowGroupId flow{static_cast<vanguard::u32>(200 + serial)};
                const rendering::CommandScopeId scope{static_cast<vanguard::u32>(200 + serial)};
                constexpr rendering::FlowSpaceId space{200};
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId resource;
                rendering::ResourceUseId use;
                if (!maintenanceAllocator.BeginFrame(serial, {placed, processEviction}, &failure) ||
                    !maintenanceAllocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.DeclareBuffer({space, name}, descriptorDesc, resource, &failure) || !writer.BeginBufferUse(resource, descriptorWrite, use, &failure) ||
                    !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
                    !maintenanceAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !maintenanceAllocator.Resolve(nullptr, &failure))
                    return false;
                stats = maintenanceAllocator.GetStats();
                const rendering::ExecutionGenerationId generation = maintenanceAllocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt receipt{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                  vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                                                                  rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                return maintenanceAllocator.Finish(terminal, &failure);
            };

            rendering::RenderFlowResourceAllocatorStats dedicatedCold;
            rendering::RenderFlowResourceAllocatorStats placedCold;
            rendering::RenderFlowResourceAllocatorStats afterMaintenance;
            if (!runMaintenanceFrame(1, false, false, "rf3.maintenance.dedicated", dedicatedCold) || dedicatedCold.canonicalDescriptorLiveSlots != 2 ||
                !runMaintenanceFrame(2, true, false, "rf3.maintenance.placed", placedCold) || placedCold.canonicalDescriptorLiveSlots != 4)
                return fail("RF3 global-maintenance warm setup");
            if (!runMaintenanceFrame(3, false, true, "rf3.maintenance.dedicated", afterMaintenance) || afterMaintenance.dedicatedResourcePoolHits != 1 ||
                afterMaintenance.canonicalDescriptorMaintenanceEvictions != 1 || afterMaintenance.canonicalDescriptorPressureEvictions != 0 ||
                afterMaintenance.canonicalDescriptorPublicationMisses != 3 || afterMaintenance.canonicalDescriptorLiveSlots != 4)
                return fail("RF3 global-maintenance oldest-owner selection");
            if (!maintenanceAllocator.Shutdown(&failure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("RF3 global-maintenance healthy shutdown");
        }

        // Shared RHI-domain exhaustion is distinct from the allocator hard cap.
        // Keep one externally-owned slot live so the placed publication consumes
        // one failed RHI attempt, globally evicts the colder dedicated pair, and
        // then succeeds on the single bounded retry.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 shared-domain descriptor domain");
            const gpu::DescriptorHandle external = gpu::AllocateDescriptor(descriptors.GetRef(), &rhiFailure);
            if (!external.IsValid())
                return fail("RF3 shared-domain external slot");
            rendering::RenderFlowResourceAllocatorConfig sharedConfig;
            sharedConfig.resourceDescriptors = descriptors.GetRef();
            sharedConfig.descriptorCacheTarget = 4;
            sharedConfig.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator sharedAllocator;
            if (!sharedAllocator.Initialize(sharedConfig, &failure))
                return fail("RF3 shared-domain allocator init");

            rendering::FrameBufferDesc descriptorDesc;
            descriptorDesc.active.size = 4096;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            descriptorDesc.active.initialState = gpu::ResourceState::Common;
            descriptorDesc.maximumSize = descriptorDesc.active.size;
            rendering::BufferUseDesc descriptorWrite;
            descriptorWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            descriptorWrite.access = rendering::LogicalAccessIntent::Write;
            descriptorWrite.content = rendering::ResourceContentIntent::Discard;

            const auto runSharedFrame = [&](const vanguard::u64 serial, const bool placed, const char* const name,
                                            rendering::RenderFlowResourceAllocatorStats& stats) noexcept
            {
                const rendering::RenderFlowNodeId node{static_cast<vanguard::u32>(220 + serial)};
                const rendering::GpuFlowGroupId flow{static_cast<vanguard::u32>(220 + serial)};
                const rendering::CommandScopeId scope{static_cast<vanguard::u32>(220 + serial)};
                constexpr rendering::FlowSpaceId space{220};
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId resource;
                rendering::ResourceUseId use;
                if (!sharedAllocator.BeginFrame(serial, {placed, false}, &failure) || !sharedAllocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.DeclareBuffer({space, name}, descriptorDesc, resource, &failure) || !writer.BeginBufferUse(resource, descriptorWrite, use, &failure) ||
                    !writer.EndUse(use, &failure) || !writer.Close(&failure) || !sharedAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
                    !sharedAllocator.Resolve(nullptr, &failure))
                    return false;
                stats = sharedAllocator.GetStats();
                const rendering::ExecutionGenerationId generation = sharedAllocator.GetExecutionGeneration();
                const rendering::CommandScopeExecutionReceipt receipt{scope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
                const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                                  vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                                                                  rendering::TerminalJoinToken::CompletedSynchronously(generation)};
                return sharedAllocator.Finish(terminal, &failure);
            };

            rendering::RenderFlowResourceAllocatorStats dedicatedCold;
            rendering::RenderFlowResourceAllocatorStats placedRetry;
            if (!runSharedFrame(1, false, "rf3.shared.dedicated", dedicatedCold) || dedicatedCold.canonicalDescriptorLiveSlots != 2 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 3)
                return fail("RF3 shared-domain dedicated setup");
            if (!runSharedFrame(2, true, "rf3.shared.placed", placedRetry) || placedRetry.placedObjectPoolMisses != 1 ||
                placedRetry.canonicalDescriptorPressureEvictions != 1 || placedRetry.canonicalDescriptorPublicationMisses != 2 ||
                placedRetry.canonicalDescriptorLiveSlots != 2 || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 3)
                return fail("RF3 shared-domain bounded global retry");
            if (!sharedAllocator.Shutdown(&failure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 1 ||
                !gpu::RetireDescriptor(descriptors.GetRef(), external, {}, &rhiFailure) || !gpu::FlushRetiredResources(&rhiFailure) ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("RF3 shared-domain cleanup");
        }

        // Healthy published-generation abandonment is terminal for the allocator
        // session, but it must retire the placed batch through the normal placed
        // lifetime path rather than treating it as device loss. Persistent native
        // and canonical ownership therefore remain accounted until Shutdown.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 placed abandon descriptor domain");
            rendering::RenderFlowResourceAllocatorConfig abandonConfig;
            abandonConfig.resourceDescriptors = descriptors.GetRef();
            abandonConfig.descriptorCacheTarget = 4;
            abandonConfig.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator abandonAllocator;
            if (!abandonAllocator.Initialize(abandonConfig, &failure))
                return fail("RF3 placed abandon allocator init");

            rendering::FrameBufferDesc descriptorDesc;
            descriptorDesc.active.size = 4096;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            descriptorDesc.active.initialState = gpu::ResourceState::Common;
            descriptorDesc.maximumSize = descriptorDesc.active.size;
            rendering::BufferUseDesc descriptorWrite;
            descriptorWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            descriptorWrite.access = rendering::LogicalAccessIntent::Write;
            descriptorWrite.content = rendering::ResourceContentIntent::Discard;
            constexpr rendering::RenderFlowNodeId abandonNode{240};
            constexpr rendering::GpuFlowGroupId abandonFlow{240};
            constexpr rendering::CommandScopeId abandonScope{240};
            constexpr rendering::FlowSpaceId abandonSpace{240};
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            if (!abandonAllocator.BeginFrame(240, {true, false}, &failure) || !abandonAllocator.CreatePlanningWriter(abandonNode, abandonFlow, abandonScope, writer, &failure) ||
                !writer.DeclareBuffer({abandonSpace, "rf3.abandon.placed"}, descriptorDesc, resource, &failure) ||
                !writer.BeginBufferUse(resource, descriptorWrite, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
                !abandonAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !abandonAllocator.Resolve(nullptr, &failure))
                return fail("RF3 placed abandon setup");
            const rendering::RenderFlowResourceAllocatorStats beforeAbandon = abandonAllocator.GetStats();
            if (beforeAbandon.placedObjectPoolMisses != 1 || beforeAbandon.canonicalDescriptorLiveSlots != 2 || beforeAbandon.chargedNativeBytes == 0)
                return fail("RF3 placed abandon setup accounting");
            rendering::detail::RenderFlowResourceShutdownTestAccess::AbandonPublishedExecution(abandonAllocator);
            const rendering::RenderFlowResourceAllocatorStats abandoned = abandonAllocator.GetStats();
            if (abandonAllocator.GetExecutionGeneration().IsValid() || abandonAllocator.GetState() != rendering::RenderFlowResourceSessionState::DeviceUnavailable ||
                abandoned.abortedFrames != 1 || abandoned.canonicalDescriptorLiveSlots != 2 || abandoned.chargedNativeBytes != beforeAbandon.chargedNativeBytes ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
            {
                std::fprintf(stderr,
                             "[resource-flow-placed-execution] RF3 abandon stats state=%u genValid=%u aborted=%llu live=%u reusable=%u charged=%llu before=%llu domain=%u placed pending=%llu\n",
                             static_cast<unsigned>(abandonAllocator.GetState()), abandonAllocator.GetExecutionGeneration().IsValid() ? 1u : 0u,
                             static_cast<unsigned long long>(abandoned.abortedFrames), abandoned.canonicalDescriptorLiveSlots, abandoned.canonicalDescriptorReusableSlots,
                             static_cast<unsigned long long>(abandoned.chargedNativeBytes), static_cast<unsigned long long>(beforeAbandon.chargedNativeBytes),
                             gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated,
                             static_cast<unsigned long long>(abandoned.pendingRetirementPlacedHeaps));
                return fail("RF3 placed healthy abandon ownership");
            }
            if (!abandonAllocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("RF3 placed healthy abandon cleanup");
        }

        // RF3 extends the retryable healthy terminal contract to placed owners.
        // After descriptor retirement is admitted, an injected non-device-loss
        // retirement drain failure must leave the placed native heap/object and
        // byte charge intact. A second Shutdown then crosses the commit point and
        // releases them only after a real shared retirement drain succeeds.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("RF3 placed shutdown retry descriptor domain");
            rendering::RenderFlowResourceAllocatorConfig shutdownConfig;
            shutdownConfig.resourceDescriptors = descriptors.GetRef();
            shutdownConfig.descriptorCacheTarget = 4;
            shutdownConfig.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator shutdownAllocator;
            if (!shutdownAllocator.Initialize(shutdownConfig, &failure))
                return fail("RF3 placed shutdown retry allocator init");

            rendering::FrameBufferDesc descriptorDesc;
            descriptorDesc.active.size = 4096;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            descriptorDesc.active.initialState = gpu::ResourceState::Common;
            descriptorDesc.maximumSize = descriptorDesc.active.size;
            rendering::BufferUseDesc descriptorWrite;
            descriptorWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            descriptorWrite.access = rendering::LogicalAccessIntent::Write;
            descriptorWrite.content = rendering::ResourceContentIntent::Discard;
            constexpr rendering::RenderFlowNodeId shutdownNode{241};
            constexpr rendering::GpuFlowGroupId shutdownFlow{241};
            constexpr rendering::CommandScopeId shutdownScope{241};
            constexpr rendering::FlowSpaceId shutdownSpace{241};
            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId resource;
            rendering::ResourceUseId use;
            if (!shutdownAllocator.BeginFrame(241, {true, false}, &failure) ||
                !shutdownAllocator.CreatePlanningWriter(shutdownNode, shutdownFlow, shutdownScope, writer, &failure) ||
                !writer.DeclareBuffer({shutdownSpace, "rf3.shutdown.retry.placed"}, descriptorDesc, resource, &failure) ||
                !writer.BeginBufferUse(resource, descriptorWrite, use, &failure) || !writer.EndUse(use, &failure) || !writer.Close(&failure) ||
                !shutdownAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !shutdownAllocator.Resolve(nullptr, &failure))
                return fail("RF3 placed shutdown retry setup");
            const rendering::ExecutionGenerationId generation = shutdownAllocator.GetExecutionGeneration();
            const rendering::CommandScopeExecutionReceipt receipt{shutdownScope, rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission, gpu::QueueType::Graphics, {}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Aborted,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            if (!shutdownAllocator.Finish(terminal, &failure))
                return fail("RF3 placed shutdown retry finish");
            const rendering::RenderFlowResourceAllocatorStats beforeFailure = shutdownAllocator.GetStats();
            if (beforeFailure.placedObjectPoolMisses != 1 || beforeFailure.canonicalDescriptorLiveSlots != 2 || beforeFailure.chargedNativeBytes == 0 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("RF3 placed shutdown retry setup accounting");
            rendering::detail::RenderFlowResourceShutdownTestAccess::Set(
                shutdownAllocator, {rendering::detail::RenderFlowResourceShutdownFailurePoint::RetirementFlush});
            if (shutdownAllocator.Shutdown(&failure) || !shutdownAllocator.IsInitialized() || shutdownAllocator.GetState() != rendering::RenderFlowResourceSessionState::Idle ||
                failure.code != rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure)
                return fail("RF3 placed shutdown retry injected failure");
            const rendering::RenderFlowResourceAllocatorStats admitted = shutdownAllocator.GetStats();
            if (admitted.canonicalDescriptorLiveSlots != 0 || admitted.chargedNativeBytes != beforeFailure.chargedNativeBytes || admitted.chargedNativeBytes == 0 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("RF3 placed shutdown retry ownership");
            if (!shutdownAllocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("RF3 placed shutdown retry completion");
        }

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
            !writer.OpenResourceScope(firstResource, &failure) || !writer.BeginBufferUse(firstResource, write, firstWrite, &failure) || !writer.EndUse(firstWrite, &failure) ||
            !writer.CloseResourceScope(firstResource, &failure) || !writer.OpenResourceScope(secondResource, &failure) || !writer.BeginBufferUse(secondResource, write, secondWrite, &failure) ||
            !writer.EndUse(secondWrite, &failure) || !writer.BeginBufferUse(secondResource, read, secondRead, &failure) || !writer.EndUse(secondRead, &failure) ||
            !writer.CloseResourceScope(secondResource, &failure) || !writer.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
        const rendering::RenderFlowResourceAllocatorStats resolvedStats = allocator.GetStats();
        if (resolvedStats.placedHeapPoolMisses != 1 || resolvedStats.placedObjectPoolMisses != 2 || resolvedStats.dedicatedResourcePoolMisses != 0 || resolvedStats.compiledResourceActions != 4 ||
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
        rendering::CompiledExecutionRange firstAliasRange;
        rendering::CompiledExecutionRange secondAliasWriteRange;
        rendering::CompiledExecutionRange secondAliasReadRange;
        rendering::ResolvedBufferUse resolvedFirst;
        rendering::ResolvedBufferUse wrongEpochFirst;
        rendering::ResolvedBufferUse resolvedSecondWrite;
        rendering::ResolvedBufferUse resolvedSecondRead;
        if (!readback.IsValid() || !commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.ResolveExecutionRange(0, 1, firstAliasRange, &failure) ||
            !packet.ResolveExecutionRange(1, 1, secondAliasWriteRange, &failure) ||
            !packet.ResolveExecutionRange(2, 1, secondAliasReadRange, &failure) ||
            !packet.ResolveBufferUseBinding(firstWrite, 1, resolvedFirst, &failure) ||
            !packet.ResolveBufferUseBinding(firstWrite, 2, wrongEpochFirst, &failure) ||
            !packet.ResolveBufferUseBinding(secondWrite, 2, resolvedSecondWrite, &failure) ||
            !packet.ResolveBufferUseBinding(secondRead, 3, resolvedSecondRead, &failure) ||
            firstAliasRange.beginActions.count != 1 || firstAliasRange.endActions.count != 1 ||
            secondAliasWriteRange.beginActions.count != 1 ||
            !secondAliasReadRange.beginActions.Empty() ||
            !packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 3, cursor, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution setup");
        }

        if (resolvedFirst.IsValid() || wrongEpochFirst.IsValid() || resolvedSecondWrite.IsValid() || resolvedSecondRead.IsValid() ||
            !cursor.BeginRange(firstAliasRange, 1, &failure) || !resolvedFirst.IsValid() || !resolvedFirst.GetBuffer().IsValid() || wrongEpochFirst.IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("first owner begin");
        }
        const gpu::BufferRef firstNative = resolvedFirst.GetBuffer();
        if (!cursor.EndRange(firstAliasRange, 1, &failure) || resolvedFirst.IsValid() || wrongEpochFirst.IsValid() ||
            !cursor.BeginRange(secondAliasWriteRange, 2, &failure) || wrongEpochFirst.IsValid() || wrongEpochFirst.GetBuffer().IsValid() ||
            !resolvedSecondWrite.IsValid() || !resolvedSecondWrite.GetBuffer().IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("second owner activation");
        }
        const gpu::BufferRef secondNative = resolvedSecondWrite.GetBuffer();
        if (secondNative == firstNative || !cursor.EndRange(secondAliasWriteRange, 2, &failure) || resolvedSecondWrite.IsValid() ||
            !cursor.BeginRange(secondAliasReadRange, 3, &failure) || !resolvedSecondRead.IsValid() || resolvedSecondRead.GetBuffer() != secondNative ||
            !gpu::CopyBuffer(readback.GetRef(), 0, resolvedSecondRead.GetBuffer(), 0, firstDesc.active.size, &rhiFailure) ||
            !cursor.EndRange(secondAliasReadRange, 3, &failure) || resolvedSecondRead.IsValid() || !cursor.FinalizePacket(&failure) ||
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

        // Canonical descriptor references are part of the dedicated pool's
        // exact internal ownership baseline. An unrelated owner must keep the
        // entry out of descriptor-cache maintenance; once released, the same
        // native object may re-enter reuse, shed only its descriptors, and
        // republish them on later demand.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("descriptor baseline domain");
            rendering::FrameBufferDesc descriptorDesc = bufferDesc;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::detail::DedicatedResourceRequest request =
                rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descriptorDesc));
            request.requiresCanonicalDescriptors = true;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 0;
            config.descriptorCacheMaximum = 4;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(request, 60, first, &failure) || !first.shaderResource.IsValid() || !first.unorderedAccess.IsValid() || gpu::GetRefCount(first.buffer) != 3)
                return fail("canonical descriptor exact baseline");
            gpu::Buffer escaped(first.buffer);
            if (gpu::GetRefCount(first.buffer) != 4)
                return fail("canonical descriptor escaped owner setup");
            pool.Rollback(first.entry);
            const rendering::detail::AllocatorDescriptorLedgerStats held = descriptorLedger.GetStats();
            if (held.liveSlots != 2 || held.reusableSlots != 0 || !pool.TrimDescriptorCacheToTarget(&failure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("canonical descriptor escaped owner guard");

            escaped.Reset();
            rendering::detail::DedicatedResourceAssignment warm;
            if (!pool.Acquire(request, 61, warm, &failure) || warm.entry != first.entry || warm.buffer != first.buffer || warm.shaderResource != first.shaderResource ||
                warm.unorderedAccess != first.unorderedAccess || descriptorLedger.GetStats().publicationHits != 1)
                return fail("canonical descriptor held-owner recovery");
            pool.Rollback(warm.entry);
            const vanguard::u64 chargedBeforeDescriptorTrim = pool.GetStats().chargedBytes;
            if (descriptorLedger.GetStats().reusableSlots != 2 || !pool.TrimDescriptorCacheToTarget(&failure))
                return fail("canonical descriptor maintenance trim");
            const rendering::detail::AllocatorDescriptorLedgerStats trimmed = descriptorLedger.GetStats();
            if (trimmed.liveSlots != 0 || trimmed.reusableSlots != 0 || trimmed.maintenanceEvictions != 1 || pool.GetStats().chargedBytes != chargedBeforeDescriptorTrim ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("canonical descriptor-only maintenance accounting");

            rendering::detail::DedicatedResourceAssignment republished;
            if (!pool.Acquire(request, 62, republished, &failure) || republished.entry != first.entry || republished.buffer != first.buffer ||
                !republished.shaderResource.IsValid() || !republished.unorderedAccess.IsValid())
                return fail("canonical descriptor republish after trim");
            const rendering::detail::AllocatorDescriptorLedgerStats republishedStats = descriptorLedger.GetStats();
            if (republishedStats.liveSlots != 2 || republishedStats.publicationMisses != 2 || republishedStats.publicationHits != 1)
                return fail("canonical descriptor republish accounting");
            pool.Rollback(republished.entry);

            // An external owner may appear after an entry has already entered
            // the reusable descriptor heap. Soft maintenance must demote that
            // bundle from reusable-cache accounting without treating the lack
            // of descriptor retirement as a maintenance failure.
            gpu::Buffer lateEscaped(republished.buffer);
            if (descriptorLedger.GetStats().reusableSlots != 2 || !pool.TrimDescriptorCacheToTarget(&failure))
                return fail("canonical descriptor late escaped-owner maintenance");
            const rendering::detail::AllocatorDescriptorLedgerStats demoted = descriptorLedger.GetStats();
            if (demoted.liveSlots != 2 || demoted.reusableSlots != 0 || demoted.maintenanceEvictions != 1 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("canonical descriptor late escaped-owner accounting");
            lateEscaped.Reset();
            rendering::detail::DedicatedResourceAssignment recoveredAfterDemotion;
            if (!pool.Acquire(request, 63, recoveredAfterDemotion, &failure) || recoveredAfterDemotion.buffer != republished.buffer ||
                recoveredAfterDemotion.shaderResource != republished.shaderResource || recoveredAfterDemotion.unorderedAccess != republished.unorderedAccess)
                return fail("canonical descriptor late escaped-owner recovery");
            pool.Rollback(recoveredAfterDemotion.entry);
            if (!pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("canonical descriptor baseline cleanup");
            pool.Poll();
        }

        // The allocator-level descriptor maximum is enforced before touching
        // the shared RHI domain when no reusable canonical bundle can create
        // headroom. The rejected reservation is still visible in telemetry.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("descriptor hard-cap domain");
            rendering::FrameBufferDesc firstDesc = bufferDesc;
            firstDesc.active.structureStride = 16;
            firstDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::FrameBufferDesc secondDesc = firstDesc;
            secondDesc.active.usage = secondDesc.active.usage | gpu::BufferUsage::CopySource;
            rendering::detail::DedicatedResourceRequest firstRequest =
                rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(firstDesc));
            rendering::detail::DedicatedResourceRequest secondRequest =
                rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(secondDesc));
            firstRequest.requiresCanonicalDescriptors = true;
            secondRequest.requiresCanonicalDescriptors = true;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 0;
            config.descriptorCacheMaximum = 2;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(firstRequest, 63, first, &failure))
                return fail("descriptor hard-cap first acquire");
            const vanguard::u64 allocationFailuresBefore = gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocationFailures;
            rendering::detail::DedicatedResourceAssignment rejected;
            if (pool.Acquire(secondRequest, 64, rejected, &failure) || rejected.IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::CapacityExceeded)
                return fail("descriptor hard-cap rejection");
            const rendering::detail::AllocatorDescriptorLedgerStats rejectedStats = descriptorLedger.GetStats();
            if (rejectedStats.liveSlots != 2 || rejectedStats.rejectedReservations != 1 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocationFailures != allocationFailuresBefore)
                return fail("descriptor hard-cap pre-RHI accounting");
            pool.Rollback(first.entry);
            if (!pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("descriptor hard-cap cleanup");
            pool.Poll();
        }

        // Descriptor pressure must refresh Held entries after an escaped native
        // owner disappears. Otherwise an unrelated compatibility class can hit
        // the local descriptor hard cap even though the old canonical bundle is
        // now exactly pool-owned and legally evictable.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("stale held descriptor pressure domain");
            rendering::FrameBufferDesc descA = bufferDesc;
            descA.active.structureStride = 16;
            descA.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::FrameBufferDesc descB = descA;
            descB.active.usage = descB.active.usage | gpu::BufferUsage::CopySource;
            rendering::detail::DedicatedResourceRequest requestA = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descA));
            rendering::detail::DedicatedResourceRequest requestB = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descB));
            requestA.requiresCanonicalDescriptors = true;
            requestB.requiresCanonicalDescriptors = true;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 0;
            config.descriptorCacheMaximum = 2;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment a;
            if (!pool.Acquire(requestA, 65, a, &failure))
                return fail("stale held descriptor pressure A acquire");
            const gpu::BufferRef aBuffer = a.buffer;
            gpu::Buffer escaped(a.buffer);
            pool.Rollback(a.entry);
            if (descriptorLedger.GetStats().liveSlots != 2 || descriptorLedger.GetStats().reusableSlots != 0)
                return fail("stale held descriptor pressure held setup");
            escaped.Reset();

            rendering::detail::DedicatedResourceAssignment b;
            if (!pool.Acquire(requestB, 66, b, &failure) || !b.shaderResource.IsValid() || !b.unorderedAccess.IsValid())
                return fail("stale held descriptor pressure admission");
            const rendering::detail::AllocatorDescriptorLedgerStats pressured = descriptorLedger.GetStats();
            if (pressured.liveSlots != 2 || pressured.pressureEvictions != 1 || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("stale held descriptor pressure accounting");
            pool.Rollback(b.entry);

            rendering::detail::DedicatedResourceAssignment aAgain;
            if (!pool.Acquire(requestA, 67, aAgain, &failure) || aAgain.buffer != aBuffer || !aAgain.shaderResource.IsValid() || !aAgain.unorderedAccess.IsValid())
                return fail("stale held descriptor pressure native preservation");
            pool.Rollback(aAgain.entry);
            if (!pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("stale held descriptor pressure cleanup");
            pool.Poll();
        }

        // Local hard-cap pressure drops the oldest reusable descriptor bundle
        // toward the soft target without destroying its native resource. Later
        // demand reuses that native object and republishes its canonical pair.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 6, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("descriptor pressure domain");
            rendering::FrameBufferDesc descA = bufferDesc;
            descA.active.structureStride = 16;
            descA.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::FrameBufferDesc descB = descA;
            descB.active.usage = descB.active.usage | gpu::BufferUsage::CopySource;
            rendering::FrameBufferDesc descC = descA;
            descC.active.usage = descC.active.usage | gpu::BufferUsage::CopyDestination;
            rendering::detail::DedicatedResourceRequest requestA = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descA));
            rendering::detail::DedicatedResourceRequest requestB = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descB));
            rendering::detail::DedicatedResourceRequest requestC = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descC));
            requestA.requiresCanonicalDescriptors = true;
            requestB.requiresCanonicalDescriptors = true;
            requestC.requiresCanonicalDescriptors = true;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 2;
            config.descriptorCacheMaximum = 4;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment a;
            rendering::detail::DedicatedResourceAssignment b;
            if (!pool.Acquire(requestA, 70, a, &failure))
                return fail("descriptor pressure A acquire");
            const gpu::BufferRef aBuffer = a.buffer;
            pool.Rollback(a.entry);
            if (!pool.Acquire(requestB, 71, b, &failure))
                return fail("descriptor pressure B acquire");
            pool.Rollback(b.entry);
            if (descriptorLedger.GetStats().liveSlots != 4 || descriptorLedger.GetStats().reusableSlots != 4)
                return fail("descriptor pressure reusable setup");

            rendering::detail::DedicatedResourceAssignment c;
            if (!pool.Acquire(requestC, 72, c, &failure))
                return fail("descriptor pressure C acquire");
            const rendering::detail::AllocatorDescriptorLedgerStats pressured = descriptorLedger.GetStats();
            if (pressured.liveSlots != 4 || pressured.reusableSlots != 2 || pressured.pressureEvictions != 1)
                return fail("descriptor pressure target accounting");

            rendering::detail::DedicatedResourceAssignment aAgain;
            if (!pool.Acquire(requestA, 73, aAgain, &failure) || aAgain.buffer != aBuffer || !aAgain.shaderResource.IsValid() || !aAgain.unorderedAccess.IsValid())
                return fail("descriptor pressure native reuse");
            const rendering::detail::AllocatorDescriptorLedgerStats republished = descriptorLedger.GetStats();
            if (republished.pressureEvictions != 2 || republished.publicationMisses != 4)
                return fail("descriptor pressure cold-order republish");
            pool.Rollback(c.entry);
            pool.Rollback(aAgain.entry);
            if (!pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("descriptor pressure cleanup");
            pool.Poll();
        }

        // Shared-domain exhaustion is separate from the allocator's local cap.
        // Allow the first attempt to allocate one handle and fail on the second;
        // rollback must reclaim that partial handle, evict one cold bundle, and
        // retry the complete publication exactly once.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("shared descriptor pressure domain");
            const gpu::DescriptorHandle external = gpu::AllocateDescriptor(descriptors.GetRef(), &rhiFailure);
            if (!external.IsValid())
                return fail("shared descriptor pressure external slot");
            rendering::FrameBufferDesc descA = bufferDesc;
            descA.active.structureStride = 16;
            descA.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::FrameBufferDesc descB = descA;
            descB.active.usage = descB.active.usage | gpu::BufferUsage::CopySource;
            rendering::detail::DedicatedResourceRequest requestA = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descA));
            rendering::detail::DedicatedResourceRequest requestB = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descB));
            requestA.requiresCanonicalDescriptors = true;
            requestB.requiresCanonicalDescriptors = true;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 0;
            config.descriptorCacheMaximum = 4;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment a;
            if (!pool.Acquire(requestA, 80, a, &failure))
                return fail("shared descriptor pressure A acquire");
            pool.Rollback(a.entry);
            const gpu::DescriptorDomainStats beforeRetry = gpu::GetDescriptorDomainStats(descriptors.GetRef());
            if (beforeRetry.allocated != 3 || beforeRetry.free != 1)
                return fail("shared descriptor pressure setup accounting");

            rendering::detail::DedicatedResourceAssignment b;
            if (!pool.Acquire(requestB, 81, b, &failure) || !b.shaderResource.IsValid() || !b.unorderedAccess.IsValid())
                return fail("shared descriptor pressure bounded retry");
            const gpu::DescriptorDomainStats afterRetry = gpu::GetDescriptorDomainStats(descriptors.GetRef());
            const rendering::detail::AllocatorDescriptorLedgerStats retryStats = descriptorLedger.GetStats();
            if (afterRetry.allocated != 3 || afterRetry.allocationFailures != beforeRetry.allocationFailures + 1 || retryStats.liveSlots != 2 || retryStats.pressureEvictions != 1)
                return fail("shared descriptor pressure transactional rollback");
            pool.Rollback(b.entry);
            if (!gpu::RetireDescriptor(descriptors.GetRef(), external, {}, &rhiFailure) || !pool.ClearPersistentCache(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("shared descriptor pressure cleanup");
            pool.Poll();
        }

        // Pressure-time held refresh must retry exact compatibility before
        // eviction. Otherwise a same-frame owner release can promote the exact
        // reusable entry only to have pressure immediately evict it and report
        // BudgetExceeded while that compatible object already exists.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(physicalDesc, 14, first, &failure))
                return fail("same-frame held-promotion first acquire");
            gpu::Buffer retained(first.buffer);
            pool.Rollback(first.entry);

            rendering::detail::DedicatedResourceAssignment blocked;
            if (pool.Acquire(physicalDesc, 15, blocked, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("same-frame held-promotion demotion");
            retained.Reset();

            rendering::detail::DedicatedResourceAssignment promoted;
            if (!pool.Acquire(physicalDesc, 15, promoted, &failure) || promoted.entry != first.entry)
                return fail("same-frame held-promotion compatible retry");
            const rendering::detail::DedicatedResourcePoolStats promotedStats = pool.GetStats();
            if (promotedStats.hits != 1 || promotedStats.pendingNativeDestruction != 0 || promotedStats.chargedBytes != requirements.size)
                return fail("same-frame held-promotion accounting");
            pool.Rollback(promoted.entry);
        }

        // Texture compatibility is exact, so reuse keeps the historical
        // newest-created choice independent of the order resources returned to
        // the reusable set.
        {
            rendering::FrameTextureDesc textureDesc;
            textureDesc.active.extent = {8, 8, 1};
            textureDesc.active.dimension = gpu::TextureDimension::Texture2D;
            textureDesc.active.format = gpu::Format::R8G8B8A8UNorm;
            textureDesc.active.mipCount = 1;
            textureDesc.active.arraySize = 1;
            textureDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
            textureDesc.active.initialState = gpu::ResourceState::Common;
            textureDesc.maximumExtent = textureDesc.active.extent;
            textureDesc.maximumMipCount = 1;
            const rendering::FrameResourceDesc textureResource = rendering::FrameResourceDesc::Texture(textureDesc);

            rendering::RenderFlowResourceAllocatorConfig config;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(textureResource, 16, first, &failure) || !pool.Acquire(textureResource, 17, second, &failure) || first.entry == second.entry)
                return fail("texture newest-created setup");
            pool.Rollback(second.entry);
            pool.Rollback(first.entry);

            rendering::detail::DedicatedResourceAssignment reused;
            if (!pool.Acquire(textureResource, 18, reused, &failure) || reused.entry != second.entry)
                return fail("texture newest-created reuse order");
            pool.Rollback(reused.entry);
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

        // Bytes already committed to observation-backed native destruction remain
        // charged to the hard ledger, but no longer count as retained-cache bytes
        // for the next soft-target pass. Otherwise every frame would evict another
        // reusable object while the first eviction merely waits for RHI reclamation.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.bufferSoftTargetBytes = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(physicalDesc, 30, first, &failure) || !pool.Acquire(physicalDesc, 31, second, &failure))
                return fail("soft-trim projection acquire");
            pool.Rollback(first.entry);
            pool.Rollback(second.entry);
            if (!pool.TrimToSoftTargets(&failure))
                return fail("soft-trim projection first pass");
            const rendering::detail::DedicatedResourcePoolStats firstTrim = pool.GetStats();
            if (firstTrim.pendingNativeDestruction != 1 || firstTrim.chargedBytes != requirements.size * 2u)
                return fail("soft-trim projection first accounting");
            if (!pool.TrimToSoftTargets(&failure))
                return fail("soft-trim projection second pass");
            const rendering::detail::DedicatedResourcePoolStats secondTrim = pool.GetStats();
            if (secondTrim.pendingNativeDestruction != 1 || secondTrim.chargedBytes != requirements.size * 2u)
                return fail("soft-trim projection repeated eviction guard");
            rendering::detail::DedicatedResourceAssignment retained;
            if (!pool.Acquire(physicalDesc, 32, retained, &failure) || pool.GetStats().hits != 1)
                return fail("soft-trim projection retained working set");
            pool.Rollback(retained.entry);
            if (!gpu::FlushRetiredResources(&rhiFailure))
                return fail("soft-trim projection native release");
            pool.Poll();
            if (pool.GetStats().chargedBytes != requirements.size)
                return fail("soft-trim projection hard-ledger release");
        }

        // RF4 transfer derives the canonical descriptor baseline from the pool
        // entry itself: pool + publication + populated descriptor references are
        // legal, while any additional owner still blocks export. Descriptor
        // retirement admission is transactional with respect to native ownership:
        // a partial descriptor failure leaves the pool owner/charge intact and a
        // retry retires only the remaining handle before committing transfer.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("transfer descriptor domain");
            rendering::RenderFlowResourceAllocatorConfig config;
            config.resourceDescriptors = descriptors.GetRef();
            config.descriptorCacheTarget = 2;
            config.descriptorCacheMaximum = 2;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, ledger, descriptorLedger, descriptors.GetRef());
            rendering::FrameBufferDesc transferDesc = bufferDesc;
            transferDesc.active.structureStride = 16;
            transferDesc.active.usage = transferDesc.active.usage | gpu::BufferUsage::Structured;
            rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(transferDesc));
            request.requiresCanonicalDescriptors = true;
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(request, 24, assignment, &failure) || !assignment.shaderResource.IsValid() || !assignment.unorderedAccess.IsValid() ||
                descriptorLedger.GetStats().liveSlots != 2)
                return fail("transfer ownership acquire");
            const vanguard::u64 transferBytes = assignment.requirements.size;
            gpu::Buffer publication(assignment.buffer);
            gpu::Buffer escaped(assignment.buffer);
            const rendering::detail::DedicatedResourceTransfer entries[] = {{assignment.entry}};
            if (pool.TransferOwnership({entries, 1}, {}, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation ||
                pool.GetStats().chargedBytes != transferBytes || descriptorLedger.GetStats().liveSlots != 2)
                return fail("transfer escaped-owner guard");
            escaped.Reset();
            pool.SetProviderFailureInjection({rendering::detail::DedicatedResourceProviderFailurePoint::CanonicalDescriptorRetirement, 1,
                                              rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure});
            if (pool.TransferOwnership({entries, 1}, {}, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure ||
                pool.GetStats().chargedBytes != transferBytes || descriptorLedger.GetStats().liveSlots != 1 || !publication.IsValid())
                return fail("transfer partial descriptor retirement");
            pool.ClearProviderFailureInjection();
            if (!pool.TransferOwnership({entries, 1}, {}, &failure) || pool.GetStats().chargedBytes != 0 || descriptorLedger.GetStats().liveSlots != 0 || !publication.IsValid())
                return fail("transfer exact publication ownership");
            if (!gpu::FlushRetiredResources(&rhiFailure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("transfer descriptor retirement cleanup");
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

        // RF6 persistent lane leases keep pool ownership authoritative while
        // bypassing the general reusable indices on a warm ready checkout.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            const rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(physicalDesc);
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(request, 27, first, &failure))
                return fail("persistent lease ready acquire");
            rendering::detail::DedicatedResourceLease lease;
            if (!pool.ReserveLease(first.entry, lease) || !lease.IsValid())
                return fail("persistent lease ready reserve");
            pool.Rollback(first.entry);
            rendering::detail::DedicatedResourceAssignment warm;
            if (pool.TryAcquireLease(lease, request, 28, warm, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Acquired ||
                warm.entry != first.entry || warm.buffer != first.buffer)
                return fail("persistent lease ready checkout");
            pool.Rollback(warm.entry);
            pool.ReleaseLease(lease);
            rendering::detail::DedicatedResourceAssignment generic;
            if (!pool.Acquire(request, 29, generic, &failure) || generic.entry != first.entry)
                return fail("persistent lease release to generic reuse");
            pool.Rollback(generic.entry);
        }

        // A pending lease is never waited on. The caller can provision a
        // second backing, reserve it for the same logical lane, and use that
        // ready instance while the first remains fence-gated.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size * 2u;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            const rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(physicalDesc);
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(request, 30, first, &failure))
                return fail("persistent lease pending first acquire");
            rendering::detail::DedicatedResourceLease firstLease;
            if (!pool.ReserveLease(first.entry, firstLease))
                return fail("persistent lease pending first reserve");
            gpu::ResidencyFenceSet incomplete;
            incomplete.graphics = ~vanguard::u64{0};
            pool.Retire(first.entry, incomplete);
            rendering::detail::DedicatedResourceAssignment unavailable;
            if (pool.TryAcquireLease(firstLease, request, 31, unavailable, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Unavailable)
                return fail("persistent lease pending reuse guard");

            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(request, 31, second, &failure) || second.entry == first.entry)
                return fail("persistent lease pending fallback acquire");
            rendering::detail::DedicatedResourceLease secondLease;
            if (!pool.ReserveLease(second.entry, secondLease))
                return fail("persistent lease pending fallback reserve");
            pool.Rollback(second.entry);
            rendering::detail::DedicatedResourceAssignment ready;
            if (pool.TryAcquireLease(secondLease, request, 32, ready, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Acquired ||
                ready.entry != second.entry)
                return fail("persistent lease pending ring checkout");
            pool.Rollback(ready.entry);
            pool.DeviceLost();
        }

        // storeSerial protects recycled entry slots and leaseSerial protects
        // reservation ABA on the same live entry. Re-reserving a released
        // backing must never make an older plan token valid again.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            const rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(physicalDesc);
            rendering::detail::DedicatedResourceAssignment first;
            if (!pool.Acquire(request, 33, first, &failure))
                return fail("persistent lease ABA first acquire");
            rendering::detail::DedicatedResourceLease oldLease;
            if (!pool.ReserveLease(first.entry, oldLease))
                return fail("persistent lease ABA first reserve");
            pool.Rollback(first.entry);
            pool.ReleaseLease(oldLease);

            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(request, 34, second, &failure) || second.entry != first.entry)
                return fail("persistent lease ABA generic reacquire");
            rendering::detail::DedicatedResourceLease newLease;
            if (!pool.ReserveLease(second.entry, newLease) || newLease.leaseSerial == oldLease.leaseSerial)
                return fail("persistent lease ABA new reservation");
            pool.Rollback(second.entry);
            rendering::detail::DedicatedResourceAssignment stale;
            if (pool.TryAcquireLease(oldLease, request, 35, stale, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Stale)
                return fail("persistent lease ABA stale token rejection");
            rendering::detail::DedicatedResourceAssignment current;
            if (pool.TryAcquireLease(newLease, request, 35, current, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Acquired)
                return fail("persistent lease ABA current token checkout");
            pool.Rollback(current.entry);
            pool.ReleaseLease(newLease);
        }

        // Persistent lane association does not pin an idle backing against
        // native soft pressure. Trimming may invalidate the weak lease while
        // preserving the normal observation-backed destruction contract.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.bufferSoftTargetBytes = 0;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            const rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(physicalDesc);
            rendering::detail::DedicatedResourceAssignment assignment;
            if (!pool.Acquire(request, 36, assignment, &failure))
                return fail("persistent lease trim acquire");
            rendering::detail::DedicatedResourceLease lease;
            if (!pool.ReserveLease(assignment.entry, lease))
                return fail("persistent lease trim reserve");
            pool.Rollback(assignment.entry);
            if (!pool.TrimToSoftTargets(&failure))
                return fail("persistent lease trim request");
            rendering::detail::DedicatedResourceAssignment stale;
            if (pool.TryAcquireLease(lease, request, 37, stale, &failure) != rendering::detail::DedicatedResourceLeaseAcquireResult::Stale ||
                pool.GetStats().pendingNativeDestruction != 1)
                return fail("persistent lease trim invalidation");
            pool.DeviceLost();
        }

        // Resolve failure returns a generation's dedicated assignments as one
        // pool transaction rather than reacquiring the pool lock per resource.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size * 2u;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(physicalDesc, 40, first, &failure) || !pool.Acquire(physicalDesc, 41, second, &failure) || first.entry == second.entry)
                return fail("batch rollback acquire");
            const vanguard::u32 entries[] = {first.entry, second.entry};
            pool.Rollback(entries);
            rendering::detail::DedicatedResourceAssignment reusedA;
            rendering::detail::DedicatedResourceAssignment reusedB;
            if (!pool.Acquire(physicalDesc, 42, reusedA, &failure) || !pool.Acquire(physicalDesc, 43, reusedB, &failure) ||
                reusedA.entry == reusedB.entry || pool.GetStats().hits != 2)
                return fail("batch rollback reusable publication");
            const vanguard::u32 reusedEntries[] = {reusedA.entry, reusedB.entry};
            pool.Rollback(reusedEntries);
        }

        // One terminal generation shares one fence set across all of its
        // dedicated objects. The batch path must keep every member pending
        // together while that fence is incomplete.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size * 2u;
            rendering::detail::AllocatorNativeByteLedger ledger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, ledger);
            rendering::detail::DedicatedResourceAssignment first;
            rendering::detail::DedicatedResourceAssignment second;
            if (!pool.Acquire(physicalDesc, 50, first, &failure) || !pool.Acquire(physicalDesc, 51, second, &failure) || first.entry == second.entry)
                return fail("batch retirement acquire");
            gpu::ResidencyFenceSet incomplete;
            incomplete.graphics = ~vanguard::u64{0};
            const vanguard::u32 reusableRetirements[] = {first.entry};
            const vanguard::u32 quarantinedRetirements[] = {second.entry};
            pool.Retire(reusableRetirements, quarantinedRetirements, incomplete);
            pool.Poll();
            const rendering::detail::DedicatedResourcePoolStats pending = pool.GetStats();
            rendering::detail::DedicatedResourceAssignment blocked;
            if (pending.pendingRetirement != 2 || pending.chargedBytes != requirements.size * 2u || pool.Acquire(physicalDesc, 52, blocked, &failure) ||
                failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("batch retirement shared-fence pending guard");
            pool.DeviceLost();
            if (pool.GetStats().chargedBytes != 0 || pool.GetStats().pendingRetirement != 0)
                return fail("batch retirement device-loss cleanup");
        }

        const auto resolveBuffers = [](rendering::RenderFlowResourceAllocator& allocator, const vanguard::u64 frameSerial, const vanguard::u32 count,
                                       const rendering::FrameBufferDesc& frameBufferDesc, rendering::RenderFlowResourceFailure& outputFailure) noexcept
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
                if (!writer.DeclareBuffer({space, names[index]}, frameBufferDesc, resources[index], &outputFailure))
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

        // Healthy shutdown must reject an Assigned native resource even when it
        // owns no canonical descriptors. State validation is independent of
        // descriptor presence; otherwise explicit-view-only assignments can be
        // silently released by CommitHealthyShutdown.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger);
            rendering::detail::DedicatedResourceAssignment assigned;
            if (!pool.Acquire(physicalDesc, 29, assigned, &failure))
                return fail("shutdown assigned-state setup");
            bool deviceLostEvidence = true;
            if (pool.PrepareHealthyShutdown(deviceLostEvidence, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidPhase || deviceLostEvidence)
                return fail("shutdown assigned-state rejection");
            pool.Rollback(assigned.entry);
            if (!pool.PrepareHealthyShutdown(deviceLostEvidence, &failure) || deviceLostEvidence || !pool.ValidateHealthyShutdownCommit(&failure))
                return fail("shutdown assigned-state recovery");
            pool.CommitHealthyShutdown();
        }

        // Detached-native states are required to carry no canonical descriptor
        // ownership. Synthesize the otherwise unreachable corrupt states so the
        // defensive healthy-shutdown guard cannot regress silently.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("shutdown detached-descriptor invariant domain");
            rendering::FrameBufferDesc descriptorDesc = bufferDesc;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::detail::DedicatedResourceRequest request = rendering::detail::BuildDedicatedResourceRequest(rendering::FrameResourceDesc::Buffer(descriptorDesc));
            request.requiresCanonicalDescriptors = true;
            rendering::RenderFlowResourceAllocatorConfig config;
            config.descriptorCacheTarget = 4;
            config.descriptorCacheMaximum = 4;
            rendering::detail::AllocatorNativeByteLedger nativeLedger(config.hardNativeByteLimit);
            rendering::detail::AllocatorDescriptorLedger descriptorLedger(config.descriptorCacheTarget, config.descriptorCacheMaximum);
            rendering::detail::DedicatedResourcePool pool(config, nativeLedger, descriptorLedger, descriptors.GetRef());
            rendering::detail::DedicatedResourceAssignment assigned;
            if (!pool.Acquire(request, 29, assigned, &failure) || !assigned.shaderResource.IsValid() || !assigned.unorderedAccess.IsValid())
                return fail("shutdown detached-descriptor invariant setup");
            for (const rendering::detail::DedicatedResourceState corrupt : {rendering::detail::DedicatedResourceState::EvictedPendingNativeDestruction,
                                                                            rendering::detail::DedicatedResourceState::NativeReleased})
            {
                if (!rendering::detail::DedicatedResourcePoolInvariantTestAccess::SetState(pool, assigned.entry, corrupt))
                    return fail("shutdown detached-descriptor invariant mutation");
                bool deviceLostEvidence = true;
                if (pool.PrepareHealthyShutdown(deviceLostEvidence, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::BackendContractViolation || deviceLostEvidence)
                    return fail("shutdown detached-descriptor invariant rejection");
            }
            if (!rendering::detail::DedicatedResourcePoolInvariantTestAccess::SetState(pool, assigned.entry, rendering::detail::DedicatedResourceState::Assigned))
                return fail("shutdown detached-descriptor invariant restore");
            pool.Rollback(assigned.entry);
            bool deviceLostEvidence = true;
            if (!pool.PrepareHealthyShutdown(deviceLostEvidence, &failure) || deviceLostEvidence || !gpu::FlushRetiredResources(&rhiFailure) ||
                !pool.ValidateHealthyShutdownCommit(&failure))
                return fail("shutdown detached-descriptor invariant recovery");
            pool.CommitHealthyShutdown();
        }

        // Stop the second acquisition at every provider boundary. Resolve must
        // publish no generation, return the first assignment to Reusable, and
        // keep any already-created failed native object charged until release.
        for (const rendering::detail::DedicatedResourceProviderFailurePoint point : failurePoints)
        {
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure))
                return fail("rollback allocator init");
            rendering::detail::DedicatedResourceProviderTestAccess::Set(allocator, {point, 1, rendering::RenderFlowResourceFailureCode::NativeOutOfMemory});
            if (resolveBuffers(allocator, 30, 2, bufferDesc, failure) || allocator.GetExecutionGeneration().IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::NativeOutOfMemory)
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
            if (!resolveBuffers(allocator, 31, 1, bufferDesc, failure) || allocator.GetStats().dedicatedResourcePoolHits != 1 || !abortResolved(allocator, failure) ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("rolled-back assignment reuse");
        }

        // Once a canonical bundle has been installed for an earlier dedicated
        // assignment, a later Resolve failure returns that physical owner to the
        // reusable cache without tearing its descriptors down. The next frame
        // must hit both the native object and its canonical publication.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("resolve rollback descriptor domain");
            rendering::FrameBufferDesc descriptorDesc = bufferDesc;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;

            rendering::RenderFlowResourceAllocatorConfig config;
            config.resourceDescriptors = descriptors.GetRef();
            config.descriptorCacheTarget = 4;
            config.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize(config, &failure))
                return fail("resolve rollback descriptor allocator init");
            rendering::detail::DedicatedResourceProviderTestAccess::Set(
                allocator, {rendering::detail::DedicatedResourceProviderFailurePoint::NativeCreation, 1, rendering::RenderFlowResourceFailureCode::NativeOutOfMemory});
            if (resolveBuffers(allocator, 35, 2, descriptorDesc, failure) || allocator.GetExecutionGeneration().IsValid() ||
                failure.code != rendering::RenderFlowResourceFailureCode::NativeOutOfMemory)
                return fail("resolve rollback canonical installation");
            rendering::detail::DedicatedResourceProviderTestAccess::Clear(allocator);
            const rendering::RenderFlowResourceAllocatorStats rolledBack = allocator.GetStats();
            if (rolledBack.state != rendering::RenderFlowResourceSessionState::Idle || rolledBack.canonicalDescriptorLiveSlots != 2 ||
                rolledBack.canonicalDescriptorPublicationMisses != 1 || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("resolve rollback canonical persistence");

            if (!resolveBuffers(allocator, 36, 1, descriptorDesc, failure))
                return fail("resolve rollback canonical warm reuse");
            const rendering::RenderFlowResourceAllocatorStats warm = allocator.GetStats();
            if (warm.dedicatedResourcePoolHits != 1 || warm.canonicalDescriptorLiveSlots != 2 || warm.canonicalDescriptorPublicationHits != 1 ||
                warm.canonicalDescriptorPublicationMisses != 1 || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("resolve rollback canonical warm accounting");
            if (!abortResolved(allocator, failure) || !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure) ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("resolve rollback canonical cleanup");
        }

        // Healthy shutdown admission is retryable even if one canonical handle
        // was already retired before a later retirement admission fails. Pool,
        // native resource, and descriptor-domain ownership stay attached until
        // the retry completes.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("shutdown retirement retry domain");
            rendering::FrameBufferDesc descriptorDesc = bufferDesc;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::RenderFlowResourceAllocatorConfig config;
            config.resourceDescriptors = descriptors.GetRef();
            config.descriptorCacheTarget = 4;
            config.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize(config, &failure) || !resolveBuffers(allocator, 90, 1, descriptorDesc, failure) || !abortResolved(allocator, failure))
                return fail("shutdown retirement retry setup");
            const rendering::RenderFlowResourceAllocatorStats beforeFailure = allocator.GetStats();
            if (beforeFailure.canonicalDescriptorLiveSlots != 2 || beforeFailure.chargedNativeBytes == 0 || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
                return fail("shutdown retirement retry setup accounting");
            rendering::detail::DedicatedResourceProviderTestAccess::Set(
                allocator, {rendering::detail::DedicatedResourceProviderFailurePoint::CanonicalDescriptorRetirement, 1,
                            rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure});
            if (allocator.Shutdown(&failure) || !allocator.IsInitialized() || allocator.GetState() != rendering::RenderFlowResourceSessionState::Idle ||
                failure.code != rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure)
                return fail("shutdown retirement admission failure");
            const rendering::RenderFlowResourceAllocatorStats partial = allocator.GetStats();
            if (partial.canonicalDescriptorLiveSlots != 1 || partial.chargedNativeBytes != beforeFailure.chargedNativeBytes ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 1)
                return fail("shutdown retirement partial ownership");
            if (!allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("shutdown retirement retry completion");
        }

        // A non-device-loss drain failure occurs after all descriptor retirement
        // admissions but before native-owner commit. It must leave the allocator
        // intact and retryable rather than routing through DeviceLost cleanup.
        {
            const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                    gpu::ShaderStageBit(gpu::ShaderStage::Compute);
            gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                              gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
            if (!descriptors.IsValid())
                return fail("shutdown drain retry domain");
            rendering::FrameBufferDesc descriptorDesc = bufferDesc;
            descriptorDesc.active.structureStride = 16;
            descriptorDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            rendering::RenderFlowResourceAllocatorConfig config;
            config.resourceDescriptors = descriptors.GetRef();
            config.descriptorCacheTarget = 4;
            config.descriptorCacheMaximum = 4;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize(config, &failure) || !resolveBuffers(allocator, 91, 1, descriptorDesc, failure) || !abortResolved(allocator, failure))
                return fail("shutdown drain retry setup");
            const rendering::RenderFlowResourceAllocatorStats beforeFailure = allocator.GetStats();
            rendering::detail::RenderFlowResourceShutdownTestAccess::Set(
                allocator, {rendering::detail::RenderFlowResourceShutdownFailurePoint::RetirementFlush});
            if (allocator.Shutdown(&failure) || !allocator.IsInitialized() || allocator.GetState() != rendering::RenderFlowResourceSessionState::Idle ||
                failure.code != rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure)
                return fail("shutdown drain injected failure");
            const rendering::RenderFlowResourceAllocatorStats admitted = allocator.GetStats();
            if (admitted.canonicalDescriptorLiveSlots != 0 || admitted.chargedNativeBytes != beforeFailure.chargedNativeBytes || admitted.chargedNativeBytes == 0 ||
                gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
                return fail("shutdown drain retry ownership");
            if (!allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("shutdown drain retry completion");
        }

        // The same rollback path must preserve an admitted first object when a
        // second assignment cannot fit the one-object hard budget.
        {
            rendering::RenderFlowResourceAllocatorConfig config;
            config.hardNativeByteLimit = requirements.size;
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize(config, &failure))
                return fail("budget allocator init");
            if (resolveBuffers(allocator, 40, 2, bufferDesc, failure) || allocator.GetExecutionGeneration().IsValid() || failure.code != rendering::RenderFlowResourceFailureCode::BudgetExceeded)
                return fail("hard-budget resolve rejection");
            const rendering::RenderFlowResourceAllocatorStats rejected = allocator.GetStats();
            if (rejected.state != rendering::RenderFlowResourceSessionState::Idle || rejected.chargedNativeBytes != requirements.size || rejected.dedicatedResourcePoolMisses != 1)
                return fail("hard-budget rollback accounting");
            if (!resolveBuffers(allocator, 41, 1, bufferDesc, failure) || allocator.GetStats().dedicatedResourcePoolHits != 1 || !abortResolved(allocator, failure) ||
                !allocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
                return fail("hard-budget rollback reuse");
        }

        // Device loss is a distinct terminal path: no poisoned-fence wait,
        // every pool charge is invalidated once, and the allocator stays
        // unavailable until it is recreated.
        {
            rendering::RenderFlowResourceAllocator allocator;
            if (!allocator.Initialize({}, &failure) || !resolveBuffers(allocator, 50, 1, bufferDesc, failure))
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

        if (!RunRhiUninitializedShutdownProofSubprocess())
            return fail("RHI-uninitialized shutdown/destructor proof");

        return true;
    }

    // RF11 real-backend parity: the same buffer/texture clear + repeated UAV
    // uses must execute with NVRHI automatic close-state restoration enabled or
    // disabled. Disjoint logical lifetimes also exercise the placed alias tape.
    [[nodiscard]] bool RunAutomaticExplicitStateParityTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        rendering::RenderFlowResourceAllocator allocator;
        const auto fail = [&](const char* stage) noexcept
        {
            std::fprintf(stderr, "[rf11-state-parity] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        if (!allocator.Initialize({}, &failure))
            return fail("initialize");

        rendering::ResourceUseId previousGenerationUse;
        for (vanguard::u32 mode = 0; mode < 2; ++mode)
        {
            const bool automatic = mode != 0;
            constexpr rendering::RenderFlowNodeId node{0x711};
            constexpr rendering::GpuFlowGroupId flow{0x711};
            constexpr rendering::CommandScopeId scope{0x711};
            constexpr rendering::FlowSpaceId space{0x711};
            rendering::FrameBufferDesc bufferDesc;
            bufferDesc.active.size = 4096;
            bufferDesc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess | gpu::BufferUsage::CopySource;
            bufferDesc.active.initialState = gpu::ResourceState::Common;
            bufferDesc.active.keepInitialState = automatic;
            bufferDesc.maximumSize = bufferDesc.active.size;
            rendering::FrameTextureDesc textureDesc;
            textureDesc.active.extent = {8, 8, 1};
            textureDesc.active.dimension = gpu::TextureDimension::Texture2D;
            textureDesc.active.format = gpu::Format::R32UInt;
            textureDesc.active.usage = gpu::TextureUsage::UnorderedAccess | gpu::TextureUsage::ShaderResource;
            textureDesc.active.initialState = gpu::ResourceState::Common;
            textureDesc.active.keepInitialState = automatic;
            textureDesc.maximumExtent = textureDesc.active.extent;
            textureDesc.maximumMipCount = 1;
            rendering::BufferUseDesc bufferWrite;
            bufferWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            bufferWrite.access = rendering::LogicalAccessIntent::Write;
            bufferWrite.content = rendering::ResourceContentIntent::Clear;
            bufferWrite.clearValue = rendering::BufferClearValue::Uint(0x12480000u + mode);
            rendering::TextureUseDesc textureWrite;
            textureWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            textureWrite.access = rendering::LogicalAccessIntent::Write;
            textureWrite.content = rendering::ResourceContentIntent::Clear;
            textureWrite.clearValue = rendering::TextureClearValue::Uint(0x24570000u + mode);

            rendering::ResourcePlanningWriter writer;
            rendering::LogicalResourceId logical[4];
            rendering::ResourceUseId uses[6];
            vanguard::u32 useCount = 0;
            if (!allocator.BeginFrame(711 + mode, {true}, &failure) || !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                !writer.DeclareBuffer({space, "rf11.parity.buffer.a"}, bufferDesc, logical[0], &failure) ||
                !writer.DeclareBuffer({space, "rf11.parity.buffer.b"}, bufferDesc, logical[1], &failure) ||
                !writer.DeclareTexture({space, "rf11.parity.texture.a"}, textureDesc, logical[2], &failure) ||
                !writer.DeclareTexture({space, "rf11.parity.texture.b"}, textureDesc, logical[3], &failure))
                return fail("planning declarations");
            bool planned = true;
            for (vanguard::u32 resource = 0; resource < 4 && planned; ++resource)
            {
                planned = writer.OpenResourceScope(logical[resource], &failure);
                const vanguard::u32 repetitions = resource == 1 || resource == 3 ? 2u : 1u;
                for (vanguard::u32 repeat = 0; repeat < repetitions && planned; ++repeat)
                {
                    const vanguard::u32 index = useCount++;
                    planned = resource < 2 ? writer.BeginBufferUse(logical[resource], bufferWrite, uses[index], &failure)
                                           : writer.BeginTextureUse(logical[resource], textureWrite, uses[index], &failure);
                    planned = planned && writer.EndUse(uses[index], &failure);
                }
                planned = planned && writer.CloseResourceScope(logical[resource], &failure);
            }
            if (!planned || useCount != 6 || !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
                !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
                return fail("resolve UAV/clear/alias timeline");

            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            rendering::CompiledExecutionRange ranges[6];
            rendering::ResolvedBufferUse bufferBindings[3];
            rendering::ResolvedTextureUse textureBindings[3];
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246313153544154ull + mode, &rhiFailure);
            bool prepared = commands.IsValid() && gpu::BindCommandList(commands, &rhiFailure) && allocator.PacketFor(node, packet, &failure);
            for (vanguard::u32 index = 0; prepared && index < 6; ++index)
                prepared = packet.ResolveExecutionRange(index, 1, ranges[index], &failure);
            for (vanguard::u32 index = 0; prepared && index < 6; ++index)
                prepared = index < 3 ? packet.ResolveBufferUseBinding(uses[index], index + 1, bufferBindings[index], &failure)
                                     : packet.ResolveTextureUseBinding(uses[index], index + 1, textureBindings[index - 3], &failure);
            if (prepared && mode != 0)
            {
                rendering::ResolvedBufferUse rejected;
                if (packet.ResolveBufferUseBinding(previousGenerationUse, 1, rejected, &failure) || rejected.IsValid() ||
                    failure.code != rendering::RenderFlowResourceFailureCode::IncompleteExecution)
                    prepared = false;
            }
            prepared = prepared && packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 6, cursor, &failure);
            gpu::BufferRef firstBuffer;
            gpu::TextureRef firstTexture;
            for (vanguard::u32 index = 0; prepared && index < 6; ++index)
            {
                const bool buffer = index < 3;
                prepared = cursor.BeginRange(ranges[index], index + 1, &failure) &&
                           (buffer ? bufferBindings[index].IsValid() : textureBindings[index - 3].IsValid());
                if (prepared && index == 0) firstBuffer = bufferBindings[0].GetBuffer();
                if (prepared && index == 3) firstTexture = textureBindings[0].GetTexture();
                if (prepared && index == 1) prepared = bufferBindings[1].GetBuffer() != firstBuffer;
                if (prepared && index == 4) prepared = textureBindings[1].GetTexture() != firstTexture;
                prepared = prepared && cursor.EndRange(ranges[index], index + 1, &failure) &&
                           (buffer ? !bufferBindings[index].IsValid() : !textureBindings[index - 3].IsValid());
            }
            prepared = prepared && cursor.FinalizePacket(&failure) && gpu::FlushPendingBarriers(&rhiFailure);
            if (gpu::GetBoundCommandList().IsValid()) gpu::UnbindCommandList();
            if (!prepared)
            {
                if (commands.IsValid()) gpu::DiscardCommandList(commands);
                return fail("compiled native action execution / stale generation");
            }
            const gpu::CommandListRef submissions[] = {commands};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("RF11 automatic/explicit native state parity", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
                return fail("GPU clear + UAV submission");
            const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
            const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
                return fail("finish state parity frame");
            previousGenerationUse = uses[0];
        }
        return allocator.Shutdown(&failure) && gpu::FlushRetiredResources(&rhiFailure);
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

        // RF11 keeps ordinary state transitions on the intrinsic begin-step
        // path. Only the five semantic clear actions remain in the sparse tape.
        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().compiledResourceActions != 5 || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246434c45415253ull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::CompiledExecutionRange ranges[5];
        rendering::ResolvedBufferUse resolvedBuffer;
        rendering::ResolvedTextureUse resolvedTextures[4];
        rendering::ResolvedTextureUse wrongEpochTexture;
        bool prepared = commands.IsValid() && gpu::BindCommandList(commands, &rhiFailure) && allocator.PacketFor(node, packet, &failure);
        for (vanguard::u32 index = 0; prepared && index < 5; ++index)
            prepared = packet.ResolveExecutionRange(index, 1, ranges[index], &failure);
        prepared = prepared && packet.ResolveBufferUseBinding(uses[0], 1, resolvedBuffer, &failure);
        for (vanguard::u32 index = 1; prepared && index < 5; ++index)
            prepared = packet.ResolveTextureUseBinding(uses[index], index + 1u, resolvedTextures[index - 1u], &failure);
        prepared = prepared && packet.ResolveTextureUseBinding(uses[1], 5, wrongEpochTexture, &failure);
        if (!prepared || !packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 5, cursor, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution setup");
        }
        if (resolvedBuffer.IsValid() || !cursor.BeginRange(ranges[0], 1, &failure) || !resolvedBuffer.IsValid() || !resolvedBuffer.GetBuffer().IsValid() ||
            !cursor.EndRange(ranges[0], 1, &failure) || resolvedBuffer.IsValid())
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("buffer clear recording");
        }
        for (vanguard::u32 index = 1; index < 5; ++index)
        {
            rendering::ResolvedTextureUse& resolvedTexture = resolvedTextures[index - 1u];
            if (resolvedTexture.IsValid() || wrongEpochTexture.IsValid() || !cursor.BeginRange(ranges[index], index + 1u, &failure) ||
                !resolvedTexture.IsValid() || !resolvedTexture.GetTexture().IsValid() ||
                (index == 4u && (wrongEpochTexture.IsValid() || wrongEpochTexture.GetTexture().IsValid())) ||
                !cursor.EndRange(ranges[index], index + 1u, &failure) || resolvedTexture.IsValid())
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

    [[nodiscard]] bool RunRenderFlowDescriptorPublicationSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[resource-flow-descriptors] %s failed (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };

        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        // Exactly one buffer and one texture whole-resource SRV+UAV pair fit.
        // Explicit-view-only bindings must not consume unreachable generic descriptors.
        gpu::DescriptorDomain descriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 4, 0, visibility}, &rhiFailure));
        if (!descriptors.IsValid())
            return fail("descriptor domain creation");

        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = descriptors.GetRef();
        config.descriptorCacheTarget = 4;
        config.descriptorCacheMaximum = 4;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize(config, &failure) || !allocator.BeginFrame(19, {}, &failure))
            return fail("allocator setup");

        rendering::FrameBufferDesc bufferDesc;
        bufferDesc.active.size = 4096;
        bufferDesc.active.structureStride = 16;
        bufferDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        rendering::BufferUseDesc write;
        write.requiredState = gpu::ResourceState::UnorderedAccess;
        write.access = rendering::LogicalAccessIntent::Write;
        write.content = rendering::ResourceContentIntent::Discard;
        rendering::FrameTextureDesc textureDesc;
        textureDesc.active.extent = {8, 8, 1};
        textureDesc.active.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.active.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        textureDesc.active.initialState = gpu::ResourceState::Common;
        textureDesc.maximumExtent = textureDesc.active.extent;
        textureDesc.maximumMipCount = 1;
        rendering::TextureUseDesc textureWrite;
        textureWrite.requiredState = gpu::ResourceState::UnorderedAccess;
        textureWrite.access = rendering::LogicalAccessIntent::Write;
        textureWrite.content = rendering::ResourceContentIntent::Discard;

        constexpr rendering::RenderFlowNodeId node{19};
        constexpr rendering::GpuFlowGroupId flow{19};
        constexpr rendering::CommandScopeId scope{19};
        constexpr rendering::FlowSpaceId space{19};
        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId wholeBuffer;
        rendering::LogicalResourceId viewedBuffer;
        rendering::LogicalBufferViewId explicitView;
        rendering::ResourceUseId wholeUse;
        rendering::ResourceUseId viewedUse;
        rendering::LogicalResourceId wholeTexture;
        rendering::LogicalResourceId viewedTexture;
        rendering::LogicalTextureViewId explicitTextureView;
        rendering::ResourceUseId wholeTextureUse;
        rendering::ResourceUseId viewedTextureUse;
        if (!allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.DeclareBuffer({space, "descriptor.whole"}, bufferDesc, wholeBuffer, &failure) ||
            !writer.DeclareBuffer({space, "descriptor.explicit-view"}, bufferDesc, viewedBuffer, &failure) ||
            !writer.CreateBufferView(viewedBuffer, {}, explicitView, &failure) || !writer.BeginBufferUse(wholeBuffer, write, wholeUse, &failure) ||
            !writer.BeginBufferViewUse(explicitView, write, viewedUse, &failure) ||
            !writer.DeclareTexture({space, "descriptor.texture.whole"}, textureDesc, wholeTexture, &failure) ||
            !writer.DeclareTexture({space, "descriptor.texture.explicit-view"}, textureDesc, viewedTexture, &failure) ||
            !writer.CreateTextureView(viewedTexture, {}, explicitTextureView, &failure) || !writer.BeginTextureUse(wholeTexture, textureWrite, wholeTextureUse, &failure) ||
            !writer.BeginTextureViewUse(explicitTextureView, textureWrite, viewedTextureUse, &failure) || !writer.EndUse(viewedTextureUse, &failure) ||
            !writer.EndUse(wholeTextureUse, &failure) || !writer.EndUse(viewedUse, &failure) || !writer.EndUse(wholeUse, &failure) ||
            !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure))
            return fail("resolve");
        const gpu::DescriptorDomainStats descriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (descriptorStats.allocated != 4 || descriptorStats.populated != 4 || descriptorStats.allocationFailures != 0)
            return fail("descriptor cardinality");
        if (!allocator.BeginExecution(&failure))
            return fail("begin execution");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246444553435055ull, &rhiFailure);
        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::CompiledExecutionRange descriptorRange;
        rendering::ResolvedBufferUse directWhole;
        rendering::ResolvedBufferUse directView;
        rendering::ResolvedTextureUse directWholeTexture;
        rendering::ResolvedTextureUse directTextureView;
        if (!commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
            !packet.ResolveExecutionRange(0, 4, descriptorRange, &failure) ||
            !packet.ResolveBufferUseBinding(wholeUse, 1, directWhole, &failure) || !packet.ResolveBufferUseBinding(viewedUse, 1, directView, &failure) ||
            !packet.ResolveTextureUseBinding(wholeTextureUse, 1, directWholeTexture, &failure) || !packet.ResolveTextureUseBinding(viewedTextureUse, 1, directTextureView, &failure) ||
            !packet.OpenCompiledCursor(scope, gpu::QueueType::Graphics, 1, cursor, &failure) || directWhole.IsValid() || directView.IsValid() || directWholeTexture.IsValid() || directTextureView.IsValid() ||
            !cursor.BeginRange(descriptorRange, 1, &failure) || !directWhole.IsValid() || !directWhole.GetBuffer().IsValid() ||
            !directWhole.GetShaderResourceDescriptor().IsValid() || !directWhole.GetUnorderedAccessDescriptor().IsValid() ||
            !directView.IsValid() || !directView.GetBuffer().IsValid() || !directView.HasExplicitView() || directView.GetShaderResourceDescriptor().IsValid() ||
            directView.GetUnorderedAccessDescriptor().IsValid() || !directWholeTexture.IsValid() || !directWholeTexture.GetTexture().IsValid() ||
            !directWholeTexture.GetShaderResourceDescriptor().IsValid() || !directWholeTexture.GetUnorderedAccessDescriptor().IsValid() ||
            !directTextureView.IsValid() || !directTextureView.GetTexture().IsValid() || !directTextureView.HasExplicitView() ||
            directTextureView.GetShaderResourceDescriptor().IsValid() || directTextureView.GetUnorderedAccessDescriptor().IsValid() ||
            !cursor.EndRange(descriptorRange, 1, &failure) || directWhole.IsValid() || directView.IsValid() || directWholeTexture.IsValid() || directTextureView.IsValid() ||
            !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return fail("execution");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow descriptor publication", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
            return fail("submission");
        const rendering::CommandScopeExecutionReceipt receipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion}};
        const rendering::TerminalExecutionReceipt terminal{generation, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(receipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(generation)};
        if (!allocator.Finish(terminal, &failure))
            return fail("finish");
        const gpu::DescriptorDomainStats terminalDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (!packet.IsValid() || packet.GetNode() != node || terminalDescriptorStats.allocated != 4 || terminalDescriptorStats.populated != 4 || terminalDescriptorStats.pendingRetirement != 0)
            return fail("finish persistent descriptor ownership");
        rendering::ExecutionPacketCursor terminalCursor;
        if (packet.OpenCursor(scope, gpu::QueueType::Graphics, terminalCursor, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidPhase)
            return fail("retained packet terminal guard");
        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !gpu::RetireResources())
            return fail("descriptor retirement completion");
        const gpu::DescriptorDomainStats reclaimedDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (reclaimedDescriptorStats.allocated != 4 || reclaimedDescriptorStats.populated != 4 || reclaimedDescriptorStats.pendingRetirement != 0)
            return fail("persistent descriptor premature retirement");
        if (!allocator.Shutdown(&failure))
            return fail("allocator shutdown");
        const gpu::DescriptorDomainStats shutdownDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        return shutdownDescriptorStats.allocated == 0 && shutdownDescriptorStats.populated == 0 && shutdownDescriptorStats.pendingRetirement == 0 &&
               shutdownDescriptorStats.completedRetirements >= 4;
    }

    [[nodiscard]] bool RunRetainedImportSmokeTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        gpu::BufferDesc bufferDesc;
        bufferDesc.size = 2048;
        bufferDesc.structureStride = 16;
        bufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::CopySource;
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
        const gpu::ShaderStageMask descriptorVisibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) |
                                                          gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                          gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                          gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, descriptorVisibility}, &rhiFailure));
        if (!descriptors.IsValid())
            return false;
        const vanguard::i32 initialBufferRefs = gpu::GetRefCount(externalBuffer.GetRef());
        const vanguard::i32 initialTextureRefs = gpu::GetRefCount(externalTexture.GetRef());
        const auto fail = [&](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[retained-import] %s failed (flow=%u: %s, rhi=%u: %s, refs=%d/%d expected=%d/%d)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code), rhiFailure.message[0] != '\0' ? rhiFailure.message : "none",
                         gpu::GetRefCount(externalBuffer.GetRef()), gpu::GetRefCount(externalTexture.GetRef()), initialBufferRefs, initialTextureRefs);
            return false;
        };

        rendering::RenderFlowResourceAllocatorConfig allocatorConfig;
        allocatorConfig.resourceDescriptors = descriptors.GetRef();
        // RF11 keeps imported canonical descriptors warm across completed frames.
        // A two-slot target prevents maintenance from disguising a reuse miss.
        allocatorConfig.descriptorCacheTarget = 2;
        allocatorConfig.descriptorCacheMaximum = 2;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize(allocatorConfig, &failure) || !allocator.BeginFrame(20, {}, &failure))
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
            allocator.RegisterImport(explicitWaitBuffer, rejectedImport, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch ||
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
        rendering::ResourceUseId overlappingBufferUse;
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
            !writer.BeginBufferUse(logicalBuffer, bufferRead, bufferUse, &failure) || !writer.BeginBufferUse(logicalBuffer, bufferRead, overlappingBufferUse, &failure) ||
            !writer.EndUse(overlappingBufferUse, &failure) || !writer.EndUse(bufferUse, &failure) ||
            !writer.ImportTexture({space, "retained.import.texture"}, importedTexture, logicalTexture, &failure) || !writer.BeginTextureUse(logicalTexture, textureRead, textureUse, &failure) ||
            !writer.EndUse(textureUse, &failure) || !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("planning");

        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().retainedImports != 2 || gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs + 2 ||
            gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs + 2 || !allocator.BeginExecution(&failure))
            return fail("resolve");
        const gpu::DescriptorDomainStats resolvedDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (resolvedDescriptorStats.allocated != 2 || resolvedDescriptorStats.populated != 2 || resolvedDescriptorStats.pendingRetirement != 0)
            return fail("retained import descriptor publication");
        const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();

        rendering::CompiledExecutionPacketView packet;
        rendering::ExecutionPacketCursor cursor;
        rendering::ResolvedBufferUse resolvedBuffer;
        rendering::ResolvedBufferUse overlappingResolvedBuffer;
        rendering::ResolvedTextureUse resolvedTexture;
        gpu::DescriptorHandle coldBufferShaderResource;
        gpu::DescriptorHandle coldTextureShaderResource;
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246494d504f5254ull, &rhiFailure);
        if (!commands || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) || !packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) ||
            !cursor.BeginBufferUse(bufferUse, resolvedBuffer, &failure) || resolvedBuffer.GetBuffer() != externalBuffer.GetRef() ||
            !(coldBufferShaderResource = resolvedBuffer.GetShaderResourceDescriptor()).IsValid() ||
            !cursor.BeginBufferUse(overlappingBufferUse, overlappingResolvedBuffer, &failure) || overlappingResolvedBuffer.GetBuffer() != externalBuffer.GetRef() ||
            !overlappingResolvedBuffer.GetShaderResourceDescriptor().IsValid() ||
            !cursor.EndUse(overlappingBufferUse, &failure) ||
            // The later-begun read ends first. Its EndUse must not execute the import's
            // terminal transition while the earlier read remains active. A same-state
            // transition is a state-tracker probe: the old UseBegin-ordered endpoint
            // selection had already moved this buffer back to Common here.
            !gpu::TransitionBuffer(externalBuffer.GetRef(), gpu::ResourceState::ShaderResourceGraphics, gpu::ResourceState::ShaderResourceGraphics, &rhiFailure) ||
            !cursor.EndUse(bufferUse, &failure) ||
            !cursor.BeginTextureUse(textureUse, resolvedTexture, &failure) || resolvedTexture.GetTexture() != externalTexture.GetRef() ||
            !(coldTextureShaderResource = resolvedTexture.GetShaderResourceDescriptor()).IsValid() || !cursor.EndUse(textureUse, &failure) ||
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
        if (!allocator.Finish(terminal, &failure))
            return fail("terminal completion");
        const gpu::DescriptorDomainStats warmDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (warmDescriptorStats.allocated != 2 || warmDescriptorStats.populated != 2 || warmDescriptorStats.pendingRetirement != 0 ||
            allocator.GetStats().canonicalDescriptorLiveSlots != 2 || allocator.GetStats().canonicalDescriptorReusableSlots != 2)
            return fail("retained import descriptor cache ownership after Finish");
        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !gpu::RetireResources(&rhiFailure))
            return fail("retained import completed frame fence");
        const gpu::DescriptorDomainStats stillWarm = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (stillWarm.allocated != 2 || stillWarm.populated != 2 || stillWarm.pendingRetirement != 0)
            return fail("retained import descriptors must survive completed frame fences");

        // Finish releases frame-import references while the two persistent
        // populated canonical slots each retain one native reference. Copied
        // packet/cursor metadata must not add another native owner.
        if (!packet.IsValid() || packet.GetNode() != node || gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs + 1 ||
            gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs + 1)
            return fail("finish-owned retained-import release");
        packet = {};
        cursor = {};
        if (gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs + 1 || gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs + 1)
            return fail("retained-view release is ownership-neutral");

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

        // Same exact external generations must borrow the same canonical SRVs
        // on the next frame. The old use ID must not gain a fresh liveness lease.
        rendering::ImportedResourceId warmBufferImport;
        rendering::ImportedResourceId warmTextureImport;
        rendering::ResourcePlanningWriter warmWriter;
        rendering::LogicalResourceId warmLogicalBuffer;
        rendering::LogicalResourceId warmLogicalTexture;
        rendering::ResourceUseId warmBufferUse;
        rendering::ResourceUseId warmOverlappingBufferUse;
        rendering::ResourceUseId warmTextureUse;
        if (!allocator.BeginFrame(21, {}, &failure) || !allocator.RegisterImport(bufferImport, warmBufferImport, &failure) ||
            !allocator.RegisterImport(textureImport, warmTextureImport, &failure) ||
            !allocator.CreatePlanningWriter(node, flow, scope, warmWriter, &failure) ||
            !warmWriter.ImportBuffer({space, "retained.import.buffer"}, warmBufferImport, warmLogicalBuffer, &failure) ||
            !warmWriter.BeginBufferUse(warmLogicalBuffer, bufferRead, warmBufferUse, &failure) ||
            !warmWriter.BeginBufferUse(warmLogicalBuffer, bufferRead, warmOverlappingBufferUse, &failure) ||
            !warmWriter.EndUse(warmOverlappingBufferUse, &failure) || !warmWriter.EndUse(warmBufferUse, &failure) ||
            !warmWriter.ImportTexture({space, "retained.import.texture"}, warmTextureImport, warmLogicalTexture, &failure) ||
            !warmWriter.BeginTextureUse(warmLogicalTexture, textureRead, warmTextureUse, &failure) ||
            !warmWriter.EndUse(warmTextureUse, &failure) || !warmWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("warm retained import plan");
        const rendering::RenderFlowResourceAllocatorStats warmStats = allocator.GetStats();
        if (warmStats.canonicalDescriptorLiveSlots != 2 || warmStats.canonicalDescriptorPublicationHits < 2 ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2)
            return fail("warm retained import descriptor hit");

        rendering::CompiledExecutionPacketView warmPacket;
        rendering::ExecutionPacketCursor warmCursor;
        rendering::ResolvedBufferUse warmResolvedBuffer;
        rendering::ResolvedBufferUse warmOverlappingResolvedBuffer;
        rendering::ResolvedTextureUse warmResolvedTexture;
        gpu::CommandListRef warmCommands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246494d5741524dull, &rhiFailure);
        if (!warmCommands || !gpu::BindCommandList(warmCommands, &rhiFailure) || !allocator.PacketFor(node, warmPacket, &failure) ||
            !warmPacket.OpenCursor(scope, gpu::QueueType::Graphics, warmCursor, &failure))
            return fail("warm retained import cursor");
        if (warmCursor.BeginBufferUse(bufferUse, warmResolvedBuffer, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::IncompleteExecution ||
            !warmCursor.BeginBufferUse(warmBufferUse, warmResolvedBuffer, &failure) ||
            warmResolvedBuffer.GetBuffer() != externalBuffer.GetRef() ||
            warmResolvedBuffer.GetShaderResourceDescriptor() != coldBufferShaderResource ||
            !warmCursor.BeginBufferUse(warmOverlappingBufferUse, warmOverlappingResolvedBuffer, &failure) ||
            !warmCursor.EndUse(warmOverlappingBufferUse, &failure) || !warmCursor.EndUse(warmBufferUse, &failure) ||
            !warmCursor.BeginTextureUse(warmTextureUse, warmResolvedTexture, &failure) ||
            warmResolvedTexture.GetTexture() != externalTexture.GetRef() ||
            warmResolvedTexture.GetShaderResourceDescriptor() != coldTextureShaderResource ||
            !warmCursor.EndUse(warmTextureUse, &failure) || !warmCursor.FinalizePacket(&failure) ||
            !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(warmCommands);
            return fail("warm retained import use binding");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef warmSubmissions[] = {warmCommands};
        gpu::GpuFence warmCompletion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow warm retained imports", {warmSubmissions, 1}, gpu::CommandListSyncType::None, warmCompletion, &rhiFailure))
            return fail("warm retained import submission");
        const rendering::CommandScopeExecutionReceipt warmReceipts[] = {{scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, warmCompletion}};
        const rendering::TerminalExecutionReceipt warmTerminal{allocator.GetExecutionGeneration(), rendering::TerminalExecutionCompletionKind::Completed,
                                                                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(warmReceipts),
                                                                rendering::TerminalJoinToken::CompletedSynchronously(allocator.GetExecutionGeneration())};
        if (!allocator.Finish(warmTerminal, &failure) || allocator.GetStats().canonicalDescriptorReusableSlots != 2)
            return fail("warm retained import terminal");
        warmPacket = {};
        warmCursor = {};

        // Clear before waiting for the submitted graphics fence. If the real
        // GPU has not yet finished, neither descriptor slot may be recycled;
        // the RHI must keep both in pending retirement. A fast GPU is permitted
        // to finish during this observation, so make the pending assertion only
        // while the fence is verifiably incomplete rather than inventing a fence.
        if (!allocator.ClearPersistentCaches(&failure))
            return fail("retained import explicit cache clear");
        const gpu::DescriptorDomainStats beforeFence = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (beforeFence.allocated != 0 || beforeFence.populated != 0 || allocator.GetStats().canonicalDescriptorLiveSlots != 0 ||
            (!gpu::IsGpuFenceComplete(warmCompletion) && beforeFence.pendingRetirement != 2))
            return fail("retained descriptors protected by submitted fence");
        if (!gpu::WaitForGpuFence(warmCompletion, 5'000'000'000ull, &rhiFailure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("retained import fence and retirement drain");
        const gpu::DescriptorDomainStats clearedDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        if (clearedDescriptorStats.allocated != 0 || clearedDescriptorStats.populated != 0 || clearedDescriptorStats.pendingRetirement != 0 ||
            gpu::GetRefCount(externalBuffer.GetRef()) != initialBufferRefs || gpu::GetRefCount(externalTexture.GetRef()) != initialTextureRefs ||
            allocator.GetStats().canonicalDescriptorLiveSlots != 0)
            return fail("retained import explicit cache reclamation");

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
        if (!allocator.BeginFrame(22, {}, &failure) || !allocator.RegisterImport(noUseTransition, rejectedNoUseImport, &failure) ||
            !allocator.CreatePlanningWriter(rejectedNode, rejectedFlow, rejectedScope, rejectedWriter, &failure) ||
            !rejectedWriter.ImportBuffer({space, "retained.import.no-use"}, rejectedNoUseImport, rejectedLogical, &failure) || !rejectedWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || allocator.Resolve(nullptr, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::IncompleteExecution || allocator.GetExecutionGeneration().IsValid() ||
            gpu::GetRefCount(externalBuffer.GetRef()) != bufferRefsBeforeRejectedFrame)
            return fail("unused import terminal contract");

        if (allocator.GetStats().chargedNativeBytes != 0 || !allocator.Shutdown(&failure))
            return false;
        const gpu::DescriptorDomainStats shutdownDescriptorStats = gpu::GetDescriptorDomainStats(descriptors.GetRef());
        return shutdownDescriptorStats.allocated == 0 && shutdownDescriptorStats.populated == 0 && shutdownDescriptorStats.pendingRetirement == 0;
    }

    // RF11: a later imported resource exhausting the shared descriptor domain
    // must roll back all earlier borrow publications, with no untracked live
    // generation left behind. The persistent cache may remain warm until clear.
    [[nodiscard]] bool RunRetainedImportRollbackAndDeviceLossTest() noexcept
    {
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* stage) noexcept
        {
            std::fprintf(stderr, "[rf11-retained-rollback] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) |
                                                gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference,
                                          gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, visibility}, &rhiFailure));
        const gpu::DescriptorHandle externalDescriptor = gpu::AllocateDescriptor(descriptors.GetRef(), &rhiFailure);
        gpu::BufferDesc bufferDesc;
        bufferDesc.size = 2048;
        bufferDesc.structureStride = 16;
        bufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        bufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer importedBuffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &rhiFailure));
        gpu::TextureDesc textureDesc;
        textureDesc.extent = {4, 4, 1};
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::ShaderResource;
        textureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture importedTexture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {}, &rhiFailure));
        if (!descriptors.IsValid() || !externalDescriptor.IsValid() || !importedBuffer.IsValid() || !importedTexture.IsValid())
            return fail("native owner setup");
        const vanguard::i32 bufferRefs = gpu::GetRefCount(importedBuffer.GetRef());
        const vanguard::i32 textureRefs = gpu::GetRefCount(importedTexture.GetRef());
        rendering::RenderFlowResourceAllocatorConfig config;
        config.resourceDescriptors = descriptors.GetRef();
        config.descriptorCacheTarget = 2;
        config.descriptorCacheMaximum = 2;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RetainedBufferImportDesc bufferImport;
        bufferImport.token = {0x5246313142554646ull};
        bufferImport.buffer = importedBuffer.GetRef();
        bufferImport.expected = bufferDesc;
        bufferImport.initialState = gpu::ResourceState::Common;
        bufferImport.terminalState = gpu::ResourceState::Common;
        rendering::RetainedTextureImportDesc textureImport;
        textureImport.token = {0x5246313154455854ull};
        textureImport.texture = importedTexture.GetRef();
        textureImport.expected = textureDesc;
        textureImport.initialState = gpu::ResourceState::Common;
        textureImport.terminalState = gpu::ResourceState::Common;
        rendering::ImportedResourceId imports[2];
        rendering::ResourcePlanningWriter writer;
        rendering::LogicalResourceId buffer;
        rendering::LogicalResourceId texture;
        rendering::ResourceUseId uses[2];
        rendering::BufferUseDesc bufferRead;
        bufferRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        bufferRead.access = rendering::LogicalAccessIntent::Read;
        bufferRead.content = rendering::ResourceContentIntent::Preserve;
        rendering::TextureUseDesc textureRead;
        textureRead.requiredState = gpu::ResourceState::ShaderResourceGraphics;
        textureRead.access = rendering::LogicalAccessIntent::Read;
        textureRead.content = rendering::ResourceContentIntent::Preserve;
        constexpr rendering::RenderFlowNodeId node{0x811};
        constexpr rendering::GpuFlowGroupId flow{0x811};
        constexpr rendering::CommandScopeId scope{0x811};
        constexpr rendering::FlowSpaceId space{0x811};
        if (!allocator.Initialize(config, &failure) || !allocator.BeginFrame(811, {}, &failure) ||
            !allocator.RegisterImport(bufferImport, imports[0], &failure) || !allocator.RegisterImport(textureImport, imports[1], &failure) ||
            !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
            !writer.ImportBuffer({space, "rf11.rollback.buffer"}, imports[0], buffer, &failure) ||
            !writer.BeginBufferUse(buffer, bufferRead, uses[0], &failure) || !writer.EndUse(uses[0], &failure) ||
            !writer.ImportTexture({space, "rf11.rollback.texture"}, imports[1], texture, &failure) ||
            !writer.BeginTextureUse(texture, textureRead, uses[1], &failure) || !writer.EndUse(uses[1], &failure) ||
            !writer.Close(&failure) || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
            return fail("rollback planning");
        // A single descriptor slot cannot satisfy both live imported borrows.
        // At least one canonical descriptor must have been published before the
        // later physical binding exhausts shared-domain capacity.
        if (allocator.Resolve(nullptr, &failure) || allocator.GetExecutionGeneration().IsValid() ||
            allocator.GetState() != rendering::RenderFlowResourceSessionState::Idle ||
            allocator.GetStats().canonicalDescriptorPublicationMisses < 2 ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 2 ||
            allocator.GetStats().canonicalDescriptorReusableSlots != 1 ||
            gpu::GetRefCount(importedBuffer.GetRef()) != bufferRefs + 1 ||
            gpu::GetRefCount(importedTexture.GetRef()) != textureRefs)
            return fail("borrow publication rollback and retained cache ownership");
        if (!allocator.ClearPersistentCaches(&failure) || gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 1 ||
            allocator.GetStats().canonicalDescriptorLiveSlots != 0 ||
            gpu::GetRefCount(importedBuffer.GetRef()) != bufferRefs || !allocator.Shutdown(&failure) ||
            !gpu::RetireDescriptor(descriptors.GetRef(), externalDescriptor, {}, &rhiFailure) || !gpu::FlushRetiredResources(&rhiFailure) ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0)
            return fail("rollback cache reclamation");

        // Device-loss completion of a still-borrowed imported canonical slot
        // must discard its cache without a healthy fence or reusable entry.
        rendering::RenderFlowResourceAllocator lostAllocator;
        rendering::RenderFlowResourceAllocatorConfig lostConfig;
        lostConfig.resourceDescriptors = descriptors.GetRef();
        lostConfig.descriptorCacheTarget = 1;
        lostConfig.descriptorCacheMaximum = 1;
        rendering::ImportedResourceId lostImport;
        rendering::ResourcePlanningWriter lostWriter;
        rendering::LogicalResourceId lostBuffer;
        rendering::ResourceUseId lostUse;
        if (!lostAllocator.Initialize(lostConfig, &failure) || !lostAllocator.BeginFrame(812, {}, &failure) ||
            !lostAllocator.RegisterImport(bufferImport, lostImport, &failure) ||
            !lostAllocator.CreatePlanningWriter(node, flow, scope, lostWriter, &failure) ||
            !lostWriter.ImportBuffer({space, "rf11.lost.buffer"}, lostImport, lostBuffer, &failure) ||
            !lostWriter.BeginBufferUse(lostBuffer, bufferRead, lostUse, &failure) || !lostWriter.EndUse(lostUse, &failure) ||
            !lostWriter.Close(&failure) || !lostAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !lostAllocator.Resolve(nullptr, &failure) || !lostAllocator.BeginExecution(&failure))
            return fail("borrowed descriptor device-loss setup");
        const rendering::ExecutionGenerationId lostGeneration = lostAllocator.GetExecutionGeneration();
        const rendering::CommandScopeExecutionReceipt lostReceipts[] = {{scope, rendering::CommandScopeCompletionKind::UnknownDueToDeviceLoss, gpu::QueueType::Graphics, {}}};
        const rendering::TerminalExecutionReceipt lostTerminal{lostGeneration, rendering::TerminalExecutionCompletionKind::DeviceLost,
                                                               vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(lostReceipts),
                                                               rendering::TerminalJoinToken::CompletedSynchronously(lostGeneration)};
        if (!lostAllocator.Finish(lostTerminal, &failure) || lostAllocator.GetStats().canonicalDescriptorLiveSlots != 0 ||
            lostAllocator.GetState() != rendering::RenderFlowResourceSessionState::DeviceUnavailable ||
            !lostAllocator.Shutdown(&failure) || !gpu::FlushRetiredResources(&rhiFailure) ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 0 ||
            gpu::GetRefCount(importedBuffer.GetRef()) != bufferRefs)
            return fail("retained canonical device-loss cleanup");
        return true;
    }

    // RF11: the presentation transition must be owned by the compiled packet,
    // not manually issued by the caller after RenderTarget use.
    [[nodiscard]] bool RunPresentationImportExecutionTest() noexcept
    {
#if defined(_WIN32)
        rendering::RenderFlowResourceFailure failure;
        gpu::Failure rhiFailure;
        const auto fail = [&](const char* stage) noexcept
        {
            std::fprintf(stderr, "[rf11-presentation] %s (flow=%u: %s, rhi=%u: %s)\n", stage, static_cast<unsigned>(failure.code),
                         failure.message != nullptr ? failure.message : "none", static_cast<unsigned>(rhiFailure.code),
                         rhiFailure.message[0] != '\0' ? rhiFailure.message : "none");
            return false;
        };
        struct WindowOwner final
        {
            HWND handle = nullptr;
            ~WindowOwner() { if (handle != nullptr) DestroyWindow(handle); }
        } window;
        window.handle = CreateWindowExW(0, L"STATIC", L"RF11 RenderFlow present", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 200,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window.handle == nullptr)
            return fail("native Win32 test window");
        ShowWindow(window.handle, SW_SHOWNA);
        UpdateWindow(window.handle);
        gpu::SwapChainDesc swapDesc;
        swapDesc.surface = {gpu::PresentationSurfaceKind::Win32, window.handle, nullptr};
        swapDesc.width = 320;
        swapDesc.height = 180;
        swapDesc.bufferCount = 3;
        swapDesc.format = gpu::Format::B8G8R8A8UNorm;
        gpu::SwapChain swapChain(gpu::AdoptReference, gpu::CreateSwapChainWithBackBuffer(swapDesc, &rhiFailure));
        if (!swapChain.IsValid())
            return fail("real D3D12 swap chain");
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceContract contract;
        rendering::RenderNodeGraph graph;
        graph.BuildRenderFlowGroups();
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures preparationFailures;
        constexpr rendering::RenderFlowNodeId node{0x611};
        constexpr rendering::GpuFlowGroupId flow{0x611};
        constexpr rendering::CommandScopeId scope{0x611};
        constexpr rendering::FlowSpaceId space{0x611};
        constexpr rendering::ResourceContractPatchId presentationPatch{0x5246313250524553ull};
        rendering::TextureUseDesc renderTarget;
        renderTarget.requiredState = gpu::ResourceState::RenderTarget;
        renderTarget.access = rendering::LogicalAccessIntent::Write;
        renderTarget.content = rendering::ResourceContentIntent::Discard;
        if (!allocator.Initialize({}, &failure) || !graph.PrepareResourceBindings(bindings, preparationFailures) || preparationFailures.HasFailure())
            return fail("presentation contract setup");

        vanguard::u64 firstAcquisitionSerial = 0;
        vanguard::u64 contractPublicationSerial = 0;
        rendering::RenderFlowResourceAllocatorStats coldStats;
        rendering::ResourceUseId firstGenerationUse;
        const auto presentFrame = [&](const vanguard::u64 frameSerial, const bool cold) noexcept
        {
            gpu::AcquiredBackBuffer backBuffer;
            if (!gpu::AcquireBackBuffer(swapChain, backBuffer, &rhiFailure) || !backBuffer.IsValid())
                return fail("real back-buffer acquisition");
            if (!cold && backBuffer.serial == firstAcquisitionSerial)
            {
                static_cast<void>(gpu::AbandonBackBuffer(backBuffer, &rhiFailure));
                return fail("new presentation acquisition serial");
            }

            rendering::CompiledExecutionPacketView packet;
            rendering::ExecutionPacketCursor cursor;
            gpu::CommandListRef commands;
            gpu::GpuFence completion;
            bool submitted = false;
            const auto reject = [&](const char* const stage) noexcept
            {
                if (gpu::GetBoundCommandList().IsValid())
                    gpu::UnbindCommandList();
                cursor = {};
                packet = {};
                if (!submitted && commands.IsValid())
                    gpu::DiscardCommandList(commands);
                const rendering::ExecutionGenerationId active = allocator.GetExecutionGeneration();
                if (active.IsValid())
                {
                    const rendering::CommandScopeExecutionReceipt receipt{
                        scope, submitted ? rendering::CommandScopeCompletionKind::Submitted : rendering::CommandScopeCompletionKind::DiscardedBeforeSubmission,
                        gpu::QueueType::Graphics, completion};
                    const rendering::TerminalExecutionReceipt terminal{
                        active, submitted ? rendering::TerminalExecutionCompletionKind::Completed : rendering::TerminalExecutionCompletionKind::Aborted,
                        vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                        rendering::TerminalJoinToken::CompletedSynchronously(active)};
                    static_cast<void>(allocator.Finish(terminal, &failure));
                }
                else if (allocator.GetState() != rendering::RenderFlowResourceSessionState::Idle)
                    allocator.CancelBeforePublication();
                if (submitted && completion.IsValid())
                    static_cast<void>(gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure));
                static_cast<void>(gpu::AbandonBackBuffer(backBuffer, &rhiFailure));
                return fail(stage);
            };

            rendering::ImportedResourceId imported;
            rendering::ResourceUseId renderTargetUse;
            if (!allocator.BeginFrame(frameSerial, {}, &failure) ||
                !allocator.RegisterPresentationImport(backBuffer, presentationPatch, imported, &failure))
                return reject("presentation frame/import registration");
            if (cold)
            {
                rendering::ResourcePlanningWriter writer;
                rendering::LogicalResourceId target;
                if (!allocator.BeginResourceContractCompilation(contract, &failure) ||
                    !allocator.CreatePlanningWriter(node, flow, scope, writer, &failure) ||
                    !writer.ImportTexture({space, "rf11.present.backbuffer"}, imported, target, &failure) ||
                    !writer.BeginTextureUse(target, renderTarget, renderTargetUse, &failure) || !writer.EndUse(renderTargetUse, &failure) ||
                    !writer.Close(&failure) || !graph.CaptureResourceContract(contract, bindings, allocator, &failure) ||
                    !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                {
                    writer.Abandon();
                    return reject("cold presentation contract compilation");
                }
                firstGenerationUse = renderTargetUse;
            }
            else
            {
                // The contract retains the semantic use ordinal, but neither
                // the previous acquisition nor its planning generation.
                renderTargetUse = {flow, 0, imported.generation};
                if (!graph.InstantiateResourceContract(contract, bindings, allocator, &failure) ||
                    !allocator.BindResourceContract(contract, &failure))
                    return reject("warm presentation contract binding");
            }

            if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().compiledResourceActions == 0 ||
                !allocator.BeginExecution(&failure))
                return reject("presentation native Resolve");
            const rendering::ExecutionGenerationId generation = allocator.GetExecutionGeneration();
            commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246313150524553ull + frameSerial, &rhiFailure);
            rendering::CompiledExecutionRange presentRange;
            rendering::ResolvedTextureUse resolved;
            if (!commands.IsValid() || !gpu::BindCommandList(commands, &rhiFailure) || !allocator.PacketFor(node, packet, &failure) ||
                !packet.ResolveExecutionRange(0, 1, presentRange, &failure) || presentRange.endActions.count != 1)
                return reject("presentation compiled packet");
            if (!cold)
            {
                rendering::ResolvedTextureUse stale;
                if (packet.ResolveTextureUseBinding(firstGenerationUse, 1, stale, &failure) || stale.IsValid() ||
                    failure.code != rendering::RenderFlowResourceFailureCode::IncompleteExecution)
                    return reject("previous-acquisition stale use rejection");
            }
            if (!packet.OpenCursor(scope, gpu::QueueType::Graphics, cursor, &failure) ||
                !cursor.BeginTextureUse(renderTargetUse, resolved, &failure) || resolved.GetTexture() != backBuffer.texture)
                return reject("current acquired texture binding");
            gpu::RenderTargetSetup setup{};
            setup.colorTargets[0].texture = resolved.GetTexture();
            setup.colorTargetCount = 1;
            if (!gpu::SetupRenderTargets(setup, &rhiFailure) || !cursor.EndUse(renderTargetUse, &failure) ||
                resolved.IsValid() || !cursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
                return reject("RenderTarget use and compiled present transition");
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[] = {commands};
            if (!gpu::CloseAndSubmitCommandLists("RF11/RF12 RenderFlow present", {submissions, 1}, gpu::CommandListSyncType::None, completion, &rhiFailure))
                return reject("presentation submission");
            submitted = true;
            const rendering::CommandScopeExecutionReceipt receipt{
                scope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, completion};
            const rendering::TerminalExecutionReceipt terminal{
                generation, rendering::TerminalExecutionCompletionKind::Completed,
                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(&receipt, 1),
                rendering::TerminalJoinToken::CompletedSynchronously(generation)};
            if (!allocator.Finish(terminal, &failure))
                return reject("presentation ownership completion");
            packet = {};
            cursor = {};
            if (!gpu::Present(backBuffer, &rhiFailure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure))
                return reject("real swapchain Present");
            if (cold)
            {
                firstAcquisitionSerial = backBuffer.serial;
                contractPublicationSerial = contract.GetPublicationSerial();
                coldStats = allocator.GetStats();
                if (!contract.IsValid() || contractPublicationSerial == 0 || coldStats.structuralPlanBuilds != 1 ||
                    coldStats.structuralPlanMisses != 1 || coldStats.structuralPlanHits != 0)
                    return fail("cold presentation contract witness");
            }
            else
            {
                const rendering::RenderFlowResourceAllocatorStats stats = allocator.GetStats();
                if (contract.GetPublicationSerial() != contractPublicationSerial || stats.structuralPlanBuilds != 1 ||
                    stats.structuralPlanMisses != coldStats.structuralPlanMisses ||
                    stats.structuralPlanHits != coldStats.structuralPlanHits + 1 || stats.planningWriters != 0)
                    return fail("warm presentation contract hit and publication stability");
            }
            return true;
        };

        if (!presentFrame(611, true) || !presentFrame(612, false) ||
            gpu::GetSwapChainStats(swapChain).presentedFrames != 2 || !allocator.Shutdown(&failure))
            return fail("two real presentation acquisitions through cold/warm contract");
        swapChain.Reset();
        return gpu::RetireResources(&rhiFailure);
#else
        return true;
#endif
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

        // Presentation imports carry swap-chain acquisition serial ownership and
        // cannot escape as a generic TextureRef export. Use a syntactically valid
        // acquisition around a present-capable test texture; Resolve must reject
        // the ownership conversion before any presentation operation is recorded.
        {
            gpu::TextureDesc presentDesc;
            presentDesc.extent = {4, 4, 1};
            presentDesc.dimension = gpu::TextureDimension::Texture2D;
            presentDesc.format = gpu::Format::B8G8R8A8UNorm;
            presentDesc.mipCount = 1;
            presentDesc.arraySize = 1;
            presentDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::Present;
            presentDesc.initialState = gpu::ResourceState::Present;
            gpu::Texture presentTexture(gpu::AdoptReference, gpu::CreateTexture(presentDesc, {}, &rhiFailure));
            if (!presentTexture.IsValid())
                return fail("presentation-export rejection texture");
            const gpu::AcquiredBackBuffer acquisition{{0, 1}, presentTexture.GetRef(), 1, 0, 4, 4};
            rendering::RenderFlowResourceAllocator presentAllocator;
            rendering::ImportedResourceId presentImport;
            rendering::ExportSlotId presentSlot;
            rendering::ResourcePlanningWriter presentWriter;
            rendering::LogicalResourceId presentLogical;
            rendering::ResourceUseId presentUse;
            rendering::TextureUseDesc presentWrite;
            presentWrite.requiredState = gpu::ResourceState::RenderTarget;
            presentWrite.access = rendering::LogicalAccessIntent::Write;
            presentWrite.content = rendering::ResourceContentIntent::Discard;
            rendering::TerminalResourceExportDesc presentExport;
            presentExport.terminalState = gpu::ResourceState::Present;
            presentExport.terminalQueue = gpu::QueueType::Graphics;
            presentExport.readiness = rendering::ExportReadinessKind::ExplicitFenceSignal;
            constexpr rendering::RenderFlowNodeId presentNode{3};
            constexpr rendering::GpuFlowGroupId presentFlow{3};
            constexpr rendering::CommandScopeId presentScope{3};
            constexpr rendering::FlowSpaceId presentSpace{3};
            if (!presentAllocator.Initialize({}, &failure) || !presentAllocator.BeginFrame(28, {}, &failure) ||
                !presentAllocator.RegisterPresentationImport(acquisition, presentImport, &failure) || !presentAllocator.ReserveExportSlot(presentSlot, &failure) ||
                !presentAllocator.CreatePlanningWriter(presentNode, presentFlow, presentScope, presentWriter, &failure) ||
                !presentWriter.ImportTexture({presentSpace, "terminal.export.presentation"}, presentImport, presentLogical, &failure) ||
                !presentWriter.BeginTextureUse(presentLogical, presentWrite, presentUse, &failure) || !presentWriter.EndUse(presentUse, &failure) ||
                !presentWriter.RequestExport(presentLogical, presentSlot, presentExport, &failure) || !presentWriter.Close(&failure) ||
                !presentAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure))
                return fail("presentation-export rejection planning");
            if (presentAllocator.Resolve(nullptr, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::UnsupportedCapability ||
                presentAllocator.GetExecutionGeneration().IsValid() || presentAllocator.GetState() != rendering::RenderFlowResourceSessionState::Idle ||
                !presentAllocator.Shutdown(&failure))
                return fail("presentation-export rejection resolve");
        }

        // RequestExport binds a logical slot, not the allocation visible at the
        // request site. A later swap must therefore publish the final mapping.
        // Keep both candidates live together so their native identities cannot
        // collapse through same-frame dedicated reuse.
        {
            rendering::RenderFlowResourceAllocator swapAllocator;
            rendering::ExportSlotId swapSlot;
            rendering::ResourcePlanningWriter swapWriter;
            rendering::LogicalResourceId swapA;
            rendering::LogicalResourceId swapB;
            rendering::ResourceUseId swapUseA;
            rendering::ResourceUseId swapUseB;
            rendering::FrameBufferDesc swapDesc;
            swapDesc.active.size = 2048;
            swapDesc.active.usage = gpu::BufferUsage::UnorderedAccess;
            swapDesc.active.initialState = gpu::ResourceState::Common;
            swapDesc.maximumSize = swapDesc.active.size;
            rendering::BufferUseDesc swapWrite;
            swapWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            swapWrite.access = rendering::LogicalAccessIntent::Write;
            swapWrite.content = rendering::ResourceContentIntent::Discard;
            rendering::TerminalResourceExportDesc swapTerminal;
            swapTerminal.terminalState = gpu::ResourceState::Common;
            swapTerminal.terminalQueue = gpu::QueueType::Graphics;
            swapTerminal.readiness = rendering::ExportReadinessKind::ExplicitFenceSignal;
            constexpr rendering::RenderFlowNodeId swapNode{4};
            constexpr rendering::GpuFlowGroupId swapFlow{4};
            constexpr rendering::CommandScopeId swapScope{4};
            constexpr rendering::FlowSpaceId swapSpace{4};
            if (!swapAllocator.Initialize({}, &failure) || !swapAllocator.BeginFrame(29, {}, &failure) || !swapAllocator.ReserveExportSlot(swapSlot, &failure) ||
                !swapAllocator.CreatePlanningWriter(swapNode, swapFlow, swapScope, swapWriter, &failure) ||
                !swapWriter.DeclareBuffer({swapSpace, "terminal.export.swap.a"}, swapDesc, swapA, &failure) ||
                !swapWriter.DeclareBuffer({swapSpace, "terminal.export.swap.b"}, swapDesc, swapB, &failure) ||
                !swapWriter.BeginBufferUse(swapA, swapWrite, swapUseA, &failure) || !swapWriter.BeginBufferUse(swapB, swapWrite, swapUseB, &failure) ||
                !swapWriter.EndUse(swapUseA, &failure) || !swapWriter.EndUse(swapUseB, &failure) ||
                !swapWriter.RequestExport(swapA, swapSlot, swapTerminal, &failure) || !swapWriter.SwapLogicalMappings(swapA, swapB, &failure) ||
                !swapWriter.Close(&failure) || !swapAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
                !swapAllocator.Resolve(nullptr, &failure) || !swapAllocator.BeginExecution(&failure))
                return fail("post-request swap export setup");

            const rendering::ExecutionGenerationId swapGeneration = swapAllocator.GetExecutionGeneration();
            rendering::CompiledExecutionPacketView swapPacket;
            rendering::ExecutionPacketCursor swapCursor;
            rendering::ResolvedBufferUse swapResolvedA;
            rendering::ResolvedBufferUse swapResolvedB;
            gpu::CommandListRef swapCommands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x535741504558504full, &rhiFailure);
            if (!swapCommands.IsValid() || !gpu::BindCommandList(swapCommands, &rhiFailure) || !swapAllocator.PacketFor(swapNode, swapPacket, &failure) ||
                !swapPacket.OpenCursor(swapScope, gpu::QueueType::Graphics, swapCursor, &failure) ||
                !swapCursor.BeginBufferUse(swapUseA, swapResolvedA, &failure) || !swapCursor.BeginBufferUse(swapUseB, swapResolvedB, &failure) ||
                !swapResolvedA.GetBuffer().IsValid() || !swapResolvedB.GetBuffer().IsValid() || swapResolvedA.GetBuffer() == swapResolvedB.GetBuffer())
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(swapCommands);
                return fail("post-request swap export execution setup");
            }
            const gpu::BufferRef nativeA = swapResolvedA.GetBuffer();
            const gpu::BufferRef nativeB = swapResolvedB.GetBuffer();
            if (!swapCursor.EndUse(swapUseA, &failure) || !swapCursor.EndUse(swapUseB, &failure) || !swapCursor.FinalizePacket(&failure) ||
                !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(swapCommands);
                return fail("post-request swap export execution");
            }
            gpu::UnbindCommandList();
            const gpu::CommandListRef swapSubmissions[] = {swapCommands};
            gpu::GpuFence swapCompletion;
            if (!gpu::CloseAndSubmitCommandLists("resource-flow post-request swap export", {swapSubmissions, 1}, gpu::CommandListSyncType::None, swapCompletion, &rhiFailure))
                return fail("post-request swap export submission");
            const rendering::CommandScopeExecutionReceipt swapReceipts[] = {
                {swapScope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, swapCompletion}};
            const rendering::TerminalExecutionReceipt swapReceipt{swapGeneration, rendering::TerminalExecutionCompletionKind::Completed,
                                                                  vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(swapReceipts),
                                                                  rendering::TerminalJoinToken::CompletedSynchronously(swapGeneration)};
            rendering::PublishedResourceExport swappedExport;
            if (!swapAllocator.Finish(swapReceipt, &failure) || !swapAllocator.TakeExport(swapSlot, swappedExport, &failure) ||
                swappedExport.GetBuffer() != nativeB || swappedExport.GetBuffer() == nativeA ||
                !gpu::WaitForGpuFence(swapCompletion, 5'000'000'000ull, &rhiFailure))
                return fail("post-request swap final mapping");
            swappedExport.Reset();
            if (!swapAllocator.Shutdown(&failure))
                return fail("post-request swap export cleanup");
        }

        gpu::BufferDesc externalDesc;
        externalDesc.size = 1024;
        externalDesc.structureStride = 16;
        externalDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        externalDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer external(gpu::AdoptReference, gpu::CreateBuffer(externalDesc, {}, &rhiFailure));
        if (!external.IsValid())
            return fail("external creation");

        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 3, 0, visibility}, &rhiFailure));
        rendering::RenderFlowResourceAllocatorConfig allocatorConfig;
        allocatorConfig.resourceDescriptors = descriptors.GetRef();
        allocatorConfig.descriptorCacheTarget = 3;
        allocatorConfig.descriptorCacheMaximum = 3;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::ExportSlotId bufferSlot;
        rendering::ExportSlotId textureSlot;
        rendering::ExportSlotId importedSlot;
        if (!descriptors.IsValid() || !allocator.Initialize(allocatorConfig, &failure) || !allocator.BeginFrame(30, {}, &failure) || !allocator.ReserveExportSlot(bufferSlot, &failure) ||
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
        bufferDesc.active.structureStride = 4;
        bufferDesc.active.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::CopySource | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::Structured;
        bufferDesc.active.initialState = gpu::ResourceState::Common;
        bufferDesc.maximumSize = bufferDesc.active.size;
        rendering::FrameTextureDesc textureDesc;
        textureDesc.active.extent = {8, 8, 1};
        textureDesc.active.dimension = gpu::TextureDimension::Texture2D;
        textureDesc.active.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.active.usage = gpu::TextureUsage::CopyDestination | gpu::TextureUsage::CopySource | gpu::TextureUsage::ShaderResource;
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

        if (!allocator.Resolve(nullptr, &failure) || allocator.GetStats().chargedNativeBytes == 0 || allocator.GetStats().canonicalDescriptorLiveSlots != 3 ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).populated != 3 || !allocator.BeginExecution(&failure))
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
            !resolvedImported.GetShaderResourceDescriptor().IsValid() ||
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
        if (!allocator.Finish(receipt, &failure) || allocator.GetStats().chargedNativeBytes != 0 || allocator.GetStats().canonicalDescriptorLiveSlots != 1 ||
            allocator.GetStats().outstandingPublishedExports != 3)
            return fail("atomic publication");
        // Export-owned canonical bundles retire at Finish. RF11's imported
        // buffer canonical SRV remains allocator-persistent and independent of
        // those exported native owners.
        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &rhiFailure) || !gpu::RetireResources(&rhiFailure) ||
            gpu::GetDescriptorDomainStats(descriptors.GetRef()).allocated != 1 || allocator.GetStats().outstandingPublishedExports != 3)
            return fail("canonical export descriptor retirement");

        rendering::PublishedResourceExport exportedBuffer;
        rendering::PublishedResourceExport exportedTexture;
        rendering::PublishedResourceExport exportedImported;
        if (!allocator.TakeExport(textureSlot, exportedTexture, &failure) || !allocator.TakeExport(importedSlot, exportedImported, &failure) ||
            exportedTexture.GetTexture() != physicalTexture || exportedImported.GetBuffer() != external.GetRef() || exportedTexture.GetTerminalQueue() != gpu::QueueType::Graphics ||
            exportedTexture.GetReadyFence() != completion || exportedImported.GetReadyFence() != completion ||
            exportedTexture.GetTextureDesc().extent.width != textureDesc.active.extent.width || allocator.GetStats().outstandingPublishedExports != 1)
            return fail("published owners");
        rendering::PublishedResourceExport duplicateTake;
        if (allocator.TakeExport(textureSlot, duplicateTake, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity)
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
        if (!allocator.Finish(abortedReceipt, &failure) || allocator.GetStats().outstandingPublishedExports != 1 || allocator.TakeExport(abortedSlot, duplicateTake, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidOrStaleIdentity || allocator.GetStats().chargedNativeBytes == 0)
            return fail("abort publication guard");

        // Imported Texture output proof: the producer's final executable endpoint is
        // RenderTarget, while the terminal export returns the resource to the locked
        // scene-sampling state and
        // publishes the exact Graphics submission fence. This mirrors the neutral P1
        // Texture-output handoff without requiring viewport ownership in this allocator test.
        gpu::TextureDesc outputTextureDesc;
        outputTextureDesc.extent = {8, 8, 1};
        outputTextureDesc.dimension = gpu::TextureDimension::Texture2D;
        outputTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        outputTextureDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::ShaderResource;
        outputTextureDesc.initialState = gpu::ResourceState::ShaderResourceGraphics;
        gpu::Texture outputTexture(gpu::AdoptReference, gpu::CreateTexture(outputTextureDesc, {}, &rhiFailure));
        rendering::RetainedTextureImportDesc outputImport;
        outputImport.token = {0x4558505445584f55ull};
        outputImport.texture = outputTexture.GetRef();
        outputImport.expected = outputTextureDesc;
        outputImport.initialState = gpu::ResourceState::ShaderResourceGraphics;
        outputImport.terminalState = gpu::ResourceState::ShaderResourceGraphics;
        outputImport.initialQueue = gpu::QueueType::Graphics;
        outputImport.terminalQueue = gpu::QueueType::Graphics;
        outputImport.readiness = rendering::ImportReadinessKind::SameQueueContinuation;
        rendering::ImportedResourceId outputImported;
        rendering::ExportSlotId outputSlot;
        rendering::ResourcePlanningWriter outputWriter;
        rendering::LogicalResourceId outputLogical;
        rendering::ResourceUseId outputUse;
        rendering::TextureUseDesc outputWrite;
        outputWrite.requiredState = gpu::ResourceState::RenderTarget;
        outputWrite.access = rendering::LogicalAccessIntent::Write;
        outputWrite.content = rendering::ResourceContentIntent::Preserve;
        rendering::TerminalResourceExportDesc outputTerminal;
        outputTerminal.terminalState = gpu::ResourceState::ShaderResourceGraphics;
        outputTerminal.terminalQueue = gpu::QueueType::Graphics;
        outputTerminal.readiness = rendering::ExportReadinessKind::SameQueueContinuation;
        constexpr rendering::RenderFlowNodeId outputNode{7};
        constexpr rendering::GpuFlowGroupId outputFlow{7};
        constexpr rendering::CommandScopeId outputScope{7};
        if (!outputTexture.IsValid() || !allocator.BeginFrame(32, {}, &failure) || !allocator.ReserveExportSlot(outputSlot, &failure) ||
            !allocator.RegisterImport(outputImport, outputImported, &failure) || !allocator.CreatePlanningWriter(outputNode, outputFlow, outputScope, outputWriter, &failure) ||
            !outputWriter.ImportTexture({space, "terminal.export.imported-texture"}, outputImported, outputLogical, &failure) ||
            !outputWriter.BeginTextureUse(outputLogical, outputWrite, outputUse, &failure) || !outputWriter.EndUse(outputUse, &failure) ||
            !outputWriter.RequestExport(outputLogical, outputSlot, outputTerminal, &failure) || !outputWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return fail("imported texture terminal export setup");
        const rendering::ExecutionGenerationId outputGeneration = allocator.GetExecutionGeneration();
        rendering::CompiledExecutionPacketView outputPacket;
        rendering::ExecutionPacketCursor outputCursor;
        rendering::ResolvedTextureUse resolvedOutput;
        gpu::CommandListRef outputCommands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x455850544558434dull, &rhiFailure);
        if (!outputCommands.IsValid() || !gpu::BindCommandList(outputCommands, &rhiFailure) || !allocator.PacketFor(outputNode, outputPacket, &failure) ||
            !outputPacket.OpenCursor(outputScope, gpu::QueueType::Graphics, outputCursor, &failure) || !outputCursor.BeginTextureUse(outputUse, resolvedOutput, &failure) ||
            resolvedOutput.GetTexture() != outputTexture.GetRef() || !outputCursor.EndUse(outputUse, &failure) || !outputCursor.FinalizePacket(&failure) ||
            !gpu::FlushPendingBarriers(&rhiFailure))
        {
            gpu::UnbindCommandList();
            return fail("imported texture terminal export execution");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef outputSubmissions[] = {outputCommands};
        gpu::GpuFence outputCompletion;
        if (!gpu::CloseAndSubmitCommandLists("resource-flow imported texture terminal export", {outputSubmissions, 1}, gpu::CommandListSyncType::None, outputCompletion, &rhiFailure))
            return fail("imported texture terminal export submission");
        const rendering::CommandScopeExecutionReceipt outputReceipts[] = {
            {outputScope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, outputCompletion}};
        const rendering::TerminalExecutionReceipt outputReceipt{outputGeneration, rendering::TerminalExecutionCompletionKind::Completed,
                                                                vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(outputReceipts),
                                                                rendering::TerminalJoinToken::CompletedSynchronously(outputGeneration)};
        rendering::PublishedResourceExport outputExport;
        if (bufferSlot.index != outputSlot.index || bufferSlot.generation == outputSlot.generation || !allocator.Finish(outputReceipt, &failure) ||
            allocator.GetStats().outstandingPublishedExports != 2 || !allocator.TakeExport(outputSlot, outputExport, &failure) || !allocator.TakeExport(bufferSlot, exportedBuffer, &failure) ||
            outputExport.GetTexture() != outputTexture.GetRef() || outputExport.GetTerminalState() != gpu::ResourceState::ShaderResourceGraphics ||
            outputExport.GetTerminalQueue() != gpu::QueueType::Graphics || outputExport.GetReadiness() != rendering::ExportReadinessKind::SameQueueContinuation ||
            outputExport.GetReadyFence() != outputCompletion || exportedBuffer.GetBuffer() != physicalBuffer || exportedBuffer.GetTerminalState() != gpu::ResourceState::Common ||
            exportedBuffer.GetReadiness() != rendering::ExportReadinessKind::ExplicitFenceSignal || exportedBuffer.GetReadyFence() != completion ||
            exportedBuffer.GetBufferDesc().size != bufferDesc.active.size || allocator.GetStats().outstandingPublishedExports != 0)
            return fail("imported texture terminal export publication");
        outputExport.Reset();

        rendering::ExportSlotId conflictSlotA;
        rendering::ExportSlotId conflictSlotB;
        rendering::ResourcePlanningWriter conflictWriter;
        rendering::LogicalResourceId conflictBuffer;
        rendering::ResourceUseId conflictUse;
        if (!allocator.BeginFrame(33, {}, &failure) || !allocator.ReserveExportSlot(conflictSlotA, &failure) || !allocator.ReserveExportSlot(conflictSlotB, &failure) ||
            !allocator.CreatePlanningWriter(abortedNode, abortedFlow, abortedScope, conflictWriter, &failure) ||
            !conflictWriter.DeclareBuffer({space, "terminal.export.conflict"}, bufferDesc, conflictBuffer, &failure) || !conflictWriter.BeginBufferUse(conflictBuffer, bufferWrite, conflictUse, &failure) ||
            !conflictWriter.EndUse(conflictUse, &failure) || !conflictWriter.RequestExport(conflictBuffer, conflictSlotA, terminal, &failure) ||
            !conflictWriter.RequestExport(conflictBuffer, conflictSlotB, terminal, &failure) || !conflictWriter.Close(&failure) ||
            !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || allocator.Resolve(nullptr, &failure) ||
            failure.code != rendering::RenderFlowResourceFailureCode::InvalidUseOrScope || allocator.GetExecutionGeneration().IsValid())
            return fail("duplicate ownership conflict");

        if (!gpu::WaitForGpuFence(outputCompletion, 5'000'000'000ull, &rhiFailure) || !allocator.Shutdown(&failure))
            return fail("terminal export allocator shutdown");

        // Forced RF4 partial export-retirement proof through the real Finish path.
        // The first canonical handle is admitted for retirement, the second is
        // injected to fail, and no publication or pool-native ownership may be
        // committed. Once the submitted fence completes, retrying the same terminal
        // receipt retires only the remaining handle and completes export exactly once.
        {
            gpu::DescriptorDomain retryDescriptors(gpu::AdoptReference,
                                                   gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, visibility}, &rhiFailure));
            rendering::RenderFlowResourceAllocatorConfig retryConfig;
            retryConfig.resourceDescriptors = retryDescriptors.GetRef();
            retryConfig.descriptorCacheTarget = 2;
            retryConfig.descriptorCacheMaximum = 2;
            rendering::RenderFlowResourceAllocator retryAllocator;
            rendering::ExportSlotId retrySlot;
            rendering::ResourcePlanningWriter retryWriter;
            rendering::LogicalResourceId retryBuffer;
            rendering::ResourceUseId retryUse;
            rendering::FrameBufferDesc retryDesc;
            retryDesc.active.size = 4096;
            retryDesc.active.structureStride = 16;
            retryDesc.active.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
            retryDesc.active.initialState = gpu::ResourceState::Common;
            retryDesc.maximumSize = retryDesc.active.size;
            rendering::BufferUseDesc retryWrite;
            retryWrite.requiredState = gpu::ResourceState::UnorderedAccess;
            retryWrite.access = rendering::LogicalAccessIntent::Write;
            retryWrite.content = rendering::ResourceContentIntent::Discard;
            constexpr rendering::RenderFlowNodeId retryNode{8};
            constexpr rendering::GpuFlowGroupId retryFlow{8};
            constexpr rendering::CommandScopeId retryScope{8};
            if (!retryDescriptors.IsValid() || !retryAllocator.Initialize(retryConfig, &failure) || !retryAllocator.BeginFrame(34, {}, &failure) ||
                !retryAllocator.ReserveExportSlot(retrySlot, &failure) || !retryAllocator.CreatePlanningWriter(retryNode, retryFlow, retryScope, retryWriter, &failure) ||
                !retryWriter.DeclareBuffer({space, "terminal.export.partial-retire"}, retryDesc, retryBuffer, &failure) ||
                !retryWriter.BeginBufferUse(retryBuffer, retryWrite, retryUse, &failure) || !retryWriter.EndUse(retryUse, &failure) ||
                !retryWriter.RequestExport(retryBuffer, retrySlot, terminal, &failure) || !retryWriter.Close(&failure) ||
                !retryAllocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !retryAllocator.Resolve(nullptr, &failure) ||
                retryAllocator.GetStats().canonicalDescriptorLiveSlots != 2 || !retryAllocator.BeginExecution(&failure))
                return fail("partial retirement setup");

            const rendering::ExecutionGenerationId retryGeneration = retryAllocator.GetExecutionGeneration();
            rendering::CompiledExecutionPacketView retryPacket;
            rendering::ExecutionPacketCursor retryCursor;
            rendering::ResolvedBufferUse retryResolved;
            gpu::CommandListRef retryCommands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246344558505254ull, &rhiFailure);
            if (!retryCommands.IsValid() || !gpu::BindCommandList(retryCommands, &rhiFailure) || !retryAllocator.PacketFor(retryNode, retryPacket, &failure) ||
                !retryPacket.OpenCursor(retryScope, gpu::QueueType::Graphics, retryCursor, &failure) || !retryCursor.BeginBufferUse(retryUse, retryResolved, &failure) ||
                !retryResolved.GetBuffer().IsValid() || !retryCursor.EndUse(retryUse, &failure) || !retryCursor.FinalizePacket(&failure) || !gpu::FlushPendingBarriers(&rhiFailure))
            {
                gpu::UnbindCommandList();
                return fail("partial retirement execution");
            }
            gpu::UnbindCommandList();
            const gpu::CommandListRef retrySubmissions[] = {retryCommands};
            gpu::GpuFence retryCompletion;
            if (!gpu::CloseAndSubmitCommandLists("resource-flow RF4 partial export retirement", {retrySubmissions, 1}, gpu::CommandListSyncType::None, retryCompletion, &rhiFailure))
                return fail("partial retirement submission");
            const rendering::CommandScopeExecutionReceipt retryReceipts[] = {
                {retryScope, rendering::CommandScopeCompletionKind::Submitted, gpu::QueueType::Graphics, retryCompletion}};
            const auto retryTerminal = [&]() noexcept
            {
                return rendering::TerminalExecutionReceipt{retryGeneration, rendering::TerminalExecutionCompletionKind::Completed,
                                                           vanguard::containers::ArraySpan<const rendering::CommandScopeExecutionReceipt>(retryReceipts),
                                                           rendering::TerminalJoinToken::CompletedSynchronously(retryGeneration)};
            };
            const vanguard::u64 retryCharge = retryAllocator.GetStats().chargedNativeBytes;
            rendering::detail::DedicatedResourceProviderTestAccess::Set(
                retryAllocator, {rendering::detail::DedicatedResourceProviderFailurePoint::CanonicalDescriptorRetirement, 1,
                                 rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure});
            const rendering::TerminalExecutionReceipt failedTerminal = retryTerminal();
            if (retryAllocator.Finish(failedTerminal, &failure) || failure.code != rendering::RenderFlowResourceFailureCode::DeviceLostOrBackendFailure ||
                retryAllocator.GetStats().chargedNativeBytes != retryCharge || retryAllocator.GetStats().canonicalDescriptorLiveSlots != 1 ||
                retryAllocator.GetStats().outstandingPublishedExports != 0 || !retryAllocator.GetExecutionGeneration().IsValid())
                return fail("partial retirement ownership preservation");
            rendering::detail::DedicatedResourceProviderTestAccess::Clear(retryAllocator);
            if (!gpu::WaitForGpuFence(retryCompletion, 5'000'000'000ull, &rhiFailure) || !gpu::RetireResources(&rhiFailure))
                return fail("partial retirement first-handle drain");
            const rendering::TerminalExecutionReceipt recoveredTerminal = retryTerminal();
            if (!retryAllocator.Finish(recoveredTerminal, &failure) || retryAllocator.GetStats().chargedNativeBytes != 0 ||
                retryAllocator.GetStats().canonicalDescriptorLiveSlots != 0 || retryAllocator.GetStats().outstandingPublishedExports != 1)
                return fail("partial retirement retry");
            rendering::PublishedResourceExport recoveredExport;
            if (!retryAllocator.TakeExport(retrySlot, recoveredExport, &failure) || !recoveredExport.GetBuffer().IsValid() ||
                !gpu::RetireResources(&rhiFailure) || gpu::GetDescriptorDomainStats(retryDescriptors.GetRef()).allocated != 0 || !retryAllocator.Shutdown(&failure))
                return fail("partial retirement cleanup");
        }
        return true;
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
        rendering::LogicalResourceId dedicatedResource;
        rendering::ResourceUseId dedicatedUse;
        rendering::FrameBufferDesc desc;
        desc.active.size = 4096;
        desc.active.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        desc.active.initialState = gpu::ResourceState::Common;
        desc.active.keepInitialState = false;
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
            !writers[1].EndUse(uses[1], &failure) || !writers[2].DeclareBuffer({space, "native.cross.queue.dedicated"}, desc, dedicatedResource, &failure) ||
            !writers[2].BeginBufferUse(dedicatedResource, useDescs[0], dedicatedUse, &failure) || !writers[2].EndUse(dedicatedUse, &failure) ||
            !writers[0].Close(&failure) || !writers[1].Close(&failure) || !writers[2].Close(&failure) ||
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
            else
            {
                rendering::ResolvedBufferUse resolved;
                if (!cursor.BeginBufferUse(dedicatedUse, resolved, &failure) || !resolved.GetBuffer().IsValid() || resolved.GetBuffer() == external.GetRef() ||
                    !cursor.EndUse(dedicatedUse, &failure))
                {
                    gpu::UnbindCommandList();
                    return fail("zero-export dedicated use recording");
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
        if (!allocator.Finish(terminal, &failure) || !gpu::WaitForGpuFence(submissions[2].completion, 5'000'000'000ull, &rhiFailure))
            return fail("terminal join");
        if (!allocator.ClearPersistentCaches(&failure) || !gpu::FlushRetiredResources(&rhiFailure))
            return fail("zero-export dedicated retirement cleanup");
        const rendering::RenderFlowResourceAllocatorStats cleared = allocator.GetStats();
        if (cleared.chargedNativeBytes != 0 || cleared.pendingNativeDestructionResources != 0 || !allocator.Shutdown(&failure))
            return fail("zero-export dedicated retirement accounting");
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
        if (!PumpTextureUpload(throttledUploader, firstThrottleRequest, failure) || !PumpTextureUpload(throttledUploader, secondThrottleRequest, failure))
            return RegressionFailure(__LINE__);
        const rendering::TextureUploadState firstThrottleState = throttledUploader.GetState(firstThrottleRequest);
        const rendering::TextureUploadState secondThrottleState = throttledUploader.GetState(secondThrottleRequest);
        if ((firstThrottleState != rendering::TextureUploadState::Submitted && firstThrottleState != rendering::TextureUploadState::ReadyToInstall) ||
            (secondThrottleState != rendering::TextureUploadState::Submitted && secondThrottleState != rendering::TextureUploadState::ReadyToInstall))
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

    struct TextureViewportReceiptExecution
    {
        vanguard::concurrency::Atomic<bool> hold{false};
        vanguard::concurrency::Atomic<bool> entered{false};
        vanguard::concurrency::Atomic<bool> release{false};
        vanguard::concurrency::Atomic<bool> fail{true};
    };

    rendering::RenderFrameExecutionStatus ExecuteTextureViewportReceiptTick(rendering::RenderFrameTickContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameExecutionStatus ExecuteTextureViewportReceiptFrame(rendering::RenderFrameContext&, void* const userData) noexcept
    {
        auto* const execution = static_cast<TextureViewportReceiptExecution*>(userData);
        if (execution == nullptr)
            return rendering::RenderFrameExecutionStatus::Failure("texture viewport receipt execution state is missing");
        execution->entered.SetValue(true);
        while (execution->hold.GetValue() && !execution->release.GetValue())
            std::this_thread::yield();
        return execution->fail.GetValue() ? rendering::RenderFrameExecutionStatus::Failure("intentional texture viewport receipt failure")
                                          : rendering::RenderFrameExecutionStatus::Success();
    }

    [[nodiscard]] bool RunTextureOutputReceiptStateMachineProof() noexcept
    {
        constexpr rendering::RenderViewportHandle viewport{3, 7};
        constexpr vanguard::u64 revision = 11;
        rendering::RenderTextureOutputContract contract;
        contract.restingState = gpu::ResourceState::ShaderResourceGraphics;
        rendering::RenderOutputReceipt receipt;
        receipt.viewport = viewport;
        receipt.outputRevision = revision;
        receipt.producerSerial = 40;
        receipt.state = rendering::RenderOutputReceiptState::Ready;
        receipt.terminalState = gpu::ResourceState::ShaderResourceGraphics;
        receipt.terminalQueue = gpu::QueueType::Graphics;
        receipt.readiness = rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation;
        receipt.readyFence = {gpu::QueueType::Graphics, 5};
        const rendering::RenderOutputReceipt priorReady = receipt;

        rendering::detail::PrearmTextureOutputReceipt(receipt, viewport, revision, 41, contract);
        if (!rendering::detail::MatchesPendingTextureOutputReceiptEpoch(receipt, viewport, revision, 41) ||
            rendering::detail::MatchesTextureOutputReceiptEpoch(receipt, viewport, revision, 40) || receipt.readyFence.IsValid())
            return false;
        if (!rendering::detail::RestorePendingTextureOutputReceipt(receipt, viewport, revision, 41, priorReady) ||
            !rendering::detail::MatchesTextureOutputReceiptEpoch(receipt, viewport, revision, 40) || !receipt.IsReady())
            return false;

        rendering::detail::PrearmTextureOutputReceipt(receipt, viewport, revision, 42, contract);
        if (rendering::detail::RestorePendingTextureOutputReceipt(receipt, viewport, revision, 41, priorReady) ||
            !rendering::detail::FailPendingTextureOutputReceipt(receipt, viewport, revision) ||
            receipt.state != rendering::RenderOutputReceiptState::Failed || receipt.readyFence.IsValid())
            return false;

        rendering::detail::PrearmTextureOutputReceipt(receipt, viewport, revision, 43, contract);
        const gpu::GpuFence readyFence{gpu::QueueType::Graphics, 9};
        if (!rendering::detail::MatchesTextureOutputPublication(contract, gpu::ResourceState::ShaderResourceGraphics, gpu::QueueType::Graphics,
                                                                rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation, readyFence) ||
            rendering::detail::MatchesTextureOutputPublication(contract, gpu::ResourceState::RenderTarget, gpu::QueueType::Graphics,
                                                               rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation, readyFence))
            return false;
        rendering::detail::PublishReadyTextureOutputReceipt(receipt, gpu::ResourceState::ShaderResourceGraphics, gpu::QueueType::Graphics,
                                                            rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation, readyFence);
        if (!receipt.IsReady() || !rendering::detail::MatchesTextureOutputReceiptEpoch(receipt, viewport, revision, 43))
            return false;

        rendering::detail::PrearmTextureOutputReceipt(receipt, viewport, revision, 44, contract);
        return rendering::detail::MatchesPendingTextureOutputReceiptEpoch(receipt, viewport, revision, 44) &&
               !rendering::detail::MatchesTextureOutputReceiptEpoch(receipt, viewport, revision, 43);
    }

    [[nodiscard]] bool RunTextureViewportReceiptProof() noexcept
    {
        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        rendering::RenderCameraStorage cameras;
        rendering::RenderCameraFailure cameraFailure;
        TextureViewportReceiptExecution execution;
        rendering::RenderCommandSystem commands;
        rendering::RenderCommandSystemConfig commandConfig;
        commandConfig.executeFrameTick = &ExecuteTextureViewportReceiptTick;
        commandConfig.executeFrame = &ExecuteTextureViewportReceiptFrame;
        commandConfig.userData = &execution;
        rendering::RenderCommandFailure commandFailure;
        rendering::ViewportManager manager;
        rendering::ViewportFailure viewportFailure;
        if (!scenes.Initialize({}, &sceneFailure) || !cameras.Initialize(scenes, {}, &cameraFailure) ||
            !commands.Initialize(scenes, cameras, commandConfig, &commandFailure) || !manager.Initialize(commands, &viewportFailure))
            return false;

        gpu::Failure rhiFailure;
        gpu::TextureDesc textureDesc;
        textureDesc.extent = {16, 16, 1};
        textureDesc.dimension = gpu::TextureDimension::Texture2D;
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::ShaderResource;
        textureDesc.initialState = gpu::ResourceState::ShaderResourceGraphics;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {}, &rhiFailure));

        rendering::RenderViewportDesc desc;
        desc.name = "Texture receipt proof";
        desc.outputKind = rendering::RenderViewportOutputKind::Texture;
        desc.renderExtent = {16, 16};
        desc.outputExtent = {16, 16};
        desc.outputTexture = texture.GetRef();
        desc.textureOutput.restingState = gpu::ResourceState::ShaderResourceGraphics;

        gpu::TextureDesc wrongUsageDesc = textureDesc;
        wrongUsageDesc.usage = gpu::TextureUsage::ShaderResource;
        gpu::Texture wrongUsage(gpu::AdoptReference, gpu::CreateTexture(wrongUsageDesc, {}, &rhiFailure));
        rendering::RenderViewportHandle rejected;
        desc.outputTexture = wrongUsage.GetRef();
        if (!texture.IsValid() || !wrongUsage.IsValid() || manager.CreateRenderViewport(desc, rejected, &viewportFailure) ||
            viewportFailure.code != rendering::ViewportFailureCode::InvalidDescriptor)
            return false;

        gpu::TextureDesc wrongStateDesc = textureDesc;
        wrongStateDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture wrongState(gpu::AdoptReference, gpu::CreateTexture(wrongStateDesc, {}, &rhiFailure));
        desc.outputTexture = wrongState.GetRef();
        if (!wrongState.IsValid() || manager.CreateRenderViewport(desc, rejected, &viewportFailure) ||
            viewportFailure.code != rendering::ViewportFailureCode::InvalidDescriptor)
            return false;

        desc.outputTexture = texture.GetRef();
        rendering::RenderViewportHandle output;
        if (!manager.CreateRenderViewport(desc, output, &viewportFailure))
            return false;
        rendering::RenderViewport* const renderOutput = manager.Resolve(output);
        if (renderOutput == nullptr || renderOutput->GetOutputRevision() == 0 || renderOutput->GetOutputTexture() != texture.GetRef())
            return false;
        const vanguard::u64 revision = renderOutput->GetOutputRevision();
        rendering::RenderOutputReceipt queried;
        if (manager.QueryOutputReceipt(output, revision, 1, queried))
            return false;

        rendering::EngineViewportDesc engineDesc;
        engineDesc.contextName = "TextureReceiptProof";
        engineDesc.output = output;
        engineDesc.presentByDefault = false;
        rendering::EngineViewportHandle engineHandle;
        if (!manager.CreateEngineViewport(engineDesc, engineHandle, &viewportFailure))
            return false;
        rendering::EngineViewport* const engine = manager.Resolve(engineHandle);
        if (engine == nullptr)
            return false;

        rendering::RenderFrameSetup frameSetup;
        frameSetup.purpose = rendering::RenderFramePurpose::Blank;
        frameSetup.present = false;
        execution.hold.SetValue(true);
        execution.release.SetValue(false);
        execution.entered.SetValue(false);
        execution.fail.SetValue(true);
        rendering::RenderFrameInfo first;
        if (!engine->BeginFrame(frameSetup, first, &viewportFailure))
            return false;
        const vanguard::u64 firstSerial = first.GetSerial();
        rendering::RenderFrameSubmission firstSubmission;
        if (!engine->SubmitFrame(first, firstSubmission, &viewportFailure))
            return false;
        const bool firstPending = manager.QueryOutputReceipt(output, revision, firstSerial, queried) &&
                                  queried.state == rendering::RenderOutputReceiptState::Pending && !queried.readyFence.IsValid() &&
                                  !manager.QueryOutputReceipt(output, revision, firstSerial + 1, queried) &&
                                  !manager.QueryOutputReceipt(output, revision + 1, firstSerial, queried);
        execution.release.SetValue(true);
        const bool firstFailedFlush = !engine->FlushFrame(&viewportFailure) && viewportFailure.code == rendering::ViewportFailureCode::SubmissionFailure;
        const bool firstFailedReceipt = manager.QueryOutputReceipt(output, revision, firstSerial, queried) &&
                                        queried.state == rendering::RenderOutputReceiptState::Failed && !queried.readyFence.IsValid();
        if (!firstPending || !firstFailedFlush || !firstFailedReceipt)
            return false;

        if (!commands.Shutdown(&commandFailure))
            return false;
        rendering::RenderFrameInfo rejectedFrame;
        if (!engine->BeginFrame(frameSetup, rejectedFrame, &viewportFailure))
        {
            static_cast<void>(commands.Initialize(scenes, cameras, commandConfig, &commandFailure));
            return false;
        }
        const vanguard::u64 rejectedSerial = rejectedFrame.GetSerial();
        rendering::RenderFrameSubmission rejectedSubmission;
        const bool rejectedSynchronously = !engine->SubmitFrame(rejectedFrame, rejectedSubmission, &viewportFailure) &&
                                           viewportFailure.code == rendering::ViewportFailureCode::SubmissionFailure;
        const bool rollbackPreservedPrevious = manager.QueryOutputReceipt(output, revision, firstSerial, queried) &&
                                               queried.state == rendering::RenderOutputReceiptState::Failed &&
                                               !manager.QueryOutputReceipt(output, revision, rejectedSerial, queried);
        const bool rejectedFrameAbandoned = engine->AbandonFrame(rejectedFrame, &viewportFailure);
        const bool commandsReinitialized = commands.Initialize(scenes, cameras, commandConfig, &commandFailure);
        if (!rejectedFrameAbandoned || !rejectedSynchronously || !rollbackPreservedPrevious || !commandsReinitialized)
            return false;

        execution.hold.SetValue(true);
        execution.release.SetValue(false);
        execution.entered.SetValue(false);
        rendering::RenderFrameInfo next;
        if (!engine->BeginFrame(frameSetup, next, &viewportFailure))
            return false;
        const vanguard::u64 nextSerial = next.GetSerial();
        rendering::RenderFrameSubmission nextSubmission;
        if (!engine->SubmitFrame(next, nextSubmission, &viewportFailure))
            return false;
        const bool nextPendingSuppressesOld = manager.QueryOutputReceipt(output, revision, nextSerial, queried) &&
                                              queried.state == rendering::RenderOutputReceiptState::Pending &&
                                              !manager.QueryOutputReceipt(output, revision, firstSerial, queried);
        rendering::RenderOutputAcquisition lostAcquisition{output, renderOutput->GetOutputTexture(), {}, revision};
        manager.DeviceLostOutput(lostAcquisition);
        const bool deviceLossFailedReceipt = !lostAcquisition.IsValid() && renderOutput->GetState() == rendering::RenderViewportState::Failed &&
                                             manager.QueryOutputReceipt(output, revision, nextSerial, queried) &&
                                             queried.state == rendering::RenderOutputReceiptState::Failed && !queried.readyFence.IsValid();
        execution.release.SetValue(true);
        const bool nextFailedFlush = !engine->FlushFrame(&viewportFailure) && viewportFailure.code == rendering::ViewportFailureCode::SubmissionFailure;
        if (!nextPendingSuppressesOld || !deviceLossFailedReceipt || !nextFailedFlush)
            return false;

        return manager.DestroyEngineViewport(engineHandle, &viewportFailure) && manager.DestroyRenderViewport(output, &viewportFailure) &&
               manager.Shutdown(&viewportFailure) && commands.Shutdown(&commandFailure) && cameras.Shutdown(&cameraFailure) && scenes.Shutdown(&sceneFailure);
    }

    [[nodiscard]] bool RunTests(gpu::Failure& failure) noexcept
    {
        if (!RunTextureOutputReceiptStateMachineProof() || !RunTextureViewportReceiptProof())
            return false;
        std::printf("Vanguard Texture viewport receipt state-machine proof passed.\n");
        if (!RunDedicatedResourceProviderSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow dedicated-resource provider smoke test passed.\n");
        if (!RunRF13PhysicalProgramCacheProof())
            return false;
        std::printf("Vanguard RF13 bounded contract physical-program cache proof passed.\n");
        if (!RunResourceContractDynamicPatchProof())
            return false;
        std::printf("Vanguard RF12 dynamic import/fence/export patch proof passed.\n");
        if (!RunRF12RevisionTopologyContractProof())
            return false;
        std::printf("Vanguard RF12 cache-revision swap/queue topology contract proof passed.\n");
        if (!RunRF12ContractDomainEpochProof())
            return false;
        std::printf("Vanguard RF12 descriptor-domain contract scope proof passed.\n");
        if (!RunRF12ContractIncarnationProofSubprocess())
            return false;
        std::printf("Vanguard RF12 RHI-incarnation contract scope proof passed.\n");
        if (!RunExplicitBufferNoOpTransitionBoundaryTest())
            return false;
        std::printf("Vanguard Resource Flow no-op transition boundary test passed.\n");
        if (!RunExplicitTextureSubresourceStateTest())
            return false;
        std::printf("Vanguard Resource Flow explicit texture subresource state test passed.\n");
        if (!RunPartialTextureUavBarrierRejectionTest())
            return false;
        std::printf("Vanguard Resource Flow partial texture UAV barrier guard test passed.\n");
        if (!RunExplicitTextureCoalescedTransitionStateTest())
            return false;
        std::printf("Vanguard Resource Flow explicit texture coalesced-transition state test passed.\n");
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
        if (!RunRFEX1DirectBindingTest())
            return false;
        std::printf("Vanguard RF-EX1 direct binding and range-witness test passed.\n");
        if (!RunPlacedResourceExecutionSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow placed-resource execution test passed.\n");
        if (!RunTypedClearSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow typed clear smoke test passed.\n");
        if (!RunAutomaticExplicitStateParityTest())
            return false;
        std::printf("Vanguard Resource Flow D3D12 automatic/explicit state parity passed.\n");
        if (!RunRenderFlowDescriptorPublicationSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow descriptor publication smoke test passed.\n");
        if (!RunRetainedImportSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow retained import smoke test passed.\n");
        if (!RunRetainedImportRollbackAndDeviceLossTest())
            return false;
        std::printf("Vanguard Resource Flow retained import rollback and device-loss test passed.\n");
        if (!RunRF11RetainedImportIncarnationProofSubprocess())
            return false;
        std::printf("Vanguard Resource Flow retained import RHI incarnation test passed.\n");
        if (!RunPresentationImportExecutionTest())
            return false;
        std::printf("Vanguard Resource Flow D3D12 presentation import execution test passed.\n");
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

        const vanguard::filesystem::AbsolutePath testDirectory = vanguard::tests::ScratchDirectory("geometry_allocator_tests").AddDirPath("vanguard_mesh_lod_geometryUploader_tests");
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
        // Exact native-release completion now remains part of pending-retirement
        // capacity. Drop the test's extra RHI references before flushing so the
        // manager can observe those retired installations as physically released.
        firstTexture.Reset();
        secondTexture.Reset();
        thirdTexture.Reset();
        fourthTexture.Reset();
        gpu::ResidencyFenceSet textureRetirementFences;
        if (!SubmitFences(0x5445585245545200ull, textureRetirementFences, failure) || !textureResidency.SealRetirements(textureRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(textureRetirementFences, &residencyLifetimeFailure) || !WaitFences(textureRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            !gpu::FlushRetiredResources(&failure) ||
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
        const bool streamedBatchSubmitted = SubmitTextureBatch(streamedTableCompletion, 1);
        const bool streamedInfoAvailable = streamedBatchSubmitted && streamedTableCompletion.IsValid() && textureResidency.GetInfo(streamedTextureHandle, textureInfo, &textureResidencyFailure);
        const bool streamedInfoValid = streamedInfoAvailable && textureInfo.state == rendering::TextureResidencyState::BindlessReady && textureInfo.texture.IsValid() &&
                                       textureInfo.firstResidentMip == 0 && textureInfo.residentMipCount == 1 && textureInfo.source.IsValid() &&
                                       textureInfo.source.GetGeneration() == textureHandle.GetGeneration() &&
                                       textureInfo.contentFingerprint == static_cast<textures::TextureResourceObject*>(textureHandle.Get())->GetMetadata().GetContentFingerprint();
        const bool streamedRetired = streamedInfoValid && textureResidency.Retire(streamedTextureHandle, &textureResidencyFailure);
        if (!streamedRetired)
        {
            std::fprintf(stderr,
                         "[geometryAllocatorTests] streamed texture retirement setup failed: batch=%u completion=%u info=%u state=%u texture=%u mip=%u count=%u source=%u sourceGen=%u handleGen=%u code=%u message=%s\n",
                         streamedBatchSubmitted ? 1u : 0u, streamedTableCompletion.IsValid() ? 1u : 0u, streamedInfoAvailable ? 1u : 0u,
                         static_cast<unsigned>(textureInfo.state), textureInfo.texture.IsValid() ? 1u : 0u, textureInfo.firstResidentMip, textureInfo.residentMipCount,
                         textureInfo.source.IsValid() ? 1u : 0u, textureInfo.source.GetGeneration(), textureHandle.GetGeneration(), static_cast<unsigned>(textureResidencyFailure.code),
                         textureResidencyFailure.message != nullptr ? textureResidencyFailure.message : "");
            return TextureTestFailure(__LINE__);
        }
        gpu::ResidencyFenceSet streamedRetirementFences;
        if (!SubmitFences(0x545853545245414dull, streamedRetirementFences, failure) || !textureResidency.SealRetirements(streamedRetirementFences, &textureResidencyFailure) ||
            !residencyLifetime.SealRetirements(streamedRetirementFences, &residencyLifetimeFailure) || !WaitFences(streamedRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            !gpu::FlushRetiredResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1)
            return TextureTestFailure(__LINE__);
        rendering::TextureGenerationRetirement streamedRetirementEvidence[2]{};
        if (textureResidency.PeekCompletedGenerationRetirements(streamedRetirementEvidence, &textureResidencyFailure) != 1 ||
            streamedRetirementEvidence[0].resourcePath != textureHandle.GetPath() ||
            streamedRetirementEvidence[0].resourceGeneration != textureHandle.GetGeneration() || streamedRetirementEvidence[0].retirementId == 0 ||
            !textureResidency.AcknowledgeGenerationRetirement(streamedRetirementEvidence[0].retirementId, &textureResidencyFailure) ||
            textureResidency.GetStats().pendingGenerationRetirements != 0)
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
            gpu::Texture forgedTransitionTexture(gpu::AdoptReference, gpu::CreateTexture(residentTextureDesc, {}, &failure));
            rendering::TextureInstallationTicket forgedTransitionTicket;
            rendering::TextureInstallationDesc forgedTransitionInstallation;
            forgedTransitionInstallation.texture = forgedTransitionTexture.GetRef();
            forgedTransitionInstallation.firstResidentMip = 2;
            forgedTransitionInstallation.residentMipCount = 1;
            forgedTransitionInstallation.source = transitionTextureHandle.ToWeak();
            forgedTransitionInstallation.contentFingerprint = static_cast<textures::TextureResourceObject*>(transitionTextureHandle.Get())->GetMetadata().GetContentFingerprint();
            if (!forgedTransitionTexture || textureResidency.Install(transitionHandle, forgedTransitionInstallation, forgedTransitionTicket, &textureResidencyFailure) ||
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
            !WaitFences(completedTransitionCutover, failure) || !gpu::RetireResources(&failure) || !gpu::FlushRetiredResources(&failure))
            return TextureTestFailure(__LINE__);
        static_cast<void>(textureResidency.CollectRetirements(&textureResidencyFailure));
        if (textureResidencyFailure.code != rendering::TextureResidencyFailureCode::None)
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
            !gpu::FlushRetiredResources(&failure) ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 1 || textureResidency.CollectRetirements(&textureResidencyFailure) != 1)
            return TextureTestFailure(__LINE__);
        rendering::TextureGenerationRetirement transitionRetirementEvidence[2]{};
        if (textureResidency.PeekCompletedGenerationRetirements(transitionRetirementEvidence, &textureResidencyFailure) != 1 ||
            transitionRetirementEvidence[0].resourcePath != transitionTextureHandle.GetPath() ||
            transitionRetirementEvidence[0].resourceGeneration != transitionTextureHandle.GetGeneration() || transitionRetirementEvidence[0].retirementId == 0 ||
            !textureResidency.AcknowledgeGenerationRetirement(transitionRetirementEvidence[0].retirementId, &textureResidencyFailure) ||
            textureResidency.GetStats().pendingGenerationRetirements != 0 || !textureResidency.Shutdown(&textureResidencyFailure))
            return TextureTestFailure(__LINE__);
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
        const bool closureWasInstallationPending = closureTextureInfo.state == rendering::TextureRuntimeState::InstallationPending;
        const bool materialResolverProof =
            vanguard::rendering::tests::RunMaterialResourceResolverProof(materialResources, textureRuntime, gpuSceneRuntime, runtimeCommands, runtimeScenes,
                                                                        meshHandle, runtimeTextureDescriptors, runtimeSamplerDescriptors);
        rendering::RenderCommandFrameTickResult closureTextureTickResult;
        if (!closureWasInstallationPending || !materialResolverProof ||
            !textureRuntime.StageGpuSceneContribution(gpuSceneRuntime, &textureRuntimeFailure) ||
            !runtimeCommands.FrameTick(runtimeScenes.GetFramePipelineScenes(), 6, closureTextureTickResult, &runtimeCommandFailure) ||
            !runtimeCommands.FlushPreviousFrameProcessing(&runtimeCommandFailure) ||
            !gpuSceneRuntime.ResolveContributions(&gpuSceneRuntimeFailure) || runtimeCommands.ConsumeExecutionFailure(runtimeCommandFailure) ||
            gpuSceneRuntime.ConsumePublicationFailure(gpuSceneRuntimeFailure) || !textureRuntime.Tick(&textureRuntimeFailure))
            return TextureTestFailure(__LINE__);
        const bool closureTextureInfoAvailable =
            textureRuntime.GetInfo(closureTextureDemand.GetResidency(), closureTextureInfo, &textureRuntimeFailure);
        if (!closureTextureInfoAvailable || closureTextureInfo.state != rendering::TextureRuntimeState::BindlessReady)
            return TextureTestFailure(__LINE__);
        const rendering::GpuSceneRuntimeStats gpuSceneRuntimeStats = gpuSceneRuntime.GetStats();
        if (gpuSceneRuntimeStats.publicationTicks != 18 || gpuSceneRuntimeStats.submittedBatches != 13 || gpuSceneRuntimeStats.stagedContributions != 6 || gpuSceneRuntimeStats.acceptedContributions != 6 ||
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
            !gpuSceneRuntime.GetLifetime().SealRetirements(runtimeRetirementFences, &runtimeLifetimeFailure) || !WaitFences(runtimeRetirementFences, failure) || !gpu::RetireResources(&failure) ||
            !gpu::FlushRetiredResources(&failure))
            return TextureTestFailure(__LINE__);
        const vanguard::u32 collectedSceneAllocations = gpuSceneRuntime.GetLifetime().Collect(&runtimeLifetimeFailure);
        const vanguard::u32 collectedTextures = textureRuntime.CollectRetirements(&textureRuntimeFailure);
        if (collectedSceneAllocations != 8 || collectedTextures != 2 || !textureRuntime.Tick(&textureRuntimeFailure))
        {
            std::fprintf(stderr, "[material3C2] retirement collection scene=%u textures=%u\n", collectedSceneAllocations, collectedTextures);
            return TextureTestFailure(__LINE__);
        }
        rendering::TextureGenerationRetirement runtimeRetirementEvidence[2]{};
        rendering::TextureResidencyFailure runtimeRetirementFailure;
        const vanguard::u32 runtimeRetirementCount = textureRuntime.GetResidencyManager().PeekCompletedGenerationRetirements(
            runtimeRetirementEvidence, &runtimeRetirementFailure);
        bool sawRuntimeTextureRetirement = false;
        bool sawClosureTextureRetirement = false;
        for (vanguard::u32 index = 0; index < runtimeRetirementCount; ++index)
        {
            const rendering::TextureGenerationRetirement& retirement = runtimeRetirementEvidence[index];
            sawRuntimeTextureRetirement |= retirement.resourcePath == textureHandle.GetPath() &&
                                          retirement.resourceGeneration == textureHandle.GetGeneration();
            sawClosureTextureRetirement |= retirement.resourcePath == transitionTextureHandle.GetPath() &&
                                          retirement.resourceGeneration == transitionTextureHandle.GetGeneration();
            if (!textureRuntime.GetResidencyManager().AcknowledgeGenerationRetirement(retirement.retirementId, &runtimeRetirementFailure))
                return TextureTestFailure(__LINE__);
        }
        if (runtimeRetirementCount != 2 || !sawRuntimeTextureRetirement || !sawClosureTextureRetirement ||
            textureRuntime.GetStats().residency.pendingGenerationRetirements != 0)
            return TextureTestFailure(__LINE__);
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
        rendering::RenderSceneHandle abandonedScene;
        rendering::RenderSceneDesc abandonedSceneDesc;
        abandonedSceneDesc.name = "device-loss live scene";
        rendering::GpuSceneAllocation abandonedContributionAllocation;
        rendering::GpuSceneUploadRequest abandonedContributionRequest;
        FailingContributionRetry abandonedContributionRetry;
        const rendering::GpuSceneContributionToken abandonedContributionToken{0x50434142414e444full, 1};
        if (!abandonedGpuScene.Initialize(runtimeScenes, gpuSceneRuntimeConfig, &gpuSceneRuntimeFailure) ||
            !abandonedGpuScene.GetLifetime().Allocate<rendering::GpuInstance>(1, abandonedContributionAllocation, &runtimeLifetimeFailure))
            return TextureTestFailure(__LINE__);
        abandonedContributionRequest = {abandonedContributionAllocation, 0, 1};
        const rendering::GpuSceneContributionDesc abandonedContribution{
            &abandonedContributionRetry, abandonedContributionToken, {&abandonedContributionRequest, 1},
            &IgnoreContributionWrite, &IgnoreContributionAccept, &FailContributionRetry};
        if (!abandonedGpuScene.StageContribution(abandonedContribution, &gpuSceneRuntimeFailure) ||
            abandonedGpuScene.ResolveContributions(&gpuSceneRuntimeFailure) ||
            gpuSceneRuntimeFailure.code != rendering::GpuSceneRuntimeFailureCode::ContributionFailure ||
            !gpuSceneRuntimeFailure.requiresRendererAbandonment || abandonedContributionRetry.retryCalls != 1 ||
            !runtimeScenes.CreateScene(abandonedSceneDesc, abandonedScene, &runtimeSceneFailure) ||
            !abandonedTextureRuntime.Initialize(abandonedGpuScene.GetLifetime(), runtimeTextureDescriptors, textureRuntimeConfig, &textureRuntimeFailure) ||
            !abandonedTextureRuntime.RequestTexture(transitionTextureHandle, abandonedTextureDemand, &textureRuntimeFailure) || !abandonedTextureDemand.IsValid() ||
            !abandonedTextureRuntime.AbandonDevice(&textureRuntimeFailure) || abandonedTextureDemand.IsValid() || !abandonedGpuScene.AbandonDevice(&gpuSceneRuntimeFailure) ||
            !runtimeScenes.DestroyScene(abandonedScene, &runtimeSceneFailure))
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
        residencyConfig.maximumResidencyRecords = 3;
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

        const auto publishReplacementMesh = [&](resources::ResourceHandle& replacementHandle) noexcept
        {
            resources::ResourceRequest replacementRequest = registry.RequestReplacement(meshReference);
            meshes::MeshPageSource replacementPageSource;
            meshes::MeshResourceObject* replacementResource = AllocateResource<meshes::MeshResourceObject>();
            if (replacementResource == nullptr || replacementPageSource.OpenLoose(meshPath) != meshes::Result::Success ||
                replacementResource->Open(std::move(replacementPageSource), {&materialHandle, 1}) != meshes::Result::Success ||
                !registry.Publish(replacementRequest, replacementResource))
            {
                DeleteResource(replacementResource);
                return false;
            }
            replacementRequest.Wait();
            replacementHandle = replacementRequest.Acquire();
            return replacementHandle.IsValid();
        };
        resources::ResourceHandle middleGenerationMesh;
        resources::ResourceHandle headGenerationMesh;
        rendering::MeshDemandHandle middleGenerationDemand;
        rendering::MeshDemandHandle headGenerationDemand;
        if (!publishReplacementMesh(middleGenerationMesh) || middleGenerationMesh.GetGeneration() == meshHandle.GetGeneration() ||
            !residency.RequestMesh(middleGenerationMesh, middleGenerationDemand, &residencyFailure) ||
            !publishReplacementMesh(headGenerationMesh) || headGenerationMesh.GetGeneration() == middleGenerationMesh.GetGeneration() ||
            !residency.RequestMesh(headGenerationMesh, headGenerationDemand, &residencyFailure) ||
            residency.GetStats().residencyRecords != 3 || residency.GetStats().activeResidencyRecords != 3)
            return TextureTestFailure(__LINE__);

        const rendering::MeshResidencyHandle middleGenerationResidency = middleGenerationDemand.GetResidency();
        const rendering::MeshResidencyHandle headGenerationResidency = headGenerationDemand.GetResidency();
        middleGenerationDemand.Reset();
        rendering::MeshDemandHandle oldGenerationAfterMiddleUnlink;
        if (!residency.RequestMesh(meshHandle, oldGenerationAfterMiddleUnlink, &residencyFailure) ||
            oldGenerationAfterMiddleUnlink.GetResidency() != residencyHandle || residency.GetStats().residencyRecords != 2)
            return TextureTestFailure(__LINE__);
        oldGenerationAfterMiddleUnlink.Reset();

        headGenerationDemand.Reset();
        rendering::MeshDemandHandle oldGenerationAfterHeadUnlink;
        if (!residency.RequestMesh(meshHandle, oldGenerationAfterHeadUnlink, &residencyFailure) ||
            oldGenerationAfterHeadUnlink.GetResidency() != residencyHandle || residency.GetStats().residencyRecords != 1)
            return TextureTestFailure(__LINE__);
        oldGenerationAfterHeadUnlink.Reset();

        rendering::MeshDemandHandle recycledGenerationDemand;
        if (!residency.RequestMesh(headGenerationMesh, recycledGenerationDemand, &residencyFailure))
            return TextureTestFailure(__LINE__);
        const rendering::MeshResidencyHandle recycledGenerationResidency = recycledGenerationDemand.GetResidency();
        if (recycledGenerationResidency.index != headGenerationResidency.index || recycledGenerationResidency.generation == headGenerationResidency.generation ||
            residency.GetInfo(middleGenerationResidency, residencyInfo, &residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::InvalidArgument ||
            residency.GetInfo(headGenerationResidency, residencyInfo, &residencyFailure) || residencyFailure.code != rendering::MeshResidencyFailureCode::InvalidArgument)
            return TextureTestFailure(__LINE__);
        recycledGenerationDemand.Reset();
        middleGenerationMesh.Reset();
        headGenerationMesh.Reset();
        if (residency.GetStats().residencyRecords != 1 || residency.GetStats().activeResidencyRecords != 1)
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
        const rendering::MeshResidencyStats progressVisitStatsBefore = residency.GetStats();
        if (!residency.Tick(&residencyFailure) || !residency.GetInfo(residencyHandle, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MeshResidencyState::Retiring ||
            residency.GetStats().progressRecordVisits != progressVisitStatsBefore.progressRecordVisits + 2u || residency.GetStats().activeResidencyRecords != 0 ||
            residency.GetStats().retiringResidencyRecords != 1)
            return TextureTestFailure(__LINE__);
        gpu::ResidencyFenceSet residencyRetirementFences;
        const rendering::MeshResidencyStats retirementVisitStatsBefore = residency.GetStats();
        if (!SubmitFences(0x4d45534852455300ull, residencyRetirementFences, failure) || !residency.SealRetirements(residencyRetirementFences, &residencyFailure) ||
            !residencyLifetime.SealRetirements(residencyRetirementFences, &residencyLifetimeFailure) || !WaitFences(residencyRetirementFences, failure) || residency.CollectRetirements(&residencyFailure) != 1 ||
            residencyLifetime.Collect(&residencyLifetimeFailure) != 2 || residency.GetStats().retirementRecordVisits != retirementVisitStatsBefore.retirementRecordVisits + 1u ||
            residency.GetStats().retiringResidencyRecords != 0)
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

    [[nodiscard]] bool RunViewportOutputProof() noexcept
    {
        if (!RunTextureOutputReceiptStateMachineProof() || !RunTextureViewportReceiptProof())
            return false;
        std::printf("Vanguard Texture viewport receipt state-machine proof passed.\n");
        if (!RunDedicatedResourceFailureAndBudgetTest())
            return false;
        std::printf("Vanguard Resource Flow provider rollback and hard-budget test passed.\n");
        if (!RunExplicitBufferNoOpTransitionBoundaryTest())
            return false;
        std::printf("Vanguard Resource Flow no-op transition boundary test passed.\n");
        if (!RunTerminalExportSmokeTest())
            return false;
        std::printf("Vanguard Resource Flow terminal export smoke test passed.\n");
        return true;
    }
} // namespace

int main(const int argumentCount, const char* const* const arguments)
{
    if (!vanguard::memory::Initialize() || !vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "geometryAllocatorTests") || !vanguard::containers::Initialize() ||
        !vanguard::io::Initialize())
        return 1;
    const vanguard::filesystem::AbsolutePath root = vanguard::tests::ScratchDirectory("geometry_allocator_tests");
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

    if (argumentCount == 2 && arguments != nullptr && std::strcmp(arguments[1], "--rf2-rhi-uninitialized-shutdown-proof") == 0)
        RunRhiUninitializedShutdownDestructorProof();
    if (argumentCount == 2 && arguments != nullptr && std::strcmp(arguments[1], "--rf11-retained-incarnation-proof") == 0)
        RunRF11RetainedImportIncarnationProof();
    if (argumentCount == 2 && arguments != nullptr && std::strcmp(arguments[1], "--rf12-contract-incarnation-proof") == 0)
        RunRF12ContractIncarnationProof();

    const bool viewportOutputProof = argumentCount == 2 && arguments != nullptr &&
                                     std::strcmp(arguments[1], "--viewport-output-proof") == 0;
    const bool passed = viewportOutputProof ? RunViewportOutputProof() : RunTests(failure);
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
