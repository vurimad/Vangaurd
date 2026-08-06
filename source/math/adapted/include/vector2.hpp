/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#pragma once

namespace vanguard::math
{
    RED_FORCE_INLINE Vector2::Vector2(const Float x, const Float y) : X(x), Y(y) {}

    RED_FORCE_INLINE Vector2::Vector2(const Vector2& v) : X(v.X), Y(v.Y) {}

    RED_FORCE_INLINE Vector2::Vector2(const Vector3& v) : X(v.X), Y(v.Y) {}

    RED_FORCE_INLINE Vector2::Vector2(const Vector4& v) : X(v.X), Y(v.Y) {}

    RED_FORCE_INLINE Vector2::Vector2(const Float f[2]) : X(f[0]), Y(f[1]) {}

    RED_FORCE_INLINE Vector2::Vector2(Float f) : X(f), Y(f) {}

    RED_FORCE_INLINE Vector2& Vector2::operator=(const Vector2& v)
    {
        Set(v.X, v.Y);
        return *this;
    }

    RED_FORCE_INLINE void Vector2::Set(Float x, Float y)
    {
        X = x;
        Y = y;
    }

    RED_INLINE Vector2 Vector2::operator-() const
    {
        return {-X, -Y};
    }

    RED_INLINE Vector2 Vector2::operator*(const Float scale) const
    {
        return {X * scale, Y * scale};
    }

    RED_INLINE Vector2 Vector2::operator*(const Vector2& v) const
    {
        return {X * v.X, Y * v.Y};
    }

    RED_INLINE Vector2 Vector2::operator/(const Float scale) const
    {
        return {X / scale, Y / scale};
    }

    RED_INLINE Vector2 Vector2::operator+(const Vector2& v) const
    {
        return {X + v.X, Y + v.Y};
    }

    RED_INLINE Vector2 Vector2::operator-(const Vector2& v) const
    {
        return {X - v.X, Y - v.Y};
    }

    RED_INLINE Vector2& Vector2::operator+=(const Vector2& v)
    {
        X += v.X;
        Y += v.Y;
        return *this;
    }

    RED_INLINE Vector2& Vector2::operator-=(const Vector2& v)
    {
        X -= v.X;
        Y -= v.Y;
        return *this;
    }

    RED_INLINE Vector2& Vector2::operator*=(const Vector2& v)
    {
        X *= v.X;
        Y *= v.Y;
        return *this;
    }

    RED_INLINE Vector2& Vector2::operator*=(const Float scale)
    {
        X *= scale;
        Y *= scale;
        return *this;
    }

    RED_INLINE Vector2& Vector2::operator/=(const Float scale)
    {
        X /= scale;
        Y /= scale;
        return *this;
    }

    RED_INLINE Bool Vector2::operator==(const Vector2& v) const
    {
        return X == v.X && Y == v.Y;
    }

    RED_INLINE Bool Vector2::operator!=(const Vector2& v) const
    {
        return X != v.X || Y != v.Y;
    }

    RED_INLINE Float Vector2::operator[](const size_t index) const
    {
        RED_FATAL_ASSERT(index < 2, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Float& Vector2::operator[](const size_t index)
    {
        RED_FATAL_ASSERT(index < 2, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Bool Vector2::IsOk() const
    {
        return std::isfinite(X) && std::isfinite(Y);
    }

    RED_INLINE Bool Vector2::IsZero() const
    {
        return X == 0.f && Y == 0.f;
    }

    RED_INLINE Bool Vector2::IsAlmostZero(const Float epsilon /*= 1e-6f*/) const
    {
        return fabs(X) < epsilon && fabs(Y) < epsilon;
    }

    RED_INLINE Float Vector2::Mag() const
    {
        return ::sqrtf(SquareMag());
    }

    RED_INLINE Float Vector2::SquareMag() const
    {
        return X * X + Y * Y;
    }

    RED_INLINE Float Vector2::Normalize()
    {
        const Float length = Mag();

        if (length > 0)
            *this /= length;

        return length;
    }

    RED_INLINE Vector2 Vector2::Normalized() const
    {
        const Float length = Mag();

        if (length > 0)
            return {X / length, Y / length};

        return *this;
    }

    RED_INLINE Vector2 Vector2::Perpendicular() const
    {
        return Vector2(-Y, X);
    }

    RED_INLINE Float Vector2::DistanceTo(const Vector2& v) const
    {
        return (*this - v).Mag();
    }

    RED_INLINE Float Vector2::DistanceSquaredTo(const Vector2& v) const
    {
        return (*this - v).SquareMag();
    }

    RED_INLINE Float Vector2::CrossZ(const Vector2& v) const
    {
        return X * v.Y - Y * v.X;
    }

    RED_FORCE_INLINE Vector2::operator Vector3() const
    {
        return {X, Y, 0.0f};
    }

    RED_FORCE_INLINE Vector2::operator const Vector3() const
    {
        return {X, Y, 0.0f};
    }

    RED_FORCE_INLINE Vector2::operator Vector4() const
    {
        return {X, Y, 0.0f};
    }

    RED_FORCE_INLINE Vector2::operator const Vector4() const
    {
        return {X, Y, 0.0f};
    }

    RED_FORCE_INLINE Vector2::operator Float*()
    {
        return &X;
    }

    RED_FORCE_INLINE Vector2::operator Float const*() const
    {
        return &X;
    }

    RED_INLINE Vector2 Vector2::Min(const Vector2& v1, const Vector2& v2)
    {
        return {vanguard::math::Min(v1.X, v2.X), vanguard::math::Min(v1.Y, v2.Y)};
    }

    RED_INLINE Vector2 Vector2::Max(const Vector2& v1, const Vector2& v2)
    {
        return {vanguard::math::Max(v1.X, v2.X), vanguard::math::Max(v1.Y, v2.Y)};
    }

    RED_INLINE Float Vector2::Dot(const Vector2& v) const
    {
        return X * v.X + Y * v.Y;
    }

    RED_INLINE Float Vector2::Yaw() const
    {
        if (fabsf(X) < 0.0001f && fabsf(Y) < 0.0001f)
            return 0.0f;

        return RAD2DEG(-atan2f(X, Y));
    }

    RED_INLINE Vector2 Vector2::Rotate(Float radians) const
    {
        const Float s = MSin(radians);
        const Float c = MCos(radians);
        return Vector2(c * X - s * Y, s * X + c * Y);
    }

    RED_INLINE Vector2 Vector2::EX()
    {
        return {1.f, 0.f};
    }

    RED_INLINE Vector2 Vector2::EY()
    {
        return {0.f, 1.f};
    }

    RED_INLINE Vector2 Vector2::ONES()
    {
        return {1.f, 1.f};
    }

    RED_INLINE Vector2 Vector2::ZEROS()
    {
        return {0.f, 0.f};
    }

    RED_INLINE Vector2 Vector2::HALVES()
    {
        return {0.5f, 0.5f};
    }

    RED_INLINE Vector2 Vector2::PLUS_MAX()
    {
        return {std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector2 Vector2::MINUS_MAX()
    {
        return {-std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector2 Vector2::PLUS_INF()
    {
        return {std::numeric_limits<Float>::infinity()};
    }

    RED_INLINE Vector2 Vector2::MINUS_INF()
    {
        return {-std::numeric_limits<Float>::infinity()};
    }
} // namespace vanguard::math