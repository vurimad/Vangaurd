#pragma once

#include <vanguard/engine/game_world_service.hpp>

namespace vanguard::engine::detail
{
    inline void ApplyRendererProgressFailure(const RenderingProgressFailure& renderingFailure, const char* const fallbackMessage,
                                             bool& rendererOwnersAbandoned, GameWorldStopFailure& firstFailure) noexcept
    {
        rendererOwnersAbandoned = rendererOwnersAbandoned || renderingFailure.ownersAbandoned;
        if (firstFailure.code != GameWorldStopFailureCode::None)
            return;
        firstFailure.code = GameWorldStopFailureCode::RendererProgress;
        firstFailure.message = renderingFailure.message != nullptr ? renderingFailure.message : fallbackMessage;
        firstFailure.renderingFailure = renderingFailure;
    }
} // namespace vanguard::engine::detail
