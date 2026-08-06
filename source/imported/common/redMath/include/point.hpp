/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	constexpr Point::Point()
		: x(0), y(0)
	{}

	constexpr Point::Point( Int32 _x, Int32 _y )
		: x( _x ), y( _y )
	{}

	constexpr Point Point::operator-() const
	{
		return { -x, -y };
	}

	constexpr Point Point::operator+( const Point p ) const
	{
		return { x + p.x, y + p.y };
	}

	constexpr Point Point::operator-( const Point p ) const
	{
		return { x - p.x, y - p.y };
	}

	constexpr Point Point::operator*( const Float f ) const
	{
		return { (Int32)( x * f ), (Int32)( y * f ) };
	}

	constexpr Point Point::operator/( const Float p ) const
	{
		return { (Int32)( x / p ), (Int32)( y / p ) };
	}

	constexpr Bool Point::operator==( const Point p ) const
	{
		return x == p.x && y == p.y;
	}

	constexpr Bool Point::operator!=( const Point p ) const
	{
		return !operator==(p);
	}

	RED_INLINE Point& Point::operator-=( const Point p )
	{
		x -= p.x;
		y -= p.y;
		return *this;
	}
	
	RED_INLINE Point& Point::operator+=( const Point p )
	{
		x += p.x;
		y += p.y;
		return *this;
	}

	RED_INLINE Point& Point::operator*=( const Float f )
	{
		x = (Int32)( x * f );
		y = (Int32)( y * f );
		return *this;
	}

	RED_INLINE Point& Point::operator/=( const Float f )
	{
		x = (Int32)( x / f );
		y = (Int32)( y / f );
		return *this;
	}
	
	constexpr Point Point::ZERO()
	{
		return { 0, 0 };
	}

} // math
