/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "vector3.h"
#include "color.h"

namespace vanguard::math
{

    Vector3 REDMATH_API HSVToRGB(const Float h, const Float s, const Float v);
    Color REDMATH_API ComputeDebugColorForPointer(const void* ptr);
    Color REDMATH_API ComputeDebugColorForIndex(Uint32 index, Float saturation = 0.5f, Float value = 0.95f);

} // namespace vanguard::math
