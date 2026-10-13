/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "assert.h"
#include <limits>
#include <utility>
#include <cmath>

constexpr Uint32 Max( Uint32 a, Uint32 b )
{ 
	return (a>=b) ? a : b; 
}

constexpr Int32 Max( Int32 a, Int32 b )
{ 
	return (a>=b) ? a : b; 
}

constexpr Uint64 Max( Uint64 a, Uint64 b )
{ 
	return (a>=b) ? a : b; 
}

constexpr Int64 Max( Int64 a, Int64 b )
{ 
	return (a>=b) ? a : b; 
}

constexpr Uint32 Min( Uint32 a, Uint32 b )
{ 
	return (a<=b) ? a : b; 
}

constexpr Int32 Min( Int32 a, Int32 b )
{ 
	return (a<=b) ? a : b; 
}

constexpr Uint64 Min( Uint64 a, Uint64 b )
{ 
	return (a<=b) ? a : b; 
}

constexpr Int64 Min( Int64 a, Int64 b )
{ 
	return (a<=b) ? a : b; 
}

template <class T> 
RED_INLINE T Max( const T& a, const T& b )   
{ 
	return (a>=b) ? a : b; 
}

template <class T> 
RED_INLINE T Min( const T& a, const T& b )   
{ 
	return (a<=b) ? a : b; 
}

template <class T> 
RED_INLINE T Max( const T& a, const T& b, const T& c )   
{ 
	return Max( Max( a, b ), c );
}

template <class T> 
RED_INLINE T Min( const T& a, const T& b, const T& c )   
{ 
	return Min( Min( a, b ), c );
}

template <class T> 
RED_INLINE T Clamp( const T& x, const T& min, const T& max )
{ 
	return Min( Max( x, min ), max );
}

template <class T>
RED_INLINE T Saturate( const T& x )
{
	return Min( Max( x, (T)0 ), (T)1 );
}

template <class T>
RED_INLINE T Remap( const T& value, const T& low1, const T& high1, const T& low2, const T& high2 )
{
	return low2 + (value - low1) * (high2 - low2) / Max( high1 - low1, (T)1e-7 );
}

template <class T>
RED_INLINE T RemapSaturate( const T& value, const T& low1, const T& high1, const T& low2, const T& high2 )
{
	return low2 + (value - low1) * Saturate<T>( (high2 - low2) / Max( high1 - low1, (T)1e-7 ) );
}

template <class T> 
RED_INLINE T Abs(const T& a)
{ 
	return (a>=T(0)) ? a : -a; 
}

template <>
RED_INLINE Float Abs< Float >( const Float& a )
{ 
	return static_cast< Float >( fabs( a ) );
}

template <class T> 
RED_INLINE Int32 Sgn( const T& a )
{ 
	static const T zero = T(0);
	return (a>zero) ? 1 : ( a<zero ? -1 : 0 );
}

template <class T>
RED_INLINE Float SgnF( const T& a )
{
	static const T zero = T( 0 );
	return (a > zero) ? 1.f : (a < zero ? -1.f : 0.f);
}

template <class T> 
RED_INLINE void Swap( T& a, T& b )
{ 
	T tmp = std::move( a ); 
	a = std::move( b );	
	b = std::move( tmp ); 
}

template < class Val >
RED_INLINE Val ArithmeticAverage( const Val& v1, const Val& v2 )
{
	return (v1 + v2) / 2;
}

namespace red
{
	// Offset pointer by number of bytes
	constexpr void* OffsetAddress( void* ptr, ptrdiff_t byteOffset )
	{
		return static_cast< Uint8* >( ptr ) + byteOffset;
	}

	// Offset pointer by number of bytes
	constexpr const void* OffsetAddress( const void* ptr, ptrdiff_t byteOffset )
	{
		return static_cast< const Uint8* >( ptr ) + byteOffset;
	}

	constexpr bool IsPowerOf2( Uint64 value )
	{
		return ( value & ( value - 1 ) ) == 0;
	}

	RED_INLINE Int32 RoundUpToPowerOf2( Int32 v )
	{
		return static_cast< Int32 >( RoundUpToPowerOf2( static_cast< Uint32 >( v ) ) );
	}

	RED_INLINE Int32 RoundDownToPowerOf2( Int32 v )
	{
		return static_cast< Int32 >( RoundDownToPowerOf2( static_cast< Uint32 >( v ) ) );
	}
	
	RED_INLINE Int64 RoundUpToPowerOf2( Int64 v )
	{
		return static_cast< Int64 >( RoundUpToPowerOf2( static_cast< Uint64 >( v ) ) );
	}
	
	RED_INLINE Int64 RoundDownToPowerOf2( Int64 v )
	{
		return static_cast< Int64 >( RoundDownToPowerOf2( static_cast< Uint64 >( v ) ) );
	}

	RED_INLINE Uint32 RoundUpToPowerOf2( Uint32 v )
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		return ++v;
	}

	RED_INLINE Uint32 RoundDownToPowerOf2( Uint32 v )
	{
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		return v - (v >> 1);
	}

	RED_INLINE Uint64 RoundUpToPowerOf2( Uint64 v )
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		return ++v;
	}

	RED_INLINE Uint64 RoundDownToPowerOf2( Uint64 v )
	{
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v |= v >> 32;
		return v - (v >> 1);
	}

	RED_INLINE Int32 AlignUp( Int32 value, Int32 alignment )
	{
		const Uint32 mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	RED_INLINE Uint16 AlignUp( Uint16 value, Uint16 alignment )
	{
		const Uint16 mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	RED_INLINE Uint32 AlignUp( Uint32 value, Uint32 alignment )
	{
		const Uint32 mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	RED_INLINE Uint64 AlignUp( Uint64 value, Uint64 alignment )
	{
		const Uint64 mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	RED_INLINE Uint64 AlignUp( Uint64 value, Uint32 alignment )
	{
		const Uint64 mask = static_cast< Uint64 >( alignment ) - 1;
		return (value + mask) & ~mask;
	}

	RED_INLINE Uint32 AlignDown( Uint32 value, Uint32 alignment )
	{
		return value & ~( alignment - 1 );
	}

	RED_INLINE Int32 AlignDown( Int32 value, Int32 alignment )
	{
		return value & ~(alignment - 1);
	}

	RED_INLINE Uint64 AlignDown( Uint64 value, Uint64 alignment )
	{
		return value & ~( alignment - 1 );
	}

	RED_INLINE Uint64 AlignDown( Uint64 value, Uint32 alignment )
	{
		return value & ~( static_cast<Uint64>( alignment ) - 1 );
	}

	RED_INLINE Uint32 DivideRoundingUp( Uint32 a, Uint32 b )
	{
		return ( a + b - 1 ) / b;
	}

	RED_INLINE bool IsAligned( Uint64 value, Uint64 alignment )
	{
		RED_ASSERT( IsPowerOf2( alignment ) && alignment > 0, "Provided alignment need to be power of 2." );
		return ( value & ( alignment - 1 ) ) == 0;
	}

	RED_INLINE bool IsAligned( const void * memory, Uint64 alignment )
	{
		return IsAligned( reinterpret_cast< Uint64 >( memory ), alignment );  
	}

	RED_INLINE Uint64 AlignAddress( Uint64 address, Uint64 alignment )
	{
		RED_ASSERT( IsPowerOf2( alignment ) && alignment > 0, "Provided alignment need to be power of 2." );
		const Uint64 mask = -static_cast< Int64 >( alignment ); 
		const Uint64 result = ( address + ( alignment - 1 ) ) & mask; 
		return result;
	}

	RED_INLINE void * AlignAddress( const void * address, Uint64 alignment )
	{
		Uint64 value = reinterpret_cast< Uint64 >( address );
		const Uint64 result = AlignAddress( value, alignment );
		return reinterpret_cast< void* >( result );
	}

	template< class T >
	RED_INLINE  T* OffsetPtr( T* ptr, ptrdiff_t byteOffset )
	{
		return (T*)( (Uint8*) ptr + byteOffset );
	}

	// Align pointer
	template< class T >
	RED_INLINE T* AlignPtr( T* ptr, size_t alignment )
	{
		uintptr_t addr = ( uintptr_t ) ptr;
		addr = (addr + (alignment - 1)) & ~( alignment - 1 );
		return ( T* ) addr;
	}

	// Align offset in memory
	RED_INLINE uintptr_t AlignOffset( uintptr_t addr, size_t alignment )
	{
		if ( alignment <= 1 )
		{
			return addr;
		}
		else
		{
			return (addr + (alignment - 1)) & ~( alignment - 1 );
		}
	}

	template< class TPtrDiff >
	RED_INLINE TPtrDiff ByteDistance( const void* begin, const void* end )
	{
		const ptrdiff_t diff = reinterpret_cast< const uintptr_t >( end ) - reinterpret_cast< const uintptr_t >( begin );
		RED_ASSERT( diff >= std::numeric_limits< TPtrDiff >::lowest() && diff <= (std::numeric_limits< TPtrDiff >::max)(), "ByteDistance cannot store value in specified type." );
		return static_cast< TPtrDiff >( diff );
	}

	template < typename TPtrDiff, typename T >
	RED_INLINE TPtrDiff Distance( const T* begin, const T* end )
	{
		const ptrdiff_t diff = end - begin;
		RED_ASSERT( diff >= std::numeric_limits< TPtrDiff >::lowest() && diff <= (std::numeric_limits< TPtrDiff >::max)(), "Distance cannot store value in specified type." );
		return static_cast< TPtrDiff >( diff );
	}

	template<class T, class U>
	constexpr T Exchange(T& val, U&& newVal)
	{
		return std::exchange(val, newVal);
	}

	template< typename T, typename U >
	RED_FORCE_INLINE ScopedFlag< T, U >::ScopedFlag( T& flag, U finalValue )
		:	m_flag( flag ),
			m_finalValue( finalValue )
	{}

	template< typename T, typename U >
	RED_FORCE_INLINE ScopedFlag< T, U >::~ScopedFlag() 
	{ 
		m_flag = m_finalValue; 
	}
}

