#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/shaders/shaders.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/textures/texture_resource.hpp>
#include <vanguard/textures/textures.hpp>

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

namespace vanguard::engine::tests
{
    bool RunStaticSurfaceNativeProof();
}

namespace
{
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace material = vanguard::materials;
    namespace meshes = vanguard::meshes;
    namespace packages = vanguard::packages;
    namespace pipeline = vanguard::pipelines;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace rhi = vanguard::rhi;
    namespace shader = vanguard::shaders;
    namespace streaming = vanguard::streaming;
    namespace textures = vanguard::textures;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    inline constexpr vanguard::u64 MaterialTechnique = rendering::standardRenderPhases::Opaque.value;
    inline constexpr vanguard::u64 MaterialTextureRole = 0x3c040010ull;
    inline constexpr vanguard::u64 MaterialParameters = 0x3c040020ull;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[materialRuntimeServiceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct TestClock
    {
        vanguard::u64 ticks = 1'000;
    };

    [[nodiscard]] vanguard::u64 ReadClock(void* const userData) noexcept
    {
        return static_cast<TestClock*>(userData)->ticks;
    }

    struct ArtifactPaths
    {
        const char* shader = nullptr;
        const char* pipeline = nullptr;
        const char* material = nullptr;
        const char* requiredTexture = nullptr;
        const char* optionalTexture = nullptr;
    };

    inline constexpr ArtifactPaths LoosePaths{"runtime/material3c4/loose.vshader", "runtime/material3c4/loose.vppl", "runtime/material3c4/loose.vmat", "runtime/material3c4/loose_required.vtex",
                                              "runtime/material3c4/loose_optional_missing.vtex"};
    inline constexpr ArtifactPaths PackagePaths{"runtime/material3c4/package.vshader", "runtime/material3c4/package.vppl", "runtime/material3c4/package.vmat", "runtime/material3c4/package_required.vtex",
                                                "runtime/material3c4/package_optional_missing.vtex"};
    inline constexpr ArtifactPaths FailurePaths{"runtime/material3c4/failure.vshader", "runtime/material3c4/failure.vppl", "runtime/material3c4/failure.vmat",
                                                "runtime/material3c4/failure_required_missing.vtex", "runtime/material3c4/failure_optional_missing.vtex"};
    inline constexpr const char* FallbackTexturePath = "runtime/material3c4/fallback.vtex";
    inline constexpr ArtifactPaths CopyPaths{"rendering/shaders/fullscreen_copy.vshader", "rendering/pipelines/fullscreen_copy.vpipeline", "unused/copy.vmat", "unused/copy.vtex", "unused/optional.vtex"};
    inline constexpr const char* MeshPath = "runtime/material3c4/mesh.vmesh";

    struct CompiledShader
    {
        ID3DBlob* bytecode = nullptr;

        ~CompiledShader()
        {
            if (bytecode != nullptr)
                bytecode->Release();
        }
    };

    struct ArtifactFixture
    {
        explicit ArtifactFixture(const ArtifactPaths& value) noexcept
            : paths(value), shaderReference(resources::ResourcePath::FromString(paths.shader), shader::ShaderResourceType),
              pipelineReference(resources::ResourcePath::FromString(paths.pipeline), pipeline::PipelineResourceType),
              materialReference(resources::ResourcePath::FromString(paths.material), material::MaterialResourceType),
              requiredTexture(resources::ResourcePath::FromString(paths.requiredTexture), textures::TextureResourceType),
              optionalTexture(resources::ResourcePath::FromString(paths.optionalTexture), textures::TextureResourceType), shaderBytes(vanguard::memory::pools::Rendering::GetInstance()),
              pipelineBytes(vanguard::memory::pools::Rendering::GetInstance()), materialBytes(vanguard::memory::pools::Rendering::GetInstance()), textureBytes(vanguard::memory::pools::Rendering::GetInstance())
        {
        }

        ArtifactPaths paths;
        resources::ResourceReference shaderReference;
        resources::ResourceReference pipelineReference;
        resources::ResourceReference materialReference;
        resources::ResourceReference requiredTexture;
        resources::ResourceReference optionalTexture;
        ByteArray shaderBytes;
        ByteArray pipelineBytes;
        ByteArray materialBytes;
        ByteArray textureBytes;
        std::array<vanguard::u8, 16> parameters{};
        std::array<streaming::DependencyDescriptor, 4> materialDependencies{};
        vanguard::u32 materialDependencyCount = 0;
        vanguard::crypto::Digest256 materialContent;
        vanguard::crypto::Digest256 materialLayout;
        vanguard::crypto::Digest256 materialDomain;
        vanguard::crypto::Digest256 textureType;
        shader::MaterialResourceShape textureShape;
    };

    [[nodiscard]] bool Compile(const char* const source, const char* const target, CompiledShader& output) noexcept
    {
        ID3DBlob* diagnostics = nullptr;
        const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &output.bytecode, &diagnostics);
        if (diagnostics != nullptr)
            diagnostics->Release();
        return SUCCEEDED(result) && output.bytecode != nullptr;
    }

    [[nodiscard]] bool BuildTexture(ByteArray& output, const vanguard::u32 variant) noexcept
    {
        std::array<vanguard::u8, 64> pixels{};
        for (vanguard::u32 index = 0; index < pixels.size(); ++index)
            pixels[index] = static_cast<vanguard::u8>(index * 17u + variant * 29u + 3u);
        const std::array<textures::SubresourceBuildRecord, 1> subresources{{{0, 0, 0, pixels.data(), pixels.size(), 16, 64}}};
        textures::BuildDescription description;
        description.dimension = textures::TextureDimension::Texture2D;
        description.format = textures::PixelFormat::R8G8B8A8UNorm;
        description.flags = textures::TextureFlags::Streamable | textures::TextureFlags::DirectGpuUpload;
        description.width = 4;
        description.height = 4;
        description.depth = 1;
        description.arrayLayers = 1;
        description.mipCount = 1;
        description.mipTailFirstLevel = 0;
        description.sourceFingerprint = vanguard::crypto::Sha256(&variant, sizeof(variant));
        description.subresources = {subresources.data(), static_cast<vanguard::u32>(subresources.size())};
        filesystem::MemoryFileWriter writer(output);
        return textures::WriteTexture(writer, description) == textures::Result::Success;
    }

    [[nodiscard]] bool BuildMesh(ByteArray& output, const resources::ResourceReference materialReference) noexcept
    {
        const std::array<float, 9> vertices{{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f}};
        const std::array<vanguard::u16, 3> indices{{0, 1, 2}};
        const std::array<meshes::BufferBuildRecord, 2> buffers{{
            {1, meshes::BufferKind::Vertex, 12, sizeof(vertices)},
            {2, meshes::BufferKind::Index, 2, sizeof(indices)},
        }};
        const std::array<meshes::PageBuildRecord, 2> pages{{
            {1, 0, vertices.data(), sizeof(vertices), 4, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload},
            {2, 0, indices.data(), sizeof(indices), 4, meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload},
        }};
        const meshes::VertexLayoutBuildRecord layout{1};
        const meshes::VertexStreamBuildRecord stream{1, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 0, 1, 0, 12};
        const meshes::MaterialSlotBuildRecord materialSlot{1, 0x3c042000ull, materialReference};
        const meshes::LodBuildRecord lod{0, 1.0f};
        meshes::Bounds bounds;
        bounds.minimum[0] = -1.0f;
        bounds.minimum[1] = -1.0f;
        bounds.minimum[2] = -1.0f;
        bounds.maximum[0] = 1.0f;
        bounds.maximum[1] = 1.0f;
        bounds.maximum[2] = 1.0f;
        bounds.sphereRadius = 1.5f;
        const meshes::SubmeshBuildRecord submesh{
            0x3c042100ull, 0x3c042101ull, 0, 1, 1, 2, meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::CastsShadow | meshes::SubmeshFlags::TwoSided, 0, 3, 0, 3,
            bounds};
        meshes::BuildDescription description;
        description.kind = meshes::MeshKind::Static;
        description.name = 0x3c042200ull;
        description.bounds = bounds;
        description.sourceFingerprint = vanguard::crypto::Sha256("material-runtime-mesh", 21);
        description.buffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
        description.pages = {pages.data(), static_cast<vanguard::u32>(pages.size())};
        description.vertexLayouts = {&layout, 1};
        description.vertexStreams = {&stream, 1};
        description.materialSlots = {&materialSlot, 1};
        description.lods = {&lod, 1};
        description.submeshes = {&submesh, 1};
        filesystem::MemoryFileWriter writer(output);
        return meshes::WriteMesh(writer, description) == meshes::Result::Success;
    }

    [[nodiscard]] bool BuildFixture(ArtifactFixture& output, const vanguard::u32 variant) noexcept
    {
        CompiledShader vertex;
        CompiledShader pixel;
        if (!Compile("cbuffer DrawContext : register(b0) { float4 drawScale; }; float4 main(float3 p : POSITION) : SV_Position { return float4(p * drawScale.xyz, 1.0); }", "vs_5_0", vertex) ||
            !Compile("float4 main() : SV_Target0 { return float4(0.25, 0.5, 0.75, 1.0); }", "ps_5_0", pixel) || !BuildTexture(output.textureBytes, variant + 1u))
            return false;

        const shader::StageBuildRecord stages[]{
            {shader::ShaderStage::Vertex, shader::NativeFormat::Dxil, 0x3c040100ull + variant, vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(), "main"},
            {shader::ShaderStage::Fragment, shader::NativeFormat::Dxil, 0x3c040200ull + variant, pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(), "main"}};
        constexpr vanguard::u64 drawContext = 0x3c040800ull;
        const std::array<shader::DescriptorBinding, 3> bindings{{
            {MaterialParameters, 2, 0, 1, shader::BindingKind::ConstantBuffer, shader::BindingAccess::Read, shader::StageBit(shader::ShaderStage::Fragment)},
            {MaterialTextureRole, 2, 1, 2, shader::BindingKind::SampledTexture, shader::BindingAccess::Read, shader::StageBit(shader::ShaderStage::Fragment)},
            {drawContext, 0, 0, 1, shader::BindingKind::ConstantBuffer, shader::BindingAccess::Read, shader::StageBit(shader::ShaderStage::Vertex)},
        }};
        const shader::ConstantMember constantMember{MaterialParameters, 0, 16, 0, 0, shader::ScalarType::F32, 1, 4, false};
        const shader::ConstantBuffer constantBuffers[]{{MaterialParameters, 2, 0, 16, 0, 1}, {drawContext, 0, 0, 16, 1, 1}};
        const shader::ConstantMember constantMembers[]{constantMember, {drawContext, 0, 16, 0, 0, shader::ScalarType::F32, 1, 4, false}};
        output.textureType = vanguard::crypto::Sha256("Texture2D<float4>", 17);
        output.textureShape = {shader::MaterialResourceAccess::Read,
                               shader::MaterialTextureDimension::D2,
                               shader::MaterialBufferKind::None,
                               shader::MaterialSamplerKind::None,
                               shader::ScalarType::F32,
                               4,
                               shader::MaterialResourceShapeFlags::None,
                               0,
                               0};
        const std::array<shader::MaterialResourceRole, 2> resourceRoles{{
            {MaterialTextureRole, 0, 0, shader::MaterialResourceKind::Texture, shader::MaterialResourceFlags::Required, 0, output.textureType, output.textureShape},
            {MaterialTextureRole, 1, 1, shader::MaterialResourceKind::Texture, shader::MaterialResourceFlags::None, 0, output.textureType, output.textureShape},
        }};
        const shader::VertexInput input{0x3c040300ull, 0, 0, shader::NumericClass::FloatingPoint, 3, 32};
        const shader::FragmentOutput fragmentOutput{0x3c040400ull, 0, 0, shader::NumericClass::FloatingPoint, 0x0f};

        shader::MaterialContractBuildDescription contract;
        contract.domain.name = 0x3c040500ull;
        contract.domain.schemaVersion = 1;
        contract.domain.legalStages = shader::StageBit(shader::ShaderStage::Fragment);
        contract.domain.inputType = vanguard::crypto::Sha256("Material3C4Input", 16);
        contract.domain.outputType = vanguard::crypto::Sha256("Material3C4Output", 17);
        contract.accessorAbiVersion = 1;
        contract.parameterByteSize = 16;
        contract.parameters = {&constantMember, 1};
        contract.resources = {resourceRoles.data(), static_cast<vanguard::u32>(resourceRoles.size())};

        shader::BuildDescription shaderDescription;
        shaderDescription.kind = shader::ProgramKind::Graphics;
        shaderDescription.program = 0x3c040600ull + variant;
        shaderDescription.permutation = vanguard::crypto::Sha256(&variant, sizeof(variant));
        shaderDescription.compilerFingerprint = vanguard::crypto::Sha256("material 3C.4 D3DCompile", 24);
        shaderDescription.pipelineInterface.stages = shader::StageBit(shader::ShaderStage::Vertex) | shader::StageBit(shader::ShaderStage::Fragment);
        shaderDescription.pipelineInterface.primitiveClass = shader::PrimitiveClass::Triangle;
        shaderDescription.pipelineInterface.renderTargetCount = 1;
        shaderDescription.stages = stages;
        shaderDescription.bindings = {bindings.data(), static_cast<vanguard::u32>(bindings.size())};
        shaderDescription.constantBuffers = constantBuffers;
        shaderDescription.constantMembers = constantMembers;
        shaderDescription.vertexInputs = {&input, 1};
        shaderDescription.fragmentOutputs = {&fragmentOutput, 1};
        shaderDescription.materialContract = &contract;
        filesystem::MemoryFileWriter shaderWriter(output.shaderBytes);
        if (shader::WriteShader(shaderWriter, shaderDescription) != shader::Result::Success)
            return false;

        filesystem::MemoryFileReader shaderReader(output.shaderBytes, 0);
        shader::ShaderFile shaderFile;
        if (shaderFile.Open(shaderReader) != shader::Result::Success || shaderFile.GetMaterialContract() == nullptr)
            return false;
        output.materialLayout = shaderFile.GetMaterialContract()->layoutFingerprint;
        output.materialDomain = shaderFile.GetMaterialContract()->domainFingerprint;

        pipeline::ShaderReference pipelineShader{output.shaderReference.GetPath().Id(),        shaderFile.GetPermutation(), shaderFile.BindingLayoutFingerprint(),
                                                 shaderFile.GetPipelineInterfaceFingerprint(), output.materialDomain,       output.materialLayout};
        const pipeline::VertexStream vertexStream{0, 12, pipeline::InputRate::PerVertex, 1};
        const pipeline::VertexAttribute vertexAttribute{0x3c040300ull, 0, 0, 0, 0, shader::NumericClass::FloatingPoint, 3, 32, pipeline::Format::R32G32B32Float, "POSITION"};
        pipeline::BuildDescription pipelineDescription;
        pipelineDescription.kind = pipeline::PipelineKind::Graphics;
        pipelineDescription.name = 0x3c040700ull + variant;
        pipelineDescription.shaders = {&pipelineShader, 1};
        pipelineDescription.vertexStreams = {&vertexStream, 1};
        pipelineDescription.vertexAttributes = {&vertexAttribute, 1};
        pipelineDescription.graphics.blend.attachmentCount = 1;
        pipelineDescription.graphics.attachmentPolicy = pipeline::AttachmentPolicy::Deferred;
        filesystem::MemoryFileWriter pipelineWriter(output.pipelineBytes);
        if (pipeline::WritePipeline(pipelineWriter, pipelineDescription) != pipeline::Result::Success)
            return false;

        filesystem::MemoryFileReader pipelineReader(output.pipelineBytes, 0);
        pipeline::PipelineFile pipelineFile;
        if (pipelineFile.Open(pipelineReader) != pipeline::Result::Success)
            return false;

        for (vanguard::u32 index = 0; index < output.parameters.size(); ++index)
            output.parameters[index] = static_cast<vanguard::u8>(variant * 37u + index * 11u + 5u);
        const material::TechniqueBuildRecord technique{MaterialTechnique, output.pipelineReference, &pipelineFile};
        const material::ConstantValueBuildRecord constant{MaterialParameters, output.parameters.data(), static_cast<vanguard::u32>(output.parameters.size())};
        const std::array<material::ResourceValueBuildRecord, 2> resources{{
            {MaterialTextureRole, 0, output.requiredTexture, resources::DependencyKind::Required},
            {MaterialTextureRole, 1, output.optionalTexture, resources::DependencyKind::Optional},
        }};
        const material::ResourceTypeCompatibility compatibility{material::ResourceParameterKind::Texture, textures::TextureResourceType};
        material::BuildDescription materialDescription;
        materialDescription.name = 0x3c040800ull + variant;
        materialDescription.shader = output.shaderReference;
        materialDescription.shaderReflection = &shaderFile;
        materialDescription.techniques = {&technique, 1};
        materialDescription.constants = {&constant, 1};
        materialDescription.resources = {resources.data(), static_cast<vanguard::u32>(resources.size())};
        materialDescription.resourceTypeCompatibility = {&compatibility, 1};
        filesystem::MemoryFileWriter materialWriter(output.materialBytes);
        if (material::WriteMaterial(materialWriter, materialDescription) != material::Result::Success)
            return false;

        filesystem::MemoryFileReader materialReader(output.materialBytes, 0);
        material::MaterialFile materialFile;
        if (materialFile.Open(materialReader) != material::Result::Success)
            return false;
        output.materialContent = materialFile.GetContentFingerprint();
        for (const material::ResourceDependency& dependency : materialFile.GetDependencies())
        {
            if (dependency.kind == resources::DependencyKind::Soft || output.materialDependencyCount >= output.materialDependencies.size())
                continue;
            output.materialDependencies[output.materialDependencyCount++] = {dependency.resource, dependency.kind};
        }
        return output.materialDependencyCount == 4;
    }

    [[nodiscard]] bool Save(const filesystem::AbsolutePath& path, const ByteArray& bytes) noexcept
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        writer->Serialize(const_cast<void*>(static_cast<const void*>(bytes.Data())), bytes.Size());
        writer->Flush();
        return writer->GetSize() == bytes.Size();
    }

    void DeleteDirectoryTree(filesystem::Manager& files, const filesystem::AbsolutePath& directory) noexcept
    {
        vanguard::containers::DynamicArray<filesystem::AbsolutePath> children(vanguard::memory::pools::Assets::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            DeleteDirectoryTree(files, child);
        vanguard::containers::DynamicArray<filesystem::AbsolutePath> storedFiles(vanguard::memory::pools::Assets::GetInstance());
        files.FindFiles(directory, vanguard::containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            static_cast<void>(files.DeleteFile(storedFile));
        static_cast<void>(files.DeletePath(directory));
    }

    inline constexpr vanguard::application::ServiceId CatalogMountServiceId = 0x4653434154414cull;

    bool BuildCopyFixture(ArtifactFixture& fixture) noexcept
    {
        using namespace vanguard;
        char path[2048];
        std::snprintf(path, sizeof(path), "%s/../../../rendering/shaders/fullscreen_copy.vsl", __FILE__);
        auto file = filesystem::RawFileReader::Create(filesystem::AbsolutePath::CreateFilePath(path));
        if (!file)
            return false;
        ByteArray source(memory::pools::Rendering::GetInstance());
        source.Resize(static_cast<u32>(file->GetSize()));
        file->Serialize(source.Data(), source.Size());
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        shader_tools::CompileRequest request;
        request.sourceName = "rendering/shaders/fullscreen_copy.vsl";
        request.moduleName = "fullscreen_copy";
        request.source = source;
        const shader_tools::EntryPoint entries[] = {{"CopyVertexMain", shader::ShaderStage::Vertex}, {"CopyFragmentMain", shader::ShaderStage::Fragment}};
        request.entryPoints = entries;
        shader_tools::CompileOutput output;
        if (compiler.Compile(request, output) != shader_tools::Result::Success)
            return false;
        filesystem::MemoryFileWriter writer(fixture.shaderBytes);
        if (output.WriteShader(writer, 0x4653434f5059ull, crypto::Sha256(source.Data(), source.Size())) != shader_tools::Result::Success)
            return false;
        filesystem::MemoryFileReader reader(fixture.shaderBytes, 0);
        shader::ShaderFile shaderFile;
        if (shaderFile.Open(reader) != shader::Result::Success)
            return false;
        const pipeline::ShaderReference reference{fixture.shaderReference.GetPath().Id(), shaderFile.GetPermutation(), shaderFile.BindingLayoutFingerprint(), shaderFile.GetPipelineInterfaceFingerprint()};
        pipeline::BuildDescription description;
        description.name = 0x4653434f5059ull;
        description.shaders = {&reference, 1};
        description.graphics.rasterizer.cull = pipeline::CullMode::None;
        description.graphics.blend.attachmentCount = 1;
        filesystem::MemoryFileWriter pipelineWriter(fixture.pipelineBytes);
        return pipeline::WritePipeline(pipelineWriter, description) == pipeline::Result::Success;
    }

    class CatalogMountService final : public vanguard::application::Service
    {
    public:
        CatalogMountService(const ArtifactFixture& fixture, ArtifactFixture& copy) noexcept : m_fixture(fixture), m_copy(copy) {}
        filesystem::AbsolutePath directory;
        bool mounted = false;

    protected:
        vanguard::application::LifecycleStatus OnInitialize(vanguard::application::ServiceContext& context) noexcept override
        {
            auto* service = engine::FindResourceStreamingService(context);
            directory = filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("vanguard_material_runtime_service_catalog");
            const auto shaderPath = directory.AddFilePath("startup.vshader");
            const auto pipelinePath = directory.AddFilePath("startup.vppl");
            const auto copyShaderPath = directory.AddFilePath("copy.vshader");
            const auto copyPipelinePath = directory.AddFilePath("copy.vpipeline");
            if (service == nullptr || !filesystem::GetManager().CreatePath(directory) || !Save(shaderPath, m_fixture.shaderBytes) || !Save(pipelinePath, m_fixture.pipelineBytes))
                return vanguard::application::LifecycleStatus::Failure("startup catalog fixture write failed");
            if (!BuildCopyFixture(m_copy) || !Save(copyShaderPath, m_copy.shaderBytes) || !Save(copyPipelinePath, m_copy.pipelineBytes))
                return vanguard::application::LifecycleStatus::Failure("copy startup fixture cooking failed");
            const streaming::DependencyDescriptor dependency{m_fixture.shaderReference, resources::DependencyKind::Required};
            const streaming::DependencyDescriptor copyDependency{m_copy.shaderReference, resources::DependencyKind::Required};
            mounted = service->GetStreamer().RegisterLoose({m_fixture.shaderReference, shaderPath, {}, 0, 0}) &&
                      service->GetStreamer().RegisterLoose({m_fixture.pipelineReference, pipelinePath, {&dependency, 1}, 0, 0}) &&
                      service->GetStreamer().RegisterLoose({m_copy.shaderReference, copyShaderPath, {}, 0, 0}) &&
                      service->GetStreamer().RegisterLoose({m_copy.pipelineReference, copyPipelinePath, {&copyDependency, 1}, 0, 0});
            return mounted ? vanguard::application::LifecycleStatus::Success() : vanguard::application::LifecycleStatus::Failure("startup catalog fixture mount failed");
        }

    private:
        const ArtifactFixture& m_fixture;
        ArtifactFixture& m_copy;
    };

    [[nodiscard]] bool BuildPackage(const ArtifactFixture& fixture, ByteArray& output) noexcept
    {
        const packages::Dependency pipelineDependency{fixture.shaderReference.GetPath().Id(), shader::ShaderResourceType, resources::DependencyKind::Required};
        std::array<packages::Dependency, 4> materialDependencies{};
        for (vanguard::u32 index = 0; index < fixture.materialDependencyCount; ++index)
        {
            const streaming::DependencyDescriptor& dependency = fixture.materialDependencies[index];
            materialDependencies[index] = {dependency.reference.GetPath().Id(), dependency.reference.ExpectedType(), dependency.kind};
        }
        const packages::BuildSegment shaderSegment{fixture.shaderBytes.Data(), fixture.shaderBytes.Size(), packages::Codec::None, 4, packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment pipelineSegment{fixture.pipelineBytes.Data(), fixture.pipelineBytes.Size(), packages::Codec::None, 4,
                                                     packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment materialSegment{fixture.materialBytes.Data(), fixture.materialBytes.Size(), packages::Codec::None, 4,
                                                     packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        const packages::BuildSegment textureSegment{fixture.textureBytes.Data(), fixture.textureBytes.Size(), packages::Codec::None, 4, packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
        packages::BuildResource shaderResource{fixture.paths.shader, shader::ShaderResourceType, packages::ResourceFlags::Streamable, {&shaderSegment, 1}, {}};
        packages::BuildResource pipelineResource{fixture.paths.pipeline, pipeline::PipelineResourceType, packages::ResourceFlags::Streamable, {&pipelineSegment, 1}, {&pipelineDependency, 1}};
        packages::BuildResource materialResource{
            fixture.paths.material, material::MaterialResourceType, packages::ResourceFlags::Streamable, {&materialSegment, 1}, {materialDependencies.data(), fixture.materialDependencyCount}};
        packages::BuildResource textureResource{fixture.paths.requiredTexture, textures::TextureResourceType, packages::ResourceFlags::Streamable, {&textureSegment, 1}, {}};
        std::array<packages::BuildResource*, 4> resources{{&shaderResource, &pipelineResource, &materialResource, &textureResource}};
        std::sort(resources.begin(), resources.end(), [](const packages::BuildResource* const left, const packages::BuildResource* const right) noexcept
                  { return packages::HashResourcePath(left->path) < packages::HashResourcePath(right->path); });
        filesystem::MemoryFileWriter writer(output);
        packages::PackageWriter packageWriter;
        if (packageWriter.Begin(writer) != packages::Result::Success)
            return false;
        for (const packages::BuildResource* const resource : resources)
            if (packageWriter.Add(*resource) != packages::Result::Success)
                return false;
        return packageWriter.Finalize() == packages::Result::Success;
    }

    [[nodiscard]] bool RunFrame(engine::FramePipelineService& frames, TestClock& clock) noexcept
    {
        clock.ticks += 16;
        engine::FrameFailure failure;
        if (frames.RunFrame(&failure))
            return true;
        std::fprintf(stderr, "[materialRuntimeServiceTests] frame failed: phase=%u participant=%llu message=%s\n", static_cast<unsigned>(failure.phase), static_cast<unsigned long long>(failure.participant),
                     failure.message != nullptr ? failure.message : "unknown");
        return false;
    }

    [[nodiscard]] bool WaitForResourceDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = pipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeOperations == 0 && pipelineStats.activeJobs == 0 && pipelineStats.activePreparations == 0 && pipelineStats.externalRequests == 0 && streamingStats.activeLoads == 0 &&
                streamingStats.activeReads == 0 && streamingStats.stagingBytesInUse == 0)
                return true;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveBinding(engine::FramePipelineService& frames, TestClock& clock, rendering::MaterialSceneBindingBridge& bindings, const rendering::RenderProxyHandle proxy,
                                    rendering::MaterialSceneBindingInfo& info) noexcept
    {
        rendering::MaterialSceneBindingFailure failure;
        for (vanguard::u32 attempt = 0; attempt < 512; ++attempt)
        {
            if (!RunFrame(frames, clock) || !bindings.GetInfo(proxy, info, &failure))
                return false;
            if (info.activeResidency.IsValid() && !info.candidateResidency.IsValid() && !info.awaitingScenePublication)
                return true;
            if (info.candidateState == rendering::MaterialResidencyState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveTechnique(engine::FramePipelineService& frames, TestClock& clock, rendering::MaterialResidencyRuntime& residency, const rendering::MaterialTechniqueRequest& request,
                                      rendering::MaterialTechniqueInfo& info) noexcept
    {
        rendering::MaterialResidencyRuntimeFailure failure;
        for (vanguard::u32 attempt = 0; attempt < 512; ++attempt)
        {
            if (!residency.GetTechniqueInfo(request, info, &failure))
                return false;
            if (info.state == rendering::MaterialTechniqueState::Ready)
                return true;
            if (info.state == rendering::MaterialTechniqueState::Failed || !RunFrame(frames, clock))
                return false;
            std::this_thread::yield();
        }
        return false;
    }

    template <typename T> [[nodiscard]] bool ReadGpuSceneElement(rendering::GpuSceneTables& tables, const vanguard::u32 index, T& output, rhi::Failure& failure, const vanguard::u64 commandName) noexcept
    {
        rendering::GpuSceneElementAddress address;
        if (!tables.Resolve<T>(index, address))
            return false;
        rhi::BufferDesc desc;
        desc.size = sizeof(T);
        desc.usage = rhi::BufferUsage::CopyDestination;
        desc.initialState = rhi::ResourceState::CopyDestination;
        desc.memoryType = rhi::MemoryType::Readback;
        rhi::Buffer readback(rhi::AdoptReference, rhi::CreateBuffer(desc, {}, &failure));
        const rhi::CommandListRef commandList = rhi::CreateCommandList(rhi::CommandListType::CopySync, commandName, &failure);
        const rhi::ResourceState shaderRead = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
        if (!readback || !commandList || !rhi::BindCommandList(commandList, &failure) || !rhi::TransitionBuffer(address.buffer, rhi::ResourceState::Unknown, rhi::ResourceState::CopySource, &failure) ||
            !rhi::CopyBuffer(readback, 0, address.buffer, address.byteOffset, sizeof(T), &failure) || !rhi::TransitionBuffer(address.buffer, rhi::ResourceState::Unknown, shaderRead, &failure))
            return false;
        rhi::UnbindCommandList();
        const rhi::CommandListRef submissions[]{commandList};
        rhi::GpuFence completion;
        if (!rhi::CloseAndSubmitCommandLists("material 3C.4 GPU Scene readback", submissions, rhi::CommandListSyncType::None, completion, &failure) ||
            !rhi::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
            return false;
        const T* const mapped = static_cast<const T*>(rhi::LockBuffer(readback, 0, sizeof(T), &failure));
        if (mapped == nullptr)
            return false;
        output = *mapped;
        rhi::UnlockBuffer(readback);
        return true;
    }

    [[nodiscard]] bool SubmitRetirementFences(rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        fences = {};
        const rhi::CommandListType types[]{rhi::CommandListType::Default, rhi::CommandListType::Compute, rhi::CommandListType::CopyAsync};
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            const rhi::CommandListRef list = rhi::CreateCommandList(types[index], 0x3c040900ull + index, &failure);
            if (!list || !rhi::BindCommandList(list, &failure))
                return false;
            rhi::UnbindCommandList();
            const rhi::CommandListRef lists[]{list};
            rhi::GpuFence completion;
            if (!rhi::CloseAndSubmitCommandLists("material 3C.4 retirement", lists, rhi::CommandListSyncType::None, completion, &failure))
                return false;
            fences.Include(completion);
        }
        return true;
    }

    [[nodiscard]] bool WaitRetirementFences(const rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        return rhi::WaitForGpuFence({rhi::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) && rhi::WaitForGpuFence({rhi::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
               rhi::WaitForGpuFence({rhi::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
    }

    [[nodiscard]] const resources::ResourceHandle* FindDependency(const material::MaterialResourceObject& object, const resources::ResourceReference reference) noexcept
    {
        for (const material::LoadedMaterialDependency& dependency : object.GetLoadedDependencies())
            if (dependency.resource == reference && dependency.handle.IsValid())
                return &dependency.handle;
        return nullptr;
    }

    [[nodiscard]] bool VerifyMaterialImage(engine::RenderingService& service, const ArtifactFixture& fixture, const resources::ResourceHandle& root, const rendering::MaterialSceneBindingInfo& binding,
                                           const rendering::GpuTextureResidencyHandle requiredTexture, const rendering::GpuTextureResidencyHandle fallbackTexture,
                                           const rendering::RenderProxyHandle proxy) noexcept
    {
        rendering::MaterialResidencyInfo residencyInfo;
        rendering::MaterialResidencyRuntimeFailure residencyFailure;
        if (!service.GetMaterialResidency().GetInfo(binding.activeResidency, residencyInfo, &residencyFailure) || residencyInfo.state != rendering::MaterialResidencyState::Resident ||
            residencyInfo.material != binding.activeMaterial || residencyInfo.resourcePath != root.GetPath() || residencyInfo.resourceGeneration != root.GetGeneration())
            return false;
        rendering::GpuMaterial materialImage;
        rendering::GpuMaterialResource requiredImage;
        rendering::GpuMaterialResource fallbackImage;
        std::array<rendering::GpuMaterialParameterWord, 4> parameterWords{};
        rendering::GpuDecal decalImage;
        rendering::RenderSceneGpuIdentity decalIdentity;
        rhi::Failure failure;
        rendering::GpuSceneTables& tables = service.GetGpuScene().GetTables();
        if (!ReadGpuSceneElement(tables, binding.activeMaterial.index, materialImage, failure, 0x3c041000ull) || materialImage.generation != binding.activeMaterial.generation ||
            materialImage.parameterByteSize != fixture.parameters.size() || materialImage.resourceCount != 2 || materialImage.materialLayout == rendering::InvalidGpuSceneIndex ||
            !ReadGpuSceneElement(tables, materialImage.firstResource, requiredImage, failure, 0x3c041001ull) ||
            !ReadGpuSceneElement(tables, materialImage.firstResource + 1u, fallbackImage, failure, 0x3c041002ull))
            return false;
        for (vanguard::u32 index = 0; index < parameterWords.size(); ++index)
            if (!ReadGpuSceneElement(tables, (materialImage.parameterByteOffset >> 2u) + index, parameterWords[index], failure, 0x3c041010ull + index))
                return false;
        rendering::MaterialProgramLayoutView layout;
        if (!service.GetMaterialProgramLayouts().Get({materialImage.materialLayout}, layout) || layout.layoutFingerprint != fixture.materialLayout || layout.domainFingerprint != fixture.materialDomain ||
            layout.parameterByteSize != fixture.parameters.size())
            return false;
        if (requiredImage.type != rendering::GpuMaterialResourceType::Texture || requiredImage.resource != requiredTexture.index || requiredImage.samplerDescriptor != rendering::InvalidGpuDescriptorIndex ||
            fallbackImage.type != rendering::GpuMaterialResourceType::Texture || fallbackImage.resource != fallbackTexture.index || fallbackImage.samplerDescriptor != rendering::InvalidGpuDescriptorIndex ||
            std::memcmp(parameterWords.data(), fixture.parameters.data(), fixture.parameters.size()) != 0)
            return false;
        if (!service.GetGpuScene().GetScenePublisher().GetIdentity(proxy, decalIdentity) || decalIdentity.kind != rendering::RenderSceneGpuObjectKind::Decal ||
            !ReadGpuSceneElement(tables, decalIdentity.allocation.first, decalImage, failure, 0x3c041020ull))
            return false;
        return decalImage.material == binding.activeMaterial.index;
    }

    [[nodiscard]] bool VerifyMeshTopology(engine::RenderingService& service, const rendering::MeshResidencyInfo& info, const rendering::GpuMaterialHandle expectedMaterial) noexcept
    {
        const rendering::RenderPhaseId opaquePhase = service.GetRenderPhases().Find(rendering::standardRenderPhases::Opaque);
        if (!info.renderable.IsValid() || !opaquePhase.IsValid())
            return false;
        rendering::GpuSceneTables& tables = service.GetGpuScene().GetTables();
        rhi::Failure failure;
        rendering::GpuRenderable renderable;
        if (!ReadGpuSceneElement(tables, info.renderable.index, renderable, failure, 0x3c042300ull) || renderable.generation != info.renderable.generation || renderable.lodCount != 1 ||
            (renderable.phaseMaskLow & (1u << opaquePhase.index)) == 0)
            return false;
        rendering::GpuLod lod;
        rendering::GpuPrimitive primitive;
        rendering::GpuPhaseParticipation participation;
        return ReadGpuSceneElement(tables, renderable.firstLod, lod, failure, 0x3c042301ull) && lod.primitiveCount == 1 && ReadGpuSceneElement(tables, lod.firstPrimitive, primitive, failure, 0x3c042302ull) &&
               primitive.material == expectedMaterial.index && primitive.phaseParticipationCount == 1 &&
               (static_cast<vanguard::u32>(primitive.flags) & static_cast<vanguard::u32>(rendering::GpuPrimitiveFlags::TwoSided)) != 0 &&
               ReadGpuSceneElement(tables, primitive.firstPhaseParticipation, participation, failure, 0x3c042303ull) && participation.phase == opaquePhase.index;
    }
} // namespace

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "materialRuntimeServiceTests"), "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    ArtifactFixture looseFixture(LoosePaths);
    ArtifactFixture packageFixture(PackagePaths);
    ArtifactFixture failureFixture(FailurePaths);
    ByteArray fallbackBytes(vanguard::memory::pools::Rendering::GetInstance());
    ByteArray packageBytes(vanguard::memory::pools::Rendering::GetInstance());
    ByteArray meshBytes(vanguard::memory::pools::Rendering::GetInstance());
    Check(BuildFixture(looseFixture, 1) && BuildFixture(packageFixture, 2) && BuildFixture(failureFixture, 3) && BuildTexture(fallbackBytes, 99) && BuildPackage(packageFixture, packageBytes),
          "build current-format loose and indexed-VPAK material fixtures");
    Check(BuildMesh(meshBytes, looseFixture.materialReference), "build current-format VMESH topology fixture");

    ArtifactFixture copyFixture(CopyPaths);
    CatalogMountService catalogMount(looseFixture, copyFixture);
    vanguard::application::EngineHost host;
    vanguard::application::HostFailure hostFailure;
    engine::RenderingServiceConfig renderingConfig;
    const engine::RenderingServiceConfig::Shader startupShaders[] = {{"StartupCatalogShader", looseFixture.shaderReference}, {"FullscreenCopy", copyFixture.shaderReference}};
    pipeline::AttachmentSignature startupAttachments;
    startupAttachments.colorCount = 1;
    // Keep the later material test's UNorm variant cold; startup prepares a distinct concrete pipeline.
    startupAttachments.colors[0] = {pipeline::Format::R16G16B16A16Float, shader::NumericClass::FloatingPoint};
    pipeline::AttachmentSignature copyAttachments;
    copyAttachments.colorCount = 1;
    copyAttachments.colors[0] = {pipeline::Format::R8G8B8A8UNorm, shader::NumericClass::FloatingPoint};
    const rhi::BindingLayoutEntry copyConstants{0, 24, rhi::BindingType::PushConstants};
    const rhi::BindingLayoutDesc copyLayout{&copyConstants, 1, 0, rhi::ShaderStageBit(rhi::ShaderStage::Pixel)};
    const rhi::BindingLayoutEntry materialConstants{0, 16, rhi::BindingType::PushConstants};
    const rhi::BindingLayoutDesc materialLayout{&materialConstants, 1, 0, rhi::ShaderStageBit(rhi::ShaderStage::Vertex)};
    const engine::RenderingServiceConfig::Pipeline startupPipelines[] = {{"StartupCatalogPipeline", looseFixture.pipelineReference, startupAttachments, {&materialLayout, 1}},
                                                                         {"FullscreenCopy", copyFixture.pipelineReference, copyAttachments, {&copyLayout, 1}}};
    renderingConfig.materialBindingLayouts = {&materialLayout, 1};
    renderingConfig.rendererShaders = startupShaders;
    renderingConfig.rendererPipelines = startupPipelines;
    renderingConfig.rendererCatalogSourceService = CatalogMountServiceId;
    renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
    renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
    renderingConfig.resourceDescriptors.capacity = 128;
    renderingConfig.samplerDescriptors.capacity = 16;
    renderingConfig.maximumMaterialProgramLayouts = 4;
    renderingConfig.gpuScene.tables.maximumPagesPerTable = 2;
    renderingConfig.gpuScene.lifetime.retirementEpochCount = 4;
    renderingConfig.gpuScene.lifetime.initialRetirementsPerEpoch = 16;
    renderingConfig.gpuScene.upload.bytesPerSegment = 2u * 1024u * 1024u;
    renderingConfig.gpuScene.upload.maximumUpdatesPerBatch = 128;
    renderingConfig.gpuScene.upload.maximumCopiesPerBatch = 256;
    renderingConfig.gpuScene.definitions.maximumGeometries = 2;
    renderingConfig.gpuScene.definitions.maximumMaterials = 4;
    renderingConfig.gpuScene.definitions.maximumRenderables = 2;
    renderingConfig.gpuScene.definitions.maximumDefinitionsPerBatch = 4;
    renderingConfig.gpuScene.definitions.maximumAllocationsPerBatch = 16;
    renderingConfig.gpuScene.definitions.maximumMaterialResourcesPerBatch = 8;
    renderingConfig.gpuScene.definitions.maximumMaterialParameterBytesPerBatch = 64;
    renderingConfig.gpuScene.maximumExternalContributions = 4;
    renderingConfig.textureResidency.residency.maximumTextures = 4;
    renderingConfig.textureResidency.residency.maximumPendingInstallations = 4;
    renderingConfig.textureResidency.residency.maximumPendingRetirements = 4;
    renderingConfig.textureResidency.uploader.maximumRequests = 4;
    renderingConfig.textureResidency.uploader.maximumAcquisitionStartsPerTick = 4;
    renderingConfig.textureResidency.uploader.maximumCandidatesPerBatch = 4;
    renderingConfig.textureResidency.uploader.maximumCompletionPollsPerTick = 4;
    renderingConfig.textureResidency.uploader.maximumReadyCandidates = 4;
    renderingConfig.textureResidency.uploader.maximumWritesPerBatch = 8;
    renderingConfig.textureResidency.uploader.maximumCopiesPerBatch = 8;
    renderingConfig.textureResidency.maximumResidencyRecords = 4;
    renderingConfig.textureResidency.maximumDemands = 8;
    renderingConfig.textureResidency.maximumInstallationsPerTick = 4;
    renderingConfig.textureResidency.maximumTableInstallationsPerFrame = 4;
    renderingConfig.textureResidency.maximumStateChecksPerTick = 4;
    renderingConfig.materialResources.maximumProviders = 2;
    renderingConfig.materialResources.maximumFallbacks = 2;
    renderingConfig.materialResources.maximumOperations = 4;
    renderingConfig.materialResources.maximumDescriptorCacheEntries = 4;
    renderingConfig.materialMaterializer.maximumOperations = 2;
    renderingConfig.materialMaterializer.maximumRolesPerMaterial = 2;
    renderingConfig.materialMaterializer.maximumMaterialsPerBatch = 2;
    renderingConfig.materialMaterializer.maximumOperationsProgressedPerUpdate = 2;
    renderingConfig.materialMaterializer.maximumResourceRolePollsPerUpdate = 4;
    renderingConfig.materialResidency.maximumResidencies = 2;
    renderingConfig.materialResidency.maximumDemands = 3;
    renderingConfig.materialResidency.maximumTechniqueRequests = 2;
    renderingConfig.materialResidency.maximumNativePrograms = 2;
    renderingConfig.materialResidency.maximumResidencyChecksPerUpdate = 2;
    renderingConfig.materialResidency.maximumRetirementsPerSeal = 2;
    renderingConfig.materialBindings.maximumBindings = 2;
    renderingConfig.materialBindings.maximumChecksPerUpdate = 2;

    Check(engine::RegisterEngineModule(host, &hostFailure), "engine module registration");
    Check(engine::RegisterIoService(host, &hostFailure), "I/O service registration");
    Check(engine::RegisterFilesystemService(host, &hostFailure), "filesystem service registration");
    Check(engine::RegisterJobsService(host, &hostFailure), "jobs service registration");
    Check(engine::RegisterFramePipelineService(host, &hostFailure), "frame pipeline service registration");
    Check(engine::RegisterReflectionService(host, &hostFailure), "reflection service registration");
    Check(engine::RegisterResourcesService(host, &hostFailure), "resources service registration");
    Check(engine::RegisterResourceStreamingService(host, &hostFailure), "resource streaming service registration");
    const vanguard::application::ServiceDependency catalogDependencies[] = {{engine::ResourceStreamingServiceId, vanguard::application::DependencyKind::Required}};
    vanguard::application::ServiceDescriptor catalogDescriptor;
    catalogDescriptor.id = CatalogMountServiceId;
    catalogDescriptor.name = "catalogMount";
    catalogDescriptor.profiles = vanguard::application::ApplicationProfile::Runtime;
    catalogDescriptor.affinity = vanguard::application::ThreadAffinity::MainThread;
    catalogDescriptor.dependencies = {catalogDependencies, 1};
    catalogDescriptor.userData = &catalogMount;
    catalogDescriptor.create = [](void* value) noexcept -> vanguard::application::Service* { return static_cast<CatalogMountService*>(value); };
    catalogDescriptor.destroy = [](vanguard::application::Service*, void*) noexcept {};
    Check(host.RegisterService(engine::EngineModuleId, catalogDescriptor, &hostFailure), "startup catalog mount service registration");
    Check(engine::RegisterRenderingService(host, renderingConfig, &hostFailure), "rendering service registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Runtime, &hostFailure), "runtime service graph compilation");
    if (!host.Start(&hostFailure))
    {
        std::fprintf(stderr, "[materialRuntimeServiceTests] startup failed: code=%u service=%llu message=%s\n", static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                     hostFailure.message != nullptr ? hostFailure.message : "unknown");
        Check(false, "runtime service graph startup");
        vanguard::diagnostics::Shutdown();
        return 1;
    }

    Check(catalogMount.mounted, "catalog sources mounted before renderer catalog initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath directory = root.AddDirPath("vanguard_material_runtime_service");
    filesystem::Manager& files = filesystem::GetManager();
    DeleteDirectoryTree(files, directory);
    Check(files.CreatePath(directory), "create clean material runtime integration directory");
    const filesystem::AbsolutePath looseShaderPath = directory.AddFilePath("loose.vshader");
    const filesystem::AbsolutePath loosePipelinePath = directory.AddFilePath("loose.vppl");
    const filesystem::AbsolutePath looseMaterialPath = directory.AddFilePath("loose.vmat");
    const filesystem::AbsolutePath looseTexturePath = directory.AddFilePath("loose_required.vtex");
    const filesystem::AbsolutePath fallbackPath = directory.AddFilePath("fallback.vtex");
    const filesystem::AbsolutePath meshPath = directory.AddFilePath("mesh.vmesh");
    const filesystem::AbsolutePath failureShaderPath = directory.AddFilePath("failure.vshader");
    const filesystem::AbsolutePath failurePipelinePath = directory.AddFilePath("failure.vppl");
    const filesystem::AbsolutePath failureMaterialPath = directory.AddFilePath("failure.vmat");
    const filesystem::AbsolutePath packagePath = directory.AddFilePath("material_runtime.vpak");
    Check(Save(looseShaderPath, looseFixture.shaderBytes) && Save(loosePipelinePath, looseFixture.pipelineBytes) && Save(looseMaterialPath, looseFixture.materialBytes) &&
              Save(looseTexturePath, looseFixture.textureBytes) && Save(fallbackPath, fallbackBytes) && Save(failureShaderPath, failureFixture.shaderBytes) &&
              Save(failurePipelinePath, failureFixture.pipelineBytes) && Save(failureMaterialPath, failureFixture.materialBytes) && Save(meshPath, meshBytes) && Save(packagePath, packageBytes),
          "write loose and indexed-VPAK material artifacts");
    auto packageFile = filesystem::RawFileReader::Create(packagePath);
    packages::PackageReader packageReader;
    Check(packageFile && packageReader.Open(*packageFile) == packages::Result::Success, "open indexed material VPAK");
    packageFile.Reset();

    engine::FramePipelineService* const frames = engine::FindFramePipelineService(host);
    engine::ResourcesService* const resourcesService = engine::FindResourcesService(host);
    engine::ResourceStreamingService* const streamingService = engine::FindResourceStreamingService(host);
    engine::RenderingService* const renderingService = engine::FindRenderingService(host);
    Check(engine::tests::RunStaticSurfaceNativeProof(), "actual generated static surface shaders create float/quantized GBuffer/depth pipeline variants without recording draws");
    Check(frames != nullptr && resourcesService != nullptr && streamingService != nullptr && renderingService != nullptr, "material runtime services are discoverable");
    TestClock clock;
    engine::FrameFailure frameFailure;
    engine::FramePipelineConfig frameConfig;
    frameConfig.clock = {&ReadClock, 1'000, &clock};
    frameConfig.pacing = engine::FramePacingMode::Disabled;
    Check(frames != nullptr && frames->Configure(frameConfig, &frameFailure) && frames->Compile(&frameFailure), "material runtime frame pipeline compilation");

    streaming::ResourceStreamer& streamer = streamingService->GetStreamer();
    const resources::ResourceReference fallbackReference(resources::ResourcePath::FromString(FallbackTexturePath), textures::TextureResourceType);
    const resources::ResourceReference meshReference(resources::ResourcePath::FromString(MeshPath), meshes::MeshResourceType);
    const streaming::DependencyDescriptor failurePipelineDependency{failureFixture.shaderReference, resources::DependencyKind::Required};
    const streaming::DependencyDescriptor meshMaterialDependency{looseFixture.materialReference, resources::DependencyKind::Required};
    Check(streamer.RegisterLoose({fallbackReference, fallbackPath, {}, 0, 0}) && streamer.RegisterLoose({looseFixture.requiredTexture, looseTexturePath, {}, 0, 0}) &&
              streamer.RegisterLoose({looseFixture.materialReference, looseMaterialPath, {looseFixture.materialDependencies.data(), looseFixture.materialDependencyCount}, 0, 0}) &&
              streamer.RegisterLoose({failureFixture.shaderReference, failureShaderPath, {}, 0, 0}) &&
              streamer.RegisterLoose({failureFixture.pipelineReference, failurePipelinePath, {&failurePipelineDependency, 1}, 0, 0}) &&
              streamer.RegisterLoose({failureFixture.materialReference, failureMaterialPath, {failureFixture.materialDependencies.data(), failureFixture.materialDependencyCount}, 0, 0}) &&
              streamer.RegisterLoose({meshReference, meshPath, {&meshMaterialDependency, 1}, 0, 0}),
          "register production loose material closure");
    Check(streamer.MountPackage(packageReader, packagePath, 0), "mount production indexed material VPAK");

    resources::PipelineRequest requiredFailureRequest = streamer.Request(failureFixture.materialReference, resources::LoadPriority::High);
    Check(requiredFailureRequest.TryWait(10'000) && requiredFailureRequest.HasFailed() && requiredFailureRequest.GetError() == resources::Failure::DependencyFailure,
          "fail a production VMAT closure with an unavailable Required dependency");
    requiredFailureRequest.Reset();

    resources::PipelineRequest fallbackRequest = streamer.Request(fallbackReference, resources::LoadPriority::High);
    resources::PipelineRequest looseRequest = streamer.Request(looseFixture.materialReference, resources::LoadPriority::High);
    resources::PipelineRequest packageRequest = streamer.Request(packageFixture.materialReference, resources::LoadPriority::High);
    resources::PipelineRequest meshRequest = streamer.Request(meshReference, resources::LoadPriority::High);
    Check(fallbackRequest.TryWait(10'000) && fallbackRequest.HasLoaded() && looseRequest.TryWait(10'000) && looseRequest.HasLoaded() && packageRequest.TryWait(10'000) && packageRequest.HasLoaded() &&
              meshRequest.TryWait(10'000) && meshRequest.HasLoaded(),
          "load loose VMESH/VMAT and indexed-VPAK VMAT closures through ResourceStreamingService");
    resources::ResourceHandle fallbackHandle = fallbackRequest.Acquire();
    resources::ResourceHandle looseHandle = looseRequest.Acquire();
    resources::ResourceHandle packageHandle = packageRequest.Acquire();
    resources::ResourceHandle meshHandle = meshRequest.Acquire();
    const auto* const looseObject = static_cast<const material::MaterialResourceObject*>(looseHandle.Get());
    const auto* const packageObject = static_cast<const material::MaterialResourceObject*>(packageHandle.Get());
    Check(looseObject != nullptr && looseObject->IsOpen() && packageObject != nullptr && packageObject->IsOpen() && looseObject->GetFile().GetContentFingerprint() == looseFixture.materialContent &&
              packageObject->GetFile().GetContentFingerprint() == packageFixture.materialContent,
          "production loaders preserve exact loose and indexed material identities");

    rendering::MaterialResourceResolverFailure resourceFailure;
    rendering::MaterialResourceFallbackDesc fallbackDesc{shader::MaterialResourceKind::Texture, textures::TextureResourceType, looseFixture.textureType, looseFixture.textureShape, fallbackHandle};
    Check(renderingService->GetMaterialResources().RegisterFallback(fallbackDesc, &resourceFailure), "register exact optional texture fallback");
    fallbackDesc.resource.Reset();

    const resources::ResourceHandle* const looseRequired = FindDependency(*looseObject, looseFixture.requiredTexture);
    const resources::ResourceHandle* const packageRequired = FindDependency(*packageObject, packageFixture.requiredTexture);
    rendering::TextureDemandHandle fallbackTextureDemand;
    rendering::TextureDemandHandle looseTextureDemand;
    rendering::TextureDemandHandle packageTextureDemand;
    rendering::TextureResidencyRuntimeFailure textureFailure;
    Check(looseRequired != nullptr && packageRequired != nullptr && renderingService->GetTextureResidency().RequestTexture(fallbackHandle, fallbackTextureDemand, &textureFailure) &&
              renderingService->GetTextureResidency().RequestTexture(*looseRequired, looseTextureDemand, &textureFailure) &&
              renderingService->GetTextureResidency().RequestTexture(*packageRequired, packageTextureDemand, &textureFailure),
          "retain expected required and fallback texture identities");

    rendering::MaterialResidencyRuntime& residency = renderingService->GetMaterialResidency();
    rendering::MaterialResidencyRuntimeFailure residencyFailure;
    rendering::MaterialDemandHandle firstDemand;
    rendering::MaterialDemandHandle coalescedDemand;
    rendering::MaterialDemandHandle thirdDemand;
    rendering::MaterialDemandHandle exhaustedDemand;
    Check(residency.RequestMaterial(looseHandle, firstDemand, &residencyFailure) && residency.RequestMaterial(looseHandle, coalescedDemand, &residencyFailure) &&
              residency.RequestMaterial(looseHandle, thirdDemand, &residencyFailure) && firstDemand.GetResidency() == coalescedDemand.GetResidency() &&
              firstDemand.GetResidency() == thirdDemand.GetResidency() && !residency.RequestMaterial(looseHandle, exhaustedDemand, &residencyFailure) &&
              residencyFailure.code == rendering::MaterialResidencyRuntimeFailureCode::CapacityExceeded && residency.GetStats().demandsCoalesced != 0,
          "service-level material demand coalescing and bounded backpressure");
    coalescedDemand.Reset();
    thirdDemand.Reset();

    rendering::RenderSceneDesc sceneDesc;
    sceneDesc.name = "material 3C.4 production vertical";
    sceneDesc.maximumProxies = 1;
    sceneDesc.maximumPendingProxyMutations = 8;
    sceneDesc.maximumViews = 1;
    rendering::RenderSceneHandle scene;
    rendering::RenderSceneFailure sceneFailure;
    Check(renderingService->GetScenes().CreateScene(sceneDesc, scene, &sceneFailure), "create production material scene");
    rendering::DecalProxyDesc decalDesc;
    decalDesc.proxy.scene = scene;
    decalDesc.proxy.debugName = "material 3C.4 decal";
    decalDesc.material = looseFixture.materialReference;
    decalDesc.materialHandle = looseHandle;
    rendering::RenderProxyHandle decal;
    Check(renderingService->GetScenes().CreateDecalProxy(decalDesc, decal, &sceneFailure), "create tracked material decal");
    decalDesc.materialHandle.Reset();
    firstDemand.Reset();
    rendering::MaterialSceneBindingFailure bindingFailure;
    Check(renderingService->GetMaterialBindings().SetDecalMaterial(decal, looseHandle, &bindingFailure), "request loose material scene binding");
    rendering::MaterialSceneBindingInfo bindingInfo;
    Check(DriveBinding(*frames, clock, renderingService->GetMaterialBindings(), decal, bindingInfo), "publish loose material through RenderingService");

    rendering::MeshDemandHandle meshDemand;
    rendering::MeshDemandHandle coalescedMeshDemand;
    rendering::MeshResidencyFailure meshFailure;
    rendering::MeshResidencyInfo meshInfo;
    Check(renderingService->GetMeshResidency().RequestMesh(meshHandle, meshDemand, &meshFailure) && renderingService->GetMeshResidency().RequestMesh(meshHandle, coalescedMeshDemand, &meshFailure) &&
              meshDemand.GetResidency() == coalescedMeshDemand.GetResidency(),
          "coalesce exact VMESH generation demand before topology construction");
    bool meshTopologyReady = false;
    for (vanguard::u32 attempt = 0; meshDemand.IsValid() && attempt < 512 && !meshTopologyReady; ++attempt)
    {
        Check(RunFrame(*frames, clock) && renderingService->GetMeshResidency().GetInfo(meshDemand.GetResidency(), meshInfo, &meshFailure), "progress VMESH renderable topology");
        meshTopologyReady = meshInfo.state == rendering::MeshResidencyState::RenderableTopologySubmitted;
        if (meshInfo.state == rendering::MeshResidencyState::Failed)
            break;
        std::this_thread::yield();
    }
    coalescedMeshDemand.Reset();
    Check(meshTopologyReady && RunFrame(*frames, clock) && renderingService->GetMeshResidency().GetInfo(meshDemand.GetResidency(), meshInfo, &meshFailure) && meshInfo.demandCount == 1 &&
              meshInfo.materialCount == 1 && VerifyMeshTopology(*renderingService, meshInfo, bindingInfo.activeMaterial),
          "publish immutable VMESH LOD/primitive/material/phase topology without drawable placement");
    pipeline::AttachmentSignature preparationAttachments;
    preparationAttachments.colorCount = 1;
    preparationAttachments.colors[0] = {pipeline::Format::R8G8B8A8UNorm, shader::NumericClass::FloatingPoint};
    rendering::MeshDrawPreparation preparedDraw;
    auto& meshResidency = renderingService->GetMeshResidency();
    Check(!meshResidency.PrepareDraw(meshDemand, 1, rendering::standardRenderPhases::Opaque, preparationAttachments, preparedDraw, &meshFailure) && !preparedDraw.mesh.IsValid(),
          "reject an absent anchor primitive without publishing preparation");
    rendering::MaterialTechniqueRequest occupiedTechnique;
    rendering::MaterialTechniqueDesc occupiedDesc{bindingInfo.activeResidency, MaterialTechnique, &preparationAttachments};
    occupiedDesc.twoSided = true;
    Check(residency.RequestTechnique(occupiedDesc, occupiedTechnique, &residencyFailure), "occupy one bounded technique slot");
    Check(!meshResidency.PrepareDraw(meshDemand, 0, rendering::standardRenderPhases::Opaque, preparationAttachments, preparedDraw, &meshFailure) &&
              meshFailure.code == rendering::MeshResidencyFailureCode::MaterialFailure && !preparedDraw.mesh.IsValid() && !preparedDraw.normal.IsValid() && !preparedDraw.mirrored.IsValid() &&
              residency.GetStats().liveTechniqueRequests == 1 && meshResidency.GetInfo(meshDemand.GetResidency(), meshInfo, &meshFailure) && meshInfo.demandCount == 1,
          "partial pipeline admission failure releases the temporary mesh demand and first technique");
    occupiedTechnique.Reset();
    Check(meshResidency.PrepareDraw(meshDemand, 0, rendering::standardRenderPhases::Opaque, preparationAttachments, preparedDraw, &meshFailure) && preparedDraw.geometry.IsValid() &&
              preparedDraw.placement.IsValid() && preparedDraw.sourceSubmesh == 0,
          "prepare the exact cooked VMESH primitive and material technique");
    rendering::MaterialTechniqueInfo normalDraw, mirroredDraw;
    Check(DriveTechnique(*frames, clock, residency, preparedDraw.normal, normalDraw) && DriveTechnique(*frames, clock, residency, preparedDraw.mirrored, mirroredDraw) &&
              normalDraw.material == bindingInfo.activeMaterial && mirroredDraw.material == normalDraw.material && normalDraw.pipeline != mirroredDraw.pipeline,
          "normal and mirrored mesh preparation reaches distinct ready native pipelines");
    meshDemand.Reset();
    Check(meshResidency.GetInfo(preparedDraw.mesh.GetResidency(), meshInfo, &meshFailure) && meshInfo.demandCount == 1 && meshInfo.state == rendering::MeshResidencyState::RenderableTopologySubmitted,
          "preparation retains geometry after the original caller releases its mesh demand without publishing drawable residency");
    preparedDraw = {};
    Check(residency.GetStats().liveTechniqueRequests == 0, "releasing mesh preparation releases both technique requests");

    rendering::MaterialTechniqueRequest firstTechnique;
    rendering::MaterialTechniqueRequest coalescedTechnique;
    pipeline::AttachmentSignature attachments;
    attachments.colorCount = 1;
    attachments.colors[0].format = pipeline::Format::R8G8B8A8UNorm;
    attachments.colors[0].numericClass = shader::NumericClass::FloatingPoint;
    rendering::MaterialTechniqueRequest failedTechnique;
    const rendering::MaterialTechniqueDesc missingAttachments{bindingInfo.activeResidency, MaterialTechnique, nullptr, vanguard::pipeline_cache::Priority::Normal};
    rendering::MaterialResidencyInfo activeResidencyInfo;
    Check(!residency.RequestTechnique(missingAttachments, failedTechnique, &residencyFailure) && residencyFailure.code == rendering::MaterialResidencyRuntimeFailureCode::PipelineFailure &&
              !failedTechnique.IsValid() && residency.GetInfo(bindingInfo.activeResidency, activeResidencyInfo, &residencyFailure) && activeResidencyInfo.state == rendering::MaterialResidencyState::Resident,
          "reject a concrete incompatible technique without disturbing resident material state");
    const rendering::MaterialTechniqueDesc techniqueDesc{bindingInfo.activeResidency, MaterialTechnique, &attachments, vanguard::pipeline_cache::Priority::Critical};
    Check(residency.RequestTechnique(techniqueDesc, firstTechnique, &residencyFailure) && residency.RequestTechnique(techniqueDesc, coalescedTechnique, &residencyFailure),
          "request coalesced concrete native material technique");
    rendering::MaterialTechniqueInfo techniqueInfo;
    Check(residency.GetTechniqueInfo(firstTechnique, techniqueInfo, &residencyFailure) && techniqueInfo.state == rendering::MaterialTechniqueState::Pending,
          "observe concrete native technique pending before frame progression");
    Check(DriveTechnique(*frames, clock, residency, firstTechnique, techniqueInfo) && techniqueInfo.material == bindingInfo.activeMaterial && techniqueInfo.pipeline,
          "resolve concrete native technique without a draw");
    Check(VerifyMaterialImage(*renderingService, looseFixture, looseHandle, bindingInfo, looseTextureDemand.GetResidency(), fallbackTextureDemand.GetResidency(), decal),
          "read back exact loose material, resources, parameters, layout, and decal binding");

    Check(renderingService->GetMaterialBindings().SetDecalMaterial(decal, packageHandle, &bindingFailure), "request indexed-VPAK replacement candidate");
    Check(renderingService->GetMaterialBindings().CancelCandidate(decal, &bindingFailure) && renderingService->GetMaterialBindings().GetInfo(decal, bindingInfo, &bindingFailure) &&
              bindingInfo.activeMaterial == techniqueInfo.material && !bindingInfo.candidateResidency.IsValid(),
          "candidate cancellation preserves the last valid loose material");
    rendering::MaterialDemandHandle packagePrewarm;
    Check(residency.RequestMaterial(packageHandle, packagePrewarm, &residencyFailure), "prewarm indexed-VPAK replacement through material residency");
    rendering::MaterialResidencyInfo packageResidencyInfo;
    bool packageResident = false;
    for (vanguard::u32 attempt = 0; attempt < 512 && !packageResident; ++attempt)
    {
        Check(RunFrame(*frames, clock) && residency.GetInfo(packagePrewarm.GetResidency(), packageResidencyInfo, &residencyFailure), "progress indexed-VPAK replacement prewarm");
        packageResident = packageResidencyInfo.state == rendering::MaterialResidencyState::Resident;
    }
    rendering::RenderCommandFailure commandFailure;
    rendering::GpuSceneRuntimeFailure gpuSceneRuntimeFailure;
    Check(renderingService->GetCommands().FlushPreviousFrameProcessing(&commandFailure) && renderingService->GetGpuScene().ResolveContributions(&gpuSceneRuntimeFailure),
          "join the preceding service publication before direct cancellation control");
    Check(packageResident && renderingService->GetMaterialBindings().SetDecalMaterial(decal, packageHandle, &bindingFailure), "stage resident indexed-VPAK replacement candidate");
    packagePrewarm.Reset();
    Check(renderingService->GetMaterialBindings().Update(&bindingFailure) && renderingService->GetMaterialBindings().GetInfo(decal, bindingInfo, &bindingFailure) && bindingInfo.awaitingScenePublication &&
              bindingInfo.activeMaterial == techniqueInfo.material,
          "hold last-valid material while replacement awaits scene publication");
    rendering::RenderSceneSnapshot replacementSnapshot;
    rendering::RenderSceneGpuPublication canceledPublication;
    rendering::RenderSceneGpuFailure gpuSceneFailure;
    const bool capturedReplacementSnapshot = renderingService->GetScenes().GetSnapshot(scene, replacementSnapshot);
    const bool preparedCanceledPublication =
        capturedReplacementSnapshot && renderingService->GetGpuScene().GetScenePublisher().Prepare(scene, replacementSnapshot.completedMutationEpoch, canceledPublication, &gpuSceneFailure);
    const bool canceledReplacementPublication = preparedCanceledPublication && renderingService->GetGpuScene().GetScenePublisher().Cancel(canceledPublication, &gpuSceneFailure);
    const bool updatedCanceledReplacement = canceledReplacementPublication && renderingService->GetMaterialBindings().Update(&bindingFailure);
    const bool queriedCanceledReplacement = updatedCanceledReplacement && renderingService->GetMaterialBindings().GetInfo(decal, bindingInfo, &bindingFailure);
    const bool preservedCanceledReplacement = queriedCanceledReplacement && bindingInfo.awaitingScenePublication && bindingInfo.activeMaterial == techniqueInfo.material;
    if (!preservedCanceledReplacement)
        std::fprintf(stderr, "[materialRuntimeServiceTests] canceled replacement: snapshot=%u prepare=%u cancel=%u update=%u query=%u awaiting=%u active=%u gpuCode=%u bindingCode=%u\n",
                     capturedReplacementSnapshot, preparedCanceledPublication, canceledReplacementPublication, updatedCanceledReplacement, queriedCanceledReplacement, bindingInfo.awaitingScenePublication,
                     bindingInfo.activeMaterial == techniqueInfo.material, static_cast<unsigned>(gpuSceneFailure.code), static_cast<unsigned>(bindingFailure.code));
    Check(preservedCanceledReplacement, "retry a canceled replacement publication without losing the active material");
    Check(DriveBinding(*frames, clock, renderingService->GetMaterialBindings(), decal, bindingInfo), "atomically replace loose material with indexed-VPAK material after publication retry");
    Check(VerifyMaterialImage(*renderingService, packageFixture, packageHandle, bindingInfo, packageTextureDemand.GetResidency(), fallbackTextureDemand.GetResidency(), decal),
          "read back exact indexed-VPAK replacement state");
    Check(renderingService->GetMaterialResources().GetStats().fallbacksSelected >= 2, "select the exact Optional texture fallback for both production materials");

    coalescedTechnique.Reset();
    firstTechnique.Reset();
    techniqueInfo = {};
    Check(renderingService->GetScenes().DestroyProxy(decal, &sceneFailure), "destroy material decal proxy");
    Check(RunFrame(*frames, clock) && renderingService->GetMaterialBindings().GetStats().bindings == 0, "proxy destruction releases material binding interests");
    fallbackTextureDemand.Reset();
    looseTextureDemand.Reset();
    packageTextureDemand.Reset();
    engine::RenderingRetirementFailure retirementFailure;
    Check(!renderingService->SealResidencyRetirements({}, &retirementFailure) && retirementFailure.code == engine::RenderingRetirementFailureCode::MissingCutover,
          "reject incomplete renderer retirement cutover");
    rhi::ResidencyFenceSet retirementFences;
    rhi::Failure rhiFailure;
    Check(SubmitRetirementFences(retirementFences, rhiFailure) && renderingService->SealResidencyRetirements(retirementFences, &retirementFailure) && WaitRetirementFences(retirementFences, rhiFailure) &&
              rhi::RetireResources(&rhiFailure),
          "seal and complete real graphics, compute, and copy retirement fences");
    for (vanguard::u32 attempt = 0; attempt < 64; ++attempt)
    {
        const rendering::MaterialResidencyRuntimeStats materialStats = renderingService->GetMaterialResidency().GetStats();
        const rendering::MaterialMaterializerStats materializerStats = renderingService->GetMaterialMaterializer().GetStats();
        const rendering::MaterialResourceResolverStats resourceStats = renderingService->GetMaterialResources().GetStats();
        const rendering::TextureResidencyRuntimeStats textureStats = renderingService->GetTextureResidency().GetStats();
        const rendering::MeshResidencyStats meshStats = renderingService->GetMeshResidency().GetStats();
        if (meshStats.residencyRecords == 0 && meshStats.liveDemands == 0 && materialStats.residencyRecords == 0 && materialStats.liveDemands == 0 && materialStats.liveTechniqueRequests == 0 &&
            materialStats.referencedNativePrograms == 0 && materializerStats.activeOperations == 0 && materializerStats.liveReferences == 0 && resourceStats.activeOperations == 0 &&
            resourceStats.liveReferences == 0 && resourceStats.cachedDescriptors == 0 && textureStats.residencyRecords == 0 && textureStats.liveDemands == 0)
            break;
        Check(RunFrame(*frames, clock), "collect material retirement frame");
    }
    rhi::ResidencyFenceSet dependencyRetirementFences;
    Check(SubmitRetirementFences(dependencyRetirementFences, rhiFailure) && renderingService->SealResidencyRetirements(dependencyRetirementFences, &retirementFailure) &&
              WaitRetirementFences(dependencyRetirementFences, rhiFailure) && rhi::RetireResources(&rhiFailure),
          "seal downstream texture retirement at the next real renderer cutover");
    for (vanguard::u32 attempt = 0; attempt < 64 && renderingService->GetTextureResidency().GetStats().residencyRecords != 0; ++attempt)
        Check(RunFrame(*frames, clock), "collect downstream texture retirement frame");
    Check(renderingService->GetScenes().DestroyScene(scene, &sceneFailure), "destroy material test scene");
    Check(residency.ClearNativeProgramCache(&residencyFailure) && renderingService->GetMaterialResources().ClearFallbacks(&resourceFailure),
          "release terminal renderer caches before dependent resource-service quiesce");

    looseHandle.Reset();
    packageHandle.Reset();
    fallbackHandle.Reset();
    meshHandle.Reset();
    looseRequest.Reset();
    packageRequest.Reset();
    fallbackRequest.Reset();
    meshRequest.Reset();
    Check(WaitForResourceDrain(resourcesService->GetPipeline(), streamer), "drain production material resource requests");
    resources::ResourceRegistry& resourceRegistry = resourcesService->GetRegistry();
    const resources::ResourcePath evictionPaths[]{meshReference.GetPath(),
                                                  looseFixture.materialReference.GetPath(),
                                                  packageFixture.materialReference.GetPath(),
                                                  looseFixture.pipelineReference.GetPath(),
                                                  packageFixture.pipelineReference.GetPath(),
                                                  looseFixture.shaderReference.GetPath(),
                                                  packageFixture.shaderReference.GetPath(),
                                                  looseFixture.requiredTexture.GetPath(),
                                                  packageFixture.requiredTexture.GetPath(),
                                                  fallbackReference.GetPath(),
                                                  failureFixture.pipelineReference.GetPath(),
                                                  failureFixture.shaderReference.GetPath(),
                                                  copyFixture.pipelineReference.GetPath(),
                                                  copyFixture.shaderReference.GetPath()};
    bool evictedClosures = true;
    for (const resources::ResourcePath path : evictionPaths)
    {
        const resources::State state = resourceRegistry.GetState(path);
        if (state == resources::State::Loaded)
            evictedClosures = resourceRegistry.Evict(path) && evictedClosures;
        else if (state != resources::State::Unloaded && state != resources::State::Evicting)
        {
            std::fprintf(stderr, "[materialRuntimeServiceTests] unexpected pre-eviction resource state: path=%llu state=%u\n", static_cast<unsigned long long>(path.Id()), static_cast<unsigned>(state));
            evictedClosures = false;
        }
    }
    Check(evictedClosures, "evict loaded material closures after all runtime interests retire");
    Check(streamer.UnmountPackage(packageReader), "unmount indexed material VPAK");
    Check(streamer.UnregisterLoose(copyFixture.pipelineReference.GetPath()) && streamer.UnregisterLoose(copyFixture.shaderReference.GetPath()), "unregister cooked copy startup catalog");
    Check(streamer.UnregisterLoose(meshReference.GetPath()) && streamer.UnregisterLoose(looseFixture.materialReference.GetPath()) && streamer.UnregisterLoose(looseFixture.pipelineReference.GetPath()) &&
              streamer.UnregisterLoose(looseFixture.shaderReference.GetPath()) && streamer.UnregisterLoose(looseFixture.requiredTexture.GetPath()) && streamer.UnregisterLoose(fallbackReference.GetPath()),
          "unregister loose material closure in reverse dependency order");
    Check(streamer.UnregisterLoose(failureFixture.materialReference.GetPath()) && streamer.UnregisterLoose(failureFixture.pipelineReference.GetPath()) &&
              streamer.UnregisterLoose(failureFixture.shaderReference.GetPath()),
          "unregister failed Required-dependency material closure");
    const rendering::MaterialResidencyRuntimeStats finalMaterialStats = renderingService->GetMaterialResidency().GetStats();
    const rendering::MaterialMaterializerStats finalMaterializerStats = renderingService->GetMaterialMaterializer().GetStats();
    const rendering::MaterialResourceResolverStats finalResourceStats = renderingService->GetMaterialResources().GetStats();
    const rendering::TextureResidencyRuntimeStats finalTextureStats = renderingService->GetTextureResidency().GetStats();
    const rendering::MeshResidencyStats finalMeshStats = renderingService->GetMeshResidency().GetStats();
    const bool zeroState = renderingService->GetMaterialBindings().GetStats().bindings == 0 && finalMeshStats.residencyRecords == 0 && finalMeshStats.liveDemands == 0 &&
                           finalMaterialStats.residencyRecords == 0 && finalMaterialStats.liveDemands == 0 && finalMaterialStats.liveTechniqueRequests == 0 && finalMaterialStats.cachedNativePrograms == 0 &&
                           finalMaterialStats.referencedNativePrograms == 0 && finalMaterializerStats.activeOperations == 0 && finalMaterializerStats.liveReferences == 0 &&
                           finalResourceStats.activeOperations == 0 && finalResourceStats.liveReferences == 0 && finalResourceStats.cachedDescriptors == 0 && finalResourceStats.registeredFallbacks == 0 &&
                           finalTextureStats.residencyRecords == 0 && finalTextureStats.liveDemands == 0;
    if (!zeroState)
        std::fprintf(stderr,
                     "[materialRuntimeServiceTests] final state: bindings=%u residencies=%u demands=%u techniques=%u programs=%u operations=%u materialRefs=%u resourceOps=%u resourceRefs=%u descriptors=%u "
                     "textures=%u textureDemands=%u meshes=%u meshDemands=%u\n",
                     renderingService->GetMaterialBindings().GetStats().bindings, finalMaterialStats.residencyRecords, finalMaterialStats.liveDemands, finalMaterialStats.liveTechniqueRequests,
                     finalMaterialStats.referencedNativePrograms, finalMaterializerStats.activeOperations, finalMaterializerStats.liveReferences, finalResourceStats.activeOperations,
                     finalResourceStats.liveReferences, finalResourceStats.cachedDescriptors, finalTextureStats.residencyRecords, finalTextureStats.liveDemands, finalMeshStats.residencyRecords,
                     finalMeshStats.liveDemands);
    Check(zeroState, "material runtime reaches zero owned state before shutdown");
    packageReader.Close();
    DeleteDirectoryTree(files, directory);
    Check(files.DeleteFile(catalogMount.directory.AddFilePath("startup.vshader")) && files.DeleteFile(catalogMount.directory.AddFilePath("startup.vppl")), "remove startup catalog fixture files");
    Check(files.DeleteFile(catalogMount.directory.AddFilePath("copy.vshader")) && files.DeleteFile(catalogMount.directory.AddFilePath("copy.vpipeline")), "remove copy startup fixture files");
    const bool shutdown = host.Shutdown(&hostFailure);
    if (!shutdown)
        std::fprintf(stderr, "[materialRuntimeServiceTests] shutdown failed: code=%u service=%llu message=%s\n", static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                     hostFailure.message != nullptr ? hostFailure.message : "unknown");
    Check(shutdown, "clean material runtime service shutdown");

    vanguard::diagnostics::Shutdown();
    if (g_failures == 0)
        std::puts("[materialRuntimeServiceTests] production loose/VPAK material runtime closure passed");
    return g_failures == 0 ? 0 : 1;
}
