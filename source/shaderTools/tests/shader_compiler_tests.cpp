#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>
#include <vanguard/shader_tools/shader_asset_compiler.hpp>

#include <cstdio>

namespace
{
    using namespace vanguard;
    namespace sht = vanguard::shader_tools;

    u32 failures = 0;

    struct SourceProviderState
    {
        const char* includeText = nullptr;
    };

    bool EqualText(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return false;
        while (*left != '\0' && *right != '\0')
            if (*left++ != *right++)
                return false;
        return *left == *right;
    }

    bool LoadShaderSource(const char* path, sht::ShaderSource& source, void* userData) noexcept
    {
        SourceProviderState& state = *static_cast<SourceProviderState*>(userData);
        if (!EqualText(path, "shaders/common/constants.slang") || state.includeText == nullptr)
            return false;
        u32 size = 0;
        while (state.includeText[size] != '\0')
            ++size;
        source.canonicalPath = "shaders/common/constants.slang";
        source.identity = resources::ResourceReference(resources::ResourcePath::FromString(source.canonicalPath), sht::ShaderSourceResourceType);
        source.content = {reinterpret_cast<const u8*>(state.includeText), size};
        return true;
    }

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        ++failures;
        std::fprintf(stderr, "[shaderToolsTests] failed: %s\n", message);
    }
} // namespace

int main()
{
    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "shaderToolsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "container initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");

    sht::ShaderCompiler compiler;
    Check(compiler.Initialize() == sht::Result::Success, "Slang compiler initialization");

    constexpr char Source[] = R"(
        [shader("compute")]
        [numthreads(8, 4, 1)]
        void BuildCandidates(uint3 dispatchThreadId : SV_DispatchThreadID)
        {
            uint value = dispatchThreadId.x + dispatchThreadId.y;
            if (value == 0xffffffffu) return;
        }
    )";
    const sht::EntryPoint entries[]{{"BuildCandidates", shaders::ShaderStage::Compute}};
    sht::CompileRequest request;
    request.sourceName = "tests/build_candidates.slang";
    request.moduleName = "build_candidates";
    request.source = {reinterpret_cast<const u8*>(Source), static_cast<u32>(sizeof(Source) - 1u)};
    request.entryPoints = entries;
    request.settings.target = sht::Target::VulkanSpirV;
    request.settings.optimization = sht::Optimization::High;

    sht::CompileOutput output;
    const sht::Result result = compiler.Compile(request, output);
    if (result != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] compile result %s\n%s", sht::ToString(result), output.GetDiagnostics());
    Check(result == sht::Result::Success, "SPIR-V compilation");
    Check(output.GetStages().Size() == 1, "one compiled stage");
    Check(!output.GetBytecode().Empty(), "compiled bytecode exists");
    Check(output.GetStages().Empty() || output.GetStages()[0].format == shaders::NativeFormat::SpirV, "SPIR-V format identity");
    Check(output.GetStages().Empty() || !output.GetStages()[0].bytecodeDigest.IsEmpty(), "bytecode digest");
    Check(output.CompilerVersion()[0] != '\0', "compiler version captured");
    Check(!output.CompilerFingerprint().IsEmpty(), "compiler fingerprint captured");

    request.settings.target = sht::Target::D3D12Dxil;
    const sht::Result dxilResult = compiler.Compile(request, output);
    if (dxilResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] DXIL compile result %s\n%s", sht::ToString(dxilResult), output.GetDiagnostics());
    Check(dxilResult == sht::Result::Success, "DXIL compilation");
    Check(output.GetStages().Size() == 1, "one DXIL stage");
    Check(!output.GetBytecode().Empty(), "DXIL bytecode exists");
    Check(output.GetStages().Empty() || output.GetStages()[0].format == shaders::NativeFormat::Dxil, "DXIL format identity");

    constexpr char ReflectionSource[] = R"(
        cbuffer FrameData : register(b0, space0)
        {
            float4x4 viewProjection;
            float3 cameraPosition;
            uint candidateCount;
            float4 clipPlanes[4];
        };
        Texture2D<float4> materialTextures[] : register(t0, space1);
        SamplerState materialSamplers[] : register(s0, space1);
        StructuredBuffer<float4> candidateBounds : register(t0, space2);
        RWStructuredBuffer<uint> visibleCandidates : register(u0, space2);
        [shader("compute")]
        [numthreads(8, 4, 1)]
        void BuildCandidates(uint3 dispatchThreadId : SV_DispatchThreadID)
        {
            if (dispatchThreadId.x >= candidateCount) return;
            const uint textureIndex = dispatchThreadId.x & 1u;
            const float4 value = materialTextures[textureIndex].SampleLevel(materialSamplers[textureIndex], float2(0.5), 0.0);
            visibleCandidates[dispatchThreadId.x] = candidateBounds[dispatchThreadId.x].x + value.x + cameraPosition.x > 0.0;
        }
    )";
    request.sourceName = "tests/reflection_fixture.slang";
    request.moduleName = "reflection_fixture";
    request.source = {reinterpret_cast<const u8*>(ReflectionSource), static_cast<u32>(sizeof(ReflectionSource) - 1u)};
    const sht::Result reflectionResult = compiler.Compile(request, output);
    if (reflectionResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] reflection compile result %s (bindings=%u buffers=%u members=%u)\n%s", sht::ToString(reflectionResult),
                     output.Bindings().Size(), output.GetConstantBuffers().Size(), output.GetConstantMembers().Size(), output.GetDiagnostics());
    Check(reflectionResult == sht::Result::Success, "reflected DXIL compilation");
    Check(output.GetInterface().stages == shaders::StageBit(shaders::ShaderStage::Compute), "reflected stage mask");
    Check(output.GetInterface().threadGroupSizeX == 8 && output.GetInterface().threadGroupSizeY == 4 && output.GetInterface().threadGroupSizeZ == 1,
          "reflected compute group size");
    Check(output.Bindings().Size() == 5, "all descriptor bindings reflected");
    Check(output.GetConstantBuffers().Size() == 1, "constant buffer reflected");
    Check(output.GetConstantMembers().Size() == 4, "constant members reflected");
    bool foundBindlessTexture = false;
    bool foundBindlessSampler = false;
    bool foundConstantArray = false;
    bool foundMatrixLayout = false;
    bool foundStructuredRead = false;
    bool foundStructuredWrite = false;
    for (const shaders::DescriptorBinding& binding : output.Bindings())
    {
        if (binding.kind == shaders::BindingKind::SampledTexture && binding.arrayCount == shaders::UnboundedDescriptorCount &&
            shaders::HasFlag(binding.flags, shaders::BindingFlags::Bindless))
            foundBindlessTexture = true;
        if (binding.kind == shaders::BindingKind::Sampler && binding.arrayCount == shaders::UnboundedDescriptorCount &&
            shaders::HasFlag(binding.flags, shaders::BindingFlags::Bindless))
            foundBindlessSampler = true;
        if (binding.kind == shaders::BindingKind::StructuredBuffer && binding.access == shaders::BindingAccess::Read)
            foundStructuredRead = true;
        if (binding.kind == shaders::BindingKind::ReadWriteStructuredBuffer && binding.access == shaders::BindingAccess::ReadWrite)
            foundStructuredWrite = true;
    }
    for (const shaders::ConstantMember& member : output.GetConstantMembers())
    {
        if (member.arrayStride != 0 && member.byteSize > member.arrayStride)
            foundConstantArray = true;
        if (member.rows == 4 && member.columns == 4)
        {
            if (member.rowMajor && member.matrixStride == 16)
                foundMatrixLayout = true;
            else
                std::fprintf(stderr, "[shaderToolsTests] matrix reflection rows=%u columns=%u rowMajor=%u stride=%u\n", member.rows, member.columns,
                             member.rowMajor ? 1u : 0u, member.matrixStride);
        }
    }
    Check(foundBindlessTexture && foundBindlessSampler, "unbounded arrays reflected as bindless capabilities");
    Check(foundStructuredRead && foundStructuredWrite, "structured buffer access reflected");
    Check(foundConstantArray, "constant array stride reflected");
    Check(foundMatrixLayout, "row-major matrix stride reflected");

    constexpr char BindlessComputeSource[] = R"(
        struct DispatchConstants { uint inputDescriptor; uint outputDescriptor; uint count; uint reserved; };
        [push_constant] ConstantBuffer<DispatchConstants> constants;
        [shader("compute")]
        [numthreads(128, 1, 1)]
        void BindlessCompute(uint3 threadId : SV_DispatchThreadID)
        {
            if (threadId.x >= constants.count) return;
            StructuredBuffer<uint> inputValues = ResourceDescriptorHeap[constants.inputDescriptor];
            RWStructuredBuffer<uint> outputValues = ResourceDescriptorHeap[constants.outputDescriptor];
            outputValues[threadId.x] = inputValues[threadId.x];
        }
    )";
    const sht::EntryPoint bindlessComputeEntry[]{{"BindlessCompute", shaders::ShaderStage::Compute}};
    request.sourceName = "tests/bindless_compute.slang";
    request.moduleName = "bindless_compute";
    request.source = {reinterpret_cast<const u8*>(BindlessComputeSource), static_cast<u32>(sizeof(BindlessComputeSource) - 1u)};
    request.entryPoints = bindlessComputeEntry;
    request.settings.target = sht::Target::D3D12Dxil;
    const sht::Result bindlessDxilResult = compiler.Compile(request, output);
    if (bindlessDxilResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] bindless DXIL result %s\n%s", sht::ToString(bindlessDxilResult), output.GetDiagnostics());
    Check(bindlessDxilResult == sht::Result::Success && output.GetInterface().threadGroupSizeX == 128, "bindless push-constant compute contract compiles to DXIL");
    request.settings.target = sht::Target::VulkanSpirV;
    const sht::Result bindlessSpirVResult = compiler.Compile(request, output);
    if (bindlessSpirVResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] bindless SPIR-V result %s\n%s", sht::ToString(bindlessSpirVResult), output.GetDiagnostics());
    Check(bindlessSpirVResult == sht::Result::Success && output.GetInterface().threadGroupSizeX == 128,
          "bindless push-constant compute contract compiles to SPIR-V");

    containers::DynamicArray<u8> cooked(memory::pools::Serialization::GetInstance());
    filesystem::MemoryFileWriter writer(cooked);
    Check(output.WriteShader(writer, 0x91f2a07du) == sht::Result::Success, "complete .vshader emission");
    filesystem::MemoryFileReader reader(cooked, 0);
    shaders::ShaderFile shaderFile;
    Check(shaderFile.Open(reader) == shaders::Result::Success, "emitted .vshader reopens");
    Check(shaderFile.IsOpen() && shaderFile.GetKind() == shaders::ProgramKind::Compute, "emitted program identity");
    Check(shaderFile.Bindings().Size() == output.Bindings().Size() && shaderFile.GetConstantMembers().Size() == output.GetConstantMembers().Size(),
          "emitted reflection survives serialization");
    Check(shaderFile.GetInterface().threadGroupSizeX == 128 && shaderFile.GetInterface().threadGroupSizeY == 1, "emitted compute interface survives serialization");

    constexpr char GraphicsSource[] = R"(
        struct VertexInput { float3 position : POSITION; float2 uv : TEXCOORD0; };
        struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
        [shader("vertex")]
        VertexOutput MainVertex(VertexInput input)
        {
            VertexOutput output;
            output.position = float4(input.position, 1.0);
            output.uv = input.uv;
            return output;
        }
        [shader("fragment")]
        float4 MainFragment(VertexOutput input) : SV_Target0
        {
            return float4(input.uv, 0.0, 1.0);
        }
    )";
    const sht::EntryPoint graphicsEntries[]{{"MainVertex", shaders::ShaderStage::Vertex}, {"MainFragment", shaders::ShaderStage::Fragment}};
    request.sourceName = "tests/graphics_reflection.slang";
    request.moduleName = "graphics_reflection";
    request.source = {reinterpret_cast<const u8*>(GraphicsSource), static_cast<u32>(sizeof(GraphicsSource) - 1u)};
    request.entryPoints = graphicsEntries;
    const sht::Result graphicsResult = compiler.Compile(request, output);
    if (graphicsResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] graphics reflection result %s\n%s", sht::ToString(graphicsResult), output.GetDiagnostics());
    Check(graphicsResult == sht::Result::Success, "graphics reflection compilation");
    Check(output.GetVertexInputs().Size() == 2, "vertex inputs reflected");
    Check(output.GetFragmentOutputs().Size() == 1 && output.GetInterface().renderTargetCount == 1, "fragment output interface reflected");
    containers::DynamicArray<u8> cookedGraphics(memory::pools::Serialization::GetInstance());
    filesystem::MemoryFileWriter graphicsWriter(cookedGraphics);
    Check(output.WriteShader(graphicsWriter, 0x4a79f611u) == sht::Result::Success, "graphics .vshader emission");
    filesystem::MemoryFileReader graphicsReader(cookedGraphics, 0);
    shaders::ShaderFile graphicsFile;
    Check(graphicsFile.Open(graphicsReader) == shaders::Result::Success && graphicsFile.GetKind() == shaders::ProgramKind::Graphics,
          "emitted graphics .vshader reopens");

    constexpr char AssetSource[] = R"(
        #include "../common/constants.slang"
        RWStructuredBuffer<uint> outputValues;
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void Main(uint3 threadId : SV_DispatchThreadID)
        {
            outputValues[threadId.x] = IncludedValue;
        }
    )";
    constexpr char IncludeV1[] = "static const uint IncludedValue = 17;";
    constexpr char IncludeV2[] = "static const uint IncludedValue = 29;";
    SourceProviderState provider{IncludeV1};
    sht::ShaderAssetCompiler assetCompiler;
    const sht::ShaderAssetCompilerConfig assetCompilerConfig{LoadShaderSource, &provider, 32, 8};
    Check(assetCompiler.Initialize(assetCompilerConfig), "shader asset compiler initialization");
    assets::BuildSystem buildSystem;
    assets::Config assetConfig;
    assetConfig.maximumCacheEntries = 8;
    assetConfig.maximumCacheBytes = 16u * 1024u * 1024u;
    Check(buildSystem.Initialize(assetConfig), "shader DDC initialization");
    Check(assetCompiler.Register(buildSystem) == assets::Result::Success, "shader compiler registration");
    const sht::EntryPoint assetEntries[]{{"Main", shaders::ShaderStage::Compute}};
    sht::ShaderBuildDescription assetDescription;
    assetDescription.sourceName = "shaders/tests/include_asset.slang";
    assetDescription.program = 0x1122334455667788ull;
    assetDescription.permutation = crypto::Sha256("shader-asset-permutation", 24);
    assetDescription.entryPoints = assetEntries;
    containers::DynamicArray<u8> assetSettings(memory::pools::Assets::GetInstance());
    Check(sht::EncodeShaderBuildSettings(assetDescription, assetSettings) == sht::BuildSettingsResult::Success, "canonical shader build settings");
    const resources::ResourceReference assetSourceReference(resources::ResourcePath::FromString("shaders/tests/include_asset.slang"),
                                                            sht::ShaderSourceResourceType);
    const resources::ResourceReference assetOutputReference(resources::ResourcePath::FromString("shaders/tests/include_asset.vshader"),
                                                            shaders::ShaderResourceType);
    const containers::ArraySpan<const u8> assetSource{reinterpret_cast<const u8*>(AssetSource), sizeof(AssetSource) - 1u};
    const assets::BuildRequest assetRequest{{assetSourceReference, assetSource, {}}, assetOutputReference, assets::TargetPlatform::WindowsD3D12, assetSettings};
    assets::BuildPlan assetPlan;
    Check(buildSystem.Prepare(assetRequest, assetPlan) == assets::Result::Success && assetPlan.GetDependencies().Size() == 2,
          "include and compiler prerequisites discovered before compilation");
    provider.includeText = IncludeV2;
    assets::BuildOutput staleDependencyBuild;
    Check(buildSystem.Execute(assetRequest, assetPlan, staleDependencyBuild) == assets::Result::CompileFailed && staleDependencyBuild.artifacts.Empty(),
          "changed include cannot publish under a stale prepared fingerprint");
    provider.includeText = IncludeV1;
    assets::BuildOutput firstAssetBuild;
    Check(buildSystem.Execute(assetRequest, assetPlan, firstAssetBuild) == assets::Result::Success &&
              firstAssetBuild.disposition == assets::BuildDisposition::Built && firstAssetBuild.artifacts.Size() == 1,
          "shader source compiles through asset build system");
    assets::BuildOutput cachedAssetBuild;
    Check(buildSystem.Build(assetRequest, cachedAssetBuild) == assets::Result::Success && cachedAssetBuild.disposition == assets::BuildDisposition::CacheHit &&
              cachedAssetBuild.buildFingerprint == firstAssetBuild.buildFingerprint,
          "unchanged shader resolves through DDC");
    if (!firstAssetBuild.artifacts.Empty())
    {
        filesystem::MemoryFileReader assetReader(firstAssetBuild.artifacts[0].bytes, 0);
        shaders::ShaderFile assetFile;
        Check(assetFile.Open(assetReader) == shaders::Result::Success && assetFile.GetProgram() == assetDescription.program &&
                  assetFile.GetPermutation() == assetDescription.permutation,
              "DDC shader artifact is runtime-loadable");
    }
    const assets::BuildRequest vulkanAssetRequest{
        {assetSourceReference, assetSource, {}}, assetOutputReference, assets::TargetPlatform::WindowsVulkan, assetSettings};
    assets::BuildOutput vulkanAssetBuild;
    Check(buildSystem.Build(vulkanAssetRequest, vulkanAssetBuild) == assets::Result::Success &&
              vulkanAssetBuild.disposition == assets::BuildDisposition::Built && vulkanAssetBuild.buildFingerprint != firstAssetBuild.buildFingerprint,
          "target platform owns a distinct shader DDC identity");
    if (!vulkanAssetBuild.artifacts.Empty())
    {
        filesystem::MemoryFileReader vulkanAssetReader(vulkanAssetBuild.artifacts[0].bytes, 0);
        shaders::ShaderFile vulkanAssetFile;
        Check(vulkanAssetFile.Open(vulkanAssetReader) == shaders::Result::Success && vulkanAssetFile.GetStages().Size() == 1 &&
                  vulkanAssetFile.GetStages()[0].format == shaders::NativeFormat::SpirV,
              "Vulkan shader build emits SPIR-V vshader payload");
    }
    provider.includeText = IncludeV2;
    assets::BuildOutput changedIncludeBuild;
    Check(buildSystem.Build(assetRequest, changedIncludeBuild) == assets::Result::Success &&
              changedIncludeBuild.disposition == assets::BuildDisposition::Built && changedIncludeBuild.buildFingerprint != firstAssetBuild.buildFingerprint,
          "include content change invalidates shader DDC key");
    Check(assetCompiler.Unregister() == assets::Result::Success, "shader compiler unregistration");
    Check(assetCompiler.Shutdown(), "shader asset compiler shutdown");
    Check(buildSystem.Shutdown(), "shader DDC shutdown");

    sht::CompileRequest invalid = request;
    invalid.entryPoints = {};
    Check(compiler.Compile(invalid, output) == sht::Result::InvalidArgument, "empty entry point set rejected");

    compiler.Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    if (failures == 0)
        std::puts("[shaderToolsTests] offline compiler checks passed");
    return failures == 0 ? 0 : 1;
}
