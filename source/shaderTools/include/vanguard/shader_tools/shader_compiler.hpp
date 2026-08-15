#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/shaders/shaders.hpp>

namespace vanguard::shader_tools
{
    inline constexpr u32 MaximumSourceNameLength = 1024;
    inline constexpr u32 MaximumModuleNameLength = 128;
    inline constexpr u32 MaximumDiagnosticBytes = 1024u * 1024u;

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        CompilerUnavailable,
        UnsupportedTarget,
        SessionCreationFailure,
        SourceFailure,
        EntryPointFailure,
        LinkFailure,
        CodeGenerationFailure,
        LimitExceeded,
        WriteFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class Target : u8
    {
        D3D12Dxil,
        VulkanSpirV
    };

    enum class Optimization : u8
    {
        None,
        Default,
        High,
        Maximum
    };

    enum class DebugInformation : u8
    {
        None,
        Minimal,
        Standard,
        Maximum
    };

    struct EntryPoint
    {
        const char* name = nullptr;
        shaders::ShaderStage stage = shaders::ShaderStage::Count;
    };

    struct Define
    {
        const char* name = nullptr;
        const char* value = nullptr;
    };

    struct CompileSettings
    {
        Target target = Target::D3D12Dxil;
        Optimization optimization = Optimization::High;
        DebugInformation debugInformation = DebugInformation::None;
        const char* profile = "sm_6_6";
        bool warningsAsErrors = true;
        bool preciseFloatingPoint = false;
    };

    struct CompileRequest
    {
        const char* sourceName = nullptr;
        const char* moduleName = nullptr;
        containers::ArraySpan<const u8> source;
        containers::ArraySpan<const EntryPoint> entryPoints;
        const char* const* searchPaths = nullptr;
        u32 searchPathCount = 0;
        containers::ArraySpan<const Define> defines;
        CompileSettings settings;

        [[nodiscard]] bool IsValid() const noexcept;
    };

    struct CompiledStage
    {
        shaders::ShaderStage stage = shaders::ShaderStage::Count;
        shaders::NativeFormat format = shaders::NativeFormat::Dxil;
        u64 entryPoint = 0;
        char entryPointName[shaders::MaximumEntryPointLength]{};
        u64 bytecodeOffset = 0;
        u64 bytecodeSize = 0;
        crypto::Digest256 bytecodeDigest;
    };

    struct SourceDependency
    {
        char path[MaximumSourceNameLength]{};
    };

    class CompileOutput final
    {
    public:
        CompileOutput() noexcept;

        void Reset() noexcept;
        [[nodiscard]] containers::ArraySpan<const CompiledStage> Stages() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> Bytecode() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceDependency> Dependencies() const noexcept;
        [[nodiscard]] const char* Diagnostics() const noexcept;
        [[nodiscard]] const char* CompilerVersion() const noexcept;
        [[nodiscard]] const crypto::Digest256& CompilerFingerprint() const noexcept;

    private:
        void AppendDiagnosticBytes(const void* bytes, usize size) noexcept;
        void TerminateDiagnostics() noexcept;

        containers::DynamicArray<CompiledStage> m_stages;
        containers::DynamicArray<u8> m_bytecode;
        containers::DynamicArray<SourceDependency> m_dependencies;
        containers::DynamicArray<char> m_diagnostics;
        char m_compilerVersion[64]{};
        crypto::Digest256 m_compilerFingerprint;

        friend class ShaderCompiler;
    };

    class ShaderCompiler final
    {
    public:
        struct Impl;

        ShaderCompiler() noexcept = default;
        ~ShaderCompiler();

        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        [[nodiscard]] Result Initialize() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] Result Compile(const CompileRequest& request, CompileOutput& output) noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::shader_tools
