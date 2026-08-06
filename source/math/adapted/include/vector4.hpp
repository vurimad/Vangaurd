/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#pragma once

#include "vector2.h"
#include "vector3.h"
#include "simdQSTransform.h"

namespace vanguard::math
{
    RED_FORCE_INLINE Vector4::Vector4(const Vector2& v) : X(v.X), Y(v.Y), Z(0.f), W(1.f) {}

    RED_FORCE_INLINE Vector4::Vector4(const Vector3& v, const Float w) : X(v.X), Y(v.Y), Z(v.Z), W(w) {}

    RED_FORCE_INLINE Vector4::Vector4(const Vector4& v) : vec(v.vec) {}

    RED_FORCE_INLINE Vector4::Vector4(const Float f[4]) : X(f[0]), Y(f[1]), Z(f[2]), W(f[3]) {}

    RED_FORCE_INLINE Vector4::Vector4(const Float x, const Float y, const Float z, const Float w) : vec(_mm_set_ps(w, z, y, x)) {}

    RED_FORCE_INLINE Vector4::Vector4(const Float f) : vec(_mm_set1_ps(f)) {}

    RED_FORCE_INLINE Vector4::Vector4(const __m128& src) : vec(src) {}

    RED_FORCE_INLINE Vector4& Vector4::SetZeros()
    {
        vec = _mm_setzero_ps();
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::SetOnes()
    {
        vec = _mm_set1_ps(1.0f);
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::Negate()
    {
        vec = _mm_sub_ps(_mm_setzero_ps(), vec);
        return *this;
    }

    RED_INLINE Vector4 Vector4::Abs() const
    {
        return {::fabsf(X), ::fabsf(Y), ::fabsf(Z), ::fabsf(W)};
    }

    RED_FORCE_INLINE Float Vector4::SquareMag2() const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, vec, 0x3F));
    }

    RED_FORCE_INLINE Float Vector4::SquareMag3() const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, vec, 0x7F));
    }

    RED_FORCE_INLINE Float Vector4::SquareMag4() const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, vec, 0xFF));
    }

    RED_FORCE_INLINE Float Vector4::Mag2() const
    {
        return sqrtf(SquareMag2());
    }

    RED_FORCE_INLINE Float Vector4::Mag3() const
    {
        return sqrtf(SquareMag3());
    }

    RED_FORCE_INLINE Float Vector4::Mag4() const
    {
        return sqrtf(SquareMag4());
    }

    RED_INLINE Float Vector4::Normalize3()
    {
        Float len = Mag3();
        if (len != 0)
        {
            *this /= Vector4(len, len, len, 1.0f);
        }
        return len;
    }

    RED_INLINE Float Vector4::Normalize2()
    {
        float len = Mag2();
        if (len != 0)
        {
            *this /= Vector4(len, len, 1.0f, 1.0f);
        }
        return len;
    }

    RED_INLINE void Vector4::Normalize4()
    {
        vec = vanguard::math::simd::QuadHelper::Normalize4(vec);
    }

    RED_INLINE Vector4 Vector4::Normalized2() const
    {
        float len = Mag2();
        if (len == 0)
        {
            return *this;
        }
        return *this / Vector4(len, len, 1.0f, 1.0f);
    }

    RED_INLINE Vector4 Vector4::Normalized3() const
    {
        float len = Mag3();
        if (len == 0)
        {
            return *this;
        }
        return *this / Vector4(len, len, len, 1.0f);
    }

    RED_INLINE Vector4 Vector4::Normalized4() const
    {
        return Vector4(vanguard::math::simd::QuadHelper::Normalize4(vec));
    }

    RED_INLINE Bool Vector4::IsNormalized3(Float eps) const
    {
        const Float len = SquareMag3();
        return fabsf(len - 1.0f) < eps;
    }

    RED_INLINE Bool Vector4::IsNormalized4(Float eps) const
    {
        const Float len = SquareMag4();
        return fabsf(len - 1.0f) < eps;
    }

    RED_INLINE Vector4 Vector4::Min4(const Vector4& a, const Vector4& b)
    {
        return _mm_min_ps(a, b);
    }

    RED_INLINE Vector4 Vector4::Max4(const Vector4& a, const Vector4& b)
    {
        return _mm_max_ps(a, b);
    }

    RED_INLINE Vector4 Vector4::Clamp4(const Vector4& a, Float min, Float max)
    {
        Vector4 vmin{min, min, min, min};
        Vector4 vmax{max, max, max, max};
        Vector4 result = Max4(a, vmin);
        result = Min4(result, vmax);
        return result;
    }

    RED_INLINE Float Vector4::Upper3() const
    {
        return Max(X, Max(Y, Z));
    }

    RED_INLINE Float Vector4::Lower3() const
    {
        return Min(X, Min(Y, Z));
    }

    RED_INLINE Vector4 Vector4::Sub3(const Vector4& a, const Vector4& b)
    {
        return {a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W};
    }

    RED_INLINE Vector4 Vector4::Add3(const Vector4& a, const Vector4& b)
    {
        return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    RED_INLINE Vector4 Vector4::Mul3(const Vector4& a, const Vector4& b)
    {
        return {a.X * b.X, a.Y * b.Y, a.Z * b.Z};
    }

    RED_INLINE Vector4& Vector4::Add3(const Vector4& v)
    {
        X += v.X;
        Y += v.Y;
        Z += v.Z;
        return *this;
    }

    RED_INLINE Vector4& Vector4::Sub3(const Vector4& v)
    {
        X -= v.X;
        Y -= v.Y;
        Z -= v.Z;
        return *this;
    }

    RED_INLINE Vector4& Vector4::Mul3(const Float f)
    {
        X *= f;
        Y *= f;
        Z *= f;
        return *this;
    }

    RED_FORCE_INLINE Bool Vector4::Equal2(const Vector4& a, const Vector4& b)
    {
        return (_mm_movemask_ps(_mm_cmpeq_ps(a, b)) & 0x3) == 0x3;
    }

    RED_FORCE_INLINE Bool Vector4::Equal3(const Vector4& a, const Vector4& b)
    {
        return (_mm_movemask_ps(_mm_cmpeq_ps(a, b)) & 0x7) == 0x7;
    }

    RED_FORCE_INLINE Bool Vector4::Equal4(const Vector4& a, const Vector4& b)
    {
        return _mm_movemask_ps(_mm_cmpeq_ps(a, b)) == 0xF;
    }

    RED_INLINE Bool Vector4::Near2(const Vector4& a, const Vector4& b, Float eps /*=1e-3f*/)
    {
        return fabsf(a.X - b.X) < eps && fabsf(a.Y - b.Y) < eps;
    }

    RED_INLINE Bool Vector4::Near3(const Vector4& a, const Vector4& b, Float eps /*=1e-3f*/)
    {
        return fabsf(a.X - b.X) < eps && fabsf(a.Y - b.Y) < eps && fabsf(a.Z - b.Z) < eps;
    }

    RED_INLINE Bool Vector4::Near4(const Vector4& a, const Vector4& b, Float eps /*=1e-3f*/)
    {
        return fabsf(a.X - b.X) < eps && fabsf(a.Y - b.Y) < eps && fabsf(a.Z - b.Z) < eps && fabsf(a.W - b.W) < eps;
    }

    RED_FORCE_INLINE Float Vector4::Dot2(const Vector4& a, const Vector4& b)
    {
        return _mm_cvtss_f32(_mm_dp_ps(a.vec, b.vec, 0x3F));
    }

    RED_FORCE_INLINE Float Vector4::Dot3(const Vector4& a, const Vector4& b)
    {
        return _mm_cvtss_f32(_mm_dp_ps(a.vec, b.vec, 0x7F));
    }

    RED_FORCE_INLINE Float Vector4::Dot4(const Vector4& a, const Vector4& b)
    {
        return _mm_cvtss_f32(_mm_dp_ps(a.vec, b.vec, 0xFF));
    }

    RED_FORCE_INLINE Float Vector4::Dot2(const Vector4& b) const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, b.vec, 0x3F));
    }

    RED_FORCE_INLINE Float Vector4::Dot3(const Vector4& b) const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, b.vec, 0x7F));
    }

    RED_FORCE_INLINE Float Vector4::Dot4(const Vector4& b) const
    {
        return _mm_cvtss_f32(_mm_dp_ps(vec, b.vec, 0xFF));
    }

    RED_INLINE Vector4 Vector4::ZeroElement(Uint32 i) const
    {
        RED_FATAL_ASSERT(i < 3);
        Vector4 vec = *this;
        vec[i] = 0.f;
        return vec;
    }

    RED_INLINE Float Vector4::DistanceTo(const Vector4& other) const
    {
        Vector4 delta = *this - other;
        return delta.Mag3();
    }

    RED_INLINE Float Vector4::DistanceSquaredTo(const Vector4& other) const
    {
        Vector4 delta = *this - other;
        return delta.SquareMag3();
    }

    RED_INLINE Float Vector4::DistanceTo2D(const Vector4& other) const
    {
        Vector4 delta = *this - other;
        return delta.Mag2();
    }

    RED_INLINE Float Vector4::DistanceSquaredTo2D(const Vector4& other) const
    {
        Vector2 delta = AsVector2() - other.AsVector2();
        return delta.SquareMag();
    }

    RED_INLINE Float Vector4::DistanceToEdge(const Vector4& a, const Vector4& b) const
    {
        Vector4 edge = (b - a).Normalized3();
        Float ta = Vector4::Dot3(edge, a);
        Float tb = Vector4::Dot3(edge, b);
        Float p = Vector4::Dot3(edge, *this);
        if (p >= ta && p <= tb)
        {
            Vector4 projected = a + edge * (p - ta);
            return DistanceTo(projected);
        }
        else if (p < ta)
        {
            return DistanceTo(a);
        }
        else
        {
            return DistanceTo(b);
        }
    }

    RED_INLINE Float Vector4::DistanceToEdge2D(const Vector4& a, const Vector4& b) const
    {
        Vector4 edgeXY = b - a;

        // almost vertical edge
        if (edgeXY.AsVector2().SquareMag() < 0.0001f)
        {
            return DistanceTo2D(a);
        }

        const Vector4 edge = edgeXY.Normalized2();
        const Float ta = Vector4::Dot2(edge, a);
        const Float tb = Vector4::Dot2(edge, b);
        const Float p = Vector4::Dot2(edge, *this);
        if (p >= ta && p <= tb)
        {
            Vector4 projected = a + edge * (p - ta);
            return DistanceTo2D(projected);
        }
        else if (p < ta)
        {
            return DistanceTo2D(a);
        }
        else
        {
            return DistanceTo2D(b);
        }
    }

    RED_INLINE Vector4 Vector4::NearestPointOnEdge(const Vector4& a, const Vector4& b) const
    {
        Vector4 d = b - a;
        Float len = d.Normalize3();

        Vector4 v{X - a.X, Y - a.Y, Z - a.Z};

        Float proj = len != 0.0f ? Vector4::Dot3(v, d) / len : 0.0f;

        if (proj <= 0)
        {
            return a;
        }
        else if (proj >= 1)
        {
            return b;
        }
        else
        {
            return {b * proj + a * (1 - proj)};
        }
    }

    RED_INLINE Vector4 Vector4::Cross(const Vector4& a, const Vector4& b, const Float w)
    {
        __m128 temp1 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
        __m128 temp2 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
        __m128 vResult = _mm_mul_ps(temp1, temp2);
        temp1 = _mm_shuffle_ps(temp1, temp1, _MM_SHUFFLE(3, 0, 2, 1));
        temp2 = _mm_shuffle_ps(temp2, temp2, _MM_SHUFFLE(3, 1, 0, 2));
        Vector4 result = _mm_sub_ps(vResult, _mm_mul_ps(temp1, temp2));
        result.W = w;
        return result;
    }

    RED_INLINE Float Vector4::Cross2(const Vector4& a, const Vector4& b)
    {
        return a.X * b.Y - b.X * a.Y;
    }

    RED_INLINE Float Vector4::Pitch() const
    {
        return (!X && !Y) ? ((Z > 0) ? 90.0f : -90.0f) : RAD2DEG(atan2f(-Z, sqrtf(X * X + Y * Y)));
    }

    RED_INLINE Float Vector4::Yaw() const
    {
        if (fabsf(X) < 0.0001f && fabsf(Y) < 0.0001f)
            return 0.0f;

        return RAD2DEG(-atan2f(X, Y));
    }

    RED_INLINE Vector4 Vector4::Permute(const Vector4& a, const Vector4& b, Uint32 x, Uint32 y, Uint32 z, Uint32 w)
    {
        return {x ? b.X : a.X, y ? b.Y : a.Y, z ? b.Z : a.Z, w ? b.W : a.W};
    }

    RED_FORCE_INLINE Vector4 Vector4::operator-() const
    {
        return _mm_sub_ps(_mm_setzero_ps(), vec);
    }

    RED_FORCE_INLINE Vector4 Vector4::operator+(const Vector4& v) const
    {
        return _mm_add_ps(vec, v.vec);
    }

    RED_FORCE_INLINE Vector4 Vector4::operator-(const Vector4& v) const
    {
        return _mm_sub_ps(vec, v.vec);
    }

    RED_FORCE_INLINE Vector4 Vector4::operator*(const Vector4& v) const
    {
        return _mm_mul_ps(vec, v.vec);
    }

    RED_FORCE_INLINE Vector4 Vector4::operator/(const Vector4& v) const
    {
        return _mm_div_ps(vec, v.vec);
    }

    RED_FORCE_INLINE Vector4& Vector4::operator=(const Vector4& v)
    {
        vec = v.vec;
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::operator+=(const Vector4& v)
    {
        vec = _mm_add_ps(vec, v);
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::operator-=(const Vector4& v)
    {
        vec = _mm_sub_ps(vec, v);
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::operator*=(const Vector4& v)
    {
        vec = _mm_mul_ps(vec, v);
        return *this;
    }

    RED_FORCE_INLINE Vector4& Vector4::operator/=(const Vector4& v)
    {
        vec = _mm_div_ps(vec, v);
        return *this;
    }

    RED_FORCE_INLINE Bool Vector4::operator==(const Vector4& a) const
    {
        return Vector4::Equal4(*this, a);
    }

    RED_FORCE_INLINE Bool Vector4::operator!=(const Vector4& a) const
    {
        return !Vector4::Equal4(*this, a);
    }

    RED_INLINE Bool Vector4::operator<(const Vector4& a) const
    {
        if (X == a.X)
        {
            if (Y == a.Y)
            {
                if (Z == a.Z)
                {
                    return W < a.W;
                }
                return Z < a.Z;
            }
            return Y < a.Y;
        }
        return X < a.X;
    }

    RED_INLINE Float Vector4::operator[](const size_t index) const
    {
        RED_FATAL_ASSERT(index < 4, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Float& Vector4::operator[](const size_t index)
    {
        RED_FATAL_ASSERT(index < 4, "Error: Index out of bounds");
        return *((Float*)&X + index);
    }

    RED_INLINE Bool Vector4::IsZero() const
    {
        return X == 0.f && Y == 0.f && Z == 0.f && W == 0.f;
    }

    RED_INLINE Bool Vector4::IsAlmostZero(Float epsilon) const
    {
        return fabsf(X) < epsilon && fabsf(Y) < epsilon && fabsf(Z) < epsilon && fabsf(W) < epsilon;
    }

    RED_FORCE_INLINE const Vector2& Vector4::AsVector2() const
    {
        return reinterpret_cast<const Vector2&>(*this);
    }

    RED_FORCE_INLINE Vector2& Vector4::AsVector2()
    {
        return reinterpret_cast<Vector2&>(*this);
    }

    RED_FORCE_INLINE const Vector3& Vector4::AsVector3() const
    {
        return reinterpret_cast<const Vector3&>(*this);
    }

    RED_FORCE_INLINE Vector3& Vector4::AsVector3()
    {
        return reinterpret_cast<Vector3&>(*this);
    }

    RED_INLINE Vector4 Vector4::Interpolate(const Vector4& a, const Vector4& b, const Float weight)
    {
        return a + (b - a) * weight;
    }

    RED_INLINE void Vector4::Interpolate(const Vector4& a, const Float weight)
    {
        *this += (a - (*this)) * weight;
    }

    RED_INLINE Vector4 Vector4::Project(const Vector4& vector, const Vector4& onNormal)
    {
        Float num = Dot3(onNormal, onNormal);

        if (num < 1e-8f) // note - value from tests
        {
            return Vector4::ZERO_3D_POINT();
        }

        return (onNormal * Dot3(vector, onNormal)) / num;
    }

    RED_INLINE Bool Vector4::IsOk() const
    {
        return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z) && std::isfinite(W);
    }

    RED_INLINE Uint32 Vector4::CalcHash() const
    {
        const Uint32* _A = (const Uint32*)AsFloat();
        return _A[0] ^ _A[1] ^ _A[2] ^ _A[3];
    }

    RED_FORCE_INLINE Vector4 Vector4::ZEROS()
    {
        return {0.f, 0.f, 0.f, 0.f};
    }

    RED_FORCE_INLINE Vector4 Vector4::ZERO_3D_POINT()
    {
        return {0.f, 0.f, 0.f, 1.f};
    }

    RED_FORCE_INLINE Vector4 Vector4::ONES()
    {
        return {1.f, 1.f, 1.f, 1.f};
    }

    RED_INLINE Vector4 Vector4::PLUS_MAX()
    {
        return {std::numeric_limits<Float>::max(), std::numeric_limits<Float>::max(), std::numeric_limits<Float>::max(),
                std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector4 Vector4::MINUS_MAX()
    {
        return {-std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max(),
                -std::numeric_limits<Float>::max()};
    }

    RED_INLINE Vector4 Vector4::PLUS_INF()
    {
        return {std::numeric_limits<Float>::infinity(), std::numeric_limits<Float>::infinity(), std::numeric_limits<Float>::infinity(),
                std::numeric_limits<Float>::infinity()};
    }

    RED_INLINE Vector4 Vector4::MINUS_INF()
    {
        return {-std::numeric_limits<Float>::infinity(), -std::numeric_limits<Float>::infinity(), -std::numeric_limits<Float>::infinity(),
                -std::numeric_limits<Float>::infinity()};
    }

    RED_FORCE_INLINE Vector4 Vector4::EX()
    {
        return {1.f, 0.f, 0.f, 0.f};
    }

    RED_FORCE_INLINE Vector4 Vector4::EY()
    {
        return {0.f, 1.f, 0.f, 0.f};
    }

    RED_FORCE_INLINE Vector4 Vector4::EZ()
    {
        return {0.f, 0.f, 1.f, 0.f};
    }

    RED_FORCE_INLINE Vector4 Vector4::EW()
    {
        return {0.f, 0.f, 0.f, 1.f};
    }

    RED_INLINE Vector4 Vector4::FRONT()
    {
        return Vector4::EY();
    }

    RED_INLINE Vector4 Vector4::RIGHT()
    {
        return Vector4::EX();
    }

    RED_INLINE Vector4 Vector4::UP()
    {
        return Vector4::EZ();
    }
} // namespace vanguard::math