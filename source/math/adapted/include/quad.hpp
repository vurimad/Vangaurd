/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    RED_INLINE Quad::Quad(const Quad& q) : Quad{q.m_points[0], q.m_points[1], q.m_points[2], q.m_points[3]} {}

    RED_INLINE Quad::Quad(const Vector4& p1, const Vector4& p2, const Vector4& p3, const Vector4& p4) : m_points{p1, p2, p3, p4} {}

    RED_INLINE Quad Quad::operator+(const Vector4& dir) const
    {
        return {m_points[0] + dir, m_points[1] + dir, m_points[2] + dir, m_points[3] + dir};
    }

    RED_INLINE Quad Quad::operator-(const Vector4& dir) const
    {
        return {m_points[0] - dir, m_points[1] - dir, m_points[2] - dir, m_points[3] - dir};
    }

    RED_INLINE Quad& Quad::operator+=(const Vector4& dir)
    {
        SanityCheck();
        m_points[0] += dir;
        m_points[1] += dir;
        m_points[2] += dir;
        m_points[3] += dir;
        return *this;
    }

    RED_INLINE Quad& Quad::operator-=(const Vector4& dir)
    {
        SanityCheck();
        m_points[0] -= dir;
        m_points[1] -= dir;
        m_points[2] -= dir;
        m_points[3] -= dir;
        return *this;
    }

    RED_INLINE Vector4 Quad::GetPosition() const
    {
        return m_points[0];
    }

    RED_INLINE void Quad::SanityCheck() const
    {
        RED_MATH_ASSERT((Vector4::Cross(m_points[2] - m_points[1], m_points[0] - m_points[1]) -
                         Vector4::Cross(m_points[3] - m_points[2], m_points[1] - m_points[2]))
                            .SquareMag3() < 1e-06);
    }
} // namespace vanguard::math