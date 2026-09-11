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
        ReflectionFailure,
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

    struct LoadedSource
    {
        containers::ArraySpan<const u8> content;
    };

    using LoadSourceFunction = bool (*)(const char* canonicalPath, LoadedSource& source, void* userData) noexcept;

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
        LoadSourceFunction loadSource = nullptr;
        void* loadSourceUserData = nullptr;
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
        [[nodiscard]] containers::ArraySpan<const CompiledStage> GetStages() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetBytecode() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceDependency> GetDependencies() const noexcept;
        [[nodiscard]] const char* GetDiagnostics() const noexcept;
        [[nodiscard]] const char* CompilerVersion() const noexcept;
        [[nodiscard]] const crypto::Digest256& CompilerFingerprint() const noexcept;
        [[nodiscard]] const shaders::PipelineInterface& GetInterface() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::DescriptorBinding> Bindings() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::ConstantBuffer> GetConstantBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::ConstantMember> GetConstantMembers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::VertexInput> GetVertexInputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::FragmentOutput> GetFragmentOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::SpecializationConstant> GetSpecializationConstants() const noexcept;
        [[nodiscard]] bool HasMaterialContract() const noexcept;
        [[nodiscard]] const shaders::MaterialDomainContract& GetMaterialDomain() const noexcept;
        [[nodiscard]] u32 GetMaterialAccessorAbiVersion() const noexcept;
        [[nodiscard]] u32 GetMaterialParameterByteSize() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::ConstantMember> GetMaterialParameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const shaders::MaterialResourceRole> GetMaterialResources() const noexcept;

        /// Emits the compiled program as a complete platform-specific .vshader document.
        [[nodiscard]] Result WriteShader(filesystem::IFile& output, u64 program, const crypto::Digest256& permutation = {}) const noexcept;

    private:
        void AppendDiagnosticBytes(const void* bytes, usize size) noexcept;
        void TerminateDiagnostics() noexcept;

        containers::DynamicArray<CompiledStage> m_stages;
        containers::DynamicArray<u8> m_bytecode;
        containers::DynamicArray<SourceDependency> m_dependencies;
        containers::DynamicArray<char> m_diagnostics;
        char m_compilerVersion[64]{};
        crypto::Digest256 m_compilerFingerprint;
        shaders::PipelineInterface m_interface;
        containers::DynamicArray<shaders::DescriptorBinding> m_bindings;
        containers::DynamicArray<shaders::ConstantBuffer> m_constantBuffers;
        containers::DynamicArray<shaders::ConstantMember> m_constantMembers;
        containers::DynamicArray<shaders::VertexInput> m_vertexInputs;
        containers::DynamicArray<shaders::FragmentOutput> m_fragmentOutputs;
        containers::DynamicArray<shaders::SpecializationConstant> m_specializationConstants;
        bool m_hasMaterialContract = false;
        shaders::MaterialDomainContract m_materialDomain;
        u32 m_materialAccessorAbiVersion = 0;
        u32 m_materialParameterByteSize = 0;
        containers::DynamicArray<shaders::ConstantMember> m_materialParameters;
        containers::DynamicArray<shaders::MaterialResourceRole> m_materialResources;

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
        [[nodiscard]] const crypto::Digest256& CompilerFingerprint() const noexcept;
        /// Performs parsing, linking, dependency collection, and reflection without
        /// requesting native target bytecode from Slang.
        [[nodiscard]] Result Reflect(const CompileRequest& request, CompileOutput& output) noexcept;
        [[nodiscard]] Result Compile(const CompileRequest& request, CompileOutput& output) noexcept;

    private:
        [[nodiscard]] Result Process(const CompileRequest& request, CompileOutput& output, bool generateNativeCode) noexcept;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::shader_tools
