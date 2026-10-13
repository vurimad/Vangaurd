/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{

	// 2D Rectangle, (with left <= right and top <= bottom)
	struct RectF
	{
		Float	m_left;
		Float	m_top;
		Float	m_right;
		Float	m_bottom;

		// Empty rectangle
		RectF() = default;

		// Construct from box XY plane, X values growing left to right and Y values growing up to down
		constexpr RectF( const Box& box );

		// Construct
		constexpr RectF( Float left, Float right, Float top, Float bottom);

		// Get width
		constexpr Float Width() const;

		// Get height
		constexpr Float Height() const;

		// Get center point
		Vector2 GetCenter() const;

		// Get the top left corner
		Vector2 GetTopLeft() const;

		// Get the top left corner
		Vector2 GetTopRight() const;

		// Get the bottom right corner
		Vector2 GetBottomRight() const;

		// Get the bottom right corner
		Vector2 GetBottomLeft() const;

		// Check if the rectangle has proper width and height
		constexpr Bool IsEmpty() const;

		// Set to invalid/empty state, so you can use Add, AddRect on it.
		RED_INLINE void Clear();

		// Check if rectangle intersects with another
		RED_INLINE Bool Intersects( const RectF& other ) const;

		// Check if point is in the rectangle
		RED_INLINE Bool Intersects( Vector2 pos ) const;

		// Get intersection of two rectangles
		RED_INLINE static Bool Intersection( const RectF& r1, const RectF& r2, RectF& out );

		// Check if rectangle completely contains another
		constexpr Bool Contains( const RectF& other ) const;

		// Check if rectangle completely contains a point
		constexpr Bool Contains( const Vector2& p ) const;

		// Expand so it contains other rectangle
		RED_INLINE RectF& AddRect( const RectF& other );

		RED_INLINE void Add( const Vector2& point );

		RED_INLINE void SanityCheck() const;
		
		// Get bounds of the union of two rectangles.
		// In many code-bases (i.e MS C#) this is called just "union", not "unionBounds" etc, so we also use this naming simplification here.
		RED_INLINE static RectF Union( const RectF& r1, const RectF& r2 );

		// predefined empty Rect
		constexpr static RectF EMPTY();
	};

} // math

#include "rectf.hpp"