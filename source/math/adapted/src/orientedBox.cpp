#include "build.h"
#include "orientedBox.h"
#include "segment.h"

namespace vanguard::math
{
    OrientedBox::OrientedBox(const Vector4& pos, const Vector4& edge1, const Vector4& edge2, const Vector4& edge3) : m_position(pos)
    {
        Float len = edge1.Mag3();
        m_edge1 = edge1 / len;
        m_edge1.W = len;
        len = edge2.Mag3();
        m_edge2 = edge2 / len;
        m_edge2.W = len;

        Vector4 v = Vector4::Cross(m_edge1, m_edge2);
        Float d = Vector4::Dot3(edge3, v);
        m_position.W = d;
    }

    Bool OrientedBox::IntersectSegment(const Segment& segment, Vector4& enterPoint) const
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

    Bool OrientedBox::IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const
    {
        Vector3 forward = m_edge1;
        Vector3 right = m_edge2;

        Vector3 up = GetEdge3();
        const Vector3 localSpaceExtends(m_edge2.W, m_edge1.W, up.Mag());
        up.Normalize();

        const Vector3 p = m_position - origin;
        Vector3 f(right.Dot(direction), forward.Dot(direction), up.Dot(direction));
        const Vector3 e(right.Dot(p), forward.Dot(p), up.Dot(p));

        Float t[6] = {0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 3; ++i)
        {
            if (vanguard::math::AlmostEquals(f[i], 0.0f))
            {
                if (((-e[i] - localSpaceExtends[i]) > 0) || ((-e[i] + localSpaceExtends[i]) < 0))
                {
                    return false;
                }
                f[i] = 0.00001f; // Avoid div by 0!
            }

            t[i * 2 + 0] = (e[i] + localSpaceExtends[i]) / f[i]; // tmin[x, y, z]
            t[i * 2 + 1] = (e[i] - localSpaceExtends[i]) / f[i]; // tmax[x, y, z]
        }

        const Float tmin =
            vanguard::math::Max(vanguard::math::Max(vanguard::math::Min(t[0], t[1]), vanguard::math::Min(t[2], t[3])), vanguard::math::Min(t[4], t[5]));
        const Float tmax =
            vanguard::math::Min(vanguard::math::Min(vanguard::math::Max(t[0], t[1]), vanguard::math::Max(t[2], t[3])), vanguard::math::Max(t[4], t[5]));

        // if tmax < 0, ray is intersecting AABB
        // but entire AABB is behind it's origin
        if (tmax < 0)
        {
            return false;
        }

        // if tmin > tmax, ray doesn't intersect AABB
        if (tmin > tmax)
        {
            return false;
        }

        // If tmin is < 0, tmax is closer
        enterDistFromOrigin = tmin;
        if (tmin < 0.0f)
        {
            enterDistFromOrigin = tmax;
        }
        return true;
    }

    Bool OrientedBox::IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const
    {
        Float t;
        Bool ret = IntersectRay(origin, direction, t);
        enterPoint = origin + direction * t;
        return ret;
    }

    EulerAngles OrientedBox::GetOrientation() const
    {
        EulerAngles result;
        Vector3 edge3(Vector4::Cross(m_edge1, m_edge2));

        Vector4 alignedLeft(Vector4::Cross(m_edge1, Vector4(0, 0, 1.0f)).Normalized3());
        Float rollCos = Vector4::Dot3(alignedLeft, edge3);
        result.Roll = -RAD2DEG(::asinf(rollCos));

        Vector4 alignedUp(Vector4::Cross(m_edge2, Vector4(0, 1.0f, 0)).Normalized3());
        Float pitchCos = Vector4::Dot3(alignedUp, m_edge1);
        result.Pitch = RAD2DEG(::asinf(pitchCos));

        Vector4 alignedForward(Vector4::Cross(edge3, Vector4(1.0f, 0, 0)).Normalized3());
        Float yawCos = Vector4::Dot3(alignedForward, m_edge2);
        result.Yaw = -RAD2DEG(::asinf(yawCos));
        return result;
    }

    Bool OrientedBox::IsEmpty() const
    {
        return m_position.W == 0.f || m_edge1.W == 0.f || m_edge2.W == 0.f;
    }

    Bool OrientedBox::Contains(const Vector4& point) const
    {
        Float t;
        Float d;

        // using SAT to check if point is inside
        t = Vector4::Dot3(point, m_edge1);
        d = Vector4::Dot3(m_position, m_edge1);
        if (t <= d || t >= d + m_edge1.W)
            return false;

        t = Vector4::Dot3(point, m_edge2);
        d = Vector4::Dot3(m_position, m_edge2);
        if (t <= d || t >= d + m_edge2.W)
            return false;

        Vector4 edge3 = GetEdge3().Normalized3();
        t = Vector4::Dot3(point, edge3);
        d = Vector4::Dot3(m_position, edge3);
        if (t <= d || t >= d + m_position.W)
            return false;

        return true;
    }

} // namespace vanguard::math