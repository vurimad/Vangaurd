#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/material_tools/material_ir.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace vgio = vanguard::io;
    namespace material = vanguard::material_tools;
    namespace memory = vanguard::memory;
    namespace shader = vanguard::shaders;
    namespace sht = vanguard::shader_tools;
    using vanguard::u8;
    using vanguard::u32;

    constexpr const char* ShaderDirectory = "D:/ENGINE/source/rendering/shaders";

    bool Read(const char* name, std::vector<u8>& output)
    {
        const std::string path = std::string(ShaderDirectory) + "/" + name;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        const std::streamsize size = file.tellg();
        if (size < 0 || static_cast<unsigned long long>(size) > 0xffffffffull)
            return false;
        output.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(output.data()), size));
    }

    bool Compile(sht::ShaderCompiler& compiler, const char* label, const char* sourceName,
                 const containers::ArraySpan<const u8> source, const containers::ArraySpan<const sht::EntryPoint> entries,
                 const sht::Target target, sht::CompileOutput& output)
    {
        const char* searchPaths[]{ShaderDirectory};
        sht::CompileRequest request;
        request.sourceName = sourceName;
        request.moduleName = label;
        request.source = source;
        request.entryPoints = entries;
        request.searchPaths = searchPaths;
        request.searchPathCount = 1;
        request.settings.target = target;
        const sht::Result result = compiler.Compile(request, output);
        if (result != sht::Result::Success)
        {
            std::fprintf(stderr, "%s %s failed: %s\n%s\n", label,
                         target == sht::Target::D3D12Dxil ? "DXIL" : "SPIR-V", sht::ToString(result), output.GetDiagnostics());
            return false;
        }
        const shader::NativeFormat expected = target == sht::Target::D3D12Dxil ? shader::NativeFormat::Dxil : shader::NativeFormat::SpirV;
        if (output.GetStages().Size() != entries.Size() || output.GetBytecode().Empty())
        {
            std::fprintf(stderr, "%s produced stages=%u expected=%u bytes=%u\n", label, output.GetStages().Size(), entries.Size(), output.GetBytecode().Size());
            return false;
        }
        for (const auto& stage : output.GetStages())
            if (stage.format != expected || stage.bytecodeSize == 0 || stage.bytecodeDigest.IsEmpty())
            {
                std::fprintf(stderr, "%s produced an invalid native stage record\n", label);
                return false;
            }
        std::printf("compiled %-28s %-6s entries=%u bytes=%u\n", label,
                    target == sht::Target::D3D12Dxil ? "DXIL" : "SPIR-V", entries.Size(), output.GetBytecode().Size());
        return true;
    }

    bool CompileFile(sht::ShaderCompiler& compiler, const char* fileName, const char* moduleName,
                     const containers::ArraySpan<const sht::EntryPoint> entries)
    {
        std::vector<u8> source;
        if (!Read(fileName, source))
        {
            std::fprintf(stderr, "cannot read %s\n", fileName);
            return false;
        }
        sht::CompileOutput output;
        const containers::ArraySpan<const u8> bytes{source.data(), static_cast<u32>(source.size())};
        return Compile(compiler, moduleName, fileName, bytes, entries, sht::Target::D3D12Dxil, output) &&
               Compile(compiler, moduleName, fileName, bytes, entries, sht::Target::VulkanSpirV, output);
    }

    bool CompileComputeEntries(sht::ShaderCompiler& compiler, const char* fileName,
                               const containers::ArraySpan<const sht::EntryPoint> entries)
    {
        std::vector<u8> source;
        if (!Read(fileName, source))
            return false;
        const containers::ArraySpan<const u8> bytes{source.data(), static_cast<u32>(source.size())};
        for (const auto& entry : entries)
        {
            const containers::ArraySpan<const sht::EntryPoint> one{&entry, 1};
            sht::CompileOutput output;
            if (!Compile(compiler, entry.name, fileName, bytes, one, sht::Target::D3D12Dxil, output) ||
                !Compile(compiler, entry.name, fileName, bytes, one, sht::Target::VulkanSpirV, output))
                return false;
        }
        return true;
    }

    material::MaterialIrType Float(const u8 components)
    {
        material::MaterialIrType type;
        type.kind = material::MaterialIrTypeKind::Numeric;
        type.scalarType = shader::ScalarType::F32;
        type.rows = components;
        return type;
    }

    bool BuildSurfaceModule(const bool sampled, material::MaterialIrModule& module)
    {
        const auto stage = shader::StageBit(shader::ShaderStage::Fragment);
        const material::MaterialSlangSymbol inputs[]{{1, "uv"}, {2, "worldNormal"}, {3, "viewRelativePosition"}};
        const material::MaterialSlangSymbol outputs[]{{100, "baseColor"}, {101, "normalTS"}, {102, "roughness"},
                                                      {103, "metallic"}, {104, "emissive"}, {105, "opacityCutoff"}};
        const u8 widths[]{4, 3, 1, 1, 3, 1};
        material::MaterialIrBuilder builder;
        builder.Reset(vanguard::crypto::Sha256(sampled ? "VanguardStaticSurfaceSampled" : "VanguardStaticSurfaceParameters",
                                                sampled ? 28 : 31));
        for (u32 index = 0; index < 6; ++index)
        {
            material::MaterialIrValueBuildDescription value;
            value.kind = material::MaterialIrValueKind::DynamicParameter;
            value.type = Float(widths[index]);
            value.legalStages = stage;
            value.semantic = 200 + index;
            material::MaterialIrValueId result;
            if (builder.AddValue(value, result) != material::MaterialIrResult::Success)
                return false;
            if (sampled && index == 0)
            {
                material::MaterialIrValueId texture, sampler, uv, color;
                value.type.kind = material::MaterialIrTypeKind::Texture;
                value.type.textureDimension = material::MaterialIrTextureDimension::D2;
                value.semantic = 300;
                if (builder.AddValue(value, texture) != material::MaterialIrResult::Success)
                    return false;
                value.type = {};
                value.type.kind = material::MaterialIrTypeKind::Sampler;
                value.semantic = 301;
                if (builder.AddValue(value, sampler) != material::MaterialIrResult::Success)
                    return false;
                value.kind = material::MaterialIrValueKind::DomainInput;
                value.type = Float(2);
                value.semantic = inputs[0].semantic;
                if (builder.AddValue(value, uv) != material::MaterialIrResult::Success)
                    return false;
                const material::MaterialIrValueId sampleOperands[]{texture, sampler, uv};
                value = {};
                value.kind = material::MaterialIrValueKind::Instruction;
                value.opcode = material::MaterialIrOpcode::TextureSample;
                value.type = Float(4);
                value.legalStages = stage;
                value.operands = sampleOperands;
                if (builder.AddValue(value, color) != material::MaterialIrResult::Success)
                    return false;
                const material::MaterialIrValueId tintOperands[]{color, result};
                value.opcode = material::MaterialIrOpcode::Multiply;
                value.operands = tintOperands;
                if (builder.AddValue(value, result) != material::MaterialIrResult::Success)
                    return false;
            }
            if (builder.AddOutput({outputs[index].semantic, result, stage}) != material::MaterialIrResult::Success)
                return false;
        }
        containers::DynamicArray<material::MaterialIrDiagnostic> irDiagnostics(memory::pools::Tools::GetInstance());
        return builder.Finalize(module, irDiagnostics) == material::MaterialIrResult::Success;
    }

    bool CompileSurface(sht::ShaderCompiler& compiler, const bool sampled,
                        const containers::ArraySpan<const u8> prefix, const containers::ArraySpan<const u8> suffix)
    {
        const auto fragmentStage = shader::StageBit(shader::ShaderStage::Fragment);
        const material::MaterialSlangSymbol inputs[]{{1, "uv"}, {2, "worldNormal"}, {3, "viewRelativePosition"}};
        const material::MaterialSlangSymbol outputs[]{{100, "baseColor"}, {101, "normalTS"}, {102, "roughness"},
                                                      {103, "metallic"}, {104, "emissive"}, {105, "opacityCutoff"}};
        material::MaterialSlangDomain domain;
        domain.stableName = "VanguardStaticSurface";
        domain.schemaVersion = 1;
        domain.legalStages = fragmentStage;
        domain.inputTypeName = "StaticSurfaceInput";
        domain.outputTypeName = "StaticSurfaceOutput";
        domain.parameterTypeName = "StaticSurfaceParameters";
        domain.resourceTypeName = "StaticSurfaceResources";
        domain.evaluationFunctionName = "EvaluateStaticSurface";
        domain.inputs = inputs;
        domain.outputs = outputs;
        domain.prefix = prefix;
        domain.suffix = suffix;

        material::MaterialIrModule module;
        if (!BuildSurfaceModule(sampled, module))
        {
            std::fprintf(stderr, "surface IR construction failed\n");
            return false;
        }
        containers::DynamicArray<u8> probe(memory::pools::Tools::GetInstance());
        const material::MaterialSlangResult generated = material::GenerateMaterialSlangProbe(module, domain, probe);
        if (generated != material::MaterialSlangResult::Success)
        {
            std::fprintf(stderr, "surface generation failed: %s\n", material::ToString(generated));
            return false;
        }

        const char* fragments[]{"StaticSurfaceGBufferMain", "StaticSurfaceDepthMain",
                                "StaticSurfaceMaskedGBufferMain", "StaticSurfaceMaskedDepthMain"};
        for (const sht::Target target : {sht::Target::D3D12Dxil, sht::Target::VulkanSpirV})
        {
            for (const char* fragment : fragments)
            {
                const sht::EntryPoint entries[]{{"StaticSurfaceVertexMain", shader::ShaderStage::Vertex},
                                                  {fragment, shader::ShaderStage::Fragment}};
                const char* searchPaths[]{ShaderDirectory};
                sht::CompileRequest request;
                request.sourceName = "static_surface.generated.vsl";
                request.moduleName = sampled ? "static_surface_sampled" : "static_surface_parameters";
                request.source = probe;
                request.entryPoints = entries;
                request.searchPaths = searchPaths;
                request.searchPathCount = 1;
                request.settings.target = target;
                sht::CompileOutput reflection;
                const sht::Result reflected = compiler.Reflect(request, reflection);
                if (reflected != sht::Result::Success)
                {
                    std::fprintf(stderr, "surface reflection failed: %s\n%s\n", sht::ToString(reflected), reflection.GetDiagnostics());
                    return false;
                }
                containers::DynamicArray<u8> source(memory::pools::Tools::GetInstance());
                const material::MaterialSlangResult finalized = material::FinalizeMaterialSlangSource(probe, reflection, source);
                if (finalized != material::MaterialSlangResult::Success)
                {
                    std::fprintf(stderr, "surface finalization failed: %s\n", material::ToString(finalized));
                    return false;
                }
                sht::CompileOutput compiled;
                const std::string label = std::string(sampled ? "surface-sampled-" : "surface-parameters-") + fragment;
                if (!Compile(compiler, label.c_str(), "static_surface.generated.vsl", source, entries, target, compiled) || !compiled.HasMaterialContract())
                    return false;
            }
        }
        return true;
    }
}

int main()
{
    const bool memoryInitialized = memory::Initialize();
    if (!memoryInitialized)
    {
        std::fprintf(stderr, "memory initialization failed\n");
        return 2;
    }
    const bool diagnosticsInitialized = diagnostics::Initialize(diagnostics::Mode::Synchronous, "shaderCompileCheck");
    if (!diagnosticsInitialized)
    {
        std::fprintf(stderr, "diagnostics initialization failed\n");
        return 2;
    }
    const bool containersInitialized = containers::Initialize();
    if (!containersInitialized)
    {
        std::fprintf(stderr, "containers initialization failed\n");
        return 2;
    }
    const bool ioInitialized = vgio::Initialize();
    if (!ioInitialized)
    {
        std::fprintf(stderr, "I/O initialization failed\n");
        return 2;
    }

    sht::ShaderCompiler compiler;
    const sht::Result compilerInitialized = compiler.Initialize();
    if (compilerInitialized != sht::Result::Success)
    {
        std::fprintf(stderr, "shader compiler initialization failed: %s\n", sht::ToString(compilerInitialized));
        return 3;
    }

    const sht::EntryPoint visibilityEntries[]{
        {"CullGpuSceneCandidates", shader::ShaderStage::Compute},
        {"CountGpuSceneGeometryWork", shader::ShaderStage::Compute},
        {"ScatterGpuSceneGeometryWork", shader::ShaderStage::Compute},
        {"ScanGpuSceneGeometryWorkBlocks", shader::ShaderStage::Compute},
        {"PrefixGpuSceneGeometryWorkBlocks", shader::ShaderStage::Compute},
        {"ResolveGpuSceneGeometryWorkOffsets", shader::ShaderStage::Compute},
        {"CountGpuSceneGeometryBins", shader::ShaderStage::Compute},
        {"ScanGpuSceneGeometryBinBlocks", shader::ShaderStage::Compute},
        {"PrefixGpuSceneGeometryBinBlocks", shader::ShaderStage::Compute},
        {"ResolveGpuSceneGeometryBinOffsets", shader::ShaderStage::Compute},
        {"ScatterGpuSceneGeometryInstances", shader::ShaderStage::Compute},
        {"InitializeGpuSceneGeometryShellRanges", shader::ShaderStage::Compute},
        {"BuildGpuSceneGeometryIndirectArguments", shader::ShaderStage::Compute}};
    const sht::EntryPoint diagnosticEntries[]{{"GeometryDiagnosticsMain", shader::ShaderStage::Compute}};
    const sht::EntryPoint copyEntries[]{{"CopyVertexMain", shader::ShaderStage::Vertex}, {"CopyFragmentMain", shader::ShaderStage::Fragment}};
    const sht::EntryPoint visualizationEntries[]{{"GeometryVisualizationVertexMain", shader::ShaderStage::Vertex},
                                                   {"GeometryVisualizationFragmentMain", shader::ShaderStage::Fragment}};
    const sht::EntryPoint lightingEntries[]{{"DirectionalDiffuseVertexMain", shader::ShaderStage::Vertex},
                                              {"DirectionalDiffuseFragmentMain", shader::ShaderStage::Fragment}};

    bool success = CompileComputeEntries(compiler, "gpu_scene_visibility.vsl", visibilityEntries) &&
                   CompileFile(compiler, "geometry_diagnostics.vsl", "geometry_diagnostics", diagnosticEntries) &&
                   CompileFile(compiler, "fullscreen_copy.vsl", "fullscreen_copy", copyEntries) &&
                   CompileFile(compiler, "geometry_visualization.vsl", "geometry_visualization", visualizationEntries) &&
                   CompileFile(compiler, "directional_diffuse.vsl", "directional_diffuse", lightingEntries);

    std::vector<u8> prefix, suffix;
    success = success && Read("static_surface_prefix.vsl", prefix) && Read("static_surface_suffix.vsl", suffix);
    if (success)
    {
        const containers::ArraySpan<const u8> prefixSpan{prefix.data(), static_cast<u32>(prefix.size())};
        const containers::ArraySpan<const u8> suffixSpan{suffix.data(), static_cast<u32>(suffix.size())};
        success = CompileSurface(compiler, false, prefixSpan, suffixSpan) && CompileSurface(compiler, true, prefixSpan, suffixSpan);
    }

    compiler.Shutdown();
    vgio::Shutdown();
    diagnostics::Shutdown();
    return success ? 0 : 1;
}
