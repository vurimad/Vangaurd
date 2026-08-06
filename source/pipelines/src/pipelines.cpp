#include <vanguard/pipelines/pipelines.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace vanguard;
    namespace pipeline = vanguard::pipelines;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 MetadataSection = serialization::MakeFourCC('P', 'I', 'P', 'E');
    constexpr u32 MetadataWireVersion = 1;
    constexpr u64 KnownDynamicStates =
        static_cast<u64>(pipeline::DynamicState::Viewport) | static_cast<u64>(pipeline::DynamicState::Scissor) |
        static_cast<u64>(pipeline::DynamicState::BlendConstants) | static_cast<u64>(pipeline::DynamicState::StencilReference) |
        static_cast<u64>(pipeline::DynamicState::DepthBias) | static_cast<u64>(pipeline::DynamicState::DepthBounds) |
        static_cast<u64>(pipeline::DynamicState::PrimitiveTopology) | static_cast<u64>(pipeline::DynamicState::FragmentShadingRate);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : shaders(memory::pools::Rendering::GetInstance()), vertexStreams(memory::pools::Rendering::GetInstance()),
              vertexAttributes(memory::pools::Rendering::GetInstance()), rayTracingGroups(memory::pools::Rendering::GetInstance())
        {
        }

        pipeline::PipelineKind kind = pipeline::PipelineKind::Graphics;
        u64 name = 0;
        pipeline::DynamicState dynamicStates = pipeline::DynamicState::None;
        containers::DynamicArray<pipeline::ShaderReference> shaders;
        pipeline::GraphicsState graphics;
        containers::DynamicArray<pipeline::VertexStream> vertexStreams;
        containers::DynamicArray<pipeline::VertexAttribute> vertexAttributes;
        pipeline::RayTracingState rayTracing;
        containers::DynamicArray<pipeline::RayTracingGroup> rayTracingGroups;
    };

    [[nodiscard]] pipeline::Result ConvertSerializationResult(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return pipeline::Result::Success;
        case serialization::Result::InvalidMagic:
            return pipeline::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
            return pipeline::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return pipeline::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return pipeline::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return pipeline::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return pipeline::Result::IoFailure;
        default:
            return pipeline::Result::InvalidLayout;
        }
    }

    [[nodiscard]] pipeline::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? pipeline::Result::Success : ConvertSerializationResult(writer.Status());
    }

    [[nodiscard]] pipeline::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? pipeline::Result::Success : ConvertSerializationResult(reader.Status());
    }

    template <typename Type> void CopySpan(const containers::ArraySpan<const Type> source, containers::DynamicArray<Type>& destination)
    {
        destination.Reserve(source.Size());
        for (u32 index = 0; index < source.Size(); ++index)
        {
            destination.PushBack(source[index]);
        }
    }

    [[nodiscard]] bool IsFinite(const f32 value) noexcept
    {
        return std::isfinite(value);
    }

    void NormalizeZero(f32& value) noexcept
    {
        if (value == 0.0f)
        {
            value = 0.0f;
        }
    }

    [[nodiscard]] bool IsSampleCountValid(const u8 value) noexcept
    {
        return value == 1 || value == 2 || value == 4 || value == 8 || value == 16;
    }

    [[nodiscard]] shaders::PrimitiveClass PrimitiveClassOf(const pipeline::PrimitiveTopology topology) noexcept
    {
        switch (topology)
        {
        case pipeline::PrimitiveTopology::PointList:
            return shaders::PrimitiveClass::Point;
        case pipeline::PrimitiveTopology::LineList:
        case pipeline::PrimitiveTopology::LineStrip:
            return shaders::PrimitiveClass::Line;
        case pipeline::PrimitiveTopology::TriangleList:
        case pipeline::PrimitiveTopology::TriangleStrip:
            return shaders::PrimitiveClass::Triangle;
        case pipeline::PrimitiveTopology::PatchList:
            return shaders::PrimitiveClass::Patch;
        }
        return shaders::PrimitiveClass::Any;
    }

    [[nodiscard]] bool ShaderReferenceLess(const pipeline::ShaderReference& left, const pipeline::ShaderReference& right) noexcept
    {
        if (left.resource != right.resource)
        {
            return left.resource < right.resource;
        }
        if (left.permutation != right.permutation)
        {
            return left.permutation < right.permutation;
        }
        if (left.bindingLayout != right.bindingLayout)
        {
            return left.bindingLayout < right.bindingLayout;
        }
        return left.pipelineInterface < right.pipelineInterface;
    }

    [[nodiscard]] bool ShaderReferenceEqual(const pipeline::ShaderReference& left, const pipeline::ShaderReference& right) noexcept
    {
        return left.resource == right.resource && left.permutation == right.permutation && left.bindingLayout == right.bindingLayout &&
               left.pipelineInterface == right.pipelineInterface;
    }

    [[nodiscard]] pipeline::Result ValidateAttachmentSignature(const pipeline::AttachmentSignature& signature) noexcept
    {
        if (signature.colorCount > pipeline::MaximumColorAttachments || !IsSampleCountValid(signature.sampleCount))
        {
            return pipeline::Result::InvalidLayout;
        }
        for (u32 index = 0; index < pipeline::MaximumColorAttachments; ++index)
        {
            const pipeline::AttachmentFormat& attachment = signature.colors[index];
            if (index < signature.colorCount)
            {
                if (attachment.format == pipeline::InvalidFormat || attachment.numericClass > shaders::NumericClass::UnsignedInteger)
                {
                    return pipeline::Result::InvalidLayout;
                }
            }
            else if (attachment.format != pipeline::InvalidFormat)
            {
                return pipeline::Result::InvalidLayout;
            }
        }
        if (signature.depthStencilClass > pipeline::DepthStencilClass::DepthStencil ||
            (signature.depthStencilFormat == pipeline::InvalidFormat) != (signature.depthStencilClass == pipeline::DepthStencilClass::None))
        {
            return pipeline::Result::InvalidLayout;
        }
        return pipeline::Result::Success;
    }

    [[nodiscard]] bool IsBlendFactorValid(const pipeline::BlendFactor value) noexcept
    {
        return value <= pipeline::BlendFactor::OneMinusSourceOneAlpha;
    }

    [[nodiscard]] pipeline::Result ValidateGraphics(CanonicalData& data) noexcept
    {
        pipeline::GraphicsState& graphics = data.graphics;
        if (graphics.topology > pipeline::PrimitiveTopology::PatchList || graphics.rasterizer.fill > pipeline::FillMode::Wireframe ||
            graphics.rasterizer.cull > pipeline::CullMode::Back || graphics.rasterizer.frontFace > pipeline::FrontFace::Clockwise ||
            graphics.depthStencil.depthCompare > pipeline::CompareOperation::Always ||
            graphics.blend.logicOperation > pipeline::LogicOperation::Set ||
            graphics.attachmentPolicy > pipeline::AttachmentPolicy::Exact || !IsSampleCountValid(graphics.multisample.sampleCount) ||
            graphics.blend.attachmentCount > pipeline::MaximumColorAttachments)
        {
            return pipeline::Result::InvalidLayout;
        }
        if (graphics.topology == pipeline::PrimitiveTopology::PatchList)
        {
            if (graphics.patchControlPoints == 0 || graphics.patchControlPoints > 32)
            {
                return pipeline::Result::InvalidLayout;
            }
        }
        else if (graphics.patchControlPoints != 0)
        {
            return pipeline::Result::InvalidLayout;
        }

        if (!IsFinite(graphics.rasterizer.depthBiasClamp) || !IsFinite(graphics.rasterizer.slopeScaledDepthBias) ||
            !IsFinite(graphics.multisample.minimumSampleShading) || graphics.multisample.minimumSampleShading < 0.0f ||
            graphics.multisample.minimumSampleShading > 1.0f || !IsFinite(graphics.depthStencil.minimumDepthBounds) ||
            !IsFinite(graphics.depthStencil.maximumDepthBounds) || graphics.depthStencil.minimumDepthBounds < 0.0f ||
            graphics.depthStencil.maximumDepthBounds > 1.0f ||
            graphics.depthStencil.minimumDepthBounds > graphics.depthStencil.maximumDepthBounds)
        {
            return pipeline::Result::InvalidLayout;
        }

        NormalizeZero(graphics.rasterizer.depthBiasClamp);
        NormalizeZero(graphics.rasterizer.slopeScaledDepthBias);
        NormalizeZero(graphics.multisample.minimumSampleShading);
        NormalizeZero(graphics.depthStencil.minimumDepthBounds);
        NormalizeZero(graphics.depthStencil.maximumDepthBounds);
        if (pipeline::HasFlag(data.dynamicStates, pipeline::DynamicState::DepthBias))
        {
            graphics.rasterizer.depthBias = 0;
            graphics.rasterizer.depthBiasClamp = 0.0f;
            graphics.rasterizer.slopeScaledDepthBias = 0.0f;
        }
        if (pipeline::HasFlag(data.dynamicStates, pipeline::DynamicState::DepthBounds))
        {
            graphics.depthStencil.minimumDepthBounds = 0.0f;
            graphics.depthStencil.maximumDepthBounds = 1.0f;
        }
        if (!graphics.multisample.sampleShading)
        {
            graphics.multisample.minimumSampleShading = 0.0f;
        }
        if (!graphics.depthStencil.depthBoundsTest)
        {
            graphics.depthStencil.minimumDepthBounds = 0.0f;
            graphics.depthStencil.maximumDepthBounds = 1.0f;
        }

        const pipeline::StencilFaceState stencilFaces[2]{graphics.depthStencil.front, graphics.depthStencil.back};
        for (const pipeline::StencilFaceState& face : stencilFaces)
        {
            if (face.fail > pipeline::StencilOperation::DecrementWrap || face.depthFail > pipeline::StencilOperation::DecrementWrap ||
                face.pass > pipeline::StencilOperation::DecrementWrap || face.compare > pipeline::CompareOperation::Always)
            {
                return pipeline::Result::InvalidLayout;
            }
        }
        for (u32 index = 0; index < pipeline::MaximumColorAttachments; ++index)
        {
            const pipeline::BlendAttachmentState& attachment = graphics.blend.attachments[index];
            if (index >= graphics.blend.attachmentCount)
            {
                continue;
            }
            if (!IsBlendFactorValid(attachment.sourceColor) || !IsBlendFactorValid(attachment.destinationColor) ||
                !IsBlendFactorValid(attachment.sourceAlpha) || !IsBlendFactorValid(attachment.destinationAlpha) ||
                attachment.colorOperation > pipeline::BlendOperation::Maximum ||
                attachment.alphaOperation > pipeline::BlendOperation::Maximum || (attachment.writeMask & ~0x0fu) != 0)
            {
                return pipeline::Result::InvalidLayout;
            }
        }

        if (graphics.attachmentPolicy == pipeline::AttachmentPolicy::Exact)
        {
            const pipeline::Result result = ValidateAttachmentSignature(graphics.exactAttachments);
            if (result != pipeline::Result::Success)
            {
                return result;
            }
            if (graphics.exactAttachments.colorCount != graphics.blend.attachmentCount ||
                graphics.exactAttachments.sampleCount != graphics.multisample.sampleCount)
            {
                return pipeline::Result::AttachmentMismatch;
            }
        }
        else
        {
            graphics.exactAttachments = {};
        }

        for (u32 index = 0; index < data.vertexStreams.Size(); ++index)
        {
            pipeline::VertexStream& stream = data.vertexStreams[index];
            if (stream.stride == 0 || stream.inputRate > pipeline::InputRate::PerInstance ||
                (stream.inputRate == pipeline::InputRate::PerVertex && stream.instanceStepRate != 1) ||
                (stream.inputRate == pipeline::InputRate::PerInstance && stream.instanceStepRate == 0))
            {
                return pipeline::Result::InvalidLayout;
            }
            if (index != 0 && data.vertexStreams[index - 1].binding == stream.binding)
            {
                return pipeline::Result::DuplicateVertexStream;
            }
        }

        for (u32 index = 0; index < data.vertexAttributes.Size(); ++index)
        {
            const pipeline::VertexAttribute& attribute = data.vertexAttributes[index];
            if (attribute.semantic == 0 || attribute.numericClass > shaders::NumericClass::UnsignedInteger ||
                attribute.componentCount == 0 || attribute.componentCount > 4 ||
                (attribute.componentBits != 8 && attribute.componentBits != 16 && attribute.componentBits != 32 &&
                 attribute.componentBits != 64) ||
                (index != 0 && data.vertexAttributes[index - 1].location == attribute.location))
            {
                return index != 0 && data.vertexAttributes[index - 1].location == attribute.location
                           ? pipeline::Result::DuplicateVertexAttribute
                           : pipeline::Result::InvalidLayout;
            }
            bool foundStream = false;
            for (const pipeline::VertexStream& stream : data.vertexStreams)
            {
                if (stream.binding == attribute.streamBinding)
                {
                    const u32 attributeBytes = static_cast<u32>(attribute.componentCount) * attribute.componentBits / 8u;
                    foundStream = attribute.byteOffset <= stream.stride && attributeBytes <= stream.stride - attribute.byteOffset;
                    break;
                }
            }
            if (!foundStream)
            {
                return pipeline::Result::InvalidLayout;
            }
        }
        return pipeline::Result::Success;
    }

    [[nodiscard]] pipeline::Result ValidateRayTracing(const CanonicalData& data) noexcept
    {
        if (data.rayTracing.maximumRecursionDepth == 0 || data.rayTracing.maximumRecursionDepth > 31 ||
            data.rayTracing.maximumPayloadBytes > 4096 || data.rayTracing.maximumAttributeBytes > 4096 || data.rayTracingGroups.Empty())
        {
            return pipeline::Result::InvalidLayout;
        }
        for (u32 index = 0; index < data.rayTracingGroups.Size(); ++index)
        {
            const pipeline::RayTracingGroup& group = data.rayTracingGroups[index];
            if (group.name == 0 || group.kind > pipeline::RayTracingGroupKind::ProceduralHitGroup ||
                group.shaderLibrary >= data.shaders.Size() || (index != 0 && data.rayTracingGroups[index - 1].name == group.name))
            {
                return index != 0 && data.rayTracingGroups[index - 1].name == group.name ? pipeline::Result::DuplicateRayTracingGroup
                                                                                         : pipeline::Result::InvalidLayout;
            }
            switch (group.kind)
            {
            case pipeline::RayTracingGroupKind::General:
                if (group.generalEntry == 0 || group.closestHitEntry != 0 || group.anyHitEntry != 0 || group.intersectionEntry != 0)
                {
                    return pipeline::Result::InvalidLayout;
                }
                break;
            case pipeline::RayTracingGroupKind::TrianglesHitGroup:
                if (group.generalEntry != 0 || group.closestHitEntry == 0 || group.intersectionEntry != 0)
                {
                    return pipeline::Result::InvalidLayout;
                }
                break;
            case pipeline::RayTracingGroupKind::ProceduralHitGroup:
                if (group.generalEntry != 0 || group.closestHitEntry == 0 || group.intersectionEntry == 0)
                {
                    return pipeline::Result::InvalidLayout;
                }
                break;
            }
        }
        return pipeline::Result::Success;
    }

    [[nodiscard]] pipeline::Result Canonicalize(const pipeline::BuildDescription& description, CanonicalData& output) noexcept
    {
        output.kind = description.kind;
        output.name = description.name;
        output.dynamicStates = description.dynamicStates;
        output.graphics = description.graphics;
        output.rayTracing = description.rayTracing;
        CopySpan(description.vertexStreams, output.vertexStreams);
        CopySpan(description.vertexAttributes, output.vertexAttributes);
        CopySpan(description.rayTracingGroups, output.rayTracingGroups);

        if (description.name == 0 || description.kind > pipeline::PipelineKind::RayTracing ||
            (static_cast<u64>(description.dynamicStates) & ~KnownDynamicStates) != 0 || description.shaders.Empty())
        {
            return pipeline::Result::InvalidArgument;
        }

        containers::DynamicArray<u32> shaderOrder{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> shaderRemap{memory::pools::Rendering::GetInstance()};
        shaderOrder.Reserve(description.shaders.Size());
        shaderRemap.Resize(description.shaders.Size());
        for (u32 index = 0; index < description.shaders.Size(); ++index)
        {
            shaderOrder.PushBack(index);
        }
        std::sort(shaderOrder.Begin(), shaderOrder.End(), [&description](const u32 left, const u32 right)
                  { return ShaderReferenceLess(description.shaders[left], description.shaders[right]); });
        output.shaders.Reserve(description.shaders.Size());
        for (u32 canonicalIndex = 0; canonicalIndex < shaderOrder.Size(); ++canonicalIndex)
        {
            const u32 sourceIndex = shaderOrder[canonicalIndex];
            const pipeline::ShaderReference& shader = description.shaders[sourceIndex];
            if (shader.resource == resources::InvalidResourceId || shader.permutation.IsEmpty() || shader.bindingLayout.IsEmpty() ||
                shader.pipelineInterface.IsEmpty())
            {
                return pipeline::Result::InvalidLayout;
            }
            if (!output.shaders.Empty() && ShaderReferenceEqual(output.shaders.Back(), shader))
            {
                return pipeline::Result::DuplicateShader;
            }
            shaderRemap[sourceIndex] = canonicalIndex;
            output.shaders.PushBack(shader);
        }
        for (pipeline::RayTracingGroup& group : output.rayTracingGroups)
        {
            if (group.shaderLibrary >= shaderRemap.Size())
            {
                return pipeline::Result::InvalidLayout;
            }
            group.shaderLibrary = shaderRemap[group.shaderLibrary];
        }

        std::sort(output.vertexStreams.Begin(), output.vertexStreams.End(),
                  [](const pipeline::VertexStream& left, const pipeline::VertexStream& right) { return left.binding < right.binding; });
        std::sort(output.vertexAttributes.Begin(), output.vertexAttributes.End(),
                  [](const pipeline::VertexAttribute& left, const pipeline::VertexAttribute& right)
                  { return left.location != right.location ? left.location < right.location : left.semanticIndex < right.semanticIndex; });
        std::sort(output.rayTracingGroups.Begin(), output.rayTracingGroups.End(),
                  [](const pipeline::RayTracingGroup& left, const pipeline::RayTracingGroup& right) { return left.name < right.name; });

        switch (output.kind)
        {
        case pipeline::PipelineKind::Graphics:
            if (output.shaders.Size() != 1 || !output.rayTracingGroups.Empty())
            {
                return pipeline::Result::InvalidLayout;
            }
            return ValidateGraphics(output);
        case pipeline::PipelineKind::Compute:
            if (output.shaders.Size() != 1 || !output.vertexStreams.Empty() || !output.vertexAttributes.Empty() ||
                !output.rayTracingGroups.Empty())
            {
                return pipeline::Result::InvalidLayout;
            }
            output.graphics = {};
            output.rayTracing = {};
            return pipeline::Result::Success;
        case pipeline::PipelineKind::RayTracing:
            if (!output.vertexStreams.Empty() || !output.vertexAttributes.Empty())
            {
                return pipeline::Result::InvalidLayout;
            }
            output.graphics = {};
            return ValidateRayTracing(output);
        }
        return pipeline::Result::InvalidLayout;
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& value) noexcept
    {
        return writer.WriteBytes(value.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& value) noexcept
    {
        return reader.ReadBytes(value.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool WriteShaderReference(serialization::BinaryWriter& writer, const pipeline::ShaderReference& value) noexcept
    {
        return writer.WriteU64(value.resource) && WriteDigest(writer, value.permutation) && WriteDigest(writer, value.bindingLayout) &&
               WriteDigest(writer, value.pipelineInterface);
    }

    [[nodiscard]] bool ReadShaderReference(serialization::BinaryReader& reader, pipeline::ShaderReference& value) noexcept
    {
        return reader.ReadU64(value.resource) && ReadDigest(reader, value.permutation) && ReadDigest(reader, value.bindingLayout) &&
               ReadDigest(reader, value.pipelineInterface);
    }

    [[nodiscard]] bool WriteAttachmentSignature(serialization::BinaryWriter& writer, const pipeline::AttachmentSignature& value) noexcept
    {
        if (!writer.WriteU32(value.colorCount) || !writer.WriteU32(value.depthStencilFormat) ||
            !writer.WriteU8(static_cast<u8>(value.depthStencilClass)) || !writer.WriteU8(value.sampleCount) || !writer.WriteU16(0))
        {
            return false;
        }
        for (const pipeline::AttachmentFormat& color : value.colors)
        {
            if (!writer.WriteU32(color.format) || !writer.WriteU8(static_cast<u8>(color.numericClass)) || !writer.WriteU8(0) ||
                !writer.WriteU16(0))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ReadAttachmentSignature(serialization::BinaryReader& reader, pipeline::AttachmentSignature& value) noexcept
    {
        u8 depthStencilClass = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU32(value.colorCount) || !reader.ReadU32(value.depthStencilFormat) || !reader.ReadU8(depthStencilClass) ||
            !reader.ReadU8(value.sampleCount) || !reader.ReadU16(reserved16) || reserved16 != 0)
        {
            return false;
        }
        value.depthStencilClass = static_cast<pipeline::DepthStencilClass>(depthStencilClass);
        for (pipeline::AttachmentFormat& color : value.colors)
        {
            u8 numericClass = 0;
            u8 reserved8 = 0;
            if (!reader.ReadU32(color.format) || !reader.ReadU8(numericClass) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) ||
                reserved8 != 0 || reserved16 != 0)
            {
                return false;
            }
            color.numericClass = static_cast<shaders::NumericClass>(numericClass);
        }
        return true;
    }

    [[nodiscard]] bool WriteRasterizer(serialization::BinaryWriter& writer, const pipeline::RasterizerState& value) noexcept
    {
        return writer.WriteU8(static_cast<u8>(value.fill)) && writer.WriteU8(static_cast<u8>(value.cull)) &&
               writer.WriteU8(static_cast<u8>(value.frontFace)) && writer.WriteBool(value.depthClipEnable) &&
               writer.WriteBool(value.conservativeRasterization) && writer.WriteBool(value.rasterizerDiscard) && writer.WriteU16(0) &&
               writer.WriteI32(value.depthBias) && writer.WriteF32(value.depthBiasClamp) && writer.WriteF32(value.slopeScaledDepthBias);
    }

    [[nodiscard]] bool ReadRasterizer(serialization::BinaryReader& reader, pipeline::RasterizerState& value) noexcept
    {
        u8 fill = 0;
        u8 cull = 0;
        u8 frontFace = 0;
        u16 reserved = 0;
        if (!reader.ReadU8(fill) || !reader.ReadU8(cull) || !reader.ReadU8(frontFace) || !reader.ReadBool(value.depthClipEnable) ||
            !reader.ReadBool(value.conservativeRasterization) || !reader.ReadBool(value.rasterizerDiscard) || !reader.ReadU16(reserved) ||
            !reader.ReadI32(value.depthBias) || !reader.ReadF32(value.depthBiasClamp) || !reader.ReadF32(value.slopeScaledDepthBias) ||
            reserved != 0)
        {
            return false;
        }
        value.fill = static_cast<pipeline::FillMode>(fill);
        value.cull = static_cast<pipeline::CullMode>(cull);
        value.frontFace = static_cast<pipeline::FrontFace>(frontFace);
        return true;
    }

    [[nodiscard]] bool WriteMultisample(serialization::BinaryWriter& writer, const pipeline::MultisampleState& value) noexcept
    {
        return writer.WriteU8(value.sampleCount) && writer.WriteBool(value.alphaToCoverage) && writer.WriteBool(value.sampleShading) &&
               writer.WriteU8(0) && writer.WriteU32(value.sampleMask) && writer.WriteF32(value.minimumSampleShading);
    }

    [[nodiscard]] bool ReadMultisample(serialization::BinaryReader& reader, pipeline::MultisampleState& value) noexcept
    {
        u8 reserved = 0;
        return reader.ReadU8(value.sampleCount) && reader.ReadBool(value.alphaToCoverage) && reader.ReadBool(value.sampleShading) &&
               reader.ReadU8(reserved) && reserved == 0 && reader.ReadU32(value.sampleMask) && reader.ReadF32(value.minimumSampleShading);
    }

    [[nodiscard]] bool WriteStencilFace(serialization::BinaryWriter& writer, const pipeline::StencilFaceState& value) noexcept
    {
        return writer.WriteU8(static_cast<u8>(value.fail)) && writer.WriteU8(static_cast<u8>(value.depthFail)) &&
               writer.WriteU8(static_cast<u8>(value.pass)) && writer.WriteU8(static_cast<u8>(value.compare));
    }

    [[nodiscard]] bool ReadStencilFace(serialization::BinaryReader& reader, pipeline::StencilFaceState& value) noexcept
    {
        u8 fail = 0;
        u8 depthFail = 0;
        u8 pass = 0;
        u8 compare = 0;
        if (!reader.ReadU8(fail) || !reader.ReadU8(depthFail) || !reader.ReadU8(pass) || !reader.ReadU8(compare))
        {
            return false;
        }
        value.fail = static_cast<pipeline::StencilOperation>(fail);
        value.depthFail = static_cast<pipeline::StencilOperation>(depthFail);
        value.pass = static_cast<pipeline::StencilOperation>(pass);
        value.compare = static_cast<pipeline::CompareOperation>(compare);
        return true;
    }

    [[nodiscard]] bool WriteDepthStencil(serialization::BinaryWriter& writer, const pipeline::DepthStencilState& value) noexcept
    {
        return writer.WriteBool(value.depthTest) && writer.WriteBool(value.depthWrite) &&
               writer.WriteU8(static_cast<u8>(value.depthCompare)) && writer.WriteBool(value.depthBoundsTest) &&
               writer.WriteF32(value.minimumDepthBounds) && writer.WriteF32(value.maximumDepthBounds) &&
               writer.WriteBool(value.stencilTest) && writer.WriteU8(value.stencilReadMask) && writer.WriteU8(value.stencilWriteMask) &&
               writer.WriteU8(0) && WriteStencilFace(writer, value.front) && WriteStencilFace(writer, value.back);
    }

    [[nodiscard]] bool ReadDepthStencil(serialization::BinaryReader& reader, pipeline::DepthStencilState& value) noexcept
    {
        u8 depthCompare = 0;
        u8 reserved = 0;
        if (!reader.ReadBool(value.depthTest) || !reader.ReadBool(value.depthWrite) || !reader.ReadU8(depthCompare) ||
            !reader.ReadBool(value.depthBoundsTest) || !reader.ReadF32(value.minimumDepthBounds) ||
            !reader.ReadF32(value.maximumDepthBounds) || !reader.ReadBool(value.stencilTest) || !reader.ReadU8(value.stencilReadMask) ||
            !reader.ReadU8(value.stencilWriteMask) || !reader.ReadU8(reserved) || reserved != 0 || !ReadStencilFace(reader, value.front) ||
            !ReadStencilFace(reader, value.back))
        {
            return false;
        }
        value.depthCompare = static_cast<pipeline::CompareOperation>(depthCompare);
        return true;
    }

    [[nodiscard]] bool WriteBlendAttachment(serialization::BinaryWriter& writer, const pipeline::BlendAttachmentState& value) noexcept
    {
        return writer.WriteBool(value.blendEnable) && writer.WriteU8(static_cast<u8>(value.sourceColor)) &&
               writer.WriteU8(static_cast<u8>(value.destinationColor)) && writer.WriteU8(static_cast<u8>(value.colorOperation)) &&
               writer.WriteU8(static_cast<u8>(value.sourceAlpha)) && writer.WriteU8(static_cast<u8>(value.destinationAlpha)) &&
               writer.WriteU8(static_cast<u8>(value.alphaOperation)) && writer.WriteU8(value.writeMask);
    }

    [[nodiscard]] bool ReadBlendAttachment(serialization::BinaryReader& reader, pipeline::BlendAttachmentState& value) noexcept
    {
        u8 sourceColor = 0;
        u8 destinationColor = 0;
        u8 colorOperation = 0;
        u8 sourceAlpha = 0;
        u8 destinationAlpha = 0;
        u8 alphaOperation = 0;
        if (!reader.ReadBool(value.blendEnable) || !reader.ReadU8(sourceColor) || !reader.ReadU8(destinationColor) ||
            !reader.ReadU8(colorOperation) || !reader.ReadU8(sourceAlpha) || !reader.ReadU8(destinationAlpha) ||
            !reader.ReadU8(alphaOperation) || !reader.ReadU8(value.writeMask))
        {
            return false;
        }
        value.sourceColor = static_cast<pipeline::BlendFactor>(sourceColor);
        value.destinationColor = static_cast<pipeline::BlendFactor>(destinationColor);
        value.colorOperation = static_cast<pipeline::BlendOperation>(colorOperation);
        value.sourceAlpha = static_cast<pipeline::BlendFactor>(sourceAlpha);
        value.destinationAlpha = static_cast<pipeline::BlendFactor>(destinationAlpha);
        value.alphaOperation = static_cast<pipeline::BlendOperation>(alphaOperation);
        return true;
    }

    [[nodiscard]] bool WriteBlend(serialization::BinaryWriter& writer, const pipeline::BlendState& value) noexcept
    {
        if (!writer.WriteBool(value.independentBlend) || !writer.WriteBool(value.logicOperationEnable) ||
            !writer.WriteU8(static_cast<u8>(value.logicOperation)) || !writer.WriteU8(0) || !writer.WriteU32(value.attachmentCount))
        {
            return false;
        }
        for (const pipeline::BlendAttachmentState& attachment : value.attachments)
        {
            if (!WriteBlendAttachment(writer, attachment))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ReadBlend(serialization::BinaryReader& reader, pipeline::BlendState& value) noexcept
    {
        u8 logicOperation = 0;
        u8 reserved = 0;
        if (!reader.ReadBool(value.independentBlend) || !reader.ReadBool(value.logicOperationEnable) || !reader.ReadU8(logicOperation) ||
            !reader.ReadU8(reserved) || reserved != 0 || !reader.ReadU32(value.attachmentCount))
        {
            return false;
        }
        value.logicOperation = static_cast<pipeline::LogicOperation>(logicOperation);
        for (pipeline::BlendAttachmentState& attachment : value.attachments)
        {
            if (!ReadBlendAttachment(reader, attachment))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool WriteGraphics(serialization::BinaryWriter& writer, const pipeline::GraphicsState& value) noexcept
    {
        return writer.WriteU8(static_cast<u8>(value.topology)) && writer.WriteU8(value.patchControlPoints) &&
               writer.WriteBool(value.primitiveRestart) && writer.WriteU8(static_cast<u8>(value.attachmentPolicy)) &&
               WriteRasterizer(writer, value.rasterizer) && WriteMultisample(writer, value.multisample) &&
               WriteDepthStencil(writer, value.depthStencil) && WriteBlend(writer, value.blend) &&
               WriteAttachmentSignature(writer, value.exactAttachments);
    }

    [[nodiscard]] bool ReadGraphics(serialization::BinaryReader& reader, pipeline::GraphicsState& value) noexcept
    {
        u8 topology = 0;
        u8 attachmentPolicy = 0;
        if (!reader.ReadU8(topology) || !reader.ReadU8(value.patchControlPoints) || !reader.ReadBool(value.primitiveRestart) ||
            !reader.ReadU8(attachmentPolicy) || !ReadRasterizer(reader, value.rasterizer) || !ReadMultisample(reader, value.multisample) ||
            !ReadDepthStencil(reader, value.depthStencil) || !ReadBlend(reader, value.blend) ||
            !ReadAttachmentSignature(reader, value.exactAttachments))
        {
            return false;
        }
        value.topology = static_cast<pipeline::PrimitiveTopology>(topology);
        value.attachmentPolicy = static_cast<pipeline::AttachmentPolicy>(attachmentPolicy);
        return true;
    }

    [[nodiscard]] bool WriteVertexStream(serialization::BinaryWriter& writer, const pipeline::VertexStream& value) noexcept
    {
        return writer.WriteU32(value.binding) && writer.WriteU32(value.stride) && writer.WriteU8(static_cast<u8>(value.inputRate)) &&
               writer.WriteU8(0) && writer.WriteU16(0) && writer.WriteU32(value.instanceStepRate);
    }

    [[nodiscard]] bool ReadVertexStream(serialization::BinaryReader& reader, pipeline::VertexStream& value) noexcept
    {
        u8 inputRate = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU32(value.binding) || !reader.ReadU32(value.stride) || !reader.ReadU8(inputRate) || !reader.ReadU8(reserved8) ||
            !reader.ReadU16(reserved16) || !reader.ReadU32(value.instanceStepRate) || reserved8 != 0 || reserved16 != 0)
        {
            return false;
        }
        value.inputRate = static_cast<pipeline::InputRate>(inputRate);
        return true;
    }

    [[nodiscard]] bool WriteVertexAttribute(serialization::BinaryWriter& writer, const pipeline::VertexAttribute& value) noexcept
    {
        return writer.WriteU64(value.semantic) && writer.WriteU32(value.semanticIndex) && writer.WriteU32(value.location) &&
               writer.WriteU32(value.streamBinding) && writer.WriteU32(value.byteOffset) &&
               writer.WriteU8(static_cast<u8>(value.numericClass)) && writer.WriteU8(value.componentCount) &&
               writer.WriteU8(value.componentBits) && writer.WriteU8(0);
    }

    [[nodiscard]] bool ReadVertexAttribute(serialization::BinaryReader& reader, pipeline::VertexAttribute& value) noexcept
    {
        u8 numericClass = 0;
        u8 reserved = 0;
        if (!reader.ReadU64(value.semantic) || !reader.ReadU32(value.semanticIndex) || !reader.ReadU32(value.location) ||
            !reader.ReadU32(value.streamBinding) || !reader.ReadU32(value.byteOffset) || !reader.ReadU8(numericClass) ||
            !reader.ReadU8(value.componentCount) || !reader.ReadU8(value.componentBits) || !reader.ReadU8(reserved) || reserved != 0)
        {
            return false;
        }
        value.numericClass = static_cast<shaders::NumericClass>(numericClass);
        return true;
    }

    [[nodiscard]] bool WriteRayTracingGroup(serialization::BinaryWriter& writer, const pipeline::RayTracingGroup& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU8(static_cast<u8>(value.kind)) && writer.WriteU8(0) && writer.WriteU16(0) &&
               writer.WriteU32(value.shaderLibrary) && writer.WriteU64(value.generalEntry) && writer.WriteU64(value.closestHitEntry) &&
               writer.WriteU64(value.anyHitEntry) && writer.WriteU64(value.intersectionEntry);
    }

    [[nodiscard]] bool ReadRayTracingGroup(serialization::BinaryReader& reader, pipeline::RayTracingGroup& value) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU64(value.name) || !reader.ReadU8(kind) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) ||
            !reader.ReadU32(value.shaderLibrary) || !reader.ReadU64(value.generalEntry) || !reader.ReadU64(value.closestHitEntry) ||
            !reader.ReadU64(value.anyHitEntry) || !reader.ReadU64(value.intersectionEntry) || reserved8 != 0 || reserved16 != 0)
        {
            return false;
        }
        value.kind = static_cast<pipeline::RayTracingGroupKind>(kind);
        return true;
    }

    [[nodiscard]] pipeline::Result WriteCanonicalBody(serialization::BinaryWriter& writer, const CanonicalData& data) noexcept
    {
        if (!writer.WriteU8(static_cast<u8>(data.kind)) || !writer.WriteU8(0) || !writer.WriteU16(0) || !writer.WriteU64(data.name) ||
            !writer.WriteU64(static_cast<u64>(data.dynamicStates)) || !writer.WriteU32(data.shaders.Size()) ||
            !writer.WriteU32(data.vertexStreams.Size()) || !writer.WriteU32(data.vertexAttributes.Size()) ||
            !writer.WriteU32(data.rayTracingGroups.Size()))
        {
            return WriterResult(writer);
        }
        for (const pipeline::ShaderReference& shader : data.shaders)
        {
            if (!WriteShaderReference(writer, shader))
            {
                return WriterResult(writer);
            }
        }
        switch (data.kind)
        {
        case pipeline::PipelineKind::Graphics:
            if (!WriteGraphics(writer, data.graphics))
            {
                return WriterResult(writer);
            }
            for (const pipeline::VertexStream& stream : data.vertexStreams)
            {
                if (!WriteVertexStream(writer, stream))
                {
                    return WriterResult(writer);
                }
            }
            for (const pipeline::VertexAttribute& attribute : data.vertexAttributes)
            {
                if (!WriteVertexAttribute(writer, attribute))
                {
                    return WriterResult(writer);
                }
            }
            break;
        case pipeline::PipelineKind::Compute:
            break;
        case pipeline::PipelineKind::RayTracing:
            if (!writer.WriteU32(data.rayTracing.maximumRecursionDepth) || !writer.WriteU32(data.rayTracing.maximumPayloadBytes) ||
                !writer.WriteU32(data.rayTracing.maximumAttributeBytes))
            {
                return WriterResult(writer);
            }
            for (const pipeline::RayTracingGroup& group : data.rayTracingGroups)
            {
                if (!WriteRayTracingGroup(writer, group))
                {
                    return WriterResult(writer);
                }
            }
            break;
        }
        return pipeline::Result::Success;
    }

    [[nodiscard]] pipeline::Result BuildTemplateFingerprint(const CanonicalData& data, crypto::Digest256& fingerprint) noexcept
    {
        ByteArray bytes{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        const pipeline::Result result = WriteCanonicalBody(writer, data);
        if (result != pipeline::Result::Success)
        {
            return result;
        }
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return pipeline::Result::Success;
    }

    [[nodiscard]] pipeline::Result WriteDocument(filesystem::IFile& file, const CanonicalData& data,
                                                 const crypto::Digest256& fingerprint) noexcept
    {
        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter metadataFile(metadata);
        serialization::BinaryWriter metadataWriter(metadataFile);
        if (!metadataWriter.WriteU32(MetadataWireVersion) || !WriteDigest(metadataWriter, fingerprint))
        {
            return WriterResult(metadataWriter);
        }
        pipeline::Result result = WriteCanonicalBody(metadataWriter, data);
        if (result != pipeline::Result::Success)
        {
            return result;
        }

        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = pipeline::PipelineMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        const u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !writer.Align(16))
        {
            return WriterResult(writer);
        }

        serialization::SectionDescriptor section;
        section.id = MetadataSection;
        section.version = FileVersion;
        section.alignmentLog2 = 4;
        section.offset = writer.Position();
        section.storedSize = metadata.Size();
        section.logicalSize = metadata.Size();
        section.storedCrc64 = serialization::Crc64(metadata.Data(), metadata.Size());
        if (!writer.WriteBytes(metadata.Data(), metadata.Size()) || !writer.Align(16))
        {
            return WriterResult(writer);
        }
        header.sectionTableOffset = writer.Position();
        if (serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success)
        {
            return WriterResult(writer);
        }
        header.fileSize = writer.Position();
        if (!writer.Seek(0))
        {
            return WriterResult(writer);
        }
        const serialization::Result patchResult = serialization::WriteDocumentHeader(writer, header);
        if (patchResult != serialization::Result::Success || !writer.Seek(header.fileSize) || !writer.Flush())
        {
            return patchResult == serialization::Result::Success ? WriterResult(writer) : ConvertSerializationResult(patchResult);
        }
        return pipeline::Result::Success;
    }

    template <typename Type>
    [[nodiscard]] bool ResizeChecked(containers::DynamicArray<Type>& array, const u32 count, const u32 limit) noexcept
    {
        if (count > limit)
        {
            return false;
        }
        array.Resize(count);
        return array.Size() == count;
    }

    [[nodiscard]] pipeline::Result ReadCanonicalBody(serialization::BinaryReader& reader, const pipeline::ReadLimits& limits,
                                                     pipeline::BuildDescription& description,
                                                     containers::DynamicArray<pipeline::ShaderReference>& shaders,
                                                     containers::DynamicArray<pipeline::VertexStream>& streams,
                                                     containers::DynamicArray<pipeline::VertexAttribute>& attributes,
                                                     containers::DynamicArray<pipeline::RayTracingGroup>& groups) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        u64 dynamicStates = 0;
        u32 shaderCount = 0;
        u32 streamCount = 0;
        u32 attributeCount = 0;
        u32 groupCount = 0;
        if (!reader.ReadU8(kind) || !reader.ReadU8(reserved8) || !reader.ReadU16(reserved16) || !reader.ReadU64(description.name) ||
            !reader.ReadU64(dynamicStates) || !reader.ReadU32(shaderCount) || !reader.ReadU32(streamCount) ||
            !reader.ReadU32(attributeCount) || !reader.ReadU32(groupCount))
        {
            return ReaderResult(reader);
        }
        if (reserved8 != 0 || reserved16 != 0 || kind > static_cast<u8>(pipeline::PipelineKind::RayTracing))
        {
            return pipeline::Result::InvalidLayout;
        }
        if (!ResizeChecked(shaders, shaderCount, limits.maximumShaders) ||
            !ResizeChecked(streams, streamCount, limits.maximumVertexStreams) ||
            !ResizeChecked(attributes, attributeCount, limits.maximumVertexAttributes) ||
            !ResizeChecked(groups, groupCount, limits.maximumRayTracingGroups))
        {
            return pipeline::Result::LimitExceeded;
        }
        description.kind = static_cast<pipeline::PipelineKind>(kind);
        description.dynamicStates = static_cast<pipeline::DynamicState>(dynamicStates);
        for (pipeline::ShaderReference& shader : shaders)
        {
            if (!ReadShaderReference(reader, shader))
            {
                return ReaderResult(reader);
            }
        }
        switch (description.kind)
        {
        case pipeline::PipelineKind::Graphics:
            if (!ReadGraphics(reader, description.graphics))
            {
                return ReaderResult(reader);
            }
            for (pipeline::VertexStream& stream : streams)
            {
                if (!ReadVertexStream(reader, stream))
                {
                    return ReaderResult(reader);
                }
            }
            for (pipeline::VertexAttribute& attribute : attributes)
            {
                if (!ReadVertexAttribute(reader, attribute))
                {
                    return ReaderResult(reader);
                }
            }
            break;
        case pipeline::PipelineKind::Compute:
            break;
        case pipeline::PipelineKind::RayTracing:
            if (!reader.ReadU32(description.rayTracing.maximumRecursionDepth) ||
                !reader.ReadU32(description.rayTracing.maximumPayloadBytes) ||
                !reader.ReadU32(description.rayTracing.maximumAttributeBytes))
            {
                return ReaderResult(reader);
            }
            for (pipeline::RayTracingGroup& group : groups)
            {
                if (!ReadRayTracingGroup(reader, group))
                {
                    return ReaderResult(reader);
                }
            }
            break;
        }
        description.shaders = shaders;
        description.vertexStreams = streams;
        description.vertexAttributes = attributes;
        description.rayTracingGroups = groups;
        return pipeline::Result::Success;
    }

    [[nodiscard]] bool AttachmentSignaturesEqual(const pipeline::AttachmentSignature& left,
                                                 const pipeline::AttachmentSignature& right) noexcept
    {
        if (left.colorCount != right.colorCount || left.depthStencilFormat != right.depthStencilFormat ||
            left.depthStencilClass != right.depthStencilClass || left.sampleCount != right.sampleCount)
        {
            return false;
        }
        for (u32 index = 0; index < pipeline::MaximumColorAttachments; ++index)
        {
            if (left.colors[index].format != right.colors[index].format ||
                left.colors[index].numericClass != right.colors[index].numericClass)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

namespace vanguard::pipelines
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::InvalidMagic:
            return "InvalidMagic";
        case Result::UnsupportedVersion:
            return "UnsupportedVersion";
        case Result::InvalidLayout:
            return "InvalidLayout";
        case Result::IntegrityFailure:
            return "IntegrityFailure";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::DuplicateShader:
            return "DuplicateShader";
        case Result::DuplicateVertexStream:
            return "DuplicateVertexStream";
        case Result::DuplicateVertexAttribute:
            return "DuplicateVertexAttribute";
        case Result::DuplicateRayTracingGroup:
            return "DuplicateRayTracingGroup";
        case Result::ShaderMismatch:
            return "ShaderMismatch";
        case Result::AttachmentMismatch:
            return "AttachmentMismatch";
        case Result::IncompatiblePipeline:
            return "IncompatiblePipeline";
        case Result::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    PipelineFile::PipelineFile() noexcept
        : m_shaders(memory::pools::Rendering::GetInstance()), m_vertexStreams(memory::pools::Rendering::GetInstance()),
          m_vertexAttributes(memory::pools::Rendering::GetInstance()), m_rayTracingGroups(memory::pools::Rendering::GetInstance())
    {
    }

    Result PipelineFile::Open(filesystem::IFile& file, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader reader(file);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 1;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(reader, PipelineMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(headerResult);
        }
        containers::DynamicArray<serialization::SectionDescriptor> sections{memory::pools::Serialization::GetInstance()};
        const serialization::Result sectionResult = serialization::ReadSectionTable(reader, header, documentLimits, sections);
        if (sectionResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(sectionResult);
        }
        if (sections.Size() != 1 || sections[0].id != MetadataSection || sections[0].version != FileVersion ||
            sections[0].codec != serialization::Codec::None || sections[0].storedSize != sections[0].logicalSize ||
            sections[0].storedSize > ~u32{0})
        {
            return Result::InvalidLayout;
        }

        ByteArray metadata{memory::pools::Serialization::GetInstance()};
        metadata.Resize(static_cast<u32>(sections[0].storedSize));
        if (!reader.Seek(sections[0].offset) || !reader.ReadBytes(metadata.Data(), metadata.Size()))
        {
            return ReaderResult(reader);
        }
        if (serialization::Crc64(metadata.Data(), metadata.Size()) != sections[0].storedCrc64)
        {
            return Result::IntegrityFailure;
        }

        filesystem::MemoryFileReader metadataFile(metadata, 0);
        serialization::BinaryReader metadataReader(metadataFile);
        u32 metadataVersion = 0;
        if (!metadataReader.ReadU32(metadataVersion) || !ReadDigest(metadataReader, m_templateFingerprint))
        {
            return ReaderResult(metadataReader);
        }
        if (metadataVersion != MetadataWireVersion)
        {
            return Result::UnsupportedVersion;
        }

        BuildDescription description;
        Result result =
            ReadCanonicalBody(metadataReader, limits, description, m_shaders, m_vertexStreams, m_vertexAttributes, m_rayTracingGroups);
        if (result != Result::Success)
        {
            Close();
            return result;
        }
        if (metadataReader.Position() != metadataReader.Size())
        {
            Close();
            return Result::InvalidLayout;
        }

        CanonicalData canonical;
        result = Canonicalize(description, canonical);
        crypto::Digest256 actualFingerprint;
        if (result == Result::Success)
        {
            result = BuildTemplateFingerprint(canonical, actualFingerprint);
        }
        if (result != Result::Success || actualFingerprint != m_templateFingerprint)
        {
            Close();
            return result == Result::Success ? Result::IntegrityFailure : result;
        }

        m_kind = canonical.kind;
        m_name = canonical.name;
        m_dynamicStates = canonical.dynamicStates;
        m_graphics = canonical.graphics;
        m_rayTracing = canonical.rayTracing;
        m_shaders = std::move(canonical.shaders);
        m_vertexStreams = std::move(canonical.vertexStreams);
        m_vertexAttributes = std::move(canonical.vertexAttributes);
        m_rayTracingGroups = std::move(canonical.rayTracingGroups);
        m_open = true;
        return Result::Success;
    }

    void PipelineFile::Close() noexcept
    {
        m_kind = PipelineKind::Graphics;
        m_name = 0;
        m_dynamicStates = DynamicState::None;
        m_templateFingerprint = {};
        m_shaders.Clear();
        m_graphics = {};
        m_vertexStreams.Clear();
        m_vertexAttributes.Clear();
        m_rayTracing = {};
        m_rayTracingGroups.Clear();
        m_open = false;
    }

    bool PipelineFile::IsOpen() const noexcept
    {
        return m_open;
    }

    PipelineKind PipelineFile::Kind() const noexcept
    {
        return m_kind;
    }

    u64 PipelineFile::Name() const noexcept
    {
        return m_name;
    }

    DynamicState PipelineFile::DynamicStates() const noexcept
    {
        return m_dynamicStates;
    }

    const crypto::Digest256& PipelineFile::TemplateFingerprint() const noexcept
    {
        return m_templateFingerprint;
    }

    containers::ArraySpan<const ShaderReference> PipelineFile::Shaders() const noexcept
    {
        return m_shaders;
    }

    const GraphicsState& PipelineFile::Graphics() const noexcept
    {
        return m_graphics;
    }

    containers::ArraySpan<const VertexStream> PipelineFile::VertexStreams() const noexcept
    {
        return m_vertexStreams;
    }

    containers::ArraySpan<const VertexAttribute> PipelineFile::VertexAttributes() const noexcept
    {
        return m_vertexAttributes;
    }

    const RayTracingState& PipelineFile::RayTracing() const noexcept
    {
        return m_rayTracing;
    }

    containers::ArraySpan<const RayTracingGroup> PipelineFile::RayTracingGroups() const noexcept
    {
        return m_rayTracingGroups;
    }

    Result WritePipeline(filesystem::IFile& writer, const BuildDescription& description) noexcept
    {
        CanonicalData canonical;
        Result result = Canonicalize(description, canonical);
        if (result != Result::Success)
        {
            return result;
        }
        crypto::Digest256 fingerprint;
        result = BuildTemplateFingerprint(canonical, fingerprint);
        return result == Result::Success ? WriteDocument(writer, canonical, fingerprint) : result;
    }

    Result CalculateTemplateFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept
    {
        CanonicalData canonical;
        const Result result = Canonicalize(description, canonical);
        return result == Result::Success ? BuildTemplateFingerprint(canonical, fingerprint) : result;
    }

    Result CalculateConcretePipelineKey(const PipelineFile& pipeline, const AttachmentSignature* const attachments,
                                        crypto::Digest256& key) noexcept
    {
        if (!pipeline.IsOpen())
        {
            return Result::InvalidState;
        }
        ByteArray bytes{memory::pools::Serialization::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!WriteDigest(writer, pipeline.TemplateFingerprint()))
        {
            return WriterResult(writer);
        }
        if (pipeline.Kind() == PipelineKind::Graphics)
        {
            const AttachmentSignature* selected = attachments;
            if (pipeline.Graphics().attachmentPolicy == AttachmentPolicy::Exact)
            {
                if (attachments != nullptr && !AttachmentSignaturesEqual(*attachments, pipeline.Graphics().exactAttachments))
                {
                    return Result::AttachmentMismatch;
                }
                selected = &pipeline.Graphics().exactAttachments;
            }
            if (selected == nullptr)
            {
                return Result::InvalidArgument;
            }
            const Result validation = ValidateAttachmentSignature(*selected);
            if (validation != Result::Success)
            {
                return validation;
            }
            if (selected->colorCount != pipeline.Graphics().blend.attachmentCount ||
                selected->sampleCount != pipeline.Graphics().multisample.sampleCount)
            {
                return Result::AttachmentMismatch;
            }
            if (!WriteAttachmentSignature(writer, *selected))
            {
                return WriterResult(writer);
            }
        }
        key = crypto::Sha256(bytes.Data(), bytes.Size());
        return Result::Success;
    }

    Result ValidateShaderCompatibility(const PipelineFile& pipeline, const shaders::ShaderFile& shader,
                                       const AttachmentSignature* const attachments) noexcept
    {
        if (!pipeline.IsOpen() || !shader.IsOpen())
        {
            return Result::InvalidState;
        }
        const ShaderReference* reference = nullptr;
        for (const ShaderReference& candidate : pipeline.Shaders())
        {
            if (candidate.permutation == shader.Permutation())
            {
                reference = &candidate;
                break;
            }
        }
        if (reference == nullptr || reference->bindingLayout != shader.BindingLayoutFingerprint() ||
            reference->pipelineInterface != shader.PipelineInterfaceFingerprint())
        {
            return Result::ShaderMismatch;
        }

        shaders::PipelineCompatibility compatibility;
        compatibility.bindingLayoutFingerprint = reference->bindingLayout;
        compatibility.pipelineInterfaceFingerprint = reference->pipelineInterface;
        containers::DynamicArray<shaders::VertexInput> vertexInputs{memory::pools::Rendering::GetInstance()};
        switch (pipeline.Kind())
        {
        case PipelineKind::Graphics:
        {
            if (shader.Kind() != shaders::ProgramKind::Graphics)
            {
                return Result::ShaderMismatch;
            }
            const AttachmentSignature* selected = attachments;
            if (pipeline.Graphics().attachmentPolicy == AttachmentPolicy::Exact)
            {
                if (attachments != nullptr && !AttachmentSignaturesEqual(*attachments, pipeline.Graphics().exactAttachments))
                {
                    return Result::AttachmentMismatch;
                }
                selected = &pipeline.Graphics().exactAttachments;
            }
            if (selected == nullptr)
            {
                return Result::InvalidArgument;
            }
            const Result attachmentResult = ValidateAttachmentSignature(*selected);
            if (attachmentResult != Result::Success)
            {
                return attachmentResult;
            }

            vertexInputs.Reserve(pipeline.VertexAttributes().Size());
            for (const VertexAttribute& attribute : pipeline.VertexAttributes())
            {
                vertexInputs.PushBack({attribute.semantic, attribute.semanticIndex, attribute.location, attribute.numericClass,
                                       attribute.componentCount, attribute.componentBits});
            }
            compatibility.kind = shaders::PipelineKind::Graphics;
            compatibility.primitiveClass = PrimitiveClassOf(pipeline.Graphics().topology);
            compatibility.renderTargetCount = selected->colorCount;
            compatibility.sampleCount = selected->sampleCount;
            compatibility.depthStencilFormatPresent = selected->depthStencilFormat != InvalidFormat;
            compatibility.dualSourceBlendEnabled = false;
            for (u32 index = 0; index < selected->colorCount; ++index)
            {
                compatibility.renderTargetClasses[index] = selected->colors[index].numericClass;
                const BlendAttachmentState& blend = pipeline.Graphics().blend.attachments[index];
                compatibility.dualSourceBlendEnabled |=
                    blend.sourceColor >= BlendFactor::SourceOneColor || blend.destinationColor >= BlendFactor::SourceOneColor ||
                    blend.sourceAlpha >= BlendFactor::SourceOneColor || blend.destinationAlpha >= BlendFactor::SourceOneColor;
            }
            compatibility.vertexLayout = {vertexInputs.TypedData(), vertexInputs.Size()};
            break;
        }
        case PipelineKind::Compute:
            if (shader.Kind() != shaders::ProgramKind::Compute)
            {
                return Result::ShaderMismatch;
            }
            compatibility.kind = shaders::PipelineKind::Compute;
            break;
        case PipelineKind::RayTracing:
            if (shader.Kind() != shaders::ProgramKind::Library)
            {
                return Result::ShaderMismatch;
            }
            compatibility.kind = shaders::PipelineKind::RayTracing;
            break;
        }
        return shaders::ValidatePipeline(shader, compatibility) == shaders::Result::Success ? Result::Success
                                                                                            : Result::IncompatiblePipeline;
    }
} // namespace vanguard::pipelines
