#include "build.h"
#include "segment.h"

namespace vanguard::math
{
    Float Segment::SegmentDistance(const Segment& s0, const Segment& s1, Float& t0, Float& t1)
    {
        // taken from http://geomalgorithms.com/a07-_distance.html

        const Vector4 w = s0.m_origin - s1.m_origin;
        Float a = Vector4::Dot3(s0.m_direction, s0.m_direction);
        Float b = Vector4::Dot3(s0.m_direction, s1.m_direction);
        Float c = Vector4::Dot3(s1.m_direction, s1.m_direction);
        Float d = Vector4::Dot3(s0.m_direction, w);
        Float e = Vector4::Dot3(s1.m_direction, w);
        Float D = a * c - b * b;
        Float n0, n1;
        Float d0 = D, d1 = D;

        if (D < 0.0001f) // segments overlapping
        {
            n0 = 0.0f;
            d0 = 1.0f;
            n1 = e;
            d1 = c;
        }
        else
        {
            // closest points on infinite lines
            n0 = b * e - c * d;
            n1 = a * e - b * d;

            // correct point for segment's "0" line
            if (n0 < 0.0f)
            {
                n0 = 0.0;
                n1 = e;
                d1 = c;
            }
            else if (n0 > d0)
            {
                n0 = d0;
                n1 = e + b;
                d1 = c;
            }
        }

        // correct points for segment's "1" line
        if (n1 < 0.0f)
        {
            n1 = 0.0f;
            if (d > 0.0f)
            {
                n0 = 0.0f;
            }
            else if (-d > a)
            {
                n0 = d0;
            }
            else
            {
                n0 = -d;
                d0 = a;
            }
        }
        else if (n1 > d1)
        {
            n1 = d1;
            if (b < d)
            {
                n0 = 0.0f;
            }
            else if (-d + b > a)
            {
                n0 = d0;
            }
            else
            {
                n0 = b - d;
                d0 = a;
            }
        }

        // calculate positions of points on the segments
        t0 = fabs(n0) < 0.0001f ? 0.0f : (n0 / d0);
        t1 = fabs(n1) < 0.0001f ? 0.0f : (n1 / d1);

        // calculate distance between points
        Vector4 diff = w + s0.m_direction * t0 - s1.m_direction * t1;
        return diff.Mag3();
    }

} // namespace vanguard::math