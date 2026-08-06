/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    // 3D sphere
    RED_ALIGNED_STRUCT(Sphere, 16)
    {
        // Holds (Px, Py, Pz, Radius)
        Vector4 CenterRadius;

        Sphere() = default;
        Sphere(const Vector4& geometry);
        Sphere(const Vector4& center, Float radius);
        Sphere(Float c0, Float c1, Float c2, Float radius);

        // Get sphere's ceneter
        Vector4 GetCenterOfVolume() const
        {
            return CenterRadius;
        }

        // Get sphere's volume
        Float GetVolume() const
        {
            return 4 * RED_PI * CenterRadius.W * CenterRadius.W * CenterRadius.W / 3;
        }

        // Return translated by a vector
        Sphere operator+(const Vector4& dir) const;

        // Return translated by a -vector
        Sphere operator-(const Vector4& dir) const;

        // Translate by a vector
        void operator+=(const Vector4& dir);

        // Translate by a -vector
        void operator-=(const Vector4& dir);

        // Get generalized orientation (added to all shapes)
        EulerAngles GetOrientation() const
        {
            return EulerAngles::ZEROS();
        }

        // Gets sphere's center position
        Vector4 GetCenter() const;

        // Gets square of sphere's radius
        Float GetSquareRadius() const;

        // Gets sphere's radius
        Float GetRadius() const;

        // Gets shortest distance to point (==0 if point lies on sphere's boundary, <0 if point is inside sphere)
        Float GetDistance(const Vector4& point) const;

        // Gets shortest distance from this sphere's boundary to another sphere's boundary (<0 if spheres intersect)
        Float GetDistance(const Sphere& sphere) const;

        // Check if sphere contains point
        Bool Contains(const Vector4& point) const;

        // Check if sphere wholly contains other sphere
        Bool Contains(const Sphere& sphere) const;

        // Check if sphere touches other sphere
        Bool Touches(const Sphere& sphere) const;

        // Check ray-sphere intersection; returns the number of intersection points (0, 1 or 2); calculated enterPoint is clamped to origin
        REDMATH_API Uint32 IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint, Vector4& exitPoint) const;

        // Check edge-sphere intersection; returns number of intersection points (0, 1 or 2); calculated intersection points are clamped
        // within a and b
        REDMATH_API Uint32 IntersectEdge(const Vector4& a, const Vector4& b, Vector4& intersectionPoint0, Vector4& intersectionPoint1)
            const;

        // Check line-sphere intersection; returns the number of intersection points (0, 1 or 2);
        // t1 and t2 are parameters of intersection points for line equation 'origin + t * direction' (direction does not need to be
        // normalized!)
        REDMATH_API Uint32 IntersectLineParametric(const Vector4& origin, const Vector4& direction, Float& out_t1, Float& out_t2) const;

        // Inflates the sphere so it contains the given point and previous sphere. Results in the smallest possible;
        REDMATH_API void AddPoint(const Vector4& point);
    };

} // namespace vanguard::math

#include "sphere.hpp"