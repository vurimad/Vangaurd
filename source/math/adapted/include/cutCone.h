/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    // 3D cut cone
    struct CutCone
    {
        Vector4 m_positionAndRadius1; //<! position = (x,y,z), radius1 = (w)
        Vector4 m_normalAndRadius2;   //<! orientation = (x,y,z), radius2 = (w)
        Float m_height;

        // Undefined constructor
        CutCone() = default;

        // Copy
        CutCone(const CutCone& cone);

        // Create from two points and two radiuses
        CutCone(const Vector4& pos1, const Vector4& pos2, Float radius1, Float radius2);

        // Create a cone rooted at position and oriented along specified normal
        CutCone(const Vector4& pos, const Vector4& normal, Float radius1, Float radius2, Float height); //<! normal length has to be 1

        // Return translated by a vector
        CutCone operator+(const Vector4& dir) const;

        // Return translated by a -vector
        CutCone operator-(const Vector4& dir) const;

        // Translate by a vector
        CutCone& operator+=(const Vector4& dir);

        // Translate by a -vector
        CutCone& operator-=(const Vector4& dir);

        // Get center of mass for the cone
        Vector4 GetMassCenter() const;

        // Compute mass (assumes density of 1)
        Float GetMass() const;

        // Get cone height
        Float GetHeight() const;

        // Get radius at first point
        Float GetRadius1() const;

        // Get radius at second point
        Float GetRadius2() const;

        // Get root position (base)
        Vector4 GetPosition() const;

        // Get second point (tip)
        Vector4 GetPosition2() const;

        // Get cone axis (normal)
        Vector4 GetNormal() const;

        // Get orientation in space
        REDMATH_API EulerAngles GetOrientation() const;

        // Check if shape contains given point
        REDMATH_API Bool Contains(const Vector4& point) const;

        // Intersect segment with this shape, returns point of entry
        REDMATH_API Bool IntersectSegment(const Segment& segment, Vector4& enterPoint) const;

        // Intersect ray with this shape, returns distance to entry
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin) const;

        // Intersect ray with this shape, returns point of entry
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const;
    };

} // namespace vanguard::math

#include "cutCone.hpp"