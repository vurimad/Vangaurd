/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "colorUtils.h"

namespace vanguard::math
{
    Vector3 REDMATH_API HSVToRGB(const Float h, const Float s, const Float v)
    {
        const Int32 h_i = (Int32)(h * 6.0f);
        const Float f = h * 6.0f - h_i;
        const Float p = v * (1.0f - s);
        const Float q = v * (1.0f - f * s);
        const Float t = v * (1.0f - (1.0f - f) * s);
        if (h_i == 0)
            return Vector3(v, t, p);
        if (h_i == 1)
            return Vector3(q, v, p);
        if (h_i == 2)
            return Vector3(p, v, t);
        if (h_i == 3)
            return Vector3(p, q, v);
        if (h_i == 4)
            return Vector3(t, p, v);
        if (h_i == 5)
            return Vector3(v, p, q);
        return Vector3::ZEROS();
    }

    Color REDMATH_API ComputeDebugColorForPointer(const void* ptr)
    {
        // compute hue based on pointer value
        const Uint32 hash = red::CalculateHash32(ptr, sizeof(ptr));
        Color color(HSVToRGB((Float)hash / (Float)UINT32_MAX, 1.0f, 1.0f));
        color.A = 255; // m_alpha;
        return color;
    }

    Float Halton(Uint32 index, Uint32 base)
    {
        Uint32 i = index;
        Float f = 1;
        Float result = 0;
        while (i > 0)
        {
            f = f / base;
            result = result + f * (i % base);
            i = i / base;
        }
        return result;
    }

    Color REDMATH_API ComputeDebugColorForIndex(Uint32 index, Float saturation, Float value)
    {
        Float hue = fmodf(Halton(index, 2u), 1.0f);
        return Color(Vector4(HSVToRGB(hue, saturation, value), 1.0f));
    }
} // namespace vanguard::math