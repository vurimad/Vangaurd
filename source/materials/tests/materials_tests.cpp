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

    constexpr resources::ResourceTypeId TextureAssetType = vanguard::serialization::MakeFourCC('V', 'T', 'E', 'X');
    constexpr resources::ResourceTypeId WrongAssetType = vanguard::serialization::MakeFourCC('V', 'M', 'S', 'H');

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
        if (left.Size() != right.Size())
            return false;
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
            if (left[index] != right[index])
                return false;
        return true;
    }

    [[nodiscard]] vanguard::u32 LoadU32(const vanguard::u8* const bytes) noexcept
    {
        return static_cast<vanguard::u32>(bytes[0]) | (static_cast<vanguard::u32>(bytes[1]) << 8u) |
               (static_cast<vanguard::u32>(bytes[2]) << 16u) | (static_cast<vanguard::u32>(bytes[3]) << 24u);
    }

    [[nodiscard]] vanguard::u64 LoadU64(const vanguard::u8* const bytes) noexcept
    {
        return static_cast<vanguard::u64>(LoadU32(bytes)) | (static_cast<vanguard::u64>(LoadU32(bytes + 4)) << 32u);
    }

    void StoreU16(vanguard::u8* const bytes, const vanguard::u16 value) noexcept
    {
        bytes[0] = static_cast<vanguard::u8>(value);
        bytes[1] = static_cast<vanguard::u8>(value >> 8u);
    }

    void StoreU32(vanguard::u8* const bytes, const vanguard::u32 value) noexcept
    {
        for (vanguard::u32 index = 0; index < 4; ++index)
            bytes[index] = static_cast<vanguard::u8>(value >> (index * 8u));
    }

    void StoreU64(vanguard::u8* const bytes, const vanguard::u64 value) noexcept
    {
        for (vanguard::u32 index = 0; index < 8; ++index)
            bytes[index] = static_cast<vanguard::u8>(value >> (index * 8u));
    }

    [[nodiscard]] bool RewriteFirstResourceExpectedType(ByteArray& bytes, const resources::ResourceTypeId replacement) noexcept
    {
        constexpr vanguard::u64 HeaderBytes = 40;
        constexpr vanguard::u64 SectionDescriptorBytes = 48;
        constexpr vanguard::u64 MetadataPrefixBytes = 4 + 32;
        constexpr vanguard::u64 BodyHeaderBytes = 8 + 16 + 32 + 32 + 5 * 4;
        constexpr vanguard::u64 TechniqueBytes = 8 + 16;
        constexpr vanguard::u64 ConstantBufferBytes = 8 + 4 * 4;
        constexpr vanguard::u64 ParameterBytes = 8 + 5 * 4 + 4;
        constexpr vanguard::u64 ResourceExpectedTypeOffset = 8 + 4 + 4 + 1 + 1 + 2;
        constexpr vanguard::u64 ResourceRecordBytes = ResourceExpectedTypeOffset + 4 + 16;
        if (bytes.Size() < HeaderBytes)
            return false;
        vanguard::u8* const data = bytes.TypedData();
        const vanguard::u64 sectionTableOffset = LoadU64(data + 24);
        if (sectionTableOffset > bytes.Size() || SectionDescriptorBytes > bytes.Size() - sectionTableOffset)
            return false;
        const vanguard::u64 sectionOffset = LoadU64(data + sectionTableOffset + 16);
        const vanguard::u64 sectionSize = LoadU64(data + sectionTableOffset + 24);
        if (sectionOffset > bytes.Size() || sectionSize > bytes.Size() - sectionOffset || sectionSize < MetadataPrefixBytes + BodyHeaderBytes)
            return false;
        const vanguard::u64 bodyOffset = sectionOffset + MetadataPrefixBytes;
        const vanguard::u32 techniqueCount = LoadU32(data + bodyOffset + 88);
        const vanguard::u32 bufferCount = LoadU32(data + bodyOffset + 92);
        const vanguard::u32 parameterCount = LoadU32(data + bodyOffset + 96);
        const vanguard::u32 resourceCount = LoadU32(data + bodyOffset + 100);
        const vanguard::u64 resourceOffset = bodyOffset + BodyHeaderBytes + static_cast<vanguard::u64>(techniqueCount) * TechniqueBytes +
                                             static_cast<vanguard::u64>(bufferCount) * ConstantBufferBytes +
                                             static_cast<vanguard::u64>(parameterCount) * ParameterBytes;
        if (resourceCount == 0 || resourceOffset > sectionOffset + sectionSize ||
            ResourceRecordBytes > sectionOffset + sectionSize - resourceOffset)
            return false;
        StoreU32(data + resourceOffset + ResourceExpectedTypeOffset, replacement);
        const vanguard::crypto::Digest256 fingerprint =
            vanguard::crypto::Sha256(data + bodyOffset, static_cast<vanguard::usize>(sectionSize - MetadataPrefixBytes));
        for (vanguard::u32 index = 0; index < sizeof(fingerprint.bytes); ++index)
            data[sectionOffset + 4 + index] = fingerprint.bytes[index];
        StoreU64(data + sectionTableOffset + 40,
                 vanguard::serialization::Crc64(data + sectionOffset, static_cast<vanguard::usize>(sectionSize)));
        return true;
    }

    [[nodiscard]] bool RewriteDocumentMinorVersion(ByteArray& bytes, const vanguard::u16 minor) noexcept
    {
        if (bytes.Size() < 40)
            return false;
        StoreU16(bytes.TypedData() + 10, minor);
        StoreU32(bytes.TypedData() + 36, vanguard::serialization::Crc32(bytes.TypedData(), 36));
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
        std::array<shaders::MaterialResourceRole, 2> materialResources;
        std::array<shaders::VertexInput, 1> inputs;
        std::array<shaders::FragmentOutput, 1> outputs;
        shaders::MaterialContractBuildDescription materialContract;
        shaders::BuildDescription description;

        ShaderFixture() noexcept
        {
            stages = {{{shaders::ShaderStage::Vertex, shaders::NativeFormat::Dxil, 0x1001, vertexBytes.data(), vertexBytes.size(), "mainVS"},
                       {shaders::ShaderStage::Fragment, shaders::NativeFormat::Dxil, 0x1002, fragmentBytes.data(), fragmentBytes.size(), "mainPS"}}};
            bindings = {
                {{0x1000, 2, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read, shaders::StageBit(shaders::ShaderStage::Fragment)},
                 {0x2000, 2, 1, 2, shaders::BindingKind::SampledTexture, shaders::BindingAccess::Read, shaders::StageBit(shaders::ShaderStage::Fragment)},
                 {0x3000, 0, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read, shaders::StageBit(shaders::ShaderStage::Vertex)}}};
            buffers = {{{0x1000, 2, 0, 32, 0, 2}}};
            members = {{{0x1100, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}, {0x1101, 16, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
            materialResources = {
                {{0x2000, 0, 0, shaders::MaterialResourceKind::Texture, shaders::MaterialResourceFlags::Required, 0,
                  vanguard::crypto::Sha256("Texture2D<float4>", 17),
                  {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::D2, shaders::MaterialBufferKind::None,
                   shaders::MaterialSamplerKind::None, shaders::ScalarType::F32, 4, shaders::MaterialResourceShapeFlags::None, 0, 0}},
                 {0x2000, 1, 1, shaders::MaterialResourceKind::Texture, shaders::MaterialResourceFlags::None, 0,
                  vanguard::crypto::Sha256("Texture2D<float4>", 17),
                  {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::D2, shaders::MaterialBufferKind::None,
                   shaders::MaterialSamplerKind::None, shaders::ScalarType::F32, 4, shaders::MaterialResourceShapeFlags::None, 0, 0}}}};
            inputs = {{{0x4000, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            outputs = {{{0x5000, 0, 0, shaders::NumericClass::FloatingPoint, 0x0f}}};
            materialContract.domain.name = 0x1000;
            materialContract.domain.schemaVersion = 1;
            materialContract.domain.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
            materialContract.domain.inputType = vanguard::crypto::Sha256("SurfaceInput", 12);
            materialContract.domain.outputType = vanguard::crypto::Sha256("SurfaceOutput", 13);
            materialContract.accessorAbiVersion = 1;
            materialContract.parameterByteSize = 32;
            materialContract.parameters = {members.data(), static_cast<vanguard::u32>(members.size())};
            materialContract.resources = {materialResources.data(), static_cast<vanguard::u32>(materialResources.size())};
            description.kind = shaders::ProgramKind::Graphics;
            description.program = 0xabcdu;
            description.permutation = vanguard::crypto::Sha256("material permutation", 20);
            description.compilerFingerprint = vanguard::crypto::Sha256("test compiler", 13);
            description.pipelineInterface.stages = shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment);
            description.pipelineInterface.primitiveClass = shaders::PrimitiveClass::Triangle;
            description.pipelineInterface.renderTargetCount = 1;
            description.stages = {stages.data(), static_cast<vanguard::u32>(stages.size())};
            description.bindings = {bindings.data(), static_cast<vanguard::u32>(bindings.size())};
            description.constantBuffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
            description.constantMembers = {members.data(), static_cast<vanguard::u32>(members.size())};
            description.vertexInputs = {inputs.data(), static_cast<vanguard::u32>(inputs.size())};
            description.fragmentOutputs = {outputs.data(), static_cast<vanguard::u32>(outputs.size())};
            description.materialContract = &materialContract;
        }
    };
} // namespace

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

    const resources::ResourceReference shaderReference(resources::ResourcePath::FromString("shaders/standard.vshader"), shaders::ShaderResourceType);
    vanguard::pipelines::ShaderReference pipelineShader{shaderReference.GetPath().Id(), shader.GetPermutation(), shader.BindingLayoutFingerprint(),
                                                        shader.GetPipelineInterfaceFingerprint()};
    pipelineShader.materialDomain = shader.GetMaterialContract()->domainFingerprint;
    pipelineShader.materialLayout = shader.GetMaterialContract()->layoutFingerprint;
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
    const std::array<materials::TechniqueBuildRecord, 2> techniques{
        {{0x9001, resources::ResourceReference(resources::ResourcePath::FromString("pipelines/shadow.vpipeline"), vanguard::pipelines::PipelineResourceType),
          &pipeline},
         {0x9000, resources::ResourceReference(resources::ResourcePath::FromString("pipelines/gbuffer.vpipeline"), vanguard::pipelines::PipelineResourceType),
          &pipeline}}};
    const std::array<float, 4> baseColor{{0.25f, 0.5f, 0.75f, 1.0f}};
    const std::array<float, 4> surface{{0.8f, 0.2f, 0.0f, 0.0f}};
    const std::array<materials::ConstantValueBuildRecord, 2> constants{
        {{0x1101, surface.data(), static_cast<vanguard::u32>(sizeof(surface))}, {0x1100, baseColor.data(), static_cast<vanguard::u32>(sizeof(baseColor))}}};
    const resources::ResourceReference albedo(resources::ResourcePath::FromString("textures/stone_albedo.vtex"), TextureAssetType);
    const std::array<materials::ResourceValueBuildRecord, 1> resourceValues{{{0x2000, 0, albedo, resources::DependencyKind::Required}}};
    const std::array<materials::ResourceTypeCompatibility, 1> resourceTypeCompatibility{
        {{materials::ResourceParameterKind::Texture, TextureAssetType}}};

    materials::BuildDescription description;
    description.name = 0x7777;
    description.shader = shaderReference;
    description.shaderReflection = &shader;
    description.techniques = {techniques.data(), static_cast<vanguard::u32>(techniques.size())};
    description.constants = {constants.data(), static_cast<vanguard::u32>(constants.size())};
    description.resources = {resourceValues.data(), static_cast<vanguard::u32>(resourceValues.size())};
    description.resourceTypeCompatibility = {resourceTypeCompatibility.data(),
                                             static_cast<vanguard::u32>(resourceTypeCompatibility.size())};

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
    Check(material.GetTechniques().Size() == 2 && material.GetTechniques()[0].name == 0x9000 && material.GetTechniques()[1].name == 0x9001,
          "techniques are canonical and pipeline-addressable");
    Check(material.GetConstantBuffers().Size() == 1 && material.GetParameters().Size() == 2 &&
              material.GetConstantBufferData(material.GetConstantBuffers()[0]).Size() == 32,
          "material constant data retains the reflected GPU buffer layout");
    const auto parameterBytes = material.GetParameterData();
    Check(parameterBytes.Size() == 32 && parameterBytes[0] == reinterpret_cast<const vanguard::u8*>(baseColor.data())[0] &&
              parameterBytes[16] == reinterpret_cast<const vanguard::u8*>(surface.data())[0],
          "constant overrides are written directly at reflected byte offsets");
    Check(material.GetResourceParameters().Size() == 2 && material.GetResourceParameters()[0].resource == albedo &&
              material.GetResourceParameters()[0].expectedAssetType == TextureAssetType &&
              material.GetResourceParameters()[1].expectedAssetType == TextureAssetType &&
              !material.GetResourceParameters()[1].resource.IsValid() && material.GetDependencies().Size() == 4,
          "resource arrays retain concrete expected asset types, logical bound/unbound values, and deduplicated dependencies");

    {
        const resources::ResourceReference wrong(resources::ResourcePath::FromString("meshes/not_a_texture.vmesh"), WrongAssetType);
        const std::array<materials::ResourceValueBuildRecord, 1> wrongValues{{{0x2000, 0, wrong, resources::DependencyKind::Required}}};
        materials::BuildDescription invalid = description;
        invalid.resources = {wrongValues.data(), static_cast<vanguard::u32>(wrongValues.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::TypeMismatch,
              "writer rejects a typed asset whose concrete type does not match the sealed resource policy");
    }
    {
        const std::array<materials::ResourceTypeCompatibility, 1> invalidPolicy{
            {{materials::ResourceParameterKind::Texture, resources::InvalidResourceTypeId}}};
        materials::BuildDescription invalid = description;
        invalid.resourceTypeCompatibility = {invalidPolicy.data(), static_cast<vanguard::u32>(invalidPolicy.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::InvalidArgument,
              "zero concrete asset types are rejected from the compatibility policy");
    }
    {
        const std::array<materials::ResourceTypeCompatibility, 2> duplicatePolicy{
            {{materials::ResourceParameterKind::Texture, TextureAssetType},
             {materials::ResourceParameterKind::Texture, WrongAssetType}}};
        materials::BuildDescription invalid = description;
        invalid.resourceTypeCompatibility = {duplicatePolicy.data(), static_cast<vanguard::u32>(duplicatePolicy.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::DuplicateResource,
              "duplicate material resource-kind mappings are rejected");
    }
    {
        ShaderFixture noContractFixture;
        noContractFixture.description.materialContract = nullptr;
        ByteArray noContractShaderBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter noContractShaderWriter(noContractShaderBytes);
        Check(shaders::WriteShader(noContractShaderWriter, noContractFixture.description) == shaders::Result::Success,
              "write deliberately contract-free shader fixture");
        filesystem::MemoryFileReader noContractShaderReader(noContractShaderBytes, 0);
        shaders::ShaderFile noContractShader;
        Check(noContractShader.Open(noContractShaderReader) == shaders::Result::Success && !noContractShader.HasMaterialContract(),
              "open deliberately contract-free shader fixture");
        materials::BuildDescription invalid = description;
        invalid.shaderReflection = &noContractShader;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::InvalidArgument,
              "canonical VMAT writing rejects a shader without a sealed material contract");
    }
    {
        std::array<float, 3> wrongSize{{1.0f, 2.0f, 3.0f}};
        const std::array<materials::ConstantValueBuildRecord, 1> wrongConstants{{{0x1100, wrongSize.data(), static_cast<vanguard::u32>(sizeof(wrongSize))}}};
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
        Check(vanguard::pipelines::WritePipeline(stalePipelineWriter, stalePipelineDescription) == vanguard::pipelines::Result::Success,
              "write deliberately stale pipeline fixture");
        filesystem::MemoryFileReader stalePipelineReader(stalePipelineBytes, 0);
        vanguard::pipelines::PipelineFile stalePipeline;
        Check(stalePipeline.Open(stalePipelineReader) == vanguard::pipelines::Result::Success, "open deliberately stale pipeline fixture");
        const std::array<materials::TechniqueBuildRecord, 1> staleTechnique{{{0x9000, techniques[1].pipeline, &stalePipeline}}};
        materials::BuildDescription invalid = description;
        invalid.techniques = {staleTechnique.data(), static_cast<vanguard::u32>(staleTechnique.size())};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, invalid) == materials::Result::TypeMismatch,
              "pipeline with a stale shader binding layout is rejected during material cooking");
    }
    {
        ShaderFixture contractFixture;
        const std::array<shaders::ConstantMember, 1> contractParameters{
            {{0x7100, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
        const std::array<shaders::MaterialResourceRole, 4> contractResources{
            {{0x7200, 0, 0, shaders::MaterialResourceKind::Texture, shaders::MaterialResourceFlags::Required, 0,
              vanguard::crypto::Sha256("Texture2D<float4>", 17),
              {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::D2, shaders::MaterialBufferKind::None,
               shaders::MaterialSamplerKind::None, shaders::ScalarType::F32, 4, shaders::MaterialResourceShapeFlags::None, 0, 0}},
             {0x7201, 0, 1, shaders::MaterialResourceKind::Buffer, shaders::MaterialResourceFlags::None, 0,
              vanguard::crypto::Sha256("StructuredBuffer<float4>", 24),
              {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::None, shaders::MaterialBufferKind::Structured,
               shaders::MaterialSamplerKind::None, shaders::ScalarType::F32, 0, shaders::MaterialResourceShapeFlags::None, 0, 16}},
             {0x7202, 0, 2, shaders::MaterialResourceKind::Sampler, shaders::MaterialResourceFlags::None, 0,
              vanguard::crypto::Sha256("SamplerState", 12),
              {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::None, shaders::MaterialBufferKind::None,
               shaders::MaterialSamplerKind::Filtering, shaders::ScalarType::F32, 0, shaders::MaterialResourceShapeFlags::None, 0, 0}},
             {0x7203, 0, 3, shaders::MaterialResourceKind::AccelerationStructure, shaders::MaterialResourceFlags::None, 0,
              vanguard::crypto::Sha256("RaytracingAccelerationStructure", 31),
              {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::None, shaders::MaterialBufferKind::None,
               shaders::MaterialSamplerKind::None, shaders::ScalarType::F32, 0, shaders::MaterialResourceShapeFlags::None, 0, 0}}}};
        shaders::MaterialContractBuildDescription contract;
        contract.domain.name = 0x7000;
        contract.domain.schemaVersion = 1;
        contract.domain.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        contract.domain.inputType = vanguard::crypto::Sha256("SurfaceInput", 12);
        contract.domain.outputType = vanguard::crypto::Sha256("SurfaceOutput", 13);
        contract.accessorAbiVersion = 1;
        contract.parameterByteSize = 16;
        contract.parameters = {contractParameters.data(), static_cast<vanguard::u32>(contractParameters.size())};
        contract.resources = {contractResources.data(), static_cast<vanguard::u32>(contractResources.size())};
        contractFixture.description.materialContract = &contract;

        ByteArray contractShaderBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter contractShaderWriter(contractShaderBytes);
        Check(shaders::WriteShader(contractShaderWriter, contractFixture.description) == shaders::Result::Success,
              "write shader with explicit material contract");
        filesystem::MemoryFileReader contractShaderReader(contractShaderBytes, 0);
        shaders::ShaderFile contractShader;
        Check(contractShader.Open(contractShaderReader) == shaders::Result::Success && contractShader.HasMaterialContract(),
              "open shader with explicit material contract");

        vanguard::pipelines::ShaderReference contractPipelineShader;
        contractPipelineShader.resource = shaderReference.GetPath().Id();
        contractPipelineShader.permutation = contractShader.GetPermutation();
        contractPipelineShader.bindingLayout = contractShader.BindingLayoutFingerprint();
        contractPipelineShader.pipelineInterface = contractShader.GetPipelineInterfaceFingerprint();
        contractPipelineShader.materialDomain = contractShader.GetMaterialContract()->domainFingerprint;
        contractPipelineShader.materialLayout = contractShader.GetMaterialContract()->layoutFingerprint;
        vanguard::pipelines::BuildDescription contractPipelineDescription = pipelineDescription;
        contractPipelineDescription.shaders = {&contractPipelineShader, 1};
        ByteArray contractPipelineBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter contractPipelineWriter(contractPipelineBytes);
        Check(vanguard::pipelines::WritePipeline(contractPipelineWriter, contractPipelineDescription) == vanguard::pipelines::Result::Success,
              "write pipeline carrying material compatibility fingerprints");
        filesystem::MemoryFileReader contractPipelineReader(contractPipelineBytes, 0);
        vanguard::pipelines::PipelineFile contractPipeline;
        Check(contractPipeline.Open(contractPipelineReader) == vanguard::pipelines::Result::Success,
              "open pipeline carrying material compatibility fingerprints");

        const std::array<materials::TechniqueBuildRecord, 1> contractTechniques{
            {{0x7300, resources::ResourceReference(resources::ResourcePath::FromString("pipelines/contract.vpipeline"),
                                                   vanguard::pipelines::PipelineResourceType),
              &contractPipeline}}};
        const std::array<materials::ConstantValueBuildRecord, 1> contractConstants{
            {{0x7100, baseColor.data(), static_cast<vanguard::u32>(sizeof(baseColor))}}};
        const std::array<materials::ResourceValueBuildRecord, 1> contractResourceValues{
            {{0x7200, 0, albedo, resources::DependencyKind::Required}}};
        materials::BuildDescription contractMaterial;
        contractMaterial.name = 0x7400;
        contractMaterial.shader = shaderReference;
        contractMaterial.shaderReflection = &contractShader;
        contractMaterial.techniques = {contractTechniques.data(), static_cast<vanguard::u32>(contractTechniques.size())};
        contractMaterial.constants = {contractConstants.data(), static_cast<vanguard::u32>(contractConstants.size())};
        contractMaterial.resources = {contractResourceValues.data(), static_cast<vanguard::u32>(contractResourceValues.size())};
        contractMaterial.resourceTypeCompatibility = {resourceTypeCompatibility.data(),
                                                      static_cast<vanguard::u32>(resourceTypeCompatibility.size())};
        ByteArray contractMaterialBytes(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter contractMaterialWriter(contractMaterialBytes);
        Check(materials::WriteMaterial(contractMaterialWriter, contractMaterial) == materials::Result::Success,
              "VMAT derives parameter and resource layout from VSHADER contract");
        filesystem::MemoryFileReader contractMaterialReader(contractMaterialBytes, 0);
        materials::MaterialFile contractMaterialFile;
        Check(contractMaterialFile.Open(contractMaterialReader) == materials::Result::Success &&
                  contractMaterialFile.GetMaterialDomainFingerprint() == contractPipelineShader.materialDomain &&
                  contractMaterialFile.GetMaterialLayoutFingerprint() == contractPipelineShader.materialLayout &&
                  contractMaterialFile.GetResourceParameters().Size() == 4 && contractMaterialFile.GetResourceParameters()[0].slot == 0 &&
                  contractMaterialFile.GetResourceParameters()[0].expectedAssetType == TextureAssetType &&
                  contractMaterialFile.GetResourceParameters()[1].expectedAssetType == resources::InvalidResourceTypeId &&
                  contractMaterialFile.GetResourceParameters()[2].expectedAssetType == resources::InvalidResourceTypeId &&
                  contractMaterialFile.GetResourceParameters()[3].expectedAssetType == resources::InvalidResourceTypeId &&
                  !contractMaterialFile.GetResourceParameters()[1].resource.IsValid() &&
                  !contractMaterialFile.GetResourceParameters()[2].resource.IsValid() &&
                  !contractMaterialFile.GetResourceParameters()[3].resource.IsValid(),
              "VMAT retains exact material compatibility, compile-time slots, and explicit unsupported optional roles");

        const std::array<vanguard::u64, 3> unsupportedNames{{0x7201, 0x7202, 0x7203}};
        for (const vanguard::u64 unsupportedName : unsupportedNames)
        {
            const resources::ResourceReference unsupported(resources::ResourcePath::FromId(unsupportedName + 0x10000), WrongAssetType);
            const materials::ResourceValueBuildRecord unsupportedValue{unsupportedName, 0, unsupported, resources::DependencyKind::Required};
            materials::BuildDescription rejectedUnsupported = contractMaterial;
            rejectedUnsupported.resources = {&unsupportedValue, 1};
            ByteArray unsupportedBytes(memory::pools::Rendering::GetInstance());
            filesystem::MemoryFileWriter unsupportedWriter(unsupportedBytes);
            Check(materials::WriteMaterial(unsupportedWriter, rejectedUnsupported) == materials::Result::TypeMismatch,
                  "required assignment to a reflected resource kind without a concrete asset mapping is rejected");
        }

        materials::BuildDescription missingRequired = contractMaterial;
        missingRequired.resources = {};
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        filesystem::MemoryFileWriter rejectedWriter(rejected);
        Check(materials::WriteMaterial(rejectedWriter, missingRequired) == materials::Result::TypeMismatch,
              "required shader-declared material resource cannot be omitted");
    }
    {
        ByteArray corrupt(first);
        corrupt[64] ^= 0x5au;
        filesystem::MemoryFileReader corruptReader(corrupt, 0);
        materials::MaterialFile rejected;
        Check(rejected.Open(corruptReader) == materials::Result::IntegrityFailure, "vmat section corruption is rejected before publication");
    }
    {
        ByteArray incompatible(first);
        Check(RewriteFirstResourceExpectedType(incompatible, WrongAssetType), "rewrite persisted expected asset type fixture");
        filesystem::MemoryFileReader incompatibleReader(incompatible, 0);
        materials::MaterialFile rejected;
        Check(rejected.Open(incompatibleReader) == materials::Result::InvalidLayout,
              "reopen validation rejects an assigned resource whose persisted expected asset type was forged");
    }
    {
        ByteArray oldVersion(first);
        Check(RewriteDocumentMinorVersion(oldVersion, 1), "rewrite old vmat version fixture");
        filesystem::MemoryFileReader oldVersionReader(oldVersion, 0);
        materials::MaterialFile rejected;
        Check(rejected.Open(oldVersionReader) == materials::Result::UnsupportedVersion,
              "clean-cut vmat 1.2 reader rejects the previous 1.1 document version");
    }

    std::array<packages::Dependency, 8> packageDependencies{};
    vanguard::u32 packageDependencyCount = 0;
    for (const materials::ResourceDependency& dependency : material.GetDependencies())
        packageDependencies[packageDependencyCount++] = {dependency.resource.GetPath().Id(), dependency.resource.ExpectedType(), dependency.kind};
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
    Check(packageWriter.Begin(packageFile) == packages::Result::Success && packageWriter.Add(packagedMaterial) == packages::Result::Success &&
              packageWriter.Finalize() == packages::Result::Success,
          "package vmat as an opaque VPAK resource");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == packages::Result::Success, "open VPAK containing vmat");
    const packages::Resource* const packagedRecord = packageReader.Find("materials/stone.vmat");
    Check(packagedRecord != nullptr && packagedRecord->type == materials::MaterialResourceType &&
              packageReader.GetDependencies(*packagedRecord).Size() == material.GetDependencies().Size(),
          "VPAK preserves shader, pipeline, and texture dependencies without interpreting vmat");
    if (packagedRecord != nullptr)
    {
        packages::ResourceFileReader packagedView;
        Check(packagedView.Open(packageReader, *packagedRecord, packageReaderFile) == packages::Result::Success, "open logical vmat directly over VPAK");
        materials::MaterialFile packaged;
        Check(packaged.Open(packagedView) == materials::Result::Success && packaged.GetContentFingerprint() == material.GetContentFingerprint(),
              "package-backed vmat opens without format translation");
    }

    material.Close();
    pipeline.Close();
    shader.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    if (g_failures == 0)
        std::puts("[materialsTests] Vanguard vmat conformance checks passed");
    return g_failures == 0 ? 0 : 1;
}
