/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	constexpr Rect::Rect( const Box& box )
		: Rect{ Int32( box.Min.X ), Int32( box.Max.X ), Int32( box.Min.Y ), Int32( box.Max.Y ) }
	{}

	RED_FORCE_INLINE Rect::Rect( EResetState )
	{
		Clear();
	}

	constexpr Rect::Rect( Int32 left, Int32 right, Int32 top, Int32 bottom)
		: m_left( left )
		, m_top( top )
		, m_right( right )
		, m_bottom( bottom )
	{}

	constexpr Rect::Rect( const Point& tl, const Point& br )
		: Rect{ tl.x, br.x, tl.y, br.y }
	{}


	RED_FORCE_INLINE void Rect::Clear()
	{
		m_left = std::numeric_limits<Int32>::max();
		m_right = std::numeric_limits<Int32>::min();
		m_top = std::numeric_limits<Int32>::max();
		m_bottom = std::numeric_limits<Int32>::min();
	}

	constexpr Bool Rect::IsEmpty() const
	{
		return m_left >= m_right || m_top >= m_bottom;
	}

	RED_FORCE_INLINE void Rect::Translate( Int32 x, Int32 y )
	{
		m_left		= x + m_left;
		m_right		= x + m_right;
		m_top		= y + m_top;
		m_bottom	= y + m_bottom;
	}

	RED_INLINE void Rect::Translate( const Point& translation )
	{
		m_left   += translation.x;
		m_right  += translation.x;
		m_top    += translation.y;
		m_bottom += translation.y;
	}

	constexpr Rect Rect::GetTranslated( Int32 x, Int32 y ) const
	{
		return { x + m_left, x + m_right, y + m_top, y + m_bottom };
	}

	constexpr Rect Rect::GetTranslated( const Point& translation ) const
	{
		return{ translation.x + m_left, translation.x + m_right, translation.y + m_top, translation.y + m_bottom };
	}

	RED_FORCE_INLINE void Rect::Trim( const Rect& trimmerRect )
	{
		m_left		= Max( m_left, trimmerRect.m_left );
		m_right		= Min( m_right, trimmerRect.m_right );
		m_top		= Max( m_top, trimmerRect.m_top );
		m_bottom	= Min( m_bottom, trimmerRect.m_bottom );
	}

	constexpr Rect Rect::GetTrimmed( const Rect& trimmerRect ) const
	{
		return 
		{
			Max( m_left, trimmerRect.m_left ),
			Min( m_right, trimmerRect.m_right ),
			Max( m_top, trimmerRect.m_top ),
			Min( m_bottom, trimmerRect.m_bottom )
		};
	}

	RED_FORCE_INLINE void Rect::Grow( Int32 sx, Int32 sy )
	{
		m_left -= sx;
		m_right += sx;
		m_top -= sy;
		m_bottom += sy;
	}

	constexpr Rect Rect::GetGrown( Int32 sx, Int32 sy ) const
	{
		return { m_left - sx, m_right + sx, m_top - sy, m_bottom + sy };
	}

	RED_FORCE_INLINE void Rect::Add( const Rect& addRect )
	{
		m_left		= Min( m_left,		addRect.m_left );
		m_right		= Max( m_right,		addRect.m_right );
		m_top		= Min( m_top,		addRect.m_top );
		m_bottom	= Max( m_bottom,	addRect.m_bottom );
	}

	RED_FORCE_INLINE void Rect::Add( const Point& point )
	{
		m_left = Min( m_left, point.x );
		m_right = Max( m_right, point.x );
		m_top = Min( m_top, point.y );
		m_bottom = Max( m_bottom, point.y );
	}

	RED_FORCE_INLINE void Rect::Add( const Vector2& point )
	{
		Add( Point( (Int32)point.X, (Int32)point.Y ) );
	}

	RED_FORCE_INLINE Bool Rect::Intersects( const Rect& other ) const
	{
		return !( m_left >= other.m_right || m_right <= other.m_left || m_bottom <= other.m_top || m_top >= other.m_bottom );
	}

	constexpr Bool Rect::Contains( const Rect& other ) const
	{
		return m_left <= other.m_left && m_right >= other.m_right && m_top <= other.m_top && m_bottom >= other.m_bottom;
	}

	constexpr Bool Rect::Contains( const Point& p ) const
	{
		return (p.x >= m_left) && (p.y >= m_top) && (p.x < m_right) && (p.y < m_bottom);
	}

	constexpr Bool Rect::Contains( const Vector2& p ) const
	{
		return ( p.X >= m_left ) && ( p.Y >= m_top ) && ( p.X < m_right ) && ( p.Y < m_bottom );
	}

	constexpr Int32 Rect::Width() const
	{
		return m_right - m_left;
	}

	constexpr Int32 Rect::Height() const
	{
		return m_bottom - m_top;
	}

	constexpr Point Rect::GetTopLeft() const
	{
		return { m_left, m_top };
	}

	constexpr Point Rect::GetTopRight() const
	{
		return { m_right, m_top };
	}

	constexpr Point Rect::GetBottomRight() const
	{
		return { m_right, m_bottom };
	}

	constexpr Point Rect::GetBottomLeft() const
	{
		return { m_left, m_bottom };
	}

	constexpr Point Rect::GetCenter() const
	{
		return { (m_right + m_left) / 2, (m_top + m_bottom) / 2 };
	}

	RED_FORCE_INLINE Bool Rect::Intersection( const Rect& r1, const Rect& r2, Rect& out )
	{
		if ( !r1.Intersects( r2 ) )
			return false;

		out.m_left		= Max( r1.m_left,	r2.m_left );
		out.m_right		= Min( r1.m_right,	r2.m_right );
		out.m_top		= Max( r1.m_top,	r2.m_top );
		out.m_bottom	= Min( r1.m_bottom, r2.m_bottom );
		return true;
	}

	RED_INLINE Rect Rect::Union( const Rect& r1, const Rect& r2 )
	{
		Rect result;
		result.m_left = Min( r1.m_left, r2.m_left );
		result.m_right = Max( r1.m_right, r2.m_right );
		result.m_top = Min( r1.m_top, r2.m_top );
		result.m_bottom = Max( r1.m_bottom, r2.m_bottom );
		return result;
	}

	constexpr Rect Rect::EMPTY()
	{
		return { std::numeric_limits<Int32>::max(), std::numeric_limits<Int32>::min(), std::numeric_limits<Int32>::max(), std::numeric_limits<Int32>::min() };
	}

	RED_INLINE Bool Rect::operator==( const Rect& other ) const
	{
		return ((m_left ^ other.m_left) | (m_right ^ other.m_right) | (m_top ^ other.m_top) | (m_bottom ^ other.m_bottom)) == 0;
	}

	RED_INLINE Bool Rect::operator!=( const Rect& other ) const
	{
		return ((m_left ^ other.m_left) | (m_right ^ other.m_right) | (m_top ^ other.m_top) | (m_bottom ^ other.m_bottom)) != 0;
	}

} // math