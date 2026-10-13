/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redThreadsAtomic.h"

#include <limits>

namespace red
{

template< typename TUnderlyingType, typename TDefaultRefCountPolicy >
class RefCount
{
	static_assert( std::is_unsigned< TUnderlyingType >::value, "Only unsigned types supported" );

public:
	using UnderlyingType = TUnderlyingType;

	// No value init
	RefCount() = default;

	static const constexpr UnderlyingType c_maxValue = std::numeric_limits< UnderlyingType >::max();

	constexpr explicit RefCount( UnderlyingType count )
		: m_count( count )
	{
	}

	// Resets the refcount. Not race condition safe.
	void Reset( UnderlyingType count );

	// !!! NEVER call Release() then check IsZero() !!!
	// It's a race condition in a multithreaded environment.
	// Use the return value from Release() instead.
	//
	// Can also be dangerous to use if the underlying object could be destroyed.
	// But this function could also be useful when known for sure that the refcount can't have been externally changed yet
	// and you want to see if it's at zero, for whatever reason.
	//
	// !!! DO NOT create a function that returns the refcount itself !!!
	// 1) It'll break the encapsulation that it's unsigned so can use all the bits, which is more important for smaller sizes.
	// 2) It's not safe at all. This function Unsafe_IsZero() is already borderline.
	template< typename U = TDefaultRefCountPolicy >
	Bool Unsafe_IsZero() const;

	template< typename U = TDefaultRefCountPolicy >
	void AddRef();

	// Returns true if the last refcount has been released.
	template< typename U = TDefaultRefCountPolicy >
	Bool Release();

private:
	mutable UnderlyingType m_count;
};

namespace prv
{

template< Uint32 NumBytes >
struct RefCountOps;

template<>
struct RefCountOps< 8 >
{
	static RED_FORCE_INLINE Uint64 AtomicIncrement( Uint64& count )
	{
		return atomic::Increment64( atomic::alias_cast64( &count ) );
	}

	static RED_FORCE_INLINE Uint64 AtomicDecrement( Uint64& count )
	{
		return atomic::Decrement64( atomic::alias_cast64( &count ) );
	}
};

template<>
struct RefCountOps< 4 >
{
	static RED_FORCE_INLINE Uint32 AtomicIncrement( Uint32& count )
	{
		return atomic::Increment32( atomic::alias_cast32( &count ) );
	}

	static RED_FORCE_INLINE Uint32 AtomicDecrement( Uint32& count )
	{
		return atomic::Decrement32( atomic::alias_cast32( &count ) );
	}
};

template<>
struct RefCountOps< 2 >
{
	static RED_FORCE_INLINE Uint16 AtomicIncrement( Uint16& count )
	{
		return atomic::Increment16( atomic::alias_cast16( &count ) );
	}

	static RED_FORCE_INLINE Uint16 AtomicDecrement( Uint16& count )
	{
		return atomic::Decrement16( atomic::alias_cast16( &count ) );
	}
};

template<>
struct RefCountOps< 1 >
{
	static RED_FORCE_INLINE Uint8 AtomicIncrement( Uint8 & count )
	{
		return atomic::Increment8( atomic::alias_cast8( &count ) );
	}

	static RED_FORCE_INLINE Uint8 AtomicDecrement( Uint8& count )
	{
		return atomic::Decrement8( atomic::alias_cast8( &count ) );
	}
};

} // prv

struct RefCountPolicyAtomic
{
	template< typename T >
	static RED_FORCE_INLINE T Increment( T& count )
	{
		return prv::RefCountOps< sizeof(T) >::AtomicIncrement( count );
	}
	
	template< typename T >
	static RED_FORCE_INLINE T Decrement( T& count )
	{
		return prv::RefCountOps< sizeof(T) >::AtomicDecrement( count );
	}

	template< typename T>
	static RED_FORCE_INLINE Bool IsZero( T& count )
	{
		// #tbd: relaxed on x64 arch, could do atomic OR but then cause RW update
		return const_cast< volatile T& >( count ) == 0;
	}
};

struct RefCountPolicyRelaxed
{
	template< typename T >
	static RED_FORCE_INLINE T Increment( T& count )
	{
		return ++count;				
	}

	template< typename T >
	static RED_FORCE_INLINE T Decrement( T& count )
	{
		return --count;
	}

	template< typename T>
	static RED_FORCE_INLINE Bool IsZero( T& count )
	{
		return count == 0;
	}
};

template< typename TDefaultRefCountPolicy = RefCountPolicyAtomic >
using RefCount8 = RefCount< Uint8, TDefaultRefCountPolicy >;

template< typename TDefaultRefCountPolicy = RefCountPolicyAtomic >
using RefCount16 = RefCount< Uint16, TDefaultRefCountPolicy >;

template< typename TDefaultRefCountPolicy = RefCountPolicyAtomic >
using RefCount32 = RefCount< Uint32, TDefaultRefCountPolicy >;

template< typename TDefaultRefCountPolicy = RefCountPolicyAtomic >
using RefCount64 = RefCount< Uint64, TDefaultRefCountPolicy >;

namespace prv
{
template< typename T >
constexpr Bool is_trivial_check()
{
	static_assert( std::is_trivially_constructible< T >::value, "is_trivially_constructible failed" );
	static_assert( std::is_trivially_copy_constructible< T >::value, "is_trivially_copy_constructible failed" );
	static_assert( std::is_trivially_move_constructible< T >::value, "is_trivially_move_constructible failed" );
	static_assert( std::is_trivially_destructible< T >::value, "is_trivially_destructible failed" );
	static_assert( std::is_trivially_copy_assignable< T >::value, "is_trivially_copy_assignable failed" );
	static_assert( std::is_trivially_move_assignable< T >::value, "is_trivially_move_assignable failed" );
	return true;
}

}

// Unlike atomics, it's useful in many cases to allow this. But of course you have to know what you're doing!
static_assert( prv::is_trivial_check< RefCount8<> >(), "" );
static_assert( prv::is_trivial_check< RefCount16<> >(), "" );
static_assert( prv::is_trivial_check< RefCount32<> >(), "" );
static_assert( prv::is_trivial_check< RefCount64<> >(), "" );

}

#include "refCount.hpp"
