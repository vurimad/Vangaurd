#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/backend/command_list_retention.hpp>
#include <vanguard/rhi/backend/resource_lifetime.hpp>
#include <vanguard/rhi/gpu_counters.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/rendering/pipeline_cache.hpp>
#include <vanguard/rendering/render_pipeline_factory.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_shader.hpp>
#include <vanguard/rendering/render_shader_map.hpp>
#include <vanguard/rendering/frame_renderer.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>
#include <vanguard/rendering/render_graph_nodes.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/rendering/presentation_service.hpp>
#include <vanguard/rendering/gpu_scene_definitions.hpp>
#include <vanguard/rendering/gpu_scene_lifetime.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/gpu_scene_upload.hpp>
#include <vanguard/rendering/render_scene_gpu.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/window/window_manager.hpp>
#include "../../../../external/nvrhi/upstream/src/common/state-tracking.h"

#include <cstdio>
#include <d3dcompiler.h>
#include <new>
#include <thread>
#include <Windows.h>

namespace
{
    namespace gpu = vanguard::rhi;
    namespace rendering = vanguard::rendering;
    namespace window = vanguard::window;

    struct FenceHarness
    {
        vanguard::concurrency::Atomic<vanguard::u64> completed[3] = {vanguard::concurrency::Atomic<vanguard::u64>(0), vanguard::concurrency::Atomic<vanguard::u64>(0),
                                                                     vanguard::concurrency::Atomic<vanguard::u64>(0)};
    };

    // Diagnostic fixture: catalog lookup occurs at construction, never during recording.
    class CatalogDrawNode final : public rendering::RenderNodeImpl
    {
    public:
        CatalogDrawNode(const rendering::FrameRenderer& renderer, const gpu::TextureRef target, const gpu::BufferRef vertices, bool& succeeded) noexcept
            : m_pipeline(renderer.GetPipeline("FeaturePipeline")), m_target(target), m_vertices(vertices), m_succeeded(succeeded) {}

        [[nodiscard]] rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::Require; }

        void Execute(const rendering::RenderNodeImplContext&, vanguard::jobs::Builder*) const override
        {
            gpu::Failure failure;
            const gpu::RenderTargetSetup targets{{{m_target, gpu::Format::Unknown, 0, 0, false}}, 1, {}};
            const gpu::VertexBufferBinding vertices{m_vertices, 0, 0};
            m_succeeded = gpu::SetPipeline(m_pipeline, &failure) && gpu::SetupRenderTargets(targets, &failure) &&
                gpu::SetViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}, &failure) && gpu::SetScissors({0, 0, 64, 64}, &failure) &&
                gpu::BindVertexBuffers(0, {&vertices, 1}, &failure) && gpu::DrawPrimitive({3, 1, 0, 0}, &failure);
            if (!m_succeeded)
                std::printf("[rhiNvrhiTests] catalog node recording failed: %s\n", failure.message);
        }

    private:
        gpu::PipelineRef m_pipeline;
        gpu::TextureRef m_target;
        gpu::BufferRef m_vertices;
        bool& m_succeeded;
    };

    bool CheckCatalogNodeDraw(const rendering::FrameRenderer& renderer, const gpu::TextureRef target, const gpu::BufferRef vertices) noexcept
    {
        bool recorded = false;
        CatalogDrawNode node(renderer, target, vertices, recorded);
        rendering::RenderNodeImplContext context;
        // Reuse the same node and borrowed pipeline across independent recordings.
        for (vanguard::u32 frame = 0; frame < 2; ++frame)
        {
            gpu::Failure failure;
            gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0, &failure);
            if (!commands)
                return false;
            if (!gpu::BindCommandList(commands, &failure))
            {
                gpu::DiscardCommandList(commands);
                return false;
            }
            recorded = false;
            if (!gpu::TransitionTexture(target, gpu::ResourceState::Unknown, gpu::ResourceState::RenderTarget, {}, &failure) ||
                !gpu::TransitionBuffer(vertices, gpu::ResourceState::Unknown, gpu::ResourceState::VertexBuffer, &failure) || !gpu::ClearColorTarget(target, {0, 0, 0, 0}, {}, nullptr, &failure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return false;
            }
            node.Execute(context, nullptr);
            recorded = recorded && gpu::TransitionTexture(target, gpu::ResourceState::Unknown, gpu::ResourceState::CopySource, {}, &failure);
            gpu::TextureReadback readback(gpu::AdoptReference, recorded ? gpu::RequestTextureReadback(target, {{}, 32, 32, 0, {1, 1, 1}}, &failure) : gpu::TextureReadbackRef{});
            gpu::UnbindCommandList();
            if (!readback)
            {
                std::printf("[rhiNvrhiTests] catalog node readback request failed: %s\n", failure.message);
                gpu::DiscardCommandList(commands);
                return false;
            }
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("catalog node draw", {&commands, 1}, gpu::CommandListSyncType::None, completion, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            {
                std::printf("[rhiNvrhiTests] catalog node submission failed: %s\n", failure.message);
                return false;
            }
            gpu::TextureReadbackMapping mapping;
            if (!gpu::MapTextureReadback(readback, mapping, &failure))
            {
                std::printf("[rhiNvrhiTests] catalog node readback mapping failed: %s\n", failure.message);
                return false;
            }
            const auto* pixel = static_cast<const vanguard::u8*>(mapping.data);
            const bool correct = mapping.dataSize >= 4 && pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255;
            if (!correct && mapping.dataSize >= 4)
                std::printf("[rhiNvrhiTests] catalog pixel = %u,%u,%u,%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
            const bool unmapped = gpu::UnmapTextureReadback(readback, &failure);
            if (!correct || !unmapped)
            {
                std::printf("[rhiNvrhiTests] catalog node pixel readback failed\n");
                return false;
            }
        }
        return true;
    }

    // Single-command-scope fixture runner; graph contents and shader identities are supplied by the caller.
    bool RunSingleScopeGraph(rendering::RenderNodeGraph& graph, rendering::RenderFlowResourceAllocator& allocator) noexcept
    {
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures failures;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderFrameInfo frame;
        rendering::RenderFrameCommandLists lists;
        lists.PrepareForFrame(graph.GetNumNodes() + static_cast<vanguard::u32>(rendering::ReservedFrameCommandList::Count));
        rendering::RenderNodeImplContext context;
        rendering::RenderNodeImplContext::InitData init(0);
        init.frame = &frame;
        init.frameCommandLists = &lists;
        context.Init(init);
        if (!graph.PrepareResourceBindings(bindings, failures))
            return false;
        vanguard::jobs::Builder declarations({vanguard::jobs::Priority::RenderPath, vanguard::jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(context, allocator, bindings, failures, declarations);
        if (!declarations.WaitForCompletion() || failures.HasFailure() || !allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) ||
            !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure) || !graph.PrepareExecutionPackets(allocator, lists, bindings, &failure))
            return false;
        vanguard::jobs::Builder gate({vanguard::jobs::Priority::RenderPath, vanguard::jobs::Affinity::AnyWorker});
        static vanguard::jobs::JobName gateName{"NativeGraph.Kickoff"};
        if (!gate.Dispatch(gateName, vanguard::jobs::Task::Create([](const vanguard::jobs::JobContext&) noexcept {})))
            return false;
        const auto kickoff = gate.ExtractCounter();
        vanguard::concurrency::Atomic<bool> completed{false};
        bool scheduled = false;
        rendering::RenderNodeJob::SetJobsRenderFrame(&frame);
        graph.AcquireExclusiveUpdateFlag();
        vanguard::jobs::Builder setup({vanguard::jobs::Priority::RenderPath, vanguard::jobs::Affinity::AnyWorker});
        static vanguard::jobs::JobName setupName{"NativeGraph.Setup"};
        const bool dispatched = setup.Dispatch(setupName, vanguard::jobs::Task::Create([&](const vanguard::jobs::JobContext& job) noexcept
        {
            scheduled = rendering::RenderNodeJob::RunRenderNodeJobs(graph, context, bindings, kickoff, job,
                vanguard::jobs::Task::Create([&](const vanguard::jobs::JobContext&) noexcept { completed.SetValue(true); }), &failure);
        }));
        const bool joined = setup.WaitForCompletion();
        graph.ReleaseExclusiveUpdateFlag();
        rendering::RenderNodeJob::ClearJobsRenderFrame(&frame);
        if (!dispatched || !joined || !scheduled || !completed.GetValue())
            return false;
        vanguard::containers::DynamicArray<rendering::CommandScopeExecutionReceipt> receipts(vanguard::memory::pools::Rendering::GetInstance());
        for (vanguard::u32 index = 0; index < graph.GetNumNodes(); ++index)
        {
            const auto node = graph.GetNode(index);
            const auto* impl = graph.GetNodeParameters(node).m_impl;
            if (!impl->HasCommandList(impl->GetCommandListUsage()))
                continue;
            rendering::CompiledExecutionPacketView packet;
            if (!allocator.PacketFor({node}, packet, &failure) || packet.GetQueue() != gpu::QueueType::Graphics)
                return false;
            bool submitted = false;
            for (const auto& receipt : receipts)
                submitted = submitted || receipt.scope == packet.GetCommandScope();
            if (submitted)
                continue;
            if (!receipts.Empty())
                return false; // Multi-scope submission ordering is not this fixture's contract.
            const auto group = graph.GetNodeContext(node).m_renderFlowGroups[static_cast<vanguard::u32>(rendering::RenderNodeDependencyType::Gpu)];
            const auto commands = lists.GetCommandList(group + static_cast<vanguard::u32>(rendering::ReservedFrameCommandList::Count));
            gpu::GpuFence fence;
            if (!gpu::CloseAndSubmitCommandLists("native graph fixture", {&commands, 1}, gpu::CommandListSyncType::None, fence) || !gpu::WaitForGpuFence(fence, 5'000'000'000ull))
                return false;
            receipts.PushBack({packet.GetCommandScope(), rendering::CommandScopeCompletionKind::Submitted, packet.GetQueue(), fence});
        }
        const auto generation = allocator.GetExecutionGeneration();
        return allocator.Finish({generation, rendering::TerminalExecutionCompletionKind::Completed, receipts, rendering::TerminalJoinToken::CompletedSynchronously(generation), {}}, &failure);
    }

    class CatalogGraphDrawNode final : public rendering::RenderNodeImpl
    {
    public:
        CatalogGraphDrawNode(const rendering::FrameRenderer& renderer, gpu::TextureRef target, gpu::BufferRef vertices, rendering::ImportedResourceId imported, bool& recorded) noexcept
            : m_draw(renderer, target, vertices, recorded), m_vertices(vertices), m_imported(imported) {}
        rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::Own; }
        gpu::CommandListRef CreateCommandList() const override { return gpu::CreateCommandList(gpu::CommandListType::Default, 0); }
        bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            const auto target = context.RTInject("CatalogTarget", m_imported);
            rendering::TextureUseDesc use;
            use.requiredState = gpu::ResourceState::RenderTarget;
            use.access = rendering::LogicalAccessIntent::Write;
            use.content = rendering::ResourceContentIntent::Discard;
            context.RTUseBegin(target, use);
            context.RTUseEnd(target);
            return true;
        }
        void Execute(const rendering::RenderNodeImplContext& context, vanguard::jobs::Builder* builder) const override
        {
            static_cast<void>(context.RTTexture("CatalogTarget"));
            if (!gpu::TransitionBuffer(m_vertices, gpu::ResourceState::Unknown, gpu::ResourceState::VertexBuffer))
                return;
            m_draw.Execute(context, builder);
        }
    private:
        CatalogDrawNode m_draw;
        gpu::BufferRef m_vertices;
        rendering::ImportedResourceId m_imported;
    };

    class ImportTexturePairNode final : public rendering::RenderNodeImpl
    {
    public:
        ImportTexturePairNode(rendering::ImportedResourceId source, rendering::ImportedResourceId target) noexcept : m_source(source), m_target(target) {}
        rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            static_cast<void>(context.RTInject("Source", m_source));
            static_cast<void>(context.RTInject("Target", m_target));
            return true;
        }
        void Execute(const rendering::RenderNodeImplContext&, vanguard::jobs::Builder*) const override {}
    private:
        rendering::ImportedResourceId m_source, m_target;
    };

    bool CheckFullscreenCopyGraph() noexcept
    {
        using namespace vanguard;
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        char path[2048];
        std::snprintf(path, sizeof(path), "%s/../../../../rendering/shaders/fullscreen_copy.vsl", __FILE__);
        auto file = filesystem::RawFileReader::Create(filesystem::AbsolutePath::CreateFilePath(path));
        if (!file)
            return false;
        containers::DynamicArray<u8> sourceBytes(memory::pools::Rendering::GetInstance());
        sourceBytes.Resize(static_cast<u32>(file->GetSize()));
        file->Serialize(sourceBytes.Data(), sourceBytes.Size());
        shader_tools::CompileRequest compile;
        compile.sourceName = "rendering/shaders/fullscreen_copy.vsl";
        compile.moduleName = "fullscreen_copy";
        compile.source = sourceBytes;
        const shader_tools::EntryPoint entries[] = {{"CopyVertexMain", shaders::ShaderStage::Vertex}, {"CopyFragmentMain", shaders::ShaderStage::Fragment}};
        compile.entryPoints = entries;
        shader_tools::CompileOutput output;
        if (compiler.Compile(compile, output) != shader_tools::Result::Success)
        {
            std::printf("[rhiNvrhiTests] copy compile: %s\n", output.GetDiagnostics());
            return false;
        }
        containers::DynamicArray<u8> shaderBytes(memory::pools::Rendering::GetInstance()), pipelineBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter shaderWriter(shaderBytes), pipelineWriter(pipelineBytes);
        if (output.WriteShader(shaderWriter, 0x4653434f5059ull, crypto::Sha256(sourceBytes.Data(), sourceBytes.Size())) != shader_tools::Result::Success)
            return false;
        shaders::ShaderFile shader;
        filesystem::MemoryFileReader shaderReader(shaderBytes, 0);
        if (shader.Open(shaderReader) != shaders::Result::Success)
            return false;
        const pipelines::ShaderReference reference{resources::ResourcePath::FromString("rendering/shaders/fullscreen_copy.vshader").Id(), shader.GetPermutation(), shader.BindingLayoutFingerprint(), shader.GetPipelineInterfaceFingerprint()};
        pipelines::BuildDescription pipelineDesc;
        pipelineDesc.name = 0x4653434f5059ull;
        pipelineDesc.shaders = {&reference, 1};
        pipelineDesc.graphics.rasterizer.cull = pipelines::CullMode::None;
        pipelineDesc.graphics.blend.attachmentCount = 1;
        if (pipelines::WritePipeline(pipelineWriter, pipelineDesc) != pipelines::Result::Success)
            return false;
        pipelines::PipelineFile pipeline;
        filesystem::MemoryFileReader pipelineReader(pipelineBytes, 0);
        if (pipeline.Open(pipelineReader) != pipelines::Result::Success)
            return false;
        gpu::DescriptorDomain resourcesDomain(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 16, 0, gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}));
        gpu::DescriptorDomain samplersDomain(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 16, 0, gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}));
        if (!resourcesDomain || !samplersDomain)
            return false;
        rendering::PipelineCache cache;
        rendering::RenderSceneManager scenes;
        rendering::RenderCameraStorage cameras;
        rendering::GpuSceneRuntime scene;
        rendering::FrameRenderer renderer(scenes, cameras, scene);
        const rendering::NamedRenderShader namedShader{"FullscreenCopy", &shader};
        if (!cache.Initialize() || renderer.InitializeShaders({&namedShader, 1}) != rendering::RenderShaderResult::Success)
            return false;
        const rendering::ResolvedRenderShader resolved{reference.resource, renderer.GetShader("FullscreenCopy")};
        const gpu::DescriptorDomainRef domains[] = {resourcesDomain, samplersDomain};
        const gpu::BindingLayoutEntry constantsEntry{0, 24, gpu::BindingType::PushConstants};
        gpu::BindingLayout constantsLayout(gpu::AdoptReference, gpu::RequestBindingLayout({&constantsEntry, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}));
        if (!constantsLayout) return false;
        const gpu::BindingLayoutRef layouts[] = {constantsLayout};
        pipelines::AttachmentSignature attachments;
        attachments.colorCount = 1;
        attachments.colors[0] = {pipelines::Format::R8G8B8A8UNorm, shaders::NumericClass::FloatingPoint};
        rendering::RenderPipelineRequest request;
        request.pipeline = &pipeline;
        request.shaders = {&resolved, 1};
        request.attachments = &attachments;
        request.interfaceResources.descriptorDomains = domains;
        request.interfaceResources.bindingLayouts = layouts;
        const rendering::NamedRenderPipeline namedPipeline{"FullscreenCopy", request};
        const auto copyPipelineResult = renderer.InitializePipelines({&namedPipeline, 1}, cache);
        if (copyPipelineResult != rendering::RenderPipelineResult::Success)
        {
            std::printf("[rhiNvrhiTests] copy pipeline: %s\n", rendering::ToString(copyPipelineResult));
            rendering::PipelineRequest evidence;
            if (rendering::RequestRenderPipeline(request, cache, evidence) == rendering::RenderPipelineResult::Success)
            {
                evidence.Wait();
                std::printf("[rhiNvrhiTests] native copy pipeline: %s\n", evidence.GetError().message);
            }
            return false;
        }
        const u8 pixels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
        gpu::TextureDesc textureDesc;
        textureDesc.extent = {2,2,1};
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::RenderTarget | gpu::TextureUsage::CopySource;
        const gpu::TextureSubresourceData data{pixels, sizeof(pixels), 8, 16, 0, 0};
        gpu::Texture source(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {&data, 1}));
        gpu::Texture target(gpu::AdoptReference, gpu::CreateTexture(textureDesc));
        gpu::SamplerStateDesc samplerDesc;
        samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = gpu::SamplerAddressMode::Clamp;
        gpu::SamplerState sampler(gpu::AdoptReference, gpu::RequestSamplerState(samplerDesc));
        const auto srv = gpu::AllocateDescriptor(resourcesDomain);
        const auto samplerHandle = gpu::AllocateDescriptor(samplersDomain);
        if (!source || !target || !sampler || !gpu::WriteDescriptor(resourcesDomain, srv, source, gpu::BindingType::TextureShaderResource) || !gpu::WriteDescriptor(samplersDomain, samplerHandle, sampler))
            return false;
        rendering::RenderFlowResourceAllocator allocator;
        if (!allocator.Initialize() || !allocator.BeginFrame(1, {}))
            return false;
        rendering::RetainedTextureImportDesc importedDesc;
        importedDesc.token = {1}; importedDesc.texture = source; importedDesc.expected = textureDesc;
        rendering::ImportedResourceId sourceImport, targetImport;
        if (!allocator.RegisterImport(importedDesc, sourceImport)) return false;
        importedDesc.token = {2}; importedDesc.texture = target;
        if (!allocator.RegisterImport(importedDesc, targetImport)) return false;
        {
            rendering::NodesContainer nodes;
            rendering::RenderNodeGraph graph;
            rendering::FullscreenCopyDesc copy;
            copy.sourceName = "Source"; copy.targetName = "Target"; copy.source = source;
            copy.sourceDescriptor = srv; copy.samplerDescriptor = samplerHandle;
            {
                rendering::NodeGraphFactory factory(&graph, &nodes);
                static_cast<void>(factory.BeginCommandListGroup(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), "CopyFixture"));
                static_cast<void>(factory.Create<ImportTexturePairNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), sourceImport, targetImport));
                static_cast<void>(factory.Create<rendering::RenderNodeFullscreenCopy>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), renderer, copy));
                factory.EndCommandListGroup();
            }
            graph.BuildRenderFlowGroups();
            if (!RunSingleScopeGraph(graph, allocator)) { std::printf("[rhiNvrhiTests] copy graph runner failed\n"); return false; }
        }
        auto commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0);
        if (!gpu::BindCommandList(commands) || !gpu::TransitionTexture(target, gpu::ResourceState::Unknown, gpu::ResourceState::CopySource)) return false;
        gpu::TextureReadback readback(gpu::AdoptReference, gpu::RequestTextureReadback(target));
        gpu::UnbindCommandList();
        gpu::GpuFence fence;
        if (!readback || !gpu::CloseAndSubmitCommandLists("copy pixels", {&commands, 1}, gpu::CommandListSyncType::None, fence) || !gpu::WaitForGpuFence(fence, 5'000'000'000ull)) return false;
        gpu::TextureReadbackMapping mapping;
        if (!gpu::MapTextureReadback(readback, mapping)) return false;
        bool correct = mapping.format == gpu::Format::R8G8B8A8UNorm && mapping.extent.width == 2 && mapping.extent.height == 2 && mapping.rowPitch >= 8 && mapping.dataSize >= mapping.rowPitch + 8;
        for (u32 y = 0; correct && y < 2; ++y)
            for (u32 x = 0; x < 8; ++x)
                correct = correct && static_cast<const u8*>(mapping.data)[y * mapping.rowPitch + x] == pixels[y * 8 + x];
        if (!gpu::UnmapTextureReadback(readback) || !allocator.Shutdown()) return false;
        renderer.ClearPipelines();
        if (!renderer.ShutdownResourceAllocator() || !cache.Shutdown()) return false;
        if (!correct) std::printf("[rhiNvrhiTests] copy pixels differ\n");
        return gpu::RetireDescriptor(resourcesDomain, srv, {}) && gpu::RetireDescriptor(samplersDomain, samplerHandle, {}) && correct;
    }

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
        using CompileFunction = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
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
        const HRESULT result =
            compile != nullptr ? compile(source, sourceSize, "rhi_pipeline_test", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &output.bytecode, &errors) : E_NOINTERFACE;
        if (errors != nullptr)
            errors->Release();
        return SUCCEEDED(result) && output.bytecode != nullptr;
    }

    template <typename T> [[nodiscard]] bool ReadGpuSceneElement(rendering::GpuSceneTables& tables, const vanguard::u32 index, T& output, gpu::Failure& failure, const vanguard::u64 commandName) noexcept
    {
        rendering::GpuSceneElementAddress address;
        if (!tables.Resolve<T>(index, address))
            return false;
        gpu::BufferDesc desc;
        desc.size = sizeof(T);
        desc.usage = gpu::BufferUsage::CopyDestination;
        desc.initialState = gpu::ResourceState::CopyDestination;
        desc.memoryType = gpu::MemoryType::Readback;
        gpu::BufferRef readback = gpu::CreateBuffer(desc, {}, &failure);
        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::CopySync, commandName, &failure);
        const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        if (!readback || !commands || !gpu::BindCommandList(commands, &failure) || !gpu::TransitionBuffer(address.buffer, gpu::ResourceState::Unknown, gpu::ResourceState::CopySource, &failure) ||
            !gpu::CopyBuffer(readback, 0, address.buffer, address.byteOffset, sizeof(T), &failure) || !gpu::TransitionBuffer(address.buffer, gpu::ResourceState::Unknown, shaderRead, &failure))
            return false;
        gpu::UnbindCommandList();
        const gpu::CommandListRef submissions[] = {commands};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("GPU Scene element readback", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) ||
            !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return false;
        const T* const mapped = static_cast<const T*>(gpu::LockBuffer(readback, 0, sizeof(T), &failure));
        if (mapped == nullptr)
            return false;
        output = *mapped;
        gpu::UnlockBuffer(readback);
        static_cast<void>(gpu::SafeRelease(readback));
        return true;
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

    // Exercises the common tracker without a native command list. In particular,
    // close/open without execution must abandon deferred publications and all
    // pointer-keyed state from the preceding recording.
    [[nodiscard]] bool TestNvrhiTrackerRecordingEpoch()
    {
        struct Messages final : nvrhi::IMessageCallback
        {
            unsigned errors = 0;
            void message(nvrhi::MessageSeverity severity, const char*) override
            {
                if (severity == nvrhi::MessageSeverity::Error)
                    ++errors;
            }
        } messages;

        nvrhi::CommandListResourceStateTracker tracker(&messages);
        nvrhi::BufferDesc bufferDesc{};
        nvrhi::BufferStateExtension buffer(bufferDesc);
        nvrhi::TextureDesc textureDesc{};
        textureDesc.keepInitialState = true;
        textureDesc.initialState = nvrhi::ResourceStates::Common;
        nvrhi::TextureStateExtension texture(textureDesc);

        tracker.beginRecording();
        tracker.beginTrackingBufferState(&buffer, nvrhi::ResourceStates::Common);
        tracker.setPermanentBufferState(&buffer, nvrhi::ResourceStates::CopyDest);
        tracker.requireTextureState(&texture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);
        tracker.keepTextureInitialStates();
        tracker.keepBufferInitialStates();
        // Abandon this closed, never-executed recording and then submit an
        // unrelated recording: neither global resource may be modified.
        tracker.beginRecording();
        if (tracker.getBufferState(&buffer) != nvrhi::ResourceStates::Unknown)
            return false;
        tracker.commandListSubmitted();
        if (buffer.permanentState != nvrhi::ResourceStates::Unknown || texture.stateInitialized)
            return false;

        tracker.beginRecording();
        tracker.beginTrackingBufferState(&buffer, nvrhi::ResourceStates::Common);
        tracker.setPermanentBufferState(&buffer, nvrhi::ResourceStates::CopyDest);
        tracker.requireTextureState(&texture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);
        tracker.keepTextureInitialStates();
        tracker.keepBufferInitialStates();
        tracker.commandListSubmitted();
        if (buffer.permanentState != nvrhi::ResourceStates::CopyDest || !texture.stateInitialized)
            return false;

        // Recycle exactly the same address with a different logical buffer.
        // A retained stale key must not make the new recording inherit state.
        alignas(nvrhi::BufferStateExtension) unsigned char slot[sizeof(nvrhi::BufferStateExtension)];
        tracker.beginRecording();
        auto* first = new (slot) nvrhi::BufferStateExtension(bufferDesc);
        tracker.beginTrackingBufferState(first, nvrhi::ResourceStates::CopySource);
        first->~BufferStateExtension();
        tracker.beginRecording();
        auto* second = new (slot) nvrhi::BufferStateExtension(bufferDesc);
        const bool pristine = tracker.getBufferState(second) == nvrhi::ResourceStates::Unknown;
        tracker.beginTrackingBufferState(second, nvrhi::ResourceStates::Common);
        const bool reset = tracker.getBufferState(second) == nvrhi::ResourceStates::Common;
        second->~BufferStateExtension();
        tracker.beginRecording();
        if (!pristine || !reset)
            return false;

        // Exercise the bounded-table overflow path without imposing a
        // semantic limit on the number of resources in one recording.
        constexpr size_t StressBuffers = 4096;
        std::vector<nvrhi::BufferStateExtension> extensions;
        extensions.reserve(StressBuffers);
        for (size_t i = 0; i < StressBuffers; ++i)
        {
            extensions.emplace_back(bufferDesc);
            tracker.beginTrackingBufferState(&extensions.back(), nvrhi::ResourceStates::Common);
        }
        for (auto& extension : extensions)
        {
            if (tracker.getBufferState(&extension) != nvrhi::ResourceStates::Common)
                return false;
        }
        tracker.commandListSubmitted();
        return messages.errors == 0;
    }

    [[nodiscard]] bool TestResourceLifetimeManager() noexcept
    {
        FenceHarness fences{};
        DestructionHarness destructions{};
        gpu::backend::ResourceLifetimeManager lifetime;
        if (!lifetime.Initialize(SmallLifetimeConfig(), &IsFenceComplete, &fences))
            return false;

        const gpu::ResourceRef first = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!first || !lifetime.IsValid(first) || lifetime.GetRefCount(first) != 1 || !lifetime.AddRef(first) || lifetime.GetRefCount(first) != 2 || lifetime.IsNativeReleaseComplete(first) ||
            lifetime.Release(first) != 1 || lifetime.Release(first) != 0 || lifetime.IsValid(first) || lifetime.IsNativeReleaseComplete(first))
            return false;

        lifetime.SealRetirementEpoch({5, 0, 0});
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 0 || lifetime.IsNativeReleaseComplete(first))
            return false;
        fences.completed[0].SetValue(5);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 1 || !lifetime.IsNativeReleaseComplete(first))
            return false;

        const gpu::ResourceRef reused = lifetime.Create(gpu::ResourceKind::Texture, nullptr, &DestroyResource, &destructions);
        if (!reused || reused.Index() != first.Index() || reused.GetGeneration() == first.GetGeneration() || lifetime.AddRef(first) || !lifetime.IsNativeReleaseComplete(first) ||
            lifetime.IsNativeReleaseComplete(reused))
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

        if (!lifetime.RecordUse(reused, gpu::QueueType::Graphics, 7) || !lifetime.RecordUse(reused, gpu::QueueType::Compute, 9) || lifetime.Release(reused) != 0)
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
        if (stats.liveResources != 0 || stats.pendingRetirements != 0 || stats.totalReferences != 0 || stats.completedRetirements != 4 || stats.peakPendingRetirements == 0 ||
            stats.retirementQueueRecoveries != 0 || stats.retirementBucketOverflows != 0 || stats.staleReferenceOperations == 0)
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

    [[nodiscard]] bool TestOwnedSubmissionFencePublication() noexcept
    {
        // A closed command-list payload owns every reference through fence
        // publication and Release. Its owned fast path must be equivalent to
        // RecordUse without an extra transient AddRef/Release for each use.
        FenceHarness fences{};
        DestructionHarness destructions{};
        gpu::backend::ResourceLifetimeManager lifetime;
        if (!lifetime.Initialize(SmallLifetimeConfig(), &IsFenceComplete, &fences))
            return false;

        const gpu::ResourceRef resource = lifetime.Create(gpu::ResourceKind::Buffer, nullptr, &DestroyResource, &destructions);
        if (!resource || lifetime.GetRefCount(resource) != 1 ||
            !lifetime.RecordOwnedUse(resource, gpu::QueueType::Graphics, 4) ||
            !lifetime.RecordOwnedUse(resource, gpu::QueueType::Graphics, 3) ||
            !lifetime.RecordOwnedUse(resource, gpu::QueueType::Compute, 7) ||
            !lifetime.RecordOwnedUse(resource, gpu::QueueType::Copy, 2) ||
            lifetime.GetRefCount(resource) != 1)
            return false;
        const gpu::backend::FenceSet lastUse = lifetime.GetLastUse(resource);
        if (lastUse.graphics != 4 || lastUse.compute != 7 || lastUse.copy != 2 ||
            lifetime.RecordOwnedUse(resource, gpu::QueueType::Graphics, 0) ||
            lifetime.RecordOwnedUse(resource, static_cast<gpu::QueueType>(3), 8) ||
            lifetime.GetRefCount(resource) != 1 || lifetime.Release(resource) != 0)
            return false;

        lifetime.SealRetirementEpoch({4, 7, 2});
        fences.completed[0].SetValue(4);
        fences.completed[1].SetValue(6);
        fences.completed[2].SetValue(2);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 0)
            return false;
        fences.completed[1].SetValue(7);
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        if (destructions.destroyed.GetValue() != 1 || !lifetime.IsNativeReleaseComplete(resource))
            return false;

        const gpu::ResourceRef replacement = lifetime.Create(gpu::ResourceKind::Buffer, nullptr, &DestroyResource, &destructions);
        if (!replacement || replacement.Index() != resource.Index() || replacement.GetGeneration() == resource.GetGeneration() ||
            lifetime.RecordOwnedUse(resource, gpu::QueueType::Graphics, 10) || lifetime.GetLastUse(replacement).graphics != 0 ||
            lifetime.Release(replacement) != 0)
            return false;
        lifetime.SealRetirementEpoch({4, 7, 2});
        lifetime.CollectGarbage();
        lifetime.WaitForReclamation();
        return destructions.destroyed.GetValue() == 2 && lifetime.ShutdownAfterGpuIdle();
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
        return destructions.destroyed.GetValue() == 3 && stats.liveResources == 0 && stats.pendingRetirements == 0 && stats.totalReferences == 0 && lifetime.ShutdownAfterGpuIdle();
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

    [[nodiscard]] bool TestCommandListRetentionPolicy() noexcept
    {
        // Upload-manager users are destroyed by default. Explicit bounded-upload roles may retain their
        // manager, but only after the normal GPU-completion boundary has made the native list recyclable.
        return gpu::backend::ShouldRecycleCommandList(true, false, gpu::CommandListRetentionPolicy::Default) &&
               !gpu::backend::ShouldRecycleCommandList(false, false, gpu::CommandListRetentionPolicy::Default) &&
               !gpu::backend::ShouldRecycleCommandList(true, true, gpu::CommandListRetentionPolicy::Default) &&
               gpu::backend::ShouldRecycleCommandList(true, true, gpu::CommandListRetentionPolicy::BoundedUploadReuse) &&
               !gpu::backend::ShouldRecycleCommandList(false, true, gpu::CommandListRetentionPolicy::BoundedUploadReuse);
    }

    [[nodiscard]] bool TestCommandListLeaseLifecycle(gpu::Failure& failure) noexcept
    {
        constexpr vanguard::u64 BoundedRole = 0x434d444c424f554eull;

        gpu::Failure boundedRoleFailure{};
        if (gpu::CreateCommandList(gpu::CommandListType::Default, 0, &boundedRoleFailure, gpu::CommandListRetentionPolicy::BoundedUploadReuse) ||
            boundedRoleFailure.code != gpu::FailureCode::InvalidArgument)
            return false;

        gpu::BufferDesc uploadDesc{};
        uploadDesc.size = 4u * 1024u * 1024u;
        uploadDesc.usage = gpu::BufferUsage::CopyDestination;
        uploadDesc.initialState = gpu::ResourceState::CopyDestination;
        gpu::Buffer upload(gpu::AdoptReference, gpu::CreateBuffer(uploadDesc, {}, &failure));
        if (!upload)
            return false;

        vanguard::u32 payload[64u * 1024u]{};
        for (vanguard::u32 index = 0; index < static_cast<vanguard::u32>(sizeof(payload) / sizeof(payload[0])); ++index)
            payload[index] = index;

        gpu::CommandListRef bounded =
            gpu::CreateCommandList(gpu::CommandListType::Default, BoundedRole, &failure, gpu::CommandListRetentionPolicy::BoundedUploadReuse);
        if (!bounded || !gpu::BindCommandList(bounded, &failure))
            return false;
        for (vanguard::u64 offset = 0; offset < uploadDesc.size; offset += sizeof(payload))
        {
            if (!gpu::WriteBuffer(upload.GetRef(), payload, sizeof(payload), offset, &failure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(bounded);
                return false;
            }
        }
        gpu::UnbindCommandList();

        const gpu::CommandListRef submittedStale = bounded;
        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{bounded};
        if (!gpu::CloseAndSubmitCommandLists("bounded command-list lease lifecycle", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) || !completion.IsValid())
            return false;

        gpu::Failure staleFailure{};
        if (gpu::BindCommandList(submittedStale, &staleFailure) || staleFailure.code != gpu::FailureCode::InvalidCommandList)
            return false;

        // This must reacquire the matching sticky bounded outer. Discard is deliberately
        // immediate: if the prior submission is still in flight, Gate 3 must retire the
        // outer against the carried prior completion fence rather than destroy it early or
        // put the unexecuted recording back into the hot pool.
        gpu::CommandListRef reused =
            gpu::CreateCommandList(gpu::CommandListType::Default, BoundedRole, &failure, gpu::CommandListRetentionPolicy::BoundedUploadReuse);
        if (!reused)
            return false;
        const gpu::CommandListRef discardedStale = reused;
        gpu::DiscardCommandList(reused);
        if (reused.IsValid())
            return false;
        staleFailure = {};
        if (gpu::BindCommandList(discardedStale, &staleFailure) || staleFailure.code != gpu::FailureCode::InvalidCommandList)
            return false;

        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || !gpu::RetireResources(&failure))
            return false;

        gpu::CommandListRef clean = gpu::CreateCommandList(gpu::CommandListType::Default, 0x434d444c434c454eull, &failure);
        if (!clean)
            return false;
        const gpu::CommandListRef cleanStale = clean;
        gpu::DiscardCommandList(clean);
        staleFailure = {};
        return !clean.IsValid() && !gpu::BindCommandList(cleanStale, &staleFailure) && staleFailure.code == gpu::FailureCode::InvalidCommandList;
    }

    [[nodiscard]] bool TestNativeCommandListSubmissionBatchSizes(gpu::Failure& failure) noexcept
    {
        // RF18: the common native submission uses inline pointer storage, but
        // batches larger than that inline capacity must retain identical order,
        // submission, and retirement semantics through the dynamic fallback.
        // The physical residency allocation is deliberately shared by every
        // command list: the submit-wide union must prepare it exactly once.
        constexpr vanguard::u32 MaximumTestBatch = 32;
        constexpr vanguard::u32 BatchSizes[]{1, 16, 17, MaximumTestBatch};
        gpu::BufferDesc desc{};
        desc.size = 256;
        desc.usage = gpu::BufferUsage::CopyDestination | gpu::BufferUsage::CopySource;
        desc.initialState = gpu::ResourceState::Common;
        gpu::Buffer shared(gpu::AdoptReference, gpu::CreateBuffer(desc, {}, &failure));
        if (!shared)
            return false;
        const gpu::ResourceRef residencyResource(shared.GetRef());
        for (const vanguard::u32 batchSize : BatchSizes)
        {
            gpu::CommandListRef commands[MaximumTestBatch]{};
            for (vanguard::u32 index = 0; index < batchSize; ++index)
            {
                commands[index] = gpu::CreateCommandList(gpu::CommandListType::Default,
                                                          0x5246313842415443ull + batchSize * MaximumTestBatch + index, &failure);
                if (!commands[index] || !gpu::BindCommandList(commands[index], &failure) ||
                    !gpu::AdmitCommandListResources({&residencyResource, 1}, &failure))
                {
                    gpu::UnbindCommandList();
                    for (gpu::CommandListRef& command : commands)
                        gpu::DiscardCommandList(command);
                    return false;
                }
                gpu::UnbindCommandList();
            }
            const gpu::CommandListRef stale = commands[batchSize - 1];
            const gpu::ResidencyStats before = gpu::GetResidencyStats();
            gpu::GpuFence completion{};
            if (!gpu::CloseAndSubmitCommandLists("RF18 native command-list batch", {commands, batchSize}, gpu::CommandListSyncType::None,
                                                 completion, &failure) || !completion.IsValid() ||
                !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
                return false;
            const gpu::ResidencyStats after = gpu::GetResidencyStats();
            if (after.automaticWorkingSetChecks != before.automaticWorkingSetChecks + 1u)
            {
                std::printf("[rhiNvrhiTests] residency union expected one physical working-set check for %u command lists; observed %llu\n",
                            batchSize, static_cast<unsigned long long>(after.automaticWorkingSetChecks - before.automaticWorkingSetChecks));
                return false;
            }
            gpu::Failure staleFailure{};
            if (gpu::BindCommandList(stale, &staleFailure) || staleFailure.code != gpu::FailureCode::InvalidCommandList)
                return false;
        }
        // A physical allocation referenced by graphics and async compute is
        // prepared once globally, but each queue still must publish its own
        // completion fence so neither queue's lifetime evidence is lost.
        gpu::CommandListRef crossQueue[] = {
            gpu::CreateCommandList(gpu::CommandListType::Default, 0x524643524f535347ull, &failure),
            gpu::CreateCommandList(gpu::CommandListType::Compute, 0x524643524f535343ull, &failure)};
        for (gpu::CommandListRef command : crossQueue)
        {
            if (!command || !gpu::BindCommandList(command, &failure) ||
                !gpu::AdmitCommandListResources({&residencyResource, 1}, &failure))
            {
                gpu::UnbindCommandList();
                for (gpu::CommandListRef discarded : crossQueue)
                    gpu::DiscardCommandList(discarded);
                return false;
            }
            gpu::UnbindCommandList();
        }
        const gpu::ResidencyStats beforeCrossQueue = gpu::GetResidencyStats();
        gpu::SubmissionReceipt crossQueueReceipt{};
        if (!gpu::CloseAndSubmitCommandLists("RF residency graphics+compute union", {crossQueue, 2},
                                             gpu::CommandListSyncType::ForkAsyncCompute, crossQueueReceipt, &failure) ||
            !crossQueueReceipt.IsValid() || crossQueueReceipt.residency.graphics == 0 || crossQueueReceipt.residency.compute == 0 ||
            crossQueueReceipt.completion.queue != gpu::QueueType::Compute ||
            !gpu::WaitForGpuFence({gpu::QueueType::Graphics, crossQueueReceipt.residency.graphics}, 5'000'000'000ull, &failure) ||
            !gpu::WaitForGpuFence(crossQueueReceipt.completion, 5'000'000'000ull, &failure))
            return false;
        const gpu::ResidencyStats afterCrossQueue = gpu::GetResidencyStats();
        if (afterCrossQueue.automaticWorkingSetChecks != beforeCrossQueue.automaticWorkingSetChecks + 1u)
        {
            std::printf("[rhiNvrhiTests] graphics+compute shared allocation was prepared more than once\n");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool TestManualTextureUploadTransition(gpu::Failure& failure) noexcept
    {
        // Streaming candidate textures declare keepInitialState with a shader-read entry state. With NVRHI
        // automatic barriers disabled, a raw WriteTexture copy requires an explicit transition of the exact
        // subresource to CopyDestination; the command list restores the initial state on close.
        vanguard::u8 pixels[4u * 4u * 4u]{};
        for (vanguard::u32 index = 0; index < sizeof(pixels); ++index)
            pixels[index] = static_cast<vanguard::u8>(index);
        const gpu::TextureSubresourceData upload{pixels, sizeof(pixels), 16, 0, 0, 0};
        gpu::TextureDesc desc{};
        desc.extent = {4, 4, 1};
        desc.format = gpu::Format::R8G8B8A8UNorm;
        desc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        desc.initialState = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(desc, {}, &failure));
        if (!texture)
            return false;

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::CopySync, 0x4d414e55504c4f41ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
        {
            gpu::DiscardCommandList(commandList);
            return false;
        }
        const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        const gpu::SubresourceRange range{0, 1, 0, 1};
        if (!gpu::TransitionTexture(texture.GetRef(), shaderRead, gpu::ResourceState::CopyDestination, range, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        // Prove the transition was materialized in NVRHI's state tracker: repeating the same entry-state
        // transition must now be rejected as a mismatch. A no-op transition would pass both calls.
        gpu::Failure repeatFailure{};
        if (gpu::TransitionTexture(texture.GetRef(), shaderRead, gpu::ResourceState::CopyDestination, range, &repeatFailure) ||
            repeatFailure.code != gpu::FailureCode::ResourceStateMismatch || !gpu::WriteTexture(texture.GetRef(), upload, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        gpu::UnbindCommandList();
        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{commandList};
        return gpu::CloseAndSubmitCommandLists("manual texture upload", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) &&
               gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure);
    }

    [[nodiscard]] bool TestLazyBarrierCommitFence(gpu::Failure& failure) noexcept
    {
        const auto failed = [&failure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rhiNvrhiTests] lazy barrier fence failed at %s: code=%u message=%s\n", stage, static_cast<unsigned>(failure.code), failure.message);
            return false;
        };

        gpu::BufferDesc bufferDesc{};
        bufferDesc.size = 256;
        bufferDesc.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination | gpu::BufferUsage::ShaderResource |
                           gpu::BufferUsage::UnorderedAccess | gpu::BufferUsage::IndirectArguments;
        bufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer buffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &failure));
        if (!buffer)
            return failed("buffer creation");

        gpu::TextureDesc textureDesc{};
        textureDesc.extent = {4, 4, 1};
        textureDesc.mipCount = 2;
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination | gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        textureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {}, &failure));
        gpu::QueryPool timestamps(gpu::AdoptReference, gpu::CreateQueryPool({gpu::QueryType::Timestamp, 1}, &failure));
        if (!texture || !timestamps)
            return failed("texture/query creation");

        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x4c415a5942415252ull, &failure);
        if (!commands || !gpu::BindCommandList(commands, &failure))
            return failed("command-list bind");

        const gpu::ResourceState combinedRead = gpu::ResourceState::ShaderResourceCompute | gpu::ResourceState::IndirectArgument;
        const gpu::SubresourceRange mipZero{0, 1, 0, 1};
        const gpu::SubresourceRange mipOne{1, 1, 0, 1};
        const bool recorded =
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::CopyDestination, gpu::ResourceState::CopySource, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, combinedRead, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), combinedRead, gpu::ResourceState::Common, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, &failure) &&
            gpu::BarrierBufferUav(buffer.GetRef(), &failure) &&
            gpu::BarrierBufferUav(buffer.GetRef(), &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::UnorderedAccess, gpu::ResourceState::Common, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, mipZero, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, mipOne, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::CopyDestination, gpu::ResourceState::CopySource, mipZero, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, mipZero, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, mipOne, &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, {}, &failure) &&
            gpu::BarrierTextureUav(texture.GetRef(), &failure) &&
            gpu::BarrierTextureUav(texture.GetRef(), &failure) &&
            gpu::TransitionTexture(texture.GetRef(), gpu::ResourceState::UnorderedAccess, gpu::ResourceState::Common, {}, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, &failure) &&
            gpu::IssueQuery(timestamps.GetRef(), 0, &failure) &&
            gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, &failure);
        if (!recorded)
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("ordered transition/query recording");
        }

        // Cross several 64-request fence batches and leave all buffers away from
        // their automatic Common close state. This also exercises Vanguard's
        // bounded pre-close restoration instead of NVRHI's unbounded tail batch.
        gpu::Buffer boundedBuffers[320];
        for (vanguard::u32 index = 0; index < 320; ++index)
        {
            boundedBuffers[index] = gpu::Buffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &failure));
            if (!boundedBuffers[index] || !gpu::TransitionBuffer(boundedBuffers[index].GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, &failure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(commands);
                return failed("bounded distinct-buffer batch");
            }
        }
        if (!gpu::ResolveQueries(timestamps.GetRef(), 0, 1, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("query resolve boundary");
        }
        gpu::UnbindCommandList();

        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{commands};
        if (!gpu::CloseAndSubmitCommandLists("lazy barrier commit fence", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) ||
            !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return failed("submission");
        if (!gpu::AcquireQueries(timestamps.GetRef(), 0, 1, &failure))
            return failed("query acquire");
        vanguard::u64 timestamp = 0;
        const bool result = gpu::GetQueryResult(timestamps.GetRef(), 0, timestamp, &failure);
        gpu::ReleaseQueries(timestamps.GetRef());
        return result || failed("query result");
    }

    [[nodiscard]] bool TestCompiledNativeBarrierBatch(gpu::Failure& failure) noexcept
    {
        const auto failed = [&failure](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rhiNvrhiTests] compiled native barrier batch failed at %s: code=%u message=%s\n", stage,
                         static_cast<unsigned>(failure.code), failure.message);
            return false;
        };

        gpu::BufferDesc bufferDesc{};
        bufferDesc.size = 256;
        bufferDesc.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination | gpu::BufferUsage::UnorderedAccess;
        bufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer buffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &failure));

        gpu::TextureDesc textureDesc{};
        textureDesc.extent = {8, 8, 1};
        textureDesc.mipCount = 2;
        textureDesc.format = gpu::Format::R8G8B8A8UNorm;
        textureDesc.usage = gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination | gpu::TextureUsage::UnorderedAccess;
        textureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {}, &failure));
        if (!buffer || !texture)
            return failed("resource creation");

        gpu::ResourceRef admitted[]{gpu::ResourceRef(buffer.GetRef()), gpu::ResourceRef(texture.GetRef())};
        if (admitted[1].value < admitted[0].value)
        {
            const gpu::ResourceRef swap = admitted[0];
            admitted[0] = admitted[1];
            admitted[1] = swap;
        }
        const vanguard::u32 bufferBinding = admitted[0] == gpu::ResourceRef(buffer.GetRef()) ? 0u : 1u;
        const vanguard::u32 textureBinding = 1u - bufferBinding;

        gpu::CommandListRef commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246313642415443ull, &failure);
        if (!commands || !gpu::BindCommandList(commands, &failure))
            return failed("command-list bind");

        const gpu::CompiledBarrier preAdmissionBarrier{
            0u, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, {}};
        if (gpu::ExecuteCompiledBarriers({&preAdmissionBarrier, 1}, &failure) || failure.code != gpu::FailureCode::InvalidCommandList)
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("pre-admission rejection");
        }

        if (!gpu::AdmitCommandListResources({admitted, 2}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("resource admission");
        }

        const gpu::SubresourceRange mipZero{0, 1, 0, 1};
        const gpu::SubresourceRange wholeTexture{0, 2, 0, 1};
        gpu::CommandListEntryState entryStates[3]{};
        if (bufferBinding == 0)
        {
            entryStates[0] = {gpu::ResourceRef(buffer.GetRef()), gpu::ResourceState::Common, {}};
            entryStates[1] = {gpu::ResourceRef(texture.GetRef()), gpu::ResourceState::Common, {0, 0}};
            entryStates[2] = {gpu::ResourceRef(texture.GetRef()), gpu::ResourceState::Common, {1, 0}};
        }
        else
        {
            entryStates[0] = {gpu::ResourceRef(texture.GetRef()), gpu::ResourceState::Common, {0, 0}};
            entryStates[1] = {gpu::ResourceRef(texture.GetRef()), gpu::ResourceState::Common, {1, 0}};
            entryStates[2] = {gpu::ResourceRef(buffer.GetRef()), gpu::ResourceState::Common, {}};
        }
        if (!gpu::SeedCommandListStates({entryStates, 3}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("entry-state seed");
        }

        const gpu::CompiledBarrier badBefore[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::CopySource, gpu::ResourceState::CopyDestination, {}}};
        if (gpu::ExecuteCompiledBarriers({badBefore, 1}, &failure) || failure.code != gpu::FailureCode::ResourceStateMismatch)
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("before-state rejection");
        }

        const gpu::CompiledBarrier firstBatch[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, {}},
            {textureBinding, gpu::CompiledBarrierKind::TextureTransition, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, mipZero}};
        const gpu::CompiledBarrier secondBatch[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::CopyDestination, gpu::ResourceState::UnorderedAccess, {}},
            {textureBinding, gpu::CompiledBarrierKind::TextureTransition, gpu::ResourceState::CopyDestination, gpu::ResourceState::Common, mipZero}};
        const gpu::CompiledBarrier bufferUav[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferUav, gpu::ResourceState::Unknown, gpu::ResourceState::Unknown, {}}};
        const gpu::CompiledBarrier bufferRestore[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::UnorderedAccess, gpu::ResourceState::Common, {}}};
        const gpu::CompiledBarrier textureToUav[]{
            {textureBinding, gpu::CompiledBarrierKind::TextureTransition, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, wholeTexture}};
        const gpu::CompiledBarrier textureUav[]{
            {textureBinding, gpu::CompiledBarrierKind::TextureUav, gpu::ResourceState::Unknown, gpu::ResourceState::Unknown, {}}};
        const gpu::CompiledBarrier textureRestore[]{
            {textureBinding, gpu::CompiledBarrierKind::TextureTransition, gpu::ResourceState::UnorderedAccess, gpu::ResourceState::Common, wholeTexture}};
        if (!gpu::ExecuteCompiledBarriers({firstBatch, 2}, &failure) || !gpu::ExecuteCompiledBarriers({secondBatch, 2}, &failure) ||
            !gpu::ExecuteCompiledBarriers({bufferUav, 1}, &failure) || !gpu::ExecuteCompiledBarriers({bufferRestore, 1}, &failure) ||
            !gpu::ExecuteCompiledBarriers({textureToUav, 1}, &failure) || !gpu::ExecuteCompiledBarriers({textureUav, 1}, &failure) ||
            !gpu::ExecuteCompiledBarriers({textureRestore, 1}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("native batch execution");
        }

        // Generic tracked work remains valid after the native batch because RF16
        // republishes each result into NVRHI's tracker without adding a barrier.
        if (!gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, &failure) ||
            !gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::CopySource, gpu::ResourceState::Common, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("compiled-to-tracked interoperability");
        }
        gpu::UnbindCommandList();
        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{commands};
        if (!gpu::CloseAndSubmitCommandLists("compiled native barrier batch", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) ||
            !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return failed("compiled batch submission");

        commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x524631364d49584full, &failure);
        if (!commands || !gpu::BindCommandList(commands, &failure) || !gpu::AdmitCommandListResources({admitted, 2}, &failure) ||
            !gpu::SeedCommandListStates({entryStates, 3}, &failure) ||
            !gpu::TransitionBuffer(buffer.GetRef(), gpu::ResourceState::Common, gpu::ResourceState::CopySource, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("tracked-mode setup");
        }
        const gpu::CompiledBarrier trackedToCompiled[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::CopySource, gpu::ResourceState::Common, {}}};
        if (!gpu::ExecuteCompiledBarriers({trackedToCompiled, 1}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("tracked-to-compiled interoperability");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef secondSubmissions[]{commands};
        if (!gpu::CloseAndSubmitCommandLists("tracked to compiled native barrier", {secondSubmissions, 1}, gpu::CommandListSyncType::None, completion, &failure) ||
            !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return failed("tracked-to-compiled submission");

        // A native run is intentionally dependency-free. Prove the backend rejects
        // two barriers for one binding and then discard the failed command list.
        commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5246313644455044ull, &failure);
        if (!commands || !gpu::BindCommandList(commands, &failure) || !gpu::AdmitCommandListResources({admitted, 2}, &failure) ||
            !gpu::SeedCommandListStates({entryStates, 3}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("dependent-run setup");
        }
        const gpu::CompiledBarrier dependentBarriers[]{
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, {}},
            {bufferBinding, gpu::CompiledBarrierKind::BufferTransition, gpu::ResourceState::CopyDestination, gpu::ResourceState::UnorderedAccess, {}}};
        const bool rejected = !gpu::ExecuteCompiledBarriers({dependentBarriers, 2}, &failure) && failure.code == gpu::FailureCode::InvalidArgument;
        gpu::UnbindCommandList();
        gpu::DiscardCommandList(commands);
        if (!rejected)
            return failed("dependent-run rejection");

        // RF17 can emit a UAV ordering barrier as the first operation for an
        // automatic resource whose immutable initial state is already UAV. NVRHI
        // deliberately leaves that automatic state implicit/Unknown until first
        // use; compiled validation must resolve it from the descriptor rather than
        // reject a correct packet-local before-state proof.
        gpu::TextureDesc implicitTextureDesc{};
        implicitTextureDesc.extent = {8, 8, 1};
        implicitTextureDesc.mipCount = 2;
        implicitTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        implicitTextureDesc.usage = gpu::TextureUsage::UnorderedAccess;
        implicitTextureDesc.initialState = gpu::ResourceState::UnorderedAccess;
        gpu::Texture implicitTexture(gpu::AdoptReference, gpu::CreateTexture(implicitTextureDesc, {}, &failure));
        gpu::BufferDesc implicitBufferDesc{};
        implicitBufferDesc.size = 256;
        implicitBufferDesc.usage = gpu::BufferUsage::UnorderedAccess;
        implicitBufferDesc.initialState = gpu::ResourceState::UnorderedAccess;
        gpu::Buffer implicitBuffer(gpu::AdoptReference, gpu::CreateBuffer(implicitBufferDesc, {}, &failure));
        if (!implicitTexture || !implicitBuffer)
            return failed("implicit UAV resource creation");

        gpu::ResourceRef implicitAdmitted[]{gpu::ResourceRef(implicitTexture.GetRef()), gpu::ResourceRef(implicitBuffer.GetRef())};
        if (implicitAdmitted[1].value < implicitAdmitted[0].value)
        {
            const gpu::ResourceRef swap = implicitAdmitted[0];
            implicitAdmitted[0] = implicitAdmitted[1];
            implicitAdmitted[1] = swap;
        }
        const vanguard::u32 implicitTextureBinding = implicitAdmitted[0] == gpu::ResourceRef(implicitTexture.GetRef()) ? 0u : 1u;
        const vanguard::u32 implicitBufferBinding = 1u - implicitTextureBinding;
        commands = gpu::CreateCommandList(gpu::CommandListType::Default, 0x52463137494d5055ull, &failure);
        if (!commands || !gpu::BindCommandList(commands, &failure) || !gpu::AdmitCommandListResources({implicitAdmitted, 2}, &failure) ||
            !gpu::SeedCommandListStates({}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("implicit UAV setup");
        }
        const gpu::CompiledBarrier implicitUavBarriers[]{
            {implicitTextureBinding, gpu::CompiledBarrierKind::TextureUav, gpu::ResourceState::Unknown, gpu::ResourceState::Unknown, {}},
            {implicitBufferBinding, gpu::CompiledBarrierKind::BufferUav, gpu::ResourceState::Unknown, gpu::ResourceState::Unknown, {}}};
        if (!gpu::ExecuteCompiledBarriers({implicitUavBarriers, 2}, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commands);
            return failed("implicit UAV batch");
        }
        gpu::UnbindCommandList();
        const gpu::CommandListRef implicitSubmission[]{commands};
        if (!gpu::CloseAndSubmitCommandLists("compiled implicit UAV barrier batch", {implicitSubmission, 1}, gpu::CommandListSyncType::None, completion, &failure) ||
            !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return failed("implicit UAV submission");
        return true;
    }

    [[nodiscard]] bool TestBlockCompressedWholeMipCopy(gpu::Failure& failure) noexcept
    {
        // D3D12 requires explicit source boxes for BC formats to use block-aligned edges. A complete mip may
        // instead be represented by a null source box. Keep the top level block-aligned so this proof does not
        // depend on OPTIONS8 unaligned-top-level support while mip 1 intentionally has odd BC dimensions.
        gpu::TextureDesc desc{};
        desc.extent = {228, 148, 1};
        desc.format = gpu::Format::BC7UNorm;
        desc.mipCount = 2;
        desc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource | gpu::TextureUsage::CopyDestination;
        desc.initialState = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        gpu::Texture source(gpu::AdoptReference, gpu::CreateTexture(desc, {}, &failure));
        gpu::Texture destination(gpu::AdoptReference, gpu::CreateTexture(desc, {}, &failure));
        if (!source || !destination)
            return false;

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::CopySync, 0x4243374d49504350ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
        {
            gpu::DiscardCommandList(commandList);
            return false;
        }

        const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        const gpu::SubresourceRange mipOne{1, 1, 0, 1};
        gpu::TextureCopyRegion region{};
        region.source.mipLevel = 1;
        region.destination.mipLevel = 1;
        if (!gpu::TransitionTexture(source.GetRef(), shaderRead, gpu::ResourceState::CopySource, mipOne, &failure) ||
            !gpu::TransitionTexture(destination.GetRef(), shaderRead, gpu::ResourceState::CopyDestination, mipOne, &failure) ||
            !gpu::CopyTexture(destination.GetRef(), source.GetRef(), region, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }

        gpu::UnbindCommandList();
        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{commandList};
        return gpu::CloseAndSubmitCommandLists("BC7 whole odd mip copy", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) &&
               gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure);
    }

    [[nodiscard]] bool TestFlushRetiredResourcesWaitsSubmissionFence(gpu::Failure& failure) noexcept
    {
        // A resource used by submitted GPU work must be natively releasable by FlushRetiredResources without
        // the caller first waiting the submission receipt: the flush owns waiting Vanguard's completion
        // boundary (which NVRHI's own idle fence does not cover) before it drains retirement.
        gpu::BufferDesc desc{};
        desc.size = 256;
        desc.usage = gpu::BufferUsage::CopyDestination;
        desc.initialState = gpu::ResourceState::CopyDestination;
        gpu::Buffer buffer(gpu::AdoptReference, gpu::CreateBuffer(desc, {}, &failure));
        if (!buffer)
            return false;

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x464c555348524554ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
        {
            gpu::DiscardCommandList(commandList);
            return false;
        }
        const vanguard::u32 payload[4]{1u, 2u, 3u, 4u};
        if (!gpu::WriteBuffer(buffer.GetRef(), payload, sizeof(payload), 0, &failure))
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        gpu::UnbindCommandList();
        gpu::GpuFence completion{};
        const gpu::CommandListRef submissions[]{commandList};
        if (!gpu::CloseAndSubmitCommandLists("flush-retired-fence", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure) || !completion.IsValid())
            return false;

        gpu::NativeReleaseObservation observation = gpu::ObserveNativeRelease(gpu::ResourceRef(buffer.GetRef()), &failure);
        if (!observation)
            return false;
        buffer.Reset();
        if (gpu::IsNativeReleaseComplete(observation))
        {
            gpu::ReleaseNativeReleaseObservation(observation);
            return false;
        }
        // Deliberately no WaitForGpuFence/WaitIdle here: the flush must be self-sufficient.
        const bool flushed = gpu::FlushRetiredResources(&failure);
        const bool released = gpu::IsNativeReleaseComplete(observation);
        gpu::ReleaseNativeReleaseObservation(observation);
        return flushed && released;
    }

    [[nodiscard]] bool TestDescriptorDomainReclaim(gpu::Failure& failure) noexcept
    {
        const gpu::Capabilities capabilities = gpu::GetCapabilities();
        if (!capabilities.bindlessResources)
            return true;

        gpu::BufferDesc bufferDesc{};
        bufferDesc.size = 256;
        bufferDesc.structureStride = sizeof(vanguard::u32);
        bufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource;
        bufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer buffer(gpu::AdoptReference, gpu::CreateBuffer(bufferDesc, {}, &failure));
        gpu::DescriptorDomain domain(
            gpu::AdoptReference,
            gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Compute)}, &failure));
        if (!buffer || !domain)
            return false;

        const auto publish = [&](const gpu::DescriptorHandle descriptor) noexcept
        {
            return gpu::WriteDescriptor(domain, descriptor, buffer.GetRef(), gpu::BindingType::StructuredBufferShaderResource, {}, &failure);
        };
        const auto submitMarker = [&](const gpu::CommandListType type, const vanguard::u64 role, const char* const name, gpu::GpuFence& completion) noexcept
        {
            gpu::CommandListRef command = gpu::CreateCommandList(type, role, &failure);
            if (!command)
                return false;
            if (!gpu::BindCommandList(command, &failure))
            {
                gpu::DiscardCommandList(command);
                return false;
            }
            if (!gpu::SetGpuMarker(name, &failure))
            {
                gpu::UnbindCommandList();
                gpu::DiscardCommandList(command);
                return false;
            }
            gpu::UnbindCommandList();
            return gpu::CloseAndSubmitCommandLists(name, {&command, 1}, gpu::CommandListSyncType::None, completion, &failure);
        };

        const gpu::DescriptorHandle first = gpu::AllocateDescriptor(domain, &failure);
        if (!first || !publish(first) || !gpu::RetireDescriptor(domain, first, {}, &failure))
            return false;
        const gpu::DescriptorDomainStats immediateStats = gpu::GetDescriptorDomainStats(domain);
        if (immediateStats.allocated != 0 || immediateStats.populated != 0 || immediateStats.pendingRetirement != 0 || immediateStats.free != 1 || immediateStats.completedRetirements != 1)
            return false;

        const gpu::DescriptorHandle second = gpu::AllocateDescriptor(domain, &failure);
        if (!second || second.index != first.index || second.generation == first.generation)
            return false;
        if (publish(first) || failure.code != gpu::FailureCode::InvalidReference || gpu::RetireDescriptor(domain, first, {}, &failure) || failure.code != gpu::FailureCode::InvalidReference ||
            !gpu::RetireDescriptor(domain, second, {}, &failure))
            return false;

        const gpu::DescriptorHandle delayed = gpu::AllocateDescriptor(domain, &failure);
        if (!delayed || !publish(delayed))
            return false;
        gpu::GpuFence baselineFence{};
        if (!submitMarker(gpu::CommandListType::Default, 0x4453435245434c30ull, "descriptor reclaim baseline", baselineFence) || baselineFence.queue != gpu::QueueType::Graphics)
            return false;
        gpu::DescriptorRetirement delayedRetirement;
        delayedRetirement.Include({gpu::QueueType::Graphics, baselineFence.value + 1u});
        if (!gpu::RetireDescriptor(domain, delayed, delayedRetirement, &failure))
            return false;
        const gpu::DescriptorDomainStats pendingStats = gpu::GetDescriptorDomainStats(domain);
        if (pendingStats.allocated != 0 || pendingStats.populated != 0 || pendingStats.pendingRetirement != 1 || pendingStats.free != 0 || pendingStats.completedRetirements != 2)
            return false;
        if (gpu::AllocateDescriptor(domain, &failure) || failure.code != gpu::FailureCode::CapacityExceeded || gpu::GetDescriptorDomainStats(domain).allocationFailures != 1)
            return false;

        gpu::GpuFence releaseFence{};
        if (!submitMarker(gpu::CommandListType::Default, 0x4453435245434c31ull, "descriptor reclaim release", releaseFence) || releaseFence.queue != gpu::QueueType::Graphics ||
            releaseFence.value < delayedRetirement.graphicsFence || !gpu::WaitForGpuFence(releaseFence, 5'000'000'000ull, &failure))
            return false;

        const gpu::DescriptorHandle reclaimed = gpu::AllocateDescriptor(domain, &failure);
        const gpu::DescriptorDomainStats reclaimedStats = gpu::GetDescriptorDomainStats(domain);
        if (!reclaimed || reclaimed.index != delayed.index || reclaimed.generation == delayed.generation || reclaimedStats.allocated != 1 || reclaimedStats.pendingRetirement != 0 ||
            reclaimedStats.completedRetirements != 3 || reclaimedStats.allocationFailures != 1)
            return false;
        if (publish(delayed) || failure.code != gpu::FailureCode::InvalidReference || gpu::RetireDescriptor(domain, delayed, {}, &failure) || failure.code != gpu::FailureCode::InvalidReference ||
            !gpu::RetireDescriptor(domain, reclaimed, {}, &failure))
            return false;
        const gpu::DescriptorDomainStats finalStats = gpu::GetDescriptorDomainStats(domain);
        if (finalStats.allocated != 0 || finalStats.populated != 0 || finalStats.pendingRetirement != 0 || finalStats.free != 1 || finalStats.completedRetirements != 4 ||
            finalStats.staleHandleOperations < 4 || finalStats.allocationFailures != 1)
            return false;

        const gpu::CommandListType secondaryType = capabilities.asyncCompute ? gpu::CommandListType::Compute : (capabilities.copyQueue ? gpu::CommandListType::CopyAsync : gpu::CommandListType::None);
        if (secondaryType != gpu::CommandListType::None)
        {
            const gpu::QueueType secondaryQueue = gpu::GetQueueType(secondaryType);
            gpu::DescriptorDomain staggeredDomain(
                gpu::AdoptReference,
                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, gpu::ShaderStageBit(gpu::ShaderStage::Compute)}, &failure));
            const gpu::DescriptorHandle middle = gpu::AllocateDescriptor(staggeredDomain, &failure);
            const gpu::DescriptorHandle head = gpu::AllocateDescriptor(staggeredDomain, &failure);
            if (!staggeredDomain || !middle || !head ||
                !gpu::WriteDescriptor(staggeredDomain, middle, buffer.GetRef(), gpu::BindingType::StructuredBufferShaderResource, {}, &failure) ||
                !gpu::WriteDescriptor(staggeredDomain, head, buffer.GetRef(), gpu::BindingType::StructuredBufferShaderResource, {}, &failure))
                return false;

            gpu::GpuFence graphicsBaseline{};
            gpu::GpuFence secondaryBaseline{};
            if (!submitMarker(gpu::CommandListType::Default, 0x445343524d494430ull, "descriptor staggered graphics baseline", graphicsBaseline) || graphicsBaseline.queue != gpu::QueueType::Graphics ||
                !submitMarker(secondaryType, 0x4453435248444230ull, "descriptor staggered secondary baseline", secondaryBaseline) || secondaryBaseline.queue != secondaryQueue)
                return false;

            gpu::DescriptorRetirement middleRetirement;
            middleRetirement.Include({gpu::QueueType::Graphics, graphicsBaseline.value + 1u});
            gpu::DescriptorRetirement headRetirement;
            headRetirement.Include({secondaryQueue, secondaryBaseline.value + 1u});
            if (!gpu::RetireDescriptor(staggeredDomain, middle, middleRetirement, &failure) || !gpu::RetireDescriptor(staggeredDomain, head, headRetirement, &failure))
                return false;
            const gpu::DescriptorDomainStats staggeredPending = gpu::GetDescriptorDomainStats(staggeredDomain);
            if (staggeredPending.allocated != 0 || staggeredPending.populated != 0 || staggeredPending.pendingRetirement != 2 || staggeredPending.free != 0 || staggeredPending.completedRetirements != 0)
                return false;

            gpu::GpuFence graphicsRelease{};
            if (!submitMarker(gpu::CommandListType::Default, 0x445343524d494431ull, "descriptor staggered graphics release", graphicsRelease) ||
                graphicsRelease.queue != gpu::QueueType::Graphics || graphicsRelease.value < middleRetirement.graphicsFence ||
                !gpu::WaitForGpuFence(graphicsRelease, 5'000'000'000ull, &failure))
                return false;
            const gpu::DescriptorHandle middleReclaimed = gpu::AllocateDescriptor(staggeredDomain, &failure);
            const gpu::DescriptorDomainStats middleReclaimedStats = gpu::GetDescriptorDomainStats(staggeredDomain);
            if (!middleReclaimed || middleReclaimed.index != middle.index || middleReclaimed.generation == middle.generation || middleReclaimedStats.allocated != 1 ||
                middleReclaimedStats.pendingRetirement != 1 || middleReclaimedStats.free != 0 || middleReclaimedStats.completedRetirements != 1)
                return false;
            const gpu::DescriptorDomainStats headStillPending = gpu::GetDescriptorDomainStats(staggeredDomain);
            if (headStillPending.allocated != 1 || headStillPending.pendingRetirement != 1 || headStillPending.free != 0 || headStillPending.completedRetirements != 1)
                return false;

            gpu::GpuFence secondaryRelease{};
            if (!submitMarker(secondaryType, 0x4453435248444231ull, "descriptor staggered secondary release", secondaryRelease) || secondaryRelease.queue != secondaryQueue ||
                secondaryRelease.value < secondaryBaseline.value + 1u || !gpu::WaitForGpuFence(secondaryRelease, 5'000'000'000ull, &failure))
                return false;
            const gpu::DescriptorHandle headReclaimed = gpu::AllocateDescriptor(staggeredDomain, &failure);
            const gpu::DescriptorDomainStats headReclaimedStats = gpu::GetDescriptorDomainStats(staggeredDomain);
            if (!headReclaimed || headReclaimed.index != head.index || headReclaimed.generation == head.generation || headReclaimedStats.allocated != 2 ||
                headReclaimedStats.pendingRetirement != 0 || headReclaimedStats.free != 0 || headReclaimedStats.completedRetirements != 2 ||
                !gpu::RetireDescriptor(staggeredDomain, middleReclaimed, {}, &failure) || !gpu::RetireDescriptor(staggeredDomain, headReclaimed, {}, &failure))
                return false;
            const gpu::DescriptorDomainStats staggeredFinal = gpu::GetDescriptorDomainStats(staggeredDomain);
            if (staggeredFinal.allocated != 0 || staggeredFinal.populated != 0 || staggeredFinal.pendingRetirement != 0 || staggeredFinal.free != 2 ||
                staggeredFinal.completedRetirements != 4)
                return false;
        }

        constexpr vanguard::u32 concurrentDescriptorCount = 64;
        gpu::DescriptorDomain concurrentDomain(
            gpu::AdoptReference,
            gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, concurrentDescriptorCount, 0, gpu::ShaderStageBit(gpu::ShaderStage::Compute)}, &failure));
        gpu::DescriptorHandle concurrentDescriptors[concurrentDescriptorCount]{};
        vanguard::concurrency::Atomic<vanguard::u32> concurrentFailures{0};
        vanguard::jobs::Builder builder;
        vanguard::jobs::JobName name{"Vanguard.RHI.DescriptorDomain.RF1ConcurrentAllocation"};
        for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
        {
            vanguard::jobs::Task task = vanguard::jobs::Task::Create(
                [&, index](const vanguard::jobs::JobContext&) noexcept
                {
                    concurrentDescriptors[index] = gpu::AllocateDescriptor(concurrentDomain);
                    if (!concurrentDescriptors[index] ||
                        !gpu::WriteDescriptor(concurrentDomain, concurrentDescriptors[index], buffer.GetRef(), gpu::BindingType::StructuredBufferShaderResource))
                        static_cast<void>(concurrentFailures.Increment());
                });
            if (!builder.Dispatch(name, static_cast<vanguard::jobs::Task&&>(task), vanguard::jobs::Fence::None))
                return false;
        }
        builder.DispatchFence();
        vanguard::jobs::Counter counter = builder.ExtractCounter();
        if (!counter.IsValid() || !counter.Wait() || concurrentFailures.GetValue() != 0)
            return false;
        for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
        {
            for (vanguard::u32 previous = 0; previous < index; ++previous)
                if (concurrentDescriptors[index].index == concurrentDescriptors[previous].index)
                    return false;
        }

        concurrentFailures.SetValue(0);
        vanguard::jobs::Builder retireBuilder;
        vanguard::jobs::JobName retireName{"Vanguard.RHI.DescriptorDomain.RF1ConcurrentRetirement"};
        for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
        {
            vanguard::jobs::Task task = vanguard::jobs::Task::Create(
                [&, index](const vanguard::jobs::JobContext&) noexcept
                {
                    if (!gpu::RetireDescriptor(concurrentDomain, concurrentDescriptors[index], {}))
                        static_cast<void>(concurrentFailures.Increment());
                });
            if (!retireBuilder.Dispatch(retireName, static_cast<vanguard::jobs::Task&&>(task), vanguard::jobs::Fence::None))
                return false;
        }
        retireBuilder.DispatchFence();
        vanguard::jobs::Counter retireCounter = retireBuilder.ExtractCounter();
        if (!retireCounter.IsValid() || !retireCounter.Wait() || concurrentFailures.GetValue() != 0)
            return false;
        const gpu::DescriptorDomainStats retiredStats = gpu::GetDescriptorDomainStats(concurrentDomain);
        if (retiredStats.allocated != 0 || retiredStats.populated != 0 || retiredStats.pendingRetirement != 0 || retiredStats.free != concurrentDescriptorCount ||
            retiredStats.peakAllocated != concurrentDescriptorCount || retiredStats.completedRetirements != concurrentDescriptorCount)
            return false;

        gpu::DescriptorHandle recycledDescriptors[concurrentDescriptorCount]{};
        concurrentFailures.SetValue(0);
        vanguard::jobs::Builder recycleBuilder;
        vanguard::jobs::JobName recycleName{"Vanguard.RHI.DescriptorDomain.RF1ConcurrentRecycle"};
        for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
        {
            vanguard::jobs::Task task = vanguard::jobs::Task::Create(
                [&, index](const vanguard::jobs::JobContext&) noexcept
                {
                    recycledDescriptors[index] = gpu::AllocateDescriptor(concurrentDomain);
                    if (!recycledDescriptors[index] ||
                        !gpu::WriteDescriptor(concurrentDomain, recycledDescriptors[index], buffer.GetRef(), gpu::BindingType::StructuredBufferShaderResource))
                        static_cast<void>(concurrentFailures.Increment());
                });
            if (!recycleBuilder.Dispatch(recycleName, static_cast<vanguard::jobs::Task&&>(task), vanguard::jobs::Fence::None))
                return false;
        }
        recycleBuilder.DispatchFence();
        vanguard::jobs::Counter recycleCounter = recycleBuilder.ExtractCounter();
        if (!recycleCounter.IsValid() || !recycleCounter.Wait() || concurrentFailures.GetValue() != 0)
            return false;
        for (vanguard::u32 index = 0; index < concurrentDescriptorCount; ++index)
        {
            bool advancedGeneration = false;
            for (vanguard::u32 previous = 0; previous < concurrentDescriptorCount; ++previous)
                if (recycledDescriptors[index].index == concurrentDescriptors[previous].index)
                {
                    advancedGeneration = recycledDescriptors[index].generation != concurrentDescriptors[previous].generation;
                    break;
                }
            if (!advancedGeneration)
                return false;
            for (vanguard::u32 previous = 0; previous < index; ++previous)
                if (recycledDescriptors[index].index == recycledDescriptors[previous].index)
                    return false;
            if (!gpu::RetireDescriptor(concurrentDomain, recycledDescriptors[index], {}, &failure))
                return false;
        }
        const gpu::DescriptorDomainStats concurrentStats = gpu::GetDescriptorDomainStats(concurrentDomain);
        return concurrentStats.allocated == 0 && concurrentStats.populated == 0 && concurrentStats.pendingRetirement == 0 && concurrentStats.free == concurrentDescriptorCount &&
               concurrentStats.peakAllocated == concurrentDescriptorCount && concurrentStats.completedRetirements == concurrentDescriptorCount * 2u;
    }

    [[nodiscard]] bool TestNativeResourcesAndSubmission(gpu::Failure& failure) noexcept
    {
        const vanguard::u32 sourceData[] = {0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u};
        gpu::BufferDesc sourceDesc{};
        sourceDesc.size = sizeof(sourceData);
        sourceDesc.structureStride = sizeof(vanguard::u32);
        sourceDesc.usage = gpu::BufferUsage::CopySource | gpu::BufferUsage::CopyDestination | gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        sourceDesc.initialState = gpu::ResourceState::Common;
        gpu::MemoryRequirements sourceRequirements{};
        if (!gpu::GetMemoryRequirements(sourceDesc, sourceRequirements, &failure) || sourceRequirements.size < sourceDesc.size || sourceRequirements.alignment == 0)
            return false;
        gpu::Buffer source(gpu::AdoptReference, gpu::CreateBuffer(sourceDesc, {sourceData, sizeof(sourceData)}, &failure));
        if (!source)
            return false;
        gpu::BufferDesc queriedSourceDesc{};
        if (!gpu::GetBufferDesc(source.GetRef(), queriedSourceDesc, &failure) || queriedSourceDesc.size != sourceDesc.size || queriedSourceDesc.usage != sourceDesc.usage ||
            queriedSourceDesc.structureStride != sourceDesc.structureStride)
            return false;

        gpu::Buffer releaseProbe(gpu::AdoptReference, gpu::CreateBuffer(sourceDesc, {}, &failure));
        const gpu::BufferRef staleReleaseProbe = releaseProbe.GetRef();
        gpu::NativeReleaseObservation releaseObservation = gpu::ObserveNativeRelease(gpu::ResourceRef(releaseProbe.GetRef()), &failure);
        if (!releaseProbe || !releaseObservation || gpu::IsNativeReleaseComplete(releaseObservation))
            return false;
        releaseProbe.Reset();
        gpu::BufferDesc staleDesc = sourceDesc;
        if (gpu::GetBufferDesc(staleReleaseProbe, staleDesc, &failure) || failure.code != gpu::FailureCode::InvalidReference || staleDesc.size != 0 ||
            gpu::ObserveNativeRelease(gpu::ResourceRef(staleReleaseProbe), &failure) || failure.code != gpu::FailureCode::InvalidReference)
            return false;
        if (gpu::IsNativeReleaseComplete(releaseObservation) || !gpu::FlushRetiredResources(&failure) || !gpu::IsNativeReleaseComplete(releaseObservation))
            return false;
        gpu::ReleaseNativeReleaseObservation(releaseObservation);
        if (releaseObservation || gpu::IsNativeReleaseComplete(releaseObservation))
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
        gpu::MemoryRequirements textureRequirements{};
        if (!gpu::GetMemoryRequirements(textureDesc, textureRequirements, &failure) || textureRequirements.size == 0 || textureRequirements.alignment == 0)
            return false;
        gpu::Texture texture(gpu::AdoptReference, gpu::CreateTexture(textureDesc, {&textureSubresource, 1}, &failure));
        if (!texture)
            return false;
        gpu::TextureDesc queriedTextureDesc{};
        if (!gpu::GetTextureDesc(texture.GetRef(), queriedTextureDesc, &failure) || queriedTextureDesc.extent.width != textureDesc.extent.width ||
            queriedTextureDesc.extent.height != textureDesc.extent.height || queriedTextureDesc.extent.depth != textureDesc.extent.depth || queriedTextureDesc.format != textureDesc.format ||
            queriedTextureDesc.usage != textureDesc.usage)
            return false;

        gpu::TextureDesc copySourceDesc = textureDesc;
        copySourceDesc.usage = gpu::TextureUsage::CopySource;
        gpu::Texture copySource(gpu::AdoptReference, gpu::CreateTexture(copySourceDesc, {&textureSubresource, 1}, &failure));
        if (!copySource)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] texture transfer resource creation failed: copy source: %s\n", failure.message);
            return false;
        }
        gpu::TextureDesc copyDestinationDesc = textureDesc;
        copyDestinationDesc.usage = gpu::TextureUsage::CopyDestination | gpu::TextureUsage::ShaderResource;
        gpu::Texture copyDestination(gpu::AdoptReference, gpu::CreateTexture(copyDestinationDesc, {}, &failure));
        if (!copyDestination)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] texture transfer resource creation failed: copy destination: %s\n", failure.message);
            return false;
        }
        gpu::TextureReadback textureReadback;

        gpu::TextureDesc resolveSourceDesc{};
        resolveSourceDesc.extent = {4, 4, 1};
        resolveSourceDesc.format = gpu::Format::R8G8B8A8UNorm;
        resolveSourceDesc.sampleCount = 4;
        resolveSourceDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::ResolveSource;
        gpu::Texture resolveSource(gpu::AdoptReference, gpu::CreateTexture(resolveSourceDesc, {}, &failure));
        if (!resolveSource)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] texture transfer resource creation failed: resolve source: %s\n", failure.message);
            return false;
        }
        gpu::TextureDesc resolveDestinationDesc = resolveSourceDesc;
        resolveDestinationDesc.sampleCount = 1;
        resolveDestinationDesc.usage = gpu::TextureUsage::ResolveDestination | gpu::TextureUsage::ShaderResource;
        gpu::Texture resolveDestination(gpu::AdoptReference, gpu::CreateTexture(resolveDestinationDesc, {}, &failure));
        if (!resolveDestination)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] texture transfer resource creation failed: resolve destination: %s\n", failure.message);
            return false;
        }

        gpu::TextureDesc integerTextureDesc{};
        integerTextureDesc.extent = {4, 4, 1};
        integerTextureDesc.format = gpu::Format::R8G8B8A8UInt;
        integerTextureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
        integerTextureDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture integerTexture(gpu::AdoptReference, gpu::CreateTexture(integerTextureDesc, {}, &failure));
        if (!integerTexture)
        {
            std::printf("[rhiNvrhiTests] integer UAV texture creation failed: %s\n", failure.message);
            return false;
        }

        gpu::TextureDesc renderTargetDesc{};
        renderTargetDesc.extent = {64, 64, 1};
        renderTargetDesc.format = gpu::Format::R8G8B8A8UNorm;
        renderTargetDesc.usage = gpu::TextureUsage::RenderTarget | gpu::TextureUsage::ShaderResource | gpu::TextureUsage::CopySource;
        renderTargetDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture renderTarget(gpu::AdoptReference, gpu::CreateTexture(renderTargetDesc, {}, &failure));
        gpu::TextureDesc depthTargetDesc{};
        depthTargetDesc.extent = {64, 64, 1};
        depthTargetDesc.format = gpu::Format::D24UNormS8UInt;
        depthTargetDesc.usage = gpu::TextureUsage::DepthStencil;
        depthTargetDesc.initialState = gpu::ResourceState::Common;
        gpu::Texture depthTarget(gpu::AdoptReference, gpu::CreateTexture(depthTargetDesc, {}, &failure));
        if (!depthTarget)
        {
            std::printf("[rhiNvrhiTests] depth-stencil texture creation failed: %s\n", failure.message);
            return false;
        }
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
        gpu::Buffer indirectBuffer(gpu::AdoptReference, gpu::CreateBuffer(indirectBufferDesc, {&indirectCommands, sizeof(indirectCommands)}, &failure));
        const vanguard::u32 indirectCountValue = 1;
        gpu::BufferDesc indirectCountBufferDesc{};
        indirectCountBufferDesc.size = sizeof(indirectCountValue);
        indirectCountBufferDesc.usage = gpu::BufferUsage::IndirectArguments;
        indirectCountBufferDesc.initialState = gpu::ResourceState::Common;
        gpu::Buffer indirectCountBuffer(gpu::AdoptReference, gpu::CreateBuffer(indirectCountBufferDesc, {&indirectCountValue, sizeof(indirectCountValue)}, &failure));
        if (!renderTarget || !depthTarget || !vertexBuffer || !indexBuffer || !indirectBuffer || !indirectCountBuffer)
        {
            std::printf("[rhiNvrhiTests] draw resource creation failed: rt=%u vb=%u ib=%u indirect=%u count=%u %s\n", static_cast<unsigned>(static_cast<bool>(renderTarget)),
                        static_cast<unsigned>(static_cast<bool>(vertexBuffer)), static_cast<unsigned>(static_cast<bool>(indexBuffer)), static_cast<unsigned>(static_cast<bool>(indirectBuffer)),
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
        gpu::Buffer computeAliasBufferBefore(gpu::AdoptReference, gpu::CreateBuffer(aliasBufferDesc, {}, &failure));
        gpu::Buffer computeAliasBufferAfter(gpu::AdoptReference, gpu::CreateBuffer(aliasBufferDesc, {}, &failure));
        const gpu::MemoryRequirements aliasRequirements = gpu::GetMemoryRequirements(aliasBufferBefore);
        gpu::Heap aliasHeap(gpu::AdoptReference,
                            gpu::CreateHeap({aliasRequirements.size, aliasRequirements.alignment, aliasRequirements.compatibilityClass, aliasRequirements.memoryType, aliasRequirements.heapCategory}, &failure));
        gpu::Heap computeAliasHeap(
            gpu::AdoptReference,
            gpu::CreateHeap({aliasRequirements.size, aliasRequirements.alignment, aliasRequirements.compatibilityClass, aliasRequirements.memoryType, aliasRequirements.heapCategory}, &failure));
        if (!aliasBufferBefore || !aliasBufferAfter || !computeAliasBufferBefore || !computeAliasBufferAfter || aliasRequirements.size == 0 || !aliasHeap || !computeAliasHeap ||
            !gpu::BindMemory(aliasBufferBefore, aliasHeap, 0, &failure) || !gpu::BindMemory(aliasBufferAfter, aliasHeap, 0, &failure))
            return false;
        if (!gpu::BindMemory(computeAliasBufferBefore, computeAliasHeap, 0, &failure) || !gpu::BindMemory(computeAliasBufferAfter, computeAliasHeap, 0, &failure))
            return false;

        gpu::TextureDesc aliasTextureDesc{};
        aliasTextureDesc.extent = {4, 4, 1};
        aliasTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        aliasTextureDesc.usage = gpu::TextureUsage::RenderTarget;
        aliasTextureDesc.initialState = gpu::ResourceState::Common;
        aliasTextureDesc.virtualResource = true;
        gpu::Texture aliasTextureBefore(gpu::AdoptReference, gpu::CreateTexture(aliasTextureDesc, {}, &failure));
        gpu::Texture aliasTextureAfter(gpu::AdoptReference, gpu::CreateTexture(aliasTextureDesc, {}, &failure));
        const gpu::MemoryRequirements aliasTextureRequirements = gpu::GetMemoryRequirements(aliasTextureBefore);
        gpu::Heap aliasTextureHeap(gpu::AdoptReference, gpu::CreateHeap({aliasTextureRequirements.size, aliasTextureRequirements.alignment, aliasTextureRequirements.compatibilityClass,
                                                                         aliasTextureRequirements.memoryType, aliasTextureRequirements.heapCategory},
                                                                        &failure));
        if (!aliasTextureBefore || !aliasTextureAfter || aliasTextureRequirements.size == 0 || !aliasTextureHeap || !gpu::BindMemory(aliasTextureBefore, aliasTextureHeap, 0, &failure) ||
            !gpu::BindMemory(aliasTextureAfter, aliasTextureHeap, 0, &failure))
            return false;

        gpu::BufferDesc fragmentPredecessorDesc = aliasBufferDesc;
        fragmentPredecessorDesc.size = aliasRequirements.size;
        gpu::BufferDesc fragmentDestinationDesc = fragmentPredecessorDesc;
        fragmentDestinationDesc.size = fragmentPredecessorDesc.size * 2u;
        gpu::Buffer fragmentDestination(gpu::AdoptReference, gpu::CreateBuffer(fragmentDestinationDesc, {}, &failure));
        gpu::Buffer fragmentFirst(gpu::AdoptReference, gpu::CreateBuffer(fragmentPredecessorDesc, {}, &failure));
        gpu::Buffer fragmentSecond(gpu::AdoptReference, gpu::CreateBuffer(fragmentPredecessorDesc, {}, &failure));
        gpu::Buffer fragmentDisjoint(gpu::AdoptReference, gpu::CreateBuffer(fragmentPredecessorDesc, {}, &failure));
        gpu::Buffer fragmentWrongHeap(gpu::AdoptReference, gpu::CreateBuffer(fragmentPredecessorDesc, {}, &failure));
        const gpu::MemoryRequirements fragmentDestinationRequirements = gpu::GetMemoryRequirements(fragmentDestination);
        const gpu::MemoryRequirements fragmentPredecessorRequirements = gpu::GetMemoryRequirements(fragmentFirst);
        gpu::Heap fragmentHeap(gpu::AdoptReference,
                               gpu::CreateHeap({fragmentDestinationRequirements.size + fragmentPredecessorRequirements.size, fragmentDestinationRequirements.alignment,
                                                fragmentDestinationRequirements.compatibilityClass, fragmentDestinationRequirements.memoryType, fragmentDestinationRequirements.heapCategory},
                                               &failure));
        gpu::Heap fragmentOtherHeap(gpu::AdoptReference, gpu::CreateHeap({fragmentPredecessorRequirements.size, fragmentPredecessorRequirements.alignment, fragmentPredecessorRequirements.compatibilityClass,
                                                                          fragmentPredecessorRequirements.memoryType, fragmentPredecessorRequirements.heapCategory},
                                                                         &failure));
        if (!fragmentDestination || !fragmentFirst || !fragmentSecond || !fragmentDisjoint || !fragmentWrongHeap || !fragmentHeap || !fragmentOtherHeap ||
            fragmentDestinationRequirements.size != fragmentPredecessorRequirements.size * 2u || !gpu::BindMemory(fragmentDestination, fragmentHeap, 0, &failure) ||
            !gpu::BindMemory(fragmentFirst, fragmentHeap, 0, &failure) || !gpu::BindMemory(fragmentSecond, fragmentHeap, fragmentPredecessorRequirements.size, &failure) ||
            !gpu::BindMemory(fragmentDisjoint, fragmentHeap, fragmentDestinationRequirements.size, &failure) || !gpu::BindMemory(fragmentWrongHeap, fragmentOtherHeap, 0, &failure))
            return false;

        gpu::BufferDesc committedAliasDesc = aliasBufferDesc;
        committedAliasDesc.virtualResource = false;
        gpu::Buffer committedAlias(gpu::AdoptReference, gpu::CreateBuffer(committedAliasDesc, {}, &failure));
        if (!committedAlias || gpu::BindMemory(committedAlias, aliasHeap, 0, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding)
        {
            std::printf("[rhiNvrhiTests] non-deferred bind was not rejected: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::Buffer checkedAlias(gpu::AdoptReference, gpu::CreateBuffer(aliasBufferDesc, {}, &failure));
        const vanguard::u64 overflowOffset = ~vanguard::u64{0} & ~(aliasRequirements.alignment - 1);
        if (!checkedAlias || gpu::BindMemory(checkedAlias, aliasHeap, 1, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::BindMemory(checkedAlias, aliasHeap, overflowOffset, &failure) || failure.code != gpu::FailureCode::CapacityExceeded ||
            gpu::BindMemory(checkedAlias, aliasHeap, aliasRequirements.alignment, &failure) || failure.code != gpu::FailureCode::CapacityExceeded)
        {
            std::printf("[rhiNvrhiTests] checked placement range validation failed: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }
        gpu::Heap underspecifiedAlignmentHeap(gpu::AdoptReference,
                                              gpu::CreateHeap({aliasRequirements.size, 1, aliasRequirements.compatibilityClass, aliasRequirements.memoryType, aliasRequirements.heapCategory}, &failure));
        if (!underspecifiedAlignmentHeap || gpu::BindMemory(checkedAlias, underspecifiedAlignmentHeap, 0, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding)
        {
            std::printf("[rhiNvrhiTests] heap-alignment validation failed: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::TextureDesc placedTextureDesc{};
        placedTextureDesc.extent = {4, 4, 1};
        placedTextureDesc.format = gpu::Format::R8G8B8A8UNorm;
        placedTextureDesc.usage = gpu::TextureUsage::ShaderResource;
        placedTextureDesc.virtualResource = true;
        gpu::MemoryRequirements placedTextureRequirements{};
        if (!gpu::GetMemoryRequirements(placedTextureDesc, placedTextureRequirements, &failure))
            return false;
        gpu::Heap textureClassHeap(gpu::AdoptReference, gpu::CreateHeap({placedTextureRequirements.size, placedTextureRequirements.alignment, placedTextureRequirements.compatibilityClass,
                                                                         placedTextureRequirements.memoryType, placedTextureRequirements.heapCategory},
                                                                        &failure));
        if (!textureClassHeap || gpu::BindMemory(checkedAlias, textureClassHeap, 0, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding || !gpu::BindMemory(checkedAlias, aliasHeap, 0, &failure))
        {
            std::printf("[rhiNvrhiTests] heap-class validation or recovery failed: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::PlacementRecord checkedPlacement{};
        gpu::HeapDesc queriedAliasHeap{};
        if (!gpu::GetPlacement(checkedAlias, checkedPlacement, &failure) || !gpu::GetHeapDesc(aliasHeap, queriedAliasHeap, &failure) || checkedPlacement.heap != aliasHeap.GetRef() ||
            checkedPlacement.offset != 0 || checkedPlacement.size != aliasRequirements.size || checkedPlacement.alignment != aliasRequirements.alignment ||
            checkedPlacement.compatibilityClass != aliasRequirements.compatibilityClass || checkedPlacement.heapCategory != gpu::PlacedHeapCategory::Buffer ||
            queriedAliasHeap.heapCategory != gpu::PlacedHeapCategory::Buffer || queriedAliasHeap.compatibilityClass != aliasRequirements.compatibilityClass)
        {
            std::printf("[rhiNvrhiTests] immutable placement metadata did not round-trip: %s\n", failure.message);
            return false;
        }
        const gpu::PlacementRecord immutablePlacement = checkedPlacement;
        if (gpu::BindMemory(checkedAlias, aliasHeap, 0, &failure) || failure.code != gpu::FailureCode::InvalidArgument || !gpu::GetPlacement(checkedAlias, checkedPlacement, &failure) ||
            checkedPlacement.generation != immutablePlacement.generation || checkedPlacement.heap != immutablePlacement.heap || checkedPlacement.offset != immutablePlacement.offset)
        {
            std::printf("[rhiNvrhiTests] repeated bind mutated placement metadata: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::HeapRef wrongClassHeap =
            gpu::CreateHeap({aliasRequirements.size, aliasRequirements.alignment, gpu::DeviceLocalTextureCompatibilityClass, gpu::MemoryType::DeviceLocal, gpu::PlacedHeapCategory::Buffer}, &failure);
        if (wrongClassHeap || failure.code != gpu::FailureCode::Unsupported)
        {
            std::printf("[rhiNvrhiTests] unsupported heap class was admitted: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }
        gpu::HeapRef wrongMemoryHeap =
            gpu::CreateHeap({aliasRequirements.size, aliasRequirements.alignment, gpu::UploadBufferCompatibilityClass, gpu::MemoryType::Upload, gpu::PlacedHeapCategory::Buffer}, &failure);
        if (wrongMemoryHeap || failure.code != gpu::FailureCode::Unsupported)
        {
            std::printf("[rhiNvrhiTests] unsupported heap memory type was admitted: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }
        const vanguard::u64 UnsupportedNativeHeapAlignment = gpu::GetCapabilities().placedResources.buffers.maximumHeapAlignment * 2u;
        gpu::HeapRef overAlignedHeap =
            gpu::CreateHeap({UnsupportedNativeHeapAlignment, UnsupportedNativeHeapAlignment, aliasRequirements.compatibilityClass, aliasRequirements.memoryType, aliasRequirements.heapCategory}, &failure);
        if (overAlignedHeap || failure.code != gpu::FailureCode::Unsupported)
        {
            std::printf("[rhiNvrhiTests] unsupported native heap alignment was admitted: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }
        gpu::MemoryBudgetSnapshot localBudget;
        const gpu::ResourceRef residencyResources[] = {gpu::ResourceRef(aliasHeap.GetRef())};
        if (!gpu::QueryMemoryBudget(gpu::MemorySegment::Local, localBudget, &failure) || localBudget.budget == 0 ||
            !gpu::SetResidencyPriority(gpu::ResourceRef(aliasHeap.GetRef()), gpu::ResidencyPriority::High, &failure) || !gpu::SetResidencyPinned(gpu::ResourceRef(aliasHeap.GetRef()), true, &failure))
        {
            std::printf("[rhiNvrhiTests] residency setup failed: %s\n", failure.message);
            return false;
        }
        if (gpu::Evict({residencyResources, 1}, {}, &failure) || failure.code != gpu::FailureCode::Busy || !gpu::SetResidencyPinned(gpu::ResourceRef(aliasHeap.GetRef()), false, &failure) ||
            !gpu::Evict({residencyResources, 1}, {}, &failure))
        {
            std::printf("[rhiNvrhiTests] pinned residency contract failed: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::SamplerState sampler(gpu::AdoptReference, gpu::RequestSamplerState({}, &failure));
        if (!sampler)
            return false;

        const gpu::BindingLayoutEntry bindingEntries[] = {{0, 1, gpu::BindingType::TextureShaderResource}, {1, 1, gpu::BindingType::StructuredBufferShaderResource}, {0, 1, gpu::BindingType::Sampler}};
        const gpu::BindingLayoutDesc bindingDesc{bindingEntries, 3, 2, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)};
        const gpu::BindingLayoutEntry reversedBindingEntries[] = {bindingEntries[2], bindingEntries[1], bindingEntries[0]};
        const gpu::BindingLayoutDesc reversedBindingDesc{reversedBindingEntries, 3, 2, bindingDesc.visibility};
        gpu::BindingLayout bindingLayout(gpu::AdoptReference, gpu::RequestBindingLayout(bindingDesc, &failure));
        gpu::BindingLayout duplicateLayout(gpu::AdoptReference, gpu::RequestBindingLayout(reversedBindingDesc, &failure));
        if (!bindingLayout || !duplicateLayout || bindingLayout.GetRef() != duplicateLayout.GetRef())
            return false;

        CompiledShader vertexBytecode;
        CompiledShader pixelBytecode;
        CompiledShader computeBytecode;
        if (!CompileShader("float4 main(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }", "vs_5_0", vertexBytecode) ||
            !CompileShader("float4 main() : SV_Target0 { return float4(1.0, 0.0, 1.0, 1.0); }", "ps_5_0", pixelBytecode) || !CompileShader("[numthreads(1,1,1)] void main() {}", "cs_5_0", computeBytecode))
            return false;
        gpu::Shader vertexShader(gpu::AdoptReference, gpu::CreateShader({gpu::ShaderStage::Vertex, vertexBytecode.bytecode->GetBufferPointer(), vertexBytecode.bytecode->GetBufferSize(), "main"}, &failure));
        gpu::Shader pixelShader(gpu::AdoptReference, gpu::CreateShader({gpu::ShaderStage::Pixel, pixelBytecode.bytecode->GetBufferPointer(), pixelBytecode.bytecode->GetBufferSize(), "main"}, &failure));
        gpu::Shader computeShader(gpu::AdoptReference, gpu::CreateShader({gpu::ShaderStage::Compute, computeBytecode.bytecode->GetBufferPointer(), computeBytecode.bytecode->GetBufferSize(), "main"}, &failure));
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
        if (!firstPipelineRequest.HasSucceeded() || !duplicatePipelineRequest.HasSucceeded() || firstPipelineRequest.GetPipeline() != duplicatePipelineRequest.GetPipeline() ||
            pipelineCache.GetStats().coalescedRequests == 0)
        {
            const vanguard::pipeline_cache::FailureEvidence cacheFailure = firstPipelineRequest.GetError();
            std::printf("[rhiNvrhiTests] cached graphics pipeline creation failed: %s\n", cacheFailure.message);
            return false;
        }

        const vanguard::shaders::StageBuildRecord cookedStages[] = {
            {vanguard::shaders::ShaderStage::Vertex, vanguard::shaders::NativeFormat::Dxil, 0x1001, vertexBytecode.bytecode->GetBufferPointer(), vertexBytecode.bytecode->GetBufferSize(), "main"},
            {vanguard::shaders::ShaderStage::Fragment, vanguard::shaders::NativeFormat::Dxil, 0x1002, pixelBytecode.bytecode->GetBufferPointer(), pixelBytecode.bytecode->GetBufferSize(), "main"}};
        const vanguard::shaders::VertexInput cookedInput[] = {{0x2001, 0, 0, vanguard::shaders::NumericClass::FloatingPoint, 3, 32}};
        const vanguard::shaders::FragmentOutput cookedOutput[] = {{0x3001, 0, 0, vanguard::shaders::NumericClass::FloatingPoint, 0x0f}};
        vanguard::shaders::BuildDescription cookedShaderDescription;
        cookedShaderDescription.kind = vanguard::shaders::ProgramKind::Graphics;
        cookedShaderDescription.program = 0x4001;
        cookedShaderDescription.permutation = vanguard::crypto::Sha256("runtime shader permutation", 26);
        cookedShaderDescription.compilerFingerprint = vanguard::crypto::Sha256("d3dcompiler test", 16);
        cookedShaderDescription.pipelineInterface.stages = vanguard::shaders::StageBit(vanguard::shaders::ShaderStage::Vertex) | vanguard::shaders::StageBit(vanguard::shaders::ShaderStage::Fragment);
        cookedShaderDescription.pipelineInterface.primitiveClass = vanguard::shaders::PrimitiveClass::Triangle;
        cookedShaderDescription.pipelineInterface.renderTargetCount = 1;
        cookedShaderDescription.stages = cookedStages;
        cookedShaderDescription.vertexInputs = cookedInput;
        cookedShaderDescription.fragmentOutputs = cookedOutput;
        vanguard::containers::DynamicArray<vanguard::u8> cookedShaderBytes(vanguard::memory::pools::Rendering::GetInstance());
        vanguard::filesystem::MemoryFileWriter cookedShaderWriter(cookedShaderBytes);
        vanguard::shaders::ShaderFile cookedShader;
        const vanguard::shaders::Result cookedShaderWrite = vanguard::shaders::WriteShader(cookedShaderWriter, cookedShaderDescription);
        vanguard::filesystem::MemoryFileReader cookedShaderReader(cookedShaderBytes, 0);
        const vanguard::shaders::Result cookedShaderOpen = cookedShaderWrite == vanguard::shaders::Result::Success ? cookedShader.Open(cookedShaderReader) : cookedShaderWrite;
        if (cookedShaderWrite != vanguard::shaders::Result::Success || cookedShaderOpen != vanguard::shaders::Result::Success)
        {
            std::printf("[rhiNvrhiTests] cooked runtime shader document failed: write=%s open=%s\n", vanguard::shaders::ToString(cookedShaderWrite), vanguard::shaders::ToString(cookedShaderOpen));
            return false;
        }
        rendering::RenderShader runtimeShader;
        {
            rendering::RenderShaderMap shaderMap;
            const rendering::NamedRenderShader duplicate[] = {{"TestShader", &cookedShader}, {"TestShader", &cookedShader}};
            if (shaderMap.Init(duplicate, &failure) != rendering::RenderShaderResult::InvalidArgument || shaderMap.IsInitialized() || shaderMap.FindShader("TestShader") != nullptr)
            {
                std::printf("[rhiNvrhiTests] shader catalog duplicate rollback failed\n");
                return false;
            }
            const rendering::NamedRenderShader catalog[] = {{"TestShader", &cookedShader}};
            if (shaderMap.Init(catalog, &failure) != rendering::RenderShaderResult::Success)
                return false;
            const rendering::RenderShader* const shader = shaderMap.GetShader("TestShader");
            if (!shader->IsLoaded() || shaderMap.GetShader("TestShader") != shader || shaderMap.FindShader("MissingShader") != nullptr ||
                shaderMap.Init(catalog, &failure) != rendering::RenderShaderResult::InvalidState || shaderMap.GetShader("TestShader") != shader)
            {
                std::printf("[rhiNvrhiTests] shader catalog stable lookup failed\n");
                return false;
            }
            shaderMap.Clear();
            if (shaderMap.IsInitialized() || shaderMap.FindShader("TestShader") != nullptr || shaderMap.Init(catalog, &failure) != rendering::RenderShaderResult::Success)
                return false;
        }
        if (runtimeShader.Load(cookedShader, &failure) != rendering::RenderShaderResult::Success)
        {
            std::printf("[rhiNvrhiTests] runtime shader materialization failed: %s\n", failure.message);
            return false;
        }

        const vanguard::pipelines::ShaderReference cookedShaderReference{0x7001, cookedShader.GetPermutation(), cookedShader.BindingLayoutFingerprint(), cookedShader.GetPipelineInterfaceFingerprint()};
        const vanguard::pipelines::VertexStream cookedStream{0, 12, vanguard::pipelines::InputRate::PerVertex, 1};
        const vanguard::pipelines::VertexAttribute cookedAttribute{0x2001, 0, 0, 0, 0, vanguard::shaders::NumericClass::FloatingPoint, 3, 32, vanguard::pipelines::Format::R32G32B32Float, "POSITION"};
        vanguard::pipelines::BuildDescription cookedPipelineDescription;
        cookedPipelineDescription.kind = vanguard::pipelines::PipelineKind::Graphics;
        cookedPipelineDescription.name = 0x8001;
        cookedPipelineDescription.shaders = {&cookedShaderReference, 1};
        cookedPipelineDescription.vertexStreams = {&cookedStream, 1};
        cookedPipelineDescription.vertexAttributes = {&cookedAttribute, 1};
        cookedPipelineDescription.graphics.blend.attachmentCount = 1;
        cookedPipelineDescription.graphics.rasterizer.cull = vanguard::pipelines::CullMode::None;
        vanguard::containers::DynamicArray<vanguard::u8> cookedPipelineBytes(vanguard::memory::pools::Rendering::GetInstance());
        vanguard::filesystem::MemoryFileWriter cookedPipelineWriter(cookedPipelineBytes);
        vanguard::pipelines::PipelineFile cookedPipeline;
        const vanguard::pipelines::Result cookedPipelineWrite = vanguard::pipelines::WritePipeline(cookedPipelineWriter, cookedPipelineDescription);
        vanguard::filesystem::MemoryFileReader cookedPipelineReader(cookedPipelineBytes, 0);
        if (cookedPipelineWrite != vanguard::pipelines::Result::Success || cookedPipeline.Open(cookedPipelineReader) != vanguard::pipelines::Result::Success)
        {
            std::printf("[rhiNvrhiTests] cooked runtime pipeline document failed\n");
            return false;
        }
        vanguard::pipelines::AttachmentSignature cookedAttachments;
        cookedAttachments.colorCount = 1;
        cookedAttachments.colors[0] = {vanguard::pipelines::Format::R8G8B8A8UNorm, vanguard::shaders::NumericClass::FloatingPoint};
        const rendering::ResolvedRenderShader resolvedShader{cookedShaderReference.resource, &runtimeShader};
        rendering::RenderPipelineRequest materialization;
        materialization.pipeline = &cookedPipeline;
        materialization.shaders = {&resolvedShader, 1};
        materialization.attachments = &cookedAttachments;
        rendering::PipelineRequest materializedPipeline;
        const rendering::RenderPipelineResult materializationResult = rendering::RequestRenderPipeline(materialization, pipelineCache, materializedPipeline);
        if (materializationResult != rendering::RenderPipelineResult::Success)
        {
            std::printf("[rhiNvrhiTests] runtime pipeline materialization failed: %s\n", rendering::ToString(materializationResult));
            return false;
        }
        materializedPipeline.Wait();
        if (!materializedPipeline.HasSucceeded())
        {
            std::printf("[rhiNvrhiTests] runtime pipeline cache creation failed: %s\n", materializedPipeline.GetError().message);
            return false;
        }
        // A two-sided request must not create another PSO when the cooked
        // pipeline already disables culling. Mirroring still changes front-face
        // semantics, including when culling is disabled.
        rendering::PipelineRequest variantPipeline;
        materialization.twoSided = true;
        if (rendering::RequestRenderPipeline(materialization, pipelineCache, variantPipeline) != rendering::RenderPipelineResult::Success)
            return false;
        variantPipeline.Wait();
        if (!variantPipeline.HasSucceeded() || variantPipeline.GetPipeline() != materializedPipeline.GetPipeline())
            return false;
        variantPipeline.Reset();
        materialization.mirrored = true;
        if (rendering::RequestRenderPipeline(materialization, pipelineCache, variantPipeline) != rendering::RenderPipelineResult::Success)
            return false;
        variantPipeline.Wait();
        if (!variantPipeline.HasSucceeded() || variantPipeline.GetPipeline() == materializedPipeline.GetPipeline())
            return false;
        rendering::PipelineRequest sameMirroredPipeline;
        materialization.twoSided = false;
        if (rendering::RequestRenderPipeline(materialization, pipelineCache, sameMirroredPipeline) != rendering::RenderPipelineResult::Success)
            return false;
        sameMirroredPipeline.Wait();
        if (!sameMirroredPipeline.HasSucceeded() || sameMirroredPipeline.GetPipeline() != variantPipeline.GetPipeline())
            return false;
        sameMirroredPipeline.Reset();
        variantPipeline.Reset();
        materialization.mirrored = false;
        // Cooked shader identity alone must not coalesce distinct native root
        // interfaces in the shared renderer/material cache.
        const gpu::BindingLayoutEntry extraConstants{7, 16, gpu::BindingType::PushConstants};
        const gpu::BindingLayoutDesc extraLayoutDesc{&extraConstants, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex)};
        gpu::BindingLayout extraLayout(gpu::AdoptReference, gpu::RequestBindingLayout(extraLayoutDesc));
        const gpu::BindingLayoutRef extraLayoutRef = extraLayout.GetRef();
        materialization.interfaceResources.bindingLayouts = {&extraLayoutRef, 1};
        if (!extraLayout || rendering::RequestRenderPipeline(materialization, pipelineCache, variantPipeline) != rendering::RenderPipelineResult::Success)
            return false;
        variantPipeline.Wait();
        if (!variantPipeline.HasSucceeded() || variantPipeline.GetPipeline() == materializedPipeline.GetPipeline())
            return false;
        variantPipeline.Reset();
        materialization.interfaceResources.bindingLayouts = {};
        extraLayout.Reset();
        materializedPipeline.Reset();
        {
            rendering::RenderSceneManager scenes;
            rendering::RenderCameraStorage cameras;
            rendering::GpuSceneRuntime scene;
            rendering::FrameRenderer renderer(scenes, cameras, scene);
            const rendering::NamedRenderShader catalog[] = {{"FeatureShader", &cookedShader}};
            if (renderer.InitializeShaders(catalog, &failure) != rendering::RenderShaderResult::Success)
                return false;
            const rendering::ResolvedRenderShader featureShader{cookedShaderReference.resource, renderer.GetShader("FeatureShader")};
            rendering::RenderPipelineRequest featureRequest = materialization;
            featureRequest.shaders = {&featureShader, 1};
            const rendering::NamedRenderPipeline duplicate[] = {{"FeaturePipeline", featureRequest}, {"FeaturePipeline", featureRequest}};
            if (renderer.InitializePipelines(duplicate, pipelineCache) != rendering::RenderPipelineResult::InvalidArgument)
                return false;
            const rendering::NamedRenderPipeline features[] = {{"FeaturePipeline", featureRequest}};
            if (renderer.InitializePipelines(features, pipelineCache) != rendering::RenderPipelineResult::Success)
                return false;
            const gpu::PipelineRef prepared = renderer.GetPipeline("FeaturePipeline");
            if (!prepared.IsValid() || prepared != renderer.GetPipeline("FeaturePipeline") || renderer.InitializePipelines(features, pipelineCache) != rendering::RenderPipelineResult::InvalidArgument)
                return false;
            if (!CheckCatalogNodeDraw(renderer, renderTarget, vertexBuffer))
                return false;
            {
                rendering::RenderFlowResourceAllocator allocator;
                rendering::RenderFlowResourceFailure resourceFailure;
                if (!allocator.Initialize({}, &resourceFailure) || !allocator.BeginFrame(1, {}, &resourceFailure))
                    return false;
                rendering::RetainedTextureImportDesc import;
                import.token = {1};
                import.texture = renderTarget;
                import.expected = renderTargetDesc;
                rendering::ImportedResourceId imported;
                if (!allocator.RegisterImport(import, imported, &resourceFailure))
                    return false;
                rendering::NodesContainer nodes;
                rendering::RenderNodeGraph graph;
                bool recorded = false;
                {
                    rendering::NodeGraphFactory factory(&graph, &nodes);
                    static_cast<void>(factory.Create<CatalogGraphDrawNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0), renderer, renderTarget.GetRef(), vertexBuffer.GetRef(), imported, recorded));
                }
                graph.BuildRenderFlowGroups();
                if (!RunSingleScopeGraph(graph, allocator) || !recorded || !allocator.Shutdown(&resourceFailure))
                {
                    std::printf("[rhiNvrhiTests] scheduled catalog graph failed\n");
                    return false;
                }
            }
            renderer.ClearPipelines();
            if (!renderer.ShutdownResourceAllocator())
                return false;
        }
        runtimeShader.Unload();
        if (!CheckFullscreenCopyGraph())
        {
            std::printf("[rhiNvrhiTests] fullscreen copy graph failed\n");
            return false;
        }
        firstPipelineRequest.Reset();
        duplicatePipelineRequest.Reset();
        // Raw pipeline, cooked baseline, mirrored cooked variant, and the
        // distinct native-layout variant; redundant two-sided requests coalesce.
        if (pipelineCache.InvalidateAll() != 4 || !pipelineCache.Shutdown())
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
                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 2, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure));
            samplerDescriptors = gpu::DescriptorDomain(
                gpu::AdoptReference,
                gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure));
            if (!resourceDescriptors || !samplerDescriptors)
                return false;
            textureDescriptor = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            bufferDescriptor = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            samplerDescriptor = gpu::AllocateDescriptor(samplerDescriptors, &failure);
            if (!textureDescriptor || !bufferDescriptor || !samplerDescriptor || !gpu::WriteDescriptor(resourceDescriptors, textureDescriptor, texture, gpu::BindingType::TextureShaderResource, {}, &failure) ||
                !gpu::WriteDescriptor(resourceDescriptors, bufferDescriptor, source, gpu::BindingType::StructuredBufferShaderResource, {}, &failure) ||
                !gpu::WriteDescriptor(samplerDescriptors, samplerDescriptor, sampler, &failure))
                return false;
            if (gpu::WriteDescriptor(resourceDescriptors, textureDescriptor, texture, gpu::BindingType::TextureShaderResource, {}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding)
                return false;
            const gpu::DescriptorDomainStats resourceStats = gpu::GetDescriptorDomainStats(resourceDescriptors);
            if (resourceStats.capacity != 2 || resourceStats.allocated != 2 || resourceStats.populated != 2 || resourceStats.free != 0)
                return false;

            constexpr vanguard::u32 concurrentDescriptorCount = 128;
            gpu::DescriptorDomain concurrentDomain(gpu::AdoptReference,
                                                   gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, concurrentDescriptorCount, 0, gpu::ShaderStageBit(gpu::ShaderStage::Compute)}, &failure));
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
                        if (!concurrentDescriptors[index] || !gpu::WriteDescriptor(concurrentDomain, concurrentDescriptors[index], texture, gpu::BindingType::TextureShaderResource))
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
            if (concurrentStats.allocated != 0 || concurrentStats.populated != 0 || concurrentStats.pendingRetirement != 0 || concurrentStats.peakAllocated != concurrentDescriptorCount ||
                concurrentStats.completedRetirements != concurrentDescriptorCount || concurrentStats.free != concurrentDescriptorCount)
                return false;

            const gpu::DescriptorHandle immediateRecycle = gpu::AllocateDescriptor(concurrentDomain, &failure);
            if (!immediateRecycle || immediateRecycle.generation == 1 || !gpu::RetireDescriptor(concurrentDomain, immediateRecycle, {}, &failure))
                return false;
            const gpu::DescriptorDomainStats immediateRecycleStats = gpu::GetDescriptorDomainStats(concurrentDomain);
            if (immediateRecycleStats.allocated != 0 || immediateRecycleStats.pendingRetirement != 0 || immediateRecycleStats.free != concurrentDescriptorCount ||
                immediateRecycleStats.completedRetirements != concurrentDescriptorCount + 1u)
                return false;
        }

        const gpu::BindingLayoutEntry pushConstantEntry{0, 16, gpu::BindingType::PushConstants};
        gpu::BindingLayout pushConstantLayout(gpu::AdoptReference,
                                              gpu::RequestBindingLayout({&pushConstantEntry, 1, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure));
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
        if (!domainPipelineCache.Initialize() || domainPipelineCache.RequestGraphics(domainPipelineKey, executionGraphicsDesc, domainPipelineRequest) != vanguard::pipeline_cache::Result::Success)
            return false;
        domainPipelineRequest.Wait();
        if (!domainPipelineRequest.HasSucceeded())
            return false;
        domainPipelineRequest.Reset();
        if (domainPipelineCache.InvalidateAll() != 1 || !domainPipelineCache.Shutdown())
            return false;

        gpu::QueryPool timestampQueries(gpu::AdoptReference, gpu::CreateQueryPool({gpu::QueryType::Timestamp, 2}, &failure));
        gpu::QueryPool occlusionQueries(gpu::AdoptReference, gpu::CreateQueryPool({gpu::QueryType::Occlusion, 1}, &failure));
        gpu::QueryPool statisticsQueries(gpu::AdoptReference, gpu::CreateQueryPool({gpu::QueryType::PipelineStatistics, 1}, &failure));
        gpu::TimestampCalibration calibration{};
        if (!timestampQueries || !occlusionQueries || !statisticsQueries || gpu::GetTimestampFrequency(gpu::QueueType::Graphics, &failure) == 0 ||
            !gpu::CalibrateTimestamps(gpu::QueueType::Graphics, calibration, &failure) || !calibration.IsValid())
            return false;
        gpu::SetResourceDebugName(timestampQueries.GetRef(), "RHI Timestamp Queries");
        gpu::SetResourceDebugName(occlusionQueries.GetRef(), "RHI Occlusion Queries");
        gpu::SetResourceDebugName(statisticsQueries.GetRef(), "RHI Pipeline Statistics");

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x56474e44524849ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
            return false;
        if (gpu::EndGpuEvent(&failure) || failure.code != gpu::FailureCode::InvalidArgument)
            return false;
        const gpu::RenderTargetSetup renderTargets{{{renderTarget, gpu::Format::Unknown, 0, 0, false}}, 1, {}};
        const gpu::VertexBufferBinding vertexBindingState{vertexBuffer, 0, 0};
        const gpu::IndexBufferBinding indexBindingState{indexBuffer, 0, gpu::IndexFormat::UInt16};
        const vanguard::u32 pushConstants[] = {textureDescriptor.GpuIndex(), bufferDescriptor.GpuIndex(), samplerDescriptor.GpuIndex(), 0};
        const gpu::Rect partialClear{8, 8, 32, 32};
        if (gpu::TransitionTexture(renderTarget, gpu::ResourceState::CopySource, gpu::ResourceState::RenderTarget, {}, &failure) || failure.code != gpu::FailureCode::ResourceStateMismatch ||
            gpu::TransitionBuffer(vertexBuffer, gpu::ResourceState::CopySource, gpu::ResourceState::VertexBuffer, &failure) || failure.code != gpu::FailureCode::ResourceStateMismatch ||
            !gpu::TransitionTexture(renderTarget, gpu::ResourceState::Common, gpu::ResourceState::RenderTarget, {}, &failure) ||
            !gpu::TransitionBuffer(vertexBuffer, gpu::ResourceState::Common, gpu::ResourceState::VertexBuffer, &failure) ||
            !gpu::TransitionBuffer(indexBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndexBuffer, &failure) ||
            !gpu::TransitionBuffer(indirectBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndirectArgument, &failure) ||
            !gpu::TransitionBuffer(indirectCountBuffer, gpu::ResourceState::Common, gpu::ResourceState::IndirectArgument, &failure))
            return false;
        const auto requireTransfer = [&failure](const bool condition, const char* const stage) noexcept
        {
            if (!condition)
                std::fprintf(stderr, "[rhiNvrhiTests] texture transfer stage failed: %s: code=%u message=%s\n", stage, static_cast<unsigned>(failure.code), failure.message);
            return condition;
        };
        const gpu::TextureCopyRegion outOfBoundsCopy{{}, {}, 3, 0, 0, 0, 0, 0, {2, 1, 1}};
        const gpu::TextureCopyRegion partialCopy{{}, {}, 0, 0, 0, 2, 2, 0, {2, 2, 1}};
        const gpu::TextureReadbackRegion partialReadback{{}, 1, 1, 0, {2, 2, 1}};
        if (!requireTransfer(!gpu::CopyTexture(copyDestination, copySource, outOfBoundsCopy, &failure) && failure.code == gpu::FailureCode::InvalidArgument, "copy bounds rejection") ||
            !requireTransfer(!gpu::CopyTexture(copyDestination, copySource, {}, &failure) && failure.code == gpu::FailureCode::ResourceStateMismatch, "copy state rejection") ||
            !requireTransfer(gpu::TransitionTexture(copySource, gpu::ResourceState::Common, gpu::ResourceState::CopySource, {}, &failure), "source copy transition") ||
            !requireTransfer(gpu::TransitionTexture(copyDestination, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, {}, &failure), "destination copy transition") ||
            !requireTransfer((textureReadback = gpu::TextureReadback(gpu::AdoptReference, gpu::RequestTextureReadback(copySource, partialReadback, &failure))).IsValid(), "texture readback request") ||
            !requireTransfer(gpu::CopyTexture(copyDestination, copySource, {}, &failure), "texture copy") ||
            !requireTransfer(gpu::CopyTexture(copyDestination, copySource, partialCopy, &failure), "texture region copy") ||
            !requireTransfer(!gpu::ResolveTexture(resolveDestination, resolveSource, {}, &failure) && failure.code == gpu::FailureCode::ResourceStateMismatch, "resolve state rejection") ||
            !requireTransfer(gpu::TransitionTexture(resolveSource, gpu::ResourceState::Common, gpu::ResourceState::RenderTarget, {}, &failure), "resolve source render transition") ||
            !requireTransfer(gpu::ClearColorTarget(resolveSource, {0.125f, 0.25f, 0.5f, 1.0f}, {}, nullptr, &failure), "resolve source clear") ||
            !requireTransfer(gpu::TransitionTexture(resolveSource, gpu::ResourceState::RenderTarget, gpu::ResourceState::ResolveSource, {}, &failure), "resolve source transition") ||
            !requireTransfer(gpu::TransitionTexture(resolveDestination, gpu::ResourceState::Common, gpu::ResourceState::ResolveDestination, {}, &failure), "resolve destination transition") ||
            !requireTransfer(gpu::ResolveTexture(resolveDestination, resolveSource, {}, &failure), "texture resolve"))
            return false;
        gpu::TextureReadbackInfo readbackInfo{};
        gpu::TextureReadbackMapping prematureMapping{};
        if (!gpu::GetTextureReadbackInfo(textureReadback, readbackInfo, &failure) || readbackInfo.state != gpu::TextureReadbackState::PendingSubmission ||
            gpu::MapTextureReadback(textureReadback, prematureMapping, &failure) || failure.code != gpu::FailureCode::Busy)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] completed texture readback contract failed: code=%u message=%s state=%u\n", static_cast<unsigned>(failure.code), failure.message,
                         static_cast<unsigned>(readbackInfo.state));
            return false;
        }
        const gpu::ResourceRef aliasTexturePredecessors[] = {gpu::ResourceRef(aliasTextureBefore.GetRef())};
        const gpu::ResourceRef aliasBufferPredecessors[] = {gpu::ResourceRef(aliasBufferBefore.GetRef())};
        const gpu::ResourceRef computeAliasBufferPredecessors[] = {gpu::ResourceRef(computeAliasBufferBefore.GetRef())};
        const gpu::ResourceRef fragmentGap[] = {gpu::ResourceRef(fragmentFirst.GetRef())};
        const gpu::ResourceRef fragmentDuplicate[] = {gpu::ResourceRef(fragmentFirst.GetRef()), gpu::ResourceRef(fragmentFirst.GetRef())};
        const gpu::ResourceRef fragmentDisjointSet[] = {gpu::ResourceRef(fragmentDisjoint.GetRef())};
        const gpu::ResourceRef fragmentWrongHeapSet[] = {gpu::ResourceRef(fragmentFirst.GetRef()), gpu::ResourceRef(fragmentWrongHeap.GetRef())};
        const gpu::ResourceRef fragmentWrongKind[] = {gpu::ResourceRef(aliasTextureBefore.GetRef())};
        const gpu::ResourceRef fragmentOutOfOrder[] = {gpu::ResourceRef(fragmentSecond.GetRef()), gpu::ResourceRef(fragmentFirst.GetRef())};
        const gpu::ResourceRef fragmentExact[] = {gpu::ResourceRef(fragmentFirst.GetRef()), gpu::ResourceRef(fragmentSecond.GetRef())};
        if (gpu::ActivateAliasedResource(gpu::ResourceRef(committedAlias.GetRef()), {aliasBufferPredecessors, 1}, &failure) || failure.code != gpu::FailureCode::MissingBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentGap, 1}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentDuplicate, 2}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentDisjointSet, 1}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentWrongHeapSet, 2}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentWrongKind, 1}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentOutOfOrder, 2}, &failure) || failure.code != gpu::FailureCode::IncompatibleBinding ||
            !gpu::ActivateAliasedResource(gpu::ResourceRef(fragmentDestination.GetRef()), {fragmentExact, 2}, &failure))
        {
            std::printf("[rhiNvrhiTests] semantic alias coverage validation failed: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        if (!gpu::BeginGpuEvent("RHI Native Conformance", &failure) || !gpu::SetGpuMarker("Resource setup", &failure) || !gpu::IssueQuery(timestampQueries.GetRef(), 0, &failure) ||
            !gpu::ClearColorTarget(renderTarget, {0.0f, 0.0f, 0.0f, 1.0f}, {}, nullptr, &failure) || !gpu::ClearColorTarget(renderTarget, {0.25f, 0.5f, 0.75f, 1.0f}, {0, 1, 0, 1}, &partialClear, &failure) ||
            !gpu::ClearDepthTarget(depthTarget, 0.0f, {}, nullptr, &failure) || !gpu::ClearStencilTarget(depthTarget, 3, {0, 1, 0, 1}, &partialClear, &failure) ||
            !gpu::ClearDepthStencilTarget(depthTarget, 1.0f, 0, {}, nullptr, &failure) || !gpu::ClearTextureUav(texture, gpu::ColorValue{0.0f, 0.0f, 0.0f, 0.0f}, {}, &failure) ||
            !gpu::ClearTextureUav(integerTexture, 0x10203040u, {}, &failure) || !gpu::ClearColorTarget(aliasTextureBefore, {0.0f, 0.0f, 0.0f, 1.0f}, {}, nullptr, &failure) ||
            !gpu::MakeStateSafeToRetire(aliasTextureBefore, &failure) || !gpu::ClearBufferUav(aliasBufferBefore, 0x11111111u, &failure) || !gpu::MakeStateSafeToRetire(aliasBufferBefore, &failure) ||
            !gpu::ActivateAliasedResource(gpu::ResourceRef(aliasTextureAfter.GetRef()), {aliasTexturePredecessors, 1}, &failure) ||
            !gpu::ActivateAliasedResource(gpu::ResourceRef(aliasBufferAfter.GetRef()), {aliasBufferPredecessors, 1}, &failure) ||
            !gpu::ClearColorTarget(aliasTextureAfter, {0.25f, 0.5f, 0.75f, 1.0f}, {}, nullptr, &failure) || !gpu::ClearBufferUav(aliasBufferAfter, 0u, &failure) ||
            !gpu::DiscardTexture(depthTarget, {}, &failure) || !gpu::SetPipeline(executionGraphicsPipeline, &failure) || !gpu::SetupRenderTargets(renderTargets, &failure) ||
            !gpu::SetViewport({0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}, &failure) || !gpu::SetScissors({0, 0, 64, 64}, &failure) || !gpu::SetStencilRefValue(17, &failure) ||
            !gpu::SetBlendFactor({0.25f, 0.5f, 0.75f, 1.0f}, &failure) || !gpu::BindVertexBuffers(0, {&vertexBindingState, 1}, &failure) || !gpu::BindIndexBuffer(indexBindingState, &failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, {}, &failure) || !gpu::SetPushConstants(pushConstants, sizeof(pushConstants), &failure) || !gpu::BeginGpuEvent("Graphics queries", &failure) ||
            !gpu::BeginQuery(occlusionQueries.GetRef(), 0, &failure) || !gpu::BeginQuery(statisticsQueries.GetRef(), 0, &failure) || !gpu::DrawPrimitive({3, 1, 0, 0}, &failure) ||
            !gpu::DrawIndexedPrimitive({3, 1, 0, 0, 0}, &failure) || !gpu::DrawPrimitiveIndirect(0, 1, &failure) || !gpu::DrawIndexedPrimitiveIndirect(sizeof(gpu::IndirectDrawArguments), 1, &failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, indirectCountBuffer, &failure) || !gpu::DrawIndexedPrimitiveIndirectCount(sizeof(gpu::IndirectDrawArguments), 0, 1, &failure) ||
            !gpu::EndQuery(statisticsQueries.GetRef(), 0, &failure) || !gpu::EndQuery(occlusionQueries.GetRef(), 0, &failure) || !gpu::EndGpuEvent(&failure) ||
            !gpu::BindIndirectArguments(indirectBuffer, {}, &failure) || !gpu::SetPipeline(computePipeline, &failure) || !gpu::DispatchCompute(1, 1, 1, &failure) ||
            !gpu::DispatchIndirectCompute(sizeof(gpu::IndirectDrawArguments) + sizeof(gpu::IndirectDrawIndexedArguments), &failure) ||
            !gpu::TransitionTexture(texture, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, {}, &failure) || !gpu::BarrierTextureUav(texture, &failure) ||
            !gpu::TransitionBuffer(source, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, &failure) || !gpu::BarrierBufferUav(source, &failure) ||
            !gpu::TransitionBuffer(source, gpu::ResourceState::UnorderedAccess, gpu::ResourceState::CopySource, &failure) ||
            !gpu::TransitionBuffer(destination, gpu::ResourceState::Common, gpu::ResourceState::CopyDestination, &failure) || !gpu::CopyBuffer(destination, 0, source, 0, sizeof(sourceData), &failure) ||
            !gpu::TransitionBuffer(destination, gpu::ResourceState::CopyDestination, gpu::ResourceState::CopySource, &failure) || !gpu::CopyBuffer(readback, 0, destination, 0, sizeof(sourceData), &failure) ||
            !gpu::IssueQuery(timestampQueries.GetRef(), 1, &failure) || !gpu::ResolveQueries(timestampQueries.GetRef(), 0, 2, &failure) || !gpu::ResolveQueries(occlusionQueries.GetRef(), 0, 1, &failure) ||
            !gpu::ResolveQueries(statisticsQueries.GetRef(), 0, 1, &failure) || !gpu::EndGpuEvent(&failure) || !gpu::FlushPendingBarriers(&failure))
        {
            std::printf("[rhiNvrhiTests] graphics/compute command recording failed: %s\n", failure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        gpu::UnbindCommandList();
        if (gpu::AcquireQueries(timestampQueries.GetRef(), 0, 2, &failure) || failure.code != gpu::FailureCode::Busy)
            return false;
        gpu::CommandListRef computeCommandList = gpu::CreateCommandList(gpu::CommandListType::Compute, 0x56474e44434f4d50ull, &failure);
        if (!computeCommandList)
            return false;
        if (!gpu::BindCommandList(computeCommandList, &failure) || !gpu::ClearBufferUav(computeAliasBufferBefore, 0x22222222u, &failure) || !gpu::MakeStateSafeToRetire(computeAliasBufferBefore, &failure) ||
            !gpu::ActivateAliasedResource(gpu::ResourceRef(computeAliasBufferAfter.GetRef()), {computeAliasBufferPredecessors, 1}, &failure) ||
            !gpu::ClearBufferUav(computeAliasBufferAfter, 0x33333333u, &failure) || gpu::ActivateAliasedResource(gpu::ResourceRef(aliasTextureAfter.GetRef()), {aliasTexturePredecessors, 1}, &failure) ||
            failure.code != gpu::FailureCode::IncompatibleBinding)
        {
            std::printf("[rhiNvrhiTests] RT-only alias discard was admitted on compute: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(computeCommandList);
            return false;
        }
        gpu::UnbindCommandList();
        gpu::CommandListRef copyCommandList = gpu::CreateCommandList(gpu::CommandListType::CopyAsync, 0x56474e44414c434full, &failure);
        if (!copyCommandList || !gpu::BindCommandList(copyCommandList, &failure) ||
            gpu::ActivateAliasedResource(gpu::ResourceRef(computeAliasBufferAfter.GetRef()), {computeAliasBufferPredecessors, 1}, &failure) || failure.code != gpu::FailureCode::InvalidCommandList)
        {
            std::printf("[rhiNvrhiTests] copy-queue alias activation was admitted: code=%u %s\n", static_cast<unsigned>(failure.code), failure.message);
            gpu::UnbindCommandList();
            if (copyCommandList)
                gpu::DiscardCommandList(copyCommandList);
            gpu::DiscardCommandList(commandList);
            gpu::DiscardCommandList(computeCommandList);
            return false;
        }
        gpu::UnbindCommandList();
        gpu::DiscardCommandList(copyCommandList);
        const gpu::CommandListRef submission[] = {commandList, computeCommandList};
        gpu::SubmissionReceipt receipt{};
        if (!gpu::CloseAndSubmitCommandLists("rhi resource conformance", {submission, 2}, gpu::CommandListSyncType::ForkAsyncCompute, receipt, &failure) || !receipt.IsValid() ||
            receipt.completion.queue != gpu::QueueType::Compute || receipt.residency.graphics == 0 || receipt.residency.compute != receipt.completion.value || receipt.residency.copy != 0)
            return false;
        const gpu::GpuFence completion = receipt.completion;
        gpu::ResidencyFenceSet inFlightUse;
        inFlightUse.Include({completion.queue, completion.value + 1u});
        if (gpu::Evict({residencyResources, 1}, inFlightUse, &failure) || failure.code != gpu::FailureCode::Busy)
            return false;

        if (resourceDescriptors)
        {
            gpu::DescriptorRetirement retirement;
            retirement.Include(completion);
            if (!gpu::RetireDescriptor(resourceDescriptors, textureDescriptor, retirement, &failure) || !gpu::RetireDescriptor(resourceDescriptors, bufferDescriptor, retirement, &failure) ||
                !gpu::RetireDescriptor(samplerDescriptors, samplerDescriptor, retirement, &failure))
                return false;
            texture.Reset();
        }
        if (!gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || !gpu::RetireResources(&failure))
            return false;
        if (!gpu::Evict({residencyResources, 1}, {}, &failure) || !gpu::MakeResident({residencyResources, 1}, &failure))
            return false;
        gpu::TextureReadbackMapping textureMapping{};
        const gpu::GpuFence graphicsCompletion{gpu::QueueType::Graphics, receipt.residency.graphics};
        if (!gpu::GetTextureReadbackInfo(textureReadback, readbackInfo, &failure) || readbackInfo.state != gpu::TextureReadbackState::Ready || readbackInfo.completion != graphicsCompletion ||
            !gpu::MapTextureReadback(textureReadback, textureMapping, &failure) || textureMapping.format != gpu::Format::R8G8B8A8UNorm || textureMapping.extent.width != 2 || textureMapping.extent.height != 2 ||
            textureMapping.rowPitch < 8 || textureMapping.depthPitch < 16)
            return false;
        bool textureReadbackMatches = true;
        const auto* const mappedPixels = static_cast<const vanguard::u8*>(textureMapping.data);
        for (vanguard::u32 row = 0; row < 2; ++row)
            for (vanguard::u32 byte = 0; byte < 8; ++byte)
                textureReadbackMatches = textureReadbackMatches && mappedPixels[row * textureMapping.rowPitch + byte] == pixels[(row + 1) * 16 + 4 + byte];
        if (!textureReadbackMatches || !gpu::UnmapTextureReadback(textureReadback, &failure))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] texture readback bytes/unmap failed: match=%u code=%u message=%s\n", textureReadbackMatches ? 1u : 0u, static_cast<unsigned>(failure.code), failure.message);
            return false;
        }

        gpu::TextureDesc discardedSourceDesc = copySourceDesc;
        discardedSourceDesc.initialState = gpu::ResourceState::CopySource;
        gpu::Texture discardedSource(gpu::AdoptReference, gpu::CreateTexture(discardedSourceDesc, {&textureSubresource, 1}, &failure));
        gpu::CommandListRef discardedCommand = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5244424b44495343ull, &failure);
        if (!discardedSource || !discardedCommand || !gpu::BindCommandList(discardedCommand, &failure))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] discarded readback setup failed: %s\n", failure.message);
            return false;
        }
        gpu::TextureReadback discardedReadback(gpu::AdoptReference, gpu::RequestTextureReadback(discardedSource, {}, &failure));
        gpu::UnbindCommandList();
        if (!discardedReadback)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] discarded readback request failed: %s\n", failure.message);
            return false;
        }
        gpu::DiscardCommandList(discardedCommand);
        gpu::TextureReadbackInfo discardedInfo{};
        if (!gpu::GetTextureReadbackInfo(discardedReadback, discardedInfo, &failure) || discardedInfo.state != gpu::TextureReadbackState::Failed || discardedInfo.completion.IsValid() ||
            gpu::MapTextureReadback(discardedReadback, textureMapping, &failure) || failure.code != gpu::FailureCode::BackendFailure)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] discarded readback state failed: state=%u code=%u message=%s\n", static_cast<unsigned>(discardedInfo.state), static_cast<unsigned>(failure.code),
                         failure.message);
            return false;
        }
        discardedReadback.Reset();
        discardedSource.Reset();
        vanguard::u64 timestampBegin = 0;
        vanguard::u64 timestampEnd = 0;
        vanguard::u64 visibleSamples = 0;
        gpu::PipelineStatistics statistics{};
        if (!gpu::AcquireQueries(timestampQueries.GetRef(), 0, 2, &failure) || !gpu::GetQueryResult(timestampQueries.GetRef(), 0, timestampBegin, &failure) ||
            !gpu::GetQueryResult(timestampQueries.GetRef(), 1, timestampEnd, &failure))
            return false;
        gpu::ReleaseQueries(timestampQueries.GetRef());
        if (!gpu::AcquireQueries(occlusionQueries.GetRef(), 0, 1, &failure) || !gpu::GetQueryResult(occlusionQueries.GetRef(), 0, visibleSamples, &failure))
            return false;
        gpu::ReleaseQueries(occlusionQueries.GetRef());
        if (!gpu::AcquireQueries(statisticsQueries.GetRef(), 0, 1, &failure) || !gpu::GetQueryResult(statisticsQueries.GetRef(), 0, statistics, &failure))
            return false;
        gpu::ReleaseQueries(statisticsQueries.GetRef());
        if (timestampBegin == 0 || timestampEnd < timestampBegin || statistics.inputAssemblerVertices == 0)
            return false;
        const gpu::ResidencyStats residencyStats = gpu::GetResidencyStats();
        if (residencyStats.budgetQueries == 0 || residencyStats.priorityChanges == 0 || residencyStats.makeResidentCalls == 0 || residencyStats.objectsMadeResident == 0 || residencyStats.objectsEvicted == 0 ||
            residencyStats.rejectedEvictions == 0 || residencyStats.automaticWorkingSetChecks == 0 || residencyStats.automaticMakeResidentCalls != 1 || residencyStats.automaticObjectsMadeResident != 1 ||
            residencyStats.automaticWorkingSetFailures != 0 || residencyStats.policyMaintenanceCalls == 0 || residencyStats.policyPinnedObjects != 0 || residencyStats.trackedAllocations == 0 ||
            residencyStats.trackedResidentBytes + residencyStats.trackedEvictedBytes == 0 || residencyStats.lastObservedBudget == 0)
        {
            std::printf("[rhiNvrhiTests] residency telemetry contract failed: tracked=%llu resident=%llu evicted=%llu maintenance=%llu "
                        "pinned=%llu\n",
                        static_cast<unsigned long long>(residencyStats.trackedAllocations), static_cast<unsigned long long>(residencyStats.trackedResidentBytes),
                        static_cast<unsigned long long>(residencyStats.trackedEvictedBytes), static_cast<unsigned long long>(residencyStats.policyMaintenanceCalls),
                        static_cast<unsigned long long>(residencyStats.policyPinnedObjects));
            return false;
        }
        if (resourceDescriptors)
        {
            const gpu::DescriptorHandle recycledTexture = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            const gpu::DescriptorHandle recycledBuffer = gpu::AllocateDescriptor(resourceDescriptors, &failure);
            const gpu::DescriptorHandle recycledSampler = gpu::AllocateDescriptor(samplerDescriptors, &failure);
            if (!recycledTexture || !recycledBuffer || !recycledSampler || recycledTexture.generation == textureDescriptor.generation || recycledBuffer.generation == bufferDescriptor.generation ||
                recycledSampler.generation == samplerDescriptor.generation)
                return false;
            if (gpu::RetireDescriptor(resourceDescriptors, textureDescriptor, {}, &failure) || failure.code != gpu::FailureCode::InvalidReference ||
                !gpu::RetireDescriptor(resourceDescriptors, recycledTexture, {}, &failure) || !gpu::RetireDescriptor(resourceDescriptors, recycledBuffer, {}, &failure) ||
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
        textureReadback.Reset();
        copySource.Reset();
        copyDestination.Reset();
        resolveSource.Reset();
        resolveDestination.Reset();
        discardedSource.Reset();
        renderTarget.Reset();
        depthTarget.Reset();
        vertexBuffer.Reset();
        indexBuffer.Reset();
        indirectBuffer.Reset();
        indirectCountBuffer.Reset();
        committedAlias.Reset();
        checkedAlias.Reset();
        aliasBufferBefore.Reset();
        aliasBufferAfter.Reset();
        computeAliasBufferBefore.Reset();
        computeAliasBufferAfter.Reset();
        computeAliasHeap.Reset();
        aliasTextureBefore.Reset();
        aliasTextureAfter.Reset();
        aliasTextureHeap.Reset();
        fragmentDestination.Reset();
        fragmentFirst.Reset();
        fragmentSecond.Reset();
        fragmentDisjoint.Reset();
        fragmentWrongHeap.Reset();
        fragmentHeap.Reset();
        fragmentOtherHeap.Reset();
        underspecifiedAlignmentHeap.Reset();
        textureClassHeap.Reset();
        aliasHeap.Reset();
        texture.Reset();
        integerTexture.Reset();
        sampler.Reset();
        executionGraphicsPipeline.Reset();
        pushConstantLayout.Reset();
        duplicateLayout.Reset();
        bindingLayout.Reset();
        resourceDescriptors.Reset();
        samplerDescriptors.Reset();
        timestampQueries.Reset();
        occlusionQueries.Reset();
        statisticsQueries.Reset();
        if (!gpu::RetireResources(&failure) || !gpu::FlushRetiredResources(&failure))
            return false;
        const gpu::ResidencyStats retiredResidencyStats = gpu::GetResidencyStats();
        if (retiredResidencyStats.trackedAllocations != 0 || retiredResidencyStats.trackedResidentBytes != 0 || retiredResidencyStats.trackedEvictedBytes != 0)
            std::printf("[rhiNvrhiTests] retired residency accounting failed: tracked=%llu resident=%llu evicted=%llu\n", static_cast<unsigned long long>(retiredResidencyStats.trackedAllocations),
                        static_cast<unsigned long long>(retiredResidencyStats.trackedResidentBytes), static_cast<unsigned long long>(retiredResidencyStats.trackedEvictedBytes));
        return retiredResidencyStats.trackedAllocations == 0 && retiredResidencyStats.trackedResidentBytes == 0 && retiredResidencyStats.trackedEvictedBytes == 0;
    }

    [[nodiscard]] bool TestGpuCounters(gpu::Failure& failure) noexcept
    {
        gpu::GpuCounterSystem counters;
        gpu::GpuCounterFrameHandle frame;
        gpu::GpuCounterScopeToken scope;
        gpu::CommandListRef commandList;
        bool commandListBound = false;

        const auto cleanup = [&]() noexcept
        {
            if (scope.IsValid() && commandListBound)
                static_cast<void>(counters.EndScope(scope));
            if (commandListBound)
            {
                gpu::UnbindCommandList();
                commandListBound = false;
            }
            if (commandList.IsValid())
                gpu::DiscardCommandList(commandList);
            if (frame.IsValid() && counters.GetFrameState(frame) != gpu::GpuCounterFrameState::Empty)
                static_cast<void>(counters.DiscardFrame(frame));
            if (counters.IsInitialized())
                static_cast<void>(counters.Shutdown());
        };

        const gpu::GpuCounterScopeId scopeId = gpu::MakeGpuCounterScopeId("Native.Counter.Conformance");
        if (!counters.Initialize({3, 8, 4, gpu::QueueType::Graphics, true}, &failure) || !counters.RegisterScope(scopeId, "Native.Counter.Conformance", &failure))
        {
            cleanup();
            return false;
        }
        commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x475055434f554e54ull, &failure);
        if (!commandList || !gpu::BindCommandList(commandList, &failure))
        {
            cleanup();
            return false;
        }
        commandListBound = true;
        if (!counters.BeginFrame(42, frame, &failure) || !counters.BeginScope(frame, scopeId, scope, &failure) || !counters.EndScope(scope, &failure) || !counters.EndFrame(frame, &failure))
        {
            cleanup();
            return false;
        }
        gpu::UnbindCommandList();
        commandListBound = false;
        const gpu::CommandListRef submission[] = {commandList};
        gpu::GpuFence completion;
        if (!gpu::CloseAndSubmitCommandLists("GPU counter conformance", {submission, 1}, gpu::CommandListSyncType::None, completion, &failure))
        {
            cleanup();
            return false;
        }
        commandList = {};
        if (!counters.CommitFrame(frame, completion, &failure) || !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure) || counters.Collect(&failure) != 1)
        {
            cleanup();
            return false;
        }
        gpu::GpuCounterFrameView view;
        if (!counters.GetOldestReadyFrame(view, &failure) || view.handle != frame || view.frameNumber != 42 || view.sampleCount != 1 || !view.samples[0].valid || view.samples[0].scope != scopeId ||
            view.gpuEnd < view.gpuBegin || view.samples[0].gpuEnd < view.samples[0].gpuBegin || !view.hasPipelineStatistics || !view.calibration.IsValid() || !counters.ConsumeFrame(frame, &failure) ||
            !counters.Shutdown(&failure))
        {
            cleanup();
            return false;
        }
        return true;
    }

    class PresentationWindowBackend final : public window::IWindowBackend
    {
    public:
        explicit PresentationWindowBackend(const HWND nativeWindow) noexcept : m_nativeWindow(nativeWindow)
        {
            m_state.placement.logicalExtent = {320, 180};
            m_state.placement.display = {1};
            m_state.placement.visible = true;
            m_state.pixelExtent = {320, 180};
            m_state.safeArea = {{0, 0}, {320, 180}};
            m_state.contentScale = 1.0f;
        }

        window::BackendStatus EnumerateDisplays(window::BackendDisplaySnapshot* const displays, const vanguard::u32 capacity, vanguard::u32& count) noexcept override
        {
            count = 1;
            if (displays == nullptr || capacity == 0)
                return window::BackendStatus::Failure(-1, "presentation test display storage is unavailable");
            displays[0].id = {1};
            displays[0].fingerprint = 1;
            displays[0].bounds = {{0, 0}, {1920, 1080}};
            displays[0].workArea = displays[0].bounds;
            displays[0].desktopPixelExtent = {1920, 1080};
            displays[0].desktopRefreshRate = {60, 1};
            displays[0].contentScale = 1.0f;
            displays[0].primary = true;
            return {};
        }

        window::BackendStatus Create(const window::BackendWindowDescriptor&, window::BackendWindowId& windowId, window::BackendWindowState& state) noexcept override
        {
            if (m_alive)
                return window::BackendStatus::Failure(-1, "presentation test window already exists");
            m_alive = true;
            windowId = {1};
            state = m_state;
            return {};
        }

        window::BackendStatus ApplyWindowState(window::BackendWindowId, const window::BackendWindowRequest&, window::BackendWindowState& state) noexcept override
        {
            if (!m_alive)
                return window::BackendStatus::Failure(-1, "presentation test window is unavailable");
            state = m_state;
            return {};
        }

        window::BackendStatus SetWindowTitle(window::BackendWindowId, const char*) noexcept override
        {
            return m_alive ? window::BackendStatus::Success() : window::BackendStatus::Failure(-1, "presentation test window is unavailable");
        }

        window::BackendStatus ResolvePresentationSurface(window::BackendWindowId, window::NativePresentationSurface& surface) noexcept override
        {
            surface = {};
            if (!m_alive || m_nativeWindow == nullptr)
                return window::BackendStatus::Failure(-1, "presentation test native surface is unavailable");
            surface = {window::NativePresentationSurfaceKind::Win32, m_nativeWindow, nullptr};
            return {};
        }

        window::BackendStatus DestroyWindow(window::BackendWindowId) noexcept override
        {
            if (!m_alive)
                return window::BackendStatus::Failure(-1, "presentation test window is unavailable");
            m_alive = false;
            return {};
        }

        [[nodiscard]] window::BackendWindowEvent ChangeState(const window::BackendEventType type, const window::WindowExtent extent, const bool minimized, const bool hdrCapable) noexcept
        {
            m_state.placement.logicalExtent = extent;
            m_state.pixelExtent = extent;
            m_state.safeArea = {{0, 0}, extent};
            m_state.minimized = minimized;
            m_state.hdrCapable = hdrCapable;
            return {type, {1}, m_state, 1, 0};
        }

    private:
        HWND m_nativeWindow = nullptr;
        window::BackendWindowState m_state;
        bool m_alive = false;
    };

    rendering::RenderFrameExecutionStatus ExecutePresentationFrame(rendering::RenderFrameContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameExecutionStatus ExecutePresentationFrameTick(rendering::RenderFrameTickContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    [[nodiscard]] bool TestPresentationService() noexcept
    {
        const auto require = [](const bool condition, const char* const stage) noexcept
        {
            if (!condition)
                std::fprintf(stderr, "[rhiNvrhiTests] presentation stage failed: %s\n", stage);
            return condition;
        };
        const HWND nativeWindow =
            CreateWindowExW(0, L"STATIC", L"Vanguard Presentation Service Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!require(nativeWindow != nullptr, "native window create"))
            return false;
        ShowWindow(nativeWindow, SW_SHOWNA);
        UpdateWindow(nativeWindow);
        PresentationWindowBackend backend(nativeWindow);
        window::WindowManager windows;
        window::Failure windowFailure;
        if (!require(windows.Initialize(backend, &windowFailure), "window manager initialize"))
            return false;

        window::WindowDescriptor windowDesc;
        windowDesc.title = "Presentation Service Test";
        windowDesc.placement.logicalExtent = {320, 180};
        windowDesc.placement.visible = true;
        window::WindowHandle windowHandle;
        if (!require(windows.Create(windowDesc, windowHandle, &windowFailure), "window create"))
            return false;

        rendering::RenderSceneManager commandScenes;
        rendering::RenderSceneFailure sceneFailure;
        if (!require(commandScenes.Initialize({}, &sceneFailure), "render command scene manager initialize"))
            return false;
        rendering::RenderCameraStorage commandCameras;
        rendering::RenderCameraFailure cameraFailure;
        if (!require(commandCameras.Initialize(commandScenes, {}, &cameraFailure), "render command camera storage initialize"))
            return false;
        rendering::RenderCommandSystem commands;
        rendering::RenderCommandFailure commandFailure;
        rendering::RenderCommandSystemConfig commandConfig;
        commandConfig.executeFrameTick = &ExecutePresentationFrameTick;
        commandConfig.executeFrame = &ExecutePresentationFrame;
        if (!require(commands.Initialize(commandScenes, commandCameras, commandConfig, &commandFailure), "render command system initialize"))
            return false;
        rendering::ViewportFailure viewportFailure;
        rendering::ViewportManager viewports;
        if (!require(viewports.Initialize(commands, &viewportFailure), "viewport manager initialize"))
            return false;

        rendering::PresentationService presentation;
        rendering::PresentationFailure presentationFailure;
        if (!require(presentation.Initialize(windows, viewports, &presentationFailure), "presentation initialize"))
            return false;
        rendering::PresentationOutputDesc outputDesc;
        outputDesc.name = "Primary Presentation";
        outputDesc.window = windowHandle;
        outputDesc.renderExtent = {320, 180};
        rendering::PresentationOutputHandle output;
        if (!require(presentation.CreateOutput(outputDesc, output, &presentationFailure), "output create"))
            return false;
        if (!presentation.Tick(&presentationFailure))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] initial reconcile failed: code=%u window=%u viewport=%u rhi=%u message=%s\n", static_cast<unsigned>(presentationFailure.code),
                         static_cast<unsigned>(presentationFailure.windowFailure.code), static_cast<unsigned>(presentationFailure.viewportFailure.code),
                         static_cast<unsigned>(presentationFailure.rhiFailure.code), presentationFailure.message != nullptr ? presentationFailure.message : "none");
            return false;
        }

        rendering::PresentationOutput* const presentationOutput = presentation.Resolve(output);
        if (!require(presentationOutput != nullptr && presentationOutput->GetState() == rendering::PresentationOutputState::Ready &&
                         presentationOutput->GetSwapChainCreationCount() == 1,
                     "initial presentation output"))
            return false;
        window::PresentationAttachmentSnapshot attachment;
        if (!require(windows.GetSnapshot(presentationOutput->GetAttachment(), attachment) && attachment.acknowledgedPixelExtentRevision == attachment.requiredPixelExtentRevision &&
                         attachment.acknowledgedSurfaceRevision == attachment.requiredSurfaceRevision,
                     "initial acknowledgement"))
            return false;

        rendering::EngineViewportDesc engineDesc;
        engineDesc.contextName = "PresentationTest";
        engineDesc.output = presentationOutput->GetRenderViewport()->GetHandle();
        rendering::EngineViewportHandle engineViewport;
        if (!require(viewports.CreateEngineViewport(engineDesc, engineViewport, &viewportFailure), "engine viewport create"))
            return false;
        if (!require(!presentation.DestroyOutput(output, &presentationFailure) && presentationFailure.code == rendering::PresentationFailureCode::Busy, "referenced output destroy rejection"))
            return false;

        window::BackendWindowEvent event = backend.ChangeState(window::BackendEventType::PixelExtentChanged, {400, 240}, false, false);
        if (!require(windows.ProcessBackendEvent(event, &windowFailure) && presentation.Tick(&presentationFailure), "resize reconcile"))
            return false;
        if (!require(presentationOutput->GetResizeApplicationCount() == 1, "resize telemetry"))
            return false;

        event = backend.ChangeState(window::BackendEventType::Minimized, {400, 240}, true, false);
        if (!require(windows.ProcessBackendEvent(event, &windowFailure) && presentation.Tick(&presentationFailure) &&
                         presentationOutput->GetState() == rendering::PresentationOutputState::Suspended,
                     "suspension reconcile"))
            return false;

        event = backend.ChangeState(window::BackendEventType::HdrStateChanged, {400, 240}, false, true);
        if (!require(windows.ProcessBackendEvent(event, &windowFailure), "surface event") || !require(presentation.Tick(&presentationFailure), "surface tick"))
            return false;
        if (!require(presentationOutput->GetState() == rendering::PresentationOutputState::Ready && presentationOutput->GetSurfaceReplacementCount() == 1 &&
                         presentationOutput->GetSwapChainCreationCount() == 2,
                     "surface replacement telemetry"))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] surface state=%u replacements=%llu creations=%llu\n", static_cast<unsigned>(presentationOutput->GetState()),
                         static_cast<unsigned long long>(presentationOutput->GetSurfaceReplacementCount()),
                         static_cast<unsigned long long>(presentationOutput->GetSwapChainCreationCount()));
            return false;
        }

        if (!require(viewports.DestroyEngineViewport(engineViewport, &viewportFailure), "engine viewport destroy") || !require(presentation.DestroyOutput(output, &presentationFailure), "output destroy") ||
            !require(presentation.Shutdown(&presentationFailure), "presentation shutdown") || !require(viewports.Shutdown(&viewportFailure), "viewport shutdown") ||
            !require(commands.Shutdown(&commandFailure), "render command system shutdown") || !require(commandCameras.Shutdown(&cameraFailure), "render command camera storage shutdown") ||
            !require(commandScenes.Shutdown(&sceneFailure), "render command scene manager shutdown") || !require(windows.DestroyWindow(windowHandle, &windowFailure), "window destroy") ||
            !require(windows.Shutdown(&windowFailure), "window manager shutdown"))
            return false;
        const bool retired = gpu::WaitIdle() && gpu::RetireResources();
        DestroyWindow(nativeWindow);
        return retired;
    }

    [[nodiscard]] bool TestSwapChain(const HWND window, gpu::Failure& failure) noexcept
    {
        ShowWindow(window, SW_SHOWNA);
        UpdateWindow(window);
        gpu::SwapChainDesc desc{};
        desc.surface = {gpu::PresentationSurfaceKind::Win32, window, nullptr};
        desc.width = 320;
        desc.height = 180;
        desc.bufferCount = 3;
        desc.format = gpu::Format::B8G8R8A8UNorm;
        desc.presentMode = gpu::PresentMode::Fifo;
        desc.allowTearing = true;
        desc.colorSpace = gpu::ColorSpace::Srgb;
        gpu::SwapChainDesc invalidLatencyDesc = desc;
        invalidLatencyDesc.frameLatency.maximumFramesInFlight = 0;
        if (gpu::CreateSwapChainWithBackBuffer(invalidLatencyDesc, &failure).IsValid() || failure.code != gpu::FailureCode::InvalidArgument)
            return false;
        gpu::SwapChain swapChain(gpu::AdoptReference, gpu::CreateSwapChainWithBackBuffer(desc, &failure));
        if (!swapChain)
            return false;
        gpu::SetResourceDebugName(swapChain, "RHI Conformance SwapChain");

        if (gpu::SetSwapChainPresentParameters(swapChain, {gpu::PresentMode::Immediate, 1, false}, &failure) || failure.code != gpu::FailureCode::InvalidArgument)
            return false;
        if (!gpu::SetSwapChainPresentParameters(swapChain, {gpu::PresentMode::Immediate, 0, false}, &failure))
            return false;

        gpu::AcquiredBackBuffer abandonedBackBuffer;
        if (!gpu::AcquireBackBuffer(swapChain, abandonedBackBuffer, &failure) || !abandonedBackBuffer.IsValid())
            return false;
        gpu::AcquiredBackBuffer duplicateAcquisition;
        if (gpu::AcquireBackBuffer(swapChain, duplicateAcquisition, &failure) || failure.code != gpu::FailureCode::Busy)
            return false;
        if (gpu::ResizeBackbuffer(400, 240, swapChain, &failure) || failure.code != gpu::FailureCode::Busy)
            return false;

        gpu::TextureRef retainedBackBuffer = abandonedBackBuffer.texture;
        gpu::AddRef(retainedBackBuffer);
        if (!gpu::AbandonBackBuffer(abandonedBackBuffer, &failure))
        {
            static_cast<void>(gpu::SafeRelease(retainedBackBuffer));
            return false;
        }
        if (gpu::ResizeBackbuffer(400, 240, swapChain, &failure) || failure.code != gpu::FailureCode::Busy)
        {
            static_cast<void>(gpu::SafeRelease(retainedBackBuffer));
            return false;
        }
        static_cast<void>(gpu::SafeRelease(retainedBackBuffer));
        if (!gpu::ResizeBackbuffer(400, 240, swapChain, &failure))
            return false;

        gpu::AcquiredBackBuffer backBuffer;
        if (!gpu::AcquireBackBuffer(swapChain, backBuffer, &failure) || backBuffer.width != 400 || backBuffer.height != 240)
            return false;
        gpu::AcquiredBackBuffer staleBackBuffer = backBuffer;
        ++staleBackBuffer.serial;
        if (gpu::AbandonBackBuffer(staleBackBuffer, &failure) || failure.code != gpu::FailureCode::InvalidReference)
            return false;

        gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x5357415043484149ull, &failure);
        gpu::RenderTargetSetup renderTargets{};
        renderTargets.colorTargets[0].texture = backBuffer.texture;
        renderTargets.colorTargetCount = 1;
        if (!commandList || !gpu::BindCommandList(commandList, &failure) || !gpu::SetupRenderTargets(renderTargets, &failure) ||
            !gpu::TransitionSwapChainPresent(backBuffer, &failure))
        {
            if (gpu::GetBoundCommandList().IsValid())
                gpu::UnbindCommandList();
            if (commandList.IsValid())
                gpu::DiscardCommandList(commandList);
            return false;
        }
        if (gpu::TransitionSwapChainPresent(backBuffer, &failure) || failure.code != gpu::FailureCode::InvalidArgument)
        {
            gpu::UnbindCommandList();
            gpu::DiscardCommandList(commandList);
            return false;
        }
        gpu::UnbindCommandList();
        if (gpu::Present(backBuffer, &failure) || failure.code != gpu::FailureCode::Busy)
        {
            gpu::DiscardCommandList(commandList);
            return false;
        }
        const gpu::CommandListRef submissions[] = {commandList};
        gpu::GpuFence completion{};
        if (!gpu::CloseAndSubmitCommandLists("swap-chain present transition", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure))
            return false;
        if (!gpu::Present(backBuffer, &failure))
            return false;
        if (gpu::Present(backBuffer, &failure) || failure.code != gpu::FailureCode::InvalidReference)
            return false;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!gpu::ResizeBackbuffer(480, 270, swapChain, &failure))
            return false;
        gpu::AcquiredBackBuffer finalBackBuffer;
        if (!gpu::AcquireBackBuffer(swapChain, finalBackBuffer, &failure) || !gpu::AbandonBackBuffer(finalBackBuffer, &failure))
            return false;
        const gpu::SwapChainStats stats = gpu::GetSwapChainStats(swapChain);
        if (stats.state != gpu::SwapChainState::Available || stats.acquisitions != 3 || stats.abandonedAcquisitions != 2 || stats.presentedFrames != 1 || stats.resizeCount != 2 || stats.width != 480 ||
            stats.height != 270 || stats.rejectedOperations < 4 || !stats.frameLatency.enabled || stats.frameLatency.maximumFramesInFlight != 2 || stats.frameLatencyWaits != stats.acquisitions ||
            stats.frameLatencyTimeouts != 0 || stats.colorSpace != gpu::ColorSpace::Srgb)
            return false;

        swapChain.Reset();
        return gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
    }

    [[nodiscard]] bool TestGpuSceneLifetime(gpu::Failure& failure) noexcept
    {
        namespace rendering = vanguard::rendering;
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        if (!descriptors)
            return false;

        rendering::GpuSceneTables tables;
        rendering::GpuSceneTablesFailure tableFailure;
        if (!tables.Initialize({descriptors, 2}, &tableFailure) || !tables.GetDirectoryBinding().IsValid())
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene tables failed: %s\n", tableFailure.message);
            return false;
        }
        rendering::GpuSceneTableDirectory primitiveDirectory;
        if (!tables.GetTableDirectory(rendering::GpuSceneTableKind::Primitive, primitiveDirectory) || primitiveDirectory.pageShift != 15 || primitiveDirectory.pageMask != 32'767 ||
            primitiveDirectory.elementsPerPage != 32'768 || primitiveDirectory.elementStride != sizeof(rendering::GpuPrimitive))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene primitive directory contract failed\n");
            return false;
        }
        rendering::GpuSceneLifetime lifetime;
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        rendering::GpuSceneLifetimeConfig lifetimeConfig;
        lifetimeConfig.retirementEpochCount = 4;
        if (!lifetime.Initialize(tables, lifetimeConfig, &lifetimeFailure))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene lifetime failed: %s\n", lifetimeFailure.message);
            return false;
        }

        const auto SubmitRetirementFences = [&failure](const vanguard::u64 name, gpu::ResidencyFenceSet& submitted) noexcept
        {
            submitted = {};
            const gpu::CommandListType types[] = {gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
            for (vanguard::u32 index = 0; index < 3; ++index)
            {
                gpu::CommandListRef list = gpu::CreateCommandList(types[index], name + index, &failure);
                if (!list || !gpu::BindCommandList(list, &failure))
                    return false;
                gpu::UnbindCommandList();
                const gpu::CommandListRef lists[] = {list};
                gpu::GpuFence completion;
                if (!gpu::CloseAndSubmitCommandLists("GPU Scene retirement queue coverage", {lists, 1}, gpu::CommandListSyncType::None, completion, &failure))
                    return false;
                submitted.Include(completion);
            }
            return true;
        };
        const auto WaitRetirementFences = [&failure](const gpu::ResidencyFenceSet& fences) noexcept
        {
            return gpu::WaitForGpuFence({gpu::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) && gpu::WaitForGpuFence({gpu::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
                   gpu::WaitForGpuFence({gpu::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
        };

        rendering::GpuSceneUploader uploader;
        rendering::GpuSceneUploadFailure uploadFailure;
        rendering::GpuSceneUploadConfig uploadConfig;
        uploadConfig.bytesPerSegment = 2u * 1024u * 1024u;
        uploadConfig.maximumUpdatesPerBatch = 64;
        uploadConfig.maximumCopiesPerBatch = 128;
        if (!uploader.Initialize(tables, lifetime, uploadConfig, &uploadFailure))
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene uploader failed: %s\n", uploadFailure.message);
            return false;
        }

        rendering::RenderSceneManager sceneManager;
        rendering::RenderSceneFailure sceneFailure;
        rendering::RenderSceneGpuPublisher scenePublisher;
        rendering::RenderSceneGpuFailure sceneGpuFailure;
        const auto ScenePublicationFailure = [](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rhiNvrhiTests] RenderScene GPU publication failed at %s\n", stage);
            return false;
        };
        const auto LifetimeFailure = [](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene lifetime failed at %s\n", stage);
            return false;
        };
        if (!sceneManager.Initialize({}, &sceneFailure) || !scenePublisher.Initialize(sceneManager, lifetime, {}, &sceneGpuFailure))
            return ScenePublicationFailure("initialization");
        rendering::RenderSceneDesc sceneDesc;
        sceneDesc.name = "GPU Scene publication test";
        sceneDesc.maximumProxies = 32;
        sceneDesc.maximumPendingProxyMutations = 32;
        rendering::RenderSceneHandle scene;
        if (!sceneManager.CreateScene(sceneDesc, scene, &sceneFailure))
            return ScenePublicationFailure("scene creation");

        rendering::LightProxyDesc lightDesc;
        lightDesc.proxy.scene = scene;
        lightDesc.proxy.debugName = "Publication test light";
        lightDesc.range = 25.0f;
        rendering::RenderProxyHandle lightProxy;
        rendering::RenderSceneGpuIdentity lightIdentity;
        if (!sceneManager.CreateLightProxy(lightDesc, lightProxy, &sceneFailure) || !scenePublisher.GetIdentity(lightProxy, lightIdentity) || lightIdentity.kind != rendering::RenderSceneGpuObjectKind::Light ||
            !sceneManager.UpdateProxyVisibility(lightProxy, rendering::RenderProxyVisibilityFlags::Visible, 0x0f0f0f0fu, &sceneFailure) ||
            !sceneManager.UpdateProxyVisibility(lightProxy, rendering::RenderProxyVisibilityFlags::Visible, 0x00ff00ffu, &sceneFailure) || scenePublisher.GetStats().pendingChanges != 1 ||
            scenePublisher.GetStats().coalescedChanges < 2)
            return ScenePublicationFailure("proxy admission and coalescing");

        rendering::RenderSceneFramePrepareResult scenePrepare;
        rendering::RenderSceneUpdateResult sceneUpdate;
        vanguard::jobs::Builder sceneBuilder;
        if (!sceneManager.PrepareSceneUpdate(scene, 1, scenePrepare, &sceneFailure) || !sceneManager.ExecuteSceneUpdate(scene, sceneBuilder, sceneUpdate, &sceneFailure))
            return ScenePublicationFailure("first scene update");
        rendering::RenderSceneGpuPublication scenePublication;
        vanguard::containers::DynamicArray<rendering::GpuSceneUploadRequest> sceneRequests(vanguard::memory::pools::Rendering::GetInstance());
        if (!scenePublisher.Prepare(scene, sceneUpdate.mutationEpoch, scenePublication, &sceneGpuFailure) || scenePublication.changeCount != 1 || scenePublication.retirements.Size() != 0 ||
            !scenePublisher.BuildUploadRequests(scenePublication, sceneRequests, &sceneGpuFailure) || sceneRequests.Size() != 1)
            return ScenePublicationFailure("publication planning");
        rendering::GpuSceneUploadReservation sceneReservation;
        if (!uploader.Begin({&sceneRequests[0], 1}, {&sceneReservation, 1}, &uploadFailure))
            return ScenePublicationFailure("direct staging write");
        const vanguard::u32 sceneWriteRangeCount = scenePublisher.GetWriteRangeCount(scenePublication);
        for (vanguard::u32 rangeIndex = 0; rangeIndex < sceneWriteRangeCount; ++rangeIndex)
            if (!scenePublisher.WriteRange(scenePublication, {&sceneReservation, 1}, rangeIndex, &sceneGpuFailure))
                return ScenePublicationFailure("direct staging range write");
        if (!scenePublisher.FinishWrites(scenePublication, &sceneGpuFailure) || !uploader.Complete(sceneReservation, &uploadFailure))
            return ScenePublicationFailure("direct staging completion");
        rendering::GpuSceneUploadResult sceneUpload;
        if (!uploader.Submit(sceneUpload, &uploadFailure) || !scenePublisher.Complete(scenePublication, &sceneGpuFailure) ||
            lifetime.GetState(lightIdentity.allocation) != rendering::GpuSceneAllocationState::Active)
            return ScenePublicationFailure("initial publication");

        if (!sceneManager.DestroyProxy(lightProxy, &sceneFailure) || !sceneManager.PrepareSceneUpdate(scene, 2, scenePrepare, &sceneFailure) ||
            !sceneManager.ExecuteSceneUpdate(scene, sceneBuilder, sceneUpdate, &sceneFailure) || !scenePublisher.Prepare(scene, sceneUpdate.mutationEpoch, scenePublication, &sceneGpuFailure) ||
            scenePublication.changeCount != 0 || scenePublication.retirements.Size() != 1 || !scenePublisher.Complete(scenePublication, &sceneGpuFailure))
            return ScenePublicationFailure("retirement publication");
        gpu::ResidencyFenceSet sceneRetirementFences;
        if (!SubmitRetirementFences(0x4750555343505542ull, sceneRetirementFences) || !lifetime.SealRetirements(sceneRetirementFences, &lifetimeFailure) || !WaitRetirementFences(sceneRetirementFences) ||
            lifetime.Collect(&lifetimeFailure) != 1 || !sceneManager.DestroyScene(scene, &sceneFailure))
            return ScenePublicationFailure("retirement collection and shutdown");

        rendering::RenderSceneDesc detachSceneDesc;
        detachSceneDesc.name = "GPU Scene tracked-count detach test";
        detachSceneDesc.maximumProxies = 8;
        detachSceneDesc.maximumPendingProxyMutations = 8;
        rendering::RenderSceneHandle detachScene;
        if (!sceneManager.CreateScene(detachSceneDesc, detachScene, &sceneFailure))
            return ScenePublicationFailure("tracked-count scene creation");
        rendering::MeshProxyDesc instanceDesc;
        instanceDesc.proxy.scene = detachScene;
        instanceDesc.proxy.debugName = "tracked-count instance";
        instanceDesc.proxy.bounds.minimum[0] = instanceDesc.proxy.bounds.minimum[1] = instanceDesc.proxy.bounds.minimum[2] = -1.0f;
        instanceDesc.proxy.bounds.maximum[0] = instanceDesc.proxy.bounds.maximum[1] = instanceDesc.proxy.bounds.maximum[2] = 1.0f;
        instanceDesc.mesh = vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("meshes/tracked_count.vmesh"), vanguard::meshes::MeshResourceType);
        rendering::RenderProxyHandle instanceProxy;
        rendering::LightProxyDesc trackedLightDesc;
        trackedLightDesc.proxy.scene = detachScene;
        trackedLightDesc.proxy.debugName = "tracked-count light";
        trackedLightDesc.range = 5.0f;
        rendering::RenderProxyHandle trackedLight;
        rendering::DecalProxyDesc trackedDecalDesc;
        trackedDecalDesc.proxy.scene = detachScene;
        trackedDecalDesc.proxy.debugName = "tracked-count decal";
        trackedDecalDesc.material = vanguard::resources::ResourceReference(vanguard::resources::ResourcePath::FromString("materials/tracked_count.vmat"),
                                                                           vanguard::materials::MaterialResourceType);
        rendering::RenderProxyHandle trackedDecal;
        if (!sceneManager.CreateMeshProxy(instanceDesc, instanceProxy, &sceneFailure) ||
            !sceneManager.CreateLightProxy(trackedLightDesc, trackedLight, &sceneFailure) ||
            !sceneManager.CreateDecalProxy(trackedDecalDesc, trackedDecal, &sceneFailure))
            return ScenePublicationFailure("tracked-count proxy admission");
        const rendering::RenderSceneGpuStats trackedCountStats = scenePublisher.GetStats();
        if (trackedCountStats.trackedInstances != 1 || trackedCountStats.trackedLights != 1 || trackedCountStats.trackedDecals != 1 || trackedCountStats.pendingChanges != 3)
            return ScenePublicationFailure("tracked-count admission accounting");
        if (!sceneManager.DestroyProxy(instanceProxy, &sceneFailure) || !sceneManager.DestroyProxy(trackedLight, &sceneFailure) || !sceneManager.DestroyProxy(trackedDecal, &sceneFailure))
            return ScenePublicationFailure("tracked-count proxy retirement");
        const rendering::RenderSceneGpuStats retiredCountStats = scenePublisher.GetStats();
        if (retiredCountStats.trackedInstances != 0 || retiredCountStats.trackedLights != 0 || retiredCountStats.trackedDecals != 0 || retiredCountStats.pendingChanges != 0 ||
            retiredCountStats.pendingRetirements != 3)
            return ScenePublicationFailure("tracked-count retirement accounting");
        if (!sceneManager.PrepareSceneUpdate(detachScene, 3, scenePrepare, &sceneFailure) || !sceneManager.ExecuteSceneUpdate(detachScene, sceneBuilder, sceneUpdate, &sceneFailure) ||
            !scenePublisher.Prepare(detachScene, sceneUpdate.mutationEpoch, scenePublication, &sceneGpuFailure) || scenePublication.changeCount != 0 ||
            scenePublication.retirements.Size() != 3 || !scenePublisher.BuildUploadRequests(scenePublication, sceneRequests, &sceneGpuFailure) || sceneRequests.Size() != 1 ||
            !uploader.Begin({sceneRequests.TypedData(), sceneRequests.Size()}, {&sceneReservation, 1}, &uploadFailure))
            return ScenePublicationFailure("tracked-count detach and shutdown");
        const vanguard::u32 retirementWriteRangeCount = scenePublisher.GetWriteRangeCount(scenePublication);
        for (vanguard::u32 rangeIndex = 0; rangeIndex < retirementWriteRangeCount; ++rangeIndex)
            if (!scenePublisher.WriteRange(scenePublication, {&sceneReservation, 1}, rangeIndex, &sceneGpuFailure))
                return ScenePublicationFailure("tracked-count retirement tombstone write");
        if (!scenePublisher.FinishWrites(scenePublication, &sceneGpuFailure) || !uploader.Complete(sceneReservation, &uploadFailure) || !uploader.Submit(sceneUpload, &uploadFailure) ||
            !scenePublisher.Complete(scenePublication, &sceneGpuFailure) || scenePublisher.GetStats().pendingRetirements != 0)
            return ScenePublicationFailure("tracked-count detach and shutdown");
        gpu::ResidencyFenceSet trackedCountRetirementFences;
        if (!SubmitRetirementFences(0x4750555343545243ull, trackedCountRetirementFences))
            return ScenePublicationFailure("tracked-count retirement fence submission");
        if (!lifetime.SealRetirements(trackedCountRetirementFences, &lifetimeFailure))
            return LifetimeFailure("tracked-count retirement sealing");
        if (!WaitRetirementFences(trackedCountRetirementFences))
            return ScenePublicationFailure("tracked-count retirement fence wait");
        const vanguard::u32 trackedCountCollected = lifetime.Collect(&lifetimeFailure);
        // Only the unpublished Instance receives a tombstone upload. That upload
        // commits its initial publication before Complete moves it to Retiring, so
        // it must pass through the fenced lifetime collector. The unpublished Light
        // and Decal identities have no retirement upload and Complete cancels them
        // directly from Allocated instead.
        if (trackedCountCollected != 1)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] tracked-count collected=%u pending=%u\n", trackedCountCollected, lifetime.GetStats().pendingRetirements);
            return LifetimeFailure("tracked-count retirement collection");
        }
        if (!sceneManager.DestroyScene(detachScene, &sceneFailure))
            return ScenePublicationFailure("tracked-count scene destruction");
        if (!scenePublisher.Shutdown(&sceneGpuFailure))
            return ScenePublicationFailure("tracked-count publisher shutdown");
        if (!sceneManager.Shutdown(&sceneFailure))
            return ScenePublicationFailure("tracked-count manager shutdown");

        rendering::GpuSceneAllocation uploadedInstances[2];
        if (!lifetime.Allocate<rendering::GpuInstance>(1, uploadedInstances[0], &lifetimeFailure) || !lifetime.Allocate<rendering::GpuInstance>(1, uploadedInstances[1], &lifetimeFailure))
            return LifetimeFailure("sparse-upload allocation");
        const rendering::GpuSceneUploadRequest uploadRequests[] = {{uploadedInstances[1], 0, 1}, {uploadedInstances[0], 0, 1}, {uploadedInstances[0], 0, 1}};
        rendering::GpuSceneUploadReservation uploadReservations[3];
        if (!uploader.Begin({uploadRequests, 3}, {uploadReservations, 3}, &uploadFailure) || uploadReservations[0].destination == nullptr || uploadReservations[1].IsValid() ||
            uploadReservations[2].destination == nullptr)
            return LifetimeFailure("sparse-upload admission");

        auto* const secondInstance = static_cast<rendering::GpuInstance*>(uploadReservations[0].destination);
        auto* const firstInstance = static_cast<rendering::GpuInstance*>(uploadReservations[2].destination);
        *firstInstance = {};
        *secondInstance = {};
        firstInstance->boundsRadius = 11.0f;
        firstInstance->deformation = 0x11111111u;
        secondInstance->boundsRadius = 22.0f;
        secondInstance->deformation = 0x22222222u;
        rendering::GpuSceneUploadResult uploadResult;
        if (uploader.Submit(uploadResult, &uploadFailure) || uploadFailure.code != rendering::GpuSceneUploadFailureCode::BatchNotReady || !uploader.Complete(uploadReservations[0], &uploadFailure) ||
            !uploader.Complete(uploadReservations[2], &uploadFailure) || !uploader.Submit(uploadResult, &uploadFailure) || !uploadResult.completion.IsValid() || uploadResult.requestedUpdates != 3 ||
            uploadResult.uniqueUpdates != 2 || uploadResult.copyCount != 1 || uploadResult.affectedPages != 1 || uploadResult.uploadedBytes != sizeof(rendering::GpuInstance) * 2 ||
            uploadResult.supersededBytes != sizeof(rendering::GpuInstance) || lifetime.GetState(uploadedInstances[0]) != rendering::GpuSceneAllocationState::Active ||
            lifetime.GetState(uploadedInstances[1]) != rendering::GpuSceneAllocationState::Active)
            return LifetimeFailure("sparse-upload submission");

        const rendering::GpuSceneUploadRequest activeUpdateRequest{uploadedInstances[1], 0, 1};
        rendering::GpuSceneUploadReservation activeUpdateReservation;
        if (!uploader.Begin({&activeUpdateRequest, 1}, {&activeUpdateReservation, 1}, &uploadFailure) || !activeUpdateReservation.IsValid())
            return LifetimeFailure("active-update admission");
        vanguard::concurrency::Atomic<bool> producerCompleted{false};
        std::thread producer(
            [&uploader, &activeUpdateReservation, &secondInstance, &producerCompleted]() noexcept
            {
                auto* const updatedInstance = static_cast<rendering::GpuInstance*>(activeUpdateReservation.destination);
                *updatedInstance = *secondInstance;
                updatedInstance->boundsRadius = 33.0f;
                updatedInstance->deformation = 0x33333333u;
                rendering::GpuSceneUploadFailure producerFailure;
                producerCompleted.SetValue(uploader.Complete(activeUpdateReservation, &producerFailure));
            });
        producer.join();
        rendering::GpuSceneUploadResult activeUpdateResult;
        if (!producerCompleted.GetValue() || !uploader.Submit(activeUpdateResult, &uploadFailure) || activeUpdateResult.copyCount != 1 || activeUpdateResult.uploadedBytes != sizeof(rendering::GpuInstance) ||
            !gpu::WaitForGpuFence(activeUpdateResult.completion, 5'000'000'000ull, &failure))
            return LifetimeFailure("active-update submission");

        rendering::GpuSceneTablePage uploadedInstancePage;
        gpu::BufferDesc uploadReadbackDesc;
        uploadReadbackDesc.size = sizeof(rendering::GpuInstance) * 2;
        uploadReadbackDesc.usage = gpu::BufferUsage::CopyDestination;
        uploadReadbackDesc.initialState = gpu::ResourceState::CopyDestination;
        uploadReadbackDesc.memoryType = gpu::MemoryType::Readback;
        gpu::BufferRef uploadReadback = gpu::CreateBuffer(uploadReadbackDesc, {}, &failure);
        gpu::CommandListRef uploadReadbackCommands = gpu::CreateCommandList(gpu::CommandListType::CopySync, 0x4750555343524541ull, &failure);
        const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
        if (!tables.GetPage<rendering::GpuInstance>(0, uploadedInstancePage) || !uploadReadback || !uploadReadbackCommands || !gpu::BindCommandList(uploadReadbackCommands, &failure) ||
            !gpu::TransitionBuffer(uploadedInstancePage.buffer, gpu::ResourceState::Unknown, gpu::ResourceState::CopySource, &failure) ||
            !gpu::CopyBuffer(uploadReadback, 0, uploadedInstancePage.buffer, static_cast<vanguard::u64>(uploadedInstances[0].first) * sizeof(rendering::GpuInstance), sizeof(rendering::GpuInstance), &failure) ||
            !gpu::CopyBuffer(uploadReadback, sizeof(rendering::GpuInstance), uploadedInstancePage.buffer,
                             static_cast<vanguard::u64>(uploadedInstances[1].first) * sizeof(rendering::GpuInstance), sizeof(rendering::GpuInstance), &failure) ||
            !gpu::TransitionBuffer(uploadedInstancePage.buffer, gpu::ResourceState::Unknown, shaderRead, &failure))
            return LifetimeFailure("sparse-upload readback recording");
        gpu::UnbindCommandList();
        gpu::GpuFence uploadReadbackCompletion;
        const gpu::CommandListRef uploadReadbackSubmission[] = {uploadReadbackCommands};
        if (!gpu::CloseAndSubmitCommandLists("GPU Scene sparse upload readback", {uploadReadbackSubmission, 1}, gpu::CommandListSyncType::None, uploadReadbackCompletion, &failure) ||
            !gpu::WaitForGpuFence(uploadReadbackCompletion, 5'000'000'000ull, &failure))
            return LifetimeFailure("sparse-upload readback submission");
        const auto* const uploadedData = static_cast<const rendering::GpuInstance*>(gpu::LockBuffer(uploadReadback, 0, sizeof(rendering::GpuInstance) * 2, &failure));
        if (uploadedData == nullptr || uploadedData[0].boundsRadius != 11.0f || uploadedData[0].deformation != 0x11111111u ||
            uploadedData[1].boundsRadius != 33.0f || uploadedData[1].deformation != 0x33333333u)
            return LifetimeFailure("sparse-upload readback verification");
        gpu::UnlockBuffer(uploadReadback);
        static_cast<void>(gpu::SafeRelease(uploadReadback));

        gpu::ResidencyFenceSet uploadedRetirementFences;
        if (!lifetime.RetireBatch({uploadedInstances, 2}, &lifetimeFailure) || !SubmitRetirementFences(0x4750555343555052ull, uploadedRetirementFences) ||
            !lifetime.SealRetirements(uploadedRetirementFences, &lifetimeFailure) || !WaitRetirementFences(uploadedRetirementFences) || lifetime.Collect(&lifetimeFailure) != 2)
            return LifetimeFailure("sparse-upload retirement");

        rendering::GpuSceneAllocation instance;
        rendering::GpuSceneAllocation primitives;
        if (!lifetime.Allocate<rendering::GpuInstance>(1, instance, &lifetimeFailure) || !lifetime.Allocate<rendering::GpuPrimitive>(37, primitives, &lifetimeFailure) ||
            !lifetime.CommitInitialPublication(instance, &lifetimeFailure) || !lifetime.CommitInitialPublication(primitives, &lifetimeFailure) || instance.count != 1 || primitives.count != 37 ||
            lifetime.GetState(instance) != rendering::GpuSceneAllocationState::Active || lifetime.GetState(primitives) != rendering::GpuSceneAllocationState::Active)
            return LifetimeFailure("basic allocation and publication");

        rendering::GpuSceneTablePage instancePage;
        rendering::GpuSceneElementAddress primitiveAddress;
        if (!tables.GetPage<rendering::GpuInstance>(0, instancePage) || !tables.Resolve<rendering::GpuPrimitive>(primitives.first + 36, primitiveAddress) || !instancePage.IsMaterialized() ||
            primitiveAddress.element != primitives.first + 36)
            return LifetimeFailure("basic table resolution");

        if (!lifetime.Retire(instance, &lifetimeFailure) || !lifetime.Retire(primitives, &lifetimeFailure) || lifetime.Collect(&lifetimeFailure) != 0 || lifetime.GetStats().pendingRetirements != 2 ||
            lifetime.SealRetirements({}, &lifetimeFailure) || lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::MissingRetirementFence)
            return LifetimeFailure("missing-retirement-fence rejection");

        gpu::ResidencyFenceSet partialCoverage;
        partialCoverage.graphics = 1;
        if (lifetime.SealRetirements(partialCoverage, &lifetimeFailure) || lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::MissingRetirementFence)
            return LifetimeFailure("partial-retirement-fence rejection");

        gpu::ResidencyFenceSet safeAfter;
        if (!SubmitRetirementFences(0x4750555343454e45ull, safeAfter))
            return LifetimeFailure("retirement fence submission");
        if (!lifetime.SealRetirements(safeAfter, &lifetimeFailure) || !WaitRetirementFences(safeAfter) || lifetime.Collect(&lifetimeFailure) != 2 || lifetime.IsValid(instance) || lifetime.IsValid(primitives))
            return LifetimeFailure("retirement collection");

        rendering::GpuSceneAllocation reusedInstance;
        rendering::GpuSceneAllocation reusedPrimitives;
        if (!lifetime.Allocate<rendering::GpuInstance>(1, reusedInstance, &lifetimeFailure) || !lifetime.Allocate<rendering::GpuPrimitive>(37, reusedPrimitives, &lifetimeFailure) ||
            reusedInstance.first != instance.first || reusedInstance.generation == instance.generation || reusedPrimitives.first != primitives.first || reusedPrimitives.generation == primitives.generation ||
            !lifetime.Cancel(reusedInstance, &lifetimeFailure) || !lifetime.Cancel(reusedPrimitives, &lifetimeFailure) || lifetime.CommitInitialPublication(instance, &lifetimeFailure) ||
            lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::InvalidState)
            return LifetimeFailure("identity reuse and stale rejection");

        const vanguard::u32 parameterPageSize = rendering::GpuSceneElementsPerPage<rendering::GpuMaterialParameterWord>();
        rendering::GpuSceneAllocation crossPageParameters;
        rendering::GpuSceneElementAddress parameterAddress;
        if (!lifetime.ReserveCapacity(rendering::GpuSceneTableKind::MaterialParameterWord, parameterPageSize + 2, &lifetimeFailure) ||
            !lifetime.Allocate<rendering::GpuMaterialParameterWord>(parameterPageSize + 2, crossPageParameters, &lifetimeFailure) ||
            !tables.Resolve<rendering::GpuMaterialParameterWord>(crossPageParameters.first + parameterPageSize + 1, parameterAddress) || parameterAddress.page != 1 || parameterAddress.element != 1)
            return LifetimeFailure("cross-page material-parameter allocation");

        rendering::GpuSceneAllocation capacityProbe;
        if (lifetime.Allocate<rendering::GpuMaterialParameterWord>(parameterPageSize + 2, capacityProbe, &lifetimeFailure) ||
            lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::TableGrowthFailure || lifetimeFailure.tableFailure.code != rendering::GpuSceneTablesFailureCode::CapacityExceeded)
            return LifetimeFailure("live material-parameter capacity rejection");

        const rendering::GpuSceneUploadRequest crossPageRequest{crossPageParameters, 0, crossPageParameters.count};
        rendering::GpuSceneUploadReservation crossPageReservation;
        if (!uploader.Begin({&crossPageRequest, 1}, {&crossPageReservation, 1}, &uploadFailure) || !crossPageReservation.IsValid())
            return LifetimeFailure("cross-page material-parameter upload admission");
        auto* const crossPageData = static_cast<rendering::GpuMaterialParameterWord*>(crossPageReservation.destination);
        for (vanguard::u32 index = 0; index < crossPageParameters.count; ++index)
            crossPageData[index] = {};
        crossPageData[parameterPageSize - 1].value = 0x44332211u;
        crossPageData[parameterPageSize].value = 0x88776655u;
        crossPageData[parameterPageSize + 1].value = 0xccbbaa99u;
        rendering::GpuSceneUploadResult crossPageResult;
        if (!uploader.Complete(crossPageReservation, &uploadFailure) || !uploader.Submit(crossPageResult, &uploadFailure) || crossPageResult.copyCount != 2 || crossPageResult.affectedPages != 2 ||
            !gpu::WaitForGpuFence(crossPageResult.completion, 5'000'000'000ull, &failure))
            return LifetimeFailure("cross-page material-parameter upload submission");

        rendering::GpuMaterialParameterWord parameterWords[3];
        if (!ReadGpuSceneElement(tables, crossPageParameters.first + parameterPageSize - 1, parameterWords[0], failure, 0x4750555343504100ull) ||
            !ReadGpuSceneElement(tables, crossPageParameters.first + parameterPageSize, parameterWords[1], failure, 0x4750555343504101ull) ||
            !ReadGpuSceneElement(tables, crossPageParameters.first + parameterPageSize + 1, parameterWords[2], failure, 0x4750555343504102ull) || parameterWords[0].value != 0x44332211u ||
            parameterWords[1].value != 0x88776655u || parameterWords[2].value != 0xccbbaa99u || ((parameterWords[0].value >> 24u) | (parameterWords[1].value << 8u)) != 0x77665544u)
            return LifetimeFailure("exact and unaligned cross-page material-parameter readback");

        const rendering::GpuSceneUploadRequest overlappingRequests[] = {{crossPageParameters, 0, 2}, {crossPageParameters, 1, 2}};
        rendering::GpuSceneUploadReservation overlappingReservations[2];
        if (uploader.Begin({overlappingRequests, 2}, {overlappingReservations, 2}, &uploadFailure) || uploadFailure.code != rendering::GpuSceneUploadFailureCode::OverlappingUpdates ||
            !lifetime.Retire(crossPageParameters, &lifetimeFailure) || lifetime.Allocate<rendering::GpuMaterialParameterWord>(parameterPageSize + 2, capacityProbe, &lifetimeFailure) ||
            lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::TableGrowthFailure || lifetimeFailure.tableFailure.code != rendering::GpuSceneTablesFailureCode::CapacityExceeded)
            return LifetimeFailure("overlap rejection");
        gpu::ResidencyFenceSet crossPageRetirementFences;
        if (!SubmitRetirementFences(0x4750555343585047ull, crossPageRetirementFences) || !lifetime.SealRetirements(crossPageRetirementFences, &lifetimeFailure) ||
            !WaitRetirementFences(crossPageRetirementFences) || lifetime.Allocate<rendering::GpuMaterialParameterWord>(parameterPageSize + 2, capacityProbe, &lifetimeFailure) ||
            lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::TableGrowthFailure || lifetimeFailure.tableFailure.code != rendering::GpuSceneTablesFailureCode::CapacityExceeded ||
            lifetime.Collect(&lifetimeFailure) != 1)
            return LifetimeFailure("fence-delayed material-parameter retirement");

        rendering::GpuSceneAllocation reusedParameters;
        if (!lifetime.Allocate<rendering::GpuMaterialParameterWord>(parameterPageSize + 2, reusedParameters, &lifetimeFailure) || reusedParameters.first != crossPageParameters.first ||
            reusedParameters.generation == crossPageParameters.generation || !lifetime.Cancel(reusedParameters, &lifetimeFailure))
            return LifetimeFailure("material-parameter range reuse");

        rendering::GpuSceneAllocation fragmentedParameters[3];
        rendering::GpuSceneAllocation coalescedParameters;
        if (!lifetime.Allocate<rendering::GpuMaterialParameterWord>(4, fragmentedParameters[0], &lifetimeFailure) ||
            !lifetime.Allocate<rendering::GpuMaterialParameterWord>(4, fragmentedParameters[1], &lifetimeFailure) ||
            !lifetime.Allocate<rendering::GpuMaterialParameterWord>(4, fragmentedParameters[2], &lifetimeFailure) || !lifetime.Cancel(fragmentedParameters[0], &lifetimeFailure) ||
            !lifetime.Cancel(fragmentedParameters[1], &lifetimeFailure) || !lifetime.Allocate<rendering::GpuMaterialParameterWord>(8, coalescedParameters, &lifetimeFailure) ||
            coalescedParameters.first != fragmentedParameters[0].first || !lifetime.Cancel(coalescedParameters, &lifetimeFailure) || !lifetime.Cancel(fragmentedParameters[2], &lifetimeFailure))
            return LifetimeFailure("material-parameter fragmentation recovery");

        constexpr vanguard::u32 batchSize = 64;
        rendering::GpuSceneAllocationRequest batchRequests[batchSize];
        rendering::GpuSceneAllocation batchAllocations[batchSize];
        for (rendering::GpuSceneAllocationRequest& request : batchRequests)
            request = {rendering::GpuSceneTableKind::Instance, 1};

        gpu::ResidencyFenceSet lastSealedFences;
        gpu::ResidencyFenceSet stalledEpochSafeAfter;
        for (vanguard::u32 epochIndex = 0; epochIndex < 4; ++epochIndex)
        {
            if (!lifetime.AllocateBatch({batchRequests, batchSize}, {batchAllocations, batchSize}, &lifetimeFailure))
                return false;
            if (epochIndex == 0)
            {
                const rendering::GpuSceneAllocation duplicateAllocations[] = {batchAllocations[0], batchAllocations[0]};
                if (lifetime.CommitInitialPublications({duplicateAllocations, 2}, &lifetimeFailure) || lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::DuplicateAllocation ||
                    lifetime.GetState(batchAllocations[0]) != rendering::GpuSceneAllocationState::Allocated)
                    return false;
            }
            if (!lifetime.CommitInitialPublications({batchAllocations, batchSize}, &lifetimeFailure) || !lifetime.RetireBatch({batchAllocations, batchSize}, &lifetimeFailure))
                return false;

            gpu::ResidencyFenceSet epochSafeAfter;
            if (!SubmitRetirementFences(0x4750555343451000ull + epochIndex * 4u, epochSafeAfter))
                return false;
            if (epochIndex < 3)
            {
                if (!lifetime.SealRetirements(epochSafeAfter, &lifetimeFailure))
                    return false;
                lastSealedFences = epochSafeAfter;
            }
            else
            {
                stalledEpochSafeAfter = epochSafeAfter;
                if (lifetime.SealRetirements(epochSafeAfter, &lifetimeFailure) || lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::RetirementEpochsExhausted)
                    return false;
            }
        }

        if (!WaitRetirementFences(lastSealedFences) || lifetime.Collect(&lifetimeFailure) != batchSize * 3 || !lifetime.SealRetirements(stalledEpochSafeAfter, &lifetimeFailure) ||
            !WaitRetirementFences(stalledEpochSafeAfter) || lifetime.Collect(&lifetimeFailure) != batchSize)
            return LifetimeFailure("retirement epoch recovery");

        const rendering::GpuSceneLifetimeStats lifetimeStats = lifetime.GetStats();
        const rendering::GpuSceneTablesStats tableStats = tables.GetStats();
        const rendering::GpuSceneUploadStats uploadStats = uploader.GetStats();
        if (lifetimeStats.allocated != 0 || lifetimeStats.active != 0 || lifetimeStats.retiring != 0 || lifetimeStats.pendingRetirements != 0 || lifetimeStats.sealedRetirements != 0 ||
            lifetimeStats.sealedEpochs != 0 || lifetimeStats.retirements != batchSize * 4 + 7 || lifetimeStats.reclaimed != batchSize * 4 + 7 || lifetimeStats.epochsSealed != 9 ||
            lifetimeStats.epochCapacityStalls != 1 || lifetimeStats.epochFencePolls < 5 || uploadStats.batchesSubmitted != 5 || uploadStats.requestedUpdates != 7 || uploadStats.uniqueUpdates != 6 ||
            uploadStats.copiesRecorded != 6 || uploadStats.bytesSuperseded != sizeof(rendering::GpuInstance) || tableStats.materializedPages < 4 || tableStats.allocatedBytes == 0)
        {
            std::fprintf(stderr,
                         "[rhiNvrhiTests] lifetime stats: alloc=%u active=%u retiring=%u pending=%u sealed=%u epochs=%u retire=%llu reclaim=%llu "
                         "epochsSealed=%llu stalls=%llu polls=%llu; upload batches=%llu requested=%llu unique=%llu copies=%llu superseded=%llu; pages=%u bytes=%llu\n",
                         lifetimeStats.allocated, lifetimeStats.active, lifetimeStats.retiring, lifetimeStats.pendingRetirements, lifetimeStats.sealedRetirements, lifetimeStats.sealedEpochs,
                         static_cast<unsigned long long>(lifetimeStats.retirements), static_cast<unsigned long long>(lifetimeStats.reclaimed), static_cast<unsigned long long>(lifetimeStats.epochsSealed),
                         static_cast<unsigned long long>(lifetimeStats.epochCapacityStalls), static_cast<unsigned long long>(lifetimeStats.epochFencePolls),
                         static_cast<unsigned long long>(uploadStats.batchesSubmitted), static_cast<unsigned long long>(uploadStats.requestedUpdates), static_cast<unsigned long long>(uploadStats.uniqueUpdates),
                         static_cast<unsigned long long>(uploadStats.copiesRecorded), static_cast<unsigned long long>(uploadStats.bytesSuperseded), tableStats.materializedPages,
                         static_cast<unsigned long long>(tableStats.allocatedBytes));
            return LifetimeFailure("final statistics and shutdown");
        }
        if (!uploader.Shutdown(&uploadFailure) || !lifetime.Shutdown(&lifetimeFailure) || !tables.Shutdown({}, &tableFailure))
            return LifetimeFailure("final shutdown");

        descriptors.Reset();
        return gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
    }
    struct GpuSceneRuntimeExecution
    {
        rendering::GpuSceneRuntime* runtime = nullptr;
    };

    rendering::RenderFrameExecutionStatus ExecuteGpuSceneRuntimeTick(rendering::RenderFrameTickContext& context, void* const userData) noexcept
    {
        auto* const execution = static_cast<GpuSceneRuntimeExecution*>(userData);
        rendering::GpuSceneRuntimeFailure failure;
        return execution != nullptr && execution->runtime != nullptr && execution->runtime->Publish(context, &failure)
                   ? rendering::RenderFrameExecutionStatus::Success()
                   : rendering::RenderFrameExecutionStatus::Failure(failure.message != nullptr ? failure.message : "GPU Scene runtime test publication failed");
    }

    rendering::RenderFrameExecutionStatus ExecuteGpuSceneRuntimeFrame(rendering::RenderFrameContext&, void*) noexcept
    {
        return rendering::RenderFrameExecutionStatus::Success();
    }

    [[nodiscard]] bool TestGpuSceneRuntime(gpu::Failure& failure) noexcept
    {
        const auto RuntimeFailure = [](const char* const stage) noexcept
        {
            std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene runtime failed at %s\n", stage);
            return false;
        };
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        if (!descriptors)
            return false;

        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        if (!scenes.Initialize({}, &sceneFailure))
            return false;

        rendering::GpuSceneRuntimeConfig config;
        config.tables.resourceDescriptors = descriptors;
        config.tables.maximumPagesPerTable = 2;
        config.upload.bytesPerSegment = 2u * 1024u * 1024u;
        config.upload.maximumUpdatesPerBatch = 64;
        config.upload.maximumCopiesPerBatch = 128;
        config.definitions.maximumGeometries = 16;
        config.definitions.maximumMaterials = 16;
        config.definitions.maximumRenderables = 16;
        config.definitions.maximumDefinitionsPerBatch = 16;
        config.definitions.maximumAllocationsPerBatch = 64;

        rendering::GpuSceneRuntime runtime;
        rendering::GpuSceneRuntimeFailure runtimeFailure;
        if (!runtime.Initialize(scenes, config, &runtimeFailure) || !runtime.IsInitialized() || !runtime.GetTables().IsInitialized() || !runtime.GetLifetime().IsInitialized() ||
            !runtime.GetUploader().IsInitialized() || !runtime.GetDefinitions().IsInitialized() || !runtime.GetScenePublisher().IsInitialized())
            return RuntimeFailure("initialization");

        rendering::RenderCameraStorage cameras;
        rendering::RenderCameraFailure cameraFailure;
        if (!cameras.Initialize(scenes, {}, &cameraFailure))
            return false;

        GpuSceneRuntimeExecution execution{&runtime};
        rendering::RenderCommandSystem commands;
        rendering::RenderCommandSystemConfig commandConfig;
        commandConfig.executeFrameTick = &ExecuteGpuSceneRuntimeTick;
        commandConfig.executeFrame = &ExecuteGpuSceneRuntimeFrame;
        commandConfig.userData = &execution;
        rendering::RenderCommandFailure commandFailure;
        if (!commands.Initialize(scenes, cameras, commandConfig, &commandFailure))
            return false;

        rendering::RenderSceneDesc sceneDesc;
        sceneDesc.name = "GPU Scene runtime ownership";
        sceneDesc.maximumProxies = 8;
        sceneDesc.maximumPendingProxyMutations = 8;
        sceneDesc.maximumViews = 1;
        rendering::RenderSceneHandle scene;
        if (!scenes.CreateScene(sceneDesc, scene, &sceneFailure))
            return false;
        rendering::LightProxyDesc lightDesc;
        lightDesc.proxy.scene = scene;
        lightDesc.proxy.debugName = "GPU Scene runtime light";
        rendering::RenderProxyHandle light;
        rendering::RenderSceneGpuIdentity identity;
        if (!scenes.CreateLightProxy(lightDesc, light, &sceneFailure) || !runtime.GetScenePublisher().GetIdentity(light, identity))
            return false;

        rendering::RenderCommandFrameTickResult tickResult;
        if (!commands.FrameTick(scenes.GetFramePipelineScenes(), 1, tickResult, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) ||
            !runtime.ResolveContributions(&runtimeFailure) || commands.ConsumeExecutionFailure(commandFailure) || runtime.ConsumePublicationFailure(runtimeFailure) ||
            runtime.GetLifetime().GetState(identity.allocation) != rendering::GpuSceneAllocationState::Active)
            return RuntimeFailure("initial publication");
        const rendering::GpuSceneRuntimeStats firstPublication = runtime.GetStats();
        if (firstPublication.publicationTicks != 1 || firstPublication.submittedBatches != 1 || firstPublication.completedPublications != 1 || firstPublication.publishedObjects != 1)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] runtime first stats: ticks=%llu batches=%llu completed=%llu objects=%llu deferred=%llu\n",
                         static_cast<unsigned long long>(firstPublication.publicationTicks), static_cast<unsigned long long>(firstPublication.submittedBatches),
                         static_cast<unsigned long long>(firstPublication.completedPublications), static_cast<unsigned long long>(firstPublication.publishedObjects),
                         static_cast<unsigned long long>(firstPublication.deferredScenes));
            return RuntimeFailure("initial publication stats");
        }

        if (!commands.FrameTick(scenes.GetFramePipelineScenes(), 2, tickResult, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) || !runtime.ResolveContributions(&runtimeFailure) ||
            commands.ConsumeExecutionFailure(commandFailure) || runtime.ConsumePublicationFailure(runtimeFailure))
            return RuntimeFailure("no-op publication");
        const rendering::GpuSceneRuntimeStats noOpPublication = runtime.GetStats();
        if (noOpPublication.publicationTicks != 2 || noOpPublication.submittedBatches != 1 || noOpPublication.completedPublications != 2 || noOpPublication.publishedObjects != 1)
        {
            std::fprintf(stderr, "[rhiNvrhiTests] runtime no-op stats: ticks=%llu batches=%llu completed=%llu objects=%llu deferred=%llu\n",
                         static_cast<unsigned long long>(noOpPublication.publicationTicks), static_cast<unsigned long long>(noOpPublication.submittedBatches),
                         static_cast<unsigned long long>(noOpPublication.completedPublications), static_cast<unsigned long long>(noOpPublication.publishedObjects),
                         static_cast<unsigned long long>(noOpPublication.deferredScenes));
            return RuntimeFailure("no-op publication stats");
        }

        const auto submitRetirementFences = [&failure](const vanguard::u64 name, gpu::ResidencyFenceSet& submitted) noexcept
        {
            submitted = {};
            const gpu::CommandListType types[] = {gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
            for (vanguard::u32 index = 0; index < 3; ++index)
            {
                gpu::CommandListRef list = gpu::CreateCommandList(types[index], name + index, &failure);
                if (!list || !gpu::BindCommandList(list, &failure))
                    return false;
                gpu::UnbindCommandList();
                const gpu::CommandListRef lists[] = {list};
                gpu::GpuFence completion;
                if (!gpu::CloseAndSubmitCommandLists("GPU Scene runtime retirement", {lists, 1}, gpu::CommandListSyncType::None, completion, &failure))
                    return false;
                submitted.Include(completion);
            }
            return true;
        };
        const auto waitRetirementFences = [&failure](const gpu::ResidencyFenceSet& fences) noexcept
        {
            return gpu::WaitForGpuFence({gpu::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) && gpu::WaitForGpuFence({gpu::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
                   gpu::WaitForGpuFence({gpu::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
        };
        if (!scenes.DestroyProxy(light, &sceneFailure) || !commands.FrameTick(scenes.GetFramePipelineScenes(), 3, tickResult, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) ||
            commands.ConsumeExecutionFailure(commandFailure) || !runtime.ResolveContributions(&runtimeFailure) || runtime.ConsumePublicationFailure(runtimeFailure) ||
            runtime.GetLifetime().GetState(identity.allocation) != rendering::GpuSceneAllocationState::Retiring)
            return RuntimeFailure("retirement publication");
        gpu::ResidencyFenceSet retirementFences;
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        if (!submitRetirementFences(0x47505552554e5449ull, retirementFences) || !runtime.GetLifetime().SealRetirements(retirementFences, &lifetimeFailure) || !waitRetirementFences(retirementFences) ||
            runtime.GetLifetime().Collect(&lifetimeFailure) != 1 || !commands.Shutdown(&commandFailure) || runtime.Shutdown({}, &runtimeFailure) ||
            runtimeFailure.code != rendering::GpuSceneRuntimeFailureCode::LiveScenesRemain || !scenes.DestroyScene(scene, &sceneFailure) || !runtime.Shutdown({}, &runtimeFailure) || runtime.IsInitialized() ||
            !cameras.Shutdown(&cameraFailure) || !scenes.Shutdown(&sceneFailure))
            return RuntimeFailure("retirement collection and shutdown");
        return true;
    }

    [[nodiscard]] bool TestGpuSceneDefinitions(gpu::Failure& failure) noexcept
    {
        const gpu::ShaderStageMask visibility = gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel) | gpu::ShaderStageBit(gpu::ShaderStage::Compute);
        gpu::DescriptorDomain descriptors(gpu::AdoptReference, gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 128, 0, visibility}, &failure));
        if (!descriptors)
            return false;

        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        if (!scenes.Initialize({}, &sceneFailure))
            return false;
        rendering::GpuSceneRuntime runtime;
        rendering::GpuSceneRuntimeFailure runtimeFailure;
        rendering::GpuSceneDefinitionFailure definitionFailure;
        rendering::GpuSceneRuntimeConfig runtimeConfig;
        runtimeConfig.tables.resourceDescriptors = descriptors;
        runtimeConfig.tables.maximumPagesPerTable = 2;
        runtimeConfig.upload.bytesPerSegment = 2u * 1024u * 1024u;
        runtimeConfig.upload.maximumUpdatesPerBatch = 64;
        runtimeConfig.upload.maximumCopiesPerBatch = 128;
        runtimeConfig.definitions.maximumGeometries = 16;
        runtimeConfig.definitions.maximumMaterials = 16;
        runtimeConfig.definitions.maximumRenderables = 16;
        runtimeConfig.definitions.maximumDefinitionsPerBatch = 16;
        runtimeConfig.definitions.maximumAllocationsPerBatch = 64;
        if (!runtime.Initialize(scenes, runtimeConfig, &runtimeFailure))
            return false;
        rendering::GpuSceneTables& tables = runtime.GetTables();
        rendering::GpuSceneLifetime& lifetime = runtime.GetLifetime();
        rendering::GpuSceneDefinitions& definitions = runtime.GetDefinitions();
        rendering::GpuSceneLifetimeFailure lifetimeFailure;

        rendering::RenderCameraStorage cameras;
        rendering::RenderCameraFailure cameraFailure;
        if (!cameras.Initialize(scenes, {}, &cameraFailure))
            return false;
        GpuSceneRuntimeExecution execution{&runtime};
        rendering::RenderCommandSystem commands;
        rendering::RenderCommandSystemConfig commandConfig;
        commandConfig.executeFrameTick = &ExecuteGpuSceneRuntimeTick;
        commandConfig.executeFrame = &ExecuteGpuSceneRuntimeFrame;
        commandConfig.userData = &execution;
        rendering::RenderCommandFailure commandFailure;
        if (!commands.Initialize(scenes, cameras, commandConfig, &commandFailure))
            return false;

        rendering::GpuVertexStream vertexStream;
        vertexStream.binding = 0;
        vertexStream.stride = 16;
        vertexStream.formatLayout = 9;
        rendering::GpuPositionDecode positionDecode;
        positionDecode.scale[0] = 10.0f;
        positionDecode.scale[1] = 20.0f;
        positionDecode.scale[2] = 30.0f;
        rendering::GpuGeometryDefinition geometry;
        geometry.key = {{1, 2, 3, 4}};
        geometry.geometry.vertexArenaSet = 3;
        geometry.geometry.vertexArenaGeneration = 1;
        geometry.geometry.indexArena = 5;
        geometry.geometry.indexArenaGeneration = 1;
        geometry.geometry.firstIndex = 256;
        geometry.geometry.indexCount = 36;
        geometry.geometry.vertexCount = 24;
        geometry.geometry.indexFormat = rendering::GpuIndexFormat::UInt16;
        geometry.geometry.flags = static_cast<rendering::GpuGeometryFlags>(static_cast<vanguard::u32>(rendering::GpuGeometryFlags::Resident) | static_cast<vanguard::u32>(rendering::GpuGeometryFlags::Indexed));
        geometry.vertexStreams = {&vertexStream, 1};
        geometry.positionDecode = &positionDecode;
        const rendering::GpuGeometryDefinition geometryBatch[] = {geometry, geometry};
        rendering::GpuGeometryHandle geometryHandles[2];
        rendering::GpuSceneDefinitionPublication geometryPublication;
        if (!definitions.AcquireGeometries({geometryBatch, 2}, {geometryHandles, 2}, geometryPublication, &definitionFailure) || geometryHandles[0] != geometryHandles[1] ||
            geometryPublication.createdDefinitions != 1 || geometryPublication.reusedDefinitions != 1 || !gpu::WaitForGpuFence(geometryPublication.completion, 5'000'000'000ull, &failure))
            return false;

        rendering::GpuMaterialResource materialResource;
        materialResource.resource = 17;
        materialResource.samplerDescriptor = 3;
        materialResource.type = rendering::GpuMaterialResourceType::Sampler;
        rendering::GpuMaterialDefinition material;
        material.key = {{5, 6, 7, 8}};
        material.material.materialLayout = 11;
        material.material.flags = rendering::GpuMaterialFlags::Resident;
        material.resources = {&materialResource, 1};
        const vanguard::u8 parameterBytes[]{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
        material.parameterBytes = {parameterBytes, sizeof(parameterBytes)};
        const rendering::GpuMaterialDefinition materialBatch[] = {material, material};
        rendering::GpuMaterialDefinitionBatch preparedMaterials;
        rendering::GpuMaterialHandle materialHandles[2];
        rendering::GpuSceneDefinitionPublication materialPublication;
        rendering::RenderCommandFrameTickResult tickResult;
        if (!definitions.PrepareMaterials({materialBatch, 2}, preparedMaterials, &definitionFailure) ||
            definitions.GetMaterialBatchState(preparedMaterials) != rendering::GpuMaterialDefinitionBatchState::Prepared || !definitions.StageMaterials(runtime, preparedMaterials, &definitionFailure) ||
            !commands.FrameTick(scenes.GetFramePipelineScenes(), 1, tickResult, &commandFailure) || !commands.FlushPreviousFrameProcessing(&commandFailure) || commands.ConsumeExecutionFailure(commandFailure) ||
            !runtime.ResolveContributions(&runtimeFailure) || runtime.ConsumePublicationFailure(runtimeFailure) ||
            definitions.GetMaterialBatchState(preparedMaterials) != rendering::GpuMaterialDefinitionBatchState::Accepted ||
            !definitions.ConsumeMaterials(preparedMaterials, {materialHandles, 2}, materialPublication, &definitionFailure) || materialHandles[0] != materialHandles[1] ||
            materialPublication.createdDefinitions != 1 || materialPublication.reusedDefinitions != 1 || !gpu::WaitForGpuFence(materialPublication.completion, 5'000'000'000ull, &failure))
            return false;

        rendering::GpuPhaseParticipation phase;
        phase.phase = 2;
        phase.flags = rendering::GpuPhaseParticipationFlags::DepthWrite;
        rendering::GpuPrimitiveDefinition primitive;
        primitive.material = materialHandles[0];
        primitive.phaseParticipationCount = 1;
        primitive.sourceSubmesh = 13;
        primitive.flags = rendering::GpuPrimitiveFlags::Indexed;
        rendering::GpuLod lod;
        lod.primitiveCount = 1;
        lod.minimumScreenCoverage = 0.25f;
        rendering::GpuRenderableDefinition renderable;
        renderable.key = {{9, 10, 11, 12}};
        renderable.lods = {&lod, 1};
        renderable.primitives = {&primitive, 1};
        renderable.phaseParticipations = {&phase, 1};
        const rendering::GpuRenderableDefinition renderableBatch[] = {renderable, renderable};
        rendering::GpuRenderableHandle renderableHandles[2];
        rendering::GpuSceneDefinitionPublication renderablePublication;
        if (!definitions.AcquireRenderables({renderableBatch, 2}, {renderableHandles, 2}, renderablePublication, &definitionFailure) || renderableHandles[0] != renderableHandles[1] ||
            renderablePublication.createdDefinitions != 1 || renderablePublication.reusedDefinitions != 1 || !gpu::WaitForGpuFence(renderablePublication.completion, 5'000'000'000ull, &failure))
            return false;

        rendering::GpuRenderable uploadedRenderable;
        rendering::GpuLod uploadedLod;
        rendering::GpuPrimitive uploadedPrimitive;
        rendering::GpuGeometryRange uploadedGeometry;
        rendering::GpuMaterial uploadedMaterial;
        rendering::GpuMaterialParameterWord uploadedParameter0;
        rendering::GpuMaterialParameterWord uploadedParameter1;
        if (!ReadGpuSceneElement(tables, renderableHandles[0].index, uploadedRenderable, failure, 0x4750555343444500ull) ||
            !ReadGpuSceneElement(tables, uploadedRenderable.firstLod, uploadedLod, failure, 0x4750555343444501ull) ||
            !ReadGpuSceneElement(tables, uploadedLod.firstPrimitive, uploadedPrimitive, failure, 0x4750555343444502ull) ||
            !ReadGpuSceneElement(tables, geometryHandles[0].index, uploadedGeometry, failure, 0x4750555343444503ull) ||
            !ReadGpuSceneElement(tables, materialHandles[0].index, uploadedMaterial, failure, 0x4750555343444504ull) ||
            !ReadGpuSceneElement(tables, uploadedMaterial.parameterByteOffset / sizeof(rendering::GpuMaterialParameterWord), uploadedParameter0, failure, 0x4750555343444505ull) ||
            !ReadGpuSceneElement(tables, uploadedMaterial.parameterByteOffset / sizeof(rendering::GpuMaterialParameterWord) + 1u, uploadedParameter1, failure, 0x4750555343444506ull) ||
            uploadedRenderable.lodCount != 1 || uploadedRenderable.phaseMaskLow != (1u << 2u) || uploadedPrimitive.material != materialHandles[0].index || uploadedPrimitive.sourceSubmesh != 13 ||
            uploadedGeometry.firstVertexStream == rendering::InvalidGpuSceneIndex || uploadedGeometry.positionDecode == rendering::InvalidGpuSceneIndex || uploadedMaterial.resourceCount != 1 ||
            uploadedMaterial.parameterByteSize != sizeof(parameterBytes) || (uploadedMaterial.parameterByteOffset & 3u) != 0 || uploadedParameter0.value != 0x44332211u ||
            uploadedParameter1.value != 0x00776655u)
            return false;

        const rendering::GpuSceneDefinitionsStats activeStats = definitions.GetStats();
        if (activeStats.geometries != 1 || activeStats.materials != 1 || activeStats.renderables != 1 || activeStats.references != 7 || activeStats.acquisitions != 6 || activeStats.reuses != 3 ||
            !definitions.Release(geometryHandles[0], &definitionFailure) || !definitions.Release(geometryHandles[1], &definitionFailure) || !definitions.Release(materialHandles[0], &definitionFailure) ||
            !definitions.Release(materialHandles[1], &definitionFailure) || !definitions.Release(renderableHandles[0], &definitionFailure) || !definitions.Release(renderableHandles[1], &definitionFailure) ||
            definitions.IsValid(geometryHandles[0]) || definitions.IsValid(materialHandles[0]) || definitions.IsValid(renderableHandles[0]))
            return false;

        gpu::ResidencyFenceSet safeAfter;
        const gpu::CommandListType queueTypes[] = {gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
        for (vanguard::u32 queue = 0; queue < 3; ++queue)
        {
            gpu::CommandListRef commandList = gpu::CreateCommandList(queueTypes[queue], 0x4750555343445200ull + queue, &failure);
            if (!commandList || !gpu::BindCommandList(commandList, &failure))
                return false;
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[] = {commandList};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("GPU Scene definition retirement", {submissions, 1}, gpu::CommandListSyncType::None, completion, &failure))
                return false;
            safeAfter.Include(completion);
        }
        if (!lifetime.SealRetirements(safeAfter, &lifetimeFailure) || !gpu::WaitForGpuFence({gpu::QueueType::Graphics, safeAfter.graphics}, 5'000'000'000ull, &failure) ||
            !gpu::WaitForGpuFence({gpu::QueueType::Compute, safeAfter.compute}, 5'000'000'000ull, &failure) || !gpu::WaitForGpuFence({gpu::QueueType::Copy, safeAfter.copy}, 5'000'000'000ull, &failure) ||
            lifetime.Collect(&lifetimeFailure) != 10)
            return false;

        const rendering::GpuSceneDefinitionsStats releasedStats = definitions.GetStats();
        if (releasedStats.geometries != 0 || releasedStats.materials != 0 || releasedStats.renderables != 0 || releasedStats.references != 0 || releasedStats.releases != 6 || releasedStats.retirements != 3 ||
            !commands.Shutdown(&commandFailure) || !runtime.Shutdown({}, &runtimeFailure) || !cameras.Shutdown(&cameraFailure) || !scenes.Shutdown(&sceneFailure))
            return false;
        descriptors.Reset();
        return gpu::WaitIdle(&failure) && gpu::RetireResources(&failure);
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
    if (!TestNvrhiTrackerRecordingEpoch() || !TestResourceLifetimeManager() || !TestOwnedSubmissionFencePublication() || !TestRetirementBucketPressure() || !TestConcurrentRetirementPublication())
    {
        std::printf("[rhiNvrhiTests] resource lifetime tests failed\n");
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestCommandListRetentionPolicy())
    {
        std::printf("[rhiNvrhiTests] command-list retention policy test failed\n");
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
        std::printf("[rhiNvrhiTests] initialization failed: %s (0x%llx)\n", failure.message, static_cast<unsigned long long>(failure.backendCode));
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }

    const vanguard::rhi::Capabilities& capabilities = vanguard::rhi::GetCapabilities();
    if (capabilities.backend != vanguard::rhi::BackendKind::D3D12 || capabilities.adapterName[0] == '\0' || vanguard::rhi::TestDeviceState() != vanguard::rhi::DeviceState::Operational ||
        !capabilities.occlusionQueries || !capabilities.pipelineStatisticsQueries || !capabilities.timestampQueries || !capabilities.timestampCalibration || !capabilities.gpuMarkers ||
        !capabilities.memoryBudgetQueries || !capabilities.explicitResidency || capabilities.rayTracing || capabilities.rayTracingPipeline || capabilities.meshShaders || capabilities.variableRateShading)
    {
        std::printf("[rhiNvrhiTests] invalid device capability contract\n");
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestCommandListLeaseLifecycle(failure) || !TestNativeCommandListSubmissionBatchSizes(failure) || !TestManualTextureUploadTransition(failure) ||
        !TestLazyBarrierCommitFence(failure) || !TestCompiledNativeBarrierBatch(failure) ||
        !TestBlockCompressedWholeMipCopy(failure) ||
        !TestFlushRetiredResourcesWaitsSubmissionFence(failure) || !TestDescriptorDomainReclaim(failure))
    {
        std::printf("[rhiNvrhiTests] NVRHI state/lifetime regression test failed: %s\n", failure.message);
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    const char* gpuSceneFailureStage = nullptr;
    if (!TestGpuSceneLifetime(failure))
        gpuSceneFailureStage = "lifetime";
    else if (!TestGpuSceneRuntime(failure))
        gpuSceneFailureStage = "runtime";
    else if (!TestGpuSceneDefinitions(failure))
        gpuSceneFailureStage = "definitions";
    if (gpuSceneFailureStage != nullptr)
    {
        std::fprintf(stderr, "[rhiNvrhiTests] GPU Scene %s integration test failed: %s\n", gpuSceneFailureStage, failure.message);
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    const HWND presentationWindow =
        CreateWindowExW(0, L"STATIC", L"Vanguard RHI SwapChain Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (presentationWindow == nullptr || !TestSwapChain(presentationWindow, failure))
    {
        std::fprintf(stderr, "[rhiNvrhiTests] swap-chain test failed: %s (0x%llx)\n", failure.message, static_cast<unsigned long long>(failure.backendCode));
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        if (presentationWindow != nullptr)
            DestroyWindow(presentationWindow);
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestPresentationService())
    {
        std::fprintf(stderr, "[rhiNvrhiTests] presentation service integration test failed\n");
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        DestroyWindow(presentationWindow);
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestNativeResourcesAndSubmission(failure))
    {
        std::printf("[rhiNvrhiTests] native resource/submission test failed: %s\n", failure.message);
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        DestroyWindow(presentationWindow);
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    if (!TestGpuCounters(failure))
    {
        std::printf("[rhiNvrhiTests] GPU counter test failed: %s\n", failure.message);
        static_cast<void>(vanguard::rhi::WaitIdle());
        static_cast<void>(vanguard::rhi::RetireResources());
        static_cast<void>(vanguard::rhi::Shutdown());
        DestroyWindow(presentationWindow);
        static_cast<void>(vanguard::jobs::Shutdown());
        return 1;
    }
    std::printf("[rhiNvrhiTests] D3D12 backend lifecycle passed on %s\n", capabilities.adapterName);
    if (!vanguard::rhi::RetireResources(&failure) || !vanguard::rhi::WaitIdle(&failure) || !vanguard::rhi::Shutdown(&failure))
    {
        const vanguard::rhi::ResourceLifetimeStats stats = vanguard::rhi::GetResourceLifetimeStats();
        std::printf("[rhiNvrhiTests] clean shutdown failed: %s (live=%u, pending=%u, references=%llu)\n", failure.message, stats.liveResources, stats.pendingRetirements,
                    static_cast<unsigned long long>(stats.totalReferences));
        static_cast<void>(vanguard::jobs::Shutdown());
        DestroyWindow(presentationWindow);
        return 1;
    }

    if (!vanguard::rhi::Initialize(backend, params, &failure))
    {
        std::printf("[rhiNvrhiTests] terminal-abandon reinitialization failed: %s\n", failure.message);
        static_cast<void>(vanguard::jobs::Shutdown());
        DestroyWindow(presentationWindow);
        return 1;
    }
    const vanguard::rhi::QueryPoolRef abandonedQueryPool = vanguard::rhi::CreateQueryPool({vanguard::rhi::QueryType::Timestamp, 1}, &failure);
    if (!abandonedQueryPool || !vanguard::rhi::AbandonDevice(&failure) || vanguard::rhi::IsInitialized())
    {
        std::printf("[rhiNvrhiTests] terminal abandonment with a live query pool failed: %s\n", failure.message);
        static_cast<void>(vanguard::jobs::Shutdown());
        DestroyWindow(presentationWindow);
        return 1;
    }
    DestroyWindow(presentationWindow);

    if (!vanguard::jobs::Shutdown())
    {
        std::printf("[rhiNvrhiTests] jobs shutdown failed\n");
        return 1;
    }
    std::printf("[rhiNvrhiTests] deferred resource lifetime tests passed\n");
    return 0;
}
