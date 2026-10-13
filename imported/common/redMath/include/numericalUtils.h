/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include <limits>
#include <utility>
#include <type_traits>
#include <algorithm>

namespace math
{
	template< typename T >
	RED_INLINE constexpr const T& Max(const T& a, const T& b )
	{
		return std::max( a, b );
	}

	template< typename T >
	RED_INLINE constexpr const T& Min(const T& a, const T& b )
	{
		return std::min( a, b );
	}

	template< typename T, typename ... Tx >
	RED_INLINE constexpr const T& Max(const T& a, const T& b, Tx&&... args)
	{
		return Max(Max(a, b), std::forward<Tx>(args)...);
	}

	template< typename T, typename ... Tx >
	RED_INLINE constexpr const T& Min(const T& a,const T& b, Tx&&... args)
	{
		return Min(Min(a, b), std::forward<Tx>(args)...);
	}

	template < typename T >
	RED_INLINE constexpr T Clamp( const T& x, const T& min, const T& max )
	{ 
		return Min( Max( x, min ), max );
	}

	template < typename T >
	RED_INLINE constexpr T ClampFromAbove(const T& x, const T& max)
	{
		return Min(x, max);
	}

	template < typename T >
	RED_INLINE constexpr T ClampFromBelow(const T& x, const T& min)
	{
		return Max(x, min);
	}

	template < typename T >
	RED_INLINE constexpr T Abs(const T& a )
	{
		return (a < T{ 0 }) ? -a : a;
	}

	template < typename T >
	RED_INLINE constexpr int Sgn(const T& a )
	{
		return (a > T{ 0 }) ? 1 : (a < T{ 0 } ? -1 : 0);
	}

	template < typename T >
	RED_INLINE void Swap( T& a, T& b )
	{
		std::swap(a, b);
	}

	template < typename T >
	RED_INLINE constexpr typename std::enable_if<!std::is_integral<T>::value, T>::type ArithmeticAverage(const T& v1, const T& v2)
	{
		return (v1 + v2) * 0.5f;
	}

	template< typename T >
	RED_INLINE constexpr typename std::enable_if<std::is_integral<T>::value, T>::type ArithmeticAverage(const T& v1, const T& v2)
	{
		return (v1 + v2) >> 1;
	}

	template < typename T >
	constexpr T Lerp( float t, const T& src, const T& dst )
	{ 
		return src * ( 1.f - t ) + dst * t;
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool AlmostEquals(const T& val1, const T& val2,const T& eps = std::numeric_limits<T>::epsilon())
	{
		return Abs(val1 - val2) <= eps;
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool AlmostEqualsOrSmaller( const T& val1, const T& val2, const T& eps = std::numeric_limits<T>::epsilon() )
	{
		return AlmostEquals( val1, val2, eps ) || val1 < val2;
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool AlmostEqualsOrGreater( const T& val1, const T& val2, const T& eps = std::numeric_limits<T>::epsilon() )
	{
		return AlmostEquals( val1, val2, eps ) || val1 > val2;
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool InRangeIncluding( const T& val1, const std::pair< T, T >& range, const T& eps = std::numeric_limits<T>::epsilon() )
	{
		return AlmostEqualsOrGreater( val1, range.first, eps ) && AlmostEqualsOrSmaller( val1, range.second, eps );
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool InRangeExcluding( const T& val1, const std::pair< T, T >& range, const T& eps = std::numeric_limits<T>::epsilon() )
	{
		return val1 > range.first && val1 < range.second;
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool RangesOverlap( const std::pair< T, T >& rangeA, const std::pair< T, T >& rangeB, const T& eps = std::numeric_limits<T>::epsilon() )
	{
		return ( AlmostEqualsOrSmaller( rangeA.first, rangeB.second) && AlmostEqualsOrGreater( rangeA.second, rangeB.first ) );
	}

	template< typename T >
	RED_FORCE_INLINE constexpr bool EqualsZero(const T& val1, const T& eps = std::numeric_limits<T>::epsilon())
	{
		return Abs(val1 - T{0}) <= eps;
	}

	RED_INLINE Uint32 IntCeil( Uint32 n, Uint32 d )
	{
		return (n + (d - 1)) / d;
	}


	//do not use std::pow - its rather slow 
	//https://baptiste-wicht.com/posts/2017/09/cpp11-performance-tip-when-to-use-std-pow.html

	template < typename T, typename Q >
	RED_INLINE T Pow( const T& a, const Q& b )
	{
		return std::pow<T>( a, b );
	}

	RED_INLINE Int32 Pow( Int32 a, Uint32 p )
	{
		if ( p == 0 ) return 1;
		if ( p == 1 ) return a;
		if ( p == 2 ) return a * a;
		if ( p == 3 ) return a * a * a;

		Int32 temp = 0;

		temp = Pow( a, static_cast<Uint32>( p * 0.5f ) );

		if ( ( p % 2 ) == 0 )
		{
			return temp * temp;
		}
		else
		{
			return a * temp * temp;
		}
	}

	RED_INLINE Uint32 Pow( Uint32 a, Uint32 p )
	{
		if ( p == 0 ) return 1;
		if ( p == 1 ) return a;
		if ( p == 2 ) return a * a;
		if ( p == 3 ) return a * a * a;

		Uint32 temp = 0;

		temp = Pow( a, static_cast<Uint32>( p * 0.5f ) );

		if ( ( p % 2 ) == 0 )
		{
			return temp * temp;
		}
		else
		{
			return a * temp * temp;
		}
	}

	RED_INLINE Float Pow( Float a, Uint32 p )
	{
		if ( p == 0 ) return 1;
		if ( p == 1 ) return a;
		if ( p == 2 ) return a * a;
		if ( p == 3 ) return a * a * a;

		float temp;

		temp = Pow( a, static_cast<Uint32>( p * 0.5f ) );
		if ( ( p % 2 ) == 0 )
		{
			return temp * temp;
		}
		else
		{
			if ( p > 0.0f )
				return a * temp * temp;
			else
				return ( temp * temp ) / a;
		}
	}

};