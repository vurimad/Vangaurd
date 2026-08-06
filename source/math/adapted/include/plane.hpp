/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    RED_INLINE Plane::Plane(const Vector4& normal, const Float& distance)
    {
        NormalDistance[0] = normal[0];
        NormalDistance[1] = normal[1];
        NormalDistance[2] = normal[2];
        NormalDistance[3] = -distance;
    }

    RED_INLINE void Plane::SetPlane(const Vector4& normal, const Vector4& point)
    {
        RED_ASSERT(normal.Mag3() > 0.995f && normal.Mag3() < 1.005f);
        NormalDistance[0] = normal[0];
        NormalDistance[1] = normal[1];
        NormalDistance[2] = normal[2];
        NormalDistance[3] = -Vector4::Dot3(point, normal);
    }

    RED_INLINE Plane::Plane(const Vector4& normal, const Vector4& point)
    {
        SetPlane(normal, point);
    }

    RED_INLINE Plane::Plane(const Vector4& p1, const Vector4& p2, const Vector4& p3)
    {
        SetPlane(p1, p2, p3);
    }

    RED_INLINE Float Plane::DistanceTo(const Vector4& point) const
    {
        return Vector4::Dot3(point, NormalDistance) + NormalDistance[3];
    }

    RED_INLINE Float Plane::DistanceTo(const Vector4& plane, const Vector4& point)
    {
        return Vector4::Dot3(point, plane) + plane[3];
    }

    RED_INLINE Plane::ESide Plane::GetSide(const Vector4& point) const
    {
        Float distance = DistanceTo(point);

        if (distance < 0.0f)
        {
            return Plane::PS_Back;
        }
        if (distance > 0.0f)
        {
            return Plane::PS_Front;
        }
        return Plane::PS_None;
    }

    RED_INLINE Plane::ESide Plane::GetSide(const Box& box) const
    {
        const Vector4 boxCenter = box.CalcCenter();
        const Vector4 boxExtents = box.CalcExtents();
        return GetSide(boxCenter, boxExtents);
    }

    RED_INLINE Plane::ESide Plane::GetSide(const Vector4& boxCenter, const Vector4& boxExtents) const
    {
        Float distance = DistanceTo(boxCenter);

        Float maxDistance = 0.0f;
        maxDistance += Abs(NormalDistance[0] * boxExtents[0]);
        maxDistance += Abs(NormalDistance[1] * boxExtents[1]);
        maxDistance += Abs(NormalDistance[2] * boxExtents[2]);

        if (distance < -maxDistance)
        {
            return Plane::PS_Back;
        }
        if (distance > +maxDistance)
        {
            return Plane::PS_Front;
        }
        return Plane::PS_Both;
    }

    RED_INLINE Vector4 Plane::Project(const Vector4& point) const
    {
        return point - NormalDistance * DistanceTo(point);
    }

    RED_INLINE Bool Plane::FrontIntersectLine(const Vector4& origin, const Vector4& direction, Vector4& intersectionPoint,
                                              Float& intersectionDistance) const
    {
        Float proj = -Vector4::Dot3(NormalDistance, direction);
        if (proj > 0.0f)
        {
            intersectionDistance = DistanceTo(origin) / proj;
            intersectionPoint = origin + (direction * intersectionDistance);
            return true;
        }

        return false;
    }

    RED_INLINE Plane::ESide Plane::IntersectLine(const Vector4& origin, const Vector4& direction, Vector4& intersectionPoint,
                                                 Float& intersectionDistance) const
    {
        Float proj = -Vector4::Dot3(NormalDistance, direction);
        if (Abs(proj) < 0.0001f)
        {
            return PS_None;
        }

        intersectionDistance = DistanceTo(origin) / proj;
        intersectionPoint = origin + (direction * intersectionDistance);
        return (proj >= 0.0f) ? PS_Front : PS_Back;
    }

    RED_INLINE Bool Plane::IntersectPlanes(const Plane& p0, const Plane& p1, Vector4& outOrigin, Vector4& outDirection)
    {
        Vector4 cross = Vector4::Cross(p0.NormalDistance, p1.NormalDistance);
        const Float det = cross.SquareMag3();
        if (det == 0.f)
        {
            return false;
        }

        outOrigin = (Vector4::Cross(cross, p1.NormalDistance) * p0.NormalDistance.W +
                     Vector4::Cross(p0.NormalDistance, cross) * p1.NormalDistance.W) /
                    det;

        outDirection = cross * (1.f / sqrtf(det));
        return true;
    }

    RED_INLINE Bool Plane::IsOk() const
    {
        if (!NormalDistance.IsOk())
            return false;

        if (!NormalDistance.IsNormalized3())
            return false;

        return true;
    }

    RED_INLINE Plane Plane::operator-() const
    {
        Plane ret;
        ret.NormalDistance = -NormalDistance;
        return ret;
    }

    RED_INLINE void Plane::SetPlane(const Vector4& p1, const Vector4& p2, const Vector4& p3)
    {
        NormalDistance = Vector4::Cross(p2 - p1, p3 - p1);
        NormalDistance.Normalize3();
        NormalDistance[3] = -Vector4::Dot3(p1, NormalDistance);
    }

} // namespace vanguard::math