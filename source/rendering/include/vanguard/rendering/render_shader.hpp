#pragma once

#include <vanguard/rhi/rhi.hpp>
#include <vanguard/shaders/shaders.hpp>

namespace vanguard::rendering
{
    enum class RenderShaderResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        UnsupportedBackendFormat,
        UnsupportedStage,
        NativeCreationFailure
    };

    [[nodiscard]] const char* ToString(RenderShaderResult result) noexcept;

    class RenderShader final
    {
    public:
        RenderShader() noexcept = default;
        ~RenderShader();

        RenderShader(const RenderShader&) = delete;
        RenderShader& operator=(const RenderShader&) = delete;

        [[nodiscard]] RenderShaderResult Load(const shaders::ShaderFile& shader, rhi::Failure* failure = nullptr) noexcept;
        void Unload() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] shaders::ProgramKind Kind() const noexcept;
        [[nodiscard]] u64 Program() const noexcept;
        [[nodiscard]] rhi::ShaderRef Stage(shaders::ShaderStage stage) const noexcept;
        [[nodiscard]] const crypto::Digest256& Permutation() const noexcept;
        [[nodiscard]] const crypto::Digest256& BindingLayoutFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& PipelineInterfaceFingerprint() const noexcept;
        [[nodiscard]] const shaders::PipelineInterface& Interface() const noexcept;

    private:
        rhi::ShaderRef m_stages[static_cast<u32>(shaders::ShaderStage::Count)]{};
        shaders::ProgramKind m_kind = shaders::ProgramKind::Graphics;
        u64 m_program = 0;
        crypto::Digest256 m_permutation;
        crypto::Digest256 m_bindingLayoutFingerprint;
        crypto::Digest256 m_pipelineInterfaceFingerprint;
        shaders::PipelineInterface m_interface;
        bool m_loaded = false;
    };
} // namespace vanguard::rendering
