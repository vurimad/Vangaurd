/*
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red { namespace policies {
	
//////////////////////////////////////////////////////////////////////////
// constructor

template < typename T >
struct TrivialConstructorExecutor
{
	RED_INLINE static void Execute( T* dst )
	{
		red::Memzero( dst, sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, Uint32 num )
	{
		red::Memzero( dst, num * sizeof( T ) );
	}
};

template < typename T >
struct NonTrivialConstructorExecutor
{
	RED_INLINE static void Execute( T* dst )
    {
		::new( dst ) T;
    }

    RED_INLINE static void Execute( T* dst, Uint32 num )
    {
		while ( num-- > 0 )
		{
			::new( dst++ ) T;
		}
    }
};

template < typename T >
struct ConstructorExecutorSelector
{
    typedef typename std::conditional< std::is_trivially_constructible< T >::value,
                                       TrivialConstructorExecutor< T >,
                                       NonTrivialConstructorExecutor< T >
                                     >::type Type;
};

//////////////////////////////////////////////////////////////////////////
// copy constructor

template < typename T >
struct TrivialCopyConstructorExecutor
{
	RED_INLINE static void Execute( T* dst, const T* src )
	{
		red::Memcpy( dst, src, sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, const T* src, Uint32 num )
	{
		red::Memcpy( dst, src, num * sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, const T& src, Uint32 num )
	{
		std::fill_n( dst, num, src );
	}
};

template < typename T >
struct NonTrivialCopyConstructorExecutor
{
	RED_INLINE static void Execute( T* dst, const T* src )
	{
		::new( dst ) T( *src );
	}

	RED_INLINE static void Execute( T* dst, const T* src, Uint32 num )
	{
		while ( num-- > 0 )
		{
			::new( dst++ ) T( *( src++ ) );
		}
	}

	RED_INLINE static void Execute( T* dst, const T& src, Uint32 num )
	{
		while ( num-- > 0 )
		{
			::new( dst++ ) T( src );
		}
	}
};

template < typename T >
struct CopyConstructorExecutorSelector
{
	typedef typename std::conditional< std::is_trivially_copy_constructible< T >::value,
									   TrivialCopyConstructorExecutor< T >,
									   NonTrivialCopyConstructorExecutor< T >
									 >::type Type;
};

//////////////////////////////////////////////////////////////////////////
// move constructor

template < typename T >
struct TrivialMoveConstructorExecutor
{
	RED_INLINE static void Execute( T* dst, T* src )
	{
		red::Memmove( dst, src, sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, T* src, Uint32 num )
	{
		red::Memmove( dst, src, num * sizeof( T ) );
	}
};

template < typename T >
struct NonTrivialMoveConstructorExecutor
{
	RED_INLINE static void Execute( T* dst, T* src )
	{
		::new( dst ) T( std::move( *src ) );
	}

	RED_INLINE static void Execute( T* dst, T* src, Uint32 num )
	{
		while ( num-- > 0 )
		{
			::new( dst++ ) T( std::move( *src++ ) );
		}
	}
};

template < typename T >
struct MoveConstructorExecutorSelector
{
	typedef typename std::conditional< std::is_trivially_move_constructible< T >::value,
		                               TrivialMoveConstructorExecutor< T >,
		                               NonTrivialMoveConstructorExecutor< T >
	                                 >::type Type;
};
	
//////////////////////////////////////////////////////////////////////////
// destructor

template < typename T >
struct TrivialDestructorExecutor
{
	RED_INLINE static void Execute( T* dst )
	{
		RED_UNUSED( dst );
	}

	RED_INLINE static void Execute( T* dst, Uint32 num )
	{
		RED_UNUSED2( dst, num );
	}
};

template < typename T >
struct NonTrivialDestructorExecutor
{
	RED_INLINE static void Execute( T* dst )
	{
		dst->~T();
	}

	RED_INLINE static void Execute( T* dst, Uint32 num )
	{
		// loop uses descending order to preserve the canonical destruction order of C++ 
		dst += ( num - 1 );
		while ( num-- > 0 )
		{
			( dst-- )->T::~T();
		}
	}
};

template < typename T >
struct DestructorExecutorSelector
{
	typedef typename std::conditional< std::is_trivially_destructible< T >::value,
									   TrivialDestructorExecutor< T >,
									   NonTrivialDestructorExecutor< T >
									 >::type Type;
};

//////////////////////////////////////////////////////////////////////////
// copy assignment

template < typename T >
struct TrivialCopyAssignmentExecutor
{
	RED_INLINE static void Execute( T* dst, const T* src )
	{
		red::Memcpy( dst, src, sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, const T* src, Uint32 num )
	{
		red::Memcpy( dst, src, num * sizeof( T ) );
	}
};

template < typename T >
struct NonTrivialCopyAssignmentExecutor
{
	RED_INLINE static void Execute( T* dst, const T* src )
	{
		*dst = *src;
	}

	RED_INLINE static void Execute( T* dst, const T* src, Uint32 num )
	{
		// if end of "src" overlaps "dst" we need to copy elements backwards

		if ( dst > src && ( dst - src ) < num )
		{
			dst += ( num - 1 );
			src += ( num - 1 );
			while ( num-- )
			{
				*( dst-- ) = *( src-- );
			}
		}
		else
		{
			while ( num-- )
			{
				*( dst++ ) = *( src++ );
			}
		}

		// MSVC sees this functions as "unsafe" and generates warning (C4996) treated as error in Debug build.
		// We should either use -D_SCL_SECURE_NO_WARNINGS or use the code above.
		//if ( dst > src && ( dst - src ) < num )
		//{
		//	std::copy_backward( src, src + num, dst + num );
		//}
		//else
		//{
		//	std::copy( src, src + num, dst );
		//}
	}
};

template < typename T >
struct CopyAssignmentExecutorSelector
{
	typedef typename std::conditional< std::is_trivially_copy_assignable< T >::value,
							           TrivialCopyAssignmentExecutor< T >,
								       NonTrivialCopyAssignmentExecutor< T >
									 >::type Type;
};

//////////////////////////////////////////////////////////////////////////
// move assignment

template < typename T >
struct TrivialMoveAssignmentExecutor
{
	RED_INLINE static void Execute( T* dst, T* src )
	{
		red::Memmove( dst, src, sizeof( T ) );
	}

	RED_INLINE static void Execute( T* dst, T* src, Uint32 num )
	{
		red::Memmove( dst, src, num * sizeof( T ) );
	}
};

template < typename T >
struct NonTrivialMoveAssignmentExecutor
{
	RED_INLINE static void Execute( T* dst, T* src )
	{
		*dst = std::move( *src );
	}

	RED_INLINE static void Execute( T* dst, T* src, Uint32 num )
	{
		// if end of "src" overlaps "dst" we need to move elements backwards

		if ( dst > src && ( dst - src ) < num )
		{
			dst += ( num - 1 );
			src += ( num - 1 );
			while ( num-- )
			{
				*( dst-- ) = std::move( *( src-- ) );
			}
		}
		else
		{
			while ( num-- )
			{
				*( dst++ ) = std::move( *( src++ ) );
			}
		}

		// MSVC sees this functions as "unsafe" and generates warning (C4996) treated as error in Debug build.
		// We should either use -D_SCL_SECURE_NO_WARNINGS or use the code above.
		//if ( dst > src && ( dst - src ) < num )
		//{
		//	std::move_backward( src, src + num, dst + num );
		//}
		//else
		//{
		//	std::move( src, src + num, dst );
		//}
	}
};

template < typename T >
struct MoveAssignmentExecutorSelector
{
	typedef typename std::conditional< std::is_trivially_move_assignable< T >::value,
									   TrivialMoveAssignmentExecutor< T >,
									   NonTrivialMoveAssignmentExecutor< T >
									 >::type Type;
};

//////////////////////////////////////////////////////////////////////////
// compare

template < typename T >
struct TrivialComparePolicy
{
	static RED_INLINE Bool Equal( const T* obj1, const T* obj2 )
	{
		return red::Memcmp( obj1, obj2, sizeof( T ) ) == 0;
	}

	static RED_INLINE Bool Equal( const T* buf1, const T* buf2, Uint32 num )
	{
		return red::Memcmp( buf1, buf2, num * sizeof( T ) ) == 0;
	}
};

template < typename T >
struct NonTrivialComparePolicy
{
	static RED_INLINE Bool Equal( const T* obj1, const T* obj2 )
	{
		return *obj1 == *obj2;
	}

	static RED_INLINE Bool Equal( const T* buf1, const T* buf2, Uint32 num )
	{
		while ( num-- > 0 )
		{
			if ( !( *buf1++ == *buf2++ ) )
			{
				return false;
			}
		}
		return true;
	}
};

template < typename T >
struct ComparePolicySelector
{
	typedef typename std::conditional< std::is_pod< T >::value,
								       TrivialComparePolicy< T >,
							           NonTrivialComparePolicy< T >
								     >::type Type;
};

} } // red::policies
