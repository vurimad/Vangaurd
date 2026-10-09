#pragma once

#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rhi::backend
{
    enum class CommandListReuseClass : u8
    {
        Clean,
        BoundedWorkingSet
    };

    // NVRHI command-list upload and scratch managers retain their high-water working set until the
    // command list itself is destroyed. Default callers therefore destroy lists that exercised persistent
    // manager storage. BoundedUploadReuse is an explicit caller contract: that role's per-recording working
    // set has a production capacity bound, so retaining its managers inside Vanguard's bounded native
    // command-list pool cannot grow without limit.

    [[nodiscard]] constexpr bool ShouldRecycleCommandList(const bool allowReuse, const bool usedPersistentWorkingSet,
                                                          const CommandListRetentionPolicy retentionPolicy) noexcept
    {
        return allowReuse && (!usedPersistentWorkingSet || retentionPolicy == CommandListRetentionPolicy::BoundedUploadReuse);
    }
} // namespace vanguard::rhi::backend
