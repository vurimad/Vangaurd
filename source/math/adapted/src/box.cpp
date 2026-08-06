#include "build.h"
#include "box.h"
#include "segment.h"
#include "sphere.h"

namespace vanguard::math
{
    Bool Box::IntersectSegment(const Segment& segment, Vector4& enterPoint) const
    {
        Float t;
        Vector4 normal = segment.m_direction.Normalized3();
        Bool ret = IntersectRay(segment.m_origin, normal, t);
        if (ret && t * t <= segment.m_direction.SquareMag3())
        {
            enterPoint = segment.m_origin + normal * t;
            return true;
        }
        return false;
    }

    Bool Box::IntersectSegment(const Segment& segment, Vector4& enterPoint, Vector4& exitPoint) const
    {
        Float t1, t2;
        Vector4 normal = segment.m_direction.Normalized3();
        Bool ret = IntersectRay(segment.m_origin, normal, t1, &t2);
        if (ret && t1 * t1 <= segment.m_direction.SquareMag3())
        {
            enterPoint = segment.m_origin + normal * t1;
            exitPoint = segment.m_origin + normal * t2;
            return true;
        }
        return false;
    }

    Bool Box::IntersectLine(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin, Float& exitDistFromOrigin) const
    {
        Float tmin = -RED_FLT_MAX, tmax = RED_FLT_MAX;

        if (direction.X != 0.0f)
        {
            Float reverse = 1.0f / direction.X;
            Float tx1 = (Min.X - origin.X) * reverse;
            Float tx2 = (Max.X - origin.X) * reverse;

            tmin = vanguard::math::Min(tx1, tx2);
            tmax = vanguard::math::Max(tx1, tx2);
        }
        else
        {
            if (origin.X <= Min.X || origin.X >= Max.X)
                return false;
        }

        if (direction.Y != 0.0f)
        {
            Float reverse = 1.0f / direction.Y;
            Float tx1 = (Min.Y - origin.Y) * reverse;
            Float tx2 = (Max.Y - origin.Y) * reverse;

            tmin = vanguard::math::Max(tmin, vanguard::math::Min(tx1, tx2));
            tmax = vanguard::math::Min(tmax, vanguard::math::Max(tx1, tx2));
        }
        else
        {
            if (origin.Y <= Min.Y || origin.Y >= Max.X)
                return false;
        }

        if (direction.Z != 0.0f)
        {
            Float reverse = 1.0f / direction.Z;
            Float tx1 = (Min.Z - origin.Z) * reverse;
            Float tx2 = (Max.Z - origin.Z) * reverse;

            tmin = vanguard::math::Max(tmin, vanguard::math::Min(tx1, tx2));
            tmax = vanguard::math::Min(tmax, vanguard::math::Max(tx1, tx2));
        }
        else
        {
            if (origin.Z <= Min.Z || origin.Z >= Max.Z)
                return false;
        }

        enterDistFromOrigin = tmin;
        exitDistFromOrigin = tmax;

        return tmax >= tmin;
    }

    Bool Box::IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin, Float* exitDistFromOrigin) const
    {
        Float tmin, tmax;
        if (!IntersectLine(origin, direction, tmin, tmax))
        {
            return false;
        }
        if (tmax < 0.f)
        {
            return false;
        }
        if (tmin < 0.f)
        {
            tmin = 0.f;
        }

        enterDistFromOrigin = tmin;
        if (exitDistFromOrigin)
        {
            (*exitDistFromOrigin) = tmax;
        }

        return true;
    }

    Bool Box::IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const
    {
        Float t;
        Bool ret = IntersectRay(origin, direction, t);
        enterPoint = origin + direction * t;
        return ret;
    }

    Bool Box::IntersectSphere(const Sphere& sphere) const
    {
        const Vector4& sphereCenter = sphere.GetCenter();
        Float distanceMinSqr = 0;

        for (Uint32 i = 0; i < 3; ++i)
        {
            if (sphereCenter[i] < Min[i])
            {
                const Float axisDistanceSqr = sphereCenter[i] - Min[i];
                distanceMinSqr += axisDistanceSqr * axisDistanceSqr;
            }
            else if (sphereCenter[i] > Max[i])
            {
                const Float axisDistanceSqr = sphereCenter[i] - Max[i];
                distanceMinSqr += axisDistanceSqr * axisDistanceSqr;
            }
        }

        return distanceMinSqr <= sphere.GetSquareRadius();
    }

} // namespace vanguard::math