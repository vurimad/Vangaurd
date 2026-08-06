/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    // 3D tetrahedron (represented as 4 planes)
    struct Tetrahedron
    {
    public:
        Vector4 m_points[4];

        // Undefined constructor
        Tetrahedron() = default;

        // Copy
        Tetrahedron(const Tetrahedron& tetra);

        // Initialize directly from points
        Tetrahedron(const Vector4& pos1, const Vector4& pos2, const Vector4& pos3, const Vector4& pos4);

        // Return translated by a vector
        Tetrahedron operator+(const Vector4& dir) const;

        // Return translated by a -vector
        Tetrahedron operator-(const Vector4& dir) const;

        // Translate by a vector
        Tetrahedron& operator+=(const Vector4& dir);

        // Translate by a -vector
        Tetrahedron& operator-=(const Vector4& dir);

        // Get root position
        Vector4 GetPosition() const;

        // Check if point is inside this shape
        REDMATH_API Bool Contains(const Vector4& point) const;

        // Intersect segment with this shape, returns point of entry
        REDMATH_API Bool IntersectSegment(const Segment& segment, Vector4& enterPoint) const;

        // Intersect ray with this shape, returns distance to point of entry
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const;

        // Intersect ray with this shape, returns point of entry
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const;

        REDMATH_API Float CalcVolume() const;

    private:
        void CalculatePlanes(Vector4* planes) const;
    };

} // namespace vanguard::math

#include "tetrahedron.hpp"