/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

#include <type_traits>

namespace vanguard::math
{
    // Vector3 is for memory critical structures. It lacks many features so don't use it everywhere. Note: must not be aligned !
    struct Vector3
    {
        RED_FORCE_INLINE Vector3() = default;

        Vector3(const Float f);
        Vector3(const Float x, const Float y, const Float z);
        Vector3(const Vector3& v);

        explicit Vector3(const Vector2& v);
        Vector3(const Vector4& v);
        Vector3(const Float f[3]);
        Vector3(std::nullptr_t) = delete;

        void Set(const Float x, const Float y, const Float z);
        Vector3& operator=(const Vector2& v);
        Vector3& operator=(const Vector3& v);
        Vector3& operator=(const Vector4& v);
        Vector3& operator=(const Float f);

        Vector3 operator-() const;
        Vector3 operator+(const Vector3& v) const;
        Vector3 operator-(const Vector3& v) const;
        Vector3 operator*(const Float scale) const;
        Vector3 operator*(const Vector3& v) const;
        Vector3 operator/(const Float scale) const;
        Vector3 operator/(const Vector3& v) const;

        Vector3& operator+=(const Vector3& v);
        Vector3& operator-=(const Vector3& v);
        Vector3& operator*=(const Vector3& v);
        Vector3& operator/=(const Vector3& v);
        Vector3& operator*=(Float scale);
        Vector3& operator/=(Float scale);

        Bool operator==(const Vector3& v) const;
        Bool operator!=(const Vector3& v) const;
        Bool operator<(const Vector3& v) const;

        Float operator[](const size_t index) const;
        Float& operator[](const size_t index);

        // Checks for bad values (denormals or infinities).
        Bool IsOk() const;

        Bool IsZero() const;
        Bool IsAlmostZero(Float epsilon = 1e-6f) const;

        Float Mag() const;
        Float SquareMag() const;

        Float Normalize();
        Vector3 Normalized() const;

        Vector3 Truncated() const;

        Float DistanceTo(const Vector3& other) const;
        Float DistanceSquaredTo(const Vector3& other) const;

        Float Dot(const Vector3& v) const;
        Vector3 Cross(const Vector3& v) const;

        Vector2& AsVector2();
        const Vector2& AsVector2() const;

        Float Pitch() const;
        Float Yaw() const;

        // Make Abs vector
        Vector3 Abs() const;

        const Float* AsFloat() const
        {
            return &X;
        }
        Float* AsFloat()
        {
            return &X;
        }

        REDMATH_API EulerAngles ToEulerAngles() const;

        static Vector3 Min(const Vector3& a, const Vector3& b);
        static Vector3 Max(const Vector3& a, const Vector3& b);

        static Bool Near3(const Vector3& a, const Vector3& b, Float eps = 1e-3f);

        static Vector3 EX();
        static Vector3 EY();
        static Vector3 EZ();
        static Vector3 ONES();
        static Vector3 ZEROS();
        static Vector3 PLUS_MAX();
        static Vector3 MINUS_MAX();
        static Vector3 PLUS_INF();
        static Vector3 MINUS_INF();

        Float X = 0.f;
        Float Y = 0.f;
        Float Z = 0.f;

        static_assert(std::is_same<decltype(X), decltype(Y)>::value, "Inconsistency in types for Vector3");
        static_assert(std::is_same<decltype(Y), decltype(Z)>::value, "Inconsistency in types for Vector3");

        using value_type = decltype(X);
    };
} // namespace vanguard::math