#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/packages/packages.hpp>

#include <array>
#include <cstdio>
#include <utility>

namespace
{
    namespace materials = vanguard::materials;
    namespace shaders = vanguard::shaders;
    namespace resources = vanguard::resources;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[materialsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    bool Equal(const ByteArray& left, const ByteArray& right) noexcept
    {
        if (left.Size() != right.Size()) return false;
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
            if (left[index] != right[index]) return false;
        return true;
    }

    struct ShaderFixture final
    {
        std::array<vanguard::u8, 8> vertexBytes{{1, 2, 3, 4, 5, 6, 7, 8}};
        std::array<vanguard::u8, 8> fragmentBytes{{8, 7, 6, 5, 4, 3, 2, 1}};
        std::array<shaders::StageBuildRecord, 2> stages;
        std::array<shaders::DescriptorBinding, 3> bindings;
        std::array<shaders::ConstantBuffer, 1> buffers;
        std::array<shaders::ConstantMember, 2> members;
        std::array<shaders::VertexInput, 1> inputs;
        std::array<shaders::FragmentOutput, 1> outputs;
        shaders::BuildDescription description;

        ShaderFixture() noexcept
        {
            stages = {{{shaders::ShaderStage::Vertex, shaders::NativeFormat::Dxil, 0x1001, vertexBytes.data(), vertexBytes.size()},
                       {shaders::ShaderStage::Fragment, shaders::NativeFormat::Dxil, 0x1002, fragmentBytes.data(), fragmentBytes.size()}}};
            bindings = {{{0x1000, 2, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read,
                          shaders::StageBit(shaders::ShaderStage::Fragment)},
                         {0x2000, 2, 1, 2, shaders::BindingKind::SampledTexture, shaders::BindingAccess::Read,
                          shaders::StageBit(shaders::ShaderStage::Fragment)},
                         {0x3000, 0, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read,
                          shaders::StageBit(shaders::ShaderStage::Vertex)}}};
            buffers = {{{0x1000, 2, 0, 32, 0, 2}}};
            members = {{{0x1100, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false},
                        {0x1101, 16, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
            inputs = {{{0x4000, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            outputs = {{{0x5000, 0, 0, shaders::NumericClass::FloatingPoint, 0x0f}}};
            description.kind = shaders::ProgramKind::Graphics;
            description.program = 0xabcdu;
            description.permutation = vanguard::crypto::Sha256("material permutation", 20);
            description.compilerFingerprint = vanguard::crypto::Sha256("test compiler", 13);
            description.pipelineInterface.stages =
                shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment);
            description.pipelineInterface.primitiveClass = shaders::PrimitiveClass::Triangle;
            description.pipelineInterface.renderTargetCount = 1;
            description.stages = {stages.data(), static_cast<vanguard::u32>(stages.size())};
            description.bindings = {bindings.data(), static_cast<vanguard::u32>(bindings.size())};
            description.constantBuffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
            description.constantMembers = {members.data(), static_cast<vanguard::u32>(members.size())};
            description.vertexInputs = {inputs.data(), static_cast<vanguard::u32>(inputs.size())};
            description.fragmentOutputs = {outputs.data(), static_cast<vanguard::u32>(outputs.size())};
        }
    };
}

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;
    namespace packages = vanguard::packages;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "materialsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    ShaderFixture shaderFixture;
    ByteArray shaderBytes(memory::pools::Rendering::GetInstance());
    filesystem::MemoryFileWriter shaderWriter(shaderBytes);
    Check(shaders::WriteShader(shaderWriter, shaderFixture.description) == shaders::Result::Success, "write shader reflection fixture");
    filesystem::MemoryFileReader shaderReader(shaderBytes, 0);
    shaders::ShaderFile shader;
    Check(shader.Open(shaderReader) == shaders::Result::Success, "open shader reflection fixture");

    const resources::ResourceReference shaderReference(resources::ResourcePath::FromString("shaders/standard.vshader"),
                                                        shaders::ShaderResourceType);
    const vanguard::pipelines::ShaderReference pipelineShader{
        shaderReference.Path().Id(), shader.Permutation(), shader.BindingLayoutFingerprint(),
        shader.PipelineInterfaceFingerprint()};
    vanguard::pipelines::BuildDescription pipelineDescription;
    pipelineDescription.kind = vanguard::pipelines::PipelineKind::Graphics;
    pipelineDescription.name = 0x8800;
    pipelineDescription.shaders = {&pipelineShader, 1};
    pipelineDescription.graphics.attachmentPolicy = vanguard::pipelines::AttachmentPolicy::Deferred;
    ByteArray pipelineBytes(memory::pools::Rendering::GetInstance());
    filesystem::MemoryFileWriter pipelineWriter(pipelineBytes);
    Check(vanguard::pipelines::WritePipeline(pipelineWriter, pipelineDescription) == vanguard::pipelines::Result::Success,
          "write compatible pipeline reflection fixture");
    filesystem::MemoryFileReader pipelineReader(pipelineBytes, 0);
    vanguard::pipelines::PipelineFile pipeline;
    Check(pipeline.Open(pipelineReader) == vanguard::pipelines::Result::Success, "open compatible pipeline fixture");
    const std::array<materials::TechniqueBuildRecord, 2> techniques{{
        {0x9001, resources::ResourceReference(resources::ResourcePath::FromString("pipelines/shadow.vpipeline"),
                                               vanguard::pipelines::PipelineResourceType), &pipeline},
        {0x9000, resources::ResourceReference(resources::ResourcePath::FromString("pipelines/gbuffer.vpipeline"),
                                               vanguard::pipelines::PipelineResourceType), &pipeline}}};
    const std::array<vanguard::u64, 1> selectedBuffers{{0x1000}};
    const std::array<vanguard::u64, 1> selectedBindings{{0x2000}};
    const std::array<float, 4> baseColor{{0.25f, 0.5f, 0.75f, 1.0f}};
    const std::array<float, 4> surface{{0.8f, 0.2f, 0.0f, 0.0f}};
    const std::array<materials::ConstantValueBuildRecord, 2> constants{{
        {0x1101, surface.data(), static_cast<vanguard::u32>(sizeof(surface))},
        {0x1100, baseColor.data(), static_cast<vanguard::u32>(sizeof(baseColor))}}};
    const resources::ResourceReference albedo(resources::ResourcePath::FromString("textures/stone_albedo.vtex"),
                                               vanguard::serialization::MakeFourCC('V', 'T', 'E', 'X'));
    const std::array<materials::ResourceValueBuildRecord, 1> resourceValues{{
        {0x2000, 0, albedo, resources::DependencyKind::Required}}};

    materials::BuildDescription description;
    description.name = 0x7777;
    description.shader = shaderReference;
    description.shaderReflection = &shader;
    description.materialConstantBuffers = {selectedBuffers.data(), static_cast<vanguard::u32>(selectedBuffers.size())};
    description.materialResourceBindings = {selectedBindings.data(), static_cast<vanguard::u32>(selectedBindings.size())};
    description.techniques = {techniques.data(), static_cast<vanguard::u32>(techniques.size())};
    description.constants = {constants.data(), static_cast<vanguard::u32>(constants.size())};
    description.resources = {resourceValues.data(), static_cast<vanguard::u32>(resourceValues.size())};

    ByteArray first(memory::pools::Rendering::GetInstance());
    filesystem::MemoryFileWriter firstWriter(first);
    Check(materials::WriteMaterial(firstWriter, description) == materials::Result::Success, "write shader-derived vmat");

    auto reorderedTechniques = techniques;
    auto reorderedConstants = constants;
    std::swap(reorderedTechniques[0], reorderedTechniques[1]);
    std::swap(reorderedConstants[0], reorderedConstants[1]);
    materials::BuildDescription reordered = description;
    reordered.techniques = {reorderedTechniques.data(), static_cast<vanguard::u32>(reorderedTechniques.size())};
    reordered.constants = {reorderedConstants.data(), static_cast<vanguard::u32>(reorderedConstants.size())};
    ByteArray second(memory::pools::Rendering::GetInstance());
    filesystem::MemoryFileWriter secondWriter(second);
    Check(materials::WriteMaterial(secondWriter, reordered) == materials::Result::Success && Equal(first, second),
          "vmat bytes are deterministic across input ordering");

    filesystem::MemoryFileReader materialReader(first, 0);
    materials::MaterialFile material;
    Check(material.Open(materialReader) == materials::Result::Success && material.IsOpen(), "open vmat");
    Check(material.BindingLayoutFingerprint() == shader.BindingLayoutFingerprint(),
          "vmat stores the exact shader binding-layout fingerprint");
    Check(material.Techniques().Size() == 2 && material.Techniques()[0].name == 0x9000 &&
          material.Techniques()[1].name == 0x9001, "techniques are canonical and pipeline-addressable");
    Check(material.ConstantBuffers().Size() == 1 && material.Parameters().Size() == 2 &&
          material.ConstantBufferData(material.ConstantBuffers()[0]).Size() == 32,
          "material constant data retains the reflected GPU buffer layout");
    const auto parameterBytes = material.ParameterData();
    Check(parameterBytes.Size() == 32 && parameterBytes[0] == reinterpret_cast<const vanguard::u8*>(baseColor.data())[0] &&
          parameterBytes[16] == reinterpret_cast<const vanguard::u8*>(surface.data())[0],
          "constant overrides are written directly at reflected byte offsets");
    Check(material.ResourceBindings().Size() == 2 && material.ResourceBindings()[0].resource == albedo &&
          !material.ResourceBindings()[1].resource.IsValid() && material.Dependencies().Size() == 4,
          "descriptor arrays retain explicit bound/unbound slots and deduplicated dependencies");

    {
        const std::array<vanguard::u64, 1> unknownBuffer{{0xdead}};
        materials::BuildDescription invalid = description;
        invalid.materialConstantBuffers = {unknownBuffer.data(), static_cast<vanguard::u32>(unknownBuffer.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::UnknownShaderInterface,
              "unknown shader-owned constant buffer is rejected explicitly");
    }
    {
        std::array<float, 3> wrongSize{{1.0f, 2.0f, 3.0f}};
        const std::array<materials::ConstantValueBuildRecord, 1> wrongConstants{{
            {0x1100, wrongSize.data(), static_cast<vanguard::u32>(sizeof(wrongSize))}}};
        materials::BuildDescription invalid = description;
        invalid.constants = {wrongConstants.data(), static_cast<vanguard::u32>(wrongConstants.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::TypeMismatch,
              "constant values must exactly match reflected storage size");
    }
    {
        vanguard::pipelines::ShaderReference staleShader = pipelineShader;
        staleShader.bindingLayout.bytes[0] ^= 1u;
        vanguard::pipelines::BuildDescription stalePipelineDescription = pipelineDescription;
        stalePipelineDescription.shaders = {&staleShader, 1};
        ByteArray stalePipelineBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter stalePipelineWriter(stalePipelineBytes);
        Check(vanguard::pipelines::WritePipeline(stalePipelineWriter, stalePipelineDescription) ==
                  vanguard::pipelines::Result::Success,
              "write deliberately stale pipeline fixture");
        filesystem::MemoryFileReader stalePipelineReader(stalePipelineBytes, 0);
        vanguard::pipelines::PipelineFile stalePipeline;
        Check(stalePipeline.Open(stalePipelineReader) == vanguard::pipelines::Result::Success,
              "open deliberately stale pipeline fixture");
        const std::array<materials::TechniqueBuildRecord, 1> staleTechnique{{
            {0x9000, techniques[1].pipeline, &stalePipeline}}};
        materials::BuildDescription invalid = description;
        invalid.techniques = {staleTechnique.data(), static_cast<vanguard::u32>(staleTechnique.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::TypeMismatch,
              "pipeline with a stale shader binding layout is rejected during material cooking");
    }
    {
        ByteArray corrupt(first);
        corrupt[64] ^= 0x5au;
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        materials::MaterialFile rejected;
        Check(rejected.Open(corruptReader) == materials::Result::IntegrityFailure,
              "vmat section corruption is rejected before publication");
    }

    std::array<packages::Dependency, 8> packageDependencies{};
    vanguard::u32 packageDependencyCount = 0;
    for (const materials::ResourceDependency& dependency : material.Dependencies())
        packageDependencies[packageDependencyCount++] =
            {dependency.resource.Path().Id(), dependency.resource.ExpectedType(), dependency.kind};
    const packages::BuildSegment materialSegment{first.TypedData(), first.Size(), packages::Codec::Lz4, 4,
                                                  packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
    packages::BuildResource packagedMaterial;
    packagedMaterial.path = "materials/stone.vmat";
    packagedMaterial.type = materials::MaterialResourceType;
    packagedMaterial.flags = packages::ResourceFlags::Streamable;
    packagedMaterial.segments = {&materialSegment, 1};
    packagedMaterial.dependencies = {packageDependencies.data(), packageDependencyCount};
    ByteArray packageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packageFile(packageBytes);
    packages::PackageWriter packageWriter;
    Check(packageWriter.Begin(packageFile) == packages::Result::Success &&
          packageWriter.Add(packagedMaterial) == packages::Result::Success &&
          packageWriter.Finalize() == packages::Result::Success, "package vmat as an opaque VPAK resource");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == packages::Result::Success, "open VPAK containing vmat");
    const packages::Resource* const packagedRecord = packageReader.Find("materials/stone.vmat");
    Check(packagedRecord != nullptr && packagedRecord->type == materials::MaterialResourceType &&
          packageReader.Dependencies(*packagedRecord).Size() == material.Dependencies().Size(),
          "VPAK preserves shader, pipeline, and texture dependencies without interpreting vmat");
    if (packagedRecord != nullptr)
    {
        packages::ResourceFileReader packagedView;
        Check(packagedView.Open(packageReader, *packagedRecord, packageReaderFile) == packages::Result::Success,
              "open logical vmat directly over VPAK");
        materials::MaterialFile packaged;
        Check(packaged.Open(packagedView) == materials::Result::Success &&
              packaged.ContentFingerprint() == material.ContentFingerprint(),
              "package-backed vmat opens without format translation");
    }

    material.Close();
    pipeline.Close();
    shader.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    if (g_failures == 0) std::puts("[materialsTests] Vanguard vmat conformance checks passed");
    return g_failures == 0 ? 0 : 1;
}
