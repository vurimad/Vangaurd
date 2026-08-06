/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

#include <type_traits>

namespace vanguard::math
{
    struct Vector2;
    struct Vector3;
    struct EulerAngles;

    /********************************/
    /* Base four element vector		*/
    /********************************/
    RED_ALIGNED_STRUCT(Vector4, 16)
    {
        RED_FORCE_INLINE Vector4() = default;

        Vector4(const Vector2& v);
        Vector4(const Vector3& v, const Float w = 1.0f);
        Vector4(const Vector4& v);
        Vector4(const Float f[4]);
        Vector4(const Float x, const Float y, const Float z, const Float w = 1.0f);
        Vector4(const Float f);
        Vector4(const __m128& src);
        Vector4(std::nullptr_t) = delete;

        operator __m128() const
        {
            return *reinterpret_cast<const __m128*>(&X);
        }

        // Special sets
        Vector4& SetZeros();
        Vector4& SetOnes();

        Vector4& Negate();

        // Make Abs vector
        Vector4 Abs() const;

        // Calculate vector sum/length
        Float SquareMag2() const;
        Float SquareMag3() const;
        Float SquareMag4() const;
        Float Mag2() const;
        Float Mag3() const;
        Float Mag4() const;

        // Calculate distance to other point
        Float DistanceTo(const Vector4& other) const;

        // Calculate squared distance to other point
        Float DistanceSquaredTo(const Vector4& other) const;

        // Calculate 2D distance to other point
        Float DistanceTo2D(const Vector4& other) const;

        // Calculate 2D squared distance to other point
        Float DistanceSquaredTo2D(const Vector4& other) const;

        // Calculate distance to edge in 3D
        Float DistanceToEdge(const Vector4& a, const Vector4& b) const;

        // Calculate distance to edge in 2D (XY plane)
        Float DistanceToEdge2D(const Vector4& a, const Vector4& b) const;

        // Calculate nearest point on edge
        Vector4 NearestPointOnEdge(const Vector4& a, const Vector4& b) const;

        // Normalization
        Float Normalize2();
        Float Normalize3();
        void Normalize4();
        Vector4 Normalized2() const;
        Vector4 Normalized3() const;
        Vector4 Normalized4() const;

        Bool IsNormalized3(Float eps = 1e-4f) const;
        Bool IsNormalized4(Float eps = 1e-4f) const;

        // Min/Max
        static Vector4 Max4(const Vector4& a, const Vector4& b);
        static Vector4 Min4(const Vector4& a, const Vector4& b);

        // Clamp vector to range
        static Vector4 Clamp4(const Vector4& a, Float min, Float max);

        // Upper/Lower bound
        Float Upper3() const;
        Float Lower3() const;

        // Binary vector-vector operations
        static Vector4 Add3(const Vector4& a, const Vector4& b);
        static Vector4 Sub3(const Vector4& a, const Vector4& b);
        static Vector4 Mul3(const Vector4& a, const Vector4& b);

        // Inplace vector operators
        Vector4& Add3(const Vector4& a);
        Vector4& Sub3(const Vector4& a);

        // Inplace scalar operators
        Vector4& Mul3(Float a);

        // Dot product
        Float Dot2(const Vector4& a) const;
        Float Dot3(const Vector4& a) const;
        Float Dot4(const Vector4& a) const;

        // Reset i-th element
        Vector4 ZeroElement(Uint32 i) const;

        // Equality testing
        static Bool Equal2(const Vector4& a, const Vector4& b);
        static Bool Equal3(const Vector4& a, const Vector4& b);
        static Bool Equal4(const Vector4& a, const Vector4& b);

        // Near equality testing
        static Bool Near2(const Vector4& a, const Vector4& b, Float eps = 1e-3f);
        static Bool Near3(const Vector4& a, const Vector4& b, Float eps = 1e-3f);
        static Bool Near4(const Vector4& a, const Vector4& b, Float eps = 1e-3f);

        // Dot product
        static Float Dot2(const Vector4& a, const Vector4& b);
        static Float Dot3(const Vector4& a, const Vector4& b);
        static Float Dot4(const Vector4& a, const Vector4& b);

        // Cross product
        static Vector4 Cross(const Vector4& a, const Vector4& b, const Float w = 1.0f);
        static Float Cross2(const Vector4& a, const Vector4& b); // == perpendicular Dot2

        // Permute vectors
        static Vector4 Permute(const Vector4& a, const Vector4& b, Uint32 x, Uint32 y, Uint32 z, Uint32 w);

        Vector4 operator-() const;
        Vector4 operator+(const Vector4& a) const;
        Vector4 operator-(const Vector4& a) const;
        Vector4 operator*(const Vector4& a) const;
        Vector4 operator/(const Vector4& a) const;

        Vector4& operator=(const Vector4& a);
        Vector4& operator+=(const Vector4& a);
        Vector4& operator-=(const Vector4& a);
        Vector4& operator*=(const Vector4& a);
        Vector4& operator/=(const Vector4& a);

        Bool operator==(const Vector4& a) const;
        Bool operator!=(const Vector4& a) const;
        Bool operator<(const Vector4& a) const;

        Float operator[](const size_t index) const;
        Float& operator[](const size_t index);

        Bool IsZero() const;
        Bool IsAlmostZero(Float epsilon = 1e-6f) const;

        const Vector2& AsVector2() const;
        Vector2& AsVector2();
        const Vector3& AsVector3() const;
        Vector3& AsVector3();
        const Float* AsFloat() const
        {
            return &X;
        }
        Float* AsFloat()
        {
            return &X;
        }

        Float Pitch() const;
        Float Yaw() const;

        // Convert to Euler angles that will transform forward vector (EY) into this one
        REDMATH_API EulerAngles ToEulerAnglesInversePitch() const;
        // Improved ToEulerAngles method without bug in pitch calculation
        REDMATH_API EulerAngles ToEulerAngles() const;

        // Helper for some vertex templates
        Vector4& Position()
        {
            return *this;
        };
        const Vector4& Position() const
        {
            return *this;
        };

        // Interpolate
        static Vector4 Interpolate(const Vector4& a, const Vector4& b, const Float weight);
        void Interpolate(const Vector4& a, const Float weight);

        // Project
        static Vector4 Project(const Vector4& vector, const Vector4& onNormal);

        // Checks for bad values (denormals or infinities).
        Bool IsOk() const;

        // Calculate a hash to use in a hash map or other structure
        Uint32 CalcHash() const;

        // Some predefined vectors
        static Vector4 ZEROS();
        static Vector4 ZERO_3D_POINT();
        static Vector4 ONES();
        static Vector4 PLUS_MAX();
        static Vector4 MINUS_MAX();
        static Vector4 PLUS_INF();
        static Vector4 MINUS_INF();
        static Vector4 EX();
        static Vector4 EY();
        static Vector4 EZ();
        static Vector4 EW();
        static Vector4 FRONT();
        static Vector4 RIGHT();
        static Vector4 UP();

        union
        {
            struct
            {
                Float X, Y, Z, W;
            };
            __m128 vec;
        };

        static_assert(std::is_same<decltype(X), decltype(Y)>::value, "Inconsistency in types for Vector4");
        static_assert(std::is_same<decltype(Y), decltype(Z)>::value, "Inconsistency in types for Vector4");
        static_assert(std::is_same<decltype(Y), decltype(W)>::value, "Inconsistency in types for Vector4");

        using value_type = decltype(X);
    };

} // namespace vanguard::math