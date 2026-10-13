/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

#include "../../redMath/include/rectf.h"

// Rectangle (float properties) ( with left <= right and top <= bottom )
struct RED_REFLECTION_API RectF : public math::RectF
{
	RTTI_DECLARE_TYPE( RectF );

	RED_FORCE_INLINE RectF() = default;

	RED_FORCE_INLINE RectF( const math::RectF& rect )
		: math::RectF( rect )
	{}

	RED_FORCE_INLINE RectF( const math::Box& box )
		: math::RectF( box )
	{}

	RED_FORCE_INLINE RectF( Float left, Float right, Float top, Float bottom) // fucking legacy format
		: math::RectF( left, right, top, bottom )
	{}

	static RectF Make( Float x, Float y, Float width, Float height )
	{
		return { x, x + width, y, y + height };
	}

	// predefined empty Rect
	RED_FORCE_INLINE static RectF EMPTY()
	{
		return math::RectF::EMPTY();
	}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::RectF>	{ enum { Value = true }; };
template <> struct TCopyableType<RectF>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::RectF>()
{
	return GetTypeName<RectF>();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Segment& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Segment& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}
