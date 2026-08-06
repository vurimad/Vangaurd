/**
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "simdScalar.h"
#include "simdVector4.h"

namespace vanguard::math::simd
{
    //////////////////////////////////////////////////////////////////////////
    // '+' Addition Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Add(Scalar& _a, const Scalar& _b, const Scalar& _c);
    RED_INLINE void Add(Vector4& _a, const Vector4& _b, const Scalar& _c);
    RED_INLINE void Add(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE void Add3(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE Scalar Add(const Scalar& _a, const Scalar& _b);
    RED_INLINE Vector4 Add(const Vector4& _a, const Scalar& _b);
    RED_INLINE Vector4 Add(const Vector4& _a, const Vector4& _b);

    RED_INLINE Vector4 Add3(const Vector4& _a, const Vector4& _b);

    RED_INLINE void SetAdd(Scalar& _a, const Scalar& _b);
    RED_INLINE void SetAdd(Vector4& _a, const Scalar& _b);
    RED_INLINE void SetAdd(Vector4& _a, const Vector4& _b);

    RED_INLINE void SetAdd3(Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // '-' Subtraction Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Sub(Scalar& _a, const Scalar& _b, const Scalar& _c);
    RED_INLINE void Sub(Vector4& _a, const Vector4& _b, const Scalar& _c);
    RED_INLINE void Sub(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE void Sub3(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE Scalar Sub(const Scalar& _a, const Scalar& _b);
    RED_INLINE Vector4 Sub(const Vector4& _a, const Scalar& _b);
    RED_INLINE Vector4 Sub(const Vector4& _a, const Vector4& _b);

    RED_INLINE Vector4 Sub3(const Vector4& _a, const Vector4& _b);

    RED_INLINE void SetSub(Scalar& _a, const Scalar& _b);
    RED_INLINE void SetSub(Vector4& _a, const Scalar& _b);
    RED_INLINE void SetSub(Vector4& _a, const Vector4& _b);

    RED_INLINE void SetSub3(Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // '*' Multiplication Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Mul(Scalar& _a, const Scalar& _b, const Scalar& _c);
    RED_INLINE void Mul(Vector4& _a, const Vector4& _b, const Scalar& _c);
    RED_INLINE void Mul(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE void Mul3(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE Scalar Mul(const Scalar& _a, const Scalar& _b);
    RED_INLINE Vector4 Mul(const Vector4& _a, const Scalar& _b);
    RED_INLINE Vector4 Mul(const Vector4& _a, const Vector4& _b);

    RED_INLINE Vector4 Mul3(const Vector4& _a, const Vector4& _b);

    RED_INLINE void SetMul(Scalar& _a, const Scalar& _b);
    RED_INLINE void SetMul(Vector4& _a, const Scalar& _b);
    RED_INLINE void SetMul(Vector4& _a, const Vector4& _b);

    RED_INLINE void SetMul3(Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // '/' Divisor Function
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Div(Scalar& _a, const Scalar& _b, const Scalar& _c);
    RED_INLINE void Div(Vector4& _a, const Vector4& _b, const Scalar& _c);
    RED_INLINE void Div(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE void Div3(Vector4& _a, const Vector4& _b, const Vector4& _c);

    RED_INLINE Scalar Div(const Scalar& _a, const Scalar& _b);
    RED_INLINE Vector4 Div(const Vector4& _a, const Scalar& _b);
    RED_INLINE Vector4 Div(const Vector4& _a, const Vector4& _b);

    RED_INLINE Vector4 Div3(const Vector4& _a, const Vector4& _b);

    RED_INLINE void SetDiv(Scalar& _a, const Scalar& _b);
    RED_INLINE void SetDiv(Vector4& _a, const Scalar& _b);
    RED_INLINE void SetDiv(Vector4& _a, const Vector4& _b);

    RED_INLINE void SetDiv3(Vector4& _a, const Vector4& _b);
    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////
    // 'MIN/MAX' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Min(Vector4& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Vector4 Min(const Vector4& _a, const Vector4& _b);

    RED_INLINE void Max(Vector4& _a, const Vector4& _b, const Vector4& _c);
    RED_INLINE Vector4 Max(const Vector4& _a, const Vector4& _b);
} // namespace vanguard::math::simd

#include "simdVectorArithmetic.hpp"