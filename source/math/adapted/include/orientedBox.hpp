/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    RED_INLINE OrientedBox::OrientedBox(const OrientedBox& box) : m_position(box.m_position), m_edge1(box.m_edge1), m_edge2(box.m_edge2) {}

    RED_INLINE Vector4 OrientedBox::GetEdge1() const
    {
        return m_edge1 * m_edge1.W;
    }

    RED_INLINE Vector4 OrientedBox::GetEdge2() const
    {
        return m_edge2 * m_edge2.W;
    }

    RED_INLINE Vector4 OrientedBox::GetEdge3() const
    {
        return Vector4::Cross(m_edge1, m_edge2) * m_position.W;
    }

    RED_INLINE Vector4 OrientedBox::GetMassCenter() const
    {
        return m_position + GetEdge1() * 0.5f + GetEdge2() * 0.5f + GetEdge3() * 0.5f;
    }

    RED_INLINE Float OrientedBox::GetMass() const
    {
        return -m_position.W * m_edge1.W * m_edge2.W;
    }

    RED_INLINE OrientedBox OrientedBox::operator+(const Vector4& dir) const
    {
        OrientedBox box(*this);
        box.m_position += dir;
        box.m_position.W -= dir.W;
        return box;
    }

    RED_INLINE OrientedBox OrientedBox::operator-(const Vector4& dir) const
    {
        OrientedBox box(*this);
        box.m_position -= dir;
        box.m_position.W += dir.W;
        return box;
    }

    RED_INLINE OrientedBox& OrientedBox::operator+=(const Vector4& dir)
    {
        m_position += dir;
        m_position.W -= dir.W;
        return *this;
    }

    RED_INLINE OrientedBox& OrientedBox::operator-=(const Vector4& dir)
    {
        m_position -= dir;
        m_position.W += dir.W;
        return *this;
    }

} // namespace vanguard::math