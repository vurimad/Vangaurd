#pragma once

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::engine
{
    class RenderSceneService : public application::Service
    {
    public:
        ~RenderSceneService() override = default;

        [[nodiscard]] virtual rendering::RenderSceneManager& Scenes() noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderSceneManager& Scenes() const noexcept = 0;

    protected:
        RenderSceneService() noexcept = default;
    };

    [[nodiscard]] RenderSceneService* FindRenderSceneService(application::EngineHost& host) noexcept;
    [[nodiscard]] RenderSceneService* FindRenderSceneService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
