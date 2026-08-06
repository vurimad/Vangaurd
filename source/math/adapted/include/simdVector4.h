/**
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "simdComparisonResult.h"
#include "simdScalar.h"

namespace vanguard::math::simd
{
    class QsTransform;

    RED_ALIGNED_CLASS_API(Vector4, REDMATH_API, 16)
    {
    public:
        // Constructors
        RED_INLINE Vector4();
        RED_INLINE constexpr Vector4(const Scalar& _v);
        RED_INLINE constexpr Vector4(const Vector4& _v);
        RED_INLINE Vector4(const red::Float* _f);
        RED_INLINE Vector4(red::Float _x, red::Float _y, red::Float _z, red::Float _w = 0.0f);
        RED_INLINE constexpr Vector4(Quad _v);

        // Destructor
        RED_INLINE ~Vector4() = default;

        RED_INLINE const red::Float* AsFloat() const;
        RED_INLINE void Store(red::Float * _f) const;

        // Operators
        RED_INLINE Vector4& operator=(const Scalar& _v);

        // Methods
        RED_INLINE void Set(const Scalar& _v);
        RED_INLINE void Set(const Vector4& _v);
        RED_INLINE void Set(red::Float _x, red::Float _y, red::Float _z, red::Float _w = 0.0f);
        RED_INLINE void Set(const red::Float* _f);

        void SetTransformedInversePos(const QsTransform& _t, const Vector4& _v);
        RED_INLINE void RotateDirection(const vanguard::math::Quaternion& _quat, const Vector4& _direction);
        RED_INLINE void RotateDirectionUnsafe(const vanguard::math::Quaternion& _quat, const Vector4& _direction);
        RED_INLINE void InverseRotateDirection(const vanguard::math::Quaternion& _quat, const Vector4& _v);
        Vector4& SetTransformedPos(const QsTransform& _trans, const Vector4& _v);

        RED_INLINE void SetZeros();
        RED_INLINE void SetOnes();

        RED_INLINE Vector4& Negate();
        RED_INLINE Vector4 Negated() const;
        RED_INLINE Vector4 Abs() const;

        RED_INLINE Scalar Sum3() const;
        RED_INLINE Scalar Sum4() const;
        RED_INLINE Scalar Length3() const;
        RED_INLINE Scalar Length4() const;
        RED_INLINE Scalar SquareLength3() const;
        RED_INLINE Scalar SquareLength4() const;

        Scalar DistanceSquaredTo(const Vector4& _t) const;
        Scalar DistanceTo(const Vector4& _t) const;

        RED_INLINE Vector4& Normalize4();
        RED_INLINE Vector4 Normalized4() const;
        RED_INLINE Vector4& Normalize3();
        RED_INLINE Vector4 Normalized3() const;

        RED_INLINE Vector4& NormalizeFast4();
        RED_INLINE Vector4 NormalizedFast4() const;
        RED_INLINE Vector4& NormalizeFast3();
        RED_INLINE Vector4 NormalizedFast3() const;

        RED_INLINE red::Bool IsNormalized4(const Quad _epsilon = EPSILON_VALUE) const;
        RED_INLINE red::Bool IsNormalized3(const Quad _epsilon = EPSILON_VALUE) const;
        RED_INLINE red::Bool IsNormalized4(float _epsilon) const;
        RED_INLINE red::Bool IsNormalized3(float _epsilon) const;

        RED_INLINE red::Bool IsAlmostEqual(const Vector4& _v, const Quad _epsilon = EPSILON_VALUE) const;
        RED_INLINE red::Bool IsAlmostEqual(const Vector4& _v, float _epsilon) const;
        RED_INLINE red::Bool IsAlmostZero(const Quad _epsilon = EPSILON_VALUE) const;
        RED_INLINE red::Bool IsZero() const;

        RED_INLINE Scalar Upper3() const;
        RED_INLINE Scalar Upper4() const;
        RED_INLINE Scalar Lower3() const;
        RED_INLINE Scalar Lower4() const;

        RED_INLINE Vector4 ZeroElement(red::Uint32 _i) const;

        RED_INLINE Scalar AsScalar(red::Uint32 _i) const;

        RED_INLINE red::Bool IsOk() const;

        static Vector4 Lerp(const Vector4& _a, const Vector4& _b, const float _weight);
        void Lerp(const Vector4& _a, const float _weight);

        RED_INLINE static Vector4 ZEROS();
        RED_INLINE static Vector4 ZERO_3D_POINT();
        RED_INLINE static Vector4 ONES();
        RED_INLINE static Vector4 EX();
        RED_INLINE static Vector4 EY();
        RED_INLINE static Vector4 EZ();
        RED_INLINE static Vector4 EW();

        union
        {
            struct
            {
                red::Float X;
                red::Float Y;
                red::Float Z;
                red::Float W;
            };

            struct
            {
                red::Uint32 Xi;
                red::Uint32 Yi;
                red::Uint32 Zi;
                red::Uint32 Wi;
            };

            red::Float f[4];
            Quad V;
        };
    };

    RED_INLINE ComparisonResult operator==(const Vector4& a, const Vector4& b);
    RED_INLINE ComparisonResult operator!=(const Vector4& a, const Vector4& b);
    RED_INLINE ComparisonResult operator>(const Vector4& a, const Vector4& b);
    RED_INLINE ComparisonResult operator>=(const Vector4& a, const Vector4& b);
    RED_INLINE ComparisonResult operator<(const Vector4& a, const Vector4& b);
    RED_INLINE ComparisonResult operator<=(const Vector4& a, const Vector4& b);

} // namespace vanguard::math::simd

#include "simdVector4.hpp"