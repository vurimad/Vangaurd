#include <vanguard/assets/asset_index.hpp>
#include <vanguard/assets/package_planner.hpp>
#include <vanguard/assets/package_set_assembly.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/scene_components.hpp>
#include <vanguard/entities/static_mesh_component.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/game_input/mapping_resource.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/material_tools/material_ir.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/pipelines/renderer_catalog.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/renderer_feature_programs.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/shader_tools/shader_asset_compiler.hpp>
#include <vanguard/shaders/shaders.hpp>
#include <vanguard/world/cells.hpp>
#include <vanguard/world/worlds.hpp>

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    namespace assets = vanguard::assets;
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace diagnostics = vanguard::diagnostics;
    namespace entities = vanguard::entities;
    namespace filesystem = vanguard::filesystem;
    namespace gameInput = vanguard::game_input;
    namespace materialTools = vanguard::material_tools;
    namespace materials = vanguard::materials;
    namespace memory = vanguard::memory;
    namespace meshes = vanguard::meshes;
    namespace packages = vanguard::packages;
    namespace pipelines = vanguard::pipelines;
    namespace prefabs = vanguard::prefabs;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace serialization = vanguard::serialization;
    namespace shaderTools = vanguard::shader_tools;
    namespace shaders = vanguard::shaders;
    namespace world = vanguard::world;

    constexpr vanguard::u64 GameId = 0x56414e4755415244ull;
    constexpr vanguard::u64 WorldId = 0x444556574f524c44ull;
    constexpr vanguard::u64 CellId = 0x44455643454c4c01ull;
    constexpr resources::ResourceTypeId InputSourceType = serialization::MakeFourCC('I', 'N', 'S', 'R');
    constexpr resources::ResourceTypeId PrefabSourceType = serialization::MakeFourCC('P', 'F', 'S', 'R');
    constexpr resources::ResourceTypeId CellSourceType = serialization::MakeFourCC('C', 'E', 'S', 'R');
    constexpr resources::ResourceTypeId WorldSourceType = serialization::MakeFourCC('W', 'L', 'S', 'R');
    constexpr resources::ResourceTypeId CatalogSourceType = serialization::MakeFourCC('R', 'C', 'S', 'R');
    constexpr vanguard::u32 FeatureCount = sizeof(rendering::RendererFeaturePrograms) / sizeof(rendering::RendererFeaturePrograms[0]);
    constexpr resources::ResourceTypeId PipelineSourceType = serialization::MakeFourCC('P', 'L', 'S', 'R');
    constexpr resources::ResourceTypeId MaterialSourceType = serialization::MakeFourCC('M', 'A', 'S', 'R');
    constexpr resources::ResourceTypeId MeshSourceType = serialization::MakeFourCC('M', 'E', 'S', 'R');
    constexpr vanguard::u64 SurfaceDepthProgram = 0x4253544450544801ull;
    constexpr vanguard::u64 SurfaceOpaqueProgram = 0x4253544f50415101ull;
    constexpr vanguard::u64 SurfaceParameterBase = 200;
    constexpr char SurfacePermutationName[] = "bootstrap-static-surface-v1";

    [[nodiscard]] bool Fail(const char* const stage) noexcept
    {
        std::fprintf(stderr, "bootstrapImage: %s failed\n", stage);
        return false;
    }

    struct StoredArtifact final
    {
        StoredArtifact() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        resources::ResourceReference resource;
        const char* path = nullptr;
        containers::String ownedPath;
        assets::ArtifactSetKey origin;
        containers::DynamicArray<vanguard::u8> bytes;
    };

    struct BootstrapContent final
    {
        BootstrapContent() noexcept
        {
            input.resource = Reference("input/default.vinput", gameInput::MappingResourceType);
            input.path = "input/default.vinput";
            prefab.resource = Reference("prefabs/bootstrap.vprefab", prefabs::PrefabResourceType);
            prefab.path = "prefabs/bootstrap.vprefab";
            mesh.resource = Reference("meshes/bootstrap_quad.vmesh", meshes::MeshResourceType);
            mesh.path = "meshes/bootstrap_quad.vmesh";
            material.resource = Reference("materials/bootstrap_surface.vmat", materials::MaterialResourceType);
            material.path = "materials/bootstrap_surface.vmat";
            depthShader.resource = Reference("materials/bootstrap_surface_depth.vshader", shaders::ShaderResourceType);
            depthShader.path = "materials/bootstrap_surface_depth.vshader";
            opaqueShader.resource = Reference("materials/bootstrap_surface_opaque.vshader", shaders::ShaderResourceType);
            opaqueShader.path = "materials/bootstrap_surface_opaque.vshader";
            depthPipeline.resource = Reference("materials/bootstrap_surface_depth.vppl", pipelines::PipelineResourceType);
            depthPipeline.path = "materials/bootstrap_surface_depth.vppl";
            opaquePipeline.resource = Reference("materials/bootstrap_surface_opaque.vppl", pipelines::PipelineResourceType);
            opaquePipeline.path = "materials/bootstrap_surface_opaque.vppl";
            cell.resource = Reference("world/bootstrap.vcell", world::CellResourceType);
            cell.path = "world/bootstrap.vcell";
            startupWorld.resource = Reference("world/bootstrap.vworld", world::WorldResourceType);
            startupWorld.path = "world/bootstrap.vworld";
            catalog.resource = Reference("engine/rendering/default.vrcat", pipelines::RendererCatalogResourceType);
            catalog.path = "engine/rendering/default.vrcat";
            for (vanguard::u32 index = 0; index < FeatureCount; ++index)
            {
                char path[192];
                std::snprintf(path, sizeof(path), "engine/rendering/%s.vshader", rendering::RendererFeaturePrograms[index].name);
                featureShaders[index].ownedPath = path;
                featureShaders[index].path = featureShaders[index].ownedPath.AsChar();
                featureShaders[index].resource = Reference(path, shaders::ShaderResourceType);
                std::snprintf(path, sizeof(path), "engine/rendering/%s.vppl", rendering::RendererFeaturePrograms[index].name);
                featurePipelines[index].ownedPath = path;
                featurePipelines[index].path = featurePipelines[index].ownedPath.AsChar();
                featurePipelines[index].resource = Reference(path, pipelines::PipelineResourceType);
            }
        }

        [[nodiscard]] static resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
        {
            return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
        }

        [[nodiscard]] StoredArtifact* Find(const resources::ResourceReference resource) noexcept
        {
            if (resource == catalog.resource)
                return &catalog;
            for (vanguard::u32 index = 0; index < FeatureCount; ++index)
            {
                if (resource == featureShaders[index].resource) return &featureShaders[index];
                if (resource == featurePipelines[index].resource) return &featurePipelines[index];
            }
            for (StoredArtifact* const artifact : {&input, &prefab, &mesh, &material, &depthShader, &opaqueShader,
                                                    &depthPipeline, &opaquePipeline, &cell, &startupWorld})
                if (artifact->resource == resource)
                    return artifact;
            return nullptr;
        }

        [[nodiscard]] const StoredArtifact* Find(const resources::ResourceReference resource) const noexcept
        {
            return const_cast<BootstrapContent*>(this)->Find(resource);
        }

        StoredArtifact input;
        StoredArtifact prefab;
        StoredArtifact mesh;
        StoredArtifact material;
        StoredArtifact depthShader;
        StoredArtifact opaqueShader;
        StoredArtifact depthPipeline;
        StoredArtifact opaquePipeline;
        StoredArtifact cell;
        StoredArtifact startupWorld;
        StoredArtifact catalog;
        StoredArtifact featureShaders[FeatureCount];
        StoredArtifact featurePipelines[FeatureCount];
        filesystem::AbsolutePath shaderDirectory;
    };

    [[nodiscard]] bool ReadFile(const filesystem::AbsolutePath& path, containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        filesystem::Manager& files = filesystem::GetManager();
        const bool exists = files.FileExist(path);
        if (!exists)
            return false;
        const vanguard::u64 size = files.GetFileSize(path);
        if (size > ~vanguard::u32{0})
            return false;
        auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!file)
            return false;
        bytes.Resize(static_cast<vanguard::u32>(size));
        file->Serialize(bytes.Data(), bytes.Size());
        return !file->HasErrors();
    }

    [[nodiscard]] materialTools::MaterialIrType FloatType(const vanguard::u8 components) noexcept
    {
        materialTools::MaterialIrType type;
        type.kind = materialTools::MaterialIrTypeKind::Numeric;
        type.scalarType = shaders::ScalarType::F32;
        type.rows = components;
        return type;
    }

    [[nodiscard]] vanguard::u64 SurfaceParameterName(const vanguard::u64 semantic) noexcept
    {
        char fieldName[19]{};
        const int length = std::snprintf(fieldName, sizeof(fieldName), "p_%016llx", static_cast<unsigned long long>(semantic));
        if (length != 18)
            return 0;
        return shaders::HashInterfaceChildName(shaders::HashInterfaceName("StaticSurfaceParameters"), fieldName);
    }

    [[nodiscard]] bool BuildSurfaceIr(materialTools::MaterialIrModule& module) noexcept
    {
        constexpr vanguard::u8 widths[]{4, 3, 1, 1, 3, 1};
        const vanguard::u32 fragment = shaders::StageBit(shaders::ShaderStage::Fragment);
        materialTools::MaterialIrBuilder builder;
        builder.Reset(crypto::Sha256("VanguardStaticSurface", 21));
        for (vanguard::u32 index = 0; index < 6; ++index)
        {
            materialTools::MaterialIrValueBuildDescription value;
            value.kind = materialTools::MaterialIrValueKind::DynamicParameter;
            value.type = FloatType(widths[index]);
            value.legalStages = fragment;
            value.semantic = SurfaceParameterBase + index;
            materialTools::MaterialIrValueId parameter;
            const materialTools::MaterialIrResult addedValue = builder.AddValue(value, parameter);
            if (addedValue != materialTools::MaterialIrResult::Success)
                return false;
            const materialTools::MaterialIrResult addedOutput = builder.AddOutput({100 + index, parameter, fragment});
            if (addedOutput != materialTools::MaterialIrResult::Success)
                return false;
        }
        containers::DynamicArray<materialTools::MaterialIrDiagnostic> diagnostics(memory::pools::Tools::GetInstance());
        const materialTools::MaterialIrResult finalized = builder.Finalize(module, diagnostics);
        return finalized == materialTools::MaterialIrResult::Success;
    }

    [[nodiscard]] bool GenerateSurfaceSource(const BootstrapContent& content, const bool depth,
                                          containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        containers::DynamicArray<vanguard::u8> prefix(memory::pools::Tools::GetInstance());
        containers::DynamicArray<vanguard::u8> suffix(memory::pools::Tools::GetInstance());
        const bool prefixRead = ReadFile(content.shaderDirectory.AddFilePath("static_surface_prefix.vsl"), prefix);
        if (!prefixRead)
            return false;
        const bool suffixRead = ReadFile(content.shaderDirectory.AddFilePath("static_surface_suffix.vsl"), suffix);
        if (!suffixRead)
            return false;

        const materialTools::MaterialSlangSymbol inputs[]{{1, "uv"}, {2, "worldNormal"}, {3, "viewRelativePosition"}};
        const materialTools::MaterialSlangSymbol outputs[]{{100, "baseColor"}, {101, "normalTS"}, {102, "roughness"},
                                                           {103, "metallic"}, {104, "emissive"}, {105, "opacityCutoff"}};
        materialTools::MaterialSlangDomain domain;
        domain.stableName = "VanguardStaticSurface";
        domain.schemaVersion = 1;
        domain.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        domain.inputTypeName = "StaticSurfaceInput";
        domain.outputTypeName = "StaticSurfaceOutput";
        domain.parameterTypeName = "StaticSurfaceParameters";
        domain.resourceTypeName = "StaticSurfaceResources";
        domain.evaluationFunctionName = "EvaluateStaticSurface";
        domain.inputs = inputs;
        domain.outputs = outputs;
        domain.prefix = prefix;
        domain.suffix = suffix;

        materialTools::MaterialIrModule module;
        containers::DynamicArray<vanguard::u8> probe(memory::pools::Tools::GetInstance());
        const bool irBuilt = BuildSurfaceIr(module);
        if (!irBuilt)
            return false;
        const materialTools::MaterialSlangResult generated = materialTools::GenerateMaterialSlangProbe(module, domain, probe);
        if (generated != materialTools::MaterialSlangResult::Success)
            return false;

        const shaderTools::EntryPoint entries[]{{"StaticSurfaceVertexMain", shaders::ShaderStage::Vertex},
                                                 {depth ? "StaticSurfaceDepthMain" : "StaticSurfaceGBufferMain", shaders::ShaderStage::Fragment}};
        const char* searchPaths[]{content.shaderDirectory.AsChar()};
        shaderTools::CompileRequest request;
        request.sourceName = "bootstrap/static_surface.generated.vsl";
        request.moduleName = depth ? "bootstrap_static_surface_depth" : "bootstrap_static_surface_opaque";
        request.source = probe;
        request.entryPoints = entries;
        request.searchPaths = searchPaths;
        request.searchPathCount = 1;
        request.settings.target = shaderTools::Target::D3D12Dxil;
        shaderTools::ShaderCompiler compiler;
        if (compiler.Initialize() != shaderTools::Result::Success)
            return false;
        shaderTools::CompileOutput reflection;
        const shaderTools::Result reflected = compiler.Reflect(request, reflection);
        containers::DynamicArray<vanguard::u8> finalSource(memory::pools::Tools::GetInstance());
        const bool finalized = reflected == shaderTools::Result::Success &&
            materialTools::FinalizeMaterialSlangSource(probe, reflection, finalSource) == materialTools::MaterialSlangResult::Success;
        if (!finalized)
        {
            std::fprintf(stderr, "bootstrapImage: surface source finalization failed: %s\n%s\n",
                         shaderTools::ToString(reflected), reflection.GetDiagnostics());
        }
        bytes = std::move(finalSource);
        compiler.Shutdown();
        return finalized;
    }

    [[nodiscard]] bool BuildSurfacePipeline(const StoredArtifact& shaderArtifact, const StoredArtifact& pipelineArtifact, const bool depth,
                                            containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        filesystem::MemoryFileReader shaderReader(shaderArtifact.bytes, 0);
        shaders::ShaderFile shader;
        const shaders::Result shaderOpened = shader.Open(shaderReader);
        if (shaderOpened != shaders::Result::Success)
            return false;
        if (shader.GetMaterialContract() == nullptr)
            return false;
        const shaders::MaterialContract& contract = *shader.GetMaterialContract();
        const pipelines::ShaderReference shaderReference{shaderArtifact.resource.GetPath().Id(), shader.GetPermutation(),
                                                         shader.BindingLayoutFingerprint(), shader.GetPipelineInterfaceFingerprint(),
                                                         contract.domainFingerprint, contract.layoutFingerprint};
        const pipelines::VertexStream streams[]{{0, 48, pipelines::InputRate::PerVertex, 1},
                                                 {15, 16, pipelines::InputRate::PerInstance, 1}};
        const char* names[]{"POSITION", "NORMAL", "TANGENT", "TEXCOORD", "VG_DRAW", "VG_DRAW"};
        const vanguard::u32 offsets[]{0, 12, 24, 40, 0, 8};
        const pipelines::Format formats[]{pipelines::Format::R32G32B32Float, pipelines::Format::R32G32B32Float,
                                          pipelines::Format::R32G32B32A32Float, pipelines::Format::R32G32Float,
                                          pipelines::Format::R32G32UInt, pipelines::Format::R32G32UInt};
        containers::DynamicArray<pipelines::VertexAttribute> attributes(memory::pools::Tools::GetInstance());
        for (vanguard::u32 index = 0; index < 6; ++index)
        {
            bool found = false;
            for (const shaders::VertexInput& input : shader.GetVertexInputs())
            {
                const vanguard::u32 semanticIndex = index == 5 ? 1u : 0u;
                if (input.semantic != shaders::HashInterfaceName(names[index]) || input.semanticIndex != semanticIndex)
                    continue;
                pipelines::VertexAttribute attribute;
                attribute.semantic = input.semantic;
                attribute.semanticIndex = input.semanticIndex;
                attribute.location = input.location;
                attribute.streamBinding = index >= 4 ? 15 : 0;
                attribute.byteOffset = offsets[index];
                attribute.numericClass = input.numericClass;
                attribute.componentCount = input.componentCount;
                attribute.componentBits = 32;
                attribute.format = formats[index];
                std::memcpy(attribute.semanticName, names[index], std::strlen(names[index]) + 1u);
                attributes.PushBack(attribute);
                found = true;
                break;
            }
            if (!found)
                return false;
        }

        pipelines::BuildDescription description;
        description.name = pipelineArtifact.resource.GetPath().Id();
        description.shaders = {&shaderReference, 1};
        description.vertexStreams = streams;
        description.vertexAttributes = attributes;
        description.graphics.depthStencil.depthTest = true;
        description.graphics.depthStencil.depthWrite = true;
        description.graphics.depthStencil.depthCompare = pipelines::CompareOperation::LessEqual;
        description.graphics.blend.attachmentCount = depth ? 0 : 3;
        description.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Exact;
        description.graphics.exactAttachments.colorCount = depth ? 0 : 3;
        if (!depth)
        {
            description.graphics.exactAttachments.colors[0] = {pipelines::Format::R8G8B8A8UNorm, shaders::NumericClass::FloatingPoint};
            description.graphics.exactAttachments.colors[1] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
            description.graphics.exactAttachments.colors[2] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
        }
        description.graphics.exactAttachments.depthStencilFormat = pipelines::Format::D32Float;
        description.graphics.exactAttachments.depthStencilClass = pipelines::DepthStencilClass::Depth;
        filesystem::MemoryFileWriter writer(bytes);
        const pipelines::Result written = pipelines::WritePipeline(writer, description);
        return written == pipelines::Result::Success;
    }

    [[nodiscard]] bool BuildSurfaceMaterial(const BootstrapContent& content, containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        filesystem::MemoryFileReader shaderReader(content.opaqueShader.bytes, 0);
        filesystem::MemoryFileReader depthPipelineReader(content.depthPipeline.bytes, 0);
        filesystem::MemoryFileReader opaquePipelineReader(content.opaquePipeline.bytes, 0);
        shaders::ShaderFile shader;
        pipelines::PipelineFile depthPipeline;
        pipelines::PipelineFile opaquePipeline;
        const shaders::Result shaderOpened = shader.Open(shaderReader);
        const pipelines::Result depthPipelineOpened = depthPipeline.Open(depthPipelineReader);
        const pipelines::Result opaquePipelineOpened = opaquePipeline.Open(opaquePipelineReader);
        if (shaderOpened != shaders::Result::Success || depthPipelineOpened != pipelines::Result::Success ||
            opaquePipelineOpened != pipelines::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: material inputs failed: shader=%s depth=%s opaque=%s\n", shaders::ToString(shaderOpened),
                         pipelines::ToString(depthPipelineOpened), pipelines::ToString(opaquePipelineOpened));
            return false;
        }

        const std::array<vanguard::f32, 4> baseColor{{0.8f, 0.15f, 0.05f, 1.0f}};
        const std::array<vanguard::f32, 3> normal{{0.0f, 0.0f, 1.0f}};
        const vanguard::f32 roughness = 0.6f;
        const vanguard::f32 metallic = 0.0f;
        const std::array<vanguard::f32, 3> emissive{{0.0f, 0.0f, 0.0f}};
        const vanguard::f32 opacityCutoff = 0.5f;
        const materials::ConstantValueBuildRecord constants[]{
            {SurfaceParameterName(SurfaceParameterBase + 0), baseColor.data(), sizeof(baseColor)},
            {SurfaceParameterName(SurfaceParameterBase + 1), normal.data(), sizeof(normal)},
            {SurfaceParameterName(SurfaceParameterBase + 2), &roughness, sizeof(roughness)},
            {SurfaceParameterName(SurfaceParameterBase + 3), &metallic, sizeof(metallic)},
            {SurfaceParameterName(SurfaceParameterBase + 4), emissive.data(), sizeof(emissive)},
            {SurfaceParameterName(SurfaceParameterBase + 5), &opacityCutoff, sizeof(opacityCutoff)}};
        const materials::TechniqueBuildRecord techniques[]{
            {rendering::standardRenderPhases::DepthPrepass.value, content.depthPipeline.resource, &depthPipeline},
            {rendering::standardRenderPhases::Opaque.value, content.opaquePipeline.resource, &opaquePipeline}};
        materials::BuildDescription description;
        description.name = content.material.resource.GetPath().Id();
        description.shader = content.opaqueShader.resource;
        description.shaderReflection = &shader;
        description.techniques = techniques;
        description.constants = constants;
        filesystem::MemoryFileWriter writer(bytes);
        const materials::Result written = materials::WriteMaterial(writer, description);
        if (written != materials::Result::Success)
            std::fprintf(stderr, "bootstrapImage: material write failed: %s\n", materials::ToString(written));
        return written == materials::Result::Success;
    }

    struct BootstrapVertex final
    {
        vanguard::f32 position[3];
        vanguard::f32 normal[3];
        vanguard::f32 tangent[4];
        vanguard::f32 uv[2];
    };
    static_assert(sizeof(BootstrapVertex) == 48);

    [[nodiscard]] bool BuildSurfaceMesh(const BootstrapContent& content, containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        const std::array<BootstrapVertex, 4> vertices{{
            {{-1.5f, -1.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
            {{-1.5f,  1.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{ 1.5f,  1.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{ 1.5f, -1.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}}};
        const std::array<vanguard::u16, 6> indices{{0, 1, 2, 0, 2, 3}};
        const meshes::BufferBuildRecord buffers[]{
            {1, meshes::BufferKind::Vertex, sizeof(BootstrapVertex), sizeof(vertices)},
            {2, meshes::BufferKind::Index, sizeof(vanguard::u16), sizeof(indices)}};
        const meshes::PageBuildRecord pages[]{
            {1, 0, vertices.data(), sizeof(vertices), 4, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload},
            {2, 0, indices.data(), sizeof(indices), 4, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload}};
        const meshes::VertexLayoutBuildRecord layout{1};
        const meshes::VertexStreamBuildRecord streams[]{
            {1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 1, 0, sizeof(BootstrapVertex)},
            {1, meshes::VertexSemantic::Normal, 0, meshes::VertexFormat::R32G32B32Float, 0, 1, 12, sizeof(BootstrapVertex)},
            {1, meshes::VertexSemantic::Tangent, 0, meshes::VertexFormat::R32G32B32A32Float, 0, 1, 24, sizeof(BootstrapVertex)},
            {1, meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, 0, 1, 40, sizeof(BootstrapVertex)}};
        const meshes::MaterialSlotBuildRecord materialSlot{1, content.material.resource.GetPath().Id(), content.material.resource};
        const meshes::LodBuildRecord lod{0, 1.0f};
        meshes::Bounds bounds;
        bounds.minimum[0] = -1.5f;
        bounds.minimum[1] = -1.0f;
        bounds.minimum[2] = 4.9f;
        bounds.maximum[0] = 1.5f;
        bounds.maximum[1] = 1.0f;
        bounds.maximum[2] = 5.1f;
        bounds.sphereRadius = 1.9f;
        const meshes::SubmeshBuildRecord submesh{content.mesh.resource.GetPath().Id(), content.mesh.resource.GetPath().Id(), 0, 1, 1, 2,
                                                  meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList,
                                                  meshes::SubmeshFlags::CastsShadow | meshes::SubmeshFlags::TwoSided,
                                                  0, static_cast<vanguard::u32>(vertices.size()), 0,
                                                  static_cast<vanguard::u32>(indices.size()), bounds};
        meshes::BuildDescription description;
        description.kind = meshes::MeshKind::Static;
        description.name = content.mesh.resource.GetPath().Id();
        description.bounds = bounds;
        description.sourceFingerprint = crypto::Sha256("bootstrap-static-quad-v1", 24);
        description.buffers = buffers;
        description.pages = pages;
        description.vertexLayouts = {&layout, 1};
        description.vertexStreams = streams;
        description.materialSlots = {&materialSlot, 1};
        description.lods = {&lod, 1};
        description.submeshes = {&submesh, 1};
        filesystem::MemoryFileWriter writer(bytes);
        const meshes::Result written = meshes::WriteMesh(writer, description);
        return written == meshes::Result::Success;
    }

    [[nodiscard]] bool BuildRenderablePrefab(const BootstrapContent& content, const crypto::Digest256& fingerprint,
                                             containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        const prefabs::EntityBuildRecord entity{1, prefabs::InvalidStableId, 0x626f6f7473747261ull, prefabs::EntityFlags::Root};
        entities::StaticMeshComponentData mesh;
        mesh.mesh = content.mesh.resource;
        entities::CameraComponentData camera;
        camera.farPlane = 100.0f;
        entities::LightComponentData light;
        light.kind = static_cast<vanguard::u8>(rendering::RenderLightKind::Directional);
        light.red = 1.0f;
        light.green = 0.95f;
        light.blue = 0.9f;
        light.intensity = 3.1415926536f;
        const prefabs::ComponentBuildRecord components[]{
            {101, 1, &entities::GetStaticMeshComponentSchema(), &mesh, prefabs::ComponentFlags::None},
            {102, 1, &entities::GetCameraComponentSchema(), &camera, prefabs::ComponentFlags::None},
            {103, 1, &entities::GetLightComponentSchema(), &light, prefabs::ComponentFlags::None}};
        prefabs::CookDescription description;
        description.name = 0x626f6f7473747261ull;
        description.entities = {&entity, 1};
        description.components = components;
        description.sourceFingerprint = fingerprint;
        filesystem::MemoryFileWriter writer(bytes);
        const prefabs::Result cooked = prefabs::CookPrefab(description, writer);
        return cooked == prefabs::Result::Success;
    }

    [[nodiscard]] bool BuildFeaturePipeline(const BootstrapContent& content, const vanguard::u32 index,
                                           containers::DynamicArray<vanguard::u8>& bytes) noexcept
    {
        const auto& feature = rendering::RendererFeaturePrograms[index];
        const auto& artifact = content.featureShaders[index];
        filesystem::MemoryFileReader reader(artifact.bytes, 0);
        shaders::ShaderFile shader;
        const auto opened = shader.Open(reader);
        if (opened != shaders::Result::Success)
            return false;
        const auto expectedKind = feature.compute != nullptr ? shaders::ProgramKind::Compute : shaders::ProgramKind::Graphics;
        if (shader.GetKind() != expectedKind || shader.GetConstantBuffers().Size() != 1 || shader.HasMaterialContract())
        {
            std::fprintf(stderr, "bootstrapImage: feature %s has an incompatible shader contract\n", feature.name);
            return false;
        }
        const pipelines::ShaderReference reference{artifact.resource.GetPath().Id(), shader.GetPermutation(),
            shader.BindingLayoutFingerprint(), shader.GetPipelineInterfaceFingerprint()};
        pipelines::BuildDescription description;
        description.name = content.featurePipelines[index].resource.GetPath().Id();
        description.kind = feature.compute != nullptr ? pipelines::PipelineKind::Compute : pipelines::PipelineKind::Graphics;
        description.shaders = {&reference, 1};
        if (feature.compute == nullptr)
        {
            description.graphics.rasterizer.cull = pipelines::CullMode::None;
            description.graphics.blend.attachmentCount = 1;
            description.graphics.attachmentPolicy = feature.frameOutput ? pipelines::AttachmentPolicy::Deferred : pipelines::AttachmentPolicy::Exact;
            if (!feature.frameOutput)
            {
                description.graphics.exactAttachments.colorCount = 1;
                description.graphics.exactAttachments.colors[0] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
            }
        }
        filesystem::MemoryFileWriter writer(bytes);
        const auto written = pipelines::WritePipeline(writer, description);
        if (written != pipelines::Result::Success)
            std::fprintf(stderr, "bootstrapImage: feature %s pipeline write failed: %s\n", feature.name, pipelines::ToString(written));
        return written == pipelines::Result::Success;
    }

    [[nodiscard]] bool DiscoverDependencies(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void* const userData) noexcept
    {
        const auto& content = *static_cast<const BootstrapContent*>(userData);
        const auto add = [&](const StoredArtifact& artifact) noexcept
        {
            return dependencies.Add({artifact.resource, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
                   assets::Result::Success;
        };
        for (vanguard::u32 index = 0; index < FeatureCount; ++index)
        {
            if (request.output == content.featurePipelines[index].resource)
                return add(content.featureShaders[index]);
            if (request.output == content.catalog.resource)
            {
                const bool shaderAdded = add(content.featureShaders[index]);
                const bool pipelineAdded = add(content.featurePipelines[index]);
                if (!shaderAdded || !pipelineAdded)
                    return false;
            }
        }
        if (request.output == content.catalog.resource)
            return true;
        if (request.output == content.startupWorld.resource)
            return add(content.cell);
        if (request.output == content.cell.resource)
            return add(content.prefab);
        if (request.output == content.prefab.resource)
            return add(content.mesh);
        if (request.output == content.mesh.resource)
            return add(content.material);
        if (request.output == content.material.resource)
            return add(content.opaqueShader) && add(content.depthPipeline) && add(content.opaquePipeline);
        if (request.output == content.depthPipeline.resource)
            return add(content.depthShader);
        if (request.output == content.opaquePipeline.resource)
            return add(content.opaqueShader);
        return request.output == content.input.resource;
    }

    void SetBounds(world::Bounds& bounds, const vanguard::f32 extent) noexcept
    {
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = -extent;
            bounds.maximum[axis] = extent;
        }
    }

    void SetBounds(world::WorldBounds& bounds, const vanguard::f64 extent) noexcept
    {
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = -extent;
            bounds.maximum[axis] = extent;
        }
    }

    [[nodiscard]] bool CompileResource(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void* const userData) noexcept
    {
        const auto& content = *static_cast<const BootstrapContent*>(userData);
        containers::DynamicArray<vanguard::u8> bytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter output(bytes);
        bool cooked = false;

        for (vanguard::u32 index = 0; index < FeatureCount; ++index)
            if (context.request.output == content.featurePipelines[index].resource)
                cooked = BuildFeaturePipeline(content, index, bytes);
        if (context.request.output == content.catalog.resource)
        {
            pipelines::RendererCatalogEntry entries[FeatureCount];
            for (vanguard::u32 index = 0; index < FeatureCount; ++index)
            {
                std::snprintf(entries[index].name, sizeof(entries[index].name), "%s", rendering::RendererFeaturePrograms[index].name);
                entries[index].shader = content.featureShaders[index].resource;
                entries[index].pipeline = content.featurePipelines[index].resource;
            }
            const auto written = pipelines::WriteRendererCatalog(output, entries);
            cooked = written == pipelines::Result::Success;
        }

        if (context.request.output == content.input.resource)
        {
            const gameInput::MappingBuildDescription description;
            cooked = gameInput::CookMapping(description, output) == gameInput::MappingResult::Success;
        }
        else if (context.request.output == content.depthPipeline.resource)
        {
            cooked = BuildSurfacePipeline(content.depthShader, content.depthPipeline, true, bytes);
        }
        else if (context.request.output == content.opaquePipeline.resource)
        {
            cooked = BuildSurfacePipeline(content.opaqueShader, content.opaquePipeline, false, bytes);
        }
        else if (context.request.output == content.material.resource)
        {
            cooked = BuildSurfaceMaterial(content, bytes);
        }
        else if (context.request.output == content.mesh.resource)
        {
            cooked = BuildSurfaceMesh(content, bytes);
        }
        else if (context.request.output == content.prefab.resource)
        {
            cooked = BuildRenderablePrefab(content, crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count()), bytes);
        }
        else if (context.request.output == content.cell.resource)
        {
            world::PlacementBuildRecord placement;
            placement.entityId = 1;
            placement.name = 0x626f6f7473747261ull;
            placement.prefab = content.prefab.resource;
            SetBounds(placement.bounds, 1.0f);
            placement.streamingDistance = 1000.0f;
            placement.visibilityDistance = 1000.0f;
            placement.flags = world::PlacementFlags::Persistent;

            world::CellBuildDescription description;
            description.cellId = CellId;
            description.worldId = WorldId;
            description.category = world::CellCategory::AlwaysLoaded;
            SetBounds(description.bounds, 100.0f);
            description.placements = {&placement, 1};
            description.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count());
            cooked = world::CookCell(description, output) == world::Result::Success;
        }
        else if (context.request.output == content.startupWorld.resource)
        {
            world::WorldCellBuildRecord cell;
            cell.cellId = CellId;
            cell.name = 0x626f6f7473747261ull;
            cell.cell = content.cell.resource;
            cell.category = world::CellCategory::AlwaysLoaded;
            cell.streamingPriority = world::StreamingPriority::Critical;
            SetBounds(cell.bounds, 100.0);
            cell.activationDistance = 1000.0f;
            cell.retentionDistance = 1200.0f;
            cell.flags = world::WorldCellFlags::AlwaysLoaded;

            world::WorldBuildDescription description;
            description.worldId = WorldId;
            SetBounds(description.bounds, 100.0);
            description.cells = {&cell, 1};
            description.sourceFingerprint = crypto::Sha256(context.request.source.content.Data(), context.request.source.content.Count());
            cooked = world::CookWorld(description, output) == world::Result::Success;
        }

        if (!cooked)
            return false;
        const assets::Result added = artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4,
                                                   bytes.Data(), bytes.Size());
        return added == assets::Result::Success;
    }

    [[nodiscard]] assets::CompilerDescriptor Compiler(const char* const name, const resources::ResourceTypeId sourceType,
                                                      const resources::ResourceTypeId outputType, BootstrapContent& content) noexcept
    {
        return {assets::HashCompilerName(name), name, 3, sourceType, outputType, &DiscoverDependencies, &CompileResource, &content};
    }

    [[nodiscard]] bool StoreBuildOutput(BootstrapContent& content, const assets::BuildOutput& output) noexcept
    {
        for (const assets::Artifact& artifact : output.artifacts)
        {
            StoredArtifact* const destination = content.Find(artifact.resource);
            if (destination == nullptr || artifact.segment != 0)
                return false;
            destination->origin = {output.buildFingerprint, output.contentFingerprint};
            destination->bytes = artifact.bytes;
        }
        return true;
    }

    [[nodiscard]] bool BuildResource(assets::BuildSystem& builds, assets::DependencyIndex& index, BootstrapContent& content, const char* const sourcePath,
                                     const resources::ResourceTypeId sourceType, StoredArtifact& artifact,
                                     const StoredArtifact* const* const generatedDependencies = nullptr,
                                     const vanguard::u32 generatedDependencyCount = 0) noexcept
    {
        constexpr char SourceContent[] = "Vanguard deterministic development bootstrap recipe 3";
        const assets::BuildRequest request{
            {BootstrapContent::Reference(sourcePath, sourceType), {reinterpret_cast<const vanguard::u8*>(SourceContent), sizeof(SourceContent) - 1u}, {}},
            artifact.resource,
            assets::TargetPlatform::WindowsD3D12,
            {}};
        assets::BuildPlan plan;
        const assets::Result prepared = builds.Prepare(request, plan);
        if (prepared != assets::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: prepare %s failed: %s\n", sourcePath, assets::ToString(prepared));
            return false;
        }
        for (vanguard::u32 dependencyIndex = 0; dependencyIndex < generatedDependencyCount; ++dependencyIndex)
        {
            const StoredArtifact* const generatedDependency = generatedDependencies[dependencyIndex];
            if (generatedDependency == nullptr)
                return false;
            assets::DependencyRecord dependencyRecord;
            const assets::IndexResult found = index.Find(generatedDependency->resource, dependencyRecord);
            if (found != assets::IndexResult::Success)
            {
                std::fprintf(stderr, "bootstrapImage: dependency lookup for %s failed: %s\n", sourcePath, assets::ToString(found));
                return false;
            }
            const assets::Result assigned = plan.SetGeneratedDependencyContent(generatedDependency->resource, dependencyRecord.contentFingerprint);
            if (assigned != assets::Result::Success)
            {
                std::fprintf(stderr, "bootstrapImage: dependency binding for %s failed: %s\n", sourcePath, assets::ToString(assigned));
                return false;
            }
        }
        assets::BuildOutput output;
        const assets::Result executed = builds.Execute(request, plan, output);
        if (executed != assets::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: execute %s failed: %s\n", sourcePath, assets::ToString(executed));
            return false;
        }
        const bool stored = StoreBuildOutput(content, output);
        if (!stored)
        {
            std::fprintf(stderr, "bootstrapImage: store %s failed\n", sourcePath);
            return false;
        }
        const assets::IndexResult published = index.Publish(request, plan, output);
        if (published != assets::IndexResult::Success)
        {
            std::fprintf(stderr, "bootstrapImage: publish %s failed: %s\n", sourcePath, assets::ToString(published));
            return false;
        }
        return true;
    }

    // This CLI prepares/executes one build at a time. Each invocation has its own
    // provider; the borrowed include bytes last until its next callback.
    struct ShaderSourceProvider
    {
        filesystem::AbsolutePath root;
        containers::String canonicalPath;
        containers::DynamicArray<vanguard::u8> bytes{memory::pools::Tools::GetInstance()};

        static bool Load(const char* path, shaderTools::ShaderSource& source, void* data) noexcept
        {
            auto& provider = *static_cast<ShaderSourceProvider*>(data);
            if (path == nullptr || filesystem::AbsolutePath::IsValidPath(containers::StringView(path)) || std::strstr(path, "..") != nullptr)
                return false;
            provider.canonicalPath = path;
            const bool read = ReadFile(provider.root.AddFilePath(path), provider.bytes);
            if (!read)
                return false;
            source = {provider.canonicalPath.AsChar(), BootstrapContent::Reference(path, shaderTools::ShaderSourceResourceType), provider.bytes};
            return true;
        }
    };

    bool BuildShaderResource(assets::BuildSystem& builds, assets::DependencyIndex& index, BootstrapContent& content,
        const char* sourceName, containers::ArraySpan<const vanguard::u8> source,
        containers::ArraySpan<const shaderTools::EntryPoint> entries, StoredArtifact& artifact, vanguard::u64 program,
        crypto::Digest256 permutation = {}) noexcept
    {
        shaderTools::ShaderBuildDescription description;
        description.sourceName = sourceName;
        description.program = program;
        description.permutation = permutation.IsEmpty() ? crypto::Sha256(artifact.path, std::strlen(artifact.path)) : permutation;
        description.entryPoints = entries;
        containers::DynamicArray<vanguard::u8> settings(memory::pools::Tools::GetInstance());
        const auto encoded = shaderTools::EncodeShaderBuildSettings(description, settings);
        if (encoded != shaderTools::BuildSettingsResult::Success)
            return false;
        const assets::BuildRequest request{{BootstrapContent::Reference(sourceName, shaderTools::ShaderSourceResourceType), source, {}},
            artifact.resource, assets::TargetPlatform::WindowsD3D12, settings};
        assets::BuildPlan plan;
        const auto prepared = builds.Prepare(request, plan);
        if (prepared != assets::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: shader prepare %s: %s\n", artifact.path, assets::ToString(prepared));
            return false;
        }
        assets::BuildOutput output;
        const auto built = builds.Execute(request, plan, output);
        if (built != assets::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: shader build %s: %s\n", artifact.path, assets::ToString(built));
            return false;
        }
        const bool stored = StoreBuildOutput(content, output);
        if (!stored)
            return false;
        const auto published = index.Publish(request, plan, output);
        return published == assets::IndexResult::Success;
    }

    bool BuildSurfaceArtifact(assets::BuildSystem& builds, assets::DependencyIndex& index, BootstrapContent& content, bool depth) noexcept
    {
        containers::DynamicArray<vanguard::u8> source(memory::pools::Tools::GetInstance());
        const bool generated = GenerateSurfaceSource(content, depth, source);
        if (!generated)
            return false;
        const shaderTools::EntryPoint entries[]{ {"StaticSurfaceVertexMain", shaders::ShaderStage::Vertex},
            {depth ? "StaticSurfaceDepthMain" : "StaticSurfaceGBufferMain", shaders::ShaderStage::Fragment} };
        return BuildShaderResource(builds, index, content, depth ? "static_surface_depth.generated.vsl" : "static_surface_opaque.generated.vsl",
            source, entries, depth ? content.depthShader : content.opaqueShader, depth ? SurfaceDepthProgram : SurfaceOpaqueProgram,
            crypto::Sha256(SurfacePermutationName, sizeof(SurfacePermutationName) - 1u));
    }

    [[nodiscard]] bool ResolvePath(const resources::ResourceReference resource, char* const destination, const vanguard::usize capacity,
                                   vanguard::usize& written, void* const userData) noexcept
    {
        const StoredArtifact* const artifact = static_cast<const BootstrapContent*>(userData)->Find(resource);
        if (artifact == nullptr || artifact->path == nullptr)
            return false;
        written = 0;
        while (artifact->path[written] != '\0')
        {
            if (written == capacity)
                return false;
            destination[written] = artifact->path[written];
            ++written;
        }
        return true;
    }

    [[nodiscard]] bool ReadArtifact(const assets::ArtifactSetKey origin, const assets::IndexedArtifact& artifact,
                                    containers::DynamicArray<vanguard::u8>& bytes, void* const userData) noexcept
    {
        const StoredArtifact* const stored = static_cast<const BootstrapContent*>(userData)->Find(artifact.resource);
        if (stored == nullptr || stored->origin != origin || artifact.segment != 0 || artifact.byteCount != stored->bytes.Size())
            return false;
        bytes = stored->bytes;
        return true;
    }

    [[nodiscard]] bool ValidateImage(filesystem::Manager& files, const filesystem::AbsolutePath& path, const BootstrapContent& content) noexcept
    {
        auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
        packages::PackageReader reader;
        if (!file)
            return false;
        const packages::Result opened = reader.Open(*file);
        if (opened != packages::Result::Success)
        {
            std::fprintf(stderr, "bootstrapImage: package reopen failed: %s\n", packages::ToString(opened));
            return false;
        }
        if (!reader.HasPackageSet())
            return false;
        const packages::PackageSet* const packageSet = reader.GetPackageSet();
        if (packageSet == nullptr || packageSet->gameId != GameId || packageSet->startupWorld != content.startupWorld.resource.GetPath().Id() ||
            packageSet->startupWorldType != content.startupWorld.resource.ExpectedType() ||
            packageSet->defaultInput != content.input.resource.GetPath().Id() || packageSet->defaultInputType != content.input.resource.ExpectedType())
            return false;
        for (const StoredArtifact* const artifact : {&content.input, &content.prefab, &content.mesh, &content.material, &content.depthShader,
                                                     &content.opaqueShader, &content.depthPipeline, &content.opaquePipeline, &content.cell,
                                                     &content.startupWorld})
        {
            const packages::Resource* const resource = reader.Find(artifact->path);
            if (resource == nullptr || resource->type != artifact->resource.ExpectedType())
                return false;
        }
        if (packageSet->rendererBootstrap != content.catalog.resource.GetPath().Id() ||
            packageSet->rendererBootstrapType != pipelines::RendererCatalogResourceType)
            return false;
        const auto validateBytes = [&](const StoredArtifact& artifact) noexcept
        {
            const auto* resource = reader.Find(artifact.path);
            if (resource == nullptr || resource->type != artifact.resource.ExpectedType())
                return false;
            containers::DynamicArray<vanguard::u8> bytes(memory::pools::Tools::GetInstance());
            bytes.Resize(artifact.bytes.Size());
            packages::ResourceFileReader resourceFile;
            const auto read = resourceFile.Open(reader, *resource, *file);
            serialization::BinaryReader resourceReader(resourceFile);
            const bool transferred = read == packages::Result::Success && resourceFile.GetSize() == bytes.Size() &&
                resourceReader.ReadBytes(bytes.Data(), bytes.Size());
            const bool valid = transferred && std::memcmp(bytes.Data(), artifact.bytes.Data(), bytes.Size()) == 0;
            if (!valid)
                std::fprintf(stderr, "bootstrapImage: package bytes disagree for %s: %s\n", artifact.path, packages::ToString(read));
            return valid;
        };
        for (vanguard::u32 index = 0; index < FeatureCount; ++index)
        {
            const bool shaderValid = validateBytes(content.featureShaders[index]);
            const bool pipelineValid = validateBytes(content.featurePipelines[index]);
            if (!shaderValid || !pipelineValid)
                return false;
        }
        const bool catalogValid = validateBytes(content.catalog);
        if (!catalogValid)
            return false;
        filesystem::MemoryFileReader catalogFile(content.catalog.bytes, 0);
        pipelines::RendererCatalogResource catalog;
        const auto openedCatalog = catalog.Open(catalogFile);
        std::fprintf(stdout, "bootstrapImage: catalog=%s entries=%u resources=%u\n", pipelines::ToString(openedCatalog),
            catalog.Entries().Size(), reader.GetResources().Count());
        return openedCatalog == pipelines::Result::Success && catalog.Entries().Size() == FeatureCount &&
            reader.GetResources().Count() == 11 + 2 * FeatureCount;
    }

    [[nodiscard]] bool BuildBootstrapImage(const char* const runtimeRootArgument, const char* const shaderDirectoryArgument) noexcept
    {
        if (runtimeRootArgument == nullptr || runtimeRootArgument[0] == '\0' || shaderDirectoryArgument == nullptr || shaderDirectoryArgument[0] == '\0')
            return false;
        const filesystem::AbsolutePath outputDirectory = filesystem::AbsolutePath::CreateDirPath(runtimeRootArgument);
        const filesystem::AbsolutePath cacheDirectory = filesystem::paths::GetUserCacheDirectory().AddDirPath("bootstrapImage");
        const bool filesystemInitialized = filesystem::Initialize({outputDirectory, outputDirectory, cacheDirectory});
        if (!filesystemInitialized)
            return Fail("filesystem initialization");
        filesystem::Manager& files = filesystem::GetManager();
        const filesystem::AbsolutePath packagePath = outputDirectory.AddFilePath("DATA000.vpak");
        if (files.FileExist(packagePath))
        {
            VG_LOG_ERROR(diagnostics::Category::Resources, "bootstrap image already exists at %s; committed DATA images are never overwritten implicitly",
                         packagePath.AsChar());
            filesystem::Shutdown();
            return false;
        }
        if (!files.CreatePath(cacheDirectory))
        {
            filesystem::Shutdown();
            return Fail("cache directory creation");
        }

        BootstrapContent content;
        content.shaderDirectory = filesystem::AbsolutePath::CreateDirPath(shaderDirectoryArgument);
        assets::Config buildConfig;
        const filesystem::AbsolutePath ddcDirectory = cacheDirectory.AddDirPath("ddc");
        static_cast<void>(files.CreatePath(ddcDirectory));
        buildConfig.persistentCacheRoot = ddcDirectory.AsChar();
        assets::BuildSystem builds;
        ShaderSourceProvider sourceProvider;
        sourceProvider.root = content.shaderDirectory;
        shaderTools::ShaderAssetCompiler shaderCompiler;
        assets::DependencyIndex index;
        assets::DependencyIndexConfig indexConfig;
        indexConfig.root = cacheDirectory.AsChar();
        indexConfig.settingsFingerprint = crypto::Sha256("bootstrapImage.settings.3", 25);

        const assets::CompilerDescriptor compilers[]{Compiler("bootstrap.input", InputSourceType, gameInput::MappingResourceType, content),
                                                     Compiler("renderer.catalog", CatalogSourceType, pipelines::RendererCatalogResourceType, content),
                                                     Compiler("bootstrap.pipeline", PipelineSourceType, pipelines::PipelineResourceType, content),
                                                     Compiler("bootstrap.material", MaterialSourceType, materials::MaterialResourceType, content),
                                                     Compiler("bootstrap.mesh", MeshSourceType, meshes::MeshResourceType, content),
                                                     Compiler("bootstrap.prefab", PrefabSourceType, prefabs::PrefabResourceType, content),
                                                     Compiler("bootstrap.cell", CellSourceType, world::CellResourceType, content),
                                                     Compiler("bootstrap.world", WorldSourceType, world::WorldResourceType, content)};
        bool succeeded = builds.Initialize(buildConfig);
        if (!succeeded)
            static_cast<void>(Fail("build-system initialization"));
        if (succeeded)
        {
            const assets::IndexResult initialized = index.Initialize(indexConfig);
            succeeded = assets::IsSuccess(initialized);
            if (!succeeded)
                std::fprintf(stderr, "bootstrapImage: dependency-index initialization failed: %s\n", assets::ToString(initialized));
        }
        for (const assets::CompilerDescriptor& compiler : compilers)
        {
            if (!succeeded)
                break;
            const assets::Result registered = builds.RegisterCompiler(compiler);
            succeeded = registered == assets::Result::Success;
            if (!succeeded)
                std::fprintf(stderr, "bootstrapImage: compiler registration %s failed: %s\n", compiler.name, assets::ToString(registered));
        }
        if (succeeded)
        {
            const bool initialized = shaderCompiler.Initialize({&ShaderSourceProvider::Load, &sourceProvider});
            succeeded = initialized;
            if (succeeded)
            {
                const auto registered = shaderCompiler.Register(builds);
                succeeded = registered == assets::Result::Success;
            }
        }
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/input/default.bootstrap", InputSourceType, content.input);
            succeeded = built;
        }
        if (succeeded)
        {
            const bool built = BuildSurfaceArtifact(builds, index, content, true);
            succeeded = built;
        }
        if (succeeded)
        {
            const bool built = BuildSurfaceArtifact(builds, index, content, false);
            succeeded = built;
        }
        const StoredArtifact* const depthPipelineDependencies[]{&content.depthShader};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/materials/bootstrap_depth_pipeline.bootstrap", PipelineSourceType,
                                             content.depthPipeline, depthPipelineDependencies, 1);
            succeeded = built;
        }
        const StoredArtifact* const opaquePipelineDependencies[]{&content.opaqueShader};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/materials/bootstrap_opaque_pipeline.bootstrap", PipelineSourceType,
                                             content.opaquePipeline, opaquePipelineDependencies, 1);
            succeeded = built;
        }
        const StoredArtifact* const materialDependencies[]{&content.opaqueShader, &content.depthPipeline, &content.opaquePipeline};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/materials/bootstrap_surface.bootstrap", MaterialSourceType,
                                             content.material, materialDependencies, 3);
            succeeded = built;
        }
        const StoredArtifact* const meshDependencies[]{&content.material};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/meshes/bootstrap_quad.bootstrap", MeshSourceType, content.mesh,
                                             meshDependencies, 1);
            succeeded = built;
        }
        const StoredArtifact* const prefabDependencies[]{&content.mesh};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/prefabs/bootstrap.bootstrap", PrefabSourceType, content.prefab,
                                             prefabDependencies, 1);
            succeeded = built;
        }
        const StoredArtifact* const cellDependencies[]{&content.prefab};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/world/bootstrap_cell.bootstrap", CellSourceType, content.cell,
                                             cellDependencies, 1);
            succeeded = built;
        }
        const StoredArtifact* const worldDependencies[]{&content.cell};
        if (succeeded)
        {
            const bool built = BuildResource(builds, index, content, "source/world/bootstrap_world.bootstrap", WorldSourceType, content.startupWorld,
                                             worldDependencies, 1);
            succeeded = built;
        }
        for (vanguard::u32 featureIndex = 0; succeeded && featureIndex < FeatureCount; ++featureIndex)
        {
            const auto& feature = rendering::RendererFeaturePrograms[featureIndex];
            containers::DynamicArray<vanguard::u8> source(memory::pools::Tools::GetInstance());
            const bool read = ReadFile(content.shaderDirectory.AddFilePath(feature.source), source);
            succeeded = read;
            const shaderTools::EntryPoint compute[]{{feature.compute, shaders::ShaderStage::Compute}};
            const shaderTools::EntryPoint graphics[]{{feature.vertex, shaders::ShaderStage::Vertex}, {feature.fragment, shaders::ShaderStage::Fragment}};
            const containers::ArraySpan<const shaderTools::EntryPoint> entries = feature.compute != nullptr
                ? containers::ArraySpan<const shaderTools::EntryPoint>(compute, 1)
                : containers::ArraySpan<const shaderTools::EntryPoint>(graphics, 2);
            if (succeeded)
                succeeded = BuildShaderResource(builds, index, content, feature.source, source, entries, content.featureShaders[featureIndex],
                    content.featureShaders[featureIndex].resource.GetPath().Id());
            const StoredArtifact* dependencies[]{&content.featureShaders[featureIndex]};
            if (succeeded)
                succeeded = BuildResource(builds, index, content, content.featurePipelines[featureIndex].path, PipelineSourceType,
                    content.featurePipelines[featureIndex], dependencies, 1);
        }
        const StoredArtifact* featureDependencies[FeatureCount * 2];
        for (vanguard::u32 slot = 0; slot < FeatureCount; ++slot)
        {
            featureDependencies[slot * 2] = &content.featureShaders[slot];
            featureDependencies[slot * 2 + 1] = &content.featurePipelines[slot];
        }
        if (succeeded)
            succeeded = BuildResource(builds, index, content, "engine/rendering/catalog.recipe", CatalogSourceType, content.catalog,
                featureDependencies, FeatureCount * 2);
        if (succeeded)
        {
            const assets::IndexResult saved = index.Save();
            succeeded = saved == assets::IndexResult::Success;
        }

        assets::PackageManifest manifest;
        manifest.packageId = GameId;
        if (succeeded)
        {
            const assets::PackagingResult added = manifest.AddRoot({content.startupWorld.resource, assets::PackageRootFlags::Startup});
            succeeded = added == assets::PackagingResult::Success;
        }
        if (succeeded)
        {
            const assets::PackagingResult added = manifest.AddRoot({content.input.resource, assets::PackageRootFlags::Startup});
            succeeded = added == assets::PackagingResult::Success;
        }
        assets::PackageBuildPlan packagePlan;
        if (succeeded)
        {
            const auto added = manifest.AddRoot({content.catalog.resource, assets::PackageRootFlags::Startup});
            succeeded = added == assets::PackagingResult::Success;
        }
        assets::PackagePlanner packagePlanner;
        if (succeeded)
        {
            const assets::PackagingResult prepared = packagePlanner.Prepare(manifest, index, packagePlan);
            succeeded = prepared == assets::PackagingResult::Success;
        }

        assets::PackageSetPlanOptions setOptions;
        setOptions.gameId = GameId;
        setOptions.startupWorld = content.startupWorld.resource;
        setOptions.defaultInput = content.input.resource;
        setOptions.rendererBootstrap = content.catalog.resource;
        assets::PackageSetBuildPlan setPlan;
        assets::PackagePlacementState placement;
        assets::PackageSetPlanner setPlanner;
        if (succeeded)
        {
            const assets::PackagingResult prepared = setPlanner.Prepare(packagePlan, setOptions, nullptr, setPlan, placement);
            succeeded = prepared == assets::PackagingResult::Success;
        }
        const assets::PackageAssemblyCallbacks callbacks{&ResolvePath, &content, &ReadArtifact, &content};
        assets::PackageSetAssembler assembler;
        if (succeeded)
        {
            const assets::PackagingResult published = assembler.Publish(setPlan, outputDirectory, callbacks);
            succeeded = published == assets::PackagingResult::Success;
        }
        if (succeeded)
        {
            const bool validated = ValidateImage(files, packagePath, content);
            succeeded = validated;
        }

        if (shaderCompiler.IsInitialized())
        {
            static_cast<void>(shaderCompiler.Unregister());
            const bool stopped = shaderCompiler.Shutdown();
            succeeded = stopped && succeeded;
        }
        for (const assets::CompilerDescriptor& compiler : compilers)
            if (builds.IsInitialized())
                static_cast<void>(builds.UnregisterCompiler(compiler.id));
        if (index.IsInitialized())
            static_cast<void>(index.Shutdown());
        if (builds.IsInitialized())
            static_cast<void>(builds.Shutdown());
        filesystem::Shutdown();
        return succeeded;
    }
} // namespace

int main(const int argumentCount, const char* const* const arguments)
{
    const bool memoryInitialized = memory::Initialize();
    if (!memoryInitialized)
        return 1;
    const bool diagnosticsInitialized = diagnostics::Initialize(diagnostics::Mode::Synchronous, "bootstrapImage");
    bool containersInitialized = false;
    if (diagnosticsInitialized)
        containersInitialized = containers::Initialize();
    bool ioInitialized = false;
    if (containersInitialized)
        ioInitialized = vanguard::io::Initialize();
    bool succeeded = false;
    if (ioInitialized && argumentCount == 3)
        succeeded = BuildBootstrapImage(arguments[1], arguments[2]);
    if (succeeded)
        VG_LOG_INFO(diagnostics::Category::Resources, "published and validated DATA000.vpak beside the runtime executable");
    else if (diagnosticsInitialized)
        VG_LOG_ERROR(diagnostics::Category::Resources,
                     "development bootstrap image generation failed; usage: bootstrapImage <runtime-image-directory> <shader-source-directory>");
    if (ioInitialized)
        vanguard::io::Shutdown();
    if (diagnosticsInitialized)
        diagnostics::Shutdown();
    return succeeded ? 0 : 1;
}
