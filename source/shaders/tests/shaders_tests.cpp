#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/shaders/shaders.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace
{
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;
    namespace shaders = vanguard::shaders;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[shadersTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    constexpr std::array<vanguard::u8, 12> VertexBytecode{0x44, 0x58, 0x49, 0x4c, 1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<vanguard::u8, 10> FragmentBytecode{0x44, 0x58, 0x49, 0x4c, 9, 8, 7, 6, 5, 4};

    struct Fixture
    {
        std::array<shaders::StageBuildRecord, 2> stages;
        std::array<shaders::DescriptorBinding, 2> bindings;
        std::array<shaders::ConstantBuffer, 1> buffers;
        std::array<shaders::ConstantMember, 2> members;
        std::array<shaders::VertexInput, 2> inputs;
        std::array<shaders::FragmentOutput, 1> outputs;
        std::array<shaders::SpecializationConstant, 1> constants;
        std::array<shaders::ConstantMember, 2> materialParameters;
        std::array<shaders::MaterialResourceRole, 2> materialResources;
        shaders::MaterialContractBuildDescription materialContract;
        shaders::BuildDescription description;

        Fixture()
        {
            stages = {{{shaders::ShaderStage::Fragment, shaders::NativeFormat::Dxil, 0x2002, FragmentBytecode.data(), FragmentBytecode.size(), "mainPS"},
                       {shaders::ShaderStage::Vertex, shaders::NativeFormat::Dxil, 0x1001, VertexBytecode.data(), VertexBytecode.size(), "mainVS"}}};
            bindings = {
                {{0x9002, 1, 3, 1, shaders::BindingKind::SampledTexture, shaders::BindingAccess::Read, shaders::StageBit(shaders::ShaderStage::Fragment)},
                 {0x9001, 0, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read, shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment)}}};
            buffers = {{{0x9001, 0, 0, 32, 0, 2}}};
            members = {{{0xa001, 16, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}, {0xa000, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
            inputs = {{{0xb001, 0, 1, shaders::NumericClass::FloatingPoint, 2, 32}, {0xb000, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            outputs = {{{0xc000, 0, 0, shaders::NumericClass::FloatingPoint, 0x0f}}};
            constants = {{{0xd000, 7, shaders::ScalarType::U32, 4, shaders::StageBit(shaders::ShaderStage::Fragment)}}};
            materialParameters = {{{0xe001, 16, 4, 0, 0, shaders::ScalarType::F32, 1, 1, false}, {0xe000, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
            materialResources = {{{0xf001,
                                   0,
                                   1,
                                   shaders::MaterialResourceKind::Sampler,
                                   shaders::MaterialResourceFlags::Required,
                                   0,
                                   vanguard::crypto::Sha256("SamplerState", 12),
                                   {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::None, shaders::MaterialBufferKind::None, shaders::MaterialSamplerKind::Filtering,
                                    shaders::ScalarType::F32, 0, shaders::MaterialResourceShapeFlags::None, 0, 0}},
                                  {0xf000,
                                   0,
                                   0,
                                   shaders::MaterialResourceKind::Texture,
                                   shaders::MaterialResourceFlags::Required,
                                   0,
                                   vanguard::crypto::Sha256("Texture2D<float4>", 17),
                                   {shaders::MaterialResourceAccess::Read, shaders::MaterialTextureDimension::D2, shaders::MaterialBufferKind::None, shaders::MaterialSamplerKind::None, shaders::ScalarType::F32,
                                    4, shaders::MaterialResourceShapeFlags::None, 0, 0}}}};

            materialContract.domain.name = 0x12345678;
            materialContract.domain.schemaVersion = 3;
            materialContract.domain.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
            materialContract.domain.requiredCapabilities =
                shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Numeric16Bit) | shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::ComparisonSampling);
            materialContract.domain.inputType = vanguard::crypto::Sha256("SurfaceInput", 12);
            materialContract.domain.outputType = vanguard::crypto::Sha256("SurfaceOutput", 13);
            materialContract.accessorAbiVersion = 1;
            materialContract.parameterByteSize = 32;
            materialContract.parameters = {materialParameters.data(), static_cast<vanguard::u32>(materialParameters.size())};
            materialContract.resources = {materialResources.data(), static_cast<vanguard::u32>(materialResources.size())};

            description.kind = shaders::ProgramKind::Graphics;
            description.program = 0x5aa55aa5;
            description.permutation = vanguard::crypto::Sha256("permutation", 11);
            description.compilerFingerprint = vanguard::crypto::Sha256("dxc-1.8-options", 15);
            description.pipelineInterface.stages = shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment);
            description.pipelineInterface.primitiveClass = shaders::PrimitiveClass::Triangle;
            description.pipelineInterface.renderTargetCount = 1;
            description.stages = {stages.data(), static_cast<vanguard::u32>(stages.size())};
            description.bindings = {bindings.data(), static_cast<vanguard::u32>(bindings.size())};
            description.constantBuffers = {buffers.data(), static_cast<vanguard::u32>(buffers.size())};
            description.constantMembers = {members.data(), static_cast<vanguard::u32>(members.size())};
            description.vertexInputs = {inputs.data(), static_cast<vanguard::u32>(inputs.size())};
            description.fragmentOutputs = {outputs.data(), static_cast<vanguard::u32>(outputs.size())};
            description.specializationConstants = {constants.data(), static_cast<vanguard::u32>(constants.size())};
            description.materialContract = &materialContract;
        }
    };

    shaders::Result WriteFixture(const shaders::BuildDescription& description, ByteArray& output)
    {
        output.Clear();
        vanguard::filesystem::MemoryFileWriter file(output);
        return shaders::WriteShader(file, description);
    }

    bool EqualBytes(const ByteArray& left, const ByteArray& right)
    {
        if (left.Size() != right.Size())
        {
            return false;
        }
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
        {
            if (left[index] != right[index])
            {
                return false;
            }
        }
        return true;
    }
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "shadersTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    Fixture fixture;
    Fixture reorderedFixture;
    std::swap(reorderedFixture.stages[0], reorderedFixture.stages[1]);
    std::swap(reorderedFixture.bindings[0], reorderedFixture.bindings[1]);
    std::swap(reorderedFixture.members[0], reorderedFixture.members[1]);
    std::swap(reorderedFixture.inputs[0], reorderedFixture.inputs[1]);
    std::swap(reorderedFixture.materialParameters[0], reorderedFixture.materialParameters[1]);
    std::swap(reorderedFixture.materialResources[0], reorderedFixture.materialResources[1]);
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == shaders::Result::Success, "write vshader");
    Check(WriteFixture(reorderedFixture.description, second) == shaders::Result::Success && EqualBytes(first, second), "canonical emission is independent of compiler reflection ordering");
    Check(first.Size() > 128, "vshader emitted bytes");

    filesystem::MemoryFileReader file(first, 0);
    shaders::ShaderFile shader;
    const shaders::Result openResult = shader.Open(file);
    if (openResult != shaders::Result::Success)
    {
        std::fprintf(stderr, "[shadersTests] vshader open result: %s\n", shaders::ToString(openResult));
    }
    Check(openResult == shaders::Result::Success, "open vshader");
    Check(shader.IsOpen() && shader.GetKind() == shaders::ProgramKind::Graphics && shader.GetProgram() == fixture.description.program, "program metadata round trip");
    Check(shader.GetStages().Size() == 2 && shader.GetStages()[0].stage == shaders::ShaderStage::Vertex && shader.GetStages()[1].stage == shaders::ShaderStage::Fragment &&
              std::strcmp(shader.GetStages()[0].entryPointName, "mainVS") == 0 && std::strcmp(shader.GetStages()[1].entryPointName, "mainPS") == 0,
          "stage records and native entry points are canonical");
    Check(shader.Bindings().Size() == 2 && shader.Bindings()[0].space == 0 && shader.Bindings()[1].space == 1, "binding records are canonical");
    Check(shader.GetConstantMembers().Size() == 2 && shader.GetConstantMembers()[0].byteOffset == 0 && shader.GetConstantMembers()[1].byteOffset == 16, "constant layout is canonical");
    Check(shader.HasMaterialContract() && shader.GetMaterialContract() != nullptr && shaders::MaterialDomainContractsEqual(shader.GetMaterialContract()->domain, fixture.materialContract.domain) &&
              !shader.GetMaterialContract()->domainFingerprint.IsEmpty() && !shader.GetMaterialContract()->layoutFingerprint.IsEmpty(),
          "explicit material domain and layout identities survive serialization");
    vanguard::crypto::Digest256 expectedMaterialDomainFingerprint;
    Check(shaders::CalculateMaterialDomainFingerprint(fixture.materialContract.domain, expectedMaterialDomainFingerprint) == shaders::Result::Success && shader.GetMaterialContract() != nullptr &&
              shader.GetMaterialContract()->domainFingerprint == expectedMaterialDomainFingerprint,
          "writer uses the public canonical material domain fingerprint");
    Check(shader.GetMaterialParameters().Size() == 2 && shader.GetMaterialParameters()[0].byteOffset == 0 && shader.GetMaterialResources().Size() == 2 && shader.GetMaterialResources()[0].slot == 0,
          "material parameter and logical resource layouts are canonical");
    if (shader.GetStages().Size() != 0)
    {
        Check(shader.GetBytecode(shader.GetStages()[0]).Size() == VertexBytecode.size(), "native vertex bytecode is directly addressable");
    }

    shaders::PipelineCompatibility pipeline;
    pipeline.kind = shaders::PipelineKind::Graphics;
    pipeline.primitiveClass = shaders::PrimitiveClass::Triangle;
    pipeline.renderTargetCount = 1;
    pipeline.renderTargetClasses[0] = shaders::NumericClass::FloatingPoint;
    pipeline.vertexLayout = shader.GetVertexInputs();
    pipeline.bindingLayoutFingerprint = shader.BindingLayoutFingerprint();
    pipeline.pipelineInterfaceFingerprint = shader.GetPipelineInterfaceFingerprint();
    Check(shaders::ValidatePipeline(shader, pipeline) == shaders::Result::Success, "compatible graphics PSO");

    pipeline.renderTargetClasses[0] = shaders::NumericClass::UnsignedInteger;
    Check(shaders::ValidatePipeline(shader, pipeline) == shaders::Result::IncompatiblePipeline, "PSO render target numeric class mismatch");
    pipeline.renderTargetClasses[0] = shaders::NumericClass::FloatingPoint;
    pipeline.primitiveClass = shaders::PrimitiveClass::Line;
    Check(shaders::ValidatePipeline(shader, pipeline) == shaders::Result::IncompatiblePipeline, "PSO primitive mismatch");
    pipeline.primitiveClass = shaders::PrimitiveClass::Triangle;
    pipeline.bindingLayoutFingerprint.bytes[0] ^= 1u;
    Check(shaders::ValidatePipeline(shader, pipeline) == shaders::Result::IncompatiblePipeline, "PSO binding layout mismatch");

    {
        Fixture sparseMaterialResources;
        sparseMaterialResources.materialResources[0].slot = 7;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(sparseMaterialResources.description, rejected) == shaders::Result::InvalidLayout, "material resource roles must form a dense compile-time slot range");
    }
    {
        Fixture invalidTextureShape;
        invalidTextureShape.materialResources[1].shape.componentCount = 0;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalidTextureShape.description, rejected) == shaders::Result::InvalidLayout, "texture roles reject a tampered reflected component shape");
    }
    {
        Fixture invalidSamplerShape;
        invalidSamplerShape.materialResources[0].shape.access = shaders::MaterialResourceAccess::Write;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(invalidSamplerShape.description, rejected) == shaders::Result::InvalidLayout, "sampler roles reject a tampered writable shape");
    }
    {
        Fixture unknownShapeFlags;
        unknownShapeFlags.materialResources[1].shape.flags = static_cast<shaders::MaterialResourceShapeFlags>(0x80u);
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(unknownShapeFlags.description, rejected) == shaders::Result::InvalidLayout, "material roles reject unknown reflected shape flags");
    }
    {
        Fixture arrayedTexture3d;
        arrayedTexture3d.materialResources[1].shape.textureDimension = shaders::MaterialTextureDimension::D3;
        arrayedTexture3d.materialResources[1].shape.flags = shaders::MaterialResourceShapeFlags::Arrayed;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(arrayedTexture3d.description, rejected) == shaders::Result::InvalidLayout, "material roles reject the non-portable Texture3D array shape");
    }
    {
        Fixture multisampledTexture1d;
        multisampledTexture1d.materialResources[1].shape.textureDimension = shaders::MaterialTextureDimension::D1;
        multisampledTexture1d.materialResources[1].shape.flags = shaders::MaterialResourceShapeFlags::Multisampled;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(multisampledTexture1d.description, rejected) == shaders::Result::InvalidLayout, "material roles restrict multisampling to two-dimensional texture shapes");
    }
    {
        Fixture changedCapabilities;
        changedCapabilities.materialContract.domain.requiredCapabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::WritableResources);
        vanguard::crypto::Digest256 changedFingerprint;
        Check(shaders::CalculateMaterialDomainFingerprint(changedCapabilities.materialContract.domain, changedFingerprint) == shaders::Result::Success && changedFingerprint != expectedMaterialDomainFingerprint,
              "required capability changes alter the material domain identity");
    }
    {
        Fixture unknownCapabilities;
        unknownCapabilities.materialContract.domain.requiredCapabilities = shaders::KnownMaterialShaderCapabilityMask | (1u << 31u);
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        vanguard::crypto::Digest256 rejectedFingerprint;
        Check(shaders::CalculateMaterialDomainFingerprint(unknownCapabilities.materialContract.domain, rejectedFingerprint) == shaders::Result::InvalidLayout &&
                  WriteFixture(unknownCapabilities.description, rejected) == shaders::Result::InvalidLayout,
              "unknown material shader capabilities are rejected");
    }
    {
        Fixture separateNamespaces;
        separateNamespaces.bindings[0].space = separateNamespaces.bindings[1].space;
        separateNamespaces.bindings[0].binding = separateNamespaces.bindings[1].binding;
        ByteArray accepted(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(separateNamespaces.description, accepted) == shaders::Result::Success, "CBV and SRV registers may share a numeric slot in one register space");
    }
    {
        Fixture duplicate;
        duplicate.bindings[0].space = duplicate.bindings[1].space;
        duplicate.bindings[0].binding = duplicate.bindings[1].binding;
        duplicate.bindings[1].kind = shaders::BindingKind::SampledTexture;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(duplicate.description, rejected) == shaders::Result::DuplicateBinding, "overlapping descriptors in the same register namespace are rejected");
    }
    {
        Fixture bindless;
        bindless.bindings[0].arrayCount = shaders::UnboundedDescriptorCount;
        bindless.bindings[0].flags = shaders::BindingFlags::Bindless;
        ByteArray cooked(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(bindless.description, cooked) == shaders::Result::Success, "bindless reflection cooking");
        filesystem::MemoryFileReader bindlessFile(cooked, 0);
        shaders::ShaderFile reflected;
        Check(reflected.Open(bindlessFile) == shaders::Result::Success && reflected.Bindings().Size() == 2 && shaders::HasFlag(reflected.Bindings()[1].flags, shaders::BindingFlags::Bindless) &&
                  reflected.Bindings()[1].arrayCount == shaders::UnboundedDescriptorCount,
              "bindless declaration round trip");
    }
    {
        ByteArray corrupt(first);
        if (corrupt.Size() > 64)
        {
            corrupt[64] ^= 1u;
        }
        filesystem::MemoryFileReader corruptFile(corrupt, 0);
        shaders::ShaderFile rejected;
        Check(corrupt.Size() > 64 && rejected.Open(corruptFile) == shaders::Result::IntegrityFailure, "checksummed metadata corruption rejection");
    }
    {
        ByteArray previousVersion(first);
        filesystem::MemoryFileWriter versionFile(previousVersion);
        vanguard::serialization::BinaryWriter versionWriter(versionFile);
        const bool patched = versionWriter.Seek(10) && versionWriter.WriteU16(2) && versionWriter.Seek(36) && versionWriter.WriteU32(vanguard::serialization::Crc32(previousVersion.Data(), 36));
        filesystem::MemoryFileReader previousVersionFile(previousVersion, 0);
        shaders::ShaderFile rejected;
        Check(patched && rejected.Open(previousVersionFile) == shaders::Result::UnsupportedVersion, "previous VSHADER versions are rejected after the clean format cut");
    }
    {
        shaders::ReadLimits limits;
        limits.maximumBytecodeBytes = 1;
        filesystem::MemoryFileReader limitedFile(first, 0);
        shaders::ShaderFile rejected;
        Check(rejected.Open(limitedFile, limits) == shaders::Result::LimitExceeded, "bytecode size limit");
    }

    shader.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[shadersTests] Vanguard vshader and PSO compatibility checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
