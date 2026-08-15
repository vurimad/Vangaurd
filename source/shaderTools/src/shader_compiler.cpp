#include <vanguard/shader_tools/shader_compiler.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>

#include <slang-com-ptr.h>
#include <slang.h>

namespace
{
    using namespace vanguard;
    namespace shader = vanguard::shaders;
    namespace tools = vanguard::shader_tools;

    constexpr u32 MaximumEntryPoints = 32;
    constexpr u64 MaximumCompiledBytes = 512ull * 1024ull * 1024ull;

    [[nodiscard]] u32 BoundedLength(const char* const value, const u32 capacity) noexcept
    {
        if (value == nullptr) return capacity;
        u32 length = 0;
        while (length < capacity && value[length] != '\0') ++length;
        return length;
    }

    template <usize Capacity> [[nodiscard]] bool CopyString(char (&destination)[Capacity], const char* const source) noexcept
    {
        const u32 length = BoundedLength(source, static_cast<u32>(Capacity));
        if (length == 0 || length >= Capacity) return false;
        for (u32 index = 0; index < length; ++index) destination[index] = source[index];
        destination[length] = '\0';
        return true;
    }

    [[nodiscard]] u64 HashName(const char* value) noexcept
    {
        u64 hash = 14695981039346656037ull;
        if (value == nullptr) return 0;
        while (*value != '\0')
        {
            hash ^= static_cast<u8>(*value++);
            hash *= 1099511628211ull;
        }
        return hash != 0 ? hash : 1;
    }

    [[nodiscard]] SlangStage ConvertStage(const shader::ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case shader::ShaderStage::Vertex: return SLANG_STAGE_VERTEX;
        case shader::ShaderStage::Hull: return SLANG_STAGE_HULL;
        case shader::ShaderStage::Domain: return SLANG_STAGE_DOMAIN;
        case shader::ShaderStage::Geometry: return SLANG_STAGE_GEOMETRY;
        case shader::ShaderStage::Fragment: return SLANG_STAGE_FRAGMENT;
        case shader::ShaderStage::Compute: return SLANG_STAGE_COMPUTE;
        case shader::ShaderStage::Task: return SLANG_STAGE_AMPLIFICATION;
        case shader::ShaderStage::Mesh: return SLANG_STAGE_MESH;
        default: return SLANG_STAGE_NONE;
        }
    }

    [[nodiscard]] SlangOptimizationLevel ConvertOptimization(const tools::Optimization optimization) noexcept
    {
        switch (optimization)
        {
        case tools::Optimization::None: return SLANG_OPTIMIZATION_LEVEL_NONE;
        case tools::Optimization::Default: return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
        case tools::Optimization::High: return SLANG_OPTIMIZATION_LEVEL_HIGH;
        case tools::Optimization::Maximum: return SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
        }
        return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
    }

    [[nodiscard]] SlangDebugInfoLevel ConvertDebugInformation(const tools::DebugInformation information) noexcept
    {
        switch (information)
        {
        case tools::DebugInformation::None: return SLANG_DEBUG_INFO_LEVEL_NONE;
        case tools::DebugInformation::Minimal: return SLANG_DEBUG_INFO_LEVEL_MINIMAL;
        case tools::DebugInformation::Standard: return SLANG_DEBUG_INFO_LEVEL_STANDARD;
        case tools::DebugInformation::Maximum: return SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
        }
        return SLANG_DEBUG_INFO_LEVEL_NONE;
    }

}

namespace vanguard::shader_tools
{
    struct ShaderCompiler::Impl final
    {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        concurrency::Mutex frontEndLock;
        crypto::Digest256 compilerFingerprint;
        char compilerVersion[64]{};
    };

    CompileOutput::CompileOutput() noexcept
        : m_stages(memory::pools::Tools::GetInstance()), m_bytecode(memory::pools::Tools::GetInstance()),
          m_dependencies(memory::pools::Tools::GetInstance()), m_diagnostics(memory::pools::Tools::GetInstance())
    {
    }

    void CompileOutput::Reset() noexcept
    {
        m_stages.Clear();
        m_bytecode.Clear();
        m_dependencies.Clear();
        m_diagnostics.Clear();
        for (char& value : m_compilerVersion) value = '\0';
        m_compilerFingerprint = {};
    }

    containers::ArraySpan<const CompiledStage> CompileOutput::Stages() const noexcept { return m_stages; }
    containers::ArraySpan<const u8> CompileOutput::Bytecode() const noexcept { return m_bytecode; }
    containers::ArraySpan<const SourceDependency> CompileOutput::Dependencies() const noexcept { return m_dependencies; }
    const char* CompileOutput::Diagnostics() const noexcept
    {
        return m_diagnostics.Empty() ? "" : static_cast<const char*>(m_diagnostics.Data());
    }
    const char* CompileOutput::CompilerVersion() const noexcept { return m_compilerVersion; }
    const crypto::Digest256& CompileOutput::CompilerFingerprint() const noexcept { return m_compilerFingerprint; }

    void CompileOutput::AppendDiagnosticBytes(const void* const data, const usize size) noexcept
    {
        if (data == nullptr || size == 0) return;
        const u8* const bytes = static_cast<const u8*>(data);
        const usize available = MaximumDiagnosticBytes > m_diagnostics.Size() ? MaximumDiagnosticBytes - m_diagnostics.Size() : 0;
        const usize count = size < available ? size : available;
        m_diagnostics.Reserve(m_diagnostics.Size() + static_cast<u32>(count) + 2u);
        for (usize index = 0; index < count; ++index)
            if (bytes[index] != 0) m_diagnostics.PushBack(static_cast<char>(bytes[index]));
        if (!m_diagnostics.Empty() && m_diagnostics[m_diagnostics.Size() - 1u] != '\n') m_diagnostics.PushBack('\n');
    }

    void CompileOutput::TerminateDiagnostics() noexcept
    {
        if (m_diagnostics.Empty() || m_diagnostics[m_diagnostics.Size() - 1u] != '\0') m_diagnostics.PushBack('\0');
    }

    bool CompileRequest::IsValid() const noexcept
    {
        if (BoundedLength(sourceName, MaximumSourceNameLength) >= MaximumSourceNameLength ||
            BoundedLength(moduleName, MaximumModuleNameLength) >= MaximumModuleNameLength || source.Empty() ||
            source.Data() == nullptr || entryPoints.Empty() || entryPoints.Size() > MaximumEntryPoints ||
            settings.target > Target::VulkanSpirV || settings.optimization > Optimization::Maximum ||
            settings.debugInformation > DebugInformation::Maximum || BoundedLength(settings.profile, 64) >= 64 ||
            (searchPathCount != 0 && searchPaths == nullptr))
            return false;
        for (u32 index = 0; index < source.Size(); ++index)
            if (source[index] == 0) return false;
        shader::StageMask stages = 0;
        for (const EntryPoint& entry : entryPoints)
        {
            if (BoundedLength(entry.name, shader::MaximumEntryPointLength) >= shader::MaximumEntryPointLength ||
                ConvertStage(entry.stage) == SLANG_STAGE_NONE || (stages & shader::StageBit(entry.stage)) != 0)
                return false;
            stages |= shader::StageBit(entry.stage);
        }
        for (const Define& define : defines)
            if (BoundedLength(define.name, 256) >= 256 || (define.value != nullptr && BoundedLength(define.value, 1024) >= 1024))
                return false;
        return true;
    }

    ShaderCompiler::~ShaderCompiler() { Shutdown(); }

    Result ShaderCompiler::Initialize() noexcept
    {
        if (m_impl != nullptr) return Result::InvalidState;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr) return Result::CompilerUnavailable;
        const SlangResult result = slang::createGlobalSession(implementation->globalSession.writeRef());
        if (SLANG_FAILED(result) || !implementation->globalSession)
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        const char* const version = implementation->globalSession->getBuildTagString();
        if (!CopyString(implementation->compilerVersion, version))
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        crypto::Sha256Builder fingerprint;
        constexpr char Policy[] = "VanguardShaderCompiler;Slang;Language2026;RowMajor;ExplicitEntryPoints;1";
        if (!fingerprint.Update(implementation->compilerVersion, BoundedLength(implementation->compilerVersion, 64)) ||
            !fingerprint.Update(Policy, sizeof(Policy) - 1u) || !fingerprint.Finalize(implementation->compilerFingerprint))
        {
            VANGUARD_DELETE(implementation);
            return Result::CompilerUnavailable;
        }
        m_impl = implementation;
        return Result::Success;
    }

    void ShaderCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr) return;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
    }

    bool ShaderCompiler::IsInitialized() const noexcept { return m_impl != nullptr; }

    Result ShaderCompiler::Compile(const CompileRequest& request, CompileOutput& output) noexcept
    {
        output.Reset();
        if (m_impl == nullptr) return Result::InvalidState;
        if (!request.IsValid()) return Result::InvalidArgument;

        concurrency::ScopedLock lock(m_impl->frontEndLock);
        const SlangCompileTarget targetFormat = request.settings.target == Target::D3D12Dxil ? SLANG_DXIL : SLANG_SPIRV;
        if (SLANG_FAILED(m_impl->globalSession->checkCompileTargetSupport(targetFormat))) return Result::UnsupportedTarget;

        slang::CompilerOptionEntry targetOptions[2]{};
        targetOptions[0].name = slang::CompilerOptionName::Optimization;
        targetOptions[0].value.intValue0 = static_cast<i32>(ConvertOptimization(request.settings.optimization));
        targetOptions[1].name = slang::CompilerOptionName::DebugInformation;
        targetOptions[1].value.intValue0 = static_cast<i32>(ConvertDebugInformation(request.settings.debugInformation));

        slang::TargetDesc target;
        target.format = targetFormat;
        target.profile = m_impl->globalSession->findProfile(request.settings.profile);
        target.floatingPointMode = request.settings.preciseFloatingPoint ? SLANG_FLOATING_POINT_MODE_PRECISE
                                                                        : SLANG_FLOATING_POINT_MODE_FAST;
        target.compilerOptionEntries = targetOptions;
        target.compilerOptionEntryCount = 2;
        if (target.profile == SLANG_PROFILE_UNKNOWN) return Result::UnsupportedTarget;

        containers::DynamicArray<slang::PreprocessorMacroDesc> macros(memory::pools::Tools::GetInstance());
        macros.Reserve(request.defines.Size());
        for (const Define& define : request.defines) macros.PushBack({define.name, define.value != nullptr ? define.value : "1"});

        slang::CompilerOptionEntry sessionOptions[3]{};
        sessionOptions[0].name = slang::CompilerOptionName::LanguageVersion;
        sessionOptions[0].value.intValue0 = SLANG_LANGUAGE_VERSION_2026;
        sessionOptions[1].name = slang::CompilerOptionName::DiagnosticColor;
        sessionOptions[1].value.intValue0 = SLANG_DIAGNOSTIC_COLOR_NEVER;
        sessionOptions[2].name = slang::CompilerOptionName::WarningsAsErrors;
        sessionOptions[2].value.kind = slang::CompilerOptionValueKind::String;
        sessionOptions[2].value.stringValue0 = request.settings.warningsAsErrors ? "all" : "none";

        slang::SessionDesc sessionDescription;
        sessionDescription.targets = &target;
        sessionDescription.targetCount = 1;
        sessionDescription.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        sessionDescription.searchPaths = request.searchPaths;
        sessionDescription.searchPathCount = request.searchPathCount;
        sessionDescription.preprocessorMacros = macros.Empty() ? nullptr : static_cast<const slang::PreprocessorMacroDesc*>(macros.Data());
        sessionDescription.preprocessorMacroCount = macros.Size();
        sessionDescription.compilerOptionEntries = sessionOptions;
        sessionDescription.compilerOptionEntryCount = request.settings.warningsAsErrors ? 3u : 2u;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(m_impl->globalSession->createSession(sessionDescription, session.writeRef())) || !session)
            return Result::SessionCreationFailure;

        containers::DynamicArray<char> source(memory::pools::Tools::GetInstance());
        source.Reserve(request.source.Size() + 1u);
        for (const u8 value : request.source) source.PushBack(static_cast<char>(value));
        source.PushBack('\0');

        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModuleFromSourceString(request.moduleName, request.sourceName,
                                                     static_cast<const char*>(source.Data()), diagnostics.writeRef());
        if (diagnostics) output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (!module)
        {
            output.TerminateDiagnostics();
            return Result::SourceFailure;
        }

        const SlangInt32 dependencyCount = module->getDependencyFileCount();
        output.m_dependencies.Reserve(dependencyCount > 0 ? static_cast<u32>(dependencyCount) : 0u);
        for (SlangInt32 index = 0; index < dependencyCount; ++index)
        {
            SourceDependency dependency;
            if (!CopyString(dependency.path, module->getDependencyFilePath(index)))
            {
                output.TerminateDiagnostics();
                return Result::LimitExceeded;
            }
            output.m_dependencies.PushBack(dependency);
        }

        Slang::ComPtr<slang::IEntryPoint> entryPoints[MaximumEntryPoints];
        slang::IComponentType* components[MaximumEntryPoints + 1u]{};
        components[0] = module.get();
        for (u32 index = 0; index < request.entryPoints.Size(); ++index)
        {
            diagnostics.setNull();
            const EntryPoint& entry = request.entryPoints[index];
            const SlangResult entryResult = module->findAndCheckEntryPoint(entry.name, ConvertStage(entry.stage),
                                                                           entryPoints[index].writeRef(), diagnostics.writeRef());
            if (diagnostics) output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
            if (SLANG_FAILED(entryResult) || !entryPoints[index])
            {
                output.TerminateDiagnostics();
                return Result::EntryPointFailure;
            }
            components[index + 1u] = entryPoints[index].get();
        }

        Slang::ComPtr<slang::IComponentType> composed;
        diagnostics.setNull();
        SlangResult slangResult = session->createCompositeComponentType(components, request.entryPoints.Size() + 1u,
                                                                         composed.writeRef(), diagnostics.writeRef());
        if (diagnostics) output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (SLANG_FAILED(slangResult) || !composed)
        {
            output.TerminateDiagnostics();
            return Result::LinkFailure;
        }

        Slang::ComPtr<slang::IComponentType> linked;
        diagnostics.setNull();
        slangResult = composed->link(linked.writeRef(), diagnostics.writeRef());
        if (diagnostics) output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
        if (SLANG_FAILED(slangResult) || !linked)
        {
            output.TerminateDiagnostics();
            return Result::LinkFailure;
        }

        output.m_stages.Reserve(request.entryPoints.Size());
        for (u32 index = 0; index < request.entryPoints.Size(); ++index)
        {
            Slang::ComPtr<slang::IBlob> code;
            diagnostics.setNull();
            slangResult = linked->getEntryPointCode(index, 0, code.writeRef(), diagnostics.writeRef());
            if (diagnostics) output.AppendDiagnosticBytes(diagnostics->getBufferPointer(), diagnostics->getBufferSize());
            if (SLANG_FAILED(slangResult) || !code || code->getBufferPointer() == nullptr || code->getBufferSize() == 0 ||
                code->getBufferSize() > MaximumCompiledBytes || output.m_bytecode.Size() > MaximumCompiledBytes - code->getBufferSize())
            {
                output.TerminateDiagnostics();
                return Result::CodeGenerationFailure;
            }

            CompiledStage stage;
            stage.stage = request.entryPoints[index].stage;
            stage.format = request.settings.target == Target::D3D12Dxil ? shader::NativeFormat::Dxil : shader::NativeFormat::SpirV;
            stage.entryPoint = HashName(request.entryPoints[index].name);
            if (!CopyString(stage.entryPointName, request.entryPoints[index].name))
            {
                output.TerminateDiagnostics();
                return Result::LimitExceeded;
            }
            stage.bytecodeOffset = output.m_bytecode.Size();
            stage.bytecodeSize = code->getBufferSize();
            stage.bytecodeDigest = crypto::Sha256(code->getBufferPointer(), code->getBufferSize());
            const u8* const codeBytes = static_cast<const u8*>(code->getBufferPointer());
            output.m_bytecode.Reserve(output.m_bytecode.Size() + static_cast<u32>(code->getBufferSize()));
            for (usize byteIndex = 0; byteIndex < code->getBufferSize(); ++byteIndex) output.m_bytecode.PushBack(codeBytes[byteIndex]);
            output.m_stages.PushBack(stage);
        }

        if (!CopyString(output.m_compilerVersion, m_impl->compilerVersion))
        {
            output.TerminateDiagnostics();
            return Result::CompilerUnavailable;
        }
        output.m_compilerFingerprint = m_impl->compilerFingerprint;
        output.TerminateDiagnostics();
        return Result::Success;
    }

    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success: return "Success";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::InvalidState: return "InvalidState";
        case Result::CompilerUnavailable: return "CompilerUnavailable";
        case Result::UnsupportedTarget: return "UnsupportedTarget";
        case Result::SessionCreationFailure: return "SessionCreationFailure";
        case Result::SourceFailure: return "SourceFailure";
        case Result::EntryPointFailure: return "EntryPointFailure";
        case Result::LinkFailure: return "LinkFailure";
        case Result::CodeGenerationFailure: return "CodeGenerationFailure";
        case Result::LimitExceeded: return "LimitExceeded";
        case Result::WriteFailure: return "WriteFailure";
        }
        return "Unknown";
    }
} // namespace vanguard::shader_tools
