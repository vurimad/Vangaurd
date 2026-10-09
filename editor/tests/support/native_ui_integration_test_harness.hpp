#pragma once
#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_ui.hpp>
namespace vanguard::engine { class RenderingService; }

namespace vanguard::editor::testing
{
    class NativeUiIntegrationTestHarness final
    {
    public:
        bool Start(EditorUiService& ui, EditorActivityService& activity,
                   engine::RenderingService& rendering, bool interactive = false,
                   bool visualPreview = false) noexcept;
        bool Tick(bool& finished) noexcept;
        void Stop() noexcept;
    private:
        enum class ActivityProofStage : u8
        {
            WaitingForSource,
            ClosedAwaitAdvance,
            ReopenedAwaitLatest,
            CancelQueuedClosed,
            AwaitCancellingPublication,
            AwaitTerminalPublication,
            ReopenedTerminal,
            Complete
        };

        struct PanelState final
        {
            u32 sentinel = 0;
        };

        struct InteractionTelemetry final
        {
            f32 dragX = 0.0f;
            f32 dragY = 0.0f;
            f32 textX = 0.0f;
            f32 textY = 0.0f;
            f32 clickX = 0.0f;
            f32 clickY = 0.0f;
            f32 windowX = 0.0f;
            f32 windowY = 0.0f;
            f32 windowWidth = 0.0f;
            f32 windowHeight = 0.0f;
            f32 dpiScale = 1.0f;
            u32 viewport = 0;
            bool docked = false;
            bool focused = false;
            bool valid = false;
        };

        static void* CreatePanelState(EditorPanelInstanceId instance, void* userData) noexcept;
        static void DestroyPanelState(void* state, void* userData) noexcept;
        static void Draw(EditorPanelContext&, void*, void*) noexcept;
        static void DrawVisualHierarchy(EditorPanelContext&, void*, void*) noexcept;
        static void DrawVisualScene(EditorPanelContext&, void*, void*) noexcept;
        static void DrawVisualProperties(EditorPanelContext&, void*, void*) noexcept;
        static void DrawVisualProject(EditorPanelContext&, void*, void*) noexcept;
        void LogTelemetry(u32 panelIndex, const InteractionTelemetry& telemetry) noexcept;
        [[nodiscard]] bool ObserveSurfaceTelemetry() noexcept;
        [[nodiscard]] bool CaptureTestActivity(EditorActivityItem& item,
                                               EditorActivityCapture& capture,
                                               bool& found) noexcept;
        [[nodiscard]] bool TickActivityProof() noexcept;
        EditorUiService* m_ui = nullptr;
        EditorActivityService* m_activity = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        rendering::ViewportExtent m_detachedExtent;
        bool m_primaryPresented = false;
        bool m_detachedPresented = false;
        bool m_resized = false;
        bool m_interactive = false;
        bool m_visualPreview = false;
        void* m_captureHeartbeat = nullptr;
        char m_text[2][128]{};
        u32 m_clicks[2]{};
        PanelState m_panelState[2];
        u32 m_panelCreateCalls[2]{};
        u32 m_panelDestroyCalls[2]{};
        bool m_panelStateObserved[2]{};
        bool m_closedFrameObserved = false;
        bool m_reopenRequested = false;
        bool m_reopenStatePreserved = false;
        bool m_automation = false;
        bool m_dockedSeen = false;
        bool m_detachedAfterDock = false;
        bool m_redockedAfterDetach = false;
        bool m_dpiTransitionObserved = false;
        bool m_atlasTriggered = false;
        bool m_atlasQueuedWhileMinimized = false;
        bool m_atlasAcceptedWhileMinimized = false;
        bool m_textObserved[2]{};
        bool m_clickObserved[2]{};
        bool m_contextPanelClosedForCancellation = false;
        InteractionTelemetry m_telemetry[2];
        EditorPanelTypeToken m_panelTypes[2];
        EditorPanelHandle m_panels[2];
        EditorPanelTypeToken m_visualPanelTypes[4];
        EditorPanelHandle m_visualPanels[4];
        EditorPanelHandle m_commandPanel;
        EditorPanelHandle m_backgroundActivityPanel;
        EditorPanelHandle m_consolePanel;
        EditorActivitySourceSnapshot m_activitySources[MaximumEditorActivitySources]{};
        EditorActivityItem m_activityItems[MaximumEditorActivityRows]{};
        ActivityProofStage m_activityProofStage = ActivityProofStage::WaitingForSource;
        u32 m_activityProofTicks = 0;
        u64 m_activityBaselineGeneration = 0;
        u64 m_activityBaselineOperationGeneration = 0;
        u64 m_activityExpectedOperationGeneration = 0;
        u32 m_activityBaselineSamples = 0;
        bool m_activityProofComplete = false;
        EditorTextureHandle m_image;
        rhi::Texture m_texture;
        rhi::SamplerState m_sampler;
        u32 m_frames = 0;
        u32 m_maxHosts = 0;
        u32 m_contextCancellationSequence = 0;
        u32 m_contextCancellationFrame = 0;
        u32 m_lastContextPopupSequence = 0;
        u32 m_lastContextAbsentSequence = 0;
    };
}
