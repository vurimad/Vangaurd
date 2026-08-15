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
        shaders::BuildDescription description;

        Fixture()
        {
            stages = {
                {{shaders::ShaderStage::Fragment, shaders::NativeFormat::Dxil, 0x2002, FragmentBytecode.data(), FragmentBytecode.size(), "mainPS"},
                 {shaders::ShaderStage::Vertex, shaders::NativeFormat::Dxil, 0x1001, VertexBytecode.data(), VertexBytecode.size(), "mainVS"}}};
            bindings = {{{0x9002, 1, 3, 1, shaders::BindingKind::SampledTexture, shaders::BindingAccess::Read,
                          shaders::StageBit(shaders::ShaderStage::Fragment)},
                         {0x9001, 0, 0, 1, shaders::BindingKind::ConstantBuffer, shaders::BindingAccess::Read,
                          shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment)}}};
            buffers = {{{0x9001, 0, 0, 32, 0, 2}}};
            members = {{{0xa001, 16, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false},
                        {0xa000, 0, 16, 0, 0, shaders::ScalarType::F32, 1, 4, false}}};
            inputs = {
                {{0xb001, 0, 1, shaders::NumericClass::FloatingPoint, 2, 32}, {0xb000, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            outputs = {{{0xc000, 0, 0, shaders::NumericClass::FloatingPoint, 0x0f}}};
            constants = {{{0xd000, 7, shaders::ScalarType::U32, 4, shaders::StageBit(shaders::ShaderStage::Fragment)}}};

            description.kind = shaders::ProgramKind::Graphics;
            description.program = 0x5aa55aa5;
            description.permutation = vanguard::crypto::Sha256("permutation", 11);
            description.compilerFingerprint = vanguard::crypto::Sha256("dxc-1.8-options", 15);
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
            description.specializationConstants = {constants.data(), static_cast<vanguard::u32>(constants.size())};
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
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == shaders::Result::Success, "write vshader");
    Check(WriteFixture(reorderedFixture.description, second) == shaders::Result::Success && EqualBytes(first, second),
          "canonical emission is independent of compiler reflection ordering");
    Check(first.Size() > 128, "vshader emitted bytes");

    filesystem::MemoryFileReader file(first, 0);
    shaders::ShaderFile shader;
    const shaders::Result openResult = shader.Open(file);
    if (openResult != shaders::Result::Success)
    {
        std::fprintf(stderr, "[shadersTests] vshader open result: %s\n", shaders::ToString(openResult));
    }
    Check(openResult == shaders::Result::Success, "open vshader");
    Check(shader.IsOpen() && shader.Kind() == shaders::ProgramKind::Graphics && shader.Program() == fixture.description.program,
          "program metadata round trip");
    Check(shader.Stages().Size() == 2 && shader.Stages()[0].stage == shaders::ShaderStage::Vertex &&
              shader.Stages()[1].stage == shaders::ShaderStage::Fragment &&
              std::strcmp(shader.Stages()[0].entryPointName, "mainVS") == 0 &&
              std::strcmp(shader.Stages()[1].entryPointName, "mainPS") == 0,
          "stage records and native entry points are canonical");
    Check(shader.Bindings().Size() == 2 && shader.Bindings()[0].space == 0 && shader.Bindings()[1].space == 1,
          "binding records are canonical");
    Check(shader.ConstantMembers().Size() == 2 && shader.ConstantMembers()[0].byteOffset == 0 &&
              shader.ConstantMembers()[1].byteOffset == 16,
          "constant layout is canonical");
    if (shader.Stages().Size() != 0)
    {
        Check(shader.Bytecode(shader.Stages()[0]).Size() == VertexBytecode.size(), "native vertex bytecode is directly addressable");
    }

    shaders::PipelineCompatibility pipeline;
    pipeline.kind = shaders::PipelineKind::Graphics;
    pipeline.primitiveClass = shaders::PrimitiveClass::Triangle;
    pipeline.renderTargetCount = 1;
    pipeline.renderTargetClasses[0] = shaders::NumericClass::FloatingPoint;
    pipeline.vertexLayout = shader.VertexInputs();
    pipeline.bindingLayoutFingerprint = shader.BindingLayoutFingerprint();
    pipeline.pipelineInterfaceFingerprint = shader.PipelineInterfaceFingerprint();
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
        Fixture separateNamespaces;
        separateNamespaces.bindings[0].space = separateNamespaces.bindings[1].space;
        separateNamespaces.bindings[0].binding = separateNamespaces.bindings[1].binding;
        ByteArray accepted(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(separateNamespaces.description, accepted) == shaders::Result::Success,
              "CBV and SRV registers may share a numeric slot in one register space");
    }
    {
        Fixture duplicate;
        duplicate.bindings[0].space = duplicate.bindings[1].space;
        duplicate.bindings[0].binding = duplicate.bindings[1].binding;
        duplicate.bindings[1].kind = shaders::BindingKind::SampledTexture;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(duplicate.description, rejected) == shaders::Result::DuplicateBinding,
              "overlapping descriptors in the same register namespace are rejected");
    }
    {
        Fixture bindless;
        bindless.bindings[0].arrayCount = shaders::UnboundedDescriptorCount;
        bindless.bindings[0].flags = shaders::BindingFlags::Bindless;
        ByteArray cooked(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(bindless.description, cooked) == shaders::Result::Success, "bindless reflection cooking");
        filesystem::MemoryFileReader bindlessFile(cooked, 0);
        shaders::ShaderFile reflected;
        Check(reflected.Open(bindlessFile) == shaders::Result::Success && reflected.Bindings().Size() == 2 &&
                  shaders::HasFlag(reflected.Bindings()[1].flags, shaders::BindingFlags::Bindless) &&
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
        Check(corrupt.Size() > 64 && rejected.Open(corruptFile) == shaders::Result::IntegrityFailure,
              "checksummed metadata corruption rejection");
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
