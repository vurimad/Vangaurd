/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#pragma once

namespace vanguard::math
{
    RED_FORCE_INLINE Vector3::Vector3(const Float f) : X(f), Y(f), Z(f) {}

    RED_FORCE_INLINE Vector3::Vector3(const Float x, const Float y, const Float z) : X(x), Y(y), Z(z) {}

    RED_FORCE_INLINE Vector3::Vector3(const Vector3& v) : X(v.X), Y(v.Y), Z(v.Z) {}

    RED_FORCE_INLINE Vector3::Vector3(const Vector2& v) : X(v.X), Y(v.Y), Z(0.f) {}

    RED_FORCE_INLINE Vector3::Vector3(const Vector4& v) : X(v.X), Y(v.Y), Z(v.Z) {}

    RED_FORCE_INLINE Vector3::Vector3(const Float f[3]) : X(f[0]), Y(f[1]), Z(f[2]) {}

    RED_FORCE_INLINE void Vector3::Set(const Float x, const Float y, const Float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    RED_FORCE_INLINE Vector3& Vector3::operator=(const Vector2& v)
    {
        Set(v.X, v.Y, 0.f);
        return *this;
    }

    RED_FORCE_INLINE Vector3& Vector3::operator=(const Vector3& v)
    {
        Set(v.X, v.Y, v.Z);
        return *this;
    }

    RED_FORCE_INLINE Vector3& Vector3::operator=(const Vector4& v)
    {
        Set(v.X, v.Y, v.Z);
        return *this;
    }

    RED_FORCE_INLINE Vector3& Vector3::operator=(const Float f)
    {
        Set(f, f, f);
        return *this;
    }

    RED_INLINE Vector3 Vector3::operator-() const
    {
        return {-X, -Y, -Z};
    }

    RED_INLINE Vector3 Vector3::operator+(const Vector3& v) const
    {
        return {X + v.X, Y + v.Y, Z + v.Z};
    }

    RED_INLINE Vector3 Vector3::operator-(const Vector3& v) const
    {
        return {X - v.X, Y - v.Y, Z - v.Z};
    }

    RED_INLINE Vector3 Vector3::operator*(const Float scale) const
    {
        return {X * scale, Y * scale, Z * scale};
    }

    RED_INLINE Vector3 Vector3::operator*(const Vector3& v) const
    {
        return {X * v.X, Y * v.Y, Z * v.Z};
    }

    RED_INLINE Vector3 Vector3::operator/(const Float scale) const
    {
        return {X / scale, Y / scale, Z / scale};
    }

    RED_INLINE Vector3 Vector3::operator/(const Vector3& v) const
    {
        return {X / v.X, Y / v.Y, Z / v.Z};
    }

    RED_INLINE Vector3& Vector3::operator+=(const Vector3& v)
    {
        X += v.X;
        Y += v.Y;
        Z += v.Z;
        return *this;
    }

    RED_INLINE Vector3& Vector3::operator-=(const Vector3& v)
    {
        X -= v.X;
        Y -= v.Y;
        Z -= v.Z;
        return *this;
    }

    RED_INLINE Vector3& Vector3::operator*=(const Vector3& v)
    {
        X *= v.X;
        Y *= v.Y;
        Z *= v.Z;
        return *this;
    }

    RED_INLINE Vector3& Vector3::operator/=(const Vector3& v)
    {
        X /= v.X;
        Y /= v.Y;
        Z /= v.Z;
        return *this;
    }

    RED_INLINE Vector3& Vector3::operator*=(Float scale)
    {
        X *= scale;
        Y *= scale;
        Z *= scale;
        return *this;
    }

    RED_INLINE Vector3& Vector3::operator/=(Float scale)
    {
        X /= scale;
        Y /= scale;
        Z /= scale;
        return *this;
    }

    RED_INLINE Bool Vector3::operator==(const Vector3& v) const
    {
        return X == v.X && Y == v.Y && Z == v.Z;
    }

    RED_FORCE_INLINE Bool Vector3::operator!=(const Vector3& v) const
    {
        return !operator==(v);
    }

    RED_INLINE Bool Vector3::operator<(const Vector3& v) const
    {
        if (X == v.X)
        {
            if (Y == v.Y)
            {
                return Z < v.Z;
            }
            return Y < v.Y;
        }
        return X < v.X;
    }

    RED_INLINE Float Vector3::operator[](const size_t index) const
    {
        RED_FATAL_ASSERT(index < 3, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Float& Vector3::operator[](const size_t index)
    {
        RED_FATAL_ASSERT(index < 3, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Bool Vector3::IsOk() const
    {
        return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
    }

    RED_INLINE Bool Vector3::IsZero() const
    {
        return X == 0.f && Y == 0.f && Z == 0.f;
    }

    RED_INLINE Bool Vector3::IsAlmostZero(Float epsilon) const
    {
        return fabs(X) < epsilon && fabs(Y) < epsilon && fabs(Z) < epsilon;
    }

    RED_INLINE Float Vector3::Mag() const
    {
        return ::sqrtf(X * X + Y * Y + Z * Z);
    }

    RED_INLINE Float Vector3::SquareMag() const
    {
        return X * X + Y * Y + Z * Z;
    }

    RED_INLINE Float Vector3::Normalize()
    {
        // for big enough numbers (mag of 18) the normalized result can still be 0,0,0 and len will be infinite

        Float len = ::sqrtf(X * X + Y * Y + Z * Z);
        if (len != 0)
        {
            Float oolen = 1.0f / len;

            RED_ASSERT(oolen != 0);

            X *= oolen;
            Y *= oolen;
            Z *= oolen;
        }
        return len;
    }

    RED_FORCE_INLINE Vector3 Vector3::Normalized() const
    {
        auto ret = *this;
        ret.Normalize();
        return ret;
    }

    RED_FORCE_INLINE Vector3 Vector3::Truncated() const
    {
        return {
            truncf(X),
            truncf(Y),
            truncf(Z),
        };
    }

    RED_INLINE Float Vector3::DistanceTo(const Vector3& other) const
    {
        Vector3 delta = *this - other;
        return delta.Mag();
    }

    RED_INLINE Float Vector3::DistanceSquaredTo(const Vector3& other) const
    {
        Vector3 delta = *this - other;
        return delta.SquareMag();
    }

    RED_INLINE Float Vector3::Dot(const Vector3& v) const
    {
        return X * v.X + Y * v.Y + Z * v.Z;
    }

    RED_INLINE Vector3 Vector3::Cross(const Vector3& v) const
    {
        return {Y * v.Z - Z * v.Y, Z * v.X - X * v.Z, X * v.Y - Y * v.X};
    }

    RED_INLINE Float Vector3::Pitch() const
    {
        return (!X && !Y) ? ((Z > 0) ? 90.0f : -90.0f) : RAD2DEG(atan2f(-Z, sqrtf(X * X + Y * Y)));
    }

    RED_INLINE Float Vector3::Yaw() const
    {
        if (fabsf(X) < 0.0001f && fabsf(Y) < 0.0001f)
            return 0.0f;

        return RAD2DEG(-atan2f(X, Y));
    }

    RED_INLINE Vector3 Vector3::Abs() const
    {
        return {::fabsf(X), ::fabsf(Y), ::fabsf(Z)};
    }

    RED_INLINE Vector3 Vector3::Min(const Vector3& v1, const Vector3& v2)
    {
        return {vanguard::math::Min(v1.X, v2.X), vanguard::math::Min(v1.Y, v2.Y), vanguard::math::Min(v1.Z, v2.Z)};
    }

    RED_INLINE Vector3 Vector3::Max(const Vector3& v1, const Vector3& v2)
    {
        return {vanguard::math::Max(v1.X, v2.X), vanguard::math::Max(v1.Y, v2.Y), vanguard::math::Max(v1.Z, v2.Z)};
    }

    RED_INLINE Bool Vector3::Near3(const Vector3& a, const Vector3& b, Float eps /*=1e-3f*/)
    {
        return fabsf(a.X - b.X) < eps && fabsf(a.Y - b.Y) < eps && fabsf(a.Z - b.Z) < eps;
    }

    RED_FORCE_INLINE Vector2& Vector3::AsVector2()
    {
        return reinterpret_cast<Vector2&>(*this);
    }

    RED_FORCE_INLINE const Vector2& Vector3::AsVector2() const
    {
        return reinterpret_cast<const Vector2&>(*this);
    }

    RED_INLINE Vector3 Vector3::EX()
    {
        return {1.f, 0.f, 0.f};
    }

    RED_INLINE Vector3 Vector3::EY()
    {
        return {0.f, 1.f, 0.f};
    }

    RED_INLINE Vector3 Vector3::EZ()
    {
        return {0.f, 0.f, 1.f};
    }

    RED_INLINE Vector3 Vector3::ONES()
    {
        return {1.f, 1.f, 1.f};
    }

    RED_INLINE Vector3 Vector3::ZEROS()
    {
        return {0.f, 0.f, 0.f};
    }

    RED_INLINE Vector3 Vector3::PLUS_MAX()
    {
        return {std::numeric_limits<Float>::max(), std::numeric_limits<Float>::max(), std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector3 Vector3::MINUS_MAX()
    {
        return {-std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector3 Vector3::PLUS_INF()
    {
        return {std::numeric_limits<Float>::infinity(), std::numeric_limits<Float>::infinity(), std::numeric_limits<Float>::infinity()};
    }

    RED_INLINE Vector3 Vector3::MINUS_INF()
    {
        return {-std::numeric_limits<Float>::infinity(), -std::numeric_limits<Float>::infinity(), -std::numeric_limits<Float>::infinity()};
    }
} // namespace vanguard::math