#pragma once
#include <vanguard/editor/editor_ui.hpp>
namespace vanguard::engine { class RenderingService; }

namespace vanguard::editor
{
    class EditorUiProof final
    {
    public:
        bool Start(EditorUiService& ui, engine::RenderingService& rendering, bool interactive = false) noexcept;
        bool Tick(bool& finished) noexcept;
        void Stop() noexcept;
    private:
        static void Draw(EditorPanelContext&, void*, void*) noexcept;
        EditorUiService* m_ui = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        rendering::ViewportExtent m_detachedExtent;
        bool m_primaryPresented = false;
        bool m_detachedPresented = false;
        bool m_resized = false;
        bool m_interactive = false;
        void* m_captureHeartbeat = nullptr;
        char m_text[2][128]{};
        u32 m_clicks[2]{};
        EditorPanelHandle m_panels[2];
        EditorTextureHandle m_image;
        rhi::Texture m_texture;
        rhi::SamplerState m_sampler;
        u32 m_frames = 0;
        u32 m_maxHosts = 0;
    };
}
