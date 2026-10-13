#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
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
            // MaterialTools owns a portable VPPL fixture, not renderer target/depth policy.
            // Keep concrete formats and depth convention renderer-owned; the native
            // engine proof supplies the selected renderer's canonical attachments.
            const u32 fragmentOutputCount = shader.GetFragmentOutputs().Size();
            if ((depth && fragmentOutputCount != 0) || (!depth && fragmentOutputCount == 0))
                return false;
            description.graphics.blend.attachmentCount = fragmentOutputCount;
            description.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Deferred;
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
        const u8 widths[]{4, 3, 1, 1, 3, 1, 3, 1};
        const MaterialSlangDomain domain = MakeImportedSurfaceSlangDomain(prefix, suffix);
        const auto techniques = GetImportedSurfaceTechniques();
        if (GetImportedSurfaceDomainId() != shaders::HashInterfaceName("VanguardStaticSurface") ||
            domain.schemaVersion != ImportedSurfaceMaterialContractVersion || domain.inputs.Count() != 3 || domain.outputs.Count() != 8 ||
            techniques.Count() != 3 || techniques[0].name != ImportedSurfaceDepthPrepassTechnique ||
            techniques[1].name != ImportedSurfaceOpaqueTechnique || techniques[2].name != ImportedSurfaceTransparentTechnique ||
            techniques[0].pipelineTemplate.ExpectedType() != MaterialPipelineInputResourceType ||
            techniques[1].pipelineTemplate.ExpectedType() != MaterialPipelineInputResourceType ||
            techniques[2].pipelineTemplate.ExpectedType() != MaterialPipelineInputResourceType ||
            std::strcmp(techniques[0].opaqueFragmentEntryPoint, "StaticSurfaceDepthMain") != 0 ||
            std::strcmp(techniques[0].maskedFragmentEntryPoint, "StaticSurfaceMaskedDepthMain") != 0 ||
            std::strcmp(techniques[1].opaqueFragmentEntryPoint, "StaticSurfaceGBufferMain") != 0 ||
            std::strcmp(techniques[1].maskedFragmentEntryPoint, "StaticSurfaceMaskedGBufferMain") != 0 ||
            std::strcmp(techniques[2].opaqueFragmentEntryPoint, "StaticSurfaceTransparentMain") != 0)
            return false;

        ImportedSurfaceMaterialContext importedContext;
        ImportedSurfaceMaterialContextDescription contextDescription;
        contextDescription.prefix = prefix;
        contextDescription.suffix = suffix;
        contextDescription.gpuSceneTypes = gpuTypes;
        contextDescription.target = assets::TargetPlatform::WindowsD3D12;
        contextDescription.compileSettings.target = shader_tools::Target::D3D12Dxil;
        if (!importedContext.Initialize(contextDescription) || !importedContext.IsInitialized() ||
            importedContext.GetSourceProviderFingerprint().IsEmpty())
            return false;
        MaterialArtifactCompilerConfig importedCompilerConfig;
        if (!importedContext.ConfigureArtifactCompiler(importedCompilerConfig) || importedCompilerConfig.loadSource == nullptr ||
            importedCompilerConfig.loadSourceUserData != &importedContext || importedCompilerConfig.sourceProviderFingerprint.IsEmpty())
            return false;
        shader_tools::LoadedSource loadedGpuTypes;
        if (!importedCompilerConfig.loadSource("generated/gpu_scene_types.hlsli", loadedGpuTypes,
                                               importedCompilerConfig.loadSourceUserData) ||
            loadedGpuTypes.content.Count() != gpuTypes.Size() ||
            std::memcmp(loadedGpuTypes.content.Data(), gpuTypes.Data(), gpuTypes.Size()) != 0)
            return false;

        const resources::ResourceReference importedMaterial(
            resources::ResourcePath::FromString("materials/tests/imported_surface_context.vmat"), materials::MaterialResourceType);
        ImportedSurfaceCanonicalIntermediates firstIntermediates;
        ImportedSurfaceCanonicalIntermediates secondIntermediates;
        if (!DeriveImportedSurfaceCanonicalIntermediates(importedMaterial, firstIntermediates) ||
            !DeriveImportedSurfaceCanonicalIntermediates(importedMaterial, secondIntermediates) ||
            firstIntermediates.programSource != secondIntermediates.programSource || firstIntermediates.shader != secondIntermediates.shader ||
            firstIntermediates.depthProgramSource != secondIntermediates.depthProgramSource || firstIntermediates.depthShader != secondIntermediates.depthShader ||
            firstIntermediates.materialSource != secondIntermediates.materialSource || firstIntermediates.depthPipeline != secondIntermediates.depthPipeline ||
            firstIntermediates.opaquePipeline != secondIntermediates.opaquePipeline ||
            firstIntermediates.transparentPipeline != secondIntermediates.transparentPipeline)
            return false;
        pipelines::BuildDescription depthRecipe;
        pipelines::BuildDescription opaqueRecipe;
        depthRecipe.kind = pipelines::PipelineKind::Graphics;
        opaqueRecipe.kind = pipelines::PipelineKind::Graphics;
        const resources::ResourceReference colorTexture(resources::ResourcePath::FromString("materials/tests/context_color.vtex"), textures::TextureResourceType);
        const resources::ResourceReference normalTexture(resources::ResourcePath::FromString("materials/tests/context_normal.vtex"), textures::TextureResourceType);
        const resources::ResourceReference metallicTexture(resources::ResourcePath::FromString("materials/tests/context_metallic.vtex"), textures::TextureResourceType);
        const resources::ResourceReference roughnessTexture(resources::ResourcePath::FromString("materials/tests/context_roughness.vtex"), textures::TextureResourceType);
        const resources::ResourceReference emissiveTexture(resources::ResourcePath::FromString("materials/tests/context_emissive.vtex"), textures::TextureResourceType);
        const resources::ResourceReference opacityTexture(resources::ResourcePath::FromString("materials/tests/context_opacity.vtex"), textures::TextureResourceType);
        const ImportedSurfaceTextureBinding contextBindings[]{
            {ImportedSurfaceTextureSemantic::Opacity, 0, opacityTexture, resources::DependencyKind::Soft},
            {ImportedSurfaceTextureSemantic::Roughness, 0, roughnessTexture, resources::DependencyKind::Required},
            {ImportedSurfaceTextureSemantic::BaseColor, 0, colorTexture, resources::DependencyKind::Required},
            {ImportedSurfaceTextureSemantic::Emissive, 0, emissiveTexture, resources::DependencyKind::Optional},
            {ImportedSurfaceTextureSemantic::Normal, 0, normalTexture, resources::DependencyKind::Required},
            {ImportedSurfaceTextureSemantic::Metallic, 0, metallicTexture, resources::DependencyKind::Optional}};
        ImportedSurfaceMaterialInput defaultImportedMaterial;
        defaultImportedMaterial.baseColor[0] = 0.8f;
        defaultImportedMaterial.baseColor[1] = 0.7f;
        defaultImportedMaterial.baseColor[2] = 0.6f;
        defaultImportedMaterial.emissive[0] = 0.4f;
        defaultImportedMaterial.emissive[1] = 0.3f;
        defaultImportedMaterial.emissive[2] = 0.2f;
        defaultImportedMaterial.metallic = 0.7f;
        defaultImportedMaterial.roughness = 0.3f;
        defaultImportedMaterial.opacity = 0.9f;
        defaultImportedMaterial.coverage = ImportedSurfaceCoverageMode::Masked;
        defaultImportedMaterial.textures = contextBindings;
        MaterialCanonicalBuildSet importedBuild;
        if (BuildImportedSurfaceCanonicalInputs(importedContext, defaultImportedMaterial, importedMaterial, depthRecipe, opaqueRecipe,
                                                importedBuild) != MaterialCanonicalResult::Success ||
            !importedBuild.IsValid() || importedBuild.Programs().Count() != 2 || importedBuild.Pipelines().Count() != 2 ||
            importedBuild.Program().output != firstIntermediates.shader || importedBuild.Material().output != importedMaterial)
            return false;

        // BLEND must publish only the transparent phase, never the opaque
        // GBuffer or depth-prepass techniques. The selected shader is its own
        // forward fragment entrypoint and the pipeline has a unique identity.
        ImportedSurfaceMaterialInput lensMaterial = defaultImportedMaterial;
        lensMaterial.coverage = ImportedSurfaceCoverageMode::Blended;
        lensMaterial.opacity = 0.109f;
        lensMaterial.ior = 1000.0f;
        lensMaterial.specular[0] = lensMaterial.specular[1] = lensMaterial.specular[2] = 0.2f;
        pipelines::BuildDescription transparentRecipe;
        transparentRecipe.kind = pipelines::PipelineKind::Graphics;
        transparentRecipe.graphics.blend.attachmentCount = 1;
        transparentRecipe.graphics.blend.attachments[0].blendEnable = true;
        transparentRecipe.graphics.depthStencil.depthTest = true;
        transparentRecipe.graphics.depthStencil.depthWrite = false;
        const resources::ResourceReference lensOutput(
            resources::ResourcePath::FromString("materials/tests/imported_surface_lens.vmat"), materials::MaterialResourceType);
        ImportedSurfaceCanonicalIntermediates lensIntermediates;
        MaterialCanonicalBuildSet blendedBuild;
        if (!DeriveImportedSurfaceCanonicalIntermediates(lensOutput, lensIntermediates) ||
            BuildImportedSurfaceCanonicalInputs(importedContext, lensMaterial, lensOutput, depthRecipe, opaqueRecipe,
                                                blendedBuild, nullptr, nullptr, {}, {}, &transparentRecipe) != MaterialCanonicalResult::Success ||
            !blendedBuild.IsValid() || blendedBuild.Programs().Count() != 1u || blendedBuild.Pipelines().Count() != 1u ||
            blendedBuild.Program().output != lensIntermediates.shader || blendedBuild.Pipelines()[0].output != lensIntermediates.transparentPipeline)
            return false;

        assets::BuildSystem importedBuildSystem;
        MaterialArtifactCompilers importedCompilers;
        bool importedProgramBuilt = importedBuildSystem.Initialize() && importedCompilers.Initialize(importedCompilerConfig) &&
                                    importedCompilers.Register(importedBuildSystem) == assets::Result::Success;
        assets::BuildPlan importedMaterialPlan;
        if (importedProgramBuilt)
            importedProgramBuilt = importedBuildSystem.Prepare(importedBuild.Material(), importedMaterialPlan) == assets::Result::Success;
        const auto hasTextureDependency = [&importedMaterialPlan](const resources::ResourceReference resource,
                                                                 const assets::DependencyRequirement requirement) noexcept
        {
            for (const assets::BuildDependency& dependency : importedMaterialPlan.GetDependencies())
                if (dependency.identity == resource && dependency.role == assets::DependencyRole::Generated &&
                    dependency.requirement == requirement)
                    return true;
            return false;
        };
        importedProgramBuilt = importedProgramBuilt &&
                               hasTextureDependency(colorTexture, assets::DependencyRequirement::Required) &&
                               hasTextureDependency(normalTexture, assets::DependencyRequirement::Required) &&
                               hasTextureDependency(metallicTexture, assets::DependencyRequirement::Optional) &&
                               hasTextureDependency(roughnessTexture, assets::DependencyRequirement::Required) &&
                               hasTextureDependency(emissiveTexture, assets::DependencyRequirement::Optional) &&
                               hasTextureDependency(opacityTexture, assets::DependencyRequirement::Soft);
        assets::BuildOutput importedProgramOutput;
        if (importedProgramBuilt)
            importedProgramBuilt = importedBuildSystem.Build(importedBuild.Program(), importedProgramOutput) == assets::Result::Success &&
                                   !importedProgramOutput.artifacts.Empty();
        if (importedCompilers.IsInitialized())
            importedProgramBuilt = importedCompilers.Shutdown() && importedProgramBuilt;
        if (importedBuildSystem.IsInitialized())
            importedProgramBuilt = importedBuildSystem.Shutdown() && importedProgramBuilt;
        if (!importedProgramBuilt)
            return false;

        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        bool success = true;
        bool productionSchemaValidated = false;
        for (u32 sampled = 0; sampled < 2 && success; ++sampled)
        {
            MaterialIrBuilder builder;
            builder.Reset(crypto::Sha256("VanguardStaticSurface", 21));
            for (u32 index = 0; index < 8; ++index)
            {
                MaterialIrValueBuildDescription value;
                value.kind = MaterialIrValueKind::DynamicParameter;
                value.type = Float(widths[index]);
                value.legalStages = stage;
                value.semantic = ImportedSurfaceBaseColorParameterSemantic + index;
                MaterialIrValueId parameter;
                if (builder.AddValue(value, parameter) != MaterialIrResult::Success)
                    return false;
                if (sampled != 0 && index == 0)
                {
                    MaterialIrValueId texture, sampler, uv, color;
                    value.type.kind = MaterialIrTypeKind::Texture;
                    value.type.textureDimension = MaterialIrTextureDimension::D2;
                    value.semantic = ImportedSurfaceBaseColorTextureSemantic;
                    if (builder.AddValue(value, texture) != MaterialIrResult::Success)
                        return false;
                    value.type = {};
                    value.type.kind = MaterialIrTypeKind::Sampler;
                    value.semantic = ImportedSurfaceSamplerSemantic;
                    if (builder.AddValue(value, sampler) != MaterialIrResult::Success)
                        return false;
                    value.kind = MaterialIrValueKind::DomainInput;
                    value.type = Float(2);
                    value.semantic = ImportedSurfaceUvInputSemantic;
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
                if (builder.AddOutput({domain.outputs[index].semantic, parameter, stage}) != MaterialIrResult::Success)
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
                    if (!productionSchemaValidated)
                    {
                        MaterialNodeRegistry nodes;
                        MaterialDomainRegistry domains;
                        if (!RegisterImportedSurfaceMaterialSchema(reflection.GetMaterialDomain(), nodes, domains) ||
                            !nodes.Freeze() || !domains.Freeze())
                            return false;

                        const resources::ResourceReference importedColorTexture(resources::ResourcePath::FromString("materials/tests/imported_color.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference importedNormalTexture(resources::ResourcePath::FromString("materials/tests/imported_normal.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference packedTexture(resources::ResourcePath::FromString("materials/tests/imported_metallic_roughness.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference importedEmissiveTexture(resources::ResourcePath::FromString("materials/tests/imported_emissive.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference importedOpacityTexture(resources::ResourcePath::FromString("materials/tests/imported_opacity.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference specularTexture(resources::ResourcePath::FromString("materials/tests/imported_specular.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference specularStrengthTexture(resources::ResourcePath::FromString("materials/tests/imported_specular_strength.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference occlusionTexture(resources::ResourcePath::FromString("materials/tests/imported_occlusion.vtex"), textures::TextureResourceType);
                        const resources::ResourceReference uv1RoughnessTexture(resources::ResourcePath::FromString("materials/tests/imported_uv1_roughness.vtex"), textures::TextureResourceType);
                        const ImportedSurfaceTextureBinding orderedBindings[]{
                            {ImportedSurfaceTextureSemantic::BaseColor, 0, importedColorTexture, resources::DependencyKind::Required},
                            {ImportedSurfaceTextureSemantic::Normal, 0, importedNormalTexture, resources::DependencyKind::Optional},
                            {ImportedSurfaceTextureSemantic::MetallicRoughness, 0, packedTexture, resources::DependencyKind::Required},
                            {ImportedSurfaceTextureSemantic::Emissive, 0, importedEmissiveTexture, resources::DependencyKind::Soft},
                            {ImportedSurfaceTextureSemantic::Opacity, 0, importedOpacityTexture, resources::DependencyKind::Optional},
                            {ImportedSurfaceTextureSemantic::Specular, 0, specularTexture, resources::DependencyKind::Required},
                            {ImportedSurfaceTextureSemantic::SpecularStrength, 0, specularStrengthTexture, resources::DependencyKind::Required},
                            {ImportedSurfaceTextureSemantic::Occlusion, 0, occlusionTexture, resources::DependencyKind::Required},
                            {ImportedSurfaceTextureSemantic::Roughness, 1, uv1RoughnessTexture, resources::DependencyKind::Required}};
                        const ImportedSurfaceTextureBinding reversedBindings[]{orderedBindings[8], orderedBindings[7], orderedBindings[6],
                                                                              orderedBindings[5], orderedBindings[4], orderedBindings[3],
                                                                              orderedBindings[2], orderedBindings[1], orderedBindings[0]};
                        ImportedSurfaceMaterialInput orderedInput;
                        orderedInput.baseColor[0] = 0.25f;
                        orderedInput.baseColor[1] = 0.5f;
                        orderedInput.baseColor[2] = 0.75f;
                        orderedInput.baseColor[3] = 0.8f;
                        orderedInput.emissive[0] = 0.1f;
                        orderedInput.emissive[1] = 0.2f;
                        orderedInput.emissive[2] = 0.3f;
                        orderedInput.specular[0] = 0.5f;
                        orderedInput.specularFactor = 0.6f;
                        orderedInput.ior = 1.8f;
                        orderedInput.occlusionStrength = 0.35f;
                        orderedInput.metallic = 0.65f;
                        orderedInput.roughness = 0.35f;
                        orderedInput.opacity = 0.75f;
                        orderedInput.opacityCutoff = 0.4f;
                        orderedInput.twoSided = true;
                        orderedInput.coverage = ImportedSurfaceCoverageMode::Masked;
                        orderedInput.textures = orderedBindings;
                        ImportedSurfaceMaterialInput reversedInput = orderedInput;
                        reversedInput.textures = reversedBindings;
                        const resources::ResourceReference graphIdentity(
                            resources::ResourcePath::FromString("materials/tests/imported_surface.graph"), MaterialValueInputResourceType);
                        ImportedSurfaceMaterialMapping orderedMapping;
                        ImportedSurfaceMaterialMapping reversedMapping;
                        if (BuildImportedSurfaceMaterialMapping(orderedInput, graphIdentity, orderedMapping) != ImportedSurfaceMappingResult::Success ||
                            BuildImportedSurfaceMaterialMapping(reversedInput, graphIdentity, reversedMapping) != ImportedSurfaceMappingResult::Success ||
                            !orderedMapping.IsValid() || !reversedMapping.IsValid() || orderedMapping.GetResources().Count() != 8 ||
                            reversedMapping.GetResources().Count() != 8 || !orderedMapping.IsTwoSided() ||
                            orderedMapping.GetCoverageMode() != ImportedSurfaceCoverageMode::Masked ||
                            HasFlag(orderedMapping.GetWarnings(), ImportedSurfaceMappingWarning::SpecularUnsupported) ||
                            HasFlag(orderedMapping.GetWarnings(), ImportedSurfaceMappingWarning::TextureSemanticUnsupported) ||
                            !HasFlag(orderedMapping.GetWarnings(), ImportedSurfaceMappingWarning::UvSetUnsupported))
                            return false;
                        const u64 expectedResourceSemantics[]{ImportedSurfaceBaseColorTextureSemantic, ImportedSurfaceNormalTextureSemantic,
                                                              ImportedSurfaceMetallicRoughnessTextureSemantic, ImportedSurfaceEmissiveTextureSemantic,
                                                              ImportedSurfaceOpacityTextureSemantic, ImportedSurfaceSpecularTextureSemantic,
                                                              ImportedSurfaceSpecularStrengthTextureSemantic, ImportedSurfaceOcclusionTextureSemantic};
                        const resources::ResourceReference expectedResources[]{importedColorTexture, importedNormalTexture, packedTexture,
                                                                               importedEmissiveTexture, importedOpacityTexture, specularTexture,
                                                                               specularStrengthTexture, occlusionTexture};
                        for (u32 resourceIndex = 0; resourceIndex < 8; ++resourceIndex)
                        {
                            const MaterialCanonicalResourceValue& orderedResource = orderedMapping.GetResources()[resourceIndex];
                            const MaterialCanonicalResourceValue& reversedResource = reversedMapping.GetResources()[resourceIndex];
                            if (orderedResource.semantic != expectedResourceSemantics[resourceIndex] ||
                                orderedResource.resource != expectedResources[resourceIndex] ||
                                reversedResource.semantic != orderedResource.semantic || reversedResource.resource != orderedResource.resource ||
                                reversedResource.dependency != orderedResource.dependency)
                                return false;
                        }

                        MaterialFrontend frontend;
                        MaterialSourceMap orderedSourceMap;
                        MaterialSourceMap reversedSourceMap;
                        MaterialIrModule orderedModule;
                        MaterialIrModule reversedModule;
                        containers::DynamicArray<MaterialIrDiagnostic> orderedDiagnostics(memory::pools::Tools::GetInstance());
                        containers::DynamicArray<MaterialIrDiagnostic> reversedDiagnostics(memory::pools::Tools::GetInstance());
                        const MaterialFrontendConfig frontendConfig{&nodes, &domains};
                        if (frontend.Lower(orderedMapping.GetGraph(), frontendConfig, orderedModule, orderedSourceMap, orderedDiagnostics) !=
                                MaterialFrontendResult::Success ||
                            frontend.Lower(reversedMapping.GetGraph(), frontendConfig, reversedModule, reversedSourceMap, reversedDiagnostics) !=
                                MaterialFrontendResult::Success ||
                            orderedModule.GetContentFingerprint() != reversedModule.GetContentFingerprint())
                            return false;
                        productionSchemaValidated = true;
                    }
                    if (FinalizeMaterialSlangSource(probe, reflection, finalSource) != MaterialSlangResult::Success)
                        return false;
                    request.source = finalSource;
                    const auto compileResult = compiler.Compile(request, compiled);
                    success = compileResult == shader_tools::Result::Success && compiled.HasMaterialContract() && compiled.GetStages().Size() == 2 && compiled.GetFragmentOutputs().Size() == (depth ? 0u : 5u) &&
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
