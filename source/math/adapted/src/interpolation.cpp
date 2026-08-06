#include "build.h"
#include "interpolation.h"
#include "simdQuad.h"

namespace vanguard::math
{
    Vector4 Interpolation<Vector4>::LinearSlow(const Vector4& src, const Vector4& dst, const red::Float t)
    {
        return src * (1.f - t) + dst * t;
    }

    Vector4 Interpolation<Vector4>::Linear(const Vector4& src, const Vector4& dst, const red::Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        Linear_SIMD(result.AsFloat(), src.AsFloat(), dst.AsFloat(), t);
        return result;
    }

    Vector4 Interpolation<Vector4>::CubicHermite(const Vector4& p0, const Vector4& c0, const Vector4& c1, const Vector4& p1,
                                                 const red::Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        CubicHermite_SIMD(result.AsFloat(), p0.AsFloat(), c0.AsFloat(), c1.AsFloat(), p1.AsFloat(), t);
        return result;
    }

    Vector4 Interpolation<Vector4>::QuadraticBezier(const Vector4& p0, const Vector4& c0, const Vector4& p1, const Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        QuadraticBezier_SIMD(result.AsFloat(), p0.AsFloat(), c0.AsFloat(), p1.AsFloat(), t);
        return result;
    }

    Vector4 Interpolation<Vector4>::CubicBezier(const Vector4& p0, const Vector4& c0, const Vector4& c1, const Vector4& p1,
                                                const red::Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        CubicBezier_SIMD(result.AsFloat(), p0.AsFloat(), c0.AsFloat(), c1.AsFloat(), p1.AsFloat(), t);
        return result;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    Color Interpolation<Color>::LinearSlow(const Color& src, const Color& dst, const red::Float t)
    {
        return Color::Lerp(t, src, dst);
    }

    Color Interpolation<Color>::Linear(const Color& src, const Color& dst, const red::Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        Linear_SIMD(result.AsFloat(), src.ToVector().AsFloat(), dst.ToVector().AsFloat(), t);
        return Color(result);
    }

    Color Interpolation<Color>::CubicHermite(const Color& p0, const Color& p1, const Color& p2, const Color& p3, const Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        CubicHermite_SIMD(result.AsFloat(), p0.ToVector().AsFloat(), p1.ToVector().AsFloat(), p2.ToVector().AsFloat(),
                          p3.ToVector().AsFloat(), t);
        return Color(result);
    }

    Color Interpolation<Color>::QuadraticBezier(const Color& p0, const Color& p1, const Color& p2, const Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        QuadraticBezier_SIMD(result.AsFloat(), p0.ToVector().AsFloat(), p1.ToVector().AsFloat(), p2.ToVector().AsFloat(), t);
        return Color(result);
    }

    Color Interpolation<Color>::CubicBezier(const Color& p0, const Color& p1, const Color& p2, const Color& p3, const Float _t)
    {
        Vector4 result;
        RED_ALIGNED_VAR(Float, 16) t[] = {_t, _t, _t, _t};
        CubicBezier_SIMD(result.AsFloat(), p0.ToVector().AsFloat(), p1.ToVector().AsFloat(), p2.ToVector().AsFloat(),
                         p3.ToVector().AsFloat(), t);
        return Color(result);
    }

    void Linear_SIMD(Float out[4], const Float src[4], const Float dst[4], const Float t[4])
    {
        RED_MATH_CHECK_SIMD_ALIGNMENT(out);
        RED_MATH_CHECK_SIMD_ALIGNMENT(src);
        RED_MATH_CHECK_SIMD_ALIGNMENT(dst);
        RED_MATH_CHECK_SIMD_ALIGNMENT(t);

        *(vanguard::math::simd::Quad*)out =
            _mm_add_ps(_mm_mul_ps(*(vanguard::math::simd::Quad*)dst, *(vanguard::math::simd::Quad*)t),
                       _mm_mul_ps(*(vanguard::math::simd::Quad*)src, _mm_sub_ps(_mm_set_ps1(1.f), *(vanguard::math::simd::Quad*)t)));
    }

    void CubicHermite_SIMD(Float out[4], const Float A[4], const Float B[4], const Float C[4], const Float D[4], const Float t[4])
    {
        RED_MATH_CHECK_SIMD_ALIGNMENT(out);
        RED_MATH_CHECK_SIMD_ALIGNMENT(A);
        RED_MATH_CHECK_SIMD_ALIGNMENT(B);
        RED_MATH_CHECK_SIMD_ALIGNMENT(C);
        RED_MATH_CHECK_SIMD_ALIGNMENT(D);
        RED_MATH_CHECK_SIMD_ALIGNMENT(t);

        const vanguard::math::simd::Quad half = _mm_set_ps1(0.5f);
        const vanguard::math::simd::Quad one_half = _mm_set_ps1(1.5f);
        const vanguard::math::simd::Quad neg_a = _mm_mul_ps(_mm_set_ps1(-1.f), *(vanguard::math::simd::Quad*)A);

        const vanguard::math::simd::Quad a =
            _mm_add_ps(_mm_sub_ps(_mm_add_ps(_mm_mul_ps(neg_a, half), _mm_mul_ps(one_half, *(vanguard::math::simd::Quad*)B)),
                                  _mm_mul_ps(one_half, *(vanguard::math::simd::Quad*)C)),
                       _mm_mul_ps(half, *(vanguard::math::simd::Quad*)D));

        const vanguard::math::simd::Quad b = _mm_add_ps(
            _mm_sub_ps(*(vanguard::math::simd::Quad*)A, _mm_mul_ps(_mm_set_ps1(2.5f), *(vanguard::math::simd::Quad*)B)),
            _mm_sub_ps(_mm_mul_ps(_mm_set_ps1(2.0f), *(vanguard::math::simd::Quad*)C), _mm_mul_ps(half, *(vanguard::math::simd::Quad*)D)));

        const vanguard::math::simd::Quad c = _mm_add_ps(_mm_mul_ps(half, neg_a), _mm_mul_ps(half, *(vanguard::math::simd::Quad*)C));

        const vanguard::math::simd::Quad a_t_cubed = _mm_mul_ps(
            *(vanguard::math::simd::Quad*)t, _mm_mul_ps(*(vanguard::math::simd::Quad*)t, _mm_mul_ps(*(vanguard::math::simd::Quad*)t, a)));
        const vanguard::math::simd::Quad b_t_squared =
            _mm_mul_ps(*(vanguard::math::simd::Quad*)t, _mm_mul_ps(*(vanguard::math::simd::Quad*)t, b));
        const vanguard::math::simd::Quad c_t = _mm_mul_ps(*(vanguard::math::simd::Quad*)t, c);

        *(vanguard::math::simd::Quad*)out =
            _mm_add_ps(_mm_add_ps(a_t_cubed, b_t_squared), _mm_add_ps(c_t, *(vanguard::math::simd::Quad*)B));
    }

    void QuadraticBezier_SIMD(Float out[4], const Float p0[4], const Float p1[4], const Float p2[4], const Float t[4])
    {
        RED_MATH_CHECK_SIMD_ALIGNMENT(out);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p0);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p1);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p2);
        RED_MATH_CHECK_SIMD_ALIGNMENT(t);

        const vanguard::math::simd::Quad one_minus_t = _mm_sub_ps(_mm_set_ps1(1.f), *(vanguard::math::simd::Quad*)t);

        *(vanguard::math::simd::Quad*)out = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(_mm_mul_ps(one_minus_t, one_minus_t), *(vanguard::math::simd::Quad*)p0),
                       _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_set_ps1(2.f), *(vanguard::math::simd::Quad*)t), one_minus_t),
                                  *(vanguard::math::simd::Quad*)p1)),
            _mm_mul_ps(_mm_mul_ps(*(vanguard::math::simd::Quad*)t, *(vanguard::math::simd::Quad*)t), *(vanguard::math::simd::Quad*)p2));
    }

    void CubicBezier_SIMD(Float out[4], const Float p0[4], const Float p1[4], const Float p2[4], const Float p3[4], const Float t[4])
    {
        RED_MATH_CHECK_SIMD_ALIGNMENT(out);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p0);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p1);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p2);
        RED_MATH_CHECK_SIMD_ALIGNMENT(p3);
        RED_MATH_CHECK_SIMD_ALIGNMENT(t);

        const vanguard::math::simd::Quad one_minus_t = _mm_sub_ps(_mm_set_ps1(1.f), *(vanguard::math::simd::Quad*)t);

        *(vanguard::math::simd::Quad*)out = _mm_add_ps(
            _mm_add_ps(
                _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(one_minus_t, one_minus_t), one_minus_t), *(vanguard::math::simd::Quad*)p0),
                _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_set_ps1(3.f), *(vanguard::math::simd::Quad*)t), _mm_mul_ps(one_minus_t, one_minus_t)),
                           *(vanguard::math::simd::Quad*)p1)),
            _mm_add_ps(_mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_set_ps1(3.f),
                                                        _mm_mul_ps(*(vanguard::math::simd::Quad*)t, *(vanguard::math::simd::Quad*)t)),
                                             one_minus_t),
                                  *(vanguard::math::simd::Quad*)p2),
                       _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(*(vanguard::math::simd::Quad*)t, *(vanguard::math::simd::Quad*)t),
                                             *(vanguard::math::simd::Quad*)t),
                                  *(vanguard::math::simd::Quad*)p3)));
    }
} // namespace vanguard::math