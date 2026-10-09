#pragma once

#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_scene_output_service.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>

namespace vanguard::editor
{
    inline constexpr engine::FrameParticipantId EditorSceneViewFrameParticipantId = 0x6573636e76667201ull;
    inline constexpr EditorPanelTypeId EditorScenePanelTypeId = 0x6573636e76707901ull;
    inline constexpr EditorPanelInstanceId EditorScenePanelInstanceId = 0x6573636e76696e01ull;

    namespace detail
    {
        inline constexpr f64 EditorSceneResizeSettleSeconds = 0.10;

        [[nodiscard]] constexpr bool ShouldCommitEditorSceneResize(const rendering::ViewportExtent requested,
                                                                   const rendering::ViewportExtent published,
                                                                   const rendering::ViewportExtent observed,
                                                                   const f64 observedSinceSeconds,
                                                                   const f64 nowSeconds) noexcept
        {
            if (!requested.IsValid() || !published.IsValid() || requested == published)
                return true;
            return requested == observed && nowSeconds >= observedSinceSeconds &&
                   nowSeconds - observedSinceSeconds >= EditorSceneResizeSettleSeconds;
        }
    } // namespace detail

    class EditorSceneViewService : public application::Service
    {
    public:
        ~EditorSceneViewService() override = default;

        /// Releases every caller-owned scene-view object that must be gone before
        /// WorldSessionService accepts stop/replacement. Main-thread only.
        [[nodiscard]] virtual bool PrepareForSessionStop() noexcept = 0;

    protected:
        EditorSceneViewService() noexcept = default;
    };

    [[nodiscard]] bool RegisterEditorSceneViewService(application::EngineHost& host,
                                                      application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorSceneViewService* FindEditorSceneViewService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorSceneViewService* FindEditorSceneViewService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
