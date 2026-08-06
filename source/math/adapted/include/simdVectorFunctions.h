/**
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "simdVectorArithmetic.h"

namespace vanguard::math::simd
{
    //////////////////////////////////////////////////////////////////////////
    // 'SQUARE ROOT' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void SqrRoot(Scalar& _a);
    RED_INLINE Scalar SqrRoot(const Scalar& _a);
    RED_INLINE void SqrRoot(Vector4& _a);
    RED_INLINE Vector4 SqrRoot(const Vector4& _a);

    RED_INLINE void SqrRoot3(Vector4& _a);
    RED_INLINE Vector4 SqrRoot3(const Vector4& _a);
    //////////////////////////////////////////////////////////////////////////
    // 'RECIPROCAL SQUARE ROOT' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void RSqrRoot(Scalar& _a);
    RED_INLINE Scalar RSqrRoot(const Scalar& _a);
    RED_INLINE void RSqrRoot(Vector4& _a);
    RED_INLINE Vector4 RSqrRoot(const Vector4& _a);

    RED_INLINE void RSqrRoot3(Vector4& _a);
    RED_INLINE Vector4 RSqrRoot3(const Vector4& _a);

    // rsqrt improved by performing a single Newton-Raphson iteration
    RED_FORCE_INLINE __m128 RcpSqrtPrecise(const __m128& v);
    //////////////////////////////////////////////////////////////////////////
    // 'DOT PRODUCT' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Dot(Scalar& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Scalar Dot(const Vector4& _a, const Vector4& _b);

    RED_INLINE void Dot3(Scalar& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Scalar Dot3(const Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////
    // 'NORMALIZED DOT PRODUCT' Functions
    // Ensure the input vectors are of unit length.
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void UnitDot(Scalar& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Scalar UnitDot(const Vector4& _a, const Vector4& _b);

    RED_INLINE void UnitDot3(Scalar& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Scalar UnitDot3(const Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////
    // 'CROSS PRODUCT' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Cross(Vector4& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Vector4 Cross(const Vector4& _a, const Vector4& _b);

    //////////////////////////////////////////////////////////////////////////
    // 'MISC' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void CalculatePerpendicularVector(const Vector4& _in, Vector4& _out);
    RED_INLINE void AxisRotateVector(Vector4& vec, const Vector4& normAxis, float angle);
} // namespace vanguard::math::simd

// functions to compute data in SoA (Struct of Arrays) format
namespace vanguard::math::simd
{
    RED_FORCE_INLINE __m128 Lerp(const __m128& a, const __m128& b, const __m128& t);
    RED_FORCE_INLINE __m128 MulAdd(const __m128& a, const __m128& b, const __m128& c);

    RED_FORCE_INLINE __m128 Dot(const __m128& xxxx, const __m128& yyyy, const __m128& zzzz, const __m128& xxxx1, const __m128& yyyy1,
                                const __m128& zzzz1);

    RED_FORCE_INLINE __m128 Dot(const __m128& xxxx, const __m128& yyyy, const __m128& zzzz, const __m128& wwww, const __m128& xxxx1,
                                const __m128& yyyy1, const __m128& zzzz1, const __m128& wwww1);

    RED_FORCE_INLINE void Normalize(__m128* outXXXX, __m128* outYYYY, __m128* outZZZZ, __m128* outWWWW, const __m128& xxxx,
                                    const __m128& yyyy, const __m128& zzzz, const __m128& wwww);

    // this could be used as an interpolation function for quaternions.
    // ex. in animation sampling
    RED_FORCE_INLINE void QuatNLerp(__m128* outXXXX, __m128* outYYYY, __m128* outZZZZ, __m128* outWWWW, const __m128& xxxx,
                                    const __m128& yyyy, const __m128& zzzz, const __m128& wwww, const __m128& xxxx1, const __m128& yyyy1,
                                    const __m128& zzzz1, const __m128& wwww1, const __m128& t);

    // functions to write computation result to data in AoS (Array of Structs) format
    RED_FORCE_INLINE void Scatter3(::vanguard::math::simd::Vector4* output0, ::vanguard::math::simd::Vector4* output1,
                                   ::vanguard::math::simd::Vector4* output2, ::vanguard::math::simd::Vector4* output3,
                                   const __m128 input[3], const __m128& w);
    RED_FORCE_INLINE void Scatter4(::vanguard::math::simd::Vector4* output0, ::vanguard::math::simd::Vector4* output1,
                                   ::vanguard::math::simd::Vector4* output2, ::vanguard::math::simd::Vector4* output3,
                                   const __m128 input[4]);
} // namespace vanguard::math::simd

#include "simdVectorFunctions.hpp"