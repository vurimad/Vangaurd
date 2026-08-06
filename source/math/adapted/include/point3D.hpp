/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    constexpr Point3D::Point3D() : x(0), y(0), z(0) {}

    constexpr Point3D::Point3D(Int32 _x, Int32 _y, Int32 _z) : x(_x), y(_y), z(_z) {}

    constexpr Point3D Point3D::operator-() const
    {
        return {-x, -y, -z};
    }

    constexpr Point3D Point3D::operator+(const Point3D& p) const
    {
        return {x + p.x, y + p.y, z + p.z};
    }

    constexpr Point3D Point3D::operator-(const Point3D& p) const
    {
        return {x - p.x, y - p.y, z - p.z};
    }

    constexpr Point3D Point3D::operator*(const Float f) const
    {
        return {(Int32)(x * f), (Int32)(y * f), (Int32)(z * f)};
    }

    constexpr Point3D Point3D::operator/(const Float f) const
    {
        return {(Int32)(x / f), (Int32)(y / f), (Int32)(z / f)};
    }

    constexpr Bool Point3D::operator==(const Point3D& p) const
    {
        return x == p.x && y == p.y && z == p.z;
    }

    constexpr Bool Point3D::operator!=(const Point3D& p) const
    {
        return !operator==(p);
    }

    RED_INLINE Point3D& Point3D::operator-=(const Point3D& p)
    {
        x -= p.x;
        y -= p.y;
        z -= p.z;
        return *this;
    }

    RED_INLINE Point3D& Point3D::operator+=(const Point3D& p)
    {
        x += p.x;
        y += p.y;
        z += p.z;
        return *this;
    }

    RED_INLINE Point3D& Point3D::operator*=(const Float f)
    {
        x = (Int32)(x * f);
        y = (Int32)(y * f);
        z = (Int32)(z * f);
        return *this;
    }

    RED_INLINE Point3D& Point3D::operator/=(const Float f)
    {
        x = (Int32)(x / f);
        y = (Int32)(y / f);
        z = (Int32)(z / f);
        return *this;
    }

    RED_INLINE Uint32 Point3D::CalcHash() const
    {
        constexpr Uint32 prime1 = 73856093;
        constexpr Uint32 prime2 = 19349663;
        constexpr Uint32 prime3 = 83492791;
        const Uint32* _A = (const Uint32*)&x;
        return (_A[0] * prime1) ^ (_A[1] * prime2) ^ (_A[2] * prime3);
    }

    constexpr Point3D Point3D::ZERO()
    {
        return {0, 0, 0};
    }

} // namespace vanguard::math
