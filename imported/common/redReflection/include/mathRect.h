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

#include "../../redMath/include/rect.h"

// 2D Rectangle (with left < right and top < bottom)
struct RED_REFLECTION_API Rect : public math::Rect
{
	RTTI_DECLARE_TYPE( Rect );

	RED_FORCE_INLINE Rect() = default;

	RED_FORCE_INLINE Rect( const math::Box& box )
		: math::Rect( box )
	{}

	RED_FORCE_INLINE Rect( const math::Rect& rect )
		: math::Rect( rect )
	{}

	RED_FORCE_INLINE Rect( EResetState )
		: math::Rect( RESET_STATE )
	{}

	RED_FORCE_INLINE Rect( Int32 left, Int32 right, Int32 top, Int32 bottom )
		: math::Rect( left, right, top, bottom )
	{}

	static Rect Make( Int32 x, Int32 y, Int32 width, Int32 height )
	{
		return { x, x + width, y, y + height };
	}

	static Rect Make( const Point& topLeft, const Point& bottomRight )
	{
		return 
		{ 
			math::Min( topLeft.x, bottomRight.x ),
			math::Max( topLeft.x, bottomRight.x ),
			math::Min( topLeft.y, bottomRight.y ),
			math::Max( topLeft.y, bottomRight.y ) 
		};
	}

	static Rect Make( const Vector2& topLeft, const Vector2& bottomRight )
	{
		const Point pTopLeft( (Int32)topLeft.X, (Int32)topLeft.Y );
		const Point pBottomRight( (Int32)bottomRight.X, (Int32)bottomRight.Y );

		return Make( pTopLeft, pBottomRight );
	}

	Rect Inflated( const Int32 x, const Int32 y ) const
	{
		return { m_left-x, m_right+x, m_top-y, m_bottom+y };
	}

	Rect CenteredIn( const Rect& r, int dir = 3) const
	{
		const auto left = m_left + ((dir & 1) ? (r.m_left + (r.Width() - Width()) / 2) : 0);
		const auto top = m_top + ((dir & 2) ? (r.m_top + (r.Height() - Height()) / 2) : 0);
		return { left, left + Width(), top, top + Height() };
	}

	// predefined empty Rect
	RED_FORCE_INLINE static Rect EMPTY()
	{
		return math::Rect::EMPTY();
	}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Rect>	{ enum { Value = true }; };
template <> struct TCopyableType<Rect>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Rect>( const math::Rect& )
{
	return TTypeName<Rect>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Rect& val )
{
	static_assert( sizeof( val ) == 16, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Rect& val )
{
	static_assert( sizeof( val ) == 16, "" );
	file.Serialize( &val, sizeof( val ) );
}
