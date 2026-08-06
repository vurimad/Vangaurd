/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    RED_INLINE Tetrahedron::Tetrahedron(const Tetrahedron& tetra)
        : Tetrahedron{tetra.m_points[0], tetra.m_points[1], tetra.m_points[2], tetra.m_points[3]}
    {
    }

    RED_INLINE Tetrahedron::Tetrahedron(const Vector4& pos1, const Vector4& pos2, const Vector4& pos3, const Vector4& pos4)
        : m_points{pos1, pos2, pos3, pos4}
    {
    }

    RED_INLINE Vector4 Tetrahedron::GetPosition() const
    {
        return m_points[0];
    }

    RED_INLINE Tetrahedron Tetrahedron::operator+(const Vector4& dir) const
    {
        return {m_points[0] + dir, m_points[1] + dir, m_points[2] + dir, m_points[3] + dir};
    }

    RED_INLINE Tetrahedron Tetrahedron::operator-(const Vector4& dir) const
    {
        return {m_points[0] - dir, m_points[1] - dir, m_points[2] - dir, m_points[3] - dir};
    }

    RED_INLINE Tetrahedron& Tetrahedron::operator+=(const Vector4& dir)
    {
        m_points[0] += dir;
        m_points[1] += dir;
        m_points[2] += dir;
        m_points[3] += dir;
        return *this;
    }

    RED_INLINE Tetrahedron& Tetrahedron::operator-=(const Vector4& dir)
    {
        m_points[0] -= dir;
        m_points[1] -= dir;
        m_points[2] -= dir;
        m_points[3] -= dir;
        return *this;
    }

} // namespace vanguard::math