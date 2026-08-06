#include "build.h"
#include "simdVector4.h"

#include "simdQSTransform.h"

namespace vanguard::math::simd
{
    void Vector4::Lerp(const Vector4& _a, const float _weight)
    {
        Set(Add(vanguard::math::simd::Mul(*this, 1.0f - _weight), vanguard::math::simd::Mul(_a, _weight)));
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 Vector4::Lerp(const Vector4& _a, const Vector4& _b, const float _weight)
    {
        return Add(Mul(_a, 1.0f - _weight), Mul(_b, _weight));
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4& Vector4::SetTransformedPos(const QsTransform& _trans, const Vector4& _v)
    {
        Vector4 scaled;
        Vector4 rotated;
        scaled = Mul(_v, _trans.Scale);
        rotated.RotateDirection(_trans.Rotation, scaled);
        Set(Add(rotated, _trans.Translation));

        return *this;
    }

    // TODO: optimize
    //////////////////////////////////////////////////////////////////////////
    void Vector4::SetTransformedInversePos(const QsTransform& _t, const Vector4& _v)
    {
        Vector4 temp(_v);
        SetSub(temp, _t.Translation);

        const Float X_0 = 1.0f - 2.0f * _t.Rotation.j * _t.Rotation.j - 2.0f * _t.Rotation.k * _t.Rotation.k;
        const Float Y_0 = 2.0f * _t.Rotation.i * _t.Rotation.j - 2.0f * _t.Rotation.k * _t.Rotation.r;
        const Float Z_0 = 2.0f * _t.Rotation.i * _t.Rotation.k - 2.0f * _t.Rotation.j * _t.Rotation.r;

        const Float X_1 = 2.0f * _t.Rotation.i * _t.Rotation.j - 2.0f * _t.Rotation.k * _t.Rotation.r;
        const Float Y_1 = 1.0f - 2.0f * _t.Rotation.i * _t.Rotation.i - 2.0f * _t.Rotation.k * _t.Rotation.k;
        const Float Z_1 = 2.0f * _t.Rotation.j * _t.Rotation.k - 2.0f * _t.Rotation.i * _t.Rotation.r;

        const Float X_2 = 2.0f * _t.Rotation.i * _t.Rotation.k - 2.0f * _t.Rotation.j * _t.Rotation.r;
        const Float Y_2 = 2.0f * _t.Rotation.j * _t.Rotation.k - 2.0f * _t.Rotation.i * _t.Rotation.r;
        const Float Z_2 = 1.0f - 2.0f * _t.Rotation.i * _t.Rotation.i - 2.0f * _t.Rotation.j * _t.Rotation.j;

        Vector4& t = *this;
        t.X = (X_0 * temp.X) + (X_1 * temp.Y) + (X_2 * temp.Z);
        t.Y = (Y_0 * temp.X) + (Y_1 * temp.Y) + (Y_2 * temp.Z);
        t.Z = (Z_0 * temp.X) + (Z_1 * temp.Y) + (Z_2 * temp.Z);
        t.W = 0.0f;

        X *= 1.0f / _t.Scale.X;
        Y *= 1.0f / _t.Scale.Y;
        Z *= 1.0f / _t.Scale.Z;
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar Vector4::DistanceSquaredTo(const Vector4& _t) const
    {
        return Sub(*this, _t).SquareLength3();
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar Vector4::DistanceTo(const Vector4& _t) const
    {
        return Sub(*this, _t).Length3();
    }
} // namespace vanguard::math::simd
