#include "build.h"
#include "quad.h"
#include "segment.h"

namespace vanguard::math
{

    /** Implementation based on http://graphics.cs.kuleuven.be/publications/LD05ERQIT/LD05ERQIT.pdf */
    Bool Quad::IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const
    {
        Vector4 e1 = m_points[1] - m_points[0];
        Vector4 e3 = m_points[3] - m_points[0];

        Vector4 t = origin - m_points[0];

        Vector4 p = Vector4::Cross(direction, e3);
        Float det = Vector4::Dot3(e1, p);
        if (Abs(det) < 1e-6)
            return false;

        Float alpha = Vector4::Dot3(t, p) / det;
        if (alpha <= 0.0f)
            return false;
        if (alpha >= 1.0f)
            return false;

        Vector4 q = Vector4::Cross(t, e1);
        Float beta = Vector4::Dot3(direction, q) / det;
        if (beta <= 0.0f)
            return false;
        if (beta >= 1.0f)
            return false;

        if (alpha + beta > 1)
        {
            Vector4 er1 = m_points[1] - m_points[2];
            Vector4 er3 = m_points[3] - m_points[2];
            Vector4 pp = Vector4::Cross(direction, er1);
            Float detp = Vector4::Dot3(er3, pp);
            if (Abs(detp) < 1e-6)
                return false;

            Vector4 tp = origin - m_points[2];
            Float alphap = Vector4::Dot3(tp, pp) / detp;
            if (alphap < 0)
                return false;

            Vector4 qp = Vector4::Cross(tp, er3);
            Float betap = Vector4::Dot3(direction, qp) / detp;
            if (betap < 0)
                return false;
        }

        enterDistFromOrigin = Vector4::Dot3(e3, q) / det;
        if (enterDistFromOrigin < 0)
            return false;

        return true;
    }

    Bool Quad::IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const
    {
        Float t;
        Bool ret = IntersectRay(origin, direction, t);
        enterPoint = origin + direction * t;
        return ret;
    }

    Bool Quad::IntersectSegment(const Segment& segment, Vector4& enterPoint)
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

} // namespace vanguard::math
