/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    struct Sphere;
    struct Segment;

    // 3D bounding box (based on 4D vectors but math is for 3D stuff)
    struct Box
    {
        Vector4 Min;
        Vector4 Max;

        enum EResetState
        {
            RESET_STATE
        };
        enum EMaximize
        {
            MAXIMIZE
        };

        // Lower-case and upper-case stand for min and max. i.e. for a box with a center in (0,0,0) and halfExtents of (1,1,1):
        // xyz would be -1,-1,-1, xYz would be -1, 1,-1, xyZ would be -1,-1,1 and so on...
        enum ECorner
        {
            xyz = 0,
            xYz,
            XYz,
            Xyz,
            xyZ,
            xYZ,
            XYZ,
            XyZ
        };

        Box() = default;
        Box(const Box& rhs);
        Box(const Vector4& min, const Vector4& max);
        Box(const Vector4& center, float radius);
        Box(EResetState);
        Box(EMaximize);

        Bool operator==(const Box& box) const;
        Bool operator!=(const Box& box) const;
        Bool operator<(const Box& box) const;

        Box& operator=(const Box& rhs) = default;

        // Return box translated by a vector
        Box operator+(const Vector4& dir) const;

        // Return box translated by a -vector
        Box operator-(const Vector4& dir) const;

        // Return box scaled box by a vector
        Box operator*(const Vector4& scale) const;

        // Translate by a vector
        void operator+=(const Vector4& dir);

        // Translate by a -vector
        void operator-=(const Vector4& dir);

        // Scale box by a vector
        void operator*=(const Vector4& scale);

        // Clear to empty box
        Box& Clear();

        // Check if bounding box contains point
        Bool Contains(const Vector4& point) const;

        // Check if bounding box contains point
        Bool Contains(const Vector3& point, Float zExt) const;

        // Check if bounding box contains other box
        Bool Contains(const Box& box) const;

        // Check if bounding box contains other box, excluding edges
        Bool ContainsExcludeEdges(const Box& box) const;

        // Check if bounding box contains other box in 2D (checks only X and Y)
        Bool Contains2D(const Box& box) const;

        // Check if bounding box contains point
        Bool Contains2D(const Vector3& point) const;

        // Check if bounding box contains point, excluding edges
        Bool ContainsExcludeEdges(const Vector4& point) const;

        // Check if bounding box touches other box
        Bool Touches(const Box& box) const;

        // Check if bounding box touches other box defined with min and max points
        Bool Touches(const Vector3& bMin, const Vector3& bMax) const;

        // Check if bounding box touches other box; only takes X and Y in consideration
        Bool Touches2D(const Box& box) const;

        // Add point to bounding box
        Box& AddPoint(const Vector4& point);

        // Add point to bounding box
        Box& AddPoint(const Vector3& point);

        // Add other box to bounding box
        Box& AddBox(const Box& box);

        // Check if bounding box is empty
        // (note that if the bounding box is valid, but at least one dimension is equal to zero, it's not empty then)
        Bool IsEmpty() const;
        Bool IsOk() const;

        // Calculate box corners
        void CalcCorners(Vector4* corners) const;

        Vector4 CalcCorner(ECorner corner) const;

        // Calculate box center
        Vector4 CalcCenter() const;

        // Calculate box extents
        Vector4 CalcExtents() const;

        // Calculate box size
        Vector4 CalcSize() const;

        // Calculate box volume
        Float CalcVolume() const;

        // Extrude by a direction
        Box& Extrude(const Vector4& dir);

        // Extrude by a value
        Box& Extrude(const Float value);

        // Expand evenly by a direction
        Box& Expand(const Vector4& dir);

        // Expand evenly by a value
        Box& Expand(const Float value);

        // Crop to box
        void Crop(const Box& box);

        // Normalize to unit box space
        Box& Normalize(const Box& unitBox);

        // Distance from point
        Float Distance(const Vector4& pos) const;

        // Squared distance from point
        Float SquaredDistance(const Vector4& pos) const;

        // Squared distance from point; only takes X and Y in consideration
        Float SquaredDistance2D(const Vector4& pos) const;

        // Squared distance from other box
        Float SquaredDistance(const Box& box) const;

        // Distance between a point and the farthest point of the box
        Float FarthestPointDistance(const Vector4& pos) const;

        // Squared distance between a point and the farthest point of the box
        Float FarthestPointDistanceSquared(const Vector4& pos) const;

        // Unit Box expanding from 0,0,0 to 1,1,1
        static Box UNIT();

        // Empty Box
        static Box EMPTY();

        // Full Box (contains everything)
        static Box FULL();

    public:
        // Check if the bounding box intersects sphere (Arvo's algorithm)
        REDMATH_API Bool IntersectSphere(const Sphere& sphere) const;

        // Check if the segment intersects the bounding box
        REDMATH_API Bool IntersectSegment(const Segment& segment, Vector4& enterPoint) const;

        // Check if the segment intersects the bounding box (returns both entry and exit point)
        REDMATH_API Bool IntersectSegment(const Segment& segment, Vector4& enterPoint, Vector4& exitPoint) const;

        // Check if the infinite line intersects the bounding box, returns enter and exit distance on the ray (possibly negative)
        REDMATH_API Bool IntersectLine(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin, Float& exitDistFromOrigin) const;

        // Check if the ray intersects the bounding box, returns enter and exit distance on the ray
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin, Float* exitDistFromOrigin = nullptr) const;

        // Check if the ray intersects the bounding box
        REDMATH_API Bool IntersectRay(const Vector4& origin, const Vector4& direction, Vector4& enterPoint) const;
    };

} // namespace vanguard::math

#include "box.hpp"