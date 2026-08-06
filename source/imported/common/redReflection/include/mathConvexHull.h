/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

/// 3D Convex Hull
struct RED_REFLECTION_API ConvexHull
{
	RTTI_DECLARE_TYPE( ConvexHull );

	red::DynArray< Vector4 > m_planes{ red::PoolEngine() };

	ConvexHull() = default;

	RED_FORCE_INLINE ConvexHull( const ConvexHull& hull )
		: m_planes( hull.m_planes )
	{}

	RED_FORCE_INLINE ConvexHull( const red::DynArray< Vector4 >& planes )
		: m_planes( planes )
	{}

	// Return translated by a vector
	ConvexHull operator+( const Vector4& dir ) const;

	// Return translated by a -vector
	ConvexHull operator-( const Vector4& dir ) const;

	// Translate by a vector
	ConvexHull& operator+=( const Vector4& dir );

	// Translate by a -vector
	ConvexHull& operator-=( const Vector4& dir );

public:
	// Check if this convex shape contains point
	Bool Contains( const Vector4& point ) const;

	// Intersect this convex shape with segment, returns point of entry
	Bool IntersectSegment( const Segment& segment, Vector4& enterPoint ) const;

	// Intersect this convex shape with ray, returns distance to point of entry
	Bool IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const;

	// Intersect this convex shape with ray, returns point of entry
	Bool IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const;

private:
	// Apply translation offset to given plane equation
	static void MovePlane( Vector4& plane, const Vector4& translation );
};

// Manual serialization
void operator<<( IFile& file, ConvexHull& val );
