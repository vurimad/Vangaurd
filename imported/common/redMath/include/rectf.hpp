/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	constexpr RectF::RectF( const Box& box )
		: RectF{ box.Min.X, box.Max.X, box.Max.Y, box.Min.Y }
	{}

	constexpr RectF::RectF( Float left, Float right, Float top, Float bottom)
		: m_left( left ), m_top( top ), m_right( right ), m_bottom( bottom )
	{}

	constexpr Float RectF::Width() const
	{
		return m_right - m_left;
	}

	// Get height
	constexpr Float RectF::Height() const
	{
		return m_bottom - m_top;
	}

	RED_INLINE Vector2 RectF::GetCenter() const
	{
		return { ( m_right + m_left ) * 0.5f, ( m_bottom + m_top ) * 0.5f };
	}

	RED_INLINE Vector2 RectF::GetTopLeft() const
	{
		return { m_left, m_top };
	}

	RED_INLINE Vector2 RectF::GetTopRight() const
	{
		return { m_right, m_top };
	}

	RED_INLINE Vector2 RectF::GetBottomRight() const
	{
		return { m_right, m_bottom };
	}

	RED_INLINE Vector2 RectF::GetBottomLeft() const
	{
		return { m_left, m_bottom };
	}

	RED_INLINE void RectF::Clear()
	{
		m_left = std::numeric_limits<Float>::max();
		m_right = std::numeric_limits<Float>::min();
		m_top = std::numeric_limits<Float>::max();
		m_bottom = std::numeric_limits<Float>::min();
	}

	constexpr Bool RectF::IsEmpty() const
	{
		return m_left >= m_right || m_top >= m_bottom;
		//return Width() == 0 || Height() == 0;
	}

	RED_INLINE Bool RectF::Intersects( const RectF& other ) const
	{
		SanityCheck();
		return !( m_left >= other.m_right || m_right <= other.m_left || m_bottom <= other.m_top || m_top >= other.m_bottom );
	}

	RED_INLINE Bool RectF::Intersects( Vector2 pos ) const
	{
		SanityCheck();
		return !( pos.X >= m_right || pos.X < m_left || pos.Y < m_top || pos.Y >= m_bottom );
	}

	RED_INLINE Bool RectF::Intersection( const RectF& r1, const RectF& r2, RectF& out )
	{
		if ( !r1.Intersects( r2 ) )
			return false;

		out.m_left		= Max<Float>( r1.m_left,	r2.m_left );
		out.m_right		= Min<Float>( r1.m_right,	r2.m_right );
		out.m_top		= Max<Float>( r1.m_top,		r2.m_top );
		out.m_bottom	= Min<Float>( r1.m_bottom,	r2.m_bottom );
		return true;
	}

	RED_INLINE RectF RectF::Union( const RectF& r1, const RectF& r2 )
	{
		RectF result;
		result.m_left = Min( r1.m_left, r2.m_left );
		result.m_right = Max( r1.m_right, r2.m_right );
		result.m_top = Min( r1.m_top, r2.m_top );
		result.m_bottom = Max( r1.m_bottom, r2.m_bottom );
		return result;
	}

	constexpr Bool RectF::Contains( const RectF& other ) const
	{
		return (m_left <= other.m_left) && (m_right >= other.m_right) && (m_top <= other.m_top) && (m_bottom >= other.m_bottom);
	}

	constexpr Bool RectF::Contains( const Vector2& p ) const
	{
		return (p.X >= m_left) && (p.Y >= m_top) && (p.X < m_right) && (p.Y < m_bottom);
	}

	RectF& RectF::AddRect( const RectF& other )
	{
		m_left = Min<Float>( m_left, other.m_left );
		m_right = Max<Float>( m_right, other.m_right );
		m_top = Min<Float>( m_top, other.m_top );
		m_bottom = Max<Float>( m_bottom, other.m_bottom );
		return *this;
	}

	RED_INLINE void RectF::Add( const Vector2& point )
	{
		m_left = Min<Float>( m_left, point.X );
		m_right = Max<Float>( m_right, point.X );
		m_top = Min<Float>( m_top, point.Y );
		m_bottom = Max<Float>( m_bottom, point.Y );
	}

	RED_INLINE void RectF::SanityCheck() const
	{
		RED_FATAL_ASSERT( m_left <= m_right, "" );
		RED_FATAL_ASSERT( m_top <= m_bottom, "" );
	}

	constexpr RectF RectF::EMPTY()
	{
		//return { 0.f, 0.f, 0.f, 0.f };
		return {std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max(), std::numeric_limits<Float>::max(), -std::numeric_limits<Float>::max()};
	}

} // math