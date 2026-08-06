/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#include "build.h"
#include "cylinder.h"
#include "mathUtils.h"
#include "segment.h"

namespace vanguard::math
{
    Cylinder::Cylinder(const Vector4& pos1, const Vector4& pos2, Float radius) : m_positionAndRadius(pos1)
    {
        m_positionAndRadius.W = radius;
        Vector4 v = pos2 - pos1;
        m_normalAndHeight.W = v.Mag3();
        Float t = 1 / m_normalAndHeight.W;
        m_normalAndHeight.X = v.X * t;
        m_normalAndHeight.Y = v.Y * t;
        m_normalAndHeight.Z = v.Z * t;
    }

    Bool Cylinder::Contains(const Vector4& point) const
    {
        Float d = Vector4::Dot3(point, m_normalAndHeight);
        Float p = Vector4::Dot3(m_positionAndRadius, m_normalAndHeight);
        if (d <= p || d >= p + GetHeight())
            return false;
        Vector4 t = m_positionAndRadius - point;

        return (t - m_normalAndHeight * Vector4::Dot3(t, m_normalAndHeight)).SquareMag3() < m_positionAndRadius.W * m_positionAndRadius.W;
    }

    Bool Cylinder::IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const
    {
        Vector4 t1 = Vector4::Cross(origin - m_positionAndRadius, m_normalAndHeight);
        Vector4 t2 = Vector4::Cross(direction, m_normalAndHeight);

        Float x1;
        Float x2;
        if (!utils::SolveQuadraticEquation(Vector4::Dot3(t2, t2), 2 * Vector4::Dot3(t1, t2),
                                           Vector4::Dot3(t1, t1) - m_positionAndRadius.W * m_positionAndRadius.W, x1, x2) ||
            t1 == t2)
            return false;

        Float min = Min(x1, x2);
        Float max = Max(x1, x2);

        Float o = Vector4::Dot3(m_positionAndRadius, m_normalAndHeight);
        Float s = Vector4::Dot3(origin, m_normalAndHeight);
        Float v = Vector4::Dot3(direction, m_normalAndHeight);

        if (v == 0)
        {
            if (s <= o || s >= o + m_normalAndHeight.W)
                return false;
        }
        else
        {
            Float reverse = 1.0f / v;
            x1 = (o - s) * reverse;
            x2 = (o + m_normalAndHeight.W - s) * reverse;
            min = Max(min, Min(x1, x2));
            max = Min(max, Max(x1, x2));
        }

        if (min >= max || max < 0)
            return false;

        enterDistFromOrigin = min;
        return true;
    }

    Bool Cylinder::IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const
    {
        Float t;
        Bool ret = IntersectRay(origin, direction, t);
        enterPoint = origin + direction * t;
        return ret;
    }

    Bool Cylinder::IntersectSegment(const Segment& segment, Vector4& enterPoint) const
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

    EulerAngles Cylinder::GetOrientation() const
    {
        return EulerAngles(RAD2DEG(::atan2f(m_normalAndHeight.X, -m_normalAndHeight.Y)),
                           RAD2DEG(::atan2f(m_normalAndHeight.Z, ::sqrtf(m_normalAndHeight.X * m_normalAndHeight.X +
                                                                         m_normalAndHeight.Y * m_normalAndHeight.Y))),
                           0.0f);
    }

} // namespace vanguard::math