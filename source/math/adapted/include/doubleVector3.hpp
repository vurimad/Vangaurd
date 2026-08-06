/**
 * Copyright © 2007 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    RED_FORCE_INLINE DoubleVector3::DoubleVector3(Double _x, Double _y, Double _z) : X(_x), Y(_y), Z(_z)
    {
        RED_MATH_CHECK_FINITE(_x);
        RED_MATH_CHECK_FINITE(_y);
        RED_MATH_CHECK_FINITE(_z);
    }

    RED_FORCE_INLINE DoubleVector3::DoubleVector3(const Double* ptr) : X(ptr[0]), Y(ptr[1]), Z(ptr[2])
    {
        RED_MATH_CHECK_FINITE(ptr[0]);
        RED_MATH_CHECK_FINITE(ptr[1]);
        RED_MATH_CHECK_FINITE(ptr[2]);
    }

    RED_FORCE_INLINE DoubleVector3::DoubleVector3(const DoubleVector3& other) : X(other.X), Y(other.Y), Z(other.Z)
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
    }

    RED_FORCE_INLINE DoubleVector3::DoubleVector3(const Vector3& vec, const Vector3& vecError)
        : X((Double)vec.X + (Double)vecError.X), Y((Double)vec.Y + (Double)vecError.Y), Z((Double)vec.Z + (Double)vecError.Z)
    {
        RED_MATH_CHECK_FINITE(vec.X);
        RED_MATH_CHECK_FINITE(vec.Y);
        RED_MATH_CHECK_FINITE(vec.Z);
        RED_MATH_CHECK_FINITE(vecError.X);
        RED_MATH_CHECK_FINITE(vecError.Y);
        RED_MATH_CHECK_FINITE(vecError.Z);
    }

    RED_FORCE_INLINE DoubleVector3::DoubleVector3(const Vector& vec, const Vector3& vecError)
        : X((Double)vec.X + (Double)vecError.X), Y((Double)vec.Y + (Double)vecError.Y), Z((Double)vec.Z + (Double)vecError.Z)
    {
        RED_MATH_CHECK_FINITE(vec.X);
        RED_MATH_CHECK_FINITE(vec.Y);
        RED_MATH_CHECK_FINITE(vec.Z);
        RED_MATH_CHECK_FINITE(vecError.X);
        RED_MATH_CHECK_FINITE(vecError.Y);
        RED_MATH_CHECK_FINITE(vecError.Z);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator=(const DoubleVector3& other)
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        X = other.X;
        Y = other.Y;
        Z = other.Z;
        return *this;
    }

    RED_FORCE_INLINE Vector3 DoubleVector3::ToVector3(Vector3& errorTerm) const
    {
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);

        const Vector3 ret((Float)X, (Float)Y, (Float)Z);
        const Float errX = (Float)(X - (Double)ret.X);
        const Float errY = (Float)(Y - (Double)ret.Y);
        const Float errZ = (Float)(Z - (Double)ret.Z);

        errorTerm = Vector3(errX, errY, errZ);
        return ret;
    }

    RED_FORCE_INLINE Vector DoubleVector3::ToVector(Vector3& errorTerm) const
    {
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);

        const Vector3 ret((Float)X, (Float)Y, (Float)Z);
        const Float errX = (Float)(X - (Double)ret.X);
        const Float errY = (Float)(Y - (Double)ret.Y);
        const Float errZ = (Float)(Z - (Double)ret.Z);

        errorTerm = Vector3(errX, errY, errZ);
        return ret;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator+(const DoubleVector3& other) const
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        return DoubleVector3(X + other.X, Y + other.Y, Z + other.Z);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator+=(const DoubleVector3& other)
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        X += other.X;
        Y += other.Y;
        Z += other.Z;
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return *this;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator-(const DoubleVector3& other) const
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        return DoubleVector3(X - other.X, Y - other.Y, Z - other.Z);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator-=(const DoubleVector3& other)
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        X -= other.X;
        Y -= other.Y;
        Z -= other.Z;
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return *this;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator*(const DoubleVector3& other) const
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        return DoubleVector3(X * other.X, Y * other.Y, Z * other.Z);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator*=(const DoubleVector3& other)
    {
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_FINITE(other.Z);
        X *= other.X;
        Y *= other.Y;
        Z *= other.Z;
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return *this;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator*(Double scalar) const
    {
        RED_MATH_CHECK_FINITE(scalar);
        return DoubleVector3(X * scalar, Y * scalar, Z * scalar);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator*=(Double scalar)
    {
        RED_MATH_CHECK_FINITE(scalar);
        X *= scalar;
        Y *= scalar;
        Z *= scalar;
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return *this;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator/(const DoubleVector3& other) const
    {
        RED_MATH_CHECK_NONZERO(other.X);
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_NONZERO(other.Y);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_NONZERO(other.Z);
        RED_MATH_CHECK_FINITE(other.Z);
        return DoubleVector3(X / other.X, Y / other.Y, Z / other.Z);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator/=(const DoubleVector3& other)
    {
        RED_MATH_CHECK_NONZERO(other.X);
        RED_MATH_CHECK_FINITE(other.X);
        RED_MATH_CHECK_NONZERO(other.Y);
        RED_MATH_CHECK_FINITE(other.Y);
        RED_MATH_CHECK_NONZERO(other.Z);
        RED_MATH_CHECK_FINITE(other.Z);
        X /= other.X;
        Y /= other.Y;
        Z /= other.Z;
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return *this;
    }

    RED_FORCE_INLINE DoubleVector3 DoubleVector3::operator/(Double scalar) const
    {
        RED_MATH_CHECK_NONZERO(scalar);
        RED_MATH_CHECK_FINITE(scalar);
        return DoubleVector3(X / scalar, Y / scalar, Z / scalar);
    }

    RED_FORCE_INLINE DoubleVector3& DoubleVector3::operator/=(Double scalar)
    {
        RED_MATH_CHECK_NONZERO(scalar);
        RED_MATH_CHECK_FINITE(scalar);
        X /= scalar;
        Y /= scalar;
        Z /= scalar;
        return *this;
    }

    RED_FORCE_INLINE Double DoubleVector3::Mag() const
    {
        RED_MATH_CHECK_FINITE(X);
        RED_MATH_CHECK_FINITE(Y);
        RED_MATH_CHECK_FINITE(Z);
        return sqrt(X * X + Y * Y + Z * Z);
    }

    RED_FORCE_INLINE Double DoubleVector3::Distance(const DoubleVector3& other) const
    {
        return (*this - other).Mag();
    }

    namespace consts
    {
        extern const DoubleVector3 EX;
        extern const DoubleVector3 EY;
        extern const DoubleVector3 EZ;

        extern const DoubleVector3 ZEROS;
        extern const DoubleVector3 ONES;

    } // namespace consts

    RED_FORCE_INLINE const DoubleVector3& DoubleVector3::EX()
    {
        return consts::EX;
    }

    RED_FORCE_INLINE const DoubleVector3& DoubleVector3::EY()
    {
        return consts::EY;
    }

    RED_FORCE_INLINE const DoubleVector3& DoubleVector3::EZ()
    {
        return consts::EZ;
    }

    RED_FORCE_INLINE const DoubleVector3& DoubleVector3::ZEROS()
    {
        return consts::ZEROS;
    }

    RED_FORCE_INLINE const DoubleVector3& DoubleVector3::ONES()
    {
        return consts::ONES;
    }

} // namespace vanguard::math