#include "build.h"
#include "fixedCapsule.h"
#include "sphere.h"

namespace vanguard::math
{

    Bool FixedCapsule::Contains(const Vector4& point) const
    {
        Float r2 = GetRadius() * GetRadius();

        // TODO: optimalization
        Float distSqr = 0.f;
        {
            Vector4 pointA = CalcPointA();
            Vector4 pointB = CalcPointB();

            Vector4 v = point - pointA;

            Vector4 s = pointB - pointA;

            Float lenSq = s.SquareMag3();
            Float dot = v.Dot3(s) / lenSq;

            Vector4 disp = s * dot;

            if ((dot > 1.f || dot < 0.f) && point.DistanceSquaredTo(pointA) > r2 && point.DistanceSquaredTo(pointB) > r2)
            {
                return false;
            }

            v -= disp;

            distSqr = v.SquareMag3();
        }

        return distSqr <= r2;
    }

    Bool FixedCapsule::Contains(const Sphere& sphere) const
    {
        Float r = sphere.GetRadius() + GetRadius();
        Float r2 = r * r;

        // TODO: optimalization
        Float distSqr = 0.f;
        {
            Vector4 pointA = CalcPointA();
            Vector4 pointB = CalcPointB();

            Vector4 v = sphere.GetCenter() - pointA;

            Vector4 s = pointB - pointA;

            Float lenSq = s.SquareMag3();
            Float dot = v.Dot3(s) / lenSq;

            Vector4 disp = s * dot;

            if ((dot > 1.f || dot < 0.f) && sphere.GetCenter().DistanceSquaredTo(pointA) > r2 && sphere.GetCenter().DistanceSquaredTo(pointB) > r2)
            {
                return false;
            }

            v -= disp;

            distSqr = v.SquareMag3();
        }

        return distSqr <= r2;
    }

} // namespace vanguard::math