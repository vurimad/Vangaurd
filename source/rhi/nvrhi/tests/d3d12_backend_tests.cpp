#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/backend/resource_lifetime.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/rendering/pipeline_cache.hpp>

#include <cstdio>
#include <d3dcompiler.h>
#include <thread>
#include <Windows.h>

namespace
{
    namespace gpu = vanguard::rhi;

    struct FenceHarness
    {
        vanguard::concurrency::Atomic<vanguard::u64> completed[3] = {vanguard::concurrency::Atomic<vanguard::u64>(0),
                                                                     vanguard::concurrency::Atomic<vanguard::u64>(0),
                                                                     vanguard::concurrency::Atomic<vanguard::u64>(0)};
    };

    struct DestructionHarness
    {
        vanguard::concurrency::Atomic<vanguard::u32> destroyed{0};
    };

    struct DependencyHarness
    {
        gpu::backend::ResourceLifetimeManager* lifetime = nullptr;
        gpu::ResourceRef dependency{};
        DestructionHarness* destructions = nullptr;
    };

    struct CompiledShader
    {
        ID3DBlob* bytecode = nullptr;
        ~CompiledShader()
        {
            if (bytecode != nullptr)
                bytecode->Release();
        }
        CompiledShader() = default;
        CompiledShader(const CompiledShader&) = delete;
        CompiledShader& operator=(const CompiledShader&) = delete;
    };

    [[nodiscard]] bool CompileShader(const char* source, const char* target, CompiledShader& output) noexcept
    {
        using CompileFunction = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                                 ID3DBlob**, ID3DBlob**);
        static HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        if (module == nullptr)
            return false;
        const auto compile = reinterpret_cast<CompileFunction>(GetProcAddress(module, "D3DCompile"));
        ID3DBlob* errors = nullptr;
        const SIZE_T sourceSize = [](const char* text) noexcept
        {
            SIZE_T size = 0;
            while (text[size] != '\0')
                ++size;
            return size;
        }(source);
        const HRESULT result = compile != nullptr ? compile(source, sourceSize, "rhi_pipeline_test", nullptr, nullptr, "main", target,
                                                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &output.bytecode, &errors)
                                                  : E_NOINTERFACE;
        if (errors != nullptr)
            errors->Release();
        return SUCCEEDED(result) && output.bytecode != nullptr;
    }

    [[nodiscard]] bool IsFenceComplete(void* const context, const gpu::QueueType queue, const vanguard::u64 value) noexcept
    {
        auto& harness = *static_cast<FenceHarness*>(context);
        return harness.completed[static_cast<vanguard::u32>(queue)].GetValue() >= value;
    }

    void DestroyResource(void* const context, gpu::ResourceRef, void*) noexcept
    {
        auto& harness = *static_cast<DestructionHarness*>(context);
        static_cast<void>(harness.destroyed.Increment());
    }

    void DestroyDependentResource(void* const context, gpu::ResourceRef, void*) noexcept
    {
        auto& harness = *static_cast<DependencyHarness*>(context);
        static_cast<void>(harness.destructions->destroyed.Increment());
        static_cast<void>(harness.lifetime->Release(harness.dependency));
    }

    [[nodiscard]] gpu::backend::ResourceLifetimeConfig SmallLifetimeConfig() noexcept
    {
        gpu::backend::ResourceLifetimeConfig config{};
        config.textureCapacity = 8;
        config.bufferCapacity = 8;
        config.heapCapacity = 4;
        config.samplerStateCapacity = 4;
        config.shaderCapacity = 4;
        config.pipelineCapacity = 4;
        config.bindingLayoutCapacity = 4;
        config.accelerationStructureCapacity = 4;
        config.swapChainCapacity = 2;
        config.commandListCapacity = 8;
        config.retirementBucketCount = 8;
        return config;
    }

    [[nodiscard]] bool TestResourceLifetimeManager() noexcept
    {
        FenceHarness fences{};
        DestructionHarness destructions{};
        gpu::backend::ResourceLifetimeManager lifetime;
        if (!lifetime.Initialize(SmallLifetimeConfig(), &IsFenceComplete, &fences))
            return false;

        const gpu::ResourceRef first = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!first || !lifetime.IsValid(first) || lifetime.GetRefCount(first) != 1 || !lifetime.AddRef(first) ||
            lifetime.GetRefCount(first) != 2 || lifetime.Release(first) != 1 || lifetime.Release(first) != 0 || lifetime.IsValid(first))
            return false;

        lifetime.SealRetirementEpoch({5, 0, 0});
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 0)
            return false;
        fences.completed[0].SetValue(5);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 1)
            return false;

        const gpu::ResourceRef reused = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!reused || reused.Index() != first.Index() || reused.Generation() == first.Generation() || lifetime.AddRef(first))
            return false;

        vanguard::concurrency::Atomic<vanguard::u32> stressFailures{0};
        constexpr vanguard::u32 ThreadCount = 8;
        constexpr vanguard::u32 IterationCount = 20000;
        std::thread workers[ThreadCount];
        for (vanguard::u32 threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
        {
            workers[threadIndex] = std::thread(
                [&]() noexcept
                {
                    for (vanguard::u32 iteration = 0; iteration < IterationCount; ++iteration)
                    {
                        if (!lifetime.AddRef(reused))
                        {
                            static_cast<void>(stressFailures.Increment());
                            continue;
                        }
                        if (lifetime.Release(reused) < 1)
                            static_cast<void>(stressFailures.Increment());
                    }
                });
        }
        for (std::thread& worker : workers)
            worker.join();
        if (stressFailures.GetValue() != 0 || lifetime.GetRefCount(reused) != 1)
            return false;

        if (!lifetime.RecordUse(reused, gpu::QueueType::Graphics, 7) || !lifetime.RecordUse(reused, gpu::QueueType::Compute, 9) ||
            lifetime.Release(reused) != 0)
            return false;
        lifetime.SealRetirementEpoch({7, 9, 0});
        fences.completed[0].SetValue(7);
        fences.completed[1].SetValue(8);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 1)
            return false;
        fences.completed[1].SetValue(9);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 2)
            return false;

        const gpu::ResourceRef child = lifetime.Create(gpu::ResourceKind::Buffer, nullptr, &DestroyResource, &destructions);
        if (!child || !lifetime.AddRef(child) || lifetime.Release(child) != 1)
            return false;
        DependencyHarness dependency{&lifetime, child, &destructions};
        const gpu::ResourceRef parent = lifetime.Create(gpu::ResourceKind::Pipeline, nullptr, &DestroyDependentResource, &dependency);
        if (!parent || lifetime.Release(parent) != 0)
            return false;
        lifetime.SealRetirementEpoch({7, 9, 0});
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 3 || lifetime.IsValid(child))
            return false;
        lifetime.SealRetirementEpoch({7, 9, 0});
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 4)
            return false;

        const gpu::ResourceLifetimeStats stats = lifetime.GetStats();
        if (stats.liveResources != 0 || stats.pendingRetirements != 0 || stats.totalReferences != 0 || stats.completedRetirements != 4 ||
            stats.peakPendingRetirements == 0 || stats.retirementQueueRecoveries != 0 || stats.retirementBucketOverflows != 0 ||
            stats.staleReferenceOperations == 0)
            return false;
        if (!lifetime.ShutdownAfterGpuIdle())
            return false;

        gpu::backend::ResourceLifetimeManager emergencyLifetime;
        if (!emergencyLifetime.Initialize(SmallLifetimeConfig(), &IsFenceComplete, &fences))
            return false;
        const gpu::ResourceRef leakedOwner = emergencyLifetime.Create(gpu::ResourceKind::Buffer, nullptr, &DestroyResource, &destructions);
        if (!leakedOwner)
            return false;
        emergencyLifetime.ForceShutdownAfterGpuIdle();
        return destructions.destroyed.GetValue() == 5;
    }

    [[nodiscard]] bool TestRetirementBucketPressure() noexcept
    {
        FenceHarness fences{};
        DestructionHarness destructions{};
        gpu::backend::ResourceLifetimeConfig config = SmallLifetimeConfig();
        config.retirementBucketCount = 2;

        gpu::backend::ResourceLifetimeManager lifetime;
        if (!lifetime.Initialize(config, &IsFenceComplete, &fences))
            return false;

        const gpu::ResourceRef first = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        const gpu::ResourceRef second = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        const gpu::ResourceRef third = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!first || !second || !third)
            return false;

        if (!lifetime.RecordUse(first, gpu::QueueType::Graphics, 1) || lifetime.Release(first) != 0)
            return false;
        lifetime.SealRetirementEpoch({1, 0, 0});

        if (!lifetime.RecordUse(second, gpu::QueueType::Graphics, 2) || lifetime.Release(second) != 0)
            return false;
        lifetime.SealRetirementEpoch({2, 0, 0});
        if (lifetime.GetStats().retirementBucketOverflows == 0)
            return false;

        if (!lifetime.RecordUse(third, gpu::QueueType::Graphics, 3) || lifetime.Release(third) != 0)
            return false;
        lifetime.SealRetirementEpoch({3, 0, 0});

        fences.completed[0].SetValue(1);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 1 || lifetime.GetStats().pendingRetirements != 2)
            return false;

        // Collection freed the oldest bucket. A new seal can now advance the bucket that accumulated the second
        // and third submissions while the ring was saturated.
        lifetime.SealRetirementEpoch({3, 0, 0});
        fences.completed[0].SetValue(3);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        const gpu::ResourceLifetimeStats stats = lifetime.GetStats();
        return destructions.destroyed.GetValue() == 3 && stats.liveResources == 0 && stats.pendingRetirements == 0 &&
               stats.totalReferences == 0 && lifetime.ShutdownAfterGpuIdle();
    }

    [[nodiscard]] bool TestConcurrentRetirementPublication() noexcept
    {
        constexpr vanguard::u32 ResourceCount = 256;
        constexpr vanguard::u32 ThreadCount = 8;

        FenceHarness fences{};
        DestructionHarness destructions{};
        gpu::backend::ResourceLifetimeConfig config = SmallLifetimeConfig();
        config.textureCapacity = ResourceCount;
        config.retirementBucketCount = 4;

        gpu::backend::ResourceLifetimeManager lifetime;
        if (!lifetime.Initialize(config, &IsFenceComplete, &fences))
            return false;

        gpu::ResourceRef resources[ResourceCount]{};
        for (vanguard::u32 index = 0; index < ResourceCount; ++index)
        {
            resources[index] = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
            if (!resources[index])
                return false;
        }

        vanguard::concurrency::Atomic<vanguard::u32> start{0};
        vanguard::concurrency::Atomic<vanguard::u32> workersDone{0};
        vanguard::concurrency::Atomic<vanguard::u32> failures{0};
        std::thread workers[ThreadCount];
        for (vanguard::u32 threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
        {
            workers[threadIndex] = std::thread(
                [&, threadIndex]() noexcept
                {
                    while (start.GetValue() == 0)
                        vanguard::concurrency::YieldCurrentThread();
                    for (vanguard::u32 index = threadIndex; index < ResourceCount; index += ThreadCount)
                    {
                        if (lifetime.Release(resources[index]) != 0)
                            static_cast<void>(failures.Increment());
                    }
                    static_cast<void>(workersDone.Increment());
                });
        }

        start.SetValue(1);
        vanguard::u64 epoch = 0;
        while (workersDone.GetValue() != ThreadCount)
        {
            ++epoch;
            lifetime.SealRetirementEpoch({epoch, epoch, epoch});
            for (auto& completed : fences.completed)
                completed.SetValue(epoch);
            lifetime.CollectGarbage();
            vanguard::concurrency::YieldCurrentThread();
        }
        for (std::thread& worker : workers)
            worker.join();

        for (vanguard::u32 attempt = 0; attempt < 16 && lifetime.GetStats().pendingRetirements != 0; ++attempt)
        {
            ++epoch;
            lifetime.SealRetirementEpoch({epoch, epoch, epoch});
            for (auto& completed : fences.completed)
                completed.SetValue(epoch);
            lifetime.CollectGarbage();
            lifetime.WaitForReclamation();
        }

        if (failures.GetValue() != 0 || destructions.destroyed.GetValue() != ResourceCount)
            return false;
        const gpu::ResourceLifetimeStats stats = lifetime.GetStats();
        if (stats.liveResources != 0 || stats.pendingRetirements != 0 || stats.totalReferences != 0)
            return false;

        const gpu::ResourceRef reused = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!reused || lifetime.Release(reused) != 0)
            return false;
        ++epoch;
        lifetime.SealRetirementEpoch({epoch, epoch, epoch});
        for (auto& completed : fences.completed)
            completed.SetValue(epoch);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        return destructions.destroyed.GetValue() == ResourceCount + 1 && lifetime.ShutdownAfterGpuIdle();
    }

    [[nodiscard]] bool TestNativeResourcesAndSubmission(gpu::Failure& failure) noexcept
    {
        const vanguard::u32 sourceData[] = {0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u};
        gpu::BufferDesc sourceDesc{};
        sourceDesc.size = sizeof(sourceData);
        sourceDesc.structureStride = sizeof(vanguard::u32);
        sourceDesc.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination | gpu::BufferUsage::Structured |
                           gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        sourceDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer source(gpu::AdoptReference, gpu::CreateBuffer(sourceDesc, {sourceData, sizeof(sourceData)}, &failure));
        if (!source)
            return false;

        gpu::BufferDesc destinationDesc = sourceDesc;
        destinationDesc.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::CopySource;
        gpu::Buffer destination(gpu::AdoptReference, gpu::CreateBuffer(destinationDesc, {}, &failure));
        if (!destination)
            return false;

        gpu::BufferDesc readbackDesc{};
        readbackDesc.size = sizeof(sourceData);
        readbackDesc.usage = gpu::BufferUsage::CopyDestination;
        readbackDesc.initialState = gpu::ResourceState::CopyDestination;
        readbackDesc.memoryType = gpu::MemoryType::Readback;
        gpu::Buffer readback(gpu::AdoptReference, gpu::CreateBuffer(readbackDesc, {}, &failure));
        if (!readback)
            return false;

        vanguard::u8 pixels[4u * 4u * 4u]{};
        for (vanguard::u32 index = 0; index < sizeof(pixels); ++index)
            pixels[index] = static_cast<vanguard::u8>(index);
        gpu::TextureSubresourceData textureSubresource{pixels, sizeof(pixels), 16, 0, 0, 0};
        gpu::TextureDesc textureDesc{};
        textureDesc.extent = {4, 4, 1};
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopyDestination | gpu::TextureUsage::UnorderedAccess;
        textureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {&textureSubresource, 1}, &failure));
        if (!texture)
            return false;

        gpu::TextureDesc renderTargetDesc{};
        renderTargetDesc.extent = {64, 64, 1};
        renderTargetDesc.format = gpu::Format::R8G8B8A8UNorm;
        renderTargetDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource;
        renderTargetDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture renderTarget(gpu::AdoptReference, gpu::CreateTexture(renderTargetDesc, {}, &failure));
        const float vertices[] = {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f};
        gpu::BufferDesc vertexBufferDesc{};
        vertexBufferDesc.size = sizeof(vertices);
        vertexBufferDesc.usage = gpu::BufferUsage::Vertex;
        vertexBufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer vertexBuffer(gpu::AdoptReference, gpu::CreateBuffer(vertexBufferDesc, {vertices, sizeof(vertices)}, &failure));
        const vanguard::u16 indices[] = {0, 1, 2};
        gpu::BufferDesc indexBufferDesc{};
        indexBufferDesc.size = sizeof(indices);
        indexBufferDesc.usage = gpu::BufferUsage::Index;
        indexBufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer indexBuffer(gpu::AdoptReference, gpu::CreateBuffer(indexBufferDesc, {indices, sizeof(indices)}, &failure));
        struct IndirectCommands
        {
            gpu::IndirectDrawArguments draw{3, 1, 0, 0};
            gpu::IndirectDrawIndexedArguments drawIndexed{3, 1, 0, 0, 0};
            gpu::IndirectDispatchArguments dispatch{1, 1, 1};
        };
        const IndirectCommands indirectCommands{};
        gpu::BufferDesc indirectBufferDesc{};
        indirectBufferDesc.size = sizeof(indirectCommands);
        indirectBufferDesc.usage = gpu::BufferUsage::IndirectArguments;
        indirectBufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer indirectBuffer(gpu::AdoptReference,
                                   gpu::CreateBuffer(indirectBufferDesc, {&indirectCommands, sizeof(indirectCommands)}, &failure));
        const vanguard::u32 indirectCountValue = 1;
        gpu::BufferDesc indirectCountBufferDesc{};
        indirectCountBufferDesc.size = sizeof(indirectCountValue);
        indirectCountBufferDesc.usage = gpu::BufferUsage::IndirectArguments;
        indirectCountBufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer indirectCountBuffer(
            gpu::AdoptReference, gpu::CreateBuffer(indirectCountBufferDesc, {&indirectCountValue, sizeof(indirectCountValue)}, &failure));
        if (!renderTarget || !vertexBuffer || !indexBuffer || !indirectBuffer || !indirectCountBuffer)
        {
            std::printf("[rhiNvrhiTests] draw resource creation failed: rt=%u vb=%u ib=%u indirect=%u count=%u %s\n",
                        static_cast<unsigned>(static_cast<bool>(renderTarget)), static_cast<unsigned>(static_cast<bool>(vertexBuffer)),
                        static_cast<unsigned>(static_cast<bool>(indexBuffer)), static_cast<unsigned>(static_cast<bool>(indirectBuffer)),
                        static_cast<unsigned>(static_cast<bool>(indirectCountBuffer)), failure.message);
            return false;
        }
        gpu::BufferDesc aliasBufferDesc{};
        aliasBufferDesc.size = 256;
        aliasBufferDesc.usage = gpu::BufferUsage::UnorderedAccess;
        aliasBufferDesc.initialState = gpu::ResourceState::Common;
        aliasBufferDesc.virtualResource = true;
        gpu::Buffer aliasBufferBefore(gpu::AdoptReference, gpu::CreateBuffer(aliasBufferDesc, {}, &failure));
        gpu::Buffer aliasBufferAfter(gpu::AdoptReference, gpu::CreateBuffer(aliasBufferDesc, {}, &failure));
        const gpu::MemoryRequirements aliasRequirements = gpu::GetMemoryRequirements(aliasBufferBefore);
        gpu::Heap aliasHeap(gpu::AdoptReference, gpu::CreateHeap({aliasRequirements.size, aliasRequirements.alignment,
                                                                  aliasRequirements.compatibilityClass, gpu::MemoryType::DeviceLocal},
                                                                 &failure));
        if (!aliasBufferBefore || !aliasBufferAfter || aliasRequirements.size == 0 || !aliasHeap ||
            !gpu::BindMemory(aliasBufferBefore, aliasHeap, 0, &failure) || !gpu::BindMemory(aliasBufferAfter, aliasHeap, 0, &failure))
            return false;

        gpu::SamplerState sampler(gpu::AdoptReference, gpu::RequestSamplerState({}, &failure));
        if (!sampler)
            return false;

        const gpu::BindingLayoutEntry bindingEntries[] = {{0, 1, gpu::BindingType::TextureShaderResource},
                                                          {1, 1, gpu::BindingType::StructuredBufferShaderResource},
                                                          {0, 1, gpu::BindingType::Sampler}};
        const gpu::BindingLayoutDesc bindingDesc{
            bindingEntries, 3, 2, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)};
        const gpu::BindingLayoutEntry reversedBindingEntries[] = {bindingEntries[2], bindingEntries[1], bindingEntries[0]};
        const gpu::BindingLayoutDesc reversedBindingDesc{reversedBindingEntries, 3, 2, bindingDesc.visibility};
        gpu::BindingLayout bindingLayout(gpu::AdoptReference, gpu::RequestBindingLayout(bindingDesc, &failure));
        gpu::BindingLayout duplicateLayout(gpu::AdoptReference, gpu::RequestBindingLayout(reversedBindingDesc, &failure));
        if (!bindingLayout || !duplicateLayout || bindingLayout.GetRef() != duplicateLayout.GetRef())
            return false;

        CompiledShader vertexBytecode;
        CompiledShader pixelBytecode;
        CompiledShader computeBytecode;
        if (!CompileShader("float4 main(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }", "vs_5_0",
                           vertexBytecode) ||
            !CompileShader("float4 main() : SV_Target0 { return float4(1.0, 0.0, 1.0, 1.0); }", "ps_5_0", pixelBytecode) ||
            !CompileShader("[numthreads(1,1,1)] void main() {}", "cs_5_0", computeBytecode))
            return false;
        gpu::Shader vertexShader(gpu::AdoptReference,
                                 gpu::CreateShader({gpu::ShaderStage::Vertex, vertexBytecode.bytecode->GetBufferPointer(),
                                                    vertexBytecode.bytecode->GetBufferSize(), "main"},
                                                   &failure));
        gpu::Shader pixelShader(gpu::AdoptReference, gpu::CreateShader({gpu::ShaderStage::Pixel, pixelBytecode.bytecode->GetBufferPointer(),
                                                                        pixelBytecode.bytecode->GetBufferSize(), "main"},
                                                                       &failure));
        gpu::Shader computeShader(gpu::AdoptReference,
                                  gpu::CreateShader({gpu::ShaderStage::Compute, computeBytecode.bytecode->GetBufferPointer(),
                                                     computeBytecode.bytecode->GetBufferSize(), "main"},
                                                    &failure));
        const gpu::VertexBindingDesc vertexBinding{0, 12, gpu::VertexInputRate::PerVertex, 1};
        const gpu::VertexAttributeDesc vertexAttribute{0, 0, 0, gpu::Format::R32G32B32Float, "POSITION", 0};
        const gpu::VertexLayoutRef vertexLayout = gpu::GetVertexLayout({&vertexBinding, 1, &vertexAttribute, 1}, &failure);
        if (!vertexShader || !pixelShader || !computeShader || !vertexLayout)
            return false;

        gpu::GraphicsPipelineDesc graphicsDesc{};
        graphicsDesc.vertexShader = vertexShader;
        graphicsDesc.pixelShader = pixelShader;
        graphicsDesc.vertexLayout = vertexLayout;
        graphicsDesc.attachments.colorFormats[0] = gpu::Format::R8G8B8A8UNorm;
        graphicsDesc.attachments.colorCount = 1;
        gpu::Pipeline graphicsPipeline(gpu::AdoptReference, gpu::CreateGraphicsPipeline(graphicsDesc, &failure));
        gpu::ComputePipelineDesc computeDesc{};
        computeDesc.computeShader = computeShader;
        gpu::Pipeline computePipeline(gpu::AdoptReference, gpu::CreateComputePipeline(computeDesc, &failure));
        if (!graphicsPipeline || !computePipeline)
            return false;

        vanguard::rendering::PipelineCache pipelineCache;
        if (!pipelineCache.Initialize())
        {
            std::printf("[rhiNvrhiTests] rendering pipeline cache initialization failed\n");
            return false;
        }
        const vanguard::crypto::Digest256 pipelineKey = vanguard::crypto::Sha256("rhi graphics pipeline", 21);
        vanguard::rendering::PipelineRequest firstPipelineRequest;
        vanguard::rendering::PipelineRequest duplicatePipelineRequest;
        if (pipelineCache.RequestGraphics(pipelineKey, graphicsDesc, firstPipelineRequest) != vanguard::pipeline_cache::Result::Success ||
            pipelineCache.RequestGraphics(pipelineKey, graphicsDesc, duplicatePipelineRequest) != vanguard::pipeline_cache::Result::Success)
        {
            std::printf("[rhiNvrhiTests] rendering pipeline cache request failed\n");
            return false;
        }
        firstPipelineRequest.Wait();
        if (!firstPipelineRequest.HasSucceeded() || !duplicatePipelineRequest.HasSucceeded() ||
            firstPipelineRequest.Pipeline() != duplicatePipelineRequest.Pipeline() || pipelineCache.GetStats().coalescedRequests == 0)
        {
            const vanguard::pipeline_cache::FailureEvidence cacheFailure = firstPipelineRequest.Error();
            std::printf("[rhiNvrhiTests] cached graphics pipeline creation failed: %s\n", cacheFailure.message);
            return false;
        }
        firstPipelineRequest.Reset();
        duplicatePipelineRequest.Reset();
        if (pipelineCache.InvalidateAll() != 1 || !pipelineCache.Shutdown())
        {
            std::printf("[rhiNvrhiTests] rendering pipeline cache shutdown failed\n");
            return false;
        }

        gpu::DescriptorDomain resourceDescriptors;
        gpu::DescriptorDomain samplerDescriptors;
        gpu::DescriptorHandle textureDescriptor{};
        gpu::DescriptorHandle bufferDescriptor{};
        gpu::DescriptorHandle samplerDescriptor{};
        if (gpu::GetCapabilities().bindlessResources)
        {
            resourceDescriptors = gpu::DescriptorDomain(
                gpu::AdoptReference,
                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0,
                                             gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)},
                                            &failure));
            samplerDescriptors = gpu::DescriptorDomain(
                gpu::AdoptReference,
                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 1, 0,
                                             gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)},
                                            &failure));
            if (!resourceDescriptors || !samplerDescriptors)
                return false;
            textureDescriptor = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            bufferDescriptor = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            samplerDescriptor = gpu::AllocateDescriptor(samplerDescriptors, &failure);
            if (!textureDescriptor || !bufferDescriptor || !samplerDescriptor ||
                !gpu::WriteDescriptor(resourceDescriptors, textureDescriptor, texture, gpu::BindingType::TextureShaderResource, {},
                                      &failure) ||
                !gpu::WriteDescriptor(resourceDescriptors, bufferDescriptor, source, gpu::BindingType::StructuredBufferShaderResource, {},
                                      &failure) ||
                !gpu::WriteDescriptor(samplerDescriptors, samplerDescriptor, sampler, &failure))
                return false;
            if (gpu::WriteDescriptor(resourceDescriptors, textureDescriptor, texture, gpu::BindingType::TextureShaderResource, {},
                                     &failure) ||
                failure.code != gpu::FailureCode::IncompatibleBinding)
                return false;
            const gpu::DescriptorDomainStats resourceStats = gpu::GetDescriptorDomainStats(resourceDescriptors);
            if (resourceStats.capacity != 2 || resourceStats.allocated != 2 || resourceStats.populated != 2 || resourceStats.free != 0)
                return false;

            constexpr vanguard::u32 concurrentDescriptorCount = 128;
            gpu::DescriptorDomain concurrentDomain(
                gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, concurrentDescriptorCount, 0,
                                                                  gpu::ShaderStageBit(gpu::ShaderStage::Compute)},
                                                                 &failure));
            gpu::DescriptorHandle concurrentDescriptors[concurrentDescriptorCount]{};
            vanguard::concurrency::Atomic<vanguard::u32> allocationFailures{0};
            vanguard::jobs::Builder builder;
            vanguard::jobs::JobName name{"Vanguard.RHI.DescriptorDomain.ConcurrentAllocation"};
            for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
            {
                vanguard::jobs::Task task = vanguard::jobs::Task::Create(
                    [&, index](const vanguard::jobs::JobContext&) noexcept
                    {
                        concurrentDescriptors[index] = gpu::AllocateDescriptor(concurrentDomain);
                        if (!concurrentDescriptors[index] || !gpu::WriteDescriptor(concurrentDomain, concurrentDescriptors[index], texture,
                                                                                   gpu::BindingType::TextureShaderResource))
                            static_cast<void>(allocationFailures.Increment());
                    });
                if (!builder.Dispatch(name, static_cast<vanguard::jobs::Task&&>(task), vanguard::jobs::Fence::None))
                    return false;
            }
            builder.DispatchFence();
            vanguard::jobs::Counter counter = builder.ExtractCounter();
            if (!counter.IsValid() || !counter.Wait() || allocationFailures.GetValue() != 0)
                return false;
            for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
            {
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (concurrentDescriptors[index].index == concurrentDescriptors[previous].index)
                        return false;
                if (!gpu::RetireDescriptor(concurrentDomain, concurrentDescriptors[index], {}, &failure))
                    return false;
            }
            const gpu::DescriptorDomainStats concurrentStats = gpu::GetDescriptorDomainStats(concurrentDomain);
            if (concurrentStats.peakAllocated != concurrentDescriptorCount ||
                concurrentStats.completedRetirements != concurrentDescriptorCount || concurrentStats.free != concurrentDescriptorCount)
                return false;
        }

        const gpu::BindingLayoutEntry pushConstantEntry{0, 16, gpu::BindingType::PushConstants};
        gpu::BindingLayout pushConstantLayout(
            gpu::AdoptReference,
            gpu::RequestBindingLayout(
                {&pushConstantEntry, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)},
                &failure));
        if (!pushConstantLayout)
        {
            std::printf("[rhiNvrhiTests] push-constant layout creation failed: %s\n", failure.message);
            return false;
        }
        const gpu::BindingLayoutRef executionLayouts[] = {pushConstantLayout};
        const gpu::DescriptorDomainRef executionDomains[] = {resourceDescriptors, samplerDescriptors};
        gpu::GraphicsPipelineDesc executionGraphicsDesc = graphicsDesc;
        executionGraphicsDesc.bindingLayouts = executionLayouts;
        executionGraphicsDesc.bindingLayoutCount = 1;
        if (resourceDescriptors)
        {
            executionGraphicsDesc.descriptorDomains = executionDomains;
            executionGraphicsDesc.descriptorDomainCount = 2;
        }
        gpu::Pipeline executionGraphicsPipeline(gpu::AdoptReference, gpu::CreateGraphicsPipeline(executionGraphicsDesc, &failure));
        if (!executionGraphicsPipeline)
        {
            std::printf("[rhiNvrhiTests] executable graphics pipeline creation failed: %s\n", failure.message);
            return false;
        }
        vanguard::rendering::PipelineCache domainPipelineCache;
        vanguard::rendering::PipelineRequest domainPipelineRequest;
        const vanguard::crypto::Digest256 domainPipelineKey = vanguard::crypto::Sha256("rhi bindless graphics pipeline", 30);
        if (!domainPipelineCache.Initialize() ||
            domainPipelineCache.RequestGraphics(domainPipelineKey, executionGraphicsDesc, domainPipelineRequest) !=
                vanguard::pipeline_cache::Result::Success)
            return false;
        domainPipelineRequest.Wait();
        if (!domainPipelineRequest.HasSucceeded())
            return false;
        domainPipelineRequest.Reset();
        if (domainPipelineCache.InvalidateAll() != 1 || !domainPipelineCache.Shutdown())
            return false;

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x56474e44524849ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
            return false;
        const gpu::RenderTargetSetup renderTargets{{{renderTarget, gpu::Format::Unknown, 0, 0, false}}, 1, {}};
        const gpu::VertexBufferBinding vertexBindingState{vertexBuffer, 0, 0};
        const gpu::IndexBufferBinding indexBindingState{indexBuffer, 0, gpu::IndexFormat::UInt16};
        const vanguard::u32 pushConstants[] = {textureDescriptor.GpuIndex(), bufferDescriptor.GpuIndex(), samplerDescriptor.GpuIndex(), 0};
        if (!gpu::TransitionTexture(renderTarget, gpu::ResourceState::Common, gpu::ResourceState::RenderTarget, {}, &failure) ||
            !gpu::TransitionBuffer(vertexBuffer, gpu::ResourceState::Common, gpu::ResourceState::VertexBuffer, &failure) ||
            !gpu::TransitionBuffer(indexBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndexBuffer, &failure) ||
            !gpu::TransitionBuffer(indirectBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndirectArgument, &failure) ||
            !gpu::TransitionBuffer(indirectCountBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndirectArgument, &failure) ||
            !gpu::BarrierBufferAliasing(false, aliasBufferAfter, aliasBufferBefore, &failure) ||
            !gpu::SetPipeline(executionGraphicsPipeline, &failure) || !gpu::SetupRenderTargets(renderTargets, &failure) ||
            !gpu::SetViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}, &failure) || !gpu::SetScissors({0, 0, 64, 64}, &failure) ||
            !gpu::BindVertexBuffers(0, {&vertexBindingState, 1}, &failure) || !gpu::BindIndexBuffer(indexBindingState, &failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, {}, &failure) ||
            !gpu::SetPushConstants(pushConstants, sizeof(pushConstants), &failure) || !gpu::DrawPrimitive({3, 1, 0, 0}, &failure) ||
            !gpu::DrawIndexedPrimitive({3, 1, 0, 0, 0}, &failure) || !gpu::DrawPrimitiveIndirect(0, 1, &failure) ||
            !gpu::DrawIndexedPrimitiveIndirect(sizeof(gpu::IndirectDrawArguments), 1, &failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, indirectCountBuffer, &failure) ||
            !gpu::DrawIndexedPrimitiveIndirectCount(sizeof(gpu::IndirectDrawArguments), 0, 1, &failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, {}, &failure) || !gpu::SetPipeline(computePipeline, &failure) ||
            !gpu::DispatchCompute(1, 1, 1, &failure) ||
            !gpu::DispatchIndirectCompute(sizeof(gpu::IndirectDrawArguments) + sizeof(gpu::IndirectDrawIndexedArguments), &failure) ||
            !gpu::TransitionTexture(texture, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, {}, &failure) ||
            !gpu::BarrierTextureUav(texture, &failure) ||
            !gpu::TransitionBuffer(source, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, &failure) ||
            !gpu::BarrierBufferUav(source, &failure) ||
            !gpu::TransitionBuffer(source, gpu::ResourceState::UnorderedAccess, gpu::ResourceState::CopySource, &failure) ||
            !gpu::TransitionBuffer(destination, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, &failure) ||
            !gpu::CopyBuffer(destination, 0, source, 0, sizeof(sourceData), &failure) ||
            !gpu::TransitionBuffer(destination, gpu::ResourceState::CopyDestination, gpu::ResourceState::CopySource, &failure) ||
            !gpu::CopyBuffer(readback, 0, destination, 0, sizeof(sourceData), &failure) || !gpu::FlushPendingBarriers(&failure))
        {
            std::printf("[rhiNvrhiTests] graphics/compute command recording failed: %s\n", failure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef submission[] = {commandList};
        gpu::GpuFence completion{};
        if (!gpu::CloseAndSubmitCommandLists("rhi resource conformance", {submission, 1}, gpu::CommandListSyncType::None, completion,
                                             &failure) ||
            !completion.IsValid())
            return false;

        if (resourceDescriptors)
        {
            gpu::DescriptorRetirement retirement;
            retirement.Include(completion);
            if (!gpu::RetireDescriptor(resourceDescriptors, textureDescriptor, retirement, &failure) ||
                !gpu::RetireDescriptor(resourceDescriptors, bufferDescriptor, retirement, &failure) ||
                !gpu::RetireDescriptor(samplerDescriptors, samplerDescriptor, retirement, &failure))
                return false;
            texture.Reset();
        }
        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || !gpu::RetireResources(&failure))
            return false;
        if (resourceDescriptors)
        {
            const gpu::DescriptorHandle recycledTexture = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            const gpu::DescriptorHandle recycledBuffer = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            const gpu::DescriptorHandle recycledSampler = gpu::AllocateDescriptor(samplerDescriptors, &failure);
            if (!recycledTexture || !recycledBuffer || !recycledSampler || recycledTexture.generation == textureDescriptor.generation ||
                recycledBuffer.generation == bufferDescriptor.generation || recycledSampler.generation == samplerDescriptor.generation)
                return false;
            if (gpu::RetireDescriptor(resourceDescriptors, textureDescriptor, {}, &failure) ||
                failure.code != gpu::FailureCode::InvalidReference ||
                !gpu::RetireDescriptor(resourceDescriptors, recycledTexture, {}, &failure) ||
                !gpu::RetireDescriptor(resourceDescriptors, recycledBuffer, {}, &failure) ||
                !gpu::RetireDescriptor(samplerDescriptors, recycledSampler, {}, &failure))
                return false;
        }

        const auto* const copiedData = static_cast<const vanguard::u32*>(gpu::LockBuffer(readback, 0, sizeof(sourceData), &failure));
        if (copiedData == nullptr)
            return false;
        bool copyMatches = true;
        for (vanguard::u32 index = 0; index < 4; ++index)
            copyMatches = copyMatches && copiedData[index] == sourceData[index];
        gpu::UnlockBuffer(readback);
        if (!copyMatches)
            return false;

        source.Reset();
        destination.Reset();
        readback.Reset();
        renderTarget.Reset();
        vertexBuffer.Reset();
        indexBuffer.Reset();
        indirectBuffer.Reset();
        indirectCountBuffer.Reset();
        aliasBufferBefore.Reset();
        aliasBufferAfter.Reset();
        aliasHeap.Reset();
        texture.Reset();
        sampler.Reset();
        executionGraphicsPipeline.Reset();
        pushConstantLayout.Reset();
        duplicateLayout.Reset();
        bindingLayout.Reset();
        resourceDescriptors.Reset();
        samplerDescriptors.Reset();
        return gpu::RetireResources(&failure) && gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
    }
} // namespace

int main()
{
    if (!vanguard::memory::Initialize())
    {
        std::printf("[rhiNvrhiTests] memory initialization failed\n");
        return 1;
    }

    if (!vanguard::jobs::Initialize(vanguard::jobs::ToolConfig()))
    {
        std::printf("[rhiNvrhiTests] jobs initialization failed\n");
        return 1;
    }
    if (!TestResourceLifetimeManager() || !TestRetirementBucketPressure() || !TestConcurrentRetirementPublication())
    {
        std::printf("[rhiNvrhiTests] resource lifetime tests failed\n");
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }

    vanguard::rhi::d3d12::Backend backend;
    vanguard::rhi::Failure failure{};
    vanguard::rhi::DeviceParams params{};
#if VG_ENABLE_ASSERTS
    params.enableValidation = true;
#endif
    if (!vanguard::rhi::Initialize(backend, params, &failure))
    {
        if (failure.code == vanguard::rhi::FailureCode::Unsupported)
        {
            std::printf("[rhiNvrhiTests] skipped: %s\n", failure.message);
            static_cast<void>(vanguard::jobs::Shutdown());
            return 0;
        }
        std::printf("[rhiNvrhiTests] initialization failed: %s (0x%llx)\n", failure.message,
                    static_cast<unsigned long long>(failure.backendCode));
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }

    const vanguard::rhi::Capabilities& capabilities = vanguard::rhi::GetCapabilities();
    if (capabilities.backend != vanguard::rhi::BackendKind::D3D12 || capabilities.adapterName[0] == '\0' ||
        vanguard::rhi::TestDeviceState() != vanguard::rhi::DeviceState::Operational)
    {
        std::printf("[rhiNvrhiTests] invalid device capability contract\n");
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestNativeResourcesAndSubmission(failure))
    {
        std::printf("[rhiNvrhiTests] native resource/submission test failed: %s\n", failure.message);
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    std::printf("[rhiNvrhiTests] D3D12 backend lifecycle passed on %s\n", capabilities.adapterName);
    if (!vanguard::rhi::RetireResources(&failure) || !vanguard::rhi::WaitIdle(&failure) || !vanguard::rhi::Shutdown(&failure))
    {
        const vanguard::rhi::ResourceLifetimeStats stats = vanguard::rhi::GetResourceLifetimeStats();
        std::printf("[rhiNvrhiTests] clean shutdown failed: %s (live=%u, pending=%u, references=%llu)\n", failure.message,
                    stats.liveResources, stats.pendingRetirements, static_cast<unsigned long long>(stats.totalReferences));
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }

    if (!vanguard::jobs::Shutdown())
    {
        std::printf("[rhiNvrhiTests] jobs shutdown failed\n");
        return 1;
    }
    std::printf("[rhiNvrhiTests] deferred resource lifetime tests passed\n");
    return 0;
}
