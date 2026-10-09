#pragma once

#include <vanguard/engine/rendering_service.hpp>

namespace vanguard::engine::detail
{
    class RenderingProgressFailureLatch final
    {
    public:
        void Reset() noexcept
        {
            m_failure = {};
            m_pending = false;
        }

        void Latch(const RenderingProgressFailure& failure) noexcept
        {
            if (m_pending || failure.code == RenderingProgressFailureCode::None)
                return;
            m_failure = failure;
            m_pending = true;
        }

        [[nodiscard]] bool Consume(RenderingProgressFailure& failure) noexcept
        {
            failure = {};
            if (!m_pending)
                return false;
            failure = m_failure;
            Reset();
            return true;
        }

    private:
        RenderingProgressFailure m_failure;
        bool m_pending = false;
    };

    [[nodiscard]] inline bool BuildDeviceUnavailableFailure(const rhi::DeviceState state,
                                                            RenderingProgressFailure& failure) noexcept
    {
        if (state != rhi::DeviceState::Removed && state != rhi::DeviceState::ResetRequired)
            return false;
        failure.code = RenderingProgressFailureCode::DeviceUnavailable;
        failure.message = "renderer device became unavailable";
        failure.ownersAbandoned = true;
        return true;
    }

    inline void BuildSubmittedReceiptCoverageFailure(RenderingProgressFailure& failure) noexcept
    {
        failure.code = RenderingProgressFailureCode::NativeResourceFailure;
        failure.message = "submitted renderer work has no complete retirement receipt";
        failure.ownersAbandoned = true;
    }
} // namespace vanguard::engine::detail
