#include <vanguard/editor/editor_ui_renderer.hpp>
#include <vanguard/editor/editor_ui_stall_probe.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/editor/editor_ui_render_data.hpp>
#include <vanguard/editor/editor_ui_shader.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/rendering/render_graph_nodes.hpp>
#include <vanguard/rendering/render_node_graph_factory.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace vanguard::editor::detail
{
    namespace
    {
        using namespace rendering;

        bool Fail(EditorUiFailure* failure, const char* message) noexcept
        {
            if (failure != nullptr) { *failure = {}; failure->code = EditorUiFailureCode::PlatformFailure; failure->message = message; }
            return false;
        }

        struct TextureBinding
        {
            ImportedResourceId imported;
            rhi::DescriptorHandle texture;
            rhi::DescriptorHandle sampler;
            bool linearInput = false;
            char name[32]{};
        };

        struct UiFrame
        {
            concurrency::Atomic<u32> references{1};
            EditorUiFrameData draw;
            EditorUiTextureFrameData uploads;
            rhi::Buffer vertices;
            rhi::Buffer indices;
            rhi::Pipeline pipeline;
            ImportedResourceId vertexImport;
            ImportedResourceId indexImport;
            containers::DynamicArray<TextureBinding> textures{memory::pools::Editor::GetInstance()};
        };

        void RetainFrame(void* data) noexcept { static_cast<void>(static_cast<UiFrame*>(data)->references.Increment()); }
        void ReleaseFrame(void* data) noexcept
        {
            auto* frame = static_cast<UiFrame*>(data);
            if (frame->references.Decrement() != 0) return;
            frame->~UiFrame();
            memory::MemoryBlock block{frame, sizeof(UiFrame), memory::PoolId::Editor};
            memory::Free(block);
        }
        UiFrame* NewFrame() noexcept
        {
            auto block = memory::Allocate(memory::PoolId::Editor, sizeof(UiFrame), alignof(UiFrame));
            return block ? new (block.address) UiFrame() : nullptr;
        }
        UiFrame& Frame(const RenderNodeImplContext& context) noexcept { return *static_cast<UiFrame*>(context.GetFrameInfo().GetPayload().data); }

        bool RegisterImports(RenderFlowResourceAllocator& allocator, const RenderFrameInfo& info, RenderFlowResourceFailure* failure) noexcept
        {
            auto& frame = *static_cast<UiFrame*>(info.GetPayload().data);
            const auto* draws = static_cast<const EditorUiRenderData*>(frame.draw.GetPayload().data);
            const auto* uploads = static_cast<const EditorUiTextureUpdateData*>(frame.uploads.GetPayload().data);
            if (draws != nullptr && !draws->vertices.Empty() && !draws->indices.Empty())
            {
                const auto importBuffer = [&](rhi::BufferRef buffer, ImportedResourceId& imported) noexcept
                {
                    RetainedBufferImportDesc desc;
                    desc.buffer = buffer; desc.token = {rhi::ResourceRef(buffer).value};
                    if (!rhi::GetBufferDesc(buffer, desc.expected)) return false;
                    desc.initialState = desc.terminalState = desc.expected.initialState;
                    return allocator.RegisterImport(desc, imported, failure);
                };
                if (!importBuffer(frame.vertices.GetRef(), frame.vertexImport) || !importBuffer(frame.indices.GetRef(), frame.indexImport)) return false;
            }
            for (u32 index = 0; index < frame.textures.Size(); ++index)
            {
                RetainedTextureImportDesc desc;
                desc.texture = draws != nullptr ? draws->textures[index].texture.GetRef() : uploads->uploads[index].texture.GetRef();
                desc.token = {rhi::ResourceRef(desc.texture).value};
                if (!rhi::GetTextureDesc(desc.texture, desc.expected)) return false;
                desc.initialState = desc.terminalState = desc.expected.initialState;
                if (!allocator.RegisterImport(desc, frame.textures[index].imported, failure)) return false;
            }
            return true;
        }

        class UiGeometryUpload final : public RenderNodeImpl
        {
        public:
            const char* GetName() const noexcept override { return "EditorUiGeometry"; }
            RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
            bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept override
            {
                const auto& frame = Frame(context);
                if (!frame.vertexImport.IsValid()) return true;
                const auto vertices = context.RTSharedInjectBuffer("UiVertices", frame.vertexImport);
                const auto indices = context.RTSharedInjectBuffer("UiIndices", frame.indexImport);
                BufferUseDesc use;
                use.requiredState = rhi::ResourceState::CopyDestination; use.access = LogicalAccessIntent::Write; use.content = ResourceContentIntent::Discard;
                context.RTUseBegin(vertices, use); context.RTUseEnd(vertices);
                context.RTUseBegin(indices, use); context.RTUseEnd(indices);
                return true;
            }
            void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
            {
                const auto& frame = Frame(context);
                if (!frame.vertexImport.IsValid()) return;
                const auto& data = *static_cast<const EditorUiRenderData*>(frame.draw.GetPayload().data);
                if (!rhi::WriteBuffer(context.RTBuffer(context.RTSharedNameTag("UiVertices")).GetBuffer(), data.vertices.Data(), u64(data.vertices.Size()) * sizeof(EditorUiVertex)) ||
                    !rhi::WriteBuffer(context.RTBuffer(context.RTSharedNameTag("UiIndices")).GetBuffer(), data.indices.Data(), u64(data.indices.Size()) * sizeof(u16)))
                    VG_FATAL("editor UI geometry upload failed");
            }
        };

        class UiNode final : public RenderNodeImpl
        {
        public:
            explicit UiNode(bool upload) noexcept : m_upload(upload) {}
            const char* GetName() const noexcept override { return m_upload ? "EditorUiTextures" : "EditorUiDraw"; }
            RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
            bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept override
            {
                auto& frame = Frame(context);
                for (const auto& binding : frame.textures)
                {
                    const auto tag = context.RTInject(binding.name, binding.imported);
                    TextureUseDesc use;
                    use.requiredState = m_upload ? rhi::ResourceState::CopyDestination : rhi::ResourceState::ShaderResourceGraphics;
                    use.access = m_upload ? LogicalAccessIntent::Write : LogicalAccessIntent::Read;
                    use.content = m_upload ? ResourceContentIntent::Discard : ResourceContentIntent::Preserve;
                    context.RTUseBegin(tag, use);
                    context.RTUseEnd(tag);
                }
                if (!m_upload)
                {
                    if (frame.vertexImport.IsValid())
                    {
                        BufferUseDesc read;
                        read.requiredState = rhi::ResourceState::VertexBuffer;
                        const auto vertices = context.RTSharedNameTag("UiVertices");
                        context.RTUseBegin(vertices, read); context.RTUseEnd(vertices);
                        read.requiredState = rhi::ResourceState::IndexBuffer;
                        const auto indices = context.RTSharedNameTag("UiIndices");
                        context.RTUseBegin(indices, read); context.RTUseEnd(indices);
                    }
                    context.RTImportFrameOutput();
                    TextureUseDesc use;
                    use.requiredState = rhi::ResourceState::RenderTarget;
                    use.access = LogicalAccessIntent::Write;
                    use.content = ResourceContentIntent::Discard;
                    auto output = context.RTSharedNameTag("FrameOutput");
                    context.RTUseBegin(output, use);
                    context.RTUseEnd(output);
                }
                return true;
            }
            void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
            {
                const bool recorded = m_upload ? Upload(context) : Draw(context);
                if (!recorded) VG_FATAL("editor UI recording failed");
            }
        private:
            static bool Upload(const RenderNodeImplContext& context) noexcept
            {
                const auto& frame = Frame(context);
                const auto& data = *static_cast<const EditorUiTextureUpdateData*>(frame.uploads.GetPayload().data);
                for (u32 index = 0; index < data.uploads.Size(); ++index)
                {
                    const auto& upload = data.uploads[index];
                    rhi::TextureSubresourceData pixels;
                    pixels.data = data.pixels.TypedData() + upload.dataOffset;
                    pixels.size = upload.dataSize;
                    pixels.rowPitch = upload.rowPitch;
                    if (!rhi::WriteTexture(context.RTTexture(frame.textures[index].name).GetTexture(), pixels)) return false;
                }
                return true;
            }
            static bool Draw(const RenderNodeImplContext& context) noexcept
            {
                const auto& frame = Frame(context);
                const auto& data = *static_cast<const EditorUiRenderData*>(frame.draw.GetPayload().data);
                const auto output = context.RTTexture(context.RTSharedNameTag("FrameOutput")).GetTexture();
                rhi::TextureDesc target;
                if (!rhi::GetTextureDesc(output, target) || !rhi::ClearColorTarget(output, {0.06f, 0.06f, 0.06f, 1.0f})) return false;
                if (data.vertices.Empty() || data.indices.Empty()) return true;
                const rhi::VertexBufferBinding vertex{context.RTBuffer(context.RTSharedNameTag("UiVertices")).GetBuffer(), 0, 0};
                const rhi::RenderTargetSetup targets{{{output, rhi::Format::Unknown, 0, 0, false}}, 1, {}};
                const auto bindState = [&]() noexcept
                {
                    return rhi::SetPipeline(frame.pipeline.GetRef()) && rhi::SetupRenderTargets(targets) &&
                           rhi::BindVertexBuffers(0, {&vertex, 1}) && rhi::BindIndexBuffer({context.RTBuffer(context.RTSharedNameTag("UiIndices")).GetBuffer(), 0, rhi::IndexFormat::UInt16}) &&
                           rhi::SetViewport({0, 0, f32(target.extent.width), f32(target.extent.height), 0, 1});
                };
                if (!bindState()) return false;
                struct Constants { f32 scale[2]; f32 translate[2]; u32 texture; u32 sampler; u32 linear; u32 padding; };
                static_assert(sizeof(Constants) == 32);
                Constants constants{{2.0f / data.displayExtent[0], -2.0f / data.displayExtent[1]}, {}, 0, 0, 0, 0};
                constants.translate[0] = -1.0f - data.displayPosition[0] * constants.scale[0];
                constants.translate[1] = 1.0f - data.displayPosition[1] * constants.scale[1];
                // Use the actual acquired target extent: resize/DPI can change between UI authoring and acquisition.
                const f32 scaleX = f32(target.extent.width) / (data.displayExtent[0] * data.framebufferScale[0]);
                const f32 scaleY = f32(target.extent.height) / (data.displayExtent[1] * data.framebufferScale[1]);
                for (const auto& command : data.commands)
                {
                    if (command.kind == EditorUiRenderCommandKind::ResetState) { if (!bindState()) return false; continue; }
                    const auto clip = [](f32 value, u32 limit) noexcept { return std::clamp(value, 0.0f, f32(limit)); };
                    const i32 left = i32(clip(command.clipMinimum[0] * scaleX, target.extent.width));
                    const i32 top = i32(clip(command.clipMinimum[1] * scaleY, target.extent.height));
                    const i32 right = i32(clip(command.clipMaximum[0] * scaleX, target.extent.width));
                    const i32 bottom = i32(clip(command.clipMaximum[1] * scaleY, target.extent.height));
                    if (left >= right || top >= bottom || command.elementCount == 0) continue;
                    if (command.texture >= frame.textures.Size()) return false;
                    const auto& binding = frame.textures[command.texture];
                    if (!rhi::AddToResidencyWorkingSet(rhi::ResourceRef(context.RTTexture(binding.name).GetTexture()))) return false;
                    constants.texture = binding.texture.GpuIndex();
                    constants.sampler = binding.sampler.GpuIndex();
                    constants.linear = binding.linearInput;
                    if (!rhi::SetScissors({left, top, right - left, bottom - top}) || !rhi::SetPushConstants(&constants, sizeof(constants)) ||
                        !rhi::DrawIndexedPrimitive({command.elementCount, 1, command.indexOffset, i32(command.vertexOffset), 0})) return false;
                }
                return true;
            }
            bool m_upload;
        };

        void BuildGraph(RenderNodeGraph& graph, NodesContainer& nodes, bool upload)
        {
            NodeGraphFactory factory(&graph, &nodes);
            const auto group = NodeGroupId::None;
            RENDER_COMMAND_LIST(group, "EditorUi")
            {
                if (!upload) { ADD_SUBNODE("EditorUiGeometry", UiGeometryUpload); }
                ADD_SUBNODE("EditorUi", UiNode, upload);
            }
            if (!upload) { RENDER_SIMPLE_COMMAND_LIST(group, "EditorUiEnd", RenderNodeEndRender); }
            ADD_NODE(group, "EditorUiSubmit", RenderNodeSynchronize, rhi::CommandListSyncType::None, "Submit_EditorUi");
            if (!upload) { ADD_NODE(group, "EditorUiPresent", RenderNodePresent); }
            factory.LinkGpu();
            factory.LinkCpu();
        }
    }

    struct EditorUiRenderer::Impl
    {
        struct Host
        {
            EditorHostWindowHandle handle;
            rendering::PresentationOutputHandle output;
            rendering::EngineViewportHandle viewport;
            rhi::Buffer vertices;
            rhi::Buffer indices;
            u64 vertexCapacity = 0;
            u64 indexCapacity = 0;
        };
        struct CachedTexture
        {
            EditorTextureHandle identity;
            rhi::DescriptorHandle texture;
            u64 used = 0;
        };
        struct CachedSampler
        {
            rhi::SamplerStateRef identity;
            rhi::DescriptorHandle descriptor;
            u64 used = 0;
        };
        struct Pipeline { rhi::Format format; rhi::Pipeline pipeline; };
        engine::RenderingService* rendering = nullptr;
        rendering::PresentationService presentation;
        rendering::RenderViewportHandle uploadOutput;
        rendering::EngineViewportHandle uploadViewport;
        NodesContainer drawNodes;
        NodesContainer uploadNodes;
        RenderNodeGraph drawGraph;
        RenderNodeGraph uploadGraph;
        rhi::DescriptorDomain resources;
        rhi::DescriptorDomain samplers;
        rhi::Shader vertexShader;
        rhi::Shader pixelShader;
        rhi::VertexLayoutRef vertexLayout;
        rhi::BindingLayout pushLayout;
        Host hosts[MaximumEditorHostWindows];
        CachedTexture textures[MaximumEditorTextures];
        containers::DynamicArray<CachedSampler> samplerCache{memory::pools::Editor::GetInstance()};
        containers::DynamicArray<Pipeline> pipelines{memory::pools::Editor::GetInstance()};
        rhi::DescriptorRetirement retirement;
        u64 frame = 0;

        bool Retire(CachedTexture& texture) noexcept
        {
            if (texture.texture.IsValid() && !rhi::RetireDescriptor(resources.GetRef(), texture.texture, retirement)) return false;
            texture = {};
            return true;
        }
        bool BindTexture(const EditorUiRenderTexture& input, TextureBinding& output) noexcept
        {
            if (!input.identity.IsValid()) return false;
            rhi::TextureDesc desc;
            if (!rhi::GetTextureDesc(input.texture.GetRef(), desc) || desc.dimension != rhi::TextureDimension::Texture2D || desc.sampleCount != 1 || desc.arraySize != 1 || desc.virtualResource)
                return false;
            const bool srgb = desc.format == rhi::Format::R8G8B8A8UNormSrgb || desc.format == rhi::Format::B8G8R8A8UNormSrgb ||
                              desc.format == rhi::Format::BC1UNormSrgb || desc.format == rhi::Format::BC2UNormSrgb || desc.format == rhi::Format::BC3UNormSrgb || desc.format == rhi::Format::BC7UNormSrgb;
            output.linearInput = srgb || input.colorSpace != EditorTextureColorSpace::DisplaySrgb;
            auto& cached = textures[input.identity.index];
            if (cached.identity != input.identity)
            {
                if (cached.identity.IsValid() && cached.used == frame) return false;
                if (!Retire(cached)) return false;
                cached.texture = rhi::AllocateDescriptor(resources.GetRef());
                if (!cached.texture.IsValid() || !rhi::WriteDescriptor(resources.GetRef(), cached.texture, input.texture.GetRef(), rhi::BindingType::TextureShaderResource)) return false;
                cached.identity = input.identity;
            }
            cached.used = frame;
            output.texture = cached.texture;
            CachedSampler* available = nullptr;
            for (auto& sampler : samplerCache)
            {
                if (sampler.identity == input.sampler.GetRef())
                {
                    sampler.used = frame; output.sampler = sampler.descriptor; return true;
                }
                if (!sampler.identity.IsValid()) available = &sampler;
            }
            if (available == nullptr) available = &samplerCache.EmplaceBack();
            available->descriptor = rhi::AllocateDescriptor(samplers.GetRef());
            if (!available->descriptor.IsValid() || !rhi::WriteDescriptor(samplers.GetRef(), available->descriptor, input.sampler.GetRef())) return false;
            available->identity = input.sampler.GetRef(); available->used = frame; output.sampler = available->descriptor;
            return true;
        }
        rhi::PipelineRef GetPipeline(rhi::Format format) noexcept
        {
            for (const auto& entry : pipelines) if (entry.format == format) return entry.pipeline.GetRef();
            rhi::GraphicsPipelineDesc desc;
            desc.vertexShader = vertexShader.GetRef(); desc.pixelShader = pixelShader.GetRef(); desc.vertexLayout = vertexLayout;
            const rhi::BindingLayoutRef layout = pushLayout.GetRef();
            desc.bindingLayouts = &layout; desc.bindingLayoutCount = 1;
            const rhi::DescriptorDomainRef domains[]{resources.GetRef(), samplers.GetRef()};
            desc.descriptorDomains = domains; desc.descriptorDomainCount = 2;
            desc.rasterizer.cull = rhi::RasterCullMode::None;
            auto& blend = desc.blend.attachments[0];
            blend.blendEnable = true;
            blend.sourceColor = rhi::BlendFactor::SourceAlpha; blend.destinationColor = rhi::BlendFactor::OneMinusSourceAlpha;
            blend.sourceAlpha = rhi::BlendFactor::One; blend.destinationAlpha = rhi::BlendFactor::OneMinusSourceAlpha;
            desc.attachments.colorCount = 1; desc.attachments.colorFormats[0] = format;
            rhi::Pipeline pipeline(rhi::AdoptReference, rhi::CreateGraphicsPipeline(desc));
            if (!pipeline.IsValid()) return {};
            pipelines.PushBack({format, static_cast<rhi::Pipeline&&>(pipeline)});
            return pipelines[pipelines.Size() - 1].pipeline.GetRef();
        }
        bool Submit(rendering::EngineViewportHandle viewport, UiFrame& payload, bool upload, EditorUiFailure* failure) noexcept
        {
            UiStallProbe probe(upload ? "upload BeginFrame/SubmitFrame" : "host BeginFrame/Acquire/SubmitFrame");
            RenderFrameInfo frameInfo;
            RenderFrameSetup setup;
            setup.mode = RenderingMode::OverlayOnly; setup.purpose = RenderFramePurpose::Blank; setup.present = !upload;
            auto& viewports = rendering->GetViewports();
            ViewportFailure viewportFailure;
            if (!viewports.BeginFrame(viewport, setup, frameInfo, &viewportFailure)) return Fail(failure, viewportFailure.message != nullptr ? viewportFailure.message : "editor UI output frame could not begin");
            frameInfo.SetGraph({upload ? &uploadGraph : &drawGraph, RegisterImports});
            if (!frameInfo.SetPayload({&payload, RetainFrame, ReleaseFrame}))
            {
                static_cast<void>(viewports.AbandonFrame(viewport, frameInfo));
                return Fail(failure, "editor UI frame payload is invalid");
            }
            RenderFrameSubmission submission;
            if (!viewports.SubmitFrame(viewport, frameInfo, submission, &viewportFailure))
            {
                VG_LOG_ERROR(diagnostics::Category::Engine, "editor UI submission failed: viewportCode=%u rhiCode=%u backendCode=%lld detail=%s", u32(viewportFailure.code), u32(viewportFailure.rhiFailure.code), static_cast<long long>(viewportFailure.rhiFailure.backendCode), viewportFailure.rhiFailure.message);
                static_cast<void>(viewports.AbandonFrame(viewport, frameInfo));
                return Fail(failure, "editor UI frame submission failed; see preceding backend diagnostic");
            }
            return true;
        }
    };

    EditorUiRenderer::~EditorUiRenderer() { Shutdown(); }

    bool EditorUiRenderer::Initialize(engine::RenderingService& rendering, window::WindowManager& windows, EditorUiFailure* failure) noexcept
    {
        if (m_impl != nullptr) return Fail(failure, "editor UI renderer already initialized");
        auto block = memory::Allocate(memory::PoolId::Editor, sizeof(Impl), alignof(Impl));
        if (!block) return Fail(failure, "editor UI renderer allocation failed");
        m_impl = new (block.address) Impl();
        auto& impl = *m_impl;
        impl.rendering = &rendering;
        if (!impl.presentation.Initialize(windows, rendering.GetViewports())) return Fail(failure, "editor presentation initialization failed");
        RenderViewportDesc output; output.name = "EditorUiUploads"; output.outputKind = RenderViewportOutputKind::Headless;
        if (!rendering.GetViewports().CreateRenderViewport(output, impl.uploadOutput) ||
            !rendering.GetViewports().CreateEngineViewport({"EditorUiUploads", impl.uploadOutput, false}, impl.uploadViewport)) return Fail(failure, "editor upload viewport creation failed");
        shader_tools::ShaderCompiler compiler;
        shader_tools::CompileOutput compiled;
        const shader_tools::EntryPoint entries[]{{"UiVertex", shaders::ShaderStage::Vertex}, {"UiFragment", shaders::ShaderStage::Fragment}};
        shader_tools::CompileRequest request;
        request.sourceName = "editor_ui.slang"; request.moduleName = "editor_ui";
        request.source = {reinterpret_cast<const u8*>(EditorUiShader), sizeof(EditorUiShader) - 1};
        request.entryPoints = {entries, 2};
        request.settings.target = rhi::GetCapabilities().backend == rhi::BackendKind::Vulkan ? shader_tools::Target::VulkanSpirV : shader_tools::Target::D3D12Dxil;
        if (compiler.Initialize() != shader_tools::Result::Success || compiler.Compile(request, compiled) != shader_tools::Result::Success)
            return Fail(failure, "editor built-in UI shader compilation failed");
        for (const auto& stage : compiled.GetStages())
        {
            const bool vertex = stage.stage == shaders::ShaderStage::Vertex;
            rhi::Shader shader(rhi::AdoptReference, rhi::CreateShader({vertex ? rhi::ShaderStage::Vertex : rhi::ShaderStage::Pixel,
                compiled.GetBytecode().Data() + stage.bytecodeOffset, stage.bytecodeSize, stage.entryPointName}));
            if (vertex) impl.vertexShader = static_cast<rhi::Shader&&>(shader); else impl.pixelShader = static_cast<rhi::Shader&&>(shader);
        }
        const auto visibility = rhi::ShaderStageBit(rhi::ShaderStage::Vertex) | rhi::ShaderStageBit(rhi::ShaderStage::Pixel);
        impl.resources = rhi::DescriptorDomain(rendering.GetResourceDescriptorDomain());
        impl.samplers = rhi::DescriptorDomain(rendering.GetSamplerDescriptorDomain());
        const rhi::BindingLayoutEntry constants{0, 32, rhi::BindingType::PushConstants};
        impl.pushLayout = rhi::BindingLayout(rhi::AdoptReference, rhi::RequestBindingLayout({&constants, 1, 0, visibility}));
        const rhi::VertexBindingDesc binding{0, sizeof(EditorUiVertex), rhi::VertexInputRate::PerVertex, 1};
        const rhi::VertexAttributeDesc attributes[]{{0, 0, 0, rhi::Format::R32G32Float, "POSITION", 0}, {1, 0, 8, rhi::Format::R32G32Float, "TEXCOORD", 0}, {2, 0, 16, rhi::Format::R8G8B8A8UNorm, "COLOR", 0}};
        impl.vertexLayout = rhi::GetVertexLayout({&binding, 1, attributes, 3});
        if (!impl.vertexShader.IsValid() || !impl.pixelShader.IsValid() || !impl.resources.IsValid() || !impl.samplers.IsValid() || !impl.pushLayout.IsValid() || !impl.vertexLayout.IsValid())
            return Fail(failure, "editor UI GPU state creation failed");
        BuildGraph(impl.drawGraph, impl.drawNodes, false);
        BuildGraph(impl.uploadGraph, impl.uploadNodes, true);
        impl.drawGraph.BuildRenderFlowGroups(); impl.uploadGraph.BuildRenderFlowGroups();
        return true;
    }

    bool EditorUiRenderer::BeginFrame(EditorUiFailure* failure) noexcept
    {
        UiStallProbe probe("renderer BeginFrame including join/reconciliation");
        if (m_impl == nullptr) return Fail(failure, "editor UI renderer unavailable");
        auto& impl = *m_impl;
        {
            UiStallProbe joinProbe("previous render-chain join");
            if (!impl.rendering->GetCommands().FlushPreviousFrameProcessing()) return Fail(failure, "editor UI previous frame join failed");
        }
        rhi::ResidencyFenceSet fences;
        if (!rhi::GetSubmittedResidencyFences(fences)) return Fail(failure, "editor UI descriptor retirement fences unavailable");
        impl.retirement = {fences.graphics, fences.compute, fences.copy};
        for (auto& cached : impl.textures)
            if (cached.identity.IsValid() && cached.used < impl.frame && !impl.Retire(cached)) return Fail(failure, "editor UI descriptor retirement failed");
        for (auto& sampler : impl.samplerCache)
            if (sampler.identity.IsValid() && sampler.used < impl.frame)
            {
                if (!rhi::RetireDescriptor(impl.samplers.GetRef(), sampler.descriptor, impl.retirement)) return Fail(failure, "editor UI sampler retirement failed");
                sampler = {};
            }
        ++impl.frame;
        UiStallProbe reconcileProbe("presentation reconciliation/resize");
        return impl.presentation.Tick() || Fail(failure, "editor host reconciliation failed");
    }

    bool EditorUiRenderer::CreateHost(EditorHostWindowHandle handle, EditorHostWindowDesc& desc, EditorUiFailure* failure) noexcept
    {
        UiStallProbe probe("CreateHost");
        if (m_impl == nullptr || !handle.IsValid() || desc.presentationOutput.IsValid()) return Fail(failure, "editor host must supply a native window without external presentation ownership");
        auto& host = m_impl->hosts[handle.index];
        PresentationOutputDesc output; output.name = "EditorUiHost"; output.window = desc.window; output.swapChain.colorPreference = SwapChainPolicy::ColorPreference::Sdr;
        // Independent tool windows must not each pace the main-thread UI loop.
        // Back-buffer reuse still waits for its GPU completion fence in the RHI.
        output.swapChain.enableFrameLatencyPacing = false;
        // Editor-host presentation comparison: keep interval-zero secondary hosts without tearing.
        output.swapChain.allowTearing = false;
        // Pace on the primary host only; secondary windows must not each wait for another refresh.
        if (!desc.primary) output.swapChain.presentMode = rhi::PresentMode::Immediate;
        if (!m_impl->presentation.CreateOutput(output, host.output)) return Fail(failure, "editor host presentation creation failed");
        const auto* presentation = m_impl->presentation.Resolve(host.output);
        const auto viewport = presentation->GetRenderViewport()->GetHandle();
        if (!m_impl->rendering->GetViewports().CreateEngineViewport({"EditorUiHost", viewport, true}, host.viewport))
        {
            static_cast<void>(m_impl->presentation.DestroyOutput(host.output)); host = {};
            return Fail(failure, "editor host frame viewport creation failed");
        }
        host.handle = handle; desc.presentationOutput = host.output; desc.renderViewport = viewport;
        return true;
    }

    bool EditorUiRenderer::DestroyHost(EditorHostWindowHandle handle, EditorUiFailure* failure) noexcept
    {
        UiStallProbe probe("DestroyHost");
        if (m_impl == nullptr || !handle.IsValid()) return true;
        auto& host = m_impl->hosts[handle.index];
        if (host.handle != handle) return true;
        if (!m_impl->rendering->GetCommands().FlushPreviousFrameProcessing() ||
            !m_impl->rendering->GetViewports().DestroyEngineViewport(host.viewport) || !m_impl->presentation.DestroyOutput(host.output))
            return Fail(failure, "editor host destruction could not join and release presentation");
        host = {};
        return true;
    }

    bool EditorUiRenderer::SubmitTextures(const EditorUiTextureFrameData& data, EditorUiFailure* failure) noexcept
    {
        if (!data.IsValid()) return true;
        if (m_impl == nullptr) return Fail(failure, "editor UI renderer unavailable");
        auto* frame = NewFrame();
        if (frame == nullptr) return Fail(failure, "editor UI upload frame allocation failed");
        frame->uploads = data;
        const auto& uploads = *static_cast<const EditorUiTextureUpdateData*>(data.GetPayload().data);
        frame->textures.Resize(uploads.uploads.Size());
        for (u32 index = 0; index < frame->textures.Size(); ++index) std::snprintf(frame->textures[index].name, sizeof(frame->textures[index].name), "UiTexture%u", index);
        // Submit even if every native host is minimized: atlas acceptance must not depend on host acquisition.
        const bool submitted = m_impl->Submit(m_impl->uploadViewport, *frame, true, failure);
        ReleaseFrame(frame);
        return submitted;
    }

    bool EditorUiRenderer::SubmitHost(const EditorUiFrameData& data, EditorUiFailure* failure) noexcept
    {
        UiStallProbe probe("SubmitHost including buffers/descriptors");
        if (!data.IsValid()) return true;
        if (m_impl == nullptr) return Fail(failure, "editor UI renderer unavailable");
        auto& impl = *m_impl;
        auto& host = impl.hosts[data.GetHost().index];
        if (host.handle != data.GetHost()) return Fail(failure, "editor UI draw references a stale host");
        const auto* output = impl.presentation.Resolve(host.output);
        if (output == nullptr) return Fail(failure, "editor host presentation unavailable");
        if (output->GetState() != PresentationOutputState::Ready) return true;
        const auto& draw = *static_cast<const EditorUiRenderData*>(data.GetPayload().data);
        if (draw.displayExtent[0] <= 0 || draw.displayExtent[1] <= 0) return true;
        const auto reserve = [](rhi::Buffer& buffer, u64& capacity, u64 required, rhi::BufferUsage usage) noexcept
        {
            if (required <= capacity) return true;
            rhi::BufferDesc desc; desc.size = std::max(required, capacity + capacity / 2 + 4096); desc.usage = usage | rhi::BufferUsage::CopyDestination;
            rhi::Buffer replacement(rhi::AdoptReference, rhi::CreateBuffer(desc));
            if (!replacement.IsValid()) return false;
            buffer = static_cast<rhi::Buffer&&>(replacement); capacity = desc.size;
            return true;
        };
        if (!reserve(host.vertices, host.vertexCapacity, u64(draw.vertices.Size()) * sizeof(EditorUiVertex), rhi::BufferUsage::Vertex) ||
            !reserve(host.indices, host.indexCapacity, u64(draw.indices.Size()) * sizeof(u16), rhi::BufferUsage::Index)) return Fail(failure, "editor UI geometry buffer allocation failed");
        auto* frame = NewFrame();
        if (frame == nullptr) return Fail(failure, "editor UI draw frame allocation failed");
        frame->draw = data; frame->vertices = host.vertices; frame->indices = host.indices; frame->pipeline = rhi::Pipeline(impl.GetPipeline(output->GetActiveFormat()));
        frame->textures.Resize(draw.textures.Size());
        bool ready = frame->pipeline.IsValid();
        for (u32 index = 0; ready && index < draw.textures.Size(); ++index)
        {
            std::snprintf(frame->textures[index].name, sizeof(frame->textures[index].name), "UiTexture%u", index);
            ready = impl.BindTexture(draw.textures[index], frame->textures[index]);
        }
        const bool submitted = ready ? impl.Submit(host.viewport, *frame, false, failure) : Fail(failure, "editor UI pipeline or texture binding failed");
        ReleaseFrame(frame);
        return submitted;
    }

    void EditorUiRenderer::Shutdown() noexcept
    {
        if (m_impl == nullptr) return;
        auto& impl = *m_impl;
        if (!impl.rendering->GetCommands().FlushPreviousFrameProcessing()) VG_FATAL("editor UI renderer shutdown could not join frames");
        rhi::ResidencyFenceSet fences;
        if (rhi::GetSubmittedResidencyFences(fences))
        {
            impl.retirement = {fences.graphics, fences.compute, fences.copy};
            for (auto& texture : impl.textures) if (!impl.Retire(texture)) VG_FATAL("editor UI texture slot retirement failed");
            for (auto& sampler : impl.samplerCache)
                if (sampler.descriptor.IsValid() && !rhi::RetireDescriptor(impl.samplers.GetRef(), sampler.descriptor, impl.retirement)) VG_FATAL("editor UI sampler slot retirement failed");
        }
        for (auto& host : impl.hosts) if (host.handle.IsValid() && !DestroyHost(host.handle, nullptr)) VG_FATAL("editor UI host shutdown failed");
        if (impl.uploadViewport.IsValid() && !impl.rendering->GetViewports().DestroyEngineViewport(impl.uploadViewport)) VG_FATAL("editor UI upload viewport shutdown failed");
        if (impl.uploadOutput.IsValid() && !impl.rendering->GetViewports().DestroyRenderViewport(impl.uploadOutput)) VG_FATAL("editor UI upload output shutdown failed");
        if (impl.presentation.IsInitialized() && !impl.presentation.Shutdown()) VG_FATAL("editor UI presentation shutdown failed");
        impl.~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Editor};
        memory::Free(block); m_impl = nullptr;
    }
}
