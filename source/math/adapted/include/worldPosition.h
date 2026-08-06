/**
 * Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "fixedPoint.h"

namespace vanguard::math
{
    struct WorldPosition
    {
        using FixedPoint = vanguard::math::FixedPoint<Int32, 17>;

        WorldPosition() {}
        constexpr WorldPosition(const WorldPosition& v);
        constexpr WorldPosition(const FixedPoint& v);
        constexpr WorldPosition(const FixedPoint& vx, const FixedPoint& vy, const FixedPoint& vz);
        RED_INLINE WorldPosition(Float vx, Float vy, Float vz);
        RED_INLINE explicit WorldPosition(const Vector3& v);
        RED_INLINE explicit WorldPosition(const Vector4& v);

        RED_FORCE_INLINE Bool operator==(const WorldPosition& v) const;
        RED_FORCE_INLINE Bool operator!=(const WorldPosition& v) const;

        RED_FORCE_INLINE WorldPosition operator+(const Vector3& v) const;
        RED_FORCE_INLINE WorldPosition operator-(const Vector3& v) const;

        RED_FORCE_INLINE WorldPosition& operator+=(const Vector3& v);
        RED_FORCE_INLINE WorldPosition& operator-=(const Vector3& v);
        RED_FORCE_INLINE WorldPosition operator+(const WorldPosition& v) const;

        RED_FORCE_INLINE Vector3 operator-(const WorldPosition& v) const;
        RED_FORCE_INLINE WorldPosition operator-() const
        {
            return WorldPosition(-x, -y, -z);
        }

        RED_FORCE_INLINE Vector2 AsVector2() const;
        RED_FORCE_INLINE Vector3 AsVector3() const;
        RED_FORCE_INLINE Vector4 AsVector4() const;

        RED_INLINE Bool IsOk() const;
        RED_INLINE Bool IsAlmostZero(Float epsilon = 1e-6f) const;

        constexpr static WorldPosition ZEROS();

        FixedPoint x = FixedPoint::ZERO();
        FixedPoint y = FixedPoint::ZERO();
        FixedPoint z = FixedPoint::ZERO();
    };
} // namespace vanguard::math

#include "worldPosition.inl"