/**
 * Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace vanguard::math
{
    constexpr WorldPosition::WorldPosition(const WorldPosition& v) : x(v.x), y(v.y), z(v.z) {}

    constexpr WorldPosition::WorldPosition(const FixedPoint& v) : x(v), y(v), z(v) {}

    constexpr WorldPosition::WorldPosition(const FixedPoint& vx, const FixedPoint& vy, const FixedPoint& vz) : x(vx), y(vy), z(vz) {}

    RED_INLINE WorldPosition::WorldPosition(Float vx, Float vy, Float vz) : x(vx), y(vy), z(vz) {}

    RED_INLINE WorldPosition::WorldPosition(const Vector3& v) : x(v.X), y(v.Y), z(v.Z) {}

    RED_INLINE WorldPosition::WorldPosition(const Vector4& v) : x(v.X), y(v.Y), z(v.Z) {}

    RED_FORCE_INLINE Bool WorldPosition::operator==(const WorldPosition& v) const
    {
        return x == v.x && y == v.y && z == v.z;
    }
    RED_FORCE_INLINE Bool WorldPosition::operator!=(const WorldPosition& v) const
    {
        return !operator==(v);
    }
    RED_FORCE_INLINE WorldPosition WorldPosition::operator+(const Vector3& v) const
    {
        return WorldPosition(x + v.X, y + v.Y, z + v.Z);
    }
    RED_FORCE_INLINE WorldPosition WorldPosition::operator-(const Vector3& v) const
    {
        return WorldPosition(x - v.X, y - v.Y, z - v.Z);
    }
    RED_FORCE_INLINE WorldPosition& WorldPosition::operator+=(const Vector3& v)
    {
        x += v.X;
        y += v.Y;
        z += v.Z;
        return *this;
    }
    RED_FORCE_INLINE WorldPosition& WorldPosition::operator-=(const Vector3& v)
    {
        x -= v.X;
        y -= v.Y;
        z -= v.Z;
        return *this;
    }
    RED_FORCE_INLINE WorldPosition WorldPosition::operator+(const WorldPosition& v) const
    {
        return WorldPosition(x + v.x, y + v.y, z + v.z);
    }
    RED_FORCE_INLINE Vector3 WorldPosition::operator-(const WorldPosition& v) const
    {
        return Vector3((x - v.x).AsFloat(), (y - v.y).AsFloat(), (z - v.z).AsFloat());
    }
    RED_FORCE_INLINE Vector2 WorldPosition::AsVector2() const
    {
        return Vector2(x.AsFloat(), y.AsFloat());
    }
    RED_FORCE_INLINE Vector3 WorldPosition::AsVector3() const
    {
        return Vector3(x.AsFloat(), y.AsFloat(), z.AsFloat());
    }

    RED_FORCE_INLINE Vector4 WorldPosition::AsVector4() const
    {
        return Vector4(x.AsFloat(), y.AsFloat(), z.AsFloat(), 1.f);
    }

    RED_INLINE Bool WorldPosition::IsOk() const
    {
        return AsVector3().IsOk();
    }
    RED_INLINE Bool WorldPosition::IsAlmostZero(Float epsilon /*= 1e-6f */) const
    {
        return fabs(x.AsFloat()) < epsilon && fabs(y.AsFloat()) < epsilon && fabs(z.AsFloat()) < epsilon;
    }

    constexpr vanguard::math::WorldPosition WorldPosition::ZEROS()
    {
        return WorldPosition(FixedPoint::ZERO());
    }
} // namespace vanguard::math