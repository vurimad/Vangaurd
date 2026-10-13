/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathConvexHull.h"
#include "containersSerialization.h"

ConvexHull ConvexHull::operator+( const Vector4& dir ) const
{
	ConvexHull convex( *this );
	for ( auto& plane : convex.m_planes )
		MovePlane( plane, dir );

	return convex;
}

ConvexHull ConvexHull::operator-( const Vector4& dir ) const
{
	ConvexHull convex( *this );
	for ( auto& plane : convex.m_planes )
		MovePlane( plane, -dir );

	return convex;
}

ConvexHull& ConvexHull::operator+=( const Vector4& dir )
{
	for ( auto& plane : m_planes )
		MovePlane( plane, dir );
	return *this;
}

ConvexHull& ConvexHull::operator-=( const Vector4& dir )
{
	for ( auto& plane : m_planes )
		MovePlane( plane, -dir );
	return *this;
}

Bool ConvexHull::Contains( const Vector4& point ) const
{
	for ( auto& plane : m_planes )
		if ( Vector4::Dot3( point, plane ) + plane.W >= 0 )
			return false;

	return true;
}

Bool ConvexHull::IntersectSegment( const Segment& segment, Vector4& enterPoint ) const
{
	RED_FATAL( "Not implemented" );
	return false;
}

Bool ConvexHull::IntersectRay( const Vector4& origin, const Vector4& direction, Float& enterDistFromOrigin ) const
{
	enterDistFromOrigin = -RED_FLT_MAX;

	for ( auto& plane : m_planes )
	{
		const Float proj = -Vector4::Dot3( plane, direction );

		if ( proj > 0.0f )
		{
			Float intersectionDistance = Vector4::Dot3( origin, plane );
			intersectionDistance += plane[3];
			intersectionDistance /= proj;

			if ( intersectionDistance > enterDistFromOrigin )
				enterDistFromOrigin = intersectionDistance;
		}
	}

	const Vector4 enterPoint = origin + ( direction * enterDistFromOrigin );
	for ( auto& plane : m_planes )
	{
		Float dist = Vector4::Dot3( enterPoint, plane );
		dist += plane.W;

		if ( dist > 1e-6f )
			return false;
	}

	return enterDistFromOrigin > 0 ;
}

Bool ConvexHull::IntersectRay( const Vector4& origin, const Vector4& direction, Vector4& enterPoint ) const
{
	Float t;
	Bool ret = IntersectRay( origin, direction, t);
	enterPoint = origin + direction * t;
	return ret;
}

void ConvexHull::MovePlane( Vector4& plane, const Vector4& translation )
{
	plane.W = - Vector4::Dot3( translation + ( plane * ( - plane.W ) ), plane );
}

void operator<<( IFile& file, ConvexHull& val )
{
	file << val.m_planes;
}

