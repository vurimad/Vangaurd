/**
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../imported/common/redSystem/include/redSystemPublic.h"
#include "../../../imported/common/redSystem/include/hash.h"

#include "redMathApi.h"

static constexpr Float RED_PI = 3.14159265358979323846264338327f;
static constexpr Float RED_PI_TWO = 6.283185307179f;
static constexpr Float RED_PI_HALF = 1.570796326794896619231f;
static constexpr Float RED_FLT_MAX = std::numeric_limits<Float>::max();
static constexpr Float RED_FLT_INF = std::numeric_limits<Float>::infinity();
static constexpr Float RED_FLT_EPSILON = std::numeric_limits<Float>::epsilon();

#if defined(RED_PLATFORM_WINPC) && !defined(RED_CONFIGURATION_FINAL)
#define RED_MATH_ASSERT(x) RED_FATAL_ASSERT(x, "Unexpected value in computation")
#define RED_MATH_CHECK_SIMD_ALIGNMENT(x) RED_FATAL_ASSERT(((ptrdiff_t)((float*)x) & 0xF) == 0, "Error: Data isn't SIMD aligned")
#else
#define RED_MATH_ASSERT(x)
#define RED_MATH_CHECK_SIMD_ALIGNMENT(x)
#endif

// use this macro to enable additional sanity checks in structs like Transform, WorldTransform, vanguard::math::simd::QsTransform
// #define RED_MATH_USE_PARANOID_SANITY_CHECK
#ifdef RED_MATH_USE_PARANOID_SANITY_CHECK
#define RED_MATH_PARANOID_SANITY_CHECK(x)                                                                                                                      \
    {                                                                                                                                                          \
        RED_MATH_ASSERT(x);                                                                                                                                    \
    }
#else
#define RED_MATH_PARANOID_SANITY_CHECK(x)
#endif

constexpr Float DEG2RAD(const Float x)
{
    return ((x / 180.f) * RED_PI);
}

constexpr Double DEG2RAD(const Double x)
{
    return ((x / 180) * (Double)RED_PI);
}

constexpr Float RAD2DEG(const Float x)
{
    return ((x / RED_PI) * 180.0f);
}

constexpr Double RAD2DEG(const Double x)
{
    return ((x / (Double)RED_PI) * 180.0);
}

constexpr Float DEG2RAD_HALF(const Float x)
{
    return ((x * RED_PI) / 360.0f);
}

constexpr Double DEG2RAD_HALF(const Double x)
{
    return ((x * (Double)RED_PI) / 360.0);
}

// forwarded math types
namespace vanguard::math
{
    typedef red::Float Float;
    typedef red::Double Double;
    typedef red::Int32 Int32;
    typedef red::Uint32 Uint32;
    typedef red::Bool Bool;
    typedef red::Uint8 Uint8;
} // namespace vanguard::math

//-----------------------------------------------------------------

// common functions - max, min, abs, etc
#include "numericalUtils.h"
#include "fpuFunctions.h"

//-----------------------------------------------------------------

// basic types - headers
#include "vector4.h"
#include "vector3.h"
#include "vector2.h"
#include "eulerAngles.h"
#include "quaternion.h"
#include "transform.h"
#include "matrix.h"

// basic types - inline implementations
#include "vector3.hpp"
#include "vector2.hpp"
#include "vector4.hpp"
#include "quaternion.hpp"
#include "matrix.hpp"
#include "eulerAngles.hpp"
#include "transform.hpp"

// color, rect, other generic stuff
#include "color.h"
#include "colorUtils.h"