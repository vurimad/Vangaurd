#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::rendering::detail
{
    enum class RenderSceneGpuMutationPhase : u8
    {
        Serial,
        Parallel,
        Sealed,
        Publishing
    };

    [[nodiscard]] constexpr bool HasPendingRenderSceneGpuPublicationWork(const RenderSceneGpuMutationPhase phase,
                                                                        const u32 pendingChanges,
                                                                        const u32 pendingRetirements) noexcept
    {
        return phase != RenderSceneGpuMutationPhase::Serial || pendingChanges != 0 || pendingRetirements != 0;
    }
} // namespace vanguard::rendering::detail
