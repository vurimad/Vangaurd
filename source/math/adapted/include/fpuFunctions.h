/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

RED_INLINE Float MDegToRad(Float deg)
{
    return (deg * RED_PI) / 180.f;
}
RED_INLINE Float MRadToDeg(Float rad)
{
    return (rad * 180.f) / RED_PI;
}

RED_INLINE Float MSqrt(Float f)
{
    return ::sqrtf(f);
}
RED_INLINE Float MRsqrt(Float f)
{
    return 1.0f / (Float)::sqrt(f);
}
RED_INLINE Float MAbs(Float f)
{
    return (Float)::fabsf(f);
}
RED_INLINE Float MCeil(Float f)
{
    return (Float)::ceil(f);
}
RED_INLINE Float MSin(Float f)
{
    return (Float)::sin(f);
}
RED_INLINE Float MCos(Float f)
{
    return (Float)::cos(f);
}
RED_INLINE Float MAsin(Float f)
{
    RED_ASSERT(vanguard::math::Abs(f) <= 1.f);
    return (Float)::asin(f);
}
RED_INLINE Float MAcos(Float f)
{
    RED_ASSERT(vanguard::math::Abs(f) <= 1.f);
    return (Float)::acos(f);
}
RED_INLINE Float MFloor(Float f)
{
    return (Float)::floor(f);
}
RED_INLINE Float MRound(Float f)
{
    return (Float)::floor(f + 0.5f);
}
RED_INLINE Double MRoundD(Double d)
{
    return (Float)::floor(d + 0.5);
}
RED_INLINE Float MFract(Float f)
{
    return f - (float)(int)f;
}
RED_INLINE Float MTan(Float f)
{
    return (Float)::tan(f);
}
RED_INLINE Float MATan(Float f)
{
    return (Float)::atan(f);
}
RED_INLINE Float MATan2(Float y, Float x)
{
    return (Float)::atan2(y, x);
}
RED_INLINE Float MPow(Float y, Float x)
{
    return ::powf(y, x);
}
RED_INLINE Float MSqr(Float x)
{
    return x * x;
}

RED_INLINE float FloatSelect(float comparand, float valueGE, float valueLT)
{
    return comparand >= 0.0f ? valueGE : valueLT;
}

RED_INLINE Float FloatAlmostEqual(Float a, Float b, Float epsilon = FLT_EPSILON)
{
    return MAbs(a - b) <= epsilon;
}

// safer version of acos - won't return invalid values if argument is out of range
RED_INLINE Float MAcos_safe(Float f)
{
    if (MAbs(f) >= 1.0f)
    {
        f = (f > 0) ? 0 : RED_PI;
        return f;
    }

    return MAcos(f);
}

// safer version asin - won't return invalid values if argument is out of range
RED_INLINE Float MAsin_safe(Float f)
{
    if (MAbs(f) >= 1.0f)
    {
        f = (f > 0) ? 0.5f * RED_PI : -0.5f * RED_PI;
        return f;
    }

    return MAsin(f);
}

RED_INLINE Float MSign(Float f)
{
    return FloatSelect(f, 1.f, -1.f);
}

RED_INLINE Int32 MLog2(Int32 val)
{
    static const Uint8 log_2[256] = {
        0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
    int l = -1;
    while (val >= 256)
    {
        l += 8;
        val >>= 8;
    }
    return l + log_2[val];
}

RED_INLINE Float MLog10(Float val)
{
    return (Float)log10(val);
}