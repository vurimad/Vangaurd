/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#include "build.h"
#include "mathUtils.h"

namespace vanguard::math
{
    namespace utils
    {

        Bool SolveQuadraticEquation(const Float a, const Float b, const Float c, Float& x1, Float& x2)
        {
            Float determinant = b * b - 4.0f * a * c;
            if (determinant < 0.0f)
                return false;

            if (a == 0.0f)
            {
                if (b == 0.0f)
                {
                    if (c == 0.0f)
                    {
                        x1 = x2 = 0.0f;
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                x1 = x2 = -c / b;
                return true;
            }

            determinant = ::sqrtf(determinant);
            Float t = 1.0f / (2.0f * a);
            x1 = (-determinant - b) * t;
            x2 = (determinant - b) * t;
            return true;
        }

        Float BezierSmoothStep(const Float t)
        {
            return Clamp(-2.f * t * t * t + 3.f * t * t, 0.f, 1.f);
        }

        Float SmoothstepInterpolation(Float t)
        {
            return Clamp(t * t * t * (t * (t * 6.f - 15.f) + 10.f), 0.f, 1.f);
        }

        Vector3 ComputeBaryCentric(const Vector3& p, const Vector3& p0, const Vector3& p1, const Vector3& p2)
        {
            const auto _triangle_area = [](const Vector3& p0, const Vector3& p1, const Vector3& p2) -> Float
            {
                Vector3 p10 = p1 - p0;
                Vector3 p20 = p2 - p0;
                Vector3 u = p10.Cross(p20);

                return u.Mag();
            };

            const Float divisor = _triangle_area(p0, p1, p2);

            auto u = _triangle_area(p1, p2, p) / divisor;
            auto v = _triangle_area(p2, p0, p) / divisor;
            auto w = _triangle_area(p0, p1, p) / divisor;

            return {u, v, w};
        }

        Vector4 ComputeBaryCentric(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
        {
            const auto _scalar_triple_product = [](const Vector3& a, const Vector3& b, const Vector3& c) -> Float
            { return a.Dot(b.Cross(c)); };

            const auto vap = p - a;
            const auto vbp = p - b;

            const auto vab = b - a;
            const auto vac = c - a;
            const auto vad = d - a;

            const auto vbc = c - b;
            const auto vbd = d - b;

            const Float va6 = _scalar_triple_product(vbp, vbd, vbc);
            const Float vb6 = _scalar_triple_product(vap, vac, vad);
            const Float vc6 = _scalar_triple_product(vap, vad, vab);
            const Float vd6 = _scalar_triple_product(vap, vab, vac);
            const Float v6 = 1.f / _scalar_triple_product(vab, vac, vad);
            return {va6 * v6, vb6 * v6, vc6 * v6, vd6 * v6};
        }

        Bool IsPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b)
        {
            const auto AB = (b - a).Mag();
            const auto AP = (p - a).Mag();
            const auto PB = (b - p).Mag();

            const auto result = fabsf(AB - (AP + PB));
            return result < 1.0e-6f;
        }

        Vector4 ClosestPointOnTriangle(const Vector4& p0, const Vector4& p1, const Vector4& p2, const Vector4& p)
        {
            auto edge0 = p1 - p0;
            auto edge1 = p2 - p0;
            auto v0 = p0 - p;

            Float a = edge0.Dot3(edge0);
            Float b = edge0.Dot3(edge1);
            Float c = edge1.Dot3(edge1);
            Float d = edge0.Dot3(v0);
            Float e = edge1.Dot3(v0);

            Float det = a * c - b * b;
            Float s = b * e - c * d;
            Float t = b * d - a * e;

            if (s + t < det)
            {
                if (s < 0.f)
                {
                    if (t < 0.f)
                    {
                        if (d < 0.f)
                        {
                            s = Clamp(-d / a, 0.f, 1.f);
                            t = 0.f;
                        }
                        else
                        {
                            s = 0.f;
                            t = Clamp(-e / c, 0.f, 1.f);
                        }
                    }
                    else
                    {
                        s = 0.f;
                        t = Clamp(-e / c, 0.f, 1.f);
                    }
                }
                else if (t < 0.f)
                {
                    s = Clamp(-d / a, 0.f, 1.f);
                    t = 0.f;
                }
                else
                {
                    Float invDet = 1.f / det;
                    s *= invDet;
                    t *= invDet;
                }
            }
            else
            {
                if (s < 0.f)
                {
                    Float tmp0 = b + d;
                    Float tmp1 = c + e;
                    if (tmp1 > tmp0)
                    {
                        Float numer = tmp1 - tmp0;
                        Float denom = a - 2 * b + c;
                        s = Clamp(numer / denom, 0.f, 1.f);
                        t = 1 - s;
                    }
                    else
                    {
                        t = Clamp(-e / c, 0.f, 1.f);
                        s = 0.f;
                    }
                }
                else if (t < 0.f)
                {
                    if (a + d > b + e)
                    {
                        Float numer = c + e - b - d;
                        Float denom = a - 2 * b + c;
                        s = Clamp(numer / denom, 0.f, 1.f);
                        t = 1 - s;
                    }
                    else
                    {
                        s = Clamp(-e / c, 0.f, 1.f);
                        t = 0.f;
                    }
                }
                else
                {
                    float numer = c + e - b - d;
                    float denom = a - 2 * b + c;
                    s = Clamp(numer / denom, 0.f, 1.f);
                    t = 1.f - s;
                }
            }

            return p0 + edge0 * s + edge1 * t;
        }
    } // namespace utils
} // namespace vanguard::math