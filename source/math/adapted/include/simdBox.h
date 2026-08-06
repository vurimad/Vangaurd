#pragma once

#include "simdQuad.h"

namespace vanguard::math::simd
{
    // TODO: merge with vanguard::math::Box class ASAP (after E3 Demo 2018)
    struct Box
    {
        Quad min;
        Quad max;
    };

    inline Box ToSimd(const vanguard::math::Box& scalar)
    {
        Box result;
        result.min = _mm_load_ps(scalar.Min.AsFloat());
        result.max = _mm_load_ps(scalar.Max.AsFloat());

        return result;
    }

    inline vanguard::math::Box ToScalar(const vanguard::math::simd::Box& box)
    {
        vanguard::math::Box result;
        result.Min = vanguard::math::Vector4((const Float*)&box.min);
        result.Max = vanguard::math::Vector4((const Float*)&box.max);
        return result;
    }

    inline Box InitBox(const Quad& pos, const Quad& radius)
    {
        Box box;
        box.min = _mm_sub_ps(pos, radius);
        box.max = _mm_add_ps(pos, radius);
        return box;
    }

    inline Box AddBox(const vanguard::math::simd::Box& a, const vanguard::math::simd::Box& b)
    {
        Box box;
        box.min = _mm_min_ps(a.min, b.min);
        box.max = _mm_max_ps(a.max, b.max);
        return box;
    }

} // namespace vanguard::math::simd
