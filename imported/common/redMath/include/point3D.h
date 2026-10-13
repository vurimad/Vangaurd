/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace math
{

	// 3D Point3D
	struct Point3D
	{
		Int32	x;
		Int32	y;
		Int32	z;

		// Empty point, 0,0,0
		constexpr Point3D();

		// Construct
		constexpr Point3D( Int32 _x, Int32 _y, Int32 _z );

		constexpr Point3D operator-() const;
		constexpr Point3D operator+( const Point3D& p ) const;
		constexpr Point3D operator-( const Point3D& p ) const;
		constexpr Point3D operator*( const Float f ) const;
		constexpr Point3D operator/( const Float f ) const;

		constexpr Bool operator==( const Point3D& p ) const;
		constexpr Bool operator!=( const Point3D& p ) const;

		RED_INLINE Point3D& operator-=( const Point3D& p );
		RED_INLINE Point3D& operator+=( const Point3D& p );
		RED_INLINE Point3D& operator*=( const Float f );
		RED_INLINE Point3D& operator/=( const Float f );

        RED_INLINE Uint32 CalcHash() const;

		// predefined zero point
		constexpr static Point3D ZERO();
	};

} // math

#include "point3D.hpp"