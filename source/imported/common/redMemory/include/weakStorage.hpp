/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_WEAK_STORAGE_HPP_
#define _RED_MEMORY_WEAK_STORAGE_HPP_

namespace red
{
	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage()
	{}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage( const WeakStorage & copyFrom )
		:	ParentType( copyFrom )
	{
		ParentType::AddRefWeak();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage( WeakStorage && pointer )
		: ParentType( std::forward< WeakStorage >( pointer ) )
	{  		
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage( const WeakStorage< U, Storage, PoolType > & copyFrom )
		:	ParentType( copyFrom )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "WeakStorage can't be constructed from non related type." );
		ParentType::AddRefWeak();
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage( WeakStorage< U, Storage, PoolType > && pointer )
		: ParentType( std::forward< WeakStorage< U, Storage  > >( pointer ) )
	{	
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "WeakStorage can't be constructed from non related type." );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U  >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::WeakStorage( const SharedStorage< U, Storage, PoolType > & copyFrom )
		:	ParentType( copyFrom )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "WeakStorage can't be constructed from non related type." );
		ParentType::AddRefWeak();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType >::~WeakStorage()
	{
		ParentType::ReleaseWeak();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType > & WeakStorage< T, Storage, PoolType >::operator=( const WeakStorage & copyFrom )
	{
		WeakStorage( copyFrom ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType > & WeakStorage< T, Storage, PoolType >::operator=( WeakStorage && pointer )
	{
		WeakStorage( std::move( pointer ) ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType > & WeakStorage< T, Storage, PoolType >::operator=( const WeakStorage< U, Storage, PoolType > & copyFrom )
	{
		WeakStorage( copyFrom ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType > & WeakStorage< T, Storage, PoolType >::operator=( WeakStorage< U, Storage, PoolType > && pointer )
	{
		WeakStorage( std::move( pointer ) ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE WeakStorage< T, Storage, PoolType > & WeakStorage< T, Storage, PoolType >::operator=( const SharedStorage< U, Storage, PoolType > & copyFrom )
	{
		WeakStorage( copyFrom ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > WeakStorage< T, Storage, PoolType >::Lock() const
	{
		return SharedStorage< T, Storage, PoolType >( *this );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE bool WeakStorage< T, Storage, PoolType >::Expired() const
	{
		return ParentType::GetRefCount() == 0;
	}
	
	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE void WeakStorage< T, Storage, PoolType >::Reset()
	{
		WeakStorage().Swap( *this );
	}
	
	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE void WeakStorage< T, Storage, PoolType >::Swap( WeakStorage & swapWith )
	{
		ParentType::Swap( swapWith );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE Int32 WeakStorage< T, Storage, PoolType >::GetRefCount() const
	{
		return ParentType::GetRefCount();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE Int32 WeakStorage< T, Storage, PoolType >::GetWeakRefCount() const
	{
		return ParentType::GetWeakRefCount();
	}

	template < typename LeftType, typename RightType, typename Storage >
	RED_MEMORY_INLINE bool operator==( const WeakStorage< LeftType, Storage > & left, const WeakStorage< RightType, Storage > & right )
	{
		return left.Get() == right.Get() && left.InternalGetRefCountStorage() == right.InternalGetRefCountStorage();
	}

	template < typename LeftType, typename RightType, typename Storage >
	RED_MEMORY_INLINE bool operator!=( const WeakStorage< LeftType, Storage > & left, const WeakStorage< RightType, Storage > & right )
	{
		return left.Get() != right.Get() || left.InternalGetRefCountStorage() != right.InternalGetRefCountStorage();
	}

}

#endif
