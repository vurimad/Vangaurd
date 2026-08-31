/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once
#include "vector4.h"

namespace vanguard::math
{
    namespace Helper
    {
        RED_INLINE Quaternion Mul(const Vector4& _v, const Quaternion& _q)
        ///
        ///	Helper function to treat vector as a pure quaternion and perform a concatenation of the two.
        ///	Useful when transforming vectors
        ///
        {
            return {_q.r * _v.X + _q.j * _v.Z - _v.Y * _q.k, _q.r * _v.Y + _q.k * _v.X - _v.Z * _q.i, _q.r * _v.Z + _q.i * _v.Y - _v.X * _q.j,
                    -_q.i * _v.X - _q.j * _v.Y - _q.k * _v.Z};
        }

        RED_INLINE Quaternion Mul(const Vector3& _v, const Quaternion& _q)
        ///
        ///	Helper function to treat vector as a pure quaternion and perform a concatenation of the two.
        ///	Useful when transforming vectors
        ///
        {
            return {_q.r * _v.X + _q.j * _v.Z - _v.Y * _q.k, _q.r * _v.Y + _q.k * _v.X - _v.Z * _q.i, _q.r * _v.Z + _q.i * _v.Y - _v.X * _q.j,
                    -_q.i * _v.X - _q.j * _v.Y - _q.k * _v.Z};
        }

        RED_INLINE Float Dot(const Vector3& _v, const Quaternion& _q)
        ///
        ///	Helper function to treat vector as a pure quaternion and perform a concatenation of the two.
        ///	Useful when decomposing a rest axis
        ///
        {
            return _v.X * _q.i + _v.Y * _q.j + _v.Z * _q.k;
        }

        RED_INLINE Float Dot(const Vector4& _v, const Quaternion& _q)
        ///
        ///	Helper function to treat vector as a pure quaternion and perform a concatenation of the two.
        ///	Useful when decomposing a rest axis
        ///
        {
            return _v.X * _q.i + _v.Y * _q.j + _v.Z * _q.k;
        }

        RED_INLINE void CalculatePerpendicularVector(const Vector4& in, Vector4& out)
        ///
        ///	Helper function to calculate a vector perpendicular to the in vector
        ///
        {
            int min = 0;
            int eleA = 1;
            int eleB = 2;

            float vA = vanguard::math::Abs(in.X);
            float vB = vanguard::math::Abs(in.Y);
            float vC = vanguard::math::Abs(in.Z);

            if (vB < vA)
            {
                eleA = 0;
                min = 1;
                vA = vB;
            }

            if (vC < vA)
            {
                eleB = min;
                min = 2;
            }

            out.SetZeros();
            out[eleA] = in[eleB];
            out[eleB] = in[eleA];
        }
    } // namespace Helper

    RED_FORCE_INLINE Quaternion::Quaternion() : i(0.0f), j(0.0f), k(0.0f), r(1.0f) {}

    RED_FORCE_INLINE Quaternion::Quaternion(const Float _i, const Float _j, const Float _k, const Float real) : i(_i), j(_j), k(_k), r(real) {}

    RED_FORCE_INLINE Quaternion::Quaternion(const Vector4& v) : vec(v) {}

    RED_FORCE_INLINE Quaternion& Quaternion::operator=(const Quaternion& other)
    {
        vec = other.vec;
        return *this;
    }

    RED_INLINE Quaternion::Quaternion(const Float roll, const Float pitch, const Float yaw)
    {
        const Float c1 = ::MCos(0.5f * yaw);
        const Float c2 = ::MCos(0.5f * pitch);
        const Float c3 = ::MCos(0.5f * roll);

        const Float s1 = MSin(0.5f * yaw);
        const Float s2 = MSin(0.5f * pitch);
        const Float s3 = MSin(0.5f * roll);

        r = c1 * c2 * c3 - s1 * s2 * s3;
        i = c1 * s2 * c3 - s1 * c2 * s3;
        j = s1 * s2 * c3 + c1 * c2 * s3;
        k = s1 * c2 * c3 + c1 * s2 * s3;
    }

    RED_FORCE_INLINE Quaternion::Quaternion(const Float arr[4]) : Quaternion{arr[0], arr[1], arr[2], arr[3]} {}

    RED_FORCE_INLINE Quaternion::Quaternion(const Quaternion& q) : Quaternion{q.i, q.j, q.k, q.r} {}

    RED_INLINE Quaternion::Quaternion(const Vector4& axis, const Float angle)
    {
        SetAxisAngle(axis, angle);
    }

    RED_INLINE Quaternion::Quaternion(const Vector3& axis, const Float angle)
    {
        const Float sin_half = MSin(angle * 0.5f);

        i = axis.X * sin_half;
        j = axis.Y * sin_half;
        k = axis.Z * sin_half;
        r = MCos(angle * 0.5f);
    }

    RED_FORCE_INLINE Quaternion Quaternion::IDENTITY()
    {
        return {0.f, 0.f, 0.f, 1.f};
    }

    RED_FORCE_INLINE Quaternion Quaternion::I()
    {
        return {1.f, 0.f, 0.f, 0.f};
    }

    RED_FORCE_INLINE Quaternion Quaternion::J()
    {
        return {0.f, 1.f, 0.f, 0.f};
    }

    RED_FORCE_INLINE Quaternion Quaternion::K()
    {
        return {0.f, 0.f, 1.f, 0.f};
    }

    RED_FORCE_INLINE void Quaternion::SetIdentity()
    {
        vec = Vector4(0.0, 0.0f, 0.0f, 1.0f);
    }

    RED_FORCE_INLINE void Quaternion::SetZeros()
    {
        vec = Vector4::ZEROS();
    }

    RED_FORCE_INLINE void Quaternion::SetInverse()
    {
        *this = Conjugate() / MagnitudeSq();
    }

    RED_FORCE_INLINE void Quaternion::SetNegative()
    {
        vec = -vec;
    }

    RED_FORCE_INLINE void Quaternion::SetConjugate()
    {
        *this = Conjugate();
    }

    RED_INLINE void Quaternion::SetAxisAngle(const Vector4& axis, const Float angle)
    {
        const Float sin_half = MSin(angle * 0.5f);

        i = axis.X * sin_half;
        j = axis.Y * sin_half;
        k = axis.Z * sin_half;
        r = MCos(angle * 0.5f);
    }

    RED_INLINE void Quaternion::SetAxisAngle(const Vector3& axis, const Float angle)
    {
        const Float sin_half = MSin(angle * 0.5f);

        i = axis.X * sin_half;
        j = axis.Y * sin_half;
        k = axis.Z * sin_half;
        r = MCos(angle * 0.5f);
    }

    RED_INLINE void Quaternion::SetXRot(const Float ccwRadians)
    {
        const Float sin_half = MSin(ccwRadians * 0.5f);
        i = sin_half;
        j = 0.f;
        k = 0.f;
        r = MCos(ccwRadians * 0.5f);
    }

    RED_INLINE void Quaternion::SetYRot(const Float ccwRadians)
    {
        const Float sin_half = MSin(ccwRadians * 0.5f);
        i = 0.f;
        j = sin_half;
        k = 0.f;
        r = MCos(ccwRadians * 0.5f);
    }

    RED_INLINE void Quaternion::SetZRot(const Float ccwRadians)
    {
        const Float sin_half = MSin(ccwRadians * 0.5f);
        i = 0.f;
        j = 0.f;
        k = sin_half;
        r = MCos(ccwRadians * 0.5f);
    }

    RED_INLINE void Quaternion::SetAdd(const Quaternion& q1, const Quaternion& q2)
    {
        *this = q1 + q2;
    }

    RED_INLINE void Quaternion::SetSub(const Quaternion& q1, const Quaternion& q2)
    {
        *this = q1 - q2;
    }

    RED_INLINE void Quaternion::SetMul(const Quaternion& q1, const Quaternion& q2)
    {
        *this = q1 * q2;
    }

    RED_INLINE void Quaternion::SetMul(const Quaternion& q, const Float s)
    {
        *this = q * s;
    }

    RED_INLINE void Quaternion::SetDiv(const Quaternion& q, const Float s)
    {
        *this = q / s;
    }

    RED_INLINE void Quaternion::Power(Float exponent)
    {
        // This function was taken from 3d math primer,
        // probably can be done better

        if (Abs(r) < 1.0f) // identity ?
        {
            // Extract the half angle alpha (alpha = theta/2)
            Float alpha = MAcos_safe(r);

            // Compute new alpha value
            Float newAlpha = alpha * exponent;

            // Compute new w value
            r = MCos(newAlpha);

            // Compute new xyz values
            Float mult = MSin(newAlpha) / MSin(alpha);
            i *= mult;
            j *= mult;
            k *= mult;
        }
    }

    RED_INLINE Quaternion Quaternion::Powered(Float exponent) const
    {
        Quaternion q = *this;
        q.Power(exponent);
        return q;
    }

    RED_INLINE Float Quaternion::Dot(const Quaternion& q) const
    {
        return r * q.r + i * q.i + j * q.j + k * q.k;
    }

    RED_INLINE Quaternion Quaternion::Neg() const
    {
        return {-i, -j, -k, -r};
    }

    RED_FORCE_INLINE Quaternion Quaternion::operator+(const Quaternion& q) const
    {
        return Quaternion(vec + q.vec);
    }

    RED_FORCE_INLINE Quaternion Quaternion::operator-(const Quaternion& q) const
    {
        return Quaternion(vec - q.vec);
    }

    RED_INLINE Quaternion Quaternion::operator*(const Quaternion& q) const
    {
        return {r * q.i + q.r * i + j * q.k - q.j * k, r * q.j + q.r * j + k * q.i - q.k * i, r * q.k + q.r * k + i * q.j - q.i * j,
                r * q.r - q.i * i - j * q.j - q.k * k};
    }

    RED_FORCE_INLINE Quaternion Quaternion::operator*(const Float s) const
    {
        return Quaternion(vec * s);
    }

    RED_FORCE_INLINE Quaternion Quaternion::operator/(const Float s) const
    {
        return Quaternion(vec / s);
    }

    RED_INLINE Bool Quaternion::operator==(const Quaternion& q) const
    {
        return r == q.r && i == q.i && j == q.j && k == q.k;
    }

    RED_INLINE Bool Quaternion::operator!=(const Quaternion& q) const
    {
        return r != q.r || i != q.i || j != q.j || k != q.k;
    }

    RED_INLINE Quaternion& Quaternion::operator+=(const Quaternion& q)
    {
        *this = *this + q;
        return *this;
    }

    RED_INLINE Quaternion& Quaternion::operator-=(const Quaternion& q)
    {
        *this = *this - q;
        return *this;
    }

    RED_INLINE Quaternion& Quaternion::operator*=(const Quaternion& q)
    {
        *this = *this * q;
        return *this;
    }

    RED_INLINE Quaternion& Quaternion::operator*=(const Float s)
    {
        *this = *this * s;
        return *this;
    }

    RED_INLINE Quaternion& Quaternion::operator/=(const Float s)
    {
        *this = *this / s;
        return *this;
    }

    RED_FORCE_INLINE Vector3 Quaternion::operator*(const Vector3& v) const
    {
        return Transform(v);
    }

    RED_FORCE_INLINE Vector4 Quaternion::operator*(const Vector4& v) const
    {
        return Transform(v);
    }

    RED_INLINE Quaternion Quaternion::MulInverse(const Quaternion& q) const
    {
        return *this * q.Inverse();
    }

    RED_INLINE Quaternion Quaternion::MulConjugate(const Quaternion& q) const
    {
        return *this * q.Conjugate();
    }

    RED_FORCE_INLINE Float Quaternion::MagnitudeSq() const
    {
        return vec.SquareMag4();
    }

    RED_FORCE_INLINE Float Quaternion::Magnitude() const
    {
        return sqrtf(MagnitudeSq());
    }

    RED_INLINE Quaternion Quaternion::Conjugate() const
    {
        return {-i, -j, -k, r};
    }

    // ---
    // Safe versions with magnitude check.
    // We can put an assert in future to catch operations on invalid quaternions
    RED_INLINE Quaternion Quaternion::Inverse() const
    {
        const Float magSqr = MagnitudeSq();
        const Float magSqrRcp = (magSqr > FLT_EPSILON) ? 1.f / magSqr : 0.f;
        return Conjugate() * magSqrRcp;
    }

    RED_INLINE Quaternion Quaternion::Normalized() const
    {
        return Quaternion(vec.Normalized4());
    }

    RED_INLINE void Quaternion::Normalize()
    {
        vec.Normalize4();
    }
    // ---

    RED_INLINE Vector4 Quaternion::TransformUnsafe(const Vector4& v) const
    {
        const Quaternion& q = *this;
        Vector4 t = Vector4::Cross(q.vec, v);
        t = t + t;
        return v + t * q.r + Vector4::Cross(q.vec, t);
    }

    RED_FORCE_INLINE Vector3 Quaternion::TransformUnsafe(const Vector3& v) const
    {
        return (TransformUnsafe(Vector4(v))).AsVector3();
    }

    RED_INLINE Vector4 Quaternion::Transform(const Vector4& v) const
    {
        const Quaternion q = Normalized();
        return Vector4(q.TransformUnsafe(v), 0.f);
    }

    RED_INLINE Vector3 Quaternion::Transform(const Vector3& v) const
    {
        const Quaternion q = Normalized();
        return q.TransformUnsafe(v);
    }

    RED_INLINE Vector4 Quaternion::TransformInverse(const Vector4& in) const
    {
        return Inverse().Transform(in);
    }

    RED_INLINE Vector3 Quaternion::TransformInverse(const Vector3& in) const
    {
        return Inverse().Transform(in);
    }

    RED_INLINE Vector4 Quaternion::TransformConjugate(const Vector4& in) const
    {
        return Conjugate().Transform(in);
    }

    RED_INLINE Vector3 Quaternion::TransformConjugate(const Vector3& in) const
    {
        return Conjugate().Transform(in);
    }

    RED_INLINE Vector3 Quaternion::GetXAxisUnsafe() const
    {
        const Quaternion& q = *this;
        return {1.f - 2.f * q.j * q.j - 2.f * q.k * q.k, 2.f * q.i * q.j + 2.f * q.k * q.r, 2.f * q.i * q.k - 2.f * q.j * q.r};
    }
    RED_INLINE Vector3 Quaternion::GetYAxisUnsafe() const
    {
        const Quaternion& q = *this;
        return {2.f * q.i * q.j - 2.f * q.k * q.r, 1.f - 2.f * q.i * q.i - 2.f * q.k * q.k, 2.f * q.j * q.k + 2.f * q.i * q.r};
    }
    RED_INLINE Vector3 Quaternion::GetZAxisUnsafe() const
    {
        const Quaternion& q = *this;
        return {2.f * q.i * q.k + 2.f * q.j * q.r, 2.f * q.j * q.k - 2.f * q.i * q.r, 1.f - 2.f * q.i * q.i - 2.f * q.j * q.j};
    }

    RED_INLINE Vector4 Quaternion::GetXAxis4() const
    {
        const Quaternion q = Normalized();
        return Vector4(q.GetXAxisUnsafe(), 0.f);
    }

    RED_INLINE Vector3 Quaternion::GetXAxis3() const
    {
        const Quaternion q = Normalized();
        return q.GetXAxisUnsafe();
    }

    RED_INLINE Vector4 Quaternion::GetYAxis4() const
    {
        const Quaternion q = Normalized();
        return Vector4(q.GetYAxisUnsafe(), 0.f);
    }

    RED_INLINE Vector3 Quaternion::GetYAxis3() const
    {
        const Quaternion q = Normalized();
        return q.GetYAxisUnsafe();
    }

    RED_INLINE Vector4 Quaternion::GetZAxis4() const
    {
        const Quaternion q = Normalized();
        return Vector4(q.GetZAxisUnsafe(), 0.f);
    }

    RED_INLINE Vector3 Quaternion::GetZAxis3() const
    {
        const Quaternion q = Normalized();
        return q.GetZAxisUnsafe();
    }

    RED_INLINE Vector3 Quaternion::GetForward3() const
    {
        return GetYAxis3();
    }

    RED_INLINE Vector3 Quaternion::GetRight3() const
    {
        return GetXAxis3();
    }

    RED_INLINE Vector3 Quaternion::GetUp3() const
    {
        return GetZAxis3();
    }

    RED_INLINE Vector4 Quaternion::GetForward4() const
    {
        return GetYAxis4();
    }

    RED_INLINE Vector4 Quaternion::GetRight4() const
    {
        return GetXAxis4();
    }

    RED_INLINE Vector4 Quaternion::GetUp4() const
    {
        return GetZAxis4();
    }

    RED_INLINE Float Quaternion::GetAngle() const
    {
        const Float angle = MAcos_safe(vanguard::math::Abs(r)) * 2.f;
        return angle;
    }

    RED_INLINE Vector4 Quaternion::GetAxis4() const
    {
        Vector4 axis(i, j, k, 0.f);
        axis.Normalize3();

        if (r < 0.0f)
        {
            axis *= -1.f;
        }

        return axis;
    }

    RED_INLINE Vector3 Quaternion::GetAxis3() const
    {
        Vector3 axis(i, j, k);
        axis.Normalize();

        if (r < 0.0f)
        {
            axis *= -1.f;
        }

        return axis;
    }

    RED_INLINE Float Quaternion::GetPitch() const
    {
        const Float y2 = j * j;
        const Float z2 = k * k;

        const Float unitLength = r * r + i * i + y2 + z2;
        const Float wxyz = r * i + j * k;
        const Float eps = 0.001f;

        if (wxyz > (0.5f - eps) * unitLength)
        {
            return RAD2DEG(RED_PI_HALF);
        }
        else if (wxyz < (-0.5f + eps) * unitLength)
        {
            return RAD2DEG(-RED_PI_HALF);
        }
        else
        {
            return RAD2DEG(::asinf(2.f * wxyz / unitLength));
        }
    }

    RED_INLINE Float Quaternion::GetYaw() const
    {
        const Float x2 = i * i;
        const Float z2 = k * k;

        const Float unitLength = r * r + x2 + j * j + z2;
        const Float wxyz = r * i + j * k;
        const Float eps = 0.001f;

        if (wxyz > (0.5f - eps) * unitLength)
        {
            return RAD2DEG(2.f * ::atan2f(j, r));
        }
        else if (wxyz < (-0.5f + eps) * unitLength)
        {
            return RAD2DEG(-2.f * ::atan2f(j, r));
        }
        else
        {
            return RAD2DEG(::atan2f(2.f * (r * k - i * j), 1.f - 2.f * (z2 + x2)));
        }
    }

    RED_INLINE Float Quaternion::GetRoll() const
    {
        const Float x2 = i * i;
        const Float y2 = j * j;

        const Float unitLength = r * r + x2 + y2 + k * k;
        const Float wxyz = r * i + j * k;
        const Float eps = 0.001f;

        if (wxyz > (0.5f - eps) * unitLength || wxyz < (-0.5f + eps) * unitLength)
        {
            return 0.f;
        }
        else
        {
            return RAD2DEG(::atan2f(2.f * (r * j - i * k), 1.f - 2.f * (y2 + x2)));
        }
    }

    RED_FORCE_INLINE const Vector4& Quaternion::AsVector() const
    {
        return vec;
    }

    RED_INLINE Matrix Quaternion::ToMatrix() const
    {
        const Quaternion q = Normalized();
        Matrix m;

        m.SetRow(0, Vector4(q.GetXAxisUnsafe(), 0.f));
        m.SetRow(1, Vector4(q.GetYAxisUnsafe(), 0.f));
        m.SetRow(2, Vector4(q.GetZAxisUnsafe(), 0.f));
        m.SetRow(3, Vector4::ZERO_3D_POINT());

        return m;
    }

    RED_INLINE Matrix Quaternion::ToMatrixUnsafe() const
    {
        Matrix m;

        m.SetRow(0, Vector4(GetXAxisUnsafe(), 0.f));
        m.SetRow(1, Vector4(GetYAxisUnsafe(), 0.f));
        m.SetRow(2, Vector4(GetZAxisUnsafe(), 0.f));
        m.SetRow(3, Vector4::ZERO_3D_POINT());

        return m;
    }

    RED_INLINE EulerAngles Quaternion::ToEulerAngles() const
    {
        const Float x2 = i * i;
        const Float y2 = j * j;
        const Float z2 = k * k;

        const Float unitLength = r * r + x2 + y2 + z2;
        const Float abcd = r * i + j * k;
        const Float eps = 0.001f;

        Float yaw, pitch, roll;

        if (abcd > (0.5f - eps) * unitLength)
        {
            yaw = RAD2DEG(2.f * ::atan2f(j, r));
            pitch = RAD2DEG(RED_PI_HALF);
            roll = 0.f;
        }
        else if (abcd < (-0.5f + eps) * unitLength)
        {
            yaw = RAD2DEG(-2.f * ::atan2f(j, r));
            pitch = RAD2DEG(-RED_PI_HALF);
            roll = 0;
        }
        else
        {
            const Float adbc = r * k - i * j;
            const Float acbd = r * j - i * k;
            yaw = RAD2DEG(::atan2f(2.f * adbc, 1.f - 2.f * (z2 + x2)));
            pitch = RAD2DEG(::asinf(2.f * abcd / unitLength));
            roll = RAD2DEG(::atan2f(2.f * acbd, 1.f - 2.f * (y2 + x2)));
        }

        return EulerAngles(roll, pitch, yaw);
    }

    RED_INLINE Quaternion Quaternion::Lerp(const Quaternion& q1, const Quaternion& q2, const Float t)
    {
        return Quaternion(vanguard::math::simd::QuadHelper::QuaternionLerp(q1.vec.vec, q2.vec.vec, t));
    }

    RED_INLINE Quaternion Quaternion::Slerp(const Quaternion& q1, const Quaternion& q2, const Float t, const Float tolerance)
    {
        return Quaternion(vanguard::math::simd::QuadHelper::QuaternionSlerp(q1.vec.vec, q2.vec.vec, t, tolerance));
    }

    RED_INLINE Bool Quaternion::IsOk() const
    {
        const float unitTolerance = 1e-2f;
        Bool valid = std::isfinite(i) && std::isfinite(j) && std::isfinite(k) && std::isfinite(r);
        Float length = Magnitude();
        Float err = length - 1.0f;
        Bool validLength = (vanguard::math::Abs(err) < unitTolerance); // changed because the vectorized math has a little bit less precision.
        valid = valid && validLength;
        return valid;
    }

    RED_INLINE Bool Quaternion::IsAlmostEqual(const Quaternion& q, const Float epsilon) const
    {
        const Quaternion delta = *this - q;
        return (vanguard::math::Abs(delta.r) < epsilon && vanguard::math::Abs(delta.i) < epsilon && vanguard::math::Abs(delta.j) < epsilon &&
                vanguard::math::Abs(delta.k) < epsilon);
    }

    RED_INLINE void Quaternion::RemoveAxisComponent(const Vector4& axis)
    {
        // Rotate the desired axis
        Vector4 rotatedAxis;
        Transform(axis);

        const Float dotProd = axis.Dot3(rotatedAxis);

        // Check to see if they're parallel.
        if ((dotProd - 1.0f) > -1e-3f)
        {
            SetIdentity();
            return;
        }

        // Check to see if they're opposite
        if ((dotProd + 1.0f) < 1e-3f)
        {
            Vector4 perpVector;
            Helper::CalculatePerpendicularVector(axis, perpVector);

            i = perpVector.X;
            j = perpVector.Y;
            k = perpVector.Z;
            r = RED_PI;

            return;
        }

        Vector4 rotationAxis = Vector4::Cross(axis, rotatedAxis, 0.f).Normalized3();
        *this = Quaternion(rotationAxis, MAcos_safe(dotProd));
    }

    RED_INLINE void Quaternion::RemoveAxisComponent(const Vector3& axis)
    {
        // Rotate the desired axis
        Vector3 rotatedAxis;
        Transform(axis);

        const Float dotProd = axis.Dot(rotatedAxis);

        // Check to see if they're parallel.
        if ((dotProd - 1.0f) > -1e-3f)
        {
            SetIdentity();
            return;
        }

        // Check to see if they're opposite
        if ((dotProd + 1.0f) < 1e-3f)
        {
            Vector4 perpVector;
            Helper::CalculatePerpendicularVector(axis, perpVector);

            i = perpVector.X;
            j = perpVector.Y;
            k = perpVector.Z;
            r = RED_PI;

            return;
        }

        const Float rotationAngle = MAcos_safe(dotProd);

        Vector4 rotationAxis = axis.Cross(rotatedAxis);
        rotationAxis.Normalize3();

        *this = Quaternion(rotationAxis, rotationAngle);
    }

    RED_INLINE void Quaternion::DecomposeRestAxis(const Vector4& axis, Quaternion& restOut, float& angleOut) const
    {
        Quaternion axisRot;
        {
            restOut = *this;
            restOut.RemoveAxisComponent(axis);
            axisRot = MulInverse(restOut);
        }

        angleOut = axisRot.GetAngle();
        const bool rev = ((axisRot.r * Helper::Dot(axis, axisRot)) < 0.0f);

        if (rev)
        {
            angleOut = -angleOut;
        }
    }

    RED_INLINE void Quaternion::DecomposeRestAxis(const Vector3& axis, Quaternion& restOut, float& angleOut) const
    {
        Quaternion axisRot;
        {
            restOut = *this;
            restOut.RemoveAxisComponent(axis);
            axisRot = MulInverse(restOut);
        }

        angleOut = axisRot.GetAngle();
        const bool rev = ((axisRot.r * Helper::Dot(axis, axisRot)) < 0.0f);

        if (rev)
        {
            angleOut = -angleOut;
        }
    }

    RED_INLINE void Quaternion::SetShortestRotation(const Vector4& from, const Vector4& to, const Float eps)
    {
        Vector4 perp;
        Helper::CalculatePerpendicularVector(from, perp);
        SetShortestRotation(from, to, perp, eps);
    }

    RED_INLINE void Quaternion::SetShortestRotation(const Vector4& from, const Vector4& to, const Vector4& perp, const Float eps)
    {
        const auto fromNormalized = from.Normalized3();
        const auto toNormalized = to.Normalized3();
        const Float dot = fromNormalized.Dot3(toNormalized);

        if (MAbs(dot + 1) < eps) // Singularity case (180 degree rotation)
        {
            Vector4 perpNormalized = perp.Normalized4();

            i = perpNormalized.X;
            j = perpNormalized.Y;
            k = perpNormalized.Z;
            r = 0.f;
        }
        else
        {
            // Quaternion for doubled angle is [cross.X, cross.Y, cross.Z, dot];
            // To get quaternion for single angle value, take identity quaternion [0, 0, 0, 1], add it to quaternion for doubled angle and
            // normalize the result
            const auto cross = Vector4::Cross(fromNormalized, toNormalized);

            i = cross.X;
            j = cross.Y;
            k = cross.Z;
            r = dot + 1;

            Normalize();
        }
    }

    RED_INLINE Quaternion& Quaternion::BuildFromDirectionVector(const Vector4& direction, const Vector4& upVec)
    {
        Matrix m;
        m.BuildFromDirectionVector(direction, upVec);
        *this = m.ToQuat();
        return *this;
    }

    RED_INLINE Quaternion& Quaternion::Scale(const Vector4& scale)
    {
        i *= scale.X;
        j *= scale.Y;
        k *= scale.Z;
        return *this;
    }

    RED_INLINE Quaternion Quaternion::MakeFromDirectionVector(const Vector4& direction, const Vector4& upVec)
    {
        Matrix m;
        m.BuildFromDirectionVector(direction, upVec);
        return m.ToQuat();
    }
} // namespace vanguard::math
