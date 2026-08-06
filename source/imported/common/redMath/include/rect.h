/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "point.h"

namespace math
{
	// 2D Rectangle (with left < right and top < bottom)
	struct Rect
	{
		enum EResetState { RESET_STATE };

		Int32	m_left;
		Int32	m_top;
		Int32	m_right;
		Int32	m_bottom;

		// Empty rectangle
		Rect() = default;

		// Construct from box XY plane, X values growing left to right and Y values growing up to down
		constexpr Rect( const Box& box );

		// Construct
		constexpr Rect( Int32 left, Int32 right, Int32 top, Int32 bottom);

		// Construct from two corners
		constexpr Rect( const Point& tl, const Point& br );

		// Construct an empty rectangle
		RED_FORCE_INLINE Rect( EResetState );

		// All zeroes
		RED_FORCE_INLINE void Clear();

		// Check if rectangle is empty
		constexpr Bool IsEmpty() const;

		// Translate all rectangle by x,y
		RED_FORCE_INLINE void Translate( Int32 x, Int32 y );
		RED_INLINE void Translate( const Point& translation );

		// Get translated rectangle
		constexpr Rect GetTranslated( Int32 x, Int32 y ) const;
		constexpr Rect GetTranslated( const Point& translation ) const;

		// Trim to another rectangle
		RED_FORCE_INLINE void Trim( const Rect& trimmerRect );

		// Get trimmed to another rectangle
		constexpr Rect GetTrimmed( const Rect& trimmerRect ) const;

		// Grow rect by sx, sy in every direction
		RED_FORCE_INLINE void Grow( Int32 sx, Int32 sy );

		// Get grown rect by sx, sy in every direction
		constexpr Rect GetGrown( Int32 sx, Int32 sy ) const;

		// Add rect to this rect, to get combined bounds
		RED_FORCE_INLINE void Add( const Rect& addRect );

		RED_FORCE_INLINE void Add( const Point& point );

		RED_FORCE_INLINE void Add( const Vector2& point );

		// Check if rectangle intersects with another
		RED_FORCE_INLINE Bool Intersects( const Rect& other ) const;

		// Check if rectangle completely contains another
		constexpr Bool Contains( const Rect& other ) const;

		// Check if rectangle completely contains a point
		constexpr Bool Contains( const Point& p ) const;

		// Check if rectangle completely contains a point
		constexpr Bool Contains( const Vector2& p ) const;

		// Get width
		constexpr Int32 Width() const;

		// Get height
		constexpr Int32 Height() const;

		// Get the top left corner
		constexpr Point GetTopLeft() const;

		// Get the top left corner
		constexpr Point GetTopRight() const;

		// Get the bottom right corner
		constexpr Point GetBottomRight() const;

		// Get the bottom right corner
		constexpr Point GetBottomLeft() const;

		// Get center of the rectangle
		constexpr Point GetCenter() const;

		// Get intersection of two rectangles
		RED_FORCE_INLINE static Bool Intersection( const Rect& r1, const Rect& r2, Rect& out );

		// Get bounds of the union of two rectangles.
		// In many code-bases (i.e MS C#) this is called just "union", not "unionBounds" etc, so we also use this naming simplification here.
		RED_INLINE static Rect Union( const Rect& r1, const Rect& r2 );

		// predefined empty Rect
		constexpr static Rect EMPTY();

		// Comparison operators.
		RED_INLINE Bool operator==( const Rect& other ) const;
		RED_INLINE Bool operator!=( const Rect& other ) const;
	};
} // math

#include "rect.hpp"
