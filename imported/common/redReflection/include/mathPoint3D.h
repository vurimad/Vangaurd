/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

#include "../../redMath/include/point3D.h"

// 3D Point
struct RED_REFLECTION_API Point3D : public math::Point3D
{
	RTTI_DECLARE_TYPE( Point3D );

	RED_FORCE_INLINE Point3D()
		: math::Point3D()
	{}

	RED_FORCE_INLINE Point3D( const math::Point3D& point )
		: math::Point3D( point )
	{}

	RED_FORCE_INLINE Point3D( Int32 _x, Int32 _y, Int32 _z )
		: math::Point3D( _x, _y, _z )
	{}

	RED_FORCE_INLINE static Point3D Make( Int32 _x, Int32 _y, Int32 _z )
	{
		return { _x, _y, _z };
	}

	// predefined empty Rect
	RED_FORCE_INLINE static Point3D ZERO()
	{
		return math::Point3D::ZERO();
	}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Point3D> { enum { Value = true }; };
template <> struct TCopyableType<Point3D> { enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Point3D>( const math::Point3D& )
{
	return TTypeName<Point3D>::GetTypeName();
}

// Ordering operator
RED_FORCE_INLINE static const Bool operator<( const Point3D& a, const Point3D& b )
{
	if ( a.x != b.x )
		return a.x < b.x;
	if ( a.y != b.y )
		return a.y < b.y;

	return a.z < b.z;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Point3D& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Point3D& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}
