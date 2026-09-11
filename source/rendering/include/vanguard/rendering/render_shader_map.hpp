#pragma once

#include <vanguard/rendering/render_shader.hpp>

namespace vanguard::rendering
{
    struct NamedRenderShader
    {
        containers::StringView name;
        const shaders::ShaderFile* file = nullptr;
    };

    // Fixed renderer-feature catalog. Initialize before graph construction;
    // clear only after recording has joined and cached nodes have been destroyed.
    // Input documents are borrowed only during Init; native stages are owned here.
    class RenderShaderMap final
    {
    public:
        RenderShaderMap() noexcept = default;
        ~RenderShaderMap();
        RenderShaderMap(const RenderShaderMap&) = delete;
        RenderShaderMap& operator=(const RenderShaderMap&) = delete;

        [[nodiscard]] RenderShaderResult Init(containers::ArraySpan<const NamedRenderShader> shaders, rhi::Failure* failure = nullptr) noexcept;
        void Clear() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept { return m_impl != nullptr; }
        [[nodiscard]] const RenderShader* FindShader(containers::StringView name) const noexcept;
        [[nodiscard]] const RenderShader* GetShader(containers::StringView name) const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
}
