/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#pragma once

namespace vanguard::math
{
    // 2D vector
    struct Vector2
    {
        RED_FORCE_INLINE Vector2() = default;

        Vector2(const Float x, const Float y);
        Vector2(const Vector2& v);
        explicit Vector2(const Vector3& v);
        explicit Vector2(const Vector4& v);
        explicit Vector2(const Float f[2]);
        Vector2(Float v);
        Vector2(std::nullptr_t) = delete;

        Vector2& operator=(const Vector2& v);
        void Set(Float x, Float y);

        Vector2 operator-() const;
        Vector2 operator+(const Vector2& v) const;
        Vector2 operator-(const Vector2& v) const;
        Vector2 operator*(const Float scale) const;
        Vector2 operator*(const Vector2& v) const; //< component-wise multiplication NOT a dot product
        Vector2 operator/(const Float scale) const;

        Vector2& operator+=(const Vector2& v);
        Vector2& operator-=(const Vector2& v);
        Vector2& operator*=(const Vector2& v);
        Vector2& operator*=(const Float scale);
        Vector2& operator/=(const Float scale);

        Bool operator==(const Vector2& v) const;
        Bool operator!=(const Vector2& v) const;

        Float operator[](const size_t index) const;
        Float& operator[](const size_t index);

        Bool IsOk() const;
        Bool IsZero() const;
        Bool IsAlmostZero(Float epsilon = 1e-6f) const;

        Float Mag() const;
        Float SquareMag() const;

        Float Normalize();
        Vector2 Normalized() const;

        Float DistanceTo(const Vector2& v) const;
        Float DistanceSquaredTo(const Vector2& v) const;

        Float Dot(const Vector2& v) const;

        Float CrossZ(const Vector2& vec) const;
        Float Yaw() const;

        Vector2 Rotate(Float radians) const;
        Vector2 Perpendicular() const;

        explicit operator Vector3() const;
        explicit operator const Vector3() const;

        explicit operator Vector4() const;
        explicit operator const Vector4() const;

        const Float* AsFloat() const
        {
            return &X;
        }
        Float* AsFloat()
        {
            return &X;
        }

        explicit operator Float*();
        explicit operator Float const*() const;

        static Vector2 Min(const Vector2& a, const Vector2& b);
        static Vector2 Max(const Vector2& a, const Vector2& b);

        static Vector2 EX();
        static Vector2 EY();
        static Vector2 ONES();
        static Vector2 ZEROS();
        static Vector2 HALVES();
        static Vector2 PLUS_MAX();
        static Vector2 MINUS_MAX();
        static Vector2 PLUS_INF();
        static Vector2 MINUS_INF();

        Float X = 0.f;
        Float Y = 0.f;
    };

} // namespace vanguard::math