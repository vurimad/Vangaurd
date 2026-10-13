/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "../../redFileSystem/include/file.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

#include "../../redMath/include/point.h"

// 2D Point
struct RED_REFLECTION_API Point : public math::Point
{
	RTTI_DECLARE_TYPE( Point );

	RED_FORCE_INLINE Point()
		: math::Point()
	{}

	RED_FORCE_INLINE Point( const math::Point& point )
		: math::Point( point )
	{}

	RED_FORCE_INLINE Point( Int32 _x, Int32 _y )
		: math::Point( _x, _y )
	{}

	RED_FORCE_INLINE static Point Make( Int32 _x, Int32 _y )
	{
		return { _x, _y };
	}

	// predefined empty Rect
	RED_FORCE_INLINE static Point ZERO()
	{
		return math::Point::ZERO();
	}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Point>	{ enum { Value = true }; };
template <> struct TCopyableType<Point>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Point>( const math::Point& )
{
	return TTypeName<Point>::GetTypeName();
}

// Ordering operator
RED_FORCE_INLINE static const Bool operator<(const Point& a, const Point& b)
{
	if (a.x != b.x)
		return a.x < b.x;

	return a.y < b.y;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Point& val )
{
	static_assert( sizeof( val ) == 8, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Point& val )
{
	static_assert( sizeof( val ) == 8, "" );
	file.Serialize( &val, sizeof( val ) );
}
