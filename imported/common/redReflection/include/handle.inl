/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_CORE_HANDLE_INL_
#define _RED_CORE_HANDLE_INL_

#include "../../redMemory/include/sharedPtrUtils.h"
#include "serializationMapping.h"
#include "serializable.h"

template< typename T >
RED_INLINE THandle< T >::THandle()
{}

template< typename T >
RED_INLINE THandle< T >::THandle( std::nullptr_t )
{}

template< typename T >
RED_INLINE THandle< T >::THandle( const THandle & copyFrom )
	: ParentType( copyFrom )
{}

template< typename T >
RED_INLINE THandle< T >::THandle( THandle && rvalue )
	:	ParentType( std::forward< THandle >( rvalue ) )
{}

template< typename T >
RED_INLINE THandle< T >::~THandle()
{}

template< typename T >
template< typename U >
RED_INLINE THandle< T >::THandle( const U * pointer )
	:	ParentType( const_cast< U* >( pointer ) )
{}

template< typename T >
template< typename U >
RED_INLINE THandle< T >::THandle( const THandle< U > & copyFrom )
	:	ParentType( copyFrom )
{}

template< typename T >
template< typename U >
RED_INLINE THandle< T >::THandle( THandle< U > && rvalue )
	:	ParentType( std::forward< THandle< U > >( rvalue ) )
{}

template< typename T >
template< typename U >
RED_INLINE THandle< T >::THandle( const WeakHandle< U > & copyFrom )
	:	ParentType( copyFrom )
{}

template< typename T >
template< typename U >
RED_INLINE THandle< T >::THandle( red::UniquePtr< U > && rvalue )
	:	ParentType( std::forward< red::UniquePtr< U > >( rvalue ) )
{}

template< typename T >
RED_INLINE THandle< T > & THandle< T >::operator=( const THandle & copyFrom )
{
	THandle( copyFrom ).Swap( *this );
	return *this;
}

template< typename T >
RED_INLINE THandle< T > & THandle< T >::operator=( THandle && rvalue )
{
	THandle( std::move( rvalue ) ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE THandle< T > & THandle< T >::operator=( const THandle< U >  & copyFrom )
{	
	THandle( copyFrom ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE THandle< T > & THandle< T >::operator=( THandle< U >  && rvalue )
{	
	THandle( std::move( rvalue ) ).Swap( *this );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE THandle< T > & THandle< T >::operator=( red::UniquePtr< U > && rvalue )
{	
	THandle( std::move( rvalue ) ).Swap( *this );
	return *this;
}

RED_FORCE_INLINE HandleSharedStorage::HandleSharedStorage()
	: m_pointee( nullptr ),
	m_refCount( nullptr )
{}

RED_INLINE HandleSharedStorage::HandleSharedStorage( const HandleSharedStorage & copyFrom )
	: m_pointee( copyFrom.m_pointee ),
	m_refCount( copyFrom.m_refCount )
{}


RED_INLINE void HandleSharedStorage::Swap( HandleSharedStorage & other )
{
	std::swap( m_refCount, other.m_refCount );
	std::swap( m_pointee, other.m_pointee );
}

template< typename T >
RED_INLINE void HandleSharedStorage::AddRef()
{
	InternalAddRef();
}

template< typename T >
RED_INLINE bool HandleSharedStorage::Release()
{
	return InternalRelease();
}

RED_FORCE_INLINE void * HandleSharedStorage::Get() const
{
	return m_pointee;
}

void InternalDestroyObject( const ISerializable * pointee );

template< typename T >
void HandleSharedStorage::Destroy()
{
	if( InternalDestroyRequest() )
	{
		InternalDestroy();
	}
}

RED_INLINE HandleSharedStorage::HandleSharedStorage( HandleSharedStorage&& rvalue )
	: m_pointee( nullptr )
	, m_refCount( nullptr )
{
	AssignRValue( std::forward< HandleSharedStorage >( rvalue ) );
}

RED_INLINE void HandleSharedStorage::AssignRValue( HandleSharedStorage&& rvalue )
{
	if( &rvalue != this )
	{
		Swap( rvalue );
	}
}

RED_FORCE_INLINE void HandleSharedStorage::UpgradeFromWeakToStrong( HandleSharedStorage & upgradeFrom )
{
	if(upgradeFrom.m_refCount) // if true, always true for the lifetime of this function
	{
		while(1)
		{
			const RefCount::Type count = const_cast< volatile RefCount::Type& >( upgradeFrom.m_refCount->strong );
			if(count == 0)
			{
				return;
			}

			if(atomic::CompareExchange32( &upgradeFrom.m_refCount->strong, count + 1, count ) == count)
			{
				m_refCount = upgradeFrom.m_refCount;
				m_pointee = upgradeFrom.m_pointee;
				return;
			}
		}
	}
}

RED_FORCE_INLINE void HandleSharedStorage::InternalAddRef()
{
	if( m_refCount )
	{
		atomic::Increment32( &m_refCount->strong );
	}
}

RED_FORCE_INLINE bool HandleSharedStorage::InternalRelease()
{
	if ( m_refCount )
	{
		Int32 postDecremented = atomic::Decrement32( &m_refCount->strong );
		RED_FATAL_ASSERT( postDecremented >= 0, "Ref counter underflow" );

		if ( postDecremented == 0 )
		{
			ReleaseWeak();
			return true;
		}
	}

	return false;
}


template< typename T, typename U >
RED_INLINE bool operator==( const THandle< T > & handle, U * pointer )
{ 
	return handle.Get() == pointer;
}

template< typename T, typename U >
RED_INLINE bool operator==( T * pointer, const THandle< U > & handle )
{ 
	return pointer == handle.Get();
}

template< typename T, typename U >
RED_INLINE bool operator==( const THandle< T > & left, const THandle< U > & right )
{
	return left.Get() == right.Get();
}

// Handle cast
template< class _DestType, class _SrcType >
RED_INLINE THandle< _DestType > Cast( const THandle< _SrcType >& srcObj )
{ 
	if( srcObj && srcObj->template IsA< _DestType >() )
	{ 
		THandle< _DestType > result = red::StaticCast< _DestType >( srcObj );
		return result;
	}
	return nullptr;
}

// Handle safe cast
template< class _DestType, class _SrcType >
RED_INLINE THandle< _DestType > SafeCast( const THandle< _SrcType >& srcObj )
{
	RED_FATAL_ASSERT( srcObj && srcObj->template IsA< _DestType >(), "Invalid Cast." );
	THandle< _DestType > result = red::StaticCast< _DestType >( srcObj );
	return result;
}

template< typename T, typename... Args >
RED_INLINE THandle< T > CreateHandle( Args && ... args )
{
	THandle< T > handle( ( RED_NEW( T )( std::forward< Args >( args ) ... ) ) );
	return handle;
}

template< typename T >
RED_INLINE const CName TTypeName< THandle< T > >::GetTypeName()
{
	static const CName name = rtti::FormatHandleTypeName( TTypeName< T >::GetTypeName() );
	return name;
}

#endif
