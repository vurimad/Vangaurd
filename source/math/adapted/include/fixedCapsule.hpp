/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    RED_INLINE FixedCapsule::FixedCapsule(const Vector4& point, Float radius, Float height) : PointRadius(point.X, point.Y, point.Z, radius), Height(height) {}

    RED_INLINE Vector4 FixedCapsule::GetMassCenter() const
    {
        return PointRadius + Vector4{0, 0, Height * 0.5f - PointRadius.W};
    }

    RED_INLINE Float FixedCapsule::GetMass() const
    {
        return ((1.33333333f) * RED_PI * PointRadius.W * PointRadius.W * PointRadius.W) + (RED_PI * PointRadius.W * PointRadius.W * Height);
    }

    RED_INLINE void FixedCapsule::Set(const Vector4& point, Float radius, Float height)
    {
        PointRadius = point;
        PointRadius.W = radius;
        Height = height;
    }

    RED_INLINE Vector4 FixedCapsule::GetPosition() const
    {
        return PointRadius;
    }

    RED_INLINE EulerAngles FixedCapsule::GetOrientation() const
    {
        return EulerAngles::ZEROS();
    }

    RED_INLINE Vector4 FixedCapsule::CalcPointA() const
    {
        return {PointRadius.X, PointRadius.Y, PointRadius.Z + GetRadius(), PointRadius.W};
    }

    RED_INLINE Vector4 FixedCapsule::CalcPointB() const
    {
        return {PointRadius.X, PointRadius.Y, PointRadius.Z + (GetHeight() + GetRadius()), PointRadius.W};
    }

    RED_INLINE Float FixedCapsule::GetRadius() const
    {
        return PointRadius.W;
    }

    RED_INLINE Float FixedCapsule::GetHeight() const
    {
        return Height;
    }

    RED_INLINE FixedCapsule FixedCapsule::operator+(const Vector4& dir) const
    {
        return {PointRadius + dir, GetRadius(), GetHeight()};
    }

    RED_INLINE FixedCapsule FixedCapsule::operator-(const Vector4& dir) const
    {
        return {PointRadius - dir, GetRadius(), GetHeight()};
    }

    RED_INLINE FixedCapsule& FixedCapsule::operator+=(const Vector4& dir)
    {
        PointRadius[0] += dir[0];
        PointRadius[1] += dir[1];
        PointRadius[2] += dir[2];
        return *this;
    }

    RED_INLINE FixedCapsule& FixedCapsule::operator-=(const Vector4& dir)
    {
        PointRadius[0] -= dir[0];
        PointRadius[1] -= dir[1];
        PointRadius[2] -= dir[2];
        return *this;
    }

} // namespace vanguard::math