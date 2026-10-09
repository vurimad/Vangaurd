// ============================================================================
// NATIVE EDITOR-UI INTEGRATION TEST HARNESS -- TEST SUPPORT ONLY
// ============================================================================
//
// This translation unit exists only to drive and observe the real Vanguard
// editor UI composition during native integration tests covering panel docking,
// focus, multi-host behavior, and atlas updates.
// It is intentionally opt-in and is activated only by the dedicated native UI
// integration-test executable. Normal editor/user execution must not depend on
// any behavior implemented here.
//
// IMPORTANT ARCHITECTURAL EXCEPTION
// ---------------------------------
// Vanguard production/editor features are expected to use Vanguard-owned UI,
// windowing, input, rendering, presentation, panel, and command APIs. They must
// not copy this file's direct ImGui usage or treat it as permission to bypass
// those ownership boundaries.
//
// This test harness is allowed to call ImGui (including selected internal
// ImGui APIs) directly only because it is instrumentation for exercising and
// observing the already-existing Vanguard ImGui integration in a real native
// editor process. Some test cases need exact ImGui state that is deliberately
// not part of Vanguard's public/editor-facing API surface, such as native
// docking telemetry or forcing a real font-atlas texture update while hosts are
// minimized. Those calls are test stimuli/observations, not product behavior.
//
// In particular, NOTHING in this file should be used to infer that:
//   * editor features may talk to ImGui directly;
//   * public/private Vanguard UI APIs should expose ImGui types;
//   * panels own native windows, presentation, focus, or input routing;
//   * a second UI/window/event/render path is acceptable; or
//   * test-only synchronization, telemetry, fixtures, or state are suitable
//     for normal editor/runtime code.
//
// The authoritative owners remain the existing Vanguard systems and adapters.
// This file must stay a consumer/observer of those systems except for narrowly
// scoped test stimulus that cannot be expressed through the normal API.
//
// LIFETIME / REMOVAL INTENT
// -------------------------
// This file is test scaffolding and intentionally lives under editor/tests/support.
// It is compiled only into the dedicated native UI integration-test executable,
// which delegates to the real editor application composition and then adds this
// test-owned service boundary.
//
// Delete this harness as soon as the editor can run, exercise, and validate its
// UI through the normal Vanguard-owned editor/UI path without this dedicated
// test fixture. Do not grow new user-facing behavior here. If future work needs
// permanent functionality presently prototyped by this file, move that behavior
// behind an appropriate Vanguard API/owner instead of preserving this harness.
//
// ============================================================================

#include <native_ui_integration_test_harness.hpp>
#include <vanguard/editor/editor_ui_imgui.hpp>
#include <native_ui_integration_test_extension.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/editor/editor_visual_style.hpp>
#include <vanguard/engine/rendering_service.hpp>

#include <imgui_internal.h>

#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cwchar>
#endif

namespace vanguard::editor::testing
{
    namespace
    {
        constexpr EditorPanelTypeId IntegrationTestPanelType = 0x6564756974651002ull;
        constexpr EditorPanelTypeId DetachedIntegrationTestPanelType = 0x6564756974651003ull;
        constexpr EditorPanelTypeId VisualHierarchyPanelType = 0x6576697375686901ull;
        constexpr EditorPanelTypeId VisualScenePanelType = 0x6576697376736301ull;
        constexpr EditorPanelTypeId VisualPropertiesPanelType = 0x6576697376707201ull;
        constexpr EditorPanelTypeId VisualProjectPanelType = 0x6576697376706a01ull;
        constexpr u32 IntegrationTestStateSentinelBase = 0x4e554900u;

        struct PanelTitleLookup final
        {
            const char* title = nullptr;
            EditorPanelHandle handle;
        };

        void FindPanelByTitle(const EditorPanelInfo& panel, void* const userData) noexcept
        {
            auto* const lookup = static_cast<PanelTitleLookup*>(userData);
            if (lookup == nullptr || lookup->handle.IsValid() || lookup->title == nullptr ||
                panel.title == nullptr)
                return;
            if (std::strcmp(panel.title, lookup->title) == 0)
                lookup->handle = panel.handle;
        }

        [[nodiscard]] ImGuiWindow* FindPanelWindow(ImGuiContext& context,
                                                   const char* const visibleTitle) noexcept
        {
            if (visibleTitle == nullptr)
                return nullptr;
            const size_t titleBytes = std::strlen(visibleTitle);
            for (ImGuiWindow* const window : context.Windows)
            {
                if (window == nullptr || window->Name == nullptr ||
                    std::strncmp(window->Name, visibleTitle, titleBytes) != 0 ||
                    std::strncmp(window->Name + titleBytes, "###", 3) != 0)
                    continue;
                return window;
            }
            return nullptr;
        }

        [[nodiscard]] ImVec2 PanelDragPoint(ImGuiWindow& window) noexcept
        {
            ImVec2 drag = {(window.TitleBarRect().Min.x + window.TitleBarRect().Max.x) * 0.5f,
                           (window.TitleBarRect().Min.y + window.TitleBarRect().Max.y) * 0.5f};
            if (window.DockNode == nullptr || window.DockNode->TabBar == nullptr)
                return drag;
            ImGuiTabBar* const tabBar = window.DockNode->TabBar;
            for (const ImGuiTabItem& tab : tabBar->Tabs)
            {
                if (tab.Window != &window)
                    continue;
                return {tabBar->BarRect.Min.x + tab.Offset - tabBar->ScrollingAnim + tab.Width * 0.5f,
                        (tabBar->BarRect.Min.y + tabBar->BarRect.Max.y) * 0.5f};
            }
            return drag;
        }

        void DrawPropertyRow(const char* const label, const char* const value) noexcept
        {
            ImGui::PushID(label);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            char buffer[128]{};
            std::snprintf(buffer, sizeof(buffer), "%s", value);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopID();
        }

        [[nodiscard]] bool BeginVisualPropertySection(const char* const label) noexcept
        {
            ImGui::PushStyleColor(ImGuiCol_Header, detail::EditorVisualColor(0x393939u));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, detail::EditorVisualColor(0x434343u));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, detail::EditorVisualColor(0x4b4b4bu));
            const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(3);
            return open;
        }

        void DrawAssetTile(const char* const name, const char* const type, const bool selected) noexcept
        {
            ImGui::PushID(name);
            const ImVec2 size{112.0f, 72.0f};
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, detail::EditorVisualColor(0x365f7du));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, detail::EditorVisualColor(0x343434u));
            ImGui::Button("##asset", size);
            ImGui::PopStyleColor();
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            const ImU32 preview = ImGui::GetColorU32(selected ? detail::EditorVisualColor(0x668ca6u)
                                                              : detail::EditorVisualColor(0x555555u));
            draw->AddRectFilled({minimum.x + 6.0f, minimum.y + 6.0f},
                                {maximum.x - 6.0f, maximum.y + 39.0f}, preview);
            draw->AddText({minimum.x + 8.0f, maximum.y - 28.0f},
                          ImGui::GetColorU32(ImGuiCol_Text), name);
            draw->AddText({minimum.x + 8.0f, maximum.y - 14.0f},
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), type);
            ImGui::PopID();
        }
    }

    bool NativeUiIntegrationTestHarness::Start(EditorUiService& ui, EditorActivityService& activity,
                                               engine::RenderingService& rendering, const bool interactive,
                                               const bool visualPreview) noexcept
    {
        m_ui = &ui;
        m_activity = &activity;
        m_rendering = &rendering;
        m_interactive = interactive;
        m_visualPreview = visualPreview;
        m_automation = false;
        m_dockedSeen = false;
        m_detachedAfterDock = false;
        m_redockedAfterDetach = false;
        m_dpiTransitionObserved = false;
        m_atlasTriggered = false;
        m_atlasQueuedWhileMinimized = false;
        m_atlasAcceptedWhileMinimized = false;
        for (u32 index = 0; index < 2; ++index)
        {
            m_panelState[index] = {};
            m_panelCreateCalls[index] = 0;
            m_panelDestroyCalls[index] = 0;
            m_panelStateObserved[index] = false;
            m_text[index][0] = '\0';
            m_clicks[index] = 0;
            m_textObserved[index] = false;
            m_clickObserved[index] = false;
            m_telemetry[index] = {};
        }
        m_closedFrameObserved = false;
        m_reopenRequested = false;
        m_reopenStatePreserved = false;
        m_contextPanelClosedForCancellation = false;
        m_contextCancellationSequence = 0;
        m_contextCancellationFrame = 0;
        m_lastContextPopupSequence = 0;
        m_lastContextAbsentSequence = 0;
        m_activityProofStage = ActivityProofStage::WaitingForSource;
        m_activityProofTicks = 0;
        m_activityBaselineGeneration = 0;
        m_activityBaselineOperationGeneration = 0;
        m_activityExpectedOperationGeneration = 0;
        m_activityBaselineSamples = 0;
        m_activityProofComplete = false;
        if (visualPreview)
        {
            PanelTitleLookup backgroundLookup{"Background Activity"};
            ui.GetPanels().VisitPanels(FindPanelByTitle, &backgroundLookup);
            m_backgroundActivityPanel = backgroundLookup.handle;
            PanelTitleLookup consoleLookup{"Console"};
            ui.GetPanels().VisitPanels(FindPanelByTitle, &consoleLookup);
            m_consolePanel = consoleLookup.handle;
            if (m_backgroundActivityPanel.IsValid())
                static_cast<void>(ui.GetPanels().SetOpen(m_backgroundActivityPanel, false));
            if (m_consolePanel.IsValid())
                static_cast<void>(ui.GetPanels().SetOpen(m_consolePanel, false));

            const EditorPanelTypeDesc visualTypes[]{
                {VisualHierarchyPanelType, "Hierarchy", nullptr, nullptr, DrawVisualHierarchy, this,
                 EditorPanelTypePolicy::Singleton, 101u, EditorPanelInitialPlacement::Left},
                {VisualScenePanelType, "Perspective", nullptr, nullptr, DrawVisualScene, this,
                 EditorPanelTypePolicy::Singleton, 102u, EditorPanelInitialPlacement::Center},
                {VisualPropertiesPanelType, "Properties", nullptr, nullptr, DrawVisualProperties, this,
                 EditorPanelTypePolicy::Singleton, 103u, EditorPanelInitialPlacement::Right},
                {VisualProjectPanelType, "Project", nullptr, nullptr, DrawVisualProject, this,
                 EditorPanelTypePolicy::Singleton, 104u, EditorPanelInitialPlacement::Bottom}};
            const EditorPanelDesc visualPanels[]{
                {101u, VisualHierarchyPanelType, "Hierarchy", true},
                {102u, VisualScenePanelType, "Perspective", true},
                {103u, VisualPropertiesPanelType, "Properties", true},
                {104u, VisualProjectPanelType, "Project", true}};
            for (u32 index = 0; index < 4u; ++index)
                if (!ui.GetPanels().RegisterType(visualTypes[index], m_visualPanelTypes[index]) ||
                    !ui.GetPanels().CreatePanel(visualPanels[index], m_visualPanels[index]))
                    return false;
            return true;
        }

        PanelTitleLookup backgroundLookup{"Background Activity"};
        ui.GetPanels().VisitPanels(FindPanelByTitle, &backgroundLookup);
        m_backgroundActivityPanel = backgroundLookup.handle;
        if (!m_backgroundActivityPanel.IsValid())
            return false;
        PanelTitleLookup commandLookup{"Native UI command focus test"};
        ui.GetPanels().VisitPanels(FindPanelByTitle, &commandLookup);
        m_commandPanel = commandLookup.handle;
        if (!m_commandPanel.IsValid())
            return false;
#if defined(_WIN32)
        m_automation = interactive && GetEnvironmentVariableW(L"VG_EDITOR_NATIVE_UI_TEST_AUTOMATION", nullptr, 0) != 0;
        if (interactive && GetEnvironmentVariableW(L"VG_EDITOR_NATIVE_UI_TEST_STALL_CAPTURE", nullptr, 0) != 0)
        {
            wchar_t eventName[96];
            std::swprintf(eventName, 96, L"Local\\VanguardNativeUiTestHeartbeat-%lu", GetCurrentProcessId());
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
        // One-time test fixture upload; production UI uploads use their existing graph.
        m_texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(desc, {&initial, 1}));
        rhi::SamplerStateDesc sampler;
        sampler.addressU = sampler.addressV = sampler.addressW = rhi::SamplerAddressMode::Clamp;
        m_sampler = rhi::SamplerState(rhi::AdoptReference, rhi::RequestSamplerState(sampler));
        if (!m_texture.IsValid() || !m_sampler.IsValid() || !ui.RegisterTexture({m_texture.GetRef(), m_sampler.GetRef(), EditorTextureColorSpace::DisplaySrgb}, m_image)) return false;
        EditorPanelTypeDesc docked{IntegrationTestPanelType, "UI integration test", CreatePanelState, DestroyPanelState, Draw, this};
        docked.initialPlacement = EditorPanelInitialPlacement::Center;
        EditorPanelTypeDesc detached{DetachedIntegrationTestPanelType, "Detached UI integration test", CreatePanelState, DestroyPanelState, Draw, this};
        detached.initialPlacement = EditorPanelInitialPlacement::Floating;
        if (!ui.GetPanels().RegisterType(docked, m_panelTypes[0]) || !ui.GetPanels().RegisterType(detached, m_panelTypes[1])) return false;
        if (!ui.GetPanels().CreatePanel({1, IntegrationTestPanelType, "UI integration test", true}, m_panels[0]) ||
            !ui.GetPanels().CreatePanel({2, DetachedIntegrationTestPanelType, "Detached UI integration test", true}, m_panels[1]))
            return false;
        return true;
    }

    void NativeUiIntegrationTestHarness::DrawVisualHierarchy(EditorPanelContext&, void*, void*) noexcept
    {
        static char filter[96]{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchyFilter", "Search scene...", filter, sizeof(filter));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 1.0f));
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNodeEx("Cardinal Canyon", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx("Environment", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::TreeNodeEx("Sun", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Sky Atmosphere", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Fog Volume", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreePop();
            }
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx("Gameplay", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::TreeNodeEx("PlayerStart", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Camera_Main", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreePop();
            }
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx("Geometry", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                const char* const names[]{"cliff_arch_01", "canyon_rock_d180", "canyon_rock_d181",
                                          "bridge_support_A", "pine_cluster_07", "ground_plateau_02"};
                for (u32 index = 0; index < 6u; ++index)
                {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (index == 1u)
                        flags |= ImGuiTreeNodeFlags_Selected;
                    ImGui::TreeNodeEx(names[index], flags);
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopStyleVar();
    }

    void NativeUiIntegrationTestHarness::DrawVisualScene(EditorPanelContext&, void*, void*) noexcept
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 2.0f));
        ImGui::SmallButton("Perspective");
        ImGui::SameLine();
        ImGui::SmallButton("Shaded");
        ImGui::SameLine();
        ImGui::SmallButton("Local");
        ImGui::SameLine();
        ImGui::SmallButton("Grid");
        ImGui::SameLine();
        ImGui::TextDisabled("  Camera_Main   |   60 FPS");
        ImGui::PopStyleVar();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 canvasSize{available.x > 40.0f ? available.x : 40.0f,
                                available.y > 40.0f ? available.y : 40.0f};
        ImGui::InvisibleButton("##VisualSceneCanvas", canvasSize);
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(detail::EditorVisualColor(0x202426u)));

        const float grid = 42.0f;
        for (float x = minimum.x; x < maximum.x; x += grid)
            draw->AddLine({x, minimum.y}, {x, maximum.y}, ImGui::GetColorU32(detail::EditorVisualColor(0x303638u)), 1.0f);
        for (float y = minimum.y; y < maximum.y; y += grid)
            draw->AddLine({minimum.x, y}, {maximum.x, y}, ImGui::GetColorU32(detail::EditorVisualColor(0x303638u)), 1.0f);

        const ImVec2 center{(minimum.x + maximum.x) * 0.53f, (minimum.y + maximum.y) * 0.56f};
        draw->AddRect({center.x - 92.0f, center.y - 58.0f}, {center.x + 92.0f, center.y + 58.0f},
                      ImGui::GetColorU32(detail::EditorVisualColor(0x6eb5e9u)), 0.0f, 0, 1.0f);
        draw->AddLine(center, {center.x + 82.0f, center.y}, ImGui::GetColorU32(detail::EditorVisualColor(0xd76868u)), 2.0f);
        draw->AddLine(center, {center.x, center.y - 82.0f}, ImGui::GetColorU32(detail::EditorVisualColor(0x77b96du)), 2.0f);
        draw->AddLine(center, {center.x - 56.0f, center.y + 54.0f}, ImGui::GetColorU32(detail::EditorVisualColor(0x6c8fd4u)), 2.0f);
        draw->AddCircleFilled({minimum.x + 170.0f, minimum.y + 145.0f}, 22.0f,
                              ImGui::GetColorU32(detail::EditorVisualColor(0x4b5558u)));
        draw->AddRectFilled({maximum.x - 230.0f, maximum.y - 160.0f}, {maximum.x - 90.0f, maximum.y - 80.0f},
                            ImGui::GetColorU32(detail::EditorVisualColor(0x454d50u)));
        draw->AddText({minimum.x + 10.0f, minimum.y + 9.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      "VISUAL REVIEW MOCKUP - scene content is intentionally fictional");
        draw->AddText({center.x - 76.0f, center.y + 66.0f}, ImGui::GetColorU32(ImGuiCol_Text), "canyon_rock_d180");
        draw->PopClipRect();
    }

    void NativeUiIntegrationTestHarness::DrawVisualProperties(EditorPanelContext&, void*, void*) noexcept
    {
        ImGui::TextDisabled("Selected object");
        ImGui::TextUnformatted("canyon_rock_d180");

        if (BeginVisualPropertySection("General"))
        {
            if (ImGui::BeginTable("##GeneralProperties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                DrawPropertyRow("Type", "Static Mesh");
                DrawPropertyRow("Name", "canyon_rock_d180");
                DrawPropertyRow("Layer", "Environment");
                ImGui::EndTable();
            }
        }

        if (BeginVisualPropertySection("Transform"))
        {
            static float position[3]{1727.95f, 878.57f, 44.67f};
            static float rotation[3]{-1.74f, -0.97f, 63.01f};
            static float scale[3]{1.0f, 1.0f, 1.0f};
            ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat3("Position", position, 0.1f);
            ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat3("Rotation", rotation, 0.1f);
            ImGui::SetNextItemWidth(-1.0f); ImGui::DragFloat3("Scale", scale, 0.01f);
        }

        if (BeginVisualPropertySection("Rendering"))
        {
            static bool castShadows = true;
            static bool visible = true;
            static bool receiveDecals = true;
            static int lod = 0;
            ImGui::Checkbox("Visible", &visible);
            ImGui::Checkbox("Cast shadows", &castShadows);
            ImGui::Checkbox("Receive decals", &receiveDecals);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo("LOD policy", &lod, "Automatic\0Force LOD 0\0Force LOD 1\0\0");
        }

        if (BeginVisualPropertySection("Material"))
        {
            if (ImGui::BeginTable("##MaterialProperties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                DrawPropertyRow("Slot 0", "rock_cliff_master");
                DrawPropertyRow("Surface", "stone_rough");
                ImGui::EndTable();
            }
        }
    }

    void NativeUiIntegrationTestHarness::DrawVisualProject(EditorPanelContext&, void*, void*) noexcept
    {
        static char filter[96]{};
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##ProjectFilter", "Search project assets...", filter, sizeof(filter));
        ImGui::SameLine();
        ImGui::SmallButton("Import");
        ImGui::SameLine();
        ImGui::SmallButton("Reimport");

        const float folderWidth = 190.0f;
        if (ImGui::BeginChild("##ProjectFolders", {folderWidth, 0.0f}, true))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::TreeNodeEx("Environment", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Selected);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Characters", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::TreePop();
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##ProjectAssets", {0.0f, 0.0f}, false))
        {
            const char* const names[]{"canyon_rock_d180", "cliff_arch_01", "pine_cluster_07", "ground_plateau_02", "bridge_support_A"};
            const char* const types[]{"Mesh", "Mesh", "Prefab", "Mesh", "Mesh"};
            const float tileWidth = 118.0f;
            const u32 columns = static_cast<u32>(ImGui::GetContentRegionAvail().x / tileWidth) > 0u
                ? static_cast<u32>(ImGui::GetContentRegionAvail().x / tileWidth) : 1u;
            for (u32 index = 0; index < 5u; ++index)
            {
                if (index != 0u && (index % columns) != 0u)
                    ImGui::SameLine();
                DrawAssetTile(names[index], types[index], index == 0u);
            }
        }
        ImGui::EndChild();
    }

    void NativeUiIntegrationTestHarness::LogTelemetry(const u32 panelIndex, const InteractionTelemetry& telemetry) noexcept
    {
        if (!m_automation)
            return;
        const InteractionTelemetry& previous = m_telemetry[panelIndex];
        const auto moved = [](const f32 left, const f32 right) noexcept
        {
            const f32 delta = left - right;
            return delta < -4.0f || delta > 4.0f;
        };
        const f32 dpiDelta = telemetry.dpiScale - previous.dpiScale;
        const bool materiallyChanged =
            !previous.valid || previous.docked != telemetry.docked || previous.focused != telemetry.focused ||
            previous.viewport != telemetry.viewport || dpiDelta < -0.01f || dpiDelta > 0.01f ||
            moved(previous.dragX, telemetry.dragX) || moved(previous.dragY, telemetry.dragY) ||
            moved(previous.textX, telemetry.textX) || moved(previous.textY, telemetry.textY) ||
            moved(previous.windowX, telemetry.windowX) || moved(previous.windowY, telemetry.windowY) ||
            moved(previous.windowWidth, telemetry.windowWidth) || moved(previous.windowHeight, telemetry.windowHeight);
        if (!materiallyChanged && (m_frames % 30u) != 0u)
            return;
        VG_LOG_INFO(
            diagnostics::Category::Engine,
            "Editor native UI test telemetry: panel=%u frame=%u docked=%u focused=%u viewport=%u dpi=%.3f drag=%.1f,%.1f text=%.1f,%.1f click=%.1f,%.1f window=%.1f,%.1f,%.1f,%.1f state=%08x create=%u destroy=%u",
            panelIndex, m_frames, u32(telemetry.docked), u32(telemetry.focused), telemetry.viewport,
            static_cast<double>(telemetry.dpiScale), static_cast<double>(telemetry.dragX),
            static_cast<double>(telemetry.dragY), static_cast<double>(telemetry.textX),
            static_cast<double>(telemetry.textY), static_cast<double>(telemetry.clickX),
            static_cast<double>(telemetry.clickY), static_cast<double>(telemetry.windowX),
            static_cast<double>(telemetry.windowY), static_cast<double>(telemetry.windowWidth),
            static_cast<double>(telemetry.windowHeight), m_panelState[panelIndex].sentinel,
            m_panelCreateCalls[panelIndex], m_panelDestroyCalls[panelIndex]);
        diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
    }

    bool NativeUiIntegrationTestHarness::ObserveSurfaceTelemetry() noexcept
    {
        if (!m_automation || m_ui == nullptr)
            return true;

        if (m_contextPanelClosedForCancellation && m_frames > m_contextCancellationFrame)
        {
            if (!m_ui->GetPanels().SetOpen(m_commandPanel, true))
                return false;
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI test context cancel: sequence=%u action=reopen frame=%u",
                        m_contextCancellationSequence, m_frames);
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            m_contextPanelClosedForCancellation = false;
        }

        u32 cancellationSequence = 0;
        if (ConsumeNativeUiContextCancellationRequest(cancellationSequence))
        {
            if (!m_ui->GetPanels().SetOpen(m_commandPanel, false))
                return false;
            m_contextPanelClosedForCancellation = true;
            m_contextCancellationSequence = cancellationSequence;
            m_contextCancellationFrame = m_frames;
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI test context cancel: sequence=%u action=close frame=%u",
                        cancellationSequence, m_frames);
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
        }

        ImGuiContext* const imgui = ImGui::GetCurrentContext();
        if (imgui == nullptr)
            return true;

        ImGuiWindow* const activityWindow = FindPanelWindow(*imgui, "Background Activity");
        if (activityWindow != nullptr)
        {
            NativeUiActivityTelemetry activityTelemetry;
            if (!GetNativeUiActivityTelemetry(activityTelemetry))
                return false;
            EditorActivityItem publishedItem;
            EditorActivityCapture publishedCapture;
            bool published = false;
            if (!CaptureTestActivity(publishedItem, publishedCapture, published))
                return false;
            const ImVec2 drag = PanelDragPoint(*activityWindow);
            const bool open = m_ui->GetPanels().IsOpen(m_backgroundActivityPanel);
            const bool docked = activityWindow->DockNode != nullptr;
            const bool visible = open && !activityWindow->Hidden &&
                                 (!docked || activityWindow->DockTabIsVisible);
            if ((m_frames % 15u) == 0u || activityTelemetry.uiCancelInteractions != 0u)
            {
                VG_LOG_INFO(
                    diagnostics::Category::Engine,
                    "Editor native UI Activity panel telemetry: frame=%u open=%u docked=%u visible=%u viewport=%u drag=%.1f,%.1f operation=%llu samples=%u producerTicks=%u state=%u cancels=%u accepted=%u uiInteractions=%u queued=%u published=%u publishedGeneration=%llu publishedState=%u cancelQueued=%u cancelResult=%u",
                    m_frames, u32(open), u32(docked), u32(visible),
                    activityWindow->Viewport != nullptr ? activityWindow->Viewport->ID : 0u,
                    static_cast<double>(drag.x), static_cast<double>(drag.y),
                    static_cast<unsigned long long>(activityTelemetry.operationGeneration),
                    activityTelemetry.samples, activityTelemetry.producerTicks,
                    static_cast<u32>(activityTelemetry.state), activityTelemetry.cancelRequests,
                    activityTelemetry.acceptedCancels, activityTelemetry.uiCancelInteractions,
                    activityTelemetry.queuedCancels, u32(published),
                    static_cast<unsigned long long>(published ? publishedItem.key.itemGeneration : 0u),
                    static_cast<u32>(published ? publishedItem.state : EditorActivityState::Idle),
                    u32(published && publishedItem.cancelQueued),
                    static_cast<u32>(published ? publishedItem.cancelResult : EditorActivityCancelResult::None));
                diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            }
        }
        if (ImGuiWindow* const consoleWindow = FindPanelWindow(*imgui, "Console");
            consoleWindow != nullptr && (m_frames % 15u) == 0u)
        {
            const ImVec2 drag = PanelDragPoint(*consoleWindow);
            const bool docked = consoleWindow->DockNode != nullptr;
            const bool visible = !consoleWindow->Hidden &&
                                 (!docked || consoleWindow->DockTabIsVisible);
            VG_LOG_INFO(
                diagnostics::Category::Engine,
                "Editor native UI Console panel telemetry: frame=%u docked=%u visible=%u viewport=%u drag=%.1f,%.1f",
                m_frames, u32(docked), u32(visible),
                consoleWindow->Viewport != nullptr ? consoleWindow->Viewport->ID : 0u,
                static_cast<double>(drag.x), static_cast<double>(drag.y));
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
        }

        if (ImGuiWindow* const menu = ImGui::FindWindowByName("##MainMenuBar"); menu != nullptr && menu->WasActive)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 fileSize = ImGui::CalcTextSize("File");
            const f32 fileX = menu->Pos.x + style.WindowPadding.x + style.ItemSpacing.x * 0.5f + fileSize.x * 0.5f;
            const f32 fileY = menu->Pos.y + menu->MenuBarHeight * 0.5f;
            if ((m_frames % 30u) == 0u)
            {
                ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
                ImGuiWindow* const toolbar = ImGui::FindWindowByName("##VanguardMainToolbar");
                VG_LOG_INFO(diagnostics::Category::Engine,
                            "Editor native UI test surface telemetry: frame=%u mainViewport=%u menuViewport=%u toolbarViewport=%u primaryMenu=%.1f,%.1f",
                            m_frames, mainViewport != nullptr ? mainViewport->ID : 0u,
                            menu->Viewport != nullptr ? menu->Viewport->ID : 0u,
                            toolbar != nullptr && toolbar->WasActive && toolbar->Viewport != nullptr ? toolbar->Viewport->ID : 0u,
                            static_cast<double>(fileX), static_cast<double>(fileY));
                diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            }
        }
        for (ImGuiWindow* const window : imgui->Windows)
        {
            if (window == nullptr || !window->WasActive || (window->Flags & ImGuiWindowFlags_ChildMenu) == 0 ||
                window->Viewport == nullptr)
                continue;
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI test menu draw telemetry: frame=%u viewport=%u indices=%u width=%.1f",
                        m_frames, window->Viewport->ID, static_cast<u32>(window->DrawList->IdxBuffer.Size),
                        static_cast<double>(window->SizeFull.x));
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            break;
        }

        NativeUiContextRequestTelemetry request;
        if (!GetNativeUiContextRequestTelemetry(request) || !request.accepted)
            return true;

        if (request.sequence > m_lastContextPopupSequence)
        {
            ImGuiWindow* popup = nullptr;
            for (ImGuiWindow* const window : imgui->Windows)
            {
                if (window == nullptr || !window->Active || (window->Flags & ImGuiWindowFlags_Popup) == 0 ||
                    (window->Flags & ImGuiWindowFlags_ChildMenu) != 0 || window->Viewport == nullptr ||
                    window->Viewport->ID != request.viewport)
                    continue;
                const f32 dx = window->Pos.x - request.anchorX;
                const f32 dy = window->Pos.y - request.anchorY;
                const f32 absX = dx < 0.0f ? -dx : dx;
                const f32 absY = dy < 0.0f ? -dy : dy;
                if (absX <= 96.0f && absY <= 96.0f)
                {
                    popup = window;
                    break;
                }
            }
            if (popup != nullptr)
            {
                ImRect itemRect;
                bool hasItemRect = false;
                if (imgui->NavInitResult.Window == popup && imgui->NavInitResult.ID != 0)
                {
                    itemRect = ImGui::WindowRectRelToAbs(popup, imgui->NavInitResult.RectRel);
                    hasItemRect = true;
                }
                else if (popup->NavLastIds[ImGuiNavLayer_Main] != 0)
                {
                    itemRect = ImGui::WindowRectRelToAbs(popup, popup->NavRectRel[ImGuiNavLayer_Main]);
                    hasItemRect = true;
                }
                if (!hasItemRect)
                    return true;

                const ImVec2 cursor = ImGui::GetIO().MousePos;
                const u32 uiFrame = static_cast<u32>(ImGui::GetFrameCount());
                const f32 itemX = (itemRect.Min.x + itemRect.Max.x) * 0.5f;
                const f32 itemY = (itemRect.Min.y + itemRect.Max.y) * 0.5f;
                VG_LOG_INFO(diagnostics::Category::Engine,
                            "Editor native UI test context popup: sequence=%u frame=%u uiFrame=%u viewport=%u rect=%.1f,%.1f,%.1f,%.1f item=%.1f,%.1f cursor=%.1f,%.1f",
                            request.sequence, m_frames, uiFrame, popup->Viewport->ID,
                            static_cast<double>(popup->Pos.x), static_cast<double>(popup->Pos.y),
                            static_cast<double>(popup->SizeFull.x), static_cast<double>(popup->SizeFull.y),
                            static_cast<double>(itemX), static_cast<double>(itemY),
                            static_cast<double>(cursor.x), static_cast<double>(cursor.y));
                diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
                m_lastContextPopupSequence = request.sequence;
            }
        }

        const u32 imguiFrame = static_cast<u32>(ImGui::GetFrameCount());
        if (request.sequence > m_lastContextAbsentSequence && imguiFrame >= request.frame + 1u &&
            (request.cancelAfterCommit || request.mode == NativeUiCommandPresentationMode::Unsupported) &&
            m_lastContextPopupSequence < request.sequence)
        {
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI test context popup absent: sequence=%u frame=%u mode=%u cancel=%u",
                        request.sequence, m_frames, static_cast<u32>(request.mode), u32(request.cancelAfterCommit));
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            m_lastContextAbsentSequence = request.sequence;
        }
        return true;
    }

    bool NativeUiIntegrationTestHarness::CaptureTestActivity(EditorActivityItem& item,
                                                             EditorActivityCapture& capture,
                                                             bool& found) noexcept
    {
        item = {};
        capture = {};
        found = false;
        if (m_activity == nullptr)
            return false;

        EditorActivityFailure failure;
        if (!m_activity->CaptureActivities({}, {m_activitySources, MaximumEditorActivitySources},
                                          {m_activityItems, MaximumEditorActivityRows},
                                          capture, &failure))
            return false;

        for (u32 sourceIndex = 0; sourceIndex < capture.sourceCount; ++sourceIndex)
        {
            const EditorActivitySourceSnapshot& source = m_activitySources[sourceIndex];
            if (source.source != NativeUiActivitySourceId)
                continue;
            if (source.stale || source.firstItem + source.itemCount > capture.itemCount)
                return false;
            for (u32 itemIndex = 0; itemIndex < source.itemCount; ++itemIndex)
            {
                const EditorActivityItem& candidate =
                    m_activityItems[source.firstItem + itemIndex];
                if (candidate.key.item != NativeUiActivityItemId)
                    continue;
                item = candidate;
                found = true;
                return true;
            }
            return false;
        }
        return true;
    }

    bool NativeUiIntegrationTestHarness::TickActivityProof() noexcept
    {
        if (m_activityProofComplete)
            return true;
        if (++m_activityProofTicks > 180u || m_ui == nullptr || m_activity == nullptr ||
            !m_backgroundActivityPanel.IsValid())
            return false;

        EditorActivityItem item;
        EditorActivityCapture capture;
        bool found = false;
        if (!CaptureTestActivity(item, capture, found))
            return false;
        NativeUiActivityTelemetry telemetry;
        if (!GetNativeUiActivityTelemetry(telemetry))
            return false;

        switch (m_activityProofStage)
        {
        case ActivityProofStage::WaitingForSource:
            if (!found || item.state != EditorActivityState::Running ||
                telemetry.operationGeneration == 0u || telemetry.samples == 0u)
                return true;
            m_activityBaselineGeneration = capture.cursor.activityGeneration;
            m_activityBaselineOperationGeneration = telemetry.operationGeneration;
            m_activityBaselineSamples = telemetry.samples;
            if (!m_ui->GetPanels().SetOpen(m_backgroundActivityPanel, false) ||
                !AdvanceNativeUiActivityOperation())
                return false;
            m_activityExpectedOperationGeneration = m_activityBaselineOperationGeneration + 1u;
            m_activityProofStage = ActivityProofStage::ClosedAwaitAdvance;
            return true;

        case ActivityProofStage::ClosedAwaitAdvance:
            if (m_ui->GetPanels().IsOpen(m_backgroundActivityPanel))
                return false;
            if (!found || item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                capture.cursor.activityGeneration <= m_activityBaselineGeneration ||
                telemetry.samples <= m_activityBaselineSamples)
                return true;
            if (telemetry.operationGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Running ||
                !m_ui->GetPanels().SetOpen(m_backgroundActivityPanel, true))
                return false;
            m_activityProofStage = ActivityProofStage::ReopenedAwaitLatest;
            return true;

        case ActivityProofStage::ReopenedAwaitLatest:
            if (!m_ui->GetPanels().IsOpen(m_backgroundActivityPanel) || !found ||
                item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Running)
                return true;
            {
                EditorActivityFailure failure;
                if (m_activity->RequestCancel(item.key, &failure) !=
                        EditorActivityCancelAdmission::Queued ||
                    m_activity->GetStatus().pendingCancellations == 0u ||
                    !m_ui->GetPanels().SetOpen(m_backgroundActivityPanel, false))
                    return false;
            }
            m_activityProofStage = ActivityProofStage::CancelQueuedClosed;
            return true;

        case ActivityProofStage::CancelQueuedClosed:
            if (m_ui->GetPanels().IsOpen(m_backgroundActivityPanel))
                return false;
            if (telemetry.cancelRequests == 0u)
            {
                if (telemetry.acceptedCancels != 0u)
                    return false;
                return true;
            }
            if (telemetry.cancelRequests != 1u || telemetry.acceptedCancels != 1u ||
                telemetry.lastCancelGeneration != m_activityExpectedOperationGeneration ||
                telemetry.state != EditorActivityState::Cancelling || !found ||
                item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Running ||
                item.cancelResult != EditorActivityCancelResult::Accepted ||
                !item.cancelQueued)
                return false;
            m_activityProofStage = ActivityProofStage::AwaitCancellingPublication;
            return true;

        case ActivityProofStage::AwaitCancellingPublication:
            if (telemetry.cancelRequests != 1u || telemetry.acceptedCancels != 1u)
                return false;
            if (!found || item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Cancelling)
                return true;
            m_activityProofStage = ActivityProofStage::AwaitTerminalPublication;
            return true;

        case ActivityProofStage::AwaitTerminalPublication:
            if (telemetry.cancelRequests != 1u || telemetry.acceptedCancels != 1u)
                return false;
            if (!found || item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Cancelled)
                return true;
            if (!m_ui->GetPanels().SetOpen(m_backgroundActivityPanel, true))
                return false;
            m_activityProofStage = ActivityProofStage::ReopenedTerminal;
            return true;

        case ActivityProofStage::ReopenedTerminal:
            if (!m_ui->GetPanels().IsOpen(m_backgroundActivityPanel) || !found ||
                item.key.itemGeneration != m_activityExpectedOperationGeneration ||
                item.state != EditorActivityState::Cancelled ||
                telemetry.cancelRequests != 1u || telemetry.acceptedCancels != 1u)
                return true;
            m_activityProofComplete = true;
            m_activityProofStage = ActivityProofStage::Complete;
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI activity lifecycle: baseline=%llu latest=%llu samples=%u cancels=%u accepted=%u lifecycle=PASS",
                        static_cast<unsigned long long>(m_activityBaselineOperationGeneration),
                        static_cast<unsigned long long>(m_activityExpectedOperationGeneration),
                        telemetry.samples, telemetry.cancelRequests, telemetry.acceptedCancels);
            diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
            return true;

        case ActivityProofStage::Complete:
            return true;
        }
        return false;
    }

    void* NativeUiIntegrationTestHarness::CreatePanelState(const EditorPanelInstanceId instance, void* const userData) noexcept
    {
        auto& test = *static_cast<NativeUiIntegrationTestHarness*>(userData);
        if (instance == 0 || instance > 2)
            return nullptr;
        const u32 index = static_cast<u32>(instance - 1u);
        ++test.m_panelCreateCalls[index];
        test.m_panelState[index].sentinel = IntegrationTestStateSentinelBase + index;
        return &test.m_panelState[index];
    }

    void NativeUiIntegrationTestHarness::DestroyPanelState(void* const state, void* const userData) noexcept
    {
        auto& test = *static_cast<NativeUiIntegrationTestHarness*>(userData);
        for (u32 index = 0; index < 2; ++index)
        {
            if (state != &test.m_panelState[index])
                continue;
            ++test.m_panelDestroyCalls[index];
            return;
        }
    }

    void NativeUiIntegrationTestHarness::Draw(EditorPanelContext& context, void* const panelState, void* userData) noexcept
    {
        auto& test = *static_cast<NativeUiIntegrationTestHarness*>(userData);
        const bool detached = context.GetInstanceId() == 2;
        const u32 panelIndex = detached ? 1u : 0u;
        const bool stateValid = panelState == &test.m_panelState[panelIndex] &&
                                test.m_panelState[panelIndex].sentinel == IntegrationTestStateSentinelBase + panelIndex;
        test.m_panelStateObserved[panelIndex] = test.m_panelStateObserved[panelIndex] || stateValid;
        if (detached && test.m_reopenRequested && stateValid && test.m_panelCreateCalls[panelIndex] == 1u &&
            test.m_panelDestroyCalls[panelIndex] == 0u)
            test.m_reopenStatePreserved = true;
        if (!test.m_interactive && detached && test.m_frames == 40)
            ImGui::SetWindowSize({560, 420});
        ImGui::TextUnformatted("Vanguard UI integration test: drag tabs to dock/redock.");
        ImGui::Text("Frame %u; native hosts observed: %u", test.m_frames, test.m_maxHosts);
        if (test.m_interactive)
        {
            ImGui::InputText("Keyboard / clipboard", test.m_text[panelIndex], sizeof(test.m_text[panelIndex]));
            const ImVec2 textMin = ImGui::GetItemRectMin();
            const ImVec2 textMax = ImGui::GetItemRectMax();
            if (ImGui::Button("Click test")) ++test.m_clicks[panelIndex];
            const ImVec2 clickMin = ImGui::GetItemRectMin();
            const ImVec2 clickMax = ImGui::GetItemRectMax();
            ImGui::Text("Clicks: %u", test.m_clicks[panelIndex]);

            if (test.m_automation)
            {
                InteractionTelemetry telemetry;
                ImGuiWindow* const window = ImGui::GetCurrentWindow();
                ImGuiViewport* const viewport = ImGui::GetWindowViewport();
                telemetry.valid = window != nullptr && viewport != nullptr;
                telemetry.docked = ImGui::IsWindowDocked();
                telemetry.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                telemetry.viewport = viewport != nullptr ? viewport->ID : 0u;
                telemetry.dpiScale = viewport != nullptr ? viewport->DpiScale : 1.0f;
                if (window != nullptr)
                {
                    telemetry.windowX = window->Pos.x;
                    telemetry.windowY = window->Pos.y;
                    telemetry.windowWidth = window->SizeFull.x;
                    telemetry.windowHeight = window->SizeFull.y;
                    const ImRect title = window->TitleBarRect();
                    telemetry.dragX = (title.Min.x + title.Max.x) * 0.5f;
                    telemetry.dragY = (title.Min.y + title.Max.y) * 0.5f;
                    if (window->DockNode != nullptr && window->DockNode->TabBar != nullptr)
                    {
                        ImGuiTabBar* const tabBar = window->DockNode->TabBar;
                        for (const ImGuiTabItem& tab : tabBar->Tabs)
                            if (tab.Window == window)
                            {
                                telemetry.dragX =
                                    tabBar->BarRect.Min.x + tab.Offset - tabBar->ScrollingAnim + tab.Width * 0.5f;
                                telemetry.dragY = (tabBar->BarRect.Min.y + tabBar->BarRect.Max.y) * 0.5f;
                                break;
                            }
                    }
                }
                telemetry.textX = (textMin.x + textMax.x) * 0.5f;
                telemetry.textY = (textMin.y + textMax.y) * 0.5f;
                telemetry.clickX = (clickMin.x + clickMax.x) * 0.5f;
                telemetry.clickY = (clickMin.y + clickMax.y) * 0.5f;

                const InteractionTelemetry previous = test.m_telemetry[panelIndex];
                if (previous.valid)
                {
                    const f32 dpiDelta = telemetry.dpiScale - previous.dpiScale;
                    test.m_dpiTransitionObserved =
                        test.m_dpiTransitionObserved || dpiDelta < -0.01f || dpiDelta > 0.01f;
                }
                if (panelIndex == 0u)
                {
                    if (telemetry.docked)
                    {
                        test.m_dockedSeen = true;
                        if (test.m_detachedAfterDock)
                            test.m_redockedAfterDetach = true;
                    }
                    else if (test.m_dockedSeen)
                    {
                        test.m_detachedAfterDock = true;
                    }
                }
                test.m_textObserved[panelIndex] = test.m_textObserved[panelIndex] || test.m_text[panelIndex][0] != '\0';
                test.m_clickObserved[panelIndex] = test.m_clickObserved[panelIndex] || test.m_clicks[panelIndex] != 0u;
                test.LogTelemetry(panelIndex, telemetry);
                test.m_telemetry[panelIndex] = telemetry;
            }
        }
        ImGui::Image(detail::ToImGuiTexture(test.m_image), {180, 120});
        ImGui::BeginChild("clipped content", {0, 100}, true);
        for (u32 index = 0; index < 32; ++index) ImGui::Text("Clipped row %u", index);
        ImGui::EndChild();
    }

    bool NativeUiIntegrationTestHarness::Tick(bool& finished) noexcept
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
            if (m_automation && (m_frames % 15u) == 0u)
            {
                const EditorPanelFocusSnapshot focus = m_ui->GetPanelFocus();
                VG_LOG_INFO(diagnostics::Category::Engine,
                            "Editor native UI test focus: frame=%u hosts=%u panel=%u:%u type=%llu instance=%llu host=%u:%u",
                            m_frames, hosts, focus.panel.index, focus.panel.generation,
                            static_cast<unsigned long long>(focus.type),
                            static_cast<unsigned long long>(focus.instance),
                            focus.host.index, focus.host.generation);
            }
            if (m_automation)
            {
                ImGui::SetCurrentContext(ImGui::GetCurrentContext());
                ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
                bool allMinimized = platform.Viewports.Size > 0;
                for (ImGuiViewport* const viewport : platform.Viewports)
                    allMinimized = allMinimized && viewport != nullptr &&
                                   (viewport->Flags & ImGuiViewportFlags_IsMinimized) != 0;
                ImFontAtlas* const atlas = ImGui::GetIO().Fonts;
                ImTextureData* const atlasTexture = atlas != nullptr ? atlas->TexData : nullptr;
                if (allMinimized && !m_atlasTriggered && atlas != nullptr && atlasTexture != nullptr &&
                    atlasTexture->Status == ImTextureStatus_OK)
                {
                    ImFontAtlasRect rect;
                    const ImFontAtlasRectId id = atlas->AddCustomRect(4, 4, &rect);
                    m_atlasTriggered = id != ImFontAtlasRectId_Invalid;
                    m_atlasQueuedWhileMinimized =
                        m_atlasTriggered && atlas->TexData != nullptr &&
                        atlas->TexData->Status == ImTextureStatus_WantUpdates;
                    VG_LOG_INFO(diagnostics::Category::Engine,
                                "Editor native UI test atlas minimized trigger: frame=%u allMinimized=%u queued=%u status=%u",
                                m_frames, u32(allMinimized), u32(m_atlasQueuedWhileMinimized),
                                atlas->TexData != nullptr ? u32(atlas->TexData->Status) : ~u32{0});
                    diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
                }
                else if (allMinimized && m_atlasQueuedWhileMinimized && !m_atlasAcceptedWhileMinimized &&
                         atlasTexture != nullptr && atlasTexture->Status == ImTextureStatus_OK)
                {
                    m_atlasAcceptedWhileMinimized = true;
                    VG_LOG_INFO(diagnostics::Category::Engine,
                                "Editor native UI test atlas minimized accepted: frame=%u allMinimized=1 status=%u",
                                m_frames, u32(atlasTexture->Status));
                    diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
                }
            }
            if (!ObserveSurfaceTelemetry())
                return false;
            ++m_frames;
            return true;
        }
        if (!TickActivityProof())
            return false;
        // Test-only observation boundary; production host submission remains asynchronous.
        if (!m_rendering->GetCommands().FlushPreviousFrameProcessing()) return false;
        rendering::RenderCommandFailure failure;
        if (m_rendering->GetCommands().ConsumeExecutionFailure(failure)) return false;
        m_ui->VisitHosts([](const EditorHostWindowInfo& host, void* data) noexcept
        {
            auto& test = *static_cast<NativeUiIntegrationTestHarness*>(data);
            const auto* viewport = test.m_rendering->GetViewports().Resolve(host.renderViewport);
            if (viewport == nullptr) return;
            if (host.primary) test.m_primaryPresented |= viewport->GetPresentedFrameCount() != 0;
            else
            {
                test.m_detachedPresented |= viewport->GetPresentedFrameCount() != 0;
                if (!test.m_detachedExtent.IsValid()) test.m_detachedExtent = viewport->GetOutputExtent();
                else test.m_resized |= test.m_detachedExtent != viewport->GetOutputExtent();
            }
        }, this);
        u32 hosts = 0;
        m_ui->VisitHosts([](const EditorHostWindowInfo&, void* count) noexcept { ++*static_cast<u32*>(count); }, &hosts);
        if (hosts > m_maxHosts) m_maxHosts = hosts;
        ++m_frames;
        if (m_frames == 80 && !m_ui->GetPanels().SetOpen(m_panels[1], false)) return false;
        if (m_frames == 81)
        {
            m_closedFrameObserved = !m_ui->GetPanels().IsOpen(m_panels[1]) && m_panelCreateCalls[1] == 1u &&
                                    m_panelDestroyCalls[1] == 0u && m_panelStateObserved[1];
            if (!m_closedFrameObserved || !m_ui->GetPanels().SetOpen(m_panels[1], true)) return false;
            m_reopenRequested = true;
        }
        if (m_frames == 100)
        {
            if (!m_reopenStatePreserved || !m_ui->GetPanels().SetOpen(m_panels[1], false)) return false;
        }
        if (m_frames < 120) return true;
        finished = true;
        const bool passed = m_maxHosts >= 2 && hosts == 1 && m_primaryPresented && m_detachedPresented && m_resized &&
                            m_closedFrameObserved && m_reopenStatePreserved &&
                            m_panelCreateCalls[1] == 1u && m_panelDestroyCalls[1] == 0u &&
                            m_activityProofComplete;
        VG_LOG_INFO(diagnostics::Category::Engine,
                    "Editor native UI lifecycle test: frames=%u peakHosts=%u finalHosts=%u primaryPresent=%u detachedPresent=%u resized=%u closeReopen=%u activity=%u lifecycle=%s (visual checks remain manual)",
                    m_frames, m_maxHosts, hosts, u32(m_primaryPresented), u32(m_detachedPresented), u32(m_resized),
                    u32(m_closedFrameObserved && m_reopenStatePreserved), u32(m_activityProofComplete),
                    passed ? "PASS" : "FAIL");
        return passed;
    }

    void NativeUiIntegrationTestHarness::Stop() noexcept
    {
#if defined(_WIN32)
        if (m_captureHeartbeat != nullptr) { CloseHandle(m_captureHeartbeat); m_captureHeartbeat = nullptr; }
#endif
        if (m_automation)
        {
            VG_LOG_INFO(
                diagnostics::Category::Engine,
                "Editor native UI test interaction summary: dockCycle=%u text0=%u text1=%u click0=%u click1=%u clicks0=%u clicks1=%u dpiTransition=%u state0=%u state1=%u hostsPeak=%u",
                u32(m_dockedSeen && m_detachedAfterDock && m_redockedAfterDetach),
                u32(m_textObserved[0]), u32(m_textObserved[1]),
                u32(m_clickObserved[0]), u32(m_clickObserved[1]), m_clicks[0], m_clicks[1],
                u32(m_dpiTransitionObserved),
                u32(m_panelStateObserved[0] && m_panelCreateCalls[0] == 1u && m_panelDestroyCalls[0] == 0u),
                u32(m_panelStateObserved[1] && m_panelCreateCalls[1] == 1u && m_panelDestroyCalls[1] == 0u),
                m_maxHosts);
            VG_LOG_INFO(diagnostics::Category::Engine,
                        "Editor native UI test minimized atlas summary: triggered=%u queuedWhileMinimized=%u acceptedWhileMinimized=%u",
                        u32(m_atlasTriggered), u32(m_atlasQueuedWhileMinimized),
                        u32(m_atlasAcceptedWhileMinimized));
            diagnostics::Flush(diagnostics::FlushMode::Synchronous);
        }
        if (m_ui == nullptr) { m_activity = nullptr; return; }
        for (auto& panel : m_visualPanels) if (panel.IsValid()) { static_cast<void>(m_ui->GetPanels().DestroyPanel(panel)); panel = {}; }
        for (auto& type : m_visualPanelTypes) if (type.IsValid()) { static_cast<void>(m_ui->GetPanels().UnregisterType(type)); type = {}; }
        if (m_commandPanel.IsValid()) { static_cast<void>(m_ui->GetPanels().DestroyPanel(m_commandPanel)); m_commandPanel = {}; }
        for (auto& panel : m_panels) if (panel.IsValid()) { static_cast<void>(m_ui->GetPanels().DestroyPanel(panel)); panel = {}; }
        for (auto& type : m_panelTypes) if (type.IsValid()) { static_cast<void>(m_ui->GetPanels().UnregisterType(type)); type = {}; }
        if (m_image.IsValid()) static_cast<void>(m_ui->UnregisterTexture(m_image));
        m_image = {}; m_sampler.Reset(); m_texture.Reset(); m_ui = nullptr; m_activity = nullptr;
    }
}
