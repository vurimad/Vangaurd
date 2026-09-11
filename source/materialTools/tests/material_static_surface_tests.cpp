#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include "static_surface_proof.hpp"

#include <cstdio>
#include <cstring>

namespace vanguard::material_tools::tests
{
    namespace
    {
        using Bytes = containers::DynamicArray<u8>;

        bool Read(const filesystem::AbsolutePath& path, Bytes& output)
        {
            auto& files = filesystem::GetManager();
            const u64 size = files.GetFileSize(path);
            if (!files.FileExist(path) || size > ~u32{0})
                return false;
            auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!file)
                return false;
            output.Resize(static_cast<u32>(size));
            file->Serialize(output.Data(), output.Size());
            return !file->HasErrors();
        }

        MaterialIrType Float(const u8 components)
        {
            MaterialIrType type;
            type.kind = MaterialIrTypeKind::Numeric;
            type.scalarType = shaders::ScalarType::F32;
            type.rows = components;
            return type;
        }
        bool ValidateSurfacePipeline(const shader_tools::CompileOutput& compiled, const bool depth, const bool quantized, StaticSurfaceNativeProof nativeProof, void* context)
        {
            Bytes shaderBytes(memory::pools::Tools::GetInstance()), pipelineBytes(memory::pools::Tools::GetInstance());
            filesystem::MemoryFileWriter shaderWriter(shaderBytes);
            if (compiled.WriteShader(shaderWriter, 1, crypto::Sha256("static-surface-proof", 20)) != shader_tools::Result::Success)
                return false;
            filesystem::MemoryFileReader shaderReader(shaderBytes, 0);
            shaders::ShaderFile shader;
            if (shader.Open(shaderReader) != shaders::Result::Success)
                return false;
            const auto* contract = shader.GetMaterialContract();
            const pipelines::ShaderReference reference{
                1, shader.GetPermutation(), shader.BindingLayoutFingerprint(), shader.GetPipelineInterfaceFingerprint(), contract->domainFingerprint, contract->layoutFingerprint};
            const u32 positionBytes = quantized ? 8 : 12;
            const pipelines::VertexStream streams[]{{0, positionBytes + 36, pipelines::InputRate::PerVertex, 1}, {15, 16, pipelines::InputRate::PerInstance, 1}};
            const char* names[]{"POSITION", "NORMAL", "TANGENT", "TEXCOORD", "VG_DRAW", "VG_DRAW"};
            const u32 offsets[]{0, positionBytes, positionBytes + 12, positionBytes + 28, 0, 8};
            const pipelines::Format formats[]{quantized ? pipelines::Format::R16G16B16A16SNorm : pipelines::Format::R32G32B32Float,
                                              pipelines::Format::R32G32B32Float,
                                              pipelines::Format::R32G32B32A32Float,
                                              pipelines::Format::R32G32Float,
                                              pipelines::Format::R32G32UInt,
                                              pipelines::Format::R32G32UInt};
            containers::DynamicArray<pipelines::VertexAttribute> attributes(memory::pools::Tools::GetInstance());
            for (u32 index = 0; index < 6; ++index)
            {
                bool found = false;
                for (const auto& input : shader.GetVertexInputs())
                {
                    if (input.semantic != shaders::HashInterfaceName(names[index]) || input.semanticIndex != (index == 5 ? 1u : 0u))
                        continue;
                    pipelines::VertexAttribute attribute;
                    attribute.semantic = input.semantic;
                    attribute.semanticIndex = input.semanticIndex;
                    attribute.location = input.location;
                    attribute.streamBinding = index >= 4 ? 15 : 0;
                    attribute.byteOffset = offsets[index];
                    attribute.numericClass = input.numericClass;
                    attribute.componentCount = quantized && index == 0 ? 4 : input.componentCount;
                    attribute.componentBits = quantized && index == 0 ? 16 : 32;
                    attribute.format = formats[index];
                    std::memcpy(attribute.semanticName, names[index], std::strlen(names[index]) + 1);
                    attributes.PushBack(attribute);
                    found = true;
                    break;
                }
                if (!found)
                    return false;
            }
            pipelines::BuildDescription description;
            description.name = 1;
            description.shaders = {&reference, 1};
            description.vertexStreams = streams;
            description.vertexAttributes = attributes;
            description.graphics.blend.attachmentCount = depth ? 0 : 3;
            description.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Exact;
            description.graphics.exactAttachments.colorCount = depth ? 0 : 3;
            for (u32 index = 0; index < description.graphics.exactAttachments.colorCount; ++index)
                description.graphics.exactAttachments.colors[index] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
            description.graphics.exactAttachments.depthStencilFormat = pipelines::Format::D32Float;
            description.graphics.exactAttachments.depthStencilClass = pipelines::DepthStencilClass::Depth;
            filesystem::MemoryFileWriter pipelineWriter(pipelineBytes);
            const auto writeResult = pipelines::WritePipeline(pipelineWriter, description);
            if (writeResult != pipelines::Result::Success)
            {
                std::fprintf(stderr, "[staticSurface] pipeline write: %s\n", pipelines::ToString(writeResult));
                return false;
            }
            filesystem::MemoryFileReader pipelineReader(pipelineBytes, 0);
            pipelines::PipelineFile pipeline;
            const auto openResult = pipeline.Open(pipelineReader);
            const auto compatible = openResult == pipelines::Result::Success ? pipelines::ValidateShaderCompatibility(pipeline, shader) : openResult;
            if (compatible != pipelines::Result::Success)
                std::fprintf(stderr, "[staticSurface] pipeline compatibility: %s\n", pipelines::ToString(compatible));
            return compatible == pipelines::Result::Success && (nativeProof == nullptr || nativeProof(shader, pipeline, context));
        }
    } // namespace

    bool RunStaticSurfaceProof(const filesystem::AbsolutePath& working, StaticSurfaceNativeProof nativeProof, void* context)
    {
        const auto directory = working.AddDirPath("source").AddDirPath("rendering").AddDirPath("shaders");
        Bytes prefix(memory::pools::Tools::GetInstance()), suffix(memory::pools::Tools::GetInstance()), gpuTypes(memory::pools::Tools::GetInstance());
        if (!Read(directory.AddFilePath("static_surface_prefix.vsl"), prefix) || !Read(directory.AddFilePath("static_surface_suffix.vsl"), suffix) ||
            !Read(directory.AddFilePath("gpu_scene_types.hlsli"), gpuTypes))
            return false;
        const auto stage = shaders::StageBit(shaders::ShaderStage::Fragment);
        const MaterialSlangSymbol inputs[]{{1, "uv"}, {2, "worldNormal"}, {3, "viewRelativePosition"}};
        const MaterialSlangSymbol outputs[]{{100, "baseColor"}, {101, "normalTS"}, {102, "roughness"}, {103, "metallic"}, {104, "emissive"}, {105, "opacityCutoff"}};
        const u8 widths[]{4, 3, 1, 1, 3, 1};
        MaterialSlangDomain domain;
        domain.stableName = "VanguardStaticSurface";
        domain.schemaVersion = 1;
        domain.legalStages = stage;
        domain.inputTypeName = "StaticSurfaceInput";
        domain.outputTypeName = "StaticSurfaceOutput";
        domain.parameterTypeName = "StaticSurfaceParameters";
        domain.resourceTypeName = "StaticSurfaceResources";
        domain.evaluationFunctionName = "EvaluateStaticSurface";
        domain.inputs = inputs;
        domain.outputs = outputs;
        domain.prefix = prefix;
        domain.suffix = suffix;
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        bool success = true;
        for (u32 sampled = 0; sampled < 2 && success; ++sampled)
        {
            MaterialIrBuilder builder;
            builder.Reset(crypto::Sha256("VanguardStaticSurface", 21));
            for (u32 index = 0; index < 6; ++index)
            {
                MaterialIrValueBuildDescription value;
                value.kind = MaterialIrValueKind::DynamicParameter;
                value.type = Float(widths[index]);
                value.legalStages = stage;
                value.semantic = 200 + index;
                MaterialIrValueId parameter;
                if (builder.AddValue(value, parameter) != MaterialIrResult::Success)
                    return false;
                if (sampled != 0 && index == 0)
                {
                    MaterialIrValueId texture, sampler, uv, color;
                    value.type.kind = MaterialIrTypeKind::Texture;
                    value.type.textureDimension = MaterialIrTextureDimension::D2;
                    value.semantic = 300;
                    if (builder.AddValue(value, texture) != MaterialIrResult::Success)
                        return false;
                    value.type = {};
                    value.type.kind = MaterialIrTypeKind::Sampler;
                    value.semantic = 301;
                    if (builder.AddValue(value, sampler) != MaterialIrResult::Success)
                        return false;
                    value.kind = MaterialIrValueKind::DomainInput;
                    value.type = Float(2);
                    value.semantic = 1;
                    if (builder.AddValue(value, uv) != MaterialIrResult::Success)
                        return false;
                    const MaterialIrValueId sampleOperands[]{texture, sampler, uv};
                    value = {};
                    value.kind = MaterialIrValueKind::Instruction;
                    value.opcode = MaterialIrOpcode::TextureSample;
                    value.type = Float(4);
                    value.legalStages = stage;
                    value.operands = sampleOperands;
                    if (builder.AddValue(value, color) != MaterialIrResult::Success)
                        return false;
                    const MaterialIrValueId tintOperands[]{color, parameter};
                    value.opcode = MaterialIrOpcode::Multiply;
                    value.operands = tintOperands;
                    if (builder.AddValue(value, parameter) != MaterialIrResult::Success)
                        return false;
                }
                if (builder.AddOutput({outputs[index].semantic, parameter, stage}) != MaterialIrResult::Success)
                    return false;
            }
            MaterialIrModule module;
            containers::DynamicArray<MaterialIrDiagnostic> diagnostics(memory::pools::Tools::GetInstance());
            Bytes probe(memory::pools::Tools::GetInstance()), finalSource(memory::pools::Tools::GetInstance());
            if (builder.Finalize(module, diagnostics) != MaterialIrResult::Success || GenerateMaterialSlangProbe(module, domain, probe) != MaterialSlangResult::Success)
                return false;
            for (u32 target = 0; target < 2 && success; ++target)
                for (u32 depth = 0; depth < 2 && success; ++depth)
                {
                    const shader_tools::EntryPoint entries[]{{"StaticSurfaceVertexMain", shaders::ShaderStage::Vertex},
                                                             {depth ? "StaticSurfaceDepthMain" : "StaticSurfaceGBufferMain", shaders::ShaderStage::Fragment}};
                    shader_tools::CompileRequest request;
                    request.sourceName = "rendering/shaders/static_surface.generated.slang";
                    request.moduleName = "static_surface";
                    request.source = probe;
                    request.entryPoints = entries;
                    request.settings.target = target ? shader_tools::Target::VulkanSpirV : shader_tools::Target::D3D12Dxil;
                    request.loadSource = [](const char* path, shader_tools::LoadedSource& loaded, void* data) noexcept
                    {
                        if (std::strstr(path, "gpu_scene_types.hlsli") == nullptr)
                            return false;
                        loaded.content = *static_cast<Bytes*>(data);
                        return true;
                    };
                    request.loadSourceUserData = &gpuTypes;
                    shader_tools::CompileOutput reflection, compiled;
                    const auto result = compiler.Reflect(request, reflection);
                    if (result != shader_tools::Result::Success)
                    {
                        std::fprintf(stderr, "[staticSurface] reflect target=%u depth=%u sampled=%u: %s %s\n", target, depth, sampled, shader_tools::ToString(result), reflection.GetDiagnostics());
                        return false;
                    }
                    if (FinalizeMaterialSlangSource(probe, reflection, finalSource) != MaterialSlangResult::Success)
                        return false;
                    request.source = finalSource;
                    const auto compileResult = compiler.Compile(request, compiled);
                    success = compileResult == shader_tools::Result::Success && compiled.HasMaterialContract() && compiled.GetStages().Size() == 2 && compiled.GetFragmentOutputs().Size() == (depth ? 0u : 3u) &&
                              ValidateSurfacePipeline(compiled, depth != 0, false, target == 0 ? nativeProof : nullptr, context) &&
                              ValidateSurfacePipeline(compiled, depth != 0, true, target == 0 ? nativeProof : nullptr, context);
                    if (!success)
                        std::fprintf(stderr, "[staticSurface] target=%u depth=%u sampled=%u: %s\n", target, depth, sampled, compiled.GetDiagnostics());
                }
        }
        compiler.Shutdown();
        return success;
    }
} // namespace vanguard::material_tools::tests
