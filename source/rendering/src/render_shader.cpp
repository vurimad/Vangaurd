#include <vanguard/rendering/render_shader.hpp>

namespace vanguard::rendering
{

    namespace
    {
        [[nodiscard]] bool IsNativeFormatCompatible(const shaders::NativeFormat format, const rhi::BackendKind backend) noexcept
        {
            if (backend == rhi::BackendKind::D3D12)
                return format == shaders::NativeFormat::Dxil;
            if (backend == rhi::BackendKind::Vulkan)
                return format == shaders::NativeFormat::SpirV;
            return false;
        }

        [[nodiscard]] bool ConvertStage(const shaders::ShaderStage source, rhi::ShaderStage& destination) noexcept
        {
            switch (source)
            {
            case shaders::ShaderStage::Vertex:
                destination = rhi::ShaderStage::Vertex;
                return true;
            case shaders::ShaderStage::Hull:
                destination = rhi::ShaderStage::Hull;
                return true;
            case shaders::ShaderStage::Domain:
                destination = rhi::ShaderStage::Domain;
                return true;
            case shaders::ShaderStage::Geometry:
                destination = rhi::ShaderStage::Geometry;
                return true;
            case shaders::ShaderStage::Fragment:
                destination = rhi::ShaderStage::Pixel;
                return true;
            case shaders::ShaderStage::Compute:
                destination = rhi::ShaderStage::Compute;
                return true;
            case shaders::ShaderStage::Task:
                destination = rhi::ShaderStage::Amplification;
                return true;
            case shaders::ShaderStage::Mesh:
                destination = rhi::ShaderStage::Mesh;
                return true;
            default:
                return false;
            }
        }
    } // namespace

    const char* ToString(const RenderShaderResult result) noexcept
    {
        switch (result)
        {
        case RenderShaderResult::Success:
            return "Success";
        case RenderShaderResult::InvalidArgument:
            return "InvalidArgument";
        case RenderShaderResult::InvalidState:
            return "InvalidState";
        case RenderShaderResult::OutOfMemory:
            return "OutOfMemory";
        case RenderShaderResult::UnsupportedBackendFormat:
            return "UnsupportedBackendFormat";
        case RenderShaderResult::UnsupportedStage:
            return "UnsupportedStage";
        case RenderShaderResult::NativeCreationFailure:
            return "NativeCreationFailure";
        }
        return "Unknown";
    }

    RenderShader::~RenderShader()
    {
        Unload();
    }

    RenderShaderResult RenderShader::Load(const shaders::ShaderFile& shader, rhi::Failure* const failure) noexcept
    {
        if (m_loaded)
            return RenderShaderResult::InvalidState;
        if (!shader.IsOpen() || !rhi::IsInitialized())
            return RenderShaderResult::InvalidArgument;

        const rhi::BackendKind backend = rhi::GetCapabilities().backend;
        for (const shaders::StageRecord& stage : shader.GetStages())
        {
            if (!IsNativeFormatCompatible(stage.format, backend))
            {
                Unload();
                return RenderShaderResult::UnsupportedBackendFormat;
            }
            rhi::ShaderStage nativeStage = rhi::ShaderStage::Count;
            if (!ConvertStage(stage.stage, nativeStage))
            {
                Unload();
                return RenderShaderResult::UnsupportedStage;
            }
            const containers::ArraySpan<const u8> bytecode = shader.GetBytecode(stage);
            const rhi::ShaderDesc description{nativeStage, bytecode.Data(), bytecode.Size(), stage.entryPointName};
            const rhi::ShaderRef native = rhi::CreateShader(description, failure);
            if (!native)
            {
                Unload();
                return RenderShaderResult::NativeCreationFailure;
            }
            m_stages[static_cast<u32>(stage.stage)] = native;
        }

        m_kind = shader.GetKind();
        m_program = shader.GetProgram();
        m_permutation = shader.GetPermutation();
        m_bindingLayoutFingerprint = shader.BindingLayoutFingerprint();
        m_pipelineInterfaceFingerprint = shader.GetPipelineInterfaceFingerprint();
        if (const shaders::MaterialContract* const material = shader.GetMaterialContract())
        {
            m_materialDomainFingerprint = material->domainFingerprint;
            m_materialLayoutFingerprint = material->layoutFingerprint;
        }
        m_interface = shader.GetInterface();
        m_loaded = true;
        return RenderShaderResult::Success;
    }

    void RenderShader::Unload() noexcept
    {
        for (rhi::ShaderRef& stage : m_stages)
            static_cast<void>(rhi::SafeRelease(stage));
        m_kind = shaders::ProgramKind::Graphics;
        m_program = 0;
        m_permutation = {};
        m_bindingLayoutFingerprint = {};
        m_pipelineInterfaceFingerprint = {};
        m_materialDomainFingerprint = {};
        m_materialLayoutFingerprint = {};
        m_interface = {};
        m_loaded = false;
    }

    bool RenderShader::IsLoaded() const noexcept
    {
        return m_loaded;
    }
    shaders::ProgramKind RenderShader::GetKind() const noexcept
    {
        return m_kind;
    }
    u64 RenderShader::GetProgram() const noexcept
    {
        return m_program;
    }
    rhi::ShaderRef RenderShader::GetStage(const shaders::ShaderStage stage) const noexcept
    {
        return stage < shaders::ShaderStage::Count ? m_stages[static_cast<u32>(stage)] : rhi::ShaderRef{};
    }
    const crypto::Digest256& RenderShader::GetPermutation() const noexcept
    {
        return m_permutation;
    }
    const crypto::Digest256& RenderShader::BindingLayoutFingerprint() const noexcept
    {
        return m_bindingLayoutFingerprint;
    }
    const crypto::Digest256& RenderShader::GetPipelineInterfaceFingerprint() const noexcept
    {
        return m_pipelineInterfaceFingerprint;
    }
    const crypto::Digest256& RenderShader::GetMaterialDomainFingerprint() const noexcept
    {
        return m_materialDomainFingerprint;
    }
    const crypto::Digest256& RenderShader::GetMaterialLayoutFingerprint() const noexcept
    {
        return m_materialLayoutFingerprint;
    }
    const shaders::PipelineInterface& RenderShader::GetInterface() const noexcept
    {
        return m_interface;
    }
} // namespace vanguard::rendering
