#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::rendering
{
    enum class FrameGpuTimingPoll : u8
    {
        Empty,
        Pending,
        Ready,
        Failed
    };

    struct FrameGpuTimingSample
    {
        u64 frameSerial = 0;
        u64 geometryWorkNanoseconds = 0;
        u64 cameraGeometryNanoseconds = 0;
        u32 cameraGeometrySamples = 0;
        u32 droppedScopes = 0;
    };
} // namespace vanguard::rendering
