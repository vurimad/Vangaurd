/**
* Copyright (c) 2013-16 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "redSystemPublic.h"


//////////////////////////////////////////////////////////////////////////
// Utility macros

#define RED_ARRAY_COUNT(arr)		( sizeof( arr ) / sizeof( ( arr )[ 0 ] ) )
#define RED_ARRAY_COUNT_U32(arr)	static_cast< ::red::Uint32 >( ( sizeof( arr ) / sizeof( ( arr )[ 0 ] ) ) )

#define RED_SAFE_RELEASE( x )		{ if ( x ) { (x)->Release(); }; x = nullptr; }

// Flags
#define RED_FLAG( x )				( 1U << ( x ) )
#define RED_FLAG64( x )				( 1LL << ( x ) )

// Define generic name for base class
#define RED_BASE_CLASS( _blassClass )				using TBaseClass = _blassClass;

// Define empty default constructor
#define RED_EMPTY_DEFAULT_CONSTRUCTOR( _class )		_class() {};

//////////////////////////////////////////////////////////////////////////
//
// Note: Overloaded on basic types so class constant integer inline defines can be used with them.
// Otherwise Clang could cause a linker error for the template version that takes references. Could have used a #define instead in the first place.

// todo: we should change all calls to these functions in headers from <function> to red::<function>

// todo: namespace red 
//{
	constexpr Uint32 Max( Uint32 a, Uint32 b );
	constexpr Int32 Max( Int32 a, Int32 b );
	constexpr Uint64 Max( Uint64 a, Uint64 b );
	constexpr Int64 Max( Int64 a, Int64 b );

	constexpr Uint32 Min( Uint32 a, Uint32 b );
	constexpr Int32 Min( Int32 a, Int32 b );
	constexpr Uint64 Min( Uint64 a, Uint64 b );
	constexpr Int64 Min( Int64 a, Int64 b );

	template <class T> 
	T Max( const T& a, const T& b );

	template <class T> 
	T Min( const T& a, const T& b );

	template <class T> 
	T Max( const T& a, const T& b, const T& c );

	template <class T> 
	T Min( const T& a, const T& b, const T& c );

	template <class T> 
	T Clamp( const T& x, const T& min, const T& max );

	template <class T> 
	T Abs(const T& a);

	template <>
	Float Abs< Float >( const Float& a );

	template <class T> 
	Int32 Sgn( const T& a );

	template <class T>
	Float SgnF( const T& a );

	template <class T> 
	void Swap( T& a, T& b );

	template < class Val >
	Val ArithmeticAverage( const Val& v1, const Val& v2 );
//}


//////////////////////////////////////////////////////////////////////////
// Utility Classes
namespace red
{
	constexpr void* OffsetAddress( void* ptr, ptrdiff_t byteOffset );
	constexpr const void* OffsetAddress( const void* ptr, ptrdiff_t byteOffset );

	constexpr bool IsPowerOf2( Uint64 value );
	Int32 RoundUpToPowerOf2( Int32 v );
	Int32 RoundDownToPowerOf2( Int32 v );
	Int64 RoundUpToPowerOf2( Int64 v );
	Int64 RoundDownToPowerOf2( Int64 v );
	Uint32 RoundUpToPowerOf2( Uint32 v );
	Uint32 RoundDownToPowerOf2( Uint32 v );
	Uint64 RoundUpToPowerOf2( Uint64 v );
	Uint64 RoundDownToPowerOf2( Uint64 v );

	Int32 AlignUp( Int32 value, Int32 alignment );
	Uint16 AlignUp( Uint16 value, Uint16 alignment );
	Uint32 AlignUp( Uint32 value, Uint32 alignment );
	Uint64 AlignUp( Uint64 value, Uint64 alignment );
	Uint64 AlignUp( Uint64 value, Uint32 alignment );
	Uint32 AlignDown( Uint32 value, Uint32 alignment );
	Uint64 AlignDown( Uint64 value, Uint64 alignment );
	Uint64 AlignDown( Uint64 value, Uint32 alignment );


	Uint32 DivideRoundingUp( Uint32 a, Uint32 b );

	bool IsAligned( Uint64 value, Uint64 alignment );
	bool IsAligned( const void * block, Uint64 alignment );

	Uint64 AlignAddress( Uint64 address, Uint64 alignment );
	void * AlignAddress( const void * address, Uint64 alignment );

	template< class T >
	T* OffsetPtr( T* ptr, ptrdiff_t byteOffset );

	template< class T >
	T* AlignPtr( T* ptr, size_t alignment );

	uintptr_t AlignOffset( uintptr_t addr, size_t alignment );

	template < typename TPtrDiff >
	TPtrDiff ByteDistance( const void* begin, const void* end );

	template < typename TPtrDiff, typename T >
	TPtrDiff Distance( const T* begin, const T* end );

    template<class T, class U = T>
    constexpr T Exchange(T& val, U&& new_val);
	
	enum EUndefined : Int32 { Undefined }; // ctremblay what is this and why its here.

	class REDSYSTEM_API BooleanLatch
	{
	public:
		BooleanLatch()
			: m_value(false)
		{}

		void Set() { m_value = true; }

		operator Bool() const { return m_value; }

	private:
		Bool m_value;
	};

	//////////////////////////////////////////////////////////////////////////
	// Inherit from this class to prevent your class from being copied
	// todo:
	class REDSYSTEM_API NonCopyable
	{
	public:

		NonCopyable( NonCopyable const & ) = delete;

		NonCopyable& operator=( NonCopyable const & ) = delete;

		// - - - - - - - - - - - - - - - - - -

		NonCopyable() = default;
		NonCopyable( NonCopyable&& ) = default;
		NonCopyable& operator=( NonCopyable&& ) = default;
	};

	//////////////////////////////////////////////////////////////////////////
	// 
	template< typename T, typename U = T >
	struct ScopedFlag : public NonCopyable
	{		
	public:
		ScopedFlag( T& flag, U finalValue );
		~ScopedFlag();

	private:
		T& m_flag;
		U  m_finalValue;
	};
}

#include "utility.hpp"
