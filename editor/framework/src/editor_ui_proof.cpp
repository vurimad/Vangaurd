#include <vanguard/editor/editor_ui_proof.hpp>
#include <vanguard/editor/editor_ui_imgui.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/rendering_service.hpp>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cwchar>
#endif

namespace vanguard::editor
{
    namespace { constexpr EditorPanelTypeId ProofType = 0x65306670726f6f66ull; }

    bool EditorUiProof::Start(EditorUiService& ui, engine::RenderingService& rendering, bool interactive) noexcept
    {
        m_ui = &ui;
        m_rendering = &rendering;
        m_interactive = interactive;
#if defined(_WIN32)
        if (interactive && GetEnvironmentVariableW(L"VG_UI_STALL_CAPTURE", nullptr, 0) != 0)
        {
            wchar_t eventName[96];
            std::swprintf(eventName, 96, L"Local\\VanguardUiHeartbeat-%lu", GetCurrentProcessId());
            m_captureHeartbeat = CreateEventW(nullptr, FALSE, FALSE, eventName);
            if (m_captureHeartbeat == nullptr) return false;
        }
#endif
        const u32 pixels[]{0xff2020ffu, 0xff20ff20u, 0xffff2020u, 0xffffffffu};
        rhi::TextureDesc desc;
        desc.extent = {2, 2, 1}; desc.format = rhi::Format::R8G8B8A8UNorm;
        desc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
        desc.initialState = rhi::ResourceState::ShaderResourceGraphics;
        const rhi::TextureSubresourceData initial{pixels, sizeof(pixels), 8, 16, 0, 0};
        // One-time proof fixture upload; production UI uploads use their existing graph.
        m_texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(desc, {&initial, 1}));
        rhi::SamplerStateDesc sampler;
        sampler.addressU = sampler.addressV = sampler.addressW = rhi::SamplerAddressMode::Clamp;
        m_sampler = rhi::SamplerState(rhi::AdoptReference, rhi::RequestSamplerState(sampler));
        if (!m_texture.IsValid() || !m_sampler.IsValid() || !ui.RegisterTexture({m_texture.GetRef(), m_sampler.GetRef(), EditorTextureColorSpace::DisplaySrgb}, m_image)) return false;
        if (!ui.GetPanels().RegisterType({ProofType, "UI integration proof", nullptr, nullptr, Draw, this})) return false;
        return ui.GetPanels().CreatePanel({1, ProofType, "UI proof", true}, m_panels[0]) && ui.GetPanels().CreatePanel({2, ProofType, "Detached UI proof", true}, m_panels[1]);
    }

    void EditorUiProof::Draw(EditorPanelContext& context, void*, void* userData) noexcept
    {
        auto& proof = *static_cast<EditorUiProof*>(userData);
        const bool detached = context.GetInstanceId() == 2;
        const ImGuiViewport* main = ImGui::GetMainViewport();
        if (!detached)
        {
            const ImGuiID dock = ImGui::DockSpaceOverViewport(0, main);
            ImGui::SetNextWindowDockID(dock, ImGuiCond_Once);
        }
        if (detached)
        {
            ImGuiWindowClass windowClass;
            windowClass.ViewportFlagsOverrideSet = proof.m_interactive ? 0 : ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass);
        }
        if (detached)
        {
            ImGui::SetNextWindowPos({main->Pos.x + main->Size.x + 30, main->Pos.y + 50}, ImGuiCond_Once);
            ImGui::SetNextWindowSize({440, 340}, ImGuiCond_Once);
        }
        if (!proof.m_interactive && detached && proof.m_frames == 40) ImGui::SetNextWindowSize({560, 420});
        bool open = true;
        if (ImGui::Begin(detached ? "Detached UI proof" : "UI proof", &open))
        {
            ImGui::TextUnformatted("Vanguard UI integration proof: drag tabs to dock/redock.");
            ImGui::Text("Frame %u; native hosts observed: %u", proof.m_frames, proof.m_maxHosts);
            if (proof.m_interactive)
            {
                const u32 panelIndex = detached ? 1 : 0;
                ImGui::InputText("Keyboard / clipboard", proof.m_text[panelIndex], sizeof(proof.m_text[panelIndex]));
                if (ImGui::Button("Click test")) ++proof.m_clicks[panelIndex];
                ImGui::Text("Clicks: %u", proof.m_clicks[panelIndex]);
            }
            ImGui::Image(detail::ToImGuiTexture(proof.m_image), {180, 120});
            ImGui::BeginChild("clipped content", {0, 100}, true);
            for (u32 index = 0; index < 32; ++index) ImGui::Text("Clipped row %u", index);
            ImGui::EndChild();
        }
        ImGui::End();
        if (!open) static_cast<void>(proof.m_ui->GetPanels().SetOpen(proof.m_panels[detached ? 1 : 0], false));
    }

    bool EditorUiProof::Tick(bool& finished) noexcept
    {
        finished = false;
        if (m_interactive)
        {
#if defined(_WIN32)
            if (m_captureHeartbeat != nullptr) SetEvent(m_captureHeartbeat);
#endif
            // Interactive inspection uses the normal UI frame join, not the assertion fixture's extra join.
            u32 hosts = 0;
            m_ui->VisitHosts([](const EditorHostWindowInfo&, void* count) noexcept { ++*static_cast<u32*>(count); }, &hosts);
            if (hosts > m_maxHosts) m_maxHosts = hosts;
            ++m_frames;
            return true;
        }
        // Proof-only observation boundary; production host submission remains asynchronous.
        if (!m_rendering->GetCommands().FlushPreviousFrameProcessing()) return false;
        rendering::RenderCommandFailure failure;
        if (m_rendering->GetCommands().ConsumeExecutionFailure(failure)) return false;
        m_ui->VisitHosts([](const EditorHostWindowInfo& host, void* data) noexcept
        {
            auto& proof = *static_cast<EditorUiProof*>(data);
            const auto* viewport = proof.m_rendering->GetViewports().Resolve(host.renderViewport);
            if (viewport == nullptr) return;
            if (host.primary) proof.m_primaryPresented |= viewport->GetPresentedFrameCount() != 0;
            else
            {
                proof.m_detachedPresented |= viewport->GetPresentedFrameCount() != 0;
                if (!proof.m_detachedExtent.IsValid()) proof.m_detachedExtent = viewport->GetOutputExtent();
                else proof.m_resized |= proof.m_detachedExtent != viewport->GetOutputExtent();
            }
        }, this);
        u32 hosts = 0;
        m_ui->VisitHosts([](const EditorHostWindowInfo&, void* count) noexcept { ++*static_cast<u32*>(count); }, &hosts);
        if (hosts > m_maxHosts) m_maxHosts = hosts;
        ++m_frames;
        if (m_frames == 80 && !m_ui->GetPanels().SetOpen(m_panels[1], false)) return false;
        if (m_frames < 120) return true;
        finished = true;
        const bool passed = m_maxHosts >= 2 && hosts == 1 && m_primaryPresented && m_detachedPresented && m_resized;
        VG_LOG_INFO(diagnostics::Category::Engine, "E0F UI proof: frames=%u peakHosts=%u finalHosts=%u primaryPresent=%u detachedPresent=%u resized=%u lifecycle=%s (visual checks remain manual)", m_frames, m_maxHosts, hosts, u32(m_primaryPresented), u32(m_detachedPresented), u32(m_resized), passed ? "PASS" : "FAIL");
        return passed;
    }

    void EditorUiProof::Stop() noexcept
    {
#if defined(_WIN32)
        if (m_captureHeartbeat != nullptr) { CloseHandle(m_captureHeartbeat); m_captureHeartbeat = nullptr; }
#endif
        if (m_ui == nullptr) return;
        for (auto& panel : m_panels) if (panel.IsValid()) { static_cast<void>(m_ui->GetPanels().DestroyPanel(panel)); panel = {}; }
        static_cast<void>(m_ui->GetPanels().UnregisterType(ProofType));
        if (m_image.IsValid()) static_cast<void>(m_ui->UnregisterTexture(m_image));
        m_image = {}; m_sampler.Reset(); m_texture.Reset(); m_ui = nullptr;
    }
}
