#include <vanguard/rendering/render_pipeline_factory.hpp>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] const RenderShader* ResolveShader(const RenderPipelineRequest& request, const pipelines::ShaderReference& reference) noexcept
        {
            for (const ResolvedRenderShader& resolved : request.shaders)
                if (resolved.resource == reference.resource)
                    return resolved.shader;
            return nullptr;
        }

        [[nodiscard]] bool IsCurrent(const RenderShader& shader, const pipelines::ShaderReference& reference) noexcept
        {
            return shader.IsLoaded() && shader.GetPermutation() == reference.permutation && shader.BindingLayoutFingerprint() == reference.bindingLayout &&
                   shader.GetPipelineInterfaceFingerprint() == reference.pipelineInterface;
        }

        [[nodiscard]] rhi::Format ConvertFormat(const pipelines::Format format) noexcept
        {
            static_assert(static_cast<u16>(pipelines::Format::R8UNorm) == static_cast<u16>(rhi::Format::R8UNorm));
            static_assert(static_cast<u16>(pipelines::Format::BC7UNormSrgb) == static_cast<u16>(rhi::Format::BC7UNormSrgb));
            return format < pipelines::Format::Count ? static_cast<rhi::Format>(format) : rhi::Format::Unknown;
        }

        [[nodiscard]] RenderPipelineResult PopulateShaders(const RenderPipelineRequest& request, rhi::GraphicsPipelineDesc& output,
                                                           rhi::ComputePipelineDesc& compute) noexcept
        {
            for (const pipelines::ShaderReference& reference : request.pipeline->GetShaders())
            {
                const RenderShader* const shader = ResolveShader(request, reference);
                if (shader == nullptr || !IsCurrent(*shader, reference))
                    return RenderPipelineResult::StaleShader;
                const auto assign = [](rhi::ShaderRef& destination, const rhi::ShaderRef source) noexcept
                {
                    if (!source)
                        return true;
                    if (destination)
                        return false;
                    destination = source;
                    return true;
                };
                if (!assign(output.vertexShader, shader->GetStage(shaders::ShaderStage::Vertex)) ||
                    !assign(output.hullShader, shader->GetStage(shaders::ShaderStage::Hull)) ||
                    !assign(output.domainShader, shader->GetStage(shaders::ShaderStage::Domain)) ||
                    !assign(output.geometryShader, shader->GetStage(shaders::ShaderStage::Geometry)) ||
                    !assign(output.pixelShader, shader->GetStage(shaders::ShaderStage::Fragment)) ||
                    !assign(compute.computeShader, shader->GetStage(shaders::ShaderStage::Compute)))
                    return RenderPipelineResult::DuplicateStage;
                if (shader->GetStage(shaders::ShaderStage::Task) || shader->GetStage(shaders::ShaderStage::Mesh) || shader->GetStage(shaders::ShaderStage::Library))
                    return RenderPipelineResult::UnsupportedState;
            }
            return RenderPipelineResult::Success;
        }

        [[nodiscard]] bool ConvertBlendFactor(const pipelines::BlendFactor source, rhi::BlendFactor& destination) noexcept
        {
            switch (source)
            {
            case pipelines::BlendFactor::Zero:
                destination = rhi::BlendFactor::Zero;
                return true;
            case pipelines::BlendFactor::One:
                destination = rhi::BlendFactor::One;
                return true;
            case pipelines::BlendFactor::SourceColor:
                destination = rhi::BlendFactor::SourceColor;
                return true;
            case pipelines::BlendFactor::OneMinusSourceColor:
                destination = rhi::BlendFactor::OneMinusSourceColor;
                return true;
            case pipelines::BlendFactor::DestinationColor:
                destination = rhi::BlendFactor::DestinationColor;
                return true;
            case pipelines::BlendFactor::OneMinusDestinationColor:
                destination = rhi::BlendFactor::OneMinusDestinationColor;
                return true;
            case pipelines::BlendFactor::SourceAlpha:
                destination = rhi::BlendFactor::SourceAlpha;
                return true;
            case pipelines::BlendFactor::OneMinusSourceAlpha:
                destination = rhi::BlendFactor::OneMinusSourceAlpha;
                return true;
            case pipelines::BlendFactor::DestinationAlpha:
                destination = rhi::BlendFactor::DestinationAlpha;
                return true;
            case pipelines::BlendFactor::OneMinusDestinationAlpha:
                destination = rhi::BlendFactor::OneMinusDestinationAlpha;
                return true;
            case pipelines::BlendFactor::ConstantColor:
                destination = rhi::BlendFactor::ConstantColor;
                return true;
            case pipelines::BlendFactor::OneMinusConstantColor:
                destination = rhi::BlendFactor::OneMinusConstantColor;
                return true;
            case pipelines::BlendFactor::SourceAlphaSaturate:
                destination = rhi::BlendFactor::SourceAlphaSaturate;
                return true;
            case pipelines::BlendFactor::SourceOneColor:
                destination = rhi::BlendFactor::SourceOneColor;
                return true;
            case pipelines::BlendFactor::OneMinusSourceOneColor:
                destination = rhi::BlendFactor::OneMinusSourceOneColor;
                return true;
            case pipelines::BlendFactor::SourceOneAlpha:
                destination = rhi::BlendFactor::SourceOneAlpha;
                return true;
            case pipelines::BlendFactor::OneMinusSourceOneAlpha:
                destination = rhi::BlendFactor::OneMinusSourceOneAlpha;
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] rhi::ComparisonFunction ConvertCompare(const pipelines::CompareOperation value) noexcept
        {
            return static_cast<rhi::ComparisonFunction>(value);
        }

        [[nodiscard]] rhi::StencilOperation ConvertStencil(const pipelines::StencilOperation value) noexcept
        {
            return static_cast<rhi::StencilOperation>(value);
        }

        [[nodiscard]] rhi::StencilFaceStateDesc ConvertStencilFace(const pipelines::StencilFaceState& source) noexcept
        {
            return {ConvertStencil(source.fail), ConvertStencil(source.depthFail), ConvertStencil(source.pass), ConvertCompare(source.compare)};
        }

        [[nodiscard]] bool BuildGraphicsDescription(const RenderPipelineRequest& request, rhi::GraphicsPipelineDesc& output,
                                                    rhi::VertexLayoutRef& ownedVertexLayout) noexcept
        {
            const pipelines::GraphicsState& source = request.pipeline->GetGraphics();
            if (source.primitiveRestart || source.rasterizer.rasterizerDiscard || source.multisample.sampleMask != 0xffffffffu ||
                source.multisample.sampleShading || source.depthStencil.depthBoundsTest || source.blend.logicOperationEnable)
                return false;

            containers::DynamicArray<rhi::VertexBindingDesc> bindings{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<rhi::VertexAttributeDesc> attributes{memory::pools::Rendering::GetInstance()};
            bindings.Reserve(request.pipeline->GetVertexStreams().Size());
            attributes.Reserve(request.pipeline->GetVertexAttributes().Size());
            for (const pipelines::VertexStream& stream : request.pipeline->GetVertexStreams())
            {
                if (stream.binding > 0xffu || stream.stride > 0xffffu || stream.instanceStepRate > 0xffffu)
                    return false;
                bindings.PushBack({static_cast<u8>(stream.binding), static_cast<u16>(stream.stride),
                                   stream.inputRate == pipelines::InputRate::PerInstance ? rhi::VertexInputRate::PerInstance : rhi::VertexInputRate::PerVertex,
                                   static_cast<u16>(stream.instanceStepRate)});
            }
            for (const pipelines::VertexAttribute& attribute : request.pipeline->GetVertexAttributes())
            {
                if (attribute.location > 0xffu || attribute.streamBinding > 0xffu || attribute.byteOffset > 0xffffu)
                    return false;
                attributes.PushBack({static_cast<u8>(attribute.location), static_cast<u8>(attribute.streamBinding), static_cast<u16>(attribute.byteOffset),
                                     ConvertFormat(attribute.format), attribute.semanticName, attribute.semanticIndex});
            }
            if (!bindings.Empty())
            {
                const rhi::VertexLayoutDesc layout{bindings.TypedData(), bindings.Size(), attributes.TypedData(), attributes.Size()};
                ownedVertexLayout = rhi::GetVertexLayout(layout);
                if (!ownedVertexLayout)
                    return false;
                output.vertexLayout = ownedVertexLayout;
            }

            output.topology = static_cast<rhi::PrimitiveTopology>(source.topology);
            output.patchControlPoints = source.patchControlPoints;
            output.rasterizer.fill = static_cast<rhi::RasterFillMode>(source.rasterizer.fill);
            output.rasterizer.cull = static_cast<rhi::RasterCullMode>(source.rasterizer.cull);
            output.rasterizer.frontCounterClockwise = source.rasterizer.frontFace == pipelines::FrontFace::CounterClockwise;
            output.rasterizer.depthClipEnable = source.rasterizer.depthClipEnable;
            output.rasterizer.conservativeRasterization = source.rasterizer.conservativeRasterization;
            output.rasterizer.depthBias = source.rasterizer.depthBias;
            output.rasterizer.depthBiasClamp = source.rasterizer.depthBiasClamp;
            output.rasterizer.slopeScaledDepthBias = source.rasterizer.slopeScaledDepthBias;
            output.rasterizer.multisampleEnable = source.multisample.sampleCount > 1;
            output.depthStencil.depthTestEnable = source.depthStencil.depthTest;
            output.depthStencil.depthWriteEnable = source.depthStencil.depthWrite;
            output.depthStencil.depthComparison = ConvertCompare(source.depthStencil.depthCompare);
            output.depthStencil.stencilEnable = source.depthStencil.stencilTest;
            output.depthStencil.stencilReadMask = source.depthStencil.stencilReadMask;
            output.depthStencil.stencilWriteMask = source.depthStencil.stencilWriteMask;
            output.depthStencil.front = ConvertStencilFace(source.depthStencil.front);
            output.depthStencil.back = ConvertStencilFace(source.depthStencil.back);
            output.blend.alphaToCoverageEnable = source.multisample.alphaToCoverage;
            for (u32 index = 0; index < source.blend.attachmentCount; ++index)
            {
                const pipelines::BlendAttachmentState& input = source.blend.attachments[index];
                rhi::BlendAttachmentStateDesc& destination = output.blend.attachments[index];
                if (!ConvertBlendFactor(input.sourceColor, destination.sourceColor) ||
                    !ConvertBlendFactor(input.destinationColor, destination.destinationColor) ||
                    !ConvertBlendFactor(input.sourceAlpha, destination.sourceAlpha) ||
                    !ConvertBlendFactor(input.destinationAlpha, destination.destinationAlpha))
                    return false;
                destination.blendEnable = input.blendEnable;
                destination.colorOperation = static_cast<rhi::BlendOperation>(input.colorOperation);
                destination.alphaOperation = static_cast<rhi::BlendOperation>(input.alphaOperation);
                destination.colorWriteMask = input.writeMask;
            }

            const pipelines::AttachmentSignature* attachments = request.attachments;
            if (source.attachmentPolicy == pipelines::AttachmentPolicy::Exact)
                attachments = &source.exactAttachments;
            if (attachments == nullptr)
                return false;
            output.attachments.colorCount = attachments->colorCount;
            output.attachments.depthStencilFormat = ConvertFormat(attachments->depthStencilFormat);
            output.attachments.sampleCount = attachments->sampleCount;
            for (u32 index = 0; index < attachments->colorCount; ++index)
                output.attachments.colorFormats[index] = ConvertFormat(attachments->colors[index].format);
            return true;
        }
    } // namespace

    const char* ToString(const RenderPipelineResult result) noexcept
    {
        switch (result)
        {
        case RenderPipelineResult::Success:
            return "Success";
        case RenderPipelineResult::InvalidArgument:
            return "InvalidArgument";
        case RenderPipelineResult::MissingShader:
            return "MissingShader";
        case RenderPipelineResult::StaleShader:
            return "StaleShader";
        case RenderPipelineResult::DuplicateStage:
            return "DuplicateStage";
        case RenderPipelineResult::UnsupportedState:
            return "UnsupportedState";
        case RenderPipelineResult::NativeObjectFailure:
            return "NativeObjectFailure";
        case RenderPipelineResult::CacheFailure:
            return "CacheFailure";
        }
        return "Unknown";
    }

    RenderPipelineResult RequestRenderPipeline(const RenderPipelineRequest& request, PipelineCache& cache, PipelineRequest& output) noexcept
    {
        output.Reset();
        if (request.pipeline == nullptr || !request.pipeline->IsOpen() || !cache.IsInitialized() ||
            request.interfaceResources.bindingLayouts.Size() > rhi::MaximumBindingLayoutsPerPipeline ||
            request.interfaceResources.descriptorDomains.Size() > rhi::MaximumDescriptorDomainsPerPipeline)
            return RenderPipelineResult::InvalidArgument;
        for (const pipelines::ShaderReference& reference : request.pipeline->GetShaders())
        {
            const RenderShader* const shader = ResolveShader(request, reference);
            if (shader == nullptr)
                return RenderPipelineResult::MissingShader;
            if (!IsCurrent(*shader, reference))
                return RenderPipelineResult::StaleShader;
        }

        crypto::Digest256 key;
        if (pipelines::CalculateConcretePipelineKey(*request.pipeline, request.attachments, key) != pipelines::Result::Success)
            return RenderPipelineResult::InvalidArgument;

        rhi::GraphicsPipelineDesc graphics;
        rhi::ComputePipelineDesc compute;
        const RenderPipelineResult shaderResult = PopulateShaders(request, graphics, compute);
        if (shaderResult != RenderPipelineResult::Success)
            return shaderResult;
        const PipelineInterfaceResources& resources = request.interfaceResources;
        if (request.pipeline->GetKind() == pipelines::PipelineKind::Graphics)
        {
            rhi::VertexLayoutRef vertexLayout;
            if (!BuildGraphicsDescription(request, graphics, vertexLayout))
                return RenderPipelineResult::UnsupportedState;
            graphics.bindingLayouts = resources.bindingLayouts.Data();
            graphics.bindingLayoutCount = resources.bindingLayouts.Size();
            graphics.descriptorDomains = resources.descriptorDomains.Data();
            graphics.descriptorDomainCount = resources.descriptorDomains.Size();
            const pipeline_cache::Result result = cache.RequestGraphics(key, graphics, output, request.priority);
            return result == pipeline_cache::Result::Success ? RenderPipelineResult::Success : RenderPipelineResult::CacheFailure;
        }
        if (request.pipeline->GetKind() == pipelines::PipelineKind::Compute)
        {
            compute.bindingLayouts = resources.bindingLayouts.Data();
            compute.bindingLayoutCount = resources.bindingLayouts.Size();
            compute.descriptorDomains = resources.descriptorDomains.Data();
            compute.descriptorDomainCount = resources.descriptorDomains.Size();
            return cache.RequestCompute(key, compute, output, request.priority) == pipeline_cache::Result::Success ? RenderPipelineResult::Success
                                                                                                                   : RenderPipelineResult::CacheFailure;
        }
        return RenderPipelineResult::UnsupportedState;
    }
} // namespace vanguard::rendering
