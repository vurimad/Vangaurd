#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>

#include <cstdio>

namespace
{
    using namespace vanguard;
    namespace sht = vanguard::shader_tools;

    u32 failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        ++failures;
        std::fprintf(stderr, "[shaderToolsTests] failed: %s\n", message);
    }
}

int main()
{
    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "container initialization");

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
        std::fprintf(stderr, "[shaderToolsTests] compile result %s\n%s", sht::ToString(result), output.Diagnostics());
    Check(result == sht::Result::Success, "SPIR-V compilation");
    Check(output.Stages().Size() == 1, "one compiled stage");
    Check(!output.Bytecode().Empty(), "compiled bytecode exists");
    Check(output.Stages().Empty() || output.Stages()[0].format == shaders::NativeFormat::SpirV, "SPIR-V format identity");
    Check(output.Stages().Empty() || !output.Stages()[0].bytecodeDigest.IsEmpty(), "bytecode digest");
    Check(output.CompilerVersion()[0] != '\0', "compiler version captured");
    Check(!output.CompilerFingerprint().IsEmpty(), "compiler fingerprint captured");

    request.settings.target = sht::Target::D3D12Dxil;
    const sht::Result dxilResult = compiler.Compile(request, output);
    if (dxilResult != sht::Result::Success)
        std::fprintf(stderr, "[shaderToolsTests] DXIL compile result %s\n%s", sht::ToString(dxilResult), output.Diagnostics());
    Check(dxilResult == sht::Result::Success, "DXIL compilation");
    Check(output.Stages().Size() == 1, "one DXIL stage");
    Check(!output.Bytecode().Empty(), "DXIL bytecode exists");
    Check(output.Stages().Empty() || output.Stages()[0].format == shaders::NativeFormat::Dxil, "DXIL format identity");

    sht::CompileRequest invalid = request;
    invalid.entryPoints = {};
    Check(compiler.Compile(invalid, output) == sht::Result::InvalidArgument, "empty entry point set rejected");

    compiler.Shutdown();
    if (failures == 0) std::puts("[shaderToolsTests] offline compiler checks passed");
    return failures == 0 ? 0 : 1;
}
