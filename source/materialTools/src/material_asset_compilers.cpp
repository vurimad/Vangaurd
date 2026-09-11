#include <vanguard/material_tools/material_asset_compilers.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>

#include <cstring>

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;
    namespace serialization = vanguard::serialization;

    constexpr u32 ProgramMagic = serialization::MakeFourCC('M', 'P', 'G', 'I');
    constexpr u32 PipelineMagic = serialization::MakeFourCC('M', 'P', 'L', 'I');
    constexpr u32 ValueMagic = serialization::MakeFourCC('M', 'V', 'L', 'I');
    constexpr u16 ProgramInputVersion = 3;
    constexpr u16 PipelineInputVersion = 1;
    constexpr u16 ValueInputVersion = 3;
    constexpr char ProgramCompilerName[] = "vanguard.material.program";
    constexpr char PipelineCompilerName[] = "vanguard.pipeline.compiler";
    constexpr char MaterialCompilerName[] = "vanguard.material.values";
    constexpr char ToolPath[] = "tools/vanguard-material-compiler";

    [[nodiscard]] u32 TextLength(const char* value, const u32 limit) noexcept
    {
        if (value == nullptr)
            return limit;
        u32 length = 0;
        while (length < limit && value[length] != '\0')
            ++length;
        return length;
    }

    [[nodiscard]] u32 SourceLineCount(const containers::ArraySpan<const u8> source) noexcept
    {
        u32 lines = source.Empty() ? 0u : 1u;
        for (const u8 value : source)
            if (value == '\n' && lines != 0xffffffffu)
                ++lines;
        return lines;
    }

    [[nodiscard]] bool WriteReference(serialization::BinaryWriter& writer, const resources::ResourceReference value) noexcept
    {
        return value.IsValid() && value.IsTyped() && writer.WriteU64(value.GetPath().Id()) && writer.WriteU32(value.ExpectedType());
    }

    [[nodiscard]] bool ReadReference(serialization::BinaryReader& reader, resources::ResourceReference& value) noexcept
    {
        u64 id = 0;
        u32 type = 0;
        if (!reader.ReadU64(id) || !reader.ReadU32(type) || id == resources::InvalidResourceId || type == resources::InvalidResourceTypeId)
            return false;
        value = resources::ResourceReference(resources::ResourcePath::FromId(id), type);
        return true;
    }

    [[nodiscard]] bool WriteMaterialDomain(serialization::BinaryWriter& writer, const shaders::MaterialDomainContract& value) noexcept
    {
        return writer.WriteU64(value.name) && writer.WriteU32(value.schemaVersion) && writer.WriteU32(value.legalStages) &&
               writer.WriteU32(value.requiredCapabilities) &&
               writer.WriteBytes(value.inputType.bytes, crypto::Digest256::ByteCount) &&
               writer.WriteBytes(value.outputType.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool ReadMaterialDomain(serialization::BinaryReader& reader, shaders::MaterialDomainContract& value) noexcept
    {
        return reader.ReadU64(value.name) && reader.ReadU32(value.schemaVersion) && reader.ReadU32(value.legalStages) &&
               reader.ReadU32(value.requiredCapabilities) &&
               reader.ReadBytes(value.inputType.bytes, crypto::Digest256::ByteCount) &&
               reader.ReadBytes(value.outputType.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool TargetMatches(const assets::TargetPlatform platform, const shader_tools::Target target) noexcept
    {
        return (platform == assets::TargetPlatform::WindowsD3D12 && target == shader_tools::Target::D3D12Dxil) ||
               ((platform == assets::TargetPlatform::WindowsVulkan || platform == assets::TargetPlatform::LinuxVulkan) &&
                target == shader_tools::Target::VulkanSpirV);
    }

    [[nodiscard]] bool WriteString(serialization::BinaryWriter& writer, const char* value, const u32 maximum) noexcept
    {
        const u32 length = TextLength(value, maximum);
        return length != 0 && length < maximum && writer.WriteU32(length) && writer.WriteBytes(value, length);
    }

    template <usize Capacity> [[nodiscard]] bool ReadString(serialization::BinaryReader& reader, char (&value)[Capacity]) noexcept
    {
        u32 length = 0;
        if (!reader.ReadU32(length) || length == 0 || length >= Capacity || !reader.ReadBytes(value, length))
            return false;
        value[length] = '\0';
        return true;
    }

    [[nodiscard]] bool WriteAttachment(serialization::BinaryWriter& writer, const pipelines::AttachmentSignature& value) noexcept
    {
        if (value.colorCount > pipelines::MaximumColorAttachments || !writer.WriteU32(value.colorCount))
            return false;
        for (u32 index = 0; index < value.colorCount; ++index)
            if (!writer.WriteU16(static_cast<u16>(value.colors[index].format)) ||
                !writer.WriteU8(static_cast<u8>(value.colors[index].numericClass)))
                return false;
        return writer.WriteU16(static_cast<u16>(value.depthStencilFormat)) && writer.WriteU8(static_cast<u8>(value.depthStencilClass)) &&
               writer.WriteU8(value.sampleCount);
    }

    [[nodiscard]] bool ReadAttachment(serialization::BinaryReader& reader, pipelines::AttachmentSignature& value) noexcept
    {
        u16 format = 0;
        u8 numeric = 0;
        if (!reader.ReadU32(value.colorCount) || value.colorCount > pipelines::MaximumColorAttachments)
            return false;
        for (u32 index = 0; index < value.colorCount; ++index)
        {
            if (!reader.ReadU16(format) || !reader.ReadU8(numeric))
                return false;
            value.colors[index].format = static_cast<pipelines::Format>(format);
            value.colors[index].numericClass = static_cast<shaders::NumericClass>(numeric);
        }
        u8 depthClass = 0;
        if (!reader.ReadU16(format) || !reader.ReadU8(depthClass) || !reader.ReadU8(value.sampleCount))
            return false;
        value.depthStencilFormat = static_cast<pipelines::Format>(format);
        value.depthStencilClass = static_cast<pipelines::DepthStencilClass>(depthClass);
        return true;
    }

    [[nodiscard]] bool WriteGraphics(serialization::BinaryWriter& writer, const pipelines::GraphicsState& value) noexcept
    {
        if (!writer.WriteU8(static_cast<u8>(value.topology)) || !writer.WriteU8(value.patchControlPoints) ||
            !writer.WriteBool(value.primitiveRestart) || !writer.WriteU8(static_cast<u8>(value.rasterizer.fill)) ||
            !writer.WriteU8(static_cast<u8>(value.rasterizer.cull)) || !writer.WriteU8(static_cast<u8>(value.rasterizer.frontFace)) ||
            !writer.WriteBool(value.rasterizer.depthClipEnable) || !writer.WriteBool(value.rasterizer.conservativeRasterization) ||
            !writer.WriteBool(value.rasterizer.rasterizerDiscard) || !writer.WriteI32(value.rasterizer.depthBias) ||
            !writer.WriteF32(value.rasterizer.depthBiasClamp) || !writer.WriteF32(value.rasterizer.slopeScaledDepthBias) ||
            !writer.WriteU8(value.multisample.sampleCount) || !writer.WriteU32(value.multisample.sampleMask) ||
            !writer.WriteBool(value.multisample.alphaToCoverage) || !writer.WriteBool(value.multisample.sampleShading) ||
            !writer.WriteF32(value.multisample.minimumSampleShading) || !writer.WriteBool(value.depthStencil.depthTest) ||
            !writer.WriteBool(value.depthStencil.depthWrite) || !writer.WriteU8(static_cast<u8>(value.depthStencil.depthCompare)) ||
            !writer.WriteBool(value.depthStencil.depthBoundsTest) || !writer.WriteF32(value.depthStencil.minimumDepthBounds) ||
            !writer.WriteF32(value.depthStencil.maximumDepthBounds) || !writer.WriteBool(value.depthStencil.stencilTest) ||
            !writer.WriteU8(value.depthStencil.stencilReadMask) || !writer.WriteU8(value.depthStencil.stencilWriteMask))
            return false;
        const pipelines::StencilFaceState faces[]{value.depthStencil.front, value.depthStencil.back};
        for (const pipelines::StencilFaceState& face : faces)
            if (!writer.WriteU8(static_cast<u8>(face.fail)) || !writer.WriteU8(static_cast<u8>(face.depthFail)) ||
                !writer.WriteU8(static_cast<u8>(face.pass)) || !writer.WriteU8(static_cast<u8>(face.compare)))
                return false;
        if (value.blend.attachmentCount > pipelines::MaximumColorAttachments || !writer.WriteBool(value.blend.independentBlend) ||
            !writer.WriteBool(value.blend.logicOperationEnable) || !writer.WriteU8(static_cast<u8>(value.blend.logicOperation)) ||
            !writer.WriteU32(value.blend.attachmentCount))
            return false;
        for (u32 index = 0; index < value.blend.attachmentCount; ++index)
        {
            const pipelines::BlendAttachmentState& blend = value.blend.attachments[index];
            if (!writer.WriteBool(blend.blendEnable) || !writer.WriteU8(static_cast<u8>(blend.sourceColor)) ||
                !writer.WriteU8(static_cast<u8>(blend.destinationColor)) || !writer.WriteU8(static_cast<u8>(blend.colorOperation)) ||
                !writer.WriteU8(static_cast<u8>(blend.sourceAlpha)) || !writer.WriteU8(static_cast<u8>(blend.destinationAlpha)) ||
                !writer.WriteU8(static_cast<u8>(blend.alphaOperation)) || !writer.WriteU8(blend.writeMask))
                return false;
        }
        return writer.WriteU8(static_cast<u8>(value.attachmentPolicy)) && WriteAttachment(writer, value.exactAttachments);
    }

    [[nodiscard]] bool ReadGraphics(serialization::BinaryReader& reader, pipelines::GraphicsState& value) noexcept
    {
        u8 temporary = 0;
        if (!reader.ReadU8(temporary)) return false;
        value.topology = static_cast<pipelines::PrimitiveTopology>(temporary);
        if (!reader.ReadU8(value.patchControlPoints) || !reader.ReadBool(value.primitiveRestart) || !reader.ReadU8(temporary)) return false;
        value.rasterizer.fill = static_cast<pipelines::FillMode>(temporary);
        if (!reader.ReadU8(temporary)) return false;
        value.rasterizer.cull = static_cast<pipelines::CullMode>(temporary);
        if (!reader.ReadU8(temporary)) return false;
        value.rasterizer.frontFace = static_cast<pipelines::FrontFace>(temporary);
        if (!reader.ReadBool(value.rasterizer.depthClipEnable) || !reader.ReadBool(value.rasterizer.conservativeRasterization) ||
            !reader.ReadBool(value.rasterizer.rasterizerDiscard) || !reader.ReadI32(value.rasterizer.depthBias) ||
            !reader.ReadF32(value.rasterizer.depthBiasClamp) || !reader.ReadF32(value.rasterizer.slopeScaledDepthBias) ||
            !reader.ReadU8(value.multisample.sampleCount) || !reader.ReadU32(value.multisample.sampleMask) ||
            !reader.ReadBool(value.multisample.alphaToCoverage) || !reader.ReadBool(value.multisample.sampleShading) ||
            !reader.ReadF32(value.multisample.minimumSampleShading) || !reader.ReadBool(value.depthStencil.depthTest) ||
            !reader.ReadBool(value.depthStencil.depthWrite) || !reader.ReadU8(temporary))
            return false;
        value.depthStencil.depthCompare = static_cast<pipelines::CompareOperation>(temporary);
        if (!reader.ReadBool(value.depthStencil.depthBoundsTest) || !reader.ReadF32(value.depthStencil.minimumDepthBounds) ||
            !reader.ReadF32(value.depthStencil.maximumDepthBounds) || !reader.ReadBool(value.depthStencil.stencilTest) ||
            !reader.ReadU8(value.depthStencil.stencilReadMask) || !reader.ReadU8(value.depthStencil.stencilWriteMask))
            return false;
        pipelines::StencilFaceState* faces[]{&value.depthStencil.front, &value.depthStencil.back};
        for (pipelines::StencilFaceState* face : faces)
        {
            if (!reader.ReadU8(temporary)) return false; face->fail = static_cast<pipelines::StencilOperation>(temporary);
            if (!reader.ReadU8(temporary)) return false; face->depthFail = static_cast<pipelines::StencilOperation>(temporary);
            if (!reader.ReadU8(temporary)) return false; face->pass = static_cast<pipelines::StencilOperation>(temporary);
            if (!reader.ReadU8(temporary)) return false; face->compare = static_cast<pipelines::CompareOperation>(temporary);
        }
        if (!reader.ReadBool(value.blend.independentBlend) || !reader.ReadBool(value.blend.logicOperationEnable) || !reader.ReadU8(temporary))
            return false;
        value.blend.logicOperation = static_cast<pipelines::LogicOperation>(temporary);
        if (!reader.ReadU32(value.blend.attachmentCount) || value.blend.attachmentCount > pipelines::MaximumColorAttachments)
            return false;
        for (u32 index = 0; index < value.blend.attachmentCount; ++index)
        {
            pipelines::BlendAttachmentState& blend = value.blend.attachments[index];
            if (!reader.ReadBool(blend.blendEnable) || !reader.ReadU8(temporary)) return false;
            blend.sourceColor = static_cast<pipelines::BlendFactor>(temporary);
            if (!reader.ReadU8(temporary)) return false; blend.destinationColor = static_cast<pipelines::BlendFactor>(temporary);
            if (!reader.ReadU8(temporary)) return false; blend.colorOperation = static_cast<pipelines::BlendOperation>(temporary);
            if (!reader.ReadU8(temporary)) return false; blend.sourceAlpha = static_cast<pipelines::BlendFactor>(temporary);
            if (!reader.ReadU8(temporary)) return false; blend.destinationAlpha = static_cast<pipelines::BlendFactor>(temporary);
            if (!reader.ReadU8(temporary)) return false; blend.alphaOperation = static_cast<pipelines::BlendOperation>(temporary);
            if (!reader.ReadU8(blend.writeMask)) return false;
        }
        if (!reader.ReadU8(temporary)) return false;
        value.attachmentPolicy = static_cast<pipelines::AttachmentPolicy>(temporary);
        return ReadAttachment(reader, value.exactAttachments);
    }

    struct ParsedProgram
    {
        ParsedProgram() noexcept
            : source(memory::pools::Tools::GetInstance()), entries(memory::pools::Tools::GetInstance()),
              entryStorage(memory::pools::Tools::GetInstance()), defines(memory::pools::Tools::GetInstance()),
              defineStorage(memory::pools::Tools::GetInstance()), sourceRanges(memory::pools::Tools::GetInstance())
        {
        }
        struct EntryStorage { char name[shaders::MaximumEntryPointLength]{}; };
        struct DefineStorage { char name[256]{}; char value[1024]{}; };
        char sourceName[shader_tools::MaximumSourceNameLength]{};
        char moduleName[shader_tools::MaximumModuleNameLength]{};
        char profile[64]{};
        u64 program = 0;
        crypto::Digest256 permutation;
        shaders::MaterialDomainContract expectedDomain;
        crypto::Digest256 expectedDomainFingerprint;
        shaders::MaterialShaderCapabilityMask requiredCapabilities = 0;
        containers::DynamicArray<u8> source;
        containers::DynamicArray<shader_tools::EntryPoint> entries;
        containers::DynamicArray<EntryStorage> entryStorage;
        containers::DynamicArray<shader_tools::Define> defines;
        containers::DynamicArray<DefineStorage> defineStorage;
        containers::DynamicArray<mt::MaterialGeneratedSourceRange> sourceRanges;
        shader_tools::CompileSettings settings;
    };

    [[nodiscard]] bool ParseProgram(const containers::ArraySpan<const u8> bytes, const mt::MaterialArtifactCompilerConfig& limits,
                                    ParsedProgram& output) noexcept
    {
        if (bytes.Empty() || bytes.Count() > limits.maximumProgramInputBytes)
            return false;
        filesystem::MemoryFileReaderExternalBuffer file(bytes.Data(), bytes.Count(), nullptr);
        serialization::BinaryReader reader(file);
        u32 magic = 0;
        u16 version = 0;
        u8 target = 0, optimization = 0, debug = 0;
        u32 sourceBytes = 0, entryCount = 0, defineCount = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(version) || magic != ProgramMagic || version != ProgramInputVersion ||
            !ReadString(reader, output.sourceName) || !ReadString(reader, output.moduleName) || !reader.ReadU64(output.program) ||
            !reader.ReadBytes(output.permutation.bytes, crypto::Digest256::ByteCount) ||
            !ReadMaterialDomain(reader, output.expectedDomain) ||
            !reader.ReadBytes(output.expectedDomainFingerprint.bytes, crypto::Digest256::ByteCount) ||
            !reader.ReadU32(output.requiredCapabilities) ||
            !reader.ReadU8(target) || !reader.ReadU8(optimization) || !reader.ReadU8(debug) || !reader.ReadBool(output.settings.warningsAsErrors) ||
            !reader.ReadBool(output.settings.preciseFloatingPoint) || !ReadString(reader, output.profile) || !reader.ReadU32(sourceBytes) ||
            sourceBytes == 0 || sourceBytes > reader.GetRemaining())
            return false;
        crypto::Digest256 calculatedDomainFingerprint;
        if (shaders::CalculateMaterialDomainFingerprint(output.expectedDomain, calculatedDomainFingerprint) != shaders::Result::Success ||
            calculatedDomainFingerprint != output.expectedDomainFingerprint ||
            !shaders::IsValidMaterialShaderCapabilityMask(output.requiredCapabilities) ||
            (output.expectedDomain.requiredCapabilities & ~output.requiredCapabilities) != 0 ||
            target > static_cast<u8>(shader_tools::Target::VulkanSpirV) ||
            optimization > static_cast<u8>(shader_tools::Optimization::Maximum) ||
            debug > static_cast<u8>(shader_tools::DebugInformation::Maximum))
            return false;
        output.settings.target = static_cast<shader_tools::Target>(target);
        output.settings.optimization = static_cast<shader_tools::Optimization>(optimization);
        output.settings.debugInformation = static_cast<shader_tools::DebugInformation>(debug);
        output.settings.profile = output.profile;
        output.source.Resize(sourceBytes);
        if (output.source.Size() != sourceBytes || !reader.ReadBytes(output.source.Data(), sourceBytes) || !reader.ReadU32(entryCount) ||
            entryCount == 0 || entryCount > limits.maximumEntryPoints)
            return false;
        output.entryStorage.Resize(entryCount);
        output.entries.Resize(entryCount);
        if (output.entries.Size() != entryCount || output.entryStorage.Size() != entryCount)
            return false;
        for (u32 index = 0; index < entryCount; ++index)
        {
            u8 stage = 0;
            if (!reader.ReadU8(stage) || stage >= static_cast<u8>(shaders::ShaderStage::Count) ||
                !ReadString(reader, output.entryStorage[index].name))
                return false;
            output.entries[index] = {output.entryStorage[index].name, static_cast<shaders::ShaderStage>(stage)};
        }
        if (!reader.ReadU32(defineCount) || defineCount > limits.maximumDefines)
            return false;
        output.defineStorage.Resize(defineCount);
        output.defines.Resize(defineCount);
        for (u32 index = 0; index < defineCount; ++index)
        {
            if (!ReadString(reader, output.defineStorage[index].name) || !ReadString(reader, output.defineStorage[index].value))
                return false;
            output.defines[index] = {output.defineStorage[index].name, output.defineStorage[index].value};
        }
        {
            u32 rangeCount = 0;
            if (!reader.ReadU32(rangeCount) || rangeCount > limits.maximumSourceRanges)
                return false;
            output.sourceRanges.Resize(rangeCount);
            if (output.sourceRanges.Size() != rangeCount)
                return false;
            const u32 lineCount = SourceLineCount(output.source);
            u32 previousLastLine = 0;
            for (u32 index = 0; index < rangeCount; ++index)
            {
                mt::MaterialGeneratedSourceRange& range = output.sourceRanges[index];
                if (!reader.ReadU32(range.value) || !reader.ReadU32(range.firstLine) || !reader.ReadU32(range.lastLine) ||
                    !reader.ReadU64(range.sourceNode) || !reader.ReadU32(range.sourcePin) || range.value != index || range.firstLine == 0 ||
                    range.lastLine < range.firstLine || range.lastLine > lineCount || range.firstLine <= previousLastLine)
                    return false;
                previousLastLine = range.lastLine;
            }
        }
        return reader.GetRemaining() == 0;
    }

    struct ParsedPipeline
    {
        ParsedPipeline() noexcept
            : streams(memory::pools::Tools::GetInstance()), attributes(memory::pools::Tools::GetInstance()), groups(memory::pools::Tools::GetInstance())
        {
        }
        resources::ResourceReference shader;
        pipelines::BuildDescription recipe;
        containers::DynamicArray<pipelines::VertexStream> streams;
        containers::DynamicArray<pipelines::VertexAttribute> attributes;
        containers::DynamicArray<pipelines::RayTracingGroup> groups;
    };

    [[nodiscard]] bool ParsePipeline(const containers::ArraySpan<const u8> bytes, ParsedPipeline& output) noexcept
    {
        filesystem::MemoryFileReaderExternalBuffer file(bytes.Data(), bytes.Count(), nullptr);
        serialization::BinaryReader reader(file);
        u32 magic = 0;
        u16 version = 0;
        u8 kind = 0;
        u32 count = 0;
        u64 dynamicStates = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(version) || magic != PipelineMagic || version != PipelineInputVersion || !ReadReference(reader, output.shader) ||
            !reader.ReadU8(kind) || !reader.ReadU64(output.recipe.name) || !reader.ReadU64(dynamicStates) ||
            !ReadGraphics(reader, output.recipe.graphics) || !reader.ReadU32(count) || count > 64)
            return false;
        output.recipe.kind = static_cast<pipelines::PipelineKind>(kind);
        output.recipe.dynamicStates = static_cast<pipelines::DynamicState>(dynamicStates);
        output.streams.Resize(count);
        for (u32 index = 0; index < count; ++index)
        {
            u8 rate = 0;
            if (!reader.ReadU32(output.streams[index].binding) || !reader.ReadU32(output.streams[index].stride) || !reader.ReadU8(rate) ||
                !reader.ReadU32(output.streams[index].instanceStepRate))
                return false;
            output.streams[index].inputRate = static_cast<pipelines::InputRate>(rate);
        }
        if (!reader.ReadU32(count) || count > 256)
            return false;
        output.attributes.Resize(count);
        for (u32 index = 0; index < count; ++index)
        {
            pipelines::VertexAttribute& attribute = output.attributes[index];
            u8 numeric = 0;
            u16 format = 0;
            if (!reader.ReadU64(attribute.semantic) || !reader.ReadU32(attribute.semanticIndex) || !reader.ReadU32(attribute.location) ||
                !reader.ReadU32(attribute.streamBinding) || !reader.ReadU32(attribute.byteOffset) || !reader.ReadU8(numeric) ||
                !reader.ReadU8(attribute.componentCount) || !reader.ReadU8(attribute.componentBits) || !reader.ReadU16(format) ||
                !ReadString(reader, attribute.semanticName))
                return false;
            attribute.numericClass = static_cast<shaders::NumericClass>(numeric);
            attribute.format = static_cast<pipelines::Format>(format);
        }
        if (!reader.ReadU32(output.recipe.rayTracing.maximumRecursionDepth) || !reader.ReadU32(output.recipe.rayTracing.maximumPayloadBytes) ||
            !reader.ReadU32(output.recipe.rayTracing.maximumAttributeBytes) || !reader.ReadU32(count) || count > 4096)
            return false;
        output.groups.Resize(count);
        for (u32 index = 0; index < count; ++index)
        {
            u8 groupKind = 0;
            pipelines::RayTracingGroup& group = output.groups[index];
            if (!reader.ReadU64(group.name) || !reader.ReadU8(groupKind) || !reader.ReadU32(group.shaderLibrary) ||
                !reader.ReadU64(group.generalEntry) || !reader.ReadU64(group.closestHitEntry) || !reader.ReadU64(group.anyHitEntry) ||
                !reader.ReadU64(group.intersectionEntry))
                return false;
            group.kind = static_cast<pipelines::RayTracingGroupKind>(groupKind);
        }
        output.recipe.vertexStreams = output.streams;
        output.recipe.vertexAttributes = output.attributes;
        output.recipe.rayTracingGroups = output.groups;
        return reader.GetRemaining() == 0;
    }

    struct ParsedMaterial
    {
        ParsedMaterial() noexcept
            : techniques(memory::pools::Tools::GetInstance()), constants(memory::pools::Tools::GetInstance()),
              constantOffsets(memory::pools::Tools::GetInstance()), constantBytes(memory::pools::Tools::GetInstance()),
              resources(memory::pools::Tools::GetInstance())
        {
        }
        struct ConstantOffset
        {
            u64 name = 0;
            shaders::ScalarType scalarType = shaders::ScalarType::F32;
            u8 rows = 1;
            u8 columns = 1;
            bool rowMajor = false;
            u32 arrayCount = 1;
            u32 offset = 0;
            u32 size = 0;
        };
        u64 name = 0;
        resources::ResourceReference shader;
        containers::DynamicArray<materials::TechniqueBuildRecord> techniques;
        containers::DynamicArray<mt::MaterialLogicalConstantValue> constants;
        containers::DynamicArray<ConstantOffset> constantOffsets;
        containers::DynamicArray<u8> constantBytes;
        containers::DynamicArray<materials::ResourceValueBuildRecord> resources;
    };

    [[nodiscard]] u32 LogicalScalarByteSize(const shaders::ScalarType type) noexcept
    {
        switch (type)
        {
        case shaders::ScalarType::Bool: return 1;
        case shaders::ScalarType::I16:
        case shaders::ScalarType::U16:
        case shaders::ScalarType::F16: return 2;
        case shaders::ScalarType::I32:
        case shaders::ScalarType::U32:
        case shaders::ScalarType::F32: return 4;
        case shaders::ScalarType::I64:
        case shaders::ScalarType::U64:
        case shaders::ScalarType::F64: return 8;
        }
        return 0;
    }

    [[nodiscard]] bool ParseMaterial(const containers::ArraySpan<const u8> bytes, const mt::MaterialArtifactCompilerConfig& limits,
                                    ParsedMaterial& output) noexcept
    {
        filesystem::MemoryFileReaderExternalBuffer file(bytes.Data(), bytes.Count(), nullptr);
        serialization::BinaryReader reader(file);
        u32 magic = 0;
        u16 version = 0;
        u32 count = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(version) || magic != ValueMagic || version != ValueInputVersion || !reader.ReadU64(output.name) ||
            !ReadReference(reader, output.shader) || !reader.ReadU32(count) || count > limits.maximumTechniques)
            return false;
        output.techniques.Resize(count);
        for (materials::TechniqueBuildRecord& technique : output.techniques)
            if (!reader.ReadU64(technique.name) || !ReadReference(reader, technique.pipeline)) return false;
        if (!reader.ReadU32(count) || count > limits.maximumConstants)
            return false;
        output.constantOffsets.Resize(count);
        for (ParsedMaterial::ConstantOffset& constant : output.constantOffsets)
        {
            u8 scalarType = 0;
            u8 rowMajor = 0;
            if (!reader.ReadU64(constant.name) || !reader.ReadU8(scalarType) || !reader.ReadU8(constant.rows) ||
                !reader.ReadU8(constant.columns) || !reader.ReadU8(rowMajor) || !reader.ReadU32(constant.arrayCount) ||
                !reader.ReadU32(constant.size) || scalarType > static_cast<u8>(shaders::ScalarType::F64) ||
                constant.rows == 0 || constant.rows > 4 || constant.columns == 0 || constant.columns > 4 ||
                rowMajor > 1 || (rowMajor != 0 && (constant.rows == 1 || constant.columns == 1)) || constant.arrayCount == 0 ||
                constant.size == 0 || constant.size > reader.GetRemaining() ||
                output.constantBytes.Size() > ~u32{0} - constant.size)
                return false;
            constant.scalarType = static_cast<shaders::ScalarType>(scalarType);
            constant.rowMajor = rowMajor != 0;
            const u64 expectedSize = static_cast<u64>(LogicalScalarByteSize(constant.scalarType)) * constant.rows * constant.columns *
                                     constant.arrayCount;
            if (expectedSize == 0 || expectedSize > ~u32{0} || constant.size != expectedSize)
                return false;
            constant.offset = output.constantBytes.Size();
            const u32 previous = output.constantBytes.Size();
            output.constantBytes.Resize(previous + constant.size);
            if (!reader.ReadBytes(output.constantBytes.TypedData() + previous, constant.size)) return false;
        }
        output.constants.Resize(count);
        for (u32 index = 0; index < count; ++index)
        {
            const ParsedMaterial::ConstantOffset& constant = output.constantOffsets[index];
            output.constants[index] = {constant.name, constant.scalarType, constant.rows, constant.columns, constant.rowMajor,
                                       constant.arrayCount,
                                       {output.constantBytes.TypedData() + constant.offset, constant.size}};
        }
        if (!reader.ReadU32(count) || count > limits.maximumResources)
            return false;
        output.resources.Resize(count);
        for (materials::ResourceValueBuildRecord& resource : output.resources)
        {
            u8 dependency = 0;
            if (!reader.ReadU64(resource.name) || !reader.ReadU32(resource.arrayIndex) || !ReadReference(reader, resource.resource) ||
                !reader.ReadU8(dependency) || dependency > static_cast<u8>(resources::DependencyKind::Soft))
                return false;
            resource.dependency = static_cast<resources::DependencyKind>(dependency);
        }
        return reader.GetRemaining() == 0;
    }

    struct PackedConstantOffset
    {
        u64 name = 0;
        u32 offset = 0;
        u32 size = 0;
    };

    [[nodiscard]] bool PackMaterialConstants(const ParsedMaterial& parsed, const shaders::ShaderFile& shader,
                                             containers::DynamicArray<materials::ConstantValueBuildRecord>& constants,
                                             containers::DynamicArray<u8>& bytes) noexcept
    {
        constants.Clear();
        bytes.Clear();
        containers::DynamicArray<PackedConstantOffset> offsets{memory::pools::Tools::GetInstance()};
        offsets.Reserve(parsed.constants.Size());
        for (const mt::MaterialLogicalConstantValue& source : parsed.constants)
        {
            const shaders::ConstantMember* reflected = nullptr;
            for (const shaders::ConstantMember& candidate : shader.GetMaterialParameters())
            {
                if (candidate.name != source.name)
                    continue;
                if (reflected != nullptr)
                    return false;
                reflected = &candidate;
            }
            const u8 reflectedRows = source.columns == 1 && source.rows > 1 ? 1 : source.rows;
            const u8 reflectedColumns = source.columns == 1 && source.rows > 1 ? source.rows : source.columns;
            if (reflected == nullptr || reflected->scalarType != source.scalarType || reflected->rows != reflectedRows ||
                reflected->columns != reflectedColumns || reflected->rowMajor != source.rowMajor)
                return false;

            const u32 logicalScalarBytes = LogicalScalarByteSize(source.scalarType);
            const u32 reflectedScalarBytes = source.scalarType == shaders::ScalarType::Bool ? 4u : logicalScalarBytes;
            const bool matrix = source.rows > 1 && source.columns > 1;
            if ((matrix && reflected->matrixStride == 0) || (!matrix && reflected->matrixStride != 0) ||
                (source.arrayCount > 1 && reflected->arrayStride == 0) ||
                (source.arrayCount == 1 && reflected->arrayStride != 0))
                return false;
            const u32 majorCount = matrix ? (source.rowMajor ? source.rows : source.columns) : 1u;
            const u32 minorCount = matrix ? (source.rowMajor ? source.columns : source.rows) : source.rows * source.columns;
            const u64 tightMinorBytes = static_cast<u64>(minorCount) * reflectedScalarBytes;
            const u64 elementSpan = matrix ? static_cast<u64>(majorCount - 1u) * reflected->matrixStride + tightMinorBytes
                                           : tightMinorBytes;
            const u64 reflectedSpan = source.arrayCount > 1 ? static_cast<u64>(source.arrayCount - 1u) * reflected->arrayStride + elementSpan
                                                            : elementSpan;
            if (logicalScalarBytes == 0 || tightMinorBytes == 0 || (matrix && reflected->matrixStride < tightMinorBytes) ||
                (source.arrayCount > 1 && reflected->arrayStride < elementSpan) || reflectedSpan > ~u32{0} ||
                reflected->byteSize < reflectedSpan || bytes.Size() > ~u32{0} - reflected->byteSize)
                return false;

            const u32 offset = bytes.Size();
            bytes.Resize(offset + reflected->byteSize);
            if (bytes.Size() != offset + reflected->byteSize)
                return false;
            std::memset(bytes.TypedData() + offset, 0, reflected->byteSize);
            for (u32 arrayIndex = 0; arrayIndex < source.arrayCount; ++arrayIndex)
            {
                const u32 destinationArrayOffset = source.arrayCount > 1 ? arrayIndex * reflected->arrayStride : 0u;
                for (u32 row = 0; row < source.rows; ++row)
                {
                    for (u32 column = 0; column < source.columns; ++column)
                    {
                        const u32 component = (arrayIndex * source.rows + row) * source.columns + column;
                        const u8* const sourceBytes = source.data.Data() + component * logicalScalarBytes;
                        u32 destinationComponentOffset = (row * source.columns + column) * reflectedScalarBytes;
                        if (matrix)
                        {
                            const u32 major = source.rowMajor ? row : column;
                            const u32 minor = source.rowMajor ? column : row;
                            destinationComponentOffset = major * reflected->matrixStride + minor * reflectedScalarBytes;
                        }
                        u8* const destination = bytes.TypedData() + offset + destinationArrayOffset + destinationComponentOffset;
                        if (source.scalarType == shaders::ScalarType::Bool)
                        {
                            if (sourceBytes[0] > 1)
                                return false;
                            destination[0] = sourceBytes[0];
                        }
                        else
                            std::memcpy(destination, sourceBytes, logicalScalarBytes);
                    }
                }
            }
            offsets.PushBack({source.name, offset, reflected->byteSize});
        }

        constants.Resize(offsets.Size());
        if (constants.Size() != offsets.Size())
            return false;
        for (u32 index = 0; index < offsets.Size(); ++index)
            constants[index] = {offsets[index].name, bytes.TypedData() + offsets[index].offset, offsets[index].size};
        return true;
    }

    [[nodiscard]] const assets::ArtifactView* PrimaryArtifact(const assets::CompileContext& context,
                                                               const resources::ResourceReference dependency) noexcept
    {
        const assets::GeneratedDependencyView* const view = context.FindGeneratedDependency(dependency);
        if (view == nullptr)
            return nullptr;
        for (const assets::ArtifactView& artifact : view->artifacts)
            if (artifact.resource == dependency && assets::HasFlag(artifact.flags, assets::ArtifactFlags::Primary))
                return &artifact;
        return nullptr;
    }

    void Report(const assets::CompileContext& context, const u32 code, const char* message) noexcept
    {
        if (context.report == nullptr || message == nullptr || message[0] == '\0')
            return;
        const assets::BuildDiagnosticLocation location{context.request.source.identity};
        const assets::BuildDiagnosticDescription diagnostic{assets::BuildDiagnosticSeverity::Error, code, {&location, 1}, message};
        static_cast<void>(context.report->Add(diagnostic));
    }

    struct DiagnosticPosition
    {
        u32 line = 0;
        u32 column = 0;
    };

    [[nodiscard]] bool ParseDecimal(const char*& cursor, const char* const end, u32& value) noexcept
    {
        if (cursor == end || *cursor < '0' || *cursor > '9')
            return false;
        u64 parsed = 0;
        do
        {
            parsed = parsed * 10u + static_cast<u32>(*cursor - '0');
            if (parsed > 0xffffffffull)
                return false;
            ++cursor;
        } while (cursor != end && *cursor >= '0' && *cursor <= '9');
        value = static_cast<u32>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseDiagnosticPosition(const char* const message, const char* const sourceName,
                                               DiagnosticPosition& position) noexcept
    {
        const u32 messageLength = TextLength(message, shader_tools::MaximumDiagnosticBytes);
        const u32 sourceNameLength = TextLength(sourceName, shader_tools::MaximumSourceNameLength);
        if (messageLength == 0 || messageLength == shader_tools::MaximumDiagnosticBytes || sourceNameLength == 0 ||
            sourceNameLength == shader_tools::MaximumSourceNameLength || sourceNameLength > messageLength)
            return false;
        const char* const end = message + messageLength;
        for (const char* candidate = message; candidate + sourceNameLength < end; ++candidate)
        {
            if (std::memcmp(candidate, sourceName, sourceNameLength) != 0)
                continue;
            const char* cursor = candidate + sourceNameLength;
            u32 line = 0;
            u32 column = 0;
            if (*cursor == '(')
            {
                ++cursor;
                if (!ParseDecimal(cursor, end, line))
                    continue;
                if (cursor != end && (*cursor == ',' || *cursor == ':'))
                {
                    ++cursor;
                    while (cursor != end && *cursor == ' ') ++cursor;
                    if (!ParseDecimal(cursor, end, column))
                        continue;
                }
                if (cursor == end || *cursor != ')')
                    continue;
            }
            else if (*cursor == ':')
            {
                ++cursor;
                if (!ParseDecimal(cursor, end, line))
                    continue;
                if (cursor != end && *cursor == ':')
                {
                    ++cursor;
                    if (!ParseDecimal(cursor, end, column))
                        continue;
                }
            }
            else
                continue;
            if (line == 0)
                continue;
            position = {line, column};
            return true;
        }
        return false;
    }

    [[nodiscard]] const mt::MaterialGeneratedSourceRange* FindSourceRange(
        const containers::ArraySpan<const mt::MaterialGeneratedSourceRange> ranges, const u32 line) noexcept
    {
        u32 first = 0;
        u32 count = ranges.Count();
        while (count != 0)
        {
            const u32 step = count / 2u;
            const u32 index = first + step;
            const mt::MaterialGeneratedSourceRange& range = ranges[index];
            if (line < range.firstLine)
                count = step;
            else if (line > range.lastLine)
            {
                first = index + 1u;
                count -= step + 1u;
            }
            else
                return &range;
        }
        return nullptr;
    }

    void ReportProgramDiagnostic(const assets::CompileContext& context, const u32 code, const ParsedProgram& program,
                                 const char* const message) noexcept
    {
        if (context.report == nullptr || message == nullptr || message[0] == '\0')
            return;
        assets::BuildDiagnosticLocation location{context.request.source.identity};
        DiagnosticPosition position;
        if (ParseDiagnosticPosition(message, program.sourceName, position))
        {
            location.line = position.line;
            location.column = position.column;
            if (const mt::MaterialGeneratedSourceRange* const range = FindSourceRange(program.sourceRanges, position.line))
            {
                location.subject = range->sourceNode;
                location.detail = range->sourcePin;
            }
        }
        const assets::BuildDiagnosticDescription diagnostic{assets::BuildDiagnosticSeverity::Error, code, {&location, 1}, message};
        static_cast<void>(context.report->Add(diagnostic));
    }

    [[nodiscard]] crypto::Digest256 ReflectionFingerprint(const shader_tools::CompileOutput& output) noexcept
    {
        const auto hashU8 = [](crypto::Sha256Builder& hash, const u8 value) noexcept
        { static_cast<void>(hash.Update(&value, sizeof(value))); };
        const auto hashU16 = [](crypto::Sha256Builder& hash, const u16 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u)};
            static_cast<void>(hash.Update(bytes, sizeof(bytes)));
        };
        const auto hashU32 = [](crypto::Sha256Builder& hash, const u32 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
            static_cast<void>(hash.Update(bytes, sizeof(bytes)));
        };
        const auto hashU64 = [](crypto::Sha256Builder& hash, const u64 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                             static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
            static_cast<void>(hash.Update(bytes, sizeof(bytes)));
        };
        crypto::Sha256Builder hash;
        const shaders::MaterialDomainContract& domain = output.GetMaterialDomain();
        hashU64(hash, domain.name);
        hashU32(hash, domain.schemaVersion);
        hashU32(hash, domain.legalStages);
        static_cast<void>(hash.Update(domain.inputType.bytes, crypto::Digest256::ByteCount));
        static_cast<void>(hash.Update(domain.outputType.bytes, crypto::Digest256::ByteCount));
        const u32 byteSize = output.GetMaterialParameterByteSize();
        hashU32(hash, byteSize);
        hashU32(hash, output.GetMaterialParameters().Size());
        for (const shaders::ConstantMember& member : output.GetMaterialParameters())
        {
            hashU64(hash, member.name);
            hashU32(hash, member.byteOffset);
            hashU32(hash, member.byteSize);
            hashU32(hash, member.arrayStride);
            hashU32(hash, member.matrixStride);
            hashU8(hash, static_cast<u8>(member.scalarType));
            hashU8(hash, member.rows);
            hashU8(hash, member.columns);
            hashU8(hash, member.rowMajor ? 1u : 0u);
        }
        hashU32(hash, output.GetMaterialResources().Size());
        for (const shaders::MaterialResourceRole& resource : output.GetMaterialResources())
        {
            hashU64(hash, resource.name);
            hashU32(hash, resource.arrayIndex);
            hashU32(hash, resource.slot);
            hashU8(hash, static_cast<u8>(resource.kind));
            hashU8(hash, static_cast<u8>(resource.flags));
            hashU16(hash, resource.reserved);
            static_cast<void>(hash.Update(resource.typeFingerprint.bytes, crypto::Digest256::ByteCount));
            hashU8(hash, static_cast<u8>(resource.shape.access));
            hashU8(hash, static_cast<u8>(resource.shape.textureDimension));
            hashU8(hash, static_cast<u8>(resource.shape.bufferKind));
            hashU8(hash, static_cast<u8>(resource.shape.samplerKind));
            hashU8(hash, static_cast<u8>(resource.shape.scalarType));
            hashU8(hash, resource.shape.componentCount);
            hashU8(hash, static_cast<u8>(resource.shape.flags));
            hashU8(hash, resource.shape.reserved);
            hashU32(hash, resource.shape.elementStride);
        }
        crypto::Digest256 fingerprint;
        static_cast<void>(hash.Finalize(fingerprint));
        return fingerprint;
    }
} // namespace

namespace vanguard::material_tools
{
    static_assert(static_cast<u32>(materials::ResourceParameterKind::Texture) == 0 &&
                  static_cast<u32>(materials::ResourceParameterKind::AccelerationStructure) == 3);

    bool MaterialOfflineContract::IsValid() const noexcept
    {
        for (u32 target = 0; target < static_cast<u32>(assets::TargetPlatform::Count); ++target)
            if (!shaders::IsValidMaterialShaderCapabilityMask(targetCapabilities[target]))
                return false;
        return true;
    }

    shaders::MaterialShaderCapabilityMask MaterialOfflineContract::Capabilities(const assets::TargetPlatform target) const noexcept
    {
        return target < assets::TargetPlatform::Count ? targetCapabilities[static_cast<u32>(target)] : 0;
    }

    resources::ResourceTypeId MaterialOfflineContract::ResourceType(const materials::ResourceParameterKind kind) const noexcept
    {
        const u32 index = static_cast<u32>(kind);
        return index < 4 ? resourceTypes[index] : resources::InvalidResourceTypeId;
    }

    bool CalculateMaterialOfflineContractFingerprint(const MaterialOfflineContract& contract, crypto::Digest256& fingerprint) noexcept
    {
        fingerprint = {};
        if (!contract.IsValid())
            return false;
        containers::DynamicArray<u8> bytes{memory::pools::Tools::GetInstance()};
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(1))
            return false;
        for (const shaders::MaterialShaderCapabilityMask capabilities : contract.targetCapabilities)
            if (!writer.WriteU32(capabilities))
                return false;
        for (const resources::ResourceTypeId type : contract.resourceTypes)
            if (!writer.WriteU32(type))
                return false;
        if (!writer.Flush())
            return false;
        fingerprint = crypto::Sha256(bytes.Data(), bytes.Size());
        return !fingerprint.IsEmpty();
    }

    struct MaterialArtifactCompilers::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Tools);
        MaterialArtifactCompilerConfig config;
        shader_tools::ShaderCompiler shaderCompiler;
        crypto::Digest256 toolFingerprint;
        crypto::Digest256 offlineContractFingerprint;
        assets::BuildSystem* buildSystem = nullptr;
        assets::CompilerId programId = assets::InvalidCompilerId;
        assets::CompilerId pipelineId = assets::InvalidCompilerId;
        assets::CompilerId materialId = assets::InvalidCompilerId;
    };

    MaterialInputResult EncodeMaterialProgramInput(const MaterialProgramInput& input, containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (input.sourceName == nullptr || input.moduleName == nullptr || input.program == 0 || input.source.Empty() || input.entryPoints.Empty())
            return MaterialInputResult::InvalidArgument;
        crypto::Digest256 calculatedDomainFingerprint;
        if (shaders::CalculateMaterialDomainFingerprint(input.expectedDomain, calculatedDomainFingerprint) != shaders::Result::Success ||
            calculatedDomainFingerprint != input.expectedDomainFingerprint ||
            !shaders::IsValidMaterialShaderCapabilityMask(input.requiredCapabilities) ||
            (input.expectedDomain.requiredCapabilities & ~input.requiredCapabilities) != 0)
            return MaterialInputResult::InvalidArgument;
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(ProgramMagic) || !writer.WriteU16(ProgramInputVersion) || !WriteString(writer, input.sourceName, shader_tools::MaximumSourceNameLength) ||
            !WriteString(writer, input.moduleName, shader_tools::MaximumModuleNameLength) || !writer.WriteU64(input.program) ||
            !writer.WriteBytes(input.permutation.bytes, crypto::Digest256::ByteCount) ||
            !WriteMaterialDomain(writer, input.expectedDomain) ||
            !writer.WriteBytes(input.expectedDomainFingerprint.bytes, crypto::Digest256::ByteCount) ||
            !writer.WriteU32(input.requiredCapabilities) ||
            !writer.WriteU8(static_cast<u8>(input.settings.target)) || !writer.WriteU8(static_cast<u8>(input.settings.optimization)) ||
            !writer.WriteU8(static_cast<u8>(input.settings.debugInformation)) || !writer.WriteBool(input.settings.warningsAsErrors) ||
            !writer.WriteBool(input.settings.preciseFloatingPoint) || !WriteString(writer, input.settings.profile, 64) ||
            !writer.WriteU32(input.source.Count()) || !writer.WriteBytes(input.source.Data(), input.source.Count()) ||
            !writer.WriteU32(input.entryPoints.Count()))
            return MaterialInputResult::InvalidArgument;
        for (const shader_tools::EntryPoint& entry : input.entryPoints)
            if (!writer.WriteU8(static_cast<u8>(entry.stage)) || !WriteString(writer, entry.name, shaders::MaximumEntryPointLength))
                return MaterialInputResult::InvalidArgument;
        if (!writer.WriteU32(input.defines.Count()))
            return MaterialInputResult::InvalidArgument;
        for (const shader_tools::Define& define : input.defines)
            if (!WriteString(writer, define.name, 256) || !WriteString(writer, define.value != nullptr ? define.value : "1", 1024))
                return MaterialInputResult::InvalidArgument;
        if (!writer.WriteU32(input.sourceRanges.Count()))
            return MaterialInputResult::InvalidArgument;
        const u32 lineCount = SourceLineCount(input.source);
        u32 previousLastLine = 0;
        for (u32 index = 0; index < input.sourceRanges.Count(); ++index)
        {
            const MaterialGeneratedSourceRange& range = input.sourceRanges[index];
            if (range.value != index || range.firstLine == 0 || range.lastLine < range.firstLine || range.lastLine > lineCount ||
                range.firstLine <= previousLastLine || !writer.WriteU32(range.value) || !writer.WriteU32(range.firstLine) ||
                !writer.WriteU32(range.lastLine) || !writer.WriteU64(range.sourceNode) || !writer.WriteU32(range.sourcePin))
                return MaterialInputResult::InvalidArgument;
            previousLastLine = range.lastLine;
        }
        return writer.Flush() ? MaterialInputResult::Success : MaterialInputResult::InvalidEncoding;
    }

    MaterialInputResult EncodeMaterialPipelineInput(const MaterialPipelineInput& input, containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (!input.shader.IsValid() || input.recipe.name == 0 || !input.recipe.shaders.Empty())
            return MaterialInputResult::InvalidArgument;
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(PipelineMagic) || !writer.WriteU16(PipelineInputVersion) || !WriteReference(writer, input.shader) ||
            !writer.WriteU8(static_cast<u8>(input.recipe.kind)) || !writer.WriteU64(input.recipe.name) ||
            !writer.WriteU64(static_cast<u64>(input.recipe.dynamicStates)) || !WriteGraphics(writer, input.recipe.graphics) ||
            !writer.WriteU32(input.recipe.vertexStreams.Count()))
            return MaterialInputResult::InvalidArgument;
        for (const pipelines::VertexStream& stream : input.recipe.vertexStreams)
            if (!writer.WriteU32(stream.binding) || !writer.WriteU32(stream.stride) || !writer.WriteU8(static_cast<u8>(stream.inputRate)) ||
                !writer.WriteU32(stream.instanceStepRate))
                return MaterialInputResult::InvalidArgument;
        if (!writer.WriteU32(input.recipe.vertexAttributes.Count())) return MaterialInputResult::InvalidArgument;
        for (const pipelines::VertexAttribute& attribute : input.recipe.vertexAttributes)
            if (!writer.WriteU64(attribute.semantic) || !writer.WriteU32(attribute.semanticIndex) || !writer.WriteU32(attribute.location) ||
                !writer.WriteU32(attribute.streamBinding) || !writer.WriteU32(attribute.byteOffset) ||
                !writer.WriteU8(static_cast<u8>(attribute.numericClass)) || !writer.WriteU8(attribute.componentCount) ||
                !writer.WriteU8(attribute.componentBits) || !writer.WriteU16(static_cast<u16>(attribute.format)) ||
                !WriteString(writer, attribute.semanticName, pipelines::MaximumVertexSemanticNameLength))
                return MaterialInputResult::InvalidArgument;
        if (!writer.WriteU32(input.recipe.rayTracing.maximumRecursionDepth) || !writer.WriteU32(input.recipe.rayTracing.maximumPayloadBytes) ||
            !writer.WriteU32(input.recipe.rayTracing.maximumAttributeBytes) || !writer.WriteU32(input.recipe.rayTracingGroups.Count()))
            return MaterialInputResult::InvalidArgument;
        for (const pipelines::RayTracingGroup& group : input.recipe.rayTracingGroups)
            if (!writer.WriteU64(group.name) || !writer.WriteU8(static_cast<u8>(group.kind)) || !writer.WriteU32(group.shaderLibrary) ||
                !writer.WriteU64(group.generalEntry) || !writer.WriteU64(group.closestHitEntry) || !writer.WriteU64(group.anyHitEntry) ||
                !writer.WriteU64(group.intersectionEntry))
                return MaterialInputResult::InvalidArgument;
        return writer.Flush() ? MaterialInputResult::Success : MaterialInputResult::InvalidEncoding;
    }

    MaterialInputResult EncodeMaterialValueInput(const MaterialValueInput& input, containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (input.name == 0 || !input.shader.IsValid() || input.techniques.Empty())
            return MaterialInputResult::InvalidArgument;
        filesystem::MemoryFileWriter file(bytes);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(ValueMagic) || !writer.WriteU16(ValueInputVersion) || !writer.WriteU64(input.name) || !WriteReference(writer, input.shader) ||
            !writer.WriteU32(input.techniques.Count())) return MaterialInputResult::InvalidArgument;
        for (const materials::TechniqueBuildRecord& technique : input.techniques)
            if (!writer.WriteU64(technique.name) || !WriteReference(writer, technique.pipeline)) return MaterialInputResult::InvalidArgument;
        if (!writer.WriteU32(input.constants.Count())) return MaterialInputResult::InvalidArgument;
        for (const MaterialLogicalConstantValue& constant : input.constants)
        {
            const u64 expectedSize = static_cast<u64>(LogicalScalarByteSize(constant.scalarType)) * constant.rows * constant.columns *
                                     constant.arrayCount;
            if (constant.name == 0 || constant.scalarType > shaders::ScalarType::F64 || constant.rows == 0 || constant.rows > 4 ||
                constant.columns == 0 || constant.columns > 4 ||
                (constant.rowMajor && (constant.rows == 1 || constant.columns == 1)) || constant.arrayCount == 0 ||
                expectedSize == 0 || expectedSize > ~u32{0} || constant.data.Count() != expectedSize ||
                !writer.WriteU64(constant.name) || !writer.WriteU8(static_cast<u8>(constant.scalarType)) ||
                !writer.WriteU8(constant.rows) || !writer.WriteU8(constant.columns) || !writer.WriteU8(constant.rowMajor ? 1u : 0u) ||
                !writer.WriteU32(constant.arrayCount) || !writer.WriteU32(constant.data.Count()) ||
                !writer.WriteBytes(constant.data.Data(), constant.data.Count()))
                return MaterialInputResult::InvalidArgument;
        }
        if (!writer.WriteU32(input.resources.Count())) return MaterialInputResult::InvalidArgument;
        for (const materials::ResourceValueBuildRecord& resource : input.resources)
            if (resource.dependency > resources::DependencyKind::Soft || !writer.WriteU64(resource.name) ||
                !writer.WriteU32(resource.arrayIndex) || !WriteReference(writer, resource.resource) ||
                !writer.WriteU8(static_cast<u8>(resource.dependency))) return MaterialInputResult::InvalidArgument;
        return writer.Flush() ? MaterialInputResult::Success : MaterialInputResult::InvalidEncoding;
    }

    namespace
    {
        [[nodiscard]] bool DiscoverProgram(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            auto& implementation = *static_cast<MaterialArtifactCompilers::Impl*>(userData);
            ParsedProgram parsed;
            const resources::ResourceReference tool(resources::ResourcePath::FromString(ToolPath), MaterialCompilerToolResourceType);
            return ParseProgram(request.source.content, implementation.config, parsed) && TargetMatches(request.target, parsed.settings.target) &&
                   (parsed.requiredCapabilities & ~implementation.config.offline.Capabilities(request.target)) == 0 &&
                   dependencies.Add({tool, implementation.toolFingerprint, assets::DependencyRole::Tool,
                                     assets::DependencyRequirement::Required}) == assets::Result::Success;
        }

        [[nodiscard]] bool EstimateProgram(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                           assets::BuildResourceEstimate& estimate, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            estimate = {implementation.config.estimatedShaderTransientBytes, implementation.config.maximumProgramArtifactBytes};
            return true;
        }

        [[nodiscard]] bool CompileProgram(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* userData) noexcept
        {
            auto& implementation = *static_cast<MaterialArtifactCompilers::Impl*>(userData);
            ParsedProgram parsed;
            if (!ParseProgram(context.request.source.content, implementation.config, parsed))
            {
                Report(context, 0x4d500001u, "invalid canonical material program input");
                return false;
            }
            if (!TargetMatches(context.request.target, parsed.settings.target))
            {
                Report(context, 0x4d500005u, "material program target does not match its build request");
                return false;
            }
            const shaders::MaterialShaderCapabilityMask supported = implementation.config.offline.Capabilities(context.request.target);
            if ((parsed.requiredCapabilities & ~supported) != 0)
            {
                Report(context, 0x4d500006u, "material program requires capabilities absent from the offline cook target");
                return false;
            }
            shader_tools::CompileRequest request;
            request.sourceName = parsed.sourceName;
            request.moduleName = parsed.moduleName;
            request.source = parsed.source;
            request.entryPoints = parsed.entries;
            request.defines = parsed.defines;
            request.settings = parsed.settings;
            shader_tools::CompileOutput probe;
            shader_tools::Result result = implementation.shaderCompiler.Reflect(request, probe);
            if (result != shader_tools::Result::Success || !probe.HasMaterialContract())
            {
                ReportProgramDiagnostic(context, 0x4d500002u, parsed,
                                        probe.GetDiagnostics()[0] != '\0' ? probe.GetDiagnostics() : shader_tools::ToString(result));
                return false;
            }
            crypto::Digest256 probeDomainFingerprint;
            if (!shaders::MaterialDomainContractsEqual(probe.GetMaterialDomain(), parsed.expectedDomain) ||
                shaders::CalculateMaterialDomainFingerprint(probe.GetMaterialDomain(), probeDomainFingerprint) != shaders::Result::Success ||
                probeDomainFingerprint != parsed.expectedDomainFingerprint)
            {
                Report(context, 0x4d500007u, "reflected material domain does not match the frozen frontend contract");
                return false;
            }
            const crypto::Digest256 probeFingerprint = ReflectionFingerprint(probe);
            if (context.IsCancellationRequested()) return false;
            containers::DynamicArray<u8> finalSource{memory::pools::Tools::GetInstance()};
            const u32 maximumGeneratedSourceBytes = implementation.config.maximumProgramInputBytes > 0xffffffffull
                                                        ? 0xffffffffu
                                                        : static_cast<u32>(implementation.config.maximumProgramInputBytes);
            const MaterialSlangResult generationResult = FinalizeMaterialSlangSource(request.source, probe, finalSource,
                                                                                     {maximumGeneratedSourceBytes});
            if (generationResult != MaterialSlangResult::Success)
            {
                Report(context, 0x4d500004u, ToString(generationResult));
                return false;
            }
            request.source = finalSource;
            shader_tools::CompileOutput compiled;
            result = implementation.shaderCompiler.Compile(request, compiled);
            crypto::Digest256 compiledDomainFingerprint;
            if (result != shader_tools::Result::Success || !compiled.HasMaterialContract() ||
                !shaders::MaterialDomainContractsEqual(compiled.GetMaterialDomain(), parsed.expectedDomain) ||
                shaders::CalculateMaterialDomainFingerprint(compiled.GetMaterialDomain(), compiledDomainFingerprint) != shaders::Result::Success ||
                compiledDomainFingerprint != parsed.expectedDomainFingerprint || ReflectionFingerprint(compiled) != probeFingerprint)
            {
                ReportProgramDiagnostic(context, 0x4d500003u, parsed,
                                        compiled.GetDiagnostics()[0] != '\0' ? compiled.GetDiagnostics() : "material layout probe mismatch");
                return false;
            }
            containers::DynamicArray<u8> bytes{memory::pools::Assets::GetInstance()};
            filesystem::MemoryFileWriter writer(bytes);
            if (compiled.WriteShader(writer, parsed.program, parsed.permutation) != shader_tools::Result::Success ||
                bytes.Size() > implementation.config.maximumProgramArtifactBytes)
                return false;
            return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                                 bytes.Size()) == assets::Result::Success;
        }

        [[nodiscard]] bool DiscoverPipeline(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            ParsedPipeline parsed;
            const resources::ResourceReference tool(resources::ResourcePath::FromString(ToolPath), MaterialCompilerToolResourceType);
            return ParsePipeline(request.source.content, parsed) &&
                   dependencies.Add({tool, implementation.toolFingerprint, assets::DependencyRole::Tool,
                                     assets::DependencyRequirement::Required}) == assets::Result::Success &&
                   dependencies.Add({parsed.shader, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) == assets::Result::Success;
        }

        [[nodiscard]] bool EstimatePipeline(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                            assets::BuildResourceEstimate& estimate, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            estimate = {implementation.config.maximumPipelineArtifactBytes, implementation.config.maximumPipelineArtifactBytes};
            return true;
        }

        [[nodiscard]] bool CompilePipeline(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            ParsedPipeline parsed;
            if (!ParsePipeline(context.request.source.content, parsed)) return false;
            const assets::ArtifactView* const shaderArtifact = PrimaryArtifact(context, parsed.shader);
            if (shaderArtifact == nullptr) return false;
            filesystem::MemoryFileReaderExternalBuffer shaderReader(shaderArtifact->bytes.Data(), shaderArtifact->bytes.Count(), nullptr);
            shaders::ShaderFile shader;
            if (shader.Open(shaderReader) != shaders::Result::Success) return false;
            pipelines::ShaderReference shaderReference;
            shaderReference.resource = parsed.shader.GetPath().Id();
            shaderReference.permutation = shader.GetPermutation();
            shaderReference.bindingLayout = shader.BindingLayoutFingerprint();
            shaderReference.pipelineInterface = shader.GetPipelineInterfaceFingerprint();
            if (shader.HasMaterialContract())
            {
                shaderReference.materialDomain = shader.GetMaterialContract()->domainFingerprint;
                shaderReference.materialLayout = shader.GetMaterialContract()->layoutFingerprint;
            }
            parsed.recipe.shaders = {&shaderReference, 1};
            containers::DynamicArray<u8> bytes{memory::pools::Assets::GetInstance()};
            filesystem::MemoryFileWriter writer(bytes);
            if (pipelines::WritePipeline(writer, parsed.recipe) != pipelines::Result::Success ||
                bytes.Size() > implementation.config.maximumPipelineArtifactBytes)
                return false;
            return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                                 bytes.Size()) == assets::Result::Success;
        }

        [[nodiscard]] bool DiscoverMaterial(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            ParsedMaterial parsed;
            const resources::ResourceReference tool(resources::ResourcePath::FromString(ToolPath), MaterialCompilerToolResourceType);
            if (!ParseMaterial(request.source.content, implementation.config, parsed) ||
                dependencies.Add({tool, implementation.toolFingerprint, assets::DependencyRole::Tool,
                                  assets::DependencyRequirement::Required}) != assets::Result::Success)
                return false;

            containers::DynamicArray<assets::BuildDependency> generated{memory::pools::Tools::GetInstance()};
            const auto include = [&generated](const resources::ResourceReference identity,
                                              const assets::DependencyRequirement requirement) noexcept
            {
                for (assets::BuildDependency& existing : generated)
                {
                    if (existing.identity != identity)
                        continue;
                    if (requirement < existing.requirement)
                        existing.requirement = requirement;
                    return true;
                }
                const u32 previous = generated.Size();
                generated.PushBack({identity, {}, assets::DependencyRole::Generated, requirement});
                return generated.Size() == previous + 1u;
            };
            if (!include(parsed.shader, assets::DependencyRequirement::Required))
                return false;
            for (const materials::TechniqueBuildRecord& technique : parsed.techniques)
                if (!include(technique.pipeline, assets::DependencyRequirement::Required))
                    return false;
            for (const materials::ResourceValueBuildRecord& resource : parsed.resources)
            {
                const assets::DependencyRequirement requirement =
                    resource.dependency == resources::DependencyKind::Required ? assets::DependencyRequirement::Required
                    : resource.dependency == resources::DependencyKind::Optional ? assets::DependencyRequirement::Optional
                                                                                 : assets::DependencyRequirement::Soft;
                if (!include(resource.resource, requirement))
                    return false;
            }
            for (const assets::BuildDependency& dependency : generated)
                if (dependencies.Add(dependency) != assets::Result::Success)
                    return false;
            return true;
        }

        [[nodiscard]] bool EstimateMaterial(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                            assets::BuildResourceEstimate& estimate, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            estimate = {implementation.config.maximumMaterialArtifactBytes, implementation.config.maximumMaterialArtifactBytes};
            return true;
        }

        [[nodiscard]] bool CompileMaterial(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* userData) noexcept
        {
            const auto& implementation = *static_cast<const MaterialArtifactCompilers::Impl*>(userData);
            ParsedMaterial parsed;
            if (!ParseMaterial(context.request.source.content, implementation.config, parsed)) return false;
            const assets::ArtifactView* const shaderArtifact = PrimaryArtifact(context, parsed.shader);
            if (shaderArtifact == nullptr) return false;
            filesystem::MemoryFileReaderExternalBuffer shaderReader(shaderArtifact->bytes.Data(), shaderArtifact->bytes.Count(), nullptr);
            shaders::ShaderFile shader;
            if (shader.Open(shaderReader) != shaders::Result::Success || !shader.HasMaterialContract()) return false;
            containers::DynamicArray<pipelines::PipelineFile*> opened{memory::pools::Tools::GetInstance()};
            opened.Reserve(parsed.techniques.Size());
            for (materials::TechniqueBuildRecord& technique : parsed.techniques)
            {
                const assets::ArtifactView* const pipelineArtifact = PrimaryArtifact(context, technique.pipeline);
                if (pipelineArtifact == nullptr)
                {
                    for (pipelines::PipelineFile* previous : opened) VANGUARD_DELETE(previous);
                    return false;
                }
                pipelines::PipelineFile* const pipeline = VANGUARD_NEW(pipelines::PipelineFile, memory::pools::Tools);
                if (pipeline == nullptr)
                {
                    for (pipelines::PipelineFile* previous : opened) VANGUARD_DELETE(previous);
                    return false;
                }
                filesystem::MemoryFileReaderExternalBuffer pipelineReader(pipelineArtifact->bytes.Data(), pipelineArtifact->bytes.Count(), nullptr);
                if (pipeline->Open(pipelineReader) != pipelines::Result::Success)
                {
                    VANGUARD_DELETE(pipeline);
                    for (pipelines::PipelineFile* previous : opened) VANGUARD_DELETE(previous);
                    return false;
                }
                opened.PushBack(pipeline);
                technique.pipelineReflection = pipeline;
            }
            containers::DynamicArray<materials::ConstantValueBuildRecord> packedConstants{memory::pools::Tools::GetInstance()};
            containers::DynamicArray<u8> packedConstantBytes{memory::pools::Tools::GetInstance()};
            if (!PackMaterialConstants(parsed, shader, packedConstants, packedConstantBytes))
            {
                for (pipelines::PipelineFile* previous : opened) VANGUARD_DELETE(previous);
                Report(context, 0x4d500106u, "logical material constants disagree with reflected VSHADER layout");
                return false;
            }
            materials::ResourceTypeCompatibility compatibility[4]{};
            u32 compatibilityCount = 0;
            for (u32 kindIndex = 0; kindIndex < 4; ++kindIndex)
            {
                const materials::ResourceParameterKind kind = static_cast<materials::ResourceParameterKind>(kindIndex);
                const resources::ResourceTypeId assetType = implementation.config.offline.ResourceType(kind);
                if (assetType != resources::InvalidResourceTypeId)
                    compatibility[compatibilityCount++] = {kind, assetType};
            }
            materials::BuildDescription description;
            description.name = parsed.name;
            description.shader = parsed.shader;
            description.shaderReflection = &shader;
            description.techniques = parsed.techniques;
            description.constants = packedConstants;
            description.resources = parsed.resources;
            description.resourceTypeCompatibility = {compatibility, compatibilityCount};
            containers::DynamicArray<u8> bytes{memory::pools::Assets::GetInstance()};
            filesystem::MemoryFileWriter writer(bytes);
            const materials::Result result = materials::WriteMaterial(writer, description);
            for (pipelines::PipelineFile* pipeline : opened) VANGUARD_DELETE(pipeline);
            if (result != materials::Result::Success)
            {
                Report(context, 0x4d500104u, materials::ToString(result));
                return false;
            }
            if (bytes.Size() > implementation.config.maximumMaterialArtifactBytes)
            {
                Report(context, 0x4d500105u, "VMAT artifact exceeds the configured byte limit");
                return false;
            }
            return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                                 bytes.Size()) == assets::Result::Success;
        }
    } // namespace

    MaterialArtifactCompilers::~MaterialArtifactCompilers() { static_cast<void>(Shutdown()); }

    bool MaterialArtifactCompilers::Initialize(const MaterialArtifactCompilerConfig& config) noexcept
    {
        if (m_impl != nullptr || !config.offline.IsValid() || config.maximumEntryPoints == 0 || config.maximumSourceRanges == 0 || config.maximumProgramArtifactBytes == 0 ||
            config.maximumPipelineArtifactBytes == 0 || config.maximumMaterialArtifactBytes == 0 || config.estimatedShaderTransientBytes == 0)
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr || implementation->shaderCompiler.Initialize() != shader_tools::Result::Success)
        {
            if (implementation != nullptr) VANGUARD_DELETE(implementation);
            return false;
        }
        implementation->config = config;
        if (!CalculateMaterialOfflineContractFingerprint(config.offline, implementation->offlineContractFingerprint))
        {
            implementation->shaderCompiler.Shutdown();
            VANGUARD_DELETE(implementation);
            return false;
        }
        constexpr char ToolPolicy[] = "VanguardMaterialArtifacts;CanonicalInput4;SealedDomain1;OfflineCapabilities1;TypedResources1;ExactDependencies1;AuthoredDiagnostics1;ProbeThenNative1;PipelineWriter1;MaterialWriter5;TechniquePrograms1";
        crypto::Sha256Builder toolHash;
        static_cast<void>(toolHash.Update(ToolPolicy, sizeof(ToolPolicy) - 1u));
        static_cast<void>(toolHash.Update(implementation->shaderCompiler.CompilerFingerprint().bytes, crypto::Digest256::ByteCount));
        static_cast<void>(toolHash.Update(implementation->offlineContractFingerprint.bytes, crypto::Digest256::ByteCount));
        if (!toolHash.Finalize(implementation->toolFingerprint))
        {
            implementation->shaderCompiler.Shutdown();
            VANGUARD_DELETE(implementation);
            return false;
        }
        implementation->programId = assets::HashCompilerName(ProgramCompilerName);
        implementation->pipelineId = assets::HashCompilerName(PipelineCompilerName);
        implementation->materialId = assets::HashCompilerName(MaterialCompilerName);
        m_impl = implementation;
        return true;
    }

    bool MaterialArtifactCompilers::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        if (m_impl->buildSystem != nullptr && Unregister() != assets::Result::Success) return false;
        m_impl->shaderCompiler.Shutdown();
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }
    bool MaterialArtifactCompilers::IsInitialized() const noexcept { return m_impl != nullptr; }

    assets::CompilerDescriptor MaterialArtifactCompilers::ProgramDescriptor() noexcept
    {
        return m_impl != nullptr ? assets::CompilerDescriptor{m_impl->programId, ProgramCompilerName, MaterialArtifactCompilerVersion,
                                                               MaterialProgramInputResourceType, shaders::ShaderResourceType, DiscoverProgram,
                                                               CompileProgram, m_impl, EstimateProgram}
                                 : assets::CompilerDescriptor{};
    }
    assets::CompilerDescriptor MaterialArtifactCompilers::PipelineDescriptor() noexcept
    {
        return m_impl != nullptr ? assets::CompilerDescriptor{m_impl->pipelineId, PipelineCompilerName, MaterialArtifactCompilerVersion,
                                                               MaterialPipelineInputResourceType, pipelines::PipelineResourceType, DiscoverPipeline,
                                                               CompilePipeline, m_impl, EstimatePipeline}
                                 : assets::CompilerDescriptor{};
    }
    assets::CompilerDescriptor MaterialArtifactCompilers::MaterialDescriptor() noexcept
    {
        return m_impl != nullptr ? assets::CompilerDescriptor{m_impl->materialId, MaterialCompilerName, MaterialArtifactCompilerVersion,
                                                               MaterialValueInputResourceType, materials::MaterialResourceType, DiscoverMaterial,
                                                               CompileMaterial, m_impl, EstimateMaterial}
                                 : assets::CompilerDescriptor{};
    }

    assets::Result MaterialArtifactCompilers::Register(assets::BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem != nullptr) return assets::Result::InvalidState;
        assets::Result result = buildSystem.RegisterCompiler(ProgramDescriptor());
        if (result != assets::Result::Success) return result;
        result = buildSystem.RegisterCompiler(PipelineDescriptor());
        if (result != assets::Result::Success)
        {
            static_cast<void>(buildSystem.UnregisterCompiler(m_impl->programId));
            return result;
        }
        result = buildSystem.RegisterCompiler(MaterialDescriptor());
        if (result != assets::Result::Success)
        {
            static_cast<void>(buildSystem.UnregisterCompiler(m_impl->pipelineId));
            static_cast<void>(buildSystem.UnregisterCompiler(m_impl->programId));
            return result;
        }
        m_impl->buildSystem = &buildSystem;
        return assets::Result::Success;
    }

    assets::Result MaterialArtifactCompilers::Unregister() noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem == nullptr) return assets::Result::InvalidState;
        assets::BuildSystem* const buildSystem = m_impl->buildSystem;
        assets::Result result = buildSystem->UnregisterCompiler(m_impl->materialId);
        if (result != assets::Result::Success) return result;
        result = buildSystem->UnregisterCompiler(m_impl->pipelineId);
        if (result != assets::Result::Success) return result;
        result = buildSystem->UnregisterCompiler(m_impl->programId);
        if (result == assets::Result::Success) m_impl->buildSystem = nullptr;
        return result;
    }

    const char* ToString(const MaterialInputResult result) noexcept
    {
        switch (result)
        {
        case MaterialInputResult::Success: return "Success";
        case MaterialInputResult::InvalidArgument: return "InvalidArgument";
        case MaterialInputResult::LimitExceeded: return "LimitExceeded";
        case MaterialInputResult::InvalidEncoding: return "InvalidEncoding";
        case MaterialInputResult::UnsupportedVersion: return "UnsupportedVersion";
        }
        return "Unknown";
    }
} // namespace vanguard::material_tools
