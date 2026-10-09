#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::editor::detail
{
    // UI texture registrations are revoked before their producer resources are
    // released. A frame that already captured the registration may still retain
    // it, so producer ownership is safe to drop only after a later UI BeginFrame.
    [[nodiscard]] constexpr bool CanRetireEditorUiFrameResource(const u64 capturedUiFrame,
                                                               const u64 currentUiFrame) noexcept
    {
        return capturedUiFrame != 0u && currentUiFrame > capturedUiFrame;
    }
} // namespace vanguard::editor::detail
