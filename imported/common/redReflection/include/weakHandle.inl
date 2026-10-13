/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_CORE_WEAK_HANDLE_INL_
#define _RED_CORE_WEAK_HANDLE_INL_

#include "rttiUtils.h"

template< typename T >
RED_INLINE WeakHandle< T >::WeakHandle()
{}

template< typename T >
RED_INLINE WeakHandle< T >::WeakHandle( std::nullptr_t )
{}

template< typename T >
RED_INLINE WeakHandle< T >::WeakHandle( const WeakHandle & copyFrom )
	:	ParentType( copyFrom )
{}

template< typename T >
RED_INLINE WeakHandle< T >::WeakHandle( WeakHandle && pointer )
	: ParentType( std::forward< WeakHandle >( pointer ) )
{
}

template< typename T >
template< typename U >
RED_INLINE WeakHandle< T >::WeakHandle( const WeakHandle< U > & copyFrom )
	:	ParentType( copyFrom )
{
}

template< typename T >
template< typename U >
RED_INLINE WeakHandle< T >::WeakHandle( WeakHandle< U > && pointer )
	:	ParentType( std::forward< WeakHandle >( pointer ) )
{
}

template< typename T >
template< typename U  >
RED_INLINE WeakHandle< T >::WeakHandle( const THandle< U > & copyFrom )
	:	ParentType( copyFrom )
{
	static_assert( std::is_base_of< ISerializable, T >::value, "WeakHandle can't be used for type unrelated to ISerializable." );
}

template< typename T >
RED_INLINE WeakHandle< T >::~WeakHandle()
{
}

template< typename T >
RED_INLINE WeakHandle< T > & WeakHandle< T >::operator=( const WeakHandle & copyFrom )
{
	WeakHandle( copyFrom ).Swap( *this );
	return *this;
}

template< typename T >
RED_INLINE WeakHandle< T > & WeakHandle< T >::operator=( WeakHandle && pointer )
{
	WeakHandle( std::move( pointer ) ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE WeakHandle< T > & WeakHandle< T >::operator=( const WeakHandle< U > & copyFrom )
{
	WeakHandle( copyFrom ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE WeakHandle< T > & WeakHandle< T >::operator=( WeakHandle< U > && pointer )
{
	WeakHandle( std::move( pointer ) ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE WeakHandle< T > & WeakHandle< T >::operator=( const THandle< U > & copyFrom )
{
	WeakHandle( copyFrom ).Swap( *this );
	return *this;
}

template< typename T >
THandle< T > WeakHandle< T >::ToHandle() const
{
	return Lock();
}

template< typename T >
THandle< T > WeakHandle< T >::Lock() const
{
	return THandle< T >( *this );
}

template< typename T >
RED_INLINE const CName TTypeName< WeakHandle< T > >::GetTypeName()
{
	static const CName name = rtti::FormatWeakHandleTypeName( TTypeName< T >::GetTypeName() );
	return name;
}

// WeakHandle cast
template< class _DestType, class _SrcType >
RED_INLINE WeakHandle< _DestType > Cast( const WeakHandle< _SrcType >& srcObj )
{
	return Cast< _DestType >( srcObj.ToHandle() );
}

// WeakHandle safe cast
template< class _DestType, class _SrcType >
RED_INLINE WeakHandle< _DestType > SafeCast( const WeakHandle< _SrcType >& srcObj )
{
	return SafeCast< _DestType >( srcObj.ToHandle() );
}

#endif
