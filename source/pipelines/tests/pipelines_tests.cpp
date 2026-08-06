#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/pipelines/pipelines.hpp>

#include <array>
#include <cstdio>
#include <utility>

namespace
{
    namespace pipelines = vanguard::pipelines;
    namespace shaders = vanguard::shaders;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[pipelinesTests] FAILED: %s\n", message);
            ++g_failures;
        }
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

    constexpr std::array<vanguard::u8, 8> VertexBytecode{0x44, 0x58, 0x49, 0x4c, 1, 2, 3, 4};
    constexpr std::array<vanguard::u8, 8> FragmentBytecode{0x44, 0x58, 0x49, 0x4c, 5, 6, 7, 8};

    struct ShaderFixture
    {
        std::array<shaders::StageBuildRecord, 2> stages;
        std::array<shaders::VertexInput, 2> inputs;
        std::array<shaders::FragmentOutput, 1> outputs;
        shaders::BuildDescription description;

        ShaderFixture()
        {
            stages = {
                {{shaders::ShaderStage::Fragment, shaders::NativeFormat::Dxil, 0x2002, FragmentBytecode.data(), FragmentBytecode.size()},
                 {shaders::ShaderStage::Vertex, shaders::NativeFormat::Dxil, 0x1001, VertexBytecode.data(), VertexBytecode.size()}}};
            inputs = {{{0x10002, 0, 1, shaders::NumericClass::FloatingPoint, 2, 32},
                       {0x10001, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            outputs = {{{0x20001, 0, 0, shaders::NumericClass::FloatingPoint, 0x0f}}};
            description.kind = shaders::ProgramKind::Graphics;
            description.program = 0x51504c4e;
            description.permutation = vanguard::crypto::Sha256("pipeline-permutation", 20);
            description.compilerFingerprint = vanguard::crypto::Sha256("compiler-and-options", 20);
            description.pipelineInterface.stages =
                shaders::StageBit(shaders::ShaderStage::Vertex) | shaders::StageBit(shaders::ShaderStage::Fragment);
            description.pipelineInterface.primitiveClass = shaders::PrimitiveClass::Triangle;
            description.pipelineInterface.renderTargetCount = 1;
            description.stages = {stages.data(), static_cast<vanguard::u32>(stages.size())};
            description.vertexInputs = {inputs.data(), static_cast<vanguard::u32>(inputs.size())};
            description.fragmentOutputs = {outputs.data(), static_cast<vanguard::u32>(outputs.size())};
        }
    };

    struct GraphicsFixture
    {
        std::array<pipelines::ShaderReference, 1> shader;
        std::array<pipelines::VertexStream, 2> streams;
        std::array<pipelines::VertexAttribute, 2> attributes;
        pipelines::BuildDescription description;

        explicit GraphicsFixture(const shaders::ShaderFile& shaderFile)
        {
            shader = {
                {{0x70001, shaderFile.Permutation(), shaderFile.BindingLayoutFingerprint(), shaderFile.PipelineInterfaceFingerprint()}}};
            streams = {{{1, 8, pipelines::InputRate::PerVertex, 1}, {0, 12, pipelines::InputRate::PerVertex, 1}}};
            attributes = {{{0x10002, 0, 1, 1, 0, shaders::NumericClass::FloatingPoint, 2, 32},
                           {0x10001, 0, 0, 0, 0, shaders::NumericClass::FloatingPoint, 3, 32}}};
            description.kind = pipelines::PipelineKind::Graphics;
            description.name = 0x90001;
            description.dynamicStates = pipelines::DynamicState::Viewport | pipelines::DynamicState::Scissor |
                                        pipelines::DynamicState::DepthBias | pipelines::DynamicState::DepthBounds;
            description.shaders = {shader.data(), static_cast<vanguard::u32>(shader.size())};
            description.vertexStreams = {streams.data(), static_cast<vanguard::u32>(streams.size())};
            description.vertexAttributes = {attributes.data(), static_cast<vanguard::u32>(attributes.size())};
            description.graphics.rasterizer.depthBias = 19;
            description.graphics.rasterizer.depthBiasClamp = 2.0f;
            description.graphics.rasterizer.slopeScaledDepthBias = 3.0f;
            description.graphics.depthStencil.depthBoundsTest = true;
            description.graphics.depthStencil.minimumDepthBounds = 0.25f;
            description.graphics.depthStencil.maximumDepthBounds = 0.75f;
            description.graphics.blend.attachmentCount = 1;
            description.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Deferred;
        }
    };

    pipelines::AttachmentSignature MakeAttachments(const pipelines::FormatId colorFormat)
    {
        pipelines::AttachmentSignature attachments;
        attachments.colorCount = 1;
        attachments.colors[0] = {colorFormat, shaders::NumericClass::FloatingPoint};
        attachments.depthStencilFormat = 2001;
        attachments.depthStencilClass = pipelines::DepthStencilClass::DepthStencil;
        attachments.sampleCount = 1;
        return attachments;
    }

    pipelines::Result WriteFixture(const pipelines::BuildDescription& description, ByteArray& bytes)
    {
        bytes.Clear();
        vanguard::filesystem::MemoryFileWriter writer(bytes);
        return pipelines::WritePipeline(writer, description);
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
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "pipelinesTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");

    ShaderFixture shaderFixture;
    ByteArray shaderBytes(memory::pools::Rendering::GetInstance());
    filesystem::MemoryFileWriter shaderWriter(shaderBytes);
    Check(shaders::WriteShader(shaderWriter, shaderFixture.description) == shaders::Result::Success, "write shader dependency");
    filesystem::MemoryFileReader shaderReader(shaderBytes, 0);
    shaders::ShaderFile shaderFile;
    Check(shaderFile.Open(shaderReader) == shaders::Result::Success, "open shader dependency");

    GraphicsFixture fixture(shaderFile);
    ByteArray first(memory::pools::Rendering::GetInstance());
    ByteArray second(memory::pools::Rendering::GetInstance());
    Check(WriteFixture(fixture.description, first) == pipelines::Result::Success, "write deferred graphics pipeline");

    GraphicsFixture reordered(shaderFile);
    std::swap(reordered.streams[0], reordered.streams[1]);
    std::swap(reordered.attributes[0], reordered.attributes[1]);
    Check(WriteFixture(reordered.description, second) == pipelines::Result::Success && EqualBytes(first, second),
          "canonical emission is independent of input ordering");

    filesystem::MemoryFileReader pipelineReader(first, 0);
    pipelines::PipelineFile pipeline;
    Check(pipeline.Open(pipelineReader) == pipelines::Result::Success, "open pipeline");
    Check(pipeline.IsOpen() && pipeline.Kind() == pipelines::PipelineKind::Graphics && pipeline.Name() == fixture.description.name,
          "pipeline identity round trip");
    Check(pipeline.Graphics().topology == pipelines::PrimitiveTopology::TriangleList && pipeline.Graphics().blend.attachmentCount == 1 &&
              pipeline.Graphics().blend.attachments[0].sourceColor == pipelines::BlendFactor::One &&
              pipeline.Graphics().blend.attachments[0].destinationColor == pipelines::BlendFactor::Zero &&
              pipeline.Graphics().blend.attachments[0].sourceAlpha == pipelines::BlendFactor::One &&
              pipeline.Graphics().blend.attachments[0].destinationAlpha == pipelines::BlendFactor::Zero,
          "graphics fixed function state round trip");
    Check(pipeline.VertexStreams().Size() == 2 && pipeline.VertexStreams()[0].binding == 0 && pipeline.VertexAttributes()[0].location == 0,
          "vertex layout is canonical");
    Check(pipeline.Shaders().Size() == 1 && pipeline.Shaders()[0].permutation == shaderFile.Permutation() &&
              pipeline.Shaders()[0].bindingLayout == shaderFile.BindingLayoutFingerprint() &&
              pipeline.Shaders()[0].pipelineInterface == shaderFile.PipelineInterfaceFingerprint(),
          "shader dependency fingerprints round trip");
    Check(pipeline.VertexAttributes().Size() == shaderFile.VertexInputs().Size() &&
              pipeline.VertexAttributes()[0].numericClass == shaderFile.VertexInputs()[0].numericClass &&
              pipeline.VertexAttributes()[0].componentCount == shaderFile.VertexInputs()[0].componentCount &&
              pipeline.VertexAttributes()[0].componentBits == shaderFile.VertexInputs()[0].componentBits &&
              pipeline.VertexAttributes()[1].numericClass == shaderFile.VertexInputs()[1].numericClass &&
              pipeline.VertexAttributes()[1].componentCount == shaderFile.VertexInputs()[1].componentCount &&
              pipeline.VertexAttributes()[1].componentBits == shaderFile.VertexInputs()[1].componentBits,
          "vertex interface round trip");
    Check(pipeline.Graphics().rasterizer.depthBias == 0 && pipeline.Graphics().rasterizer.depthBiasClamp == 0.0f &&
              pipeline.Graphics().depthStencil.minimumDepthBounds == 0.0f && pipeline.Graphics().depthStencil.maximumDepthBounds == 1.0f,
          "dynamic values are excluded from the static pipeline template");

    const pipelines::AttachmentSignature attachments = MakeAttachments(1001);
    pipelines::AttachmentSignature alternateAttachments = MakeAttachments(1002);
    Check(shaderFile.Interface().primitiveClass == shaders::PrimitiveClass::Triangle &&
              shaderFile.Interface().renderTargetCount == attachments.colorCount && shaderFile.FragmentOutputs().Size() == 1 &&
              shaderFile.FragmentOutputs()[0].numericClass == attachments.colors[0].numericClass,
          "graphics shader interface matches attachment contract");
    std::array<shaders::VertexInput, 2> reflectedLayout{
        {{pipeline.VertexAttributes()[0].semantic, pipeline.VertexAttributes()[0].semanticIndex, pipeline.VertexAttributes()[0].location,
          pipeline.VertexAttributes()[0].numericClass, pipeline.VertexAttributes()[0].componentCount,
          pipeline.VertexAttributes()[0].componentBits},
         {pipeline.VertexAttributes()[1].semantic, pipeline.VertexAttributes()[1].semanticIndex, pipeline.VertexAttributes()[1].location,
          pipeline.VertexAttributes()[1].numericClass, pipeline.VertexAttributes()[1].componentCount,
          pipeline.VertexAttributes()[1].componentBits}}};
    shaders::PipelineCompatibility directCompatibility;
    directCompatibility.kind = shaders::PipelineKind::Graphics;
    directCompatibility.primitiveClass = shaders::PrimitiveClass::Triangle;
    directCompatibility.renderTargetCount = 1;
    directCompatibility.renderTargetClasses[0] = shaders::NumericClass::FloatingPoint;
    directCompatibility.sampleCount = 1;
    directCompatibility.depthStencilFormatPresent = true;
    directCompatibility.vertexLayout = {reflectedLayout.data(), static_cast<vanguard::u32>(reflectedLayout.size())};
    directCompatibility.bindingLayoutFingerprint = shaderFile.BindingLayoutFingerprint();
    directCompatibility.pipelineInterfaceFingerprint = shaderFile.PipelineInterfaceFingerprint();
    Check(shaders::ValidatePipeline(shaderFile, directCompatibility) == shaders::Result::Success, "direct shader compatibility contract");
    vanguard::crypto::Digest256 firstKey;
    vanguard::crypto::Digest256 repeatedKey;
    vanguard::crypto::Digest256 alternateKey;
    Check(pipelines::CalculateConcretePipelineKey(pipeline, &attachments, firstKey) == pipelines::Result::Success &&
              pipelines::CalculateConcretePipelineKey(pipeline, &attachments, repeatedKey) == pipelines::Result::Success &&
              firstKey == repeatedKey,
          "concrete PSO key is deterministic");
    Check(pipelines::CalculateConcretePipelineKey(pipeline, &alternateAttachments, alternateKey) == pipelines::Result::Success &&
              alternateKey != firstKey,
          "render-graph attachment formats specialize the concrete PSO key");
    Check(pipelines::CalculateConcretePipelineKey(pipeline, nullptr, alternateKey) == pipelines::Result::InvalidArgument,
          "deferred attachment signature is explicit at materialization");
    const pipelines::Result compatibility = pipelines::ValidateShaderCompatibility(pipeline, shaderFile, &attachments);
    if (compatibility != pipelines::Result::Success)
    {
        std::fprintf(stderr, "[pipelinesTests] compatibility result: %s\n", pipelines::ToString(compatibility));
    }
    Check(compatibility == pipelines::Result::Success, "shader reflection is compatible with pipeline and attachments");
    alternateAttachments.colors[0].numericClass = shaders::NumericClass::UnsignedInteger;
    Check(pipelines::ValidateShaderCompatibility(pipeline, shaderFile, &alternateAttachments) == pipelines::Result::IncompatiblePipeline,
          "shader output numeric class mismatch is rejected");

    {
        GraphicsFixture exact(shaderFile);
        exact.description.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Exact;
        exact.description.graphics.exactAttachments = attachments;
        ByteArray bytes(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(exact.description, bytes) == pipelines::Result::Success, "write exact-attachment graphics pipeline");
        filesystem::MemoryFileReader reader(bytes, 0);
        pipelines::PipelineFile exactPipeline;
        Check(exactPipeline.Open(reader) == pipelines::Result::Success, "open exact-attachment pipeline");
        const pipelines::AttachmentSignature mismatch = MakeAttachments(1003);
        Check(pipelines::CalculateConcretePipelineKey(exactPipeline, &mismatch, alternateKey) == pipelines::Result::AttachmentMismatch,
              "exact attachment mismatch is rejected");
    }
    {
        GraphicsFixture duplicate(shaderFile);
        duplicate.streams[0].binding = duplicate.streams[1].binding;
        ByteArray rejected(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(duplicate.description, rejected) == pipelines::Result::DuplicateVertexStream,
              "duplicate vertex stream rejection");
    }
    {
        ByteArray corrupt(first);
        if (corrupt.Size() > 64)
        {
            corrupt[64] ^= 1u;
        }
        filesystem::MemoryFileReader reader(corrupt, 0);
        pipelines::PipelineFile rejected;
        Check(corrupt.Size() > 64 && rejected.Open(reader) == pipelines::Result::IntegrityFailure,
              "checksummed pipeline corruption rejection");
    }
    {
        std::array<pipelines::ShaderReference, 1> shader{{fixture.shader[0]}};
        pipelines::BuildDescription compute;
        compute.kind = pipelines::PipelineKind::Compute;
        compute.name = 0xa0001;
        compute.dynamicStates = pipelines::DynamicState::None;
        compute.shaders = {shader.data(), static_cast<vanguard::u32>(shader.size())};
        ByteArray bytes(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(compute, bytes) == pipelines::Result::Success, "compute pipeline descriptor round trip input");
        filesystem::MemoryFileReader reader(bytes, 0);
        pipelines::PipelineFile computePipeline;
        Check(computePipeline.Open(reader) == pipelines::Result::Success &&
                  pipelines::CalculateConcretePipelineKey(computePipeline, nullptr, firstKey) == pipelines::Result::Success,
              "compute pipeline needs no attachment specialization");
    }
    {
        std::array<pipelines::ShaderReference, 1> libraries{{fixture.shader[0]}};
        std::array<pipelines::RayTracingGroup, 2> groups{{{0xb0002, pipelines::RayTracingGroupKind::TrianglesHitGroup, 0, 0, 0x44, 0, 0},
                                                          {0xb0001, pipelines::RayTracingGroupKind::General, 0, 0x33, 0, 0, 0}}};
        pipelines::BuildDescription rayTracing;
        rayTracing.kind = pipelines::PipelineKind::RayTracing;
        rayTracing.name = 0xb1000;
        rayTracing.dynamicStates = pipelines::DynamicState::None;
        rayTracing.shaders = {libraries.data(), static_cast<vanguard::u32>(libraries.size())};
        rayTracing.rayTracing.maximumRecursionDepth = 2;
        rayTracing.rayTracing.maximumPayloadBytes = 32;
        rayTracing.rayTracing.maximumAttributeBytes = 8;
        rayTracing.rayTracingGroups = {groups.data(), static_cast<vanguard::u32>(groups.size())};
        ByteArray bytes(memory::pools::Rendering::GetInstance());
        Check(WriteFixture(rayTracing, bytes) == pipelines::Result::Success, "data-driven ray-tracing pipeline");
        filesystem::MemoryFileReader reader(bytes, 0);
        pipelines::PipelineFile rayTracingPipeline;
        Check(rayTracingPipeline.Open(reader) == pipelines::Result::Success && rayTracingPipeline.RayTracingGroups().Size() == 2 &&
                  rayTracingPipeline.RayTracingGroups()[0].name == 0xb0001,
              "ray-tracing groups are canonical data");
    }

    pipeline.Close();
    shaderFile.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[pipelinesTests] Vanguard vpipeline conformance checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
