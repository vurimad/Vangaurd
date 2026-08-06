/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    // 3D OBB
    struct OrientedBox
    {
        Vector4 m_position; //<! position, m_position.W is length of third edge
        Vector4 m_edge1;    //<! forward, edge1 - normalized, length in w component
        Vector4 m_edge2;    //<! right, edge2 - normalized, length in w component
                            //<! up, edge3 is m_edge1 x m_edge2 * m_position.W
        // Undefined constructor
        OrientedBox() = default;

        // Copy
        OrientedBox(const OrientedBox& box);

        // Create from root position and set of direction
        REDMATH_API OrientedBox(const Vector4& pos, const Vector4& forward, const Vector4& right, const Vector4& up);

        // Get base edge 1
        Vector4 GetEdge1() const;

        // Get base edge 2
        Vector4 GetEdge2() const;

        // Get height edge
        Vector4 GetEdge3() const;

        // Get (compute) orientation
        REDMATH_API EulerAngles GetOrientation() const;

        // Get center of mass
        Vector4 GetMassCenter() const;

        // Get mass (assumes density of 1)
        Float GetMass() const;

        // Return translated by a vector
        OrientedBox operator+(const Vector4& dir) const;

        // Return translated by a -vector
        OrientedBox operator-(const Vector4& dir) const;

        // Translate by a vector
        OrientedBox& operator+=(const Vector4& dir);

        // Translate by a -vector
        OrientedBox& operator-=(const Vector4& dir);

        // Check if the box is empty
        REDMATH_API Bool IsEmpty() const;

        // Check if bounding box contains point
        REDMATH_API Bool Contains(const Vector4& point) const;

        // Intersect segment with this oriented box, returns point of entry
        REDMATH_API Bool IntersectSegment(const Segment& segment, Vector4& enterPoint) const;

        // Intersect ray with this oriented box, returns distance to entry point
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const;

        // Intersect ray with this oriented box, returns point of entry
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const;
    };

} // namespace vanguard::math

#include "orientedBox.hpp"