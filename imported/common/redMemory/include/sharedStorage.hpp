/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_STORAGE_HPP_
#define _RED_MEMORY_SHARED_STORAGE_HPP_

namespace red
{
namespace memory
{
namespace internal
{
	struct CreateSharedPtrFromPool
	{
		template< typename T, typename Storage, typename PoolType, typename... Args >
		static SharedStorage< T, Storage, PoolType > Create( Args && ... args )
		{
			SharedStorage< T, Storage, PoolType > ptr( (RED_NEW( T, PoolType )(std::forward< Args >( args )...)) );
			return ptr;
		}
	};

	struct CreateSharedPtrDefault
	{
		template< typename T, typename Storage, typename PoolType, typename... Args >
		static SharedStorage< T, Storage, PoolType > Create( Args && ... args )
		{
			SharedStorage< T, Storage, PoolType > ptr( (RED_NEW( T )(std::forward< Args >( args )...)) );
			return ptr;
		}
	};
}
}

	RED_MEMORY_INLINE void InternalEnableShared(const volatile void *, const volatile void *)
	{}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage()
	{}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( std::nullptr_t )
	{}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( U * pointer )
		:	ParentType( pointer )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
		InternalEnableShared( pointer, this );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( const SharedStorage & copyFrom )
		:	ParentType( copyFrom )
	{
		ParentType::template AddRef< T >();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( SharedStorage && rvalue )
		:	ParentType( std::forward< SharedStorage >( rvalue ) )
	{}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( const SharedStorage< U, Storage, PoolType > & copyFrom )
		:	ParentType( copyFrom )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
		ParentType::template AddRef< T >();
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( SharedStorage< U, Storage, PoolType > && rvalue )
		:	ParentType( std::forward< SharedStorage< U, Storage, PoolType > >( rvalue ) )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( const WeakStorage< U, Storage, PoolType > & copyFrom )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
		ParentType::UpgradeFromWeakToStrong( const_cast< WeakStorage< U, Storage, PoolType > & >( copyFrom ) );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U, typename P,
		typename std::enable_if<
			std::is_same< P, void >::value
		>::type* >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( UniquePtr< U > && rvalue )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
		Reset( rvalue.ReleaseOwnership() );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U, typename P,
		typename std::enable_if<
			!std::is_same< P, void >::value
		>::type* >
		RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::SharedStorage( UniquePtr< U, PoolType > && rvalue )
	{
		static_assert( std::is_base_of< T, U >::value || std::is_same< T, U >::value, "SharedStorage can't be constructed from non related type." );
		Reset( rvalue.ReleaseOwnership() );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::~SharedStorage()
	{
		if( ParentType::template Release< T >() )
		{
			Destroy();
		}
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > & SharedStorage< T, Storage, PoolType >::operator=( const SharedStorage & copyFrom )
	{
		SharedStorage( copyFrom ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > & SharedStorage< T, Storage, PoolType >::operator=( SharedStorage && rvalue )
	{
		SharedStorage( std::move( rvalue ) ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > & SharedStorage< T, Storage, PoolType >::operator=( const SharedStorage< U, Storage, PoolType >  & copyFrom )
	{
		SharedStorage( copyFrom ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > & SharedStorage< T, Storage, PoolType >::operator=( SharedStorage< U, Storage, PoolType > && rvalue )
	{
		SharedStorage( std::move( rvalue ) ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > & SharedStorage< T, Storage, PoolType >::operator=( UniquePtr< U > && rvalue )
	{
		static_assert( std::is_same< PoolType, void >::value, "Memory Pool Mismatch." );
		static_assert( std::is_base_of< T, U >::value, "SharedStorage can't be constructed from non related type." );
		SharedStorage( std::move( rvalue ) ).Swap( *this );
		return *this;
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE typename SharedStorage< T, Storage, PoolType >::PtrType SharedStorage< T, Storage, PoolType >::Get() const
	{
		return static_cast< PtrType >( ParentType::Get() );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE typename SharedStorage< T, Storage, PoolType >::PtrType SharedStorage< T, Storage, PoolType >::operator->() const
	{
		RED_MEMORY_ASSERT( ParentType::Get(), "null pointer access is illegal." );
		return Get();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE typename SharedStorage< T, Storage, PoolType >::RefType SharedStorage< T, Storage, PoolType >::operator*() const
	{
		RED_MEMORY_ASSERT( ParentType::Get(), "null pointer access is illegal." );
		return *Get();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Reset()
	{
		SharedStorage().Swap( *this );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Reset( PtrType pointer )
	{
		SharedStorage( pointer ).Swap( *this );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Reset( U * pointer )
	{
		SharedStorage( pointer ).Swap( *this );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Swap( SharedStorage & swapWith )
	{
		ParentType::Swap( swapWith );
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE Int32 SharedStorage< T, Storage, PoolType >::GetRefCount() const
	{
		return ParentType::GetRefCount();
	}
	
	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE Int32 SharedStorage< T, Storage, PoolType >::GetWeakRefCount() const
	{
		return ParentType::GetWeakRefCount();
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType >::operator Bool() const
	{
		return ParentType::Get() != nullptr;
	}

	template< typename T, typename Storage, typename PoolType >
	RED_MEMORY_INLINE Bool SharedStorage< T, Storage, PoolType >::operator!() const
	{
		return !ParentType::Get();
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename... Args  >
	RED_MEMORY_INLINE SharedStorage< T, Storage, PoolType > SharedStorage< T, Storage, PoolType >::Create( Args && ... args )
	{
		typedef typename std::conditional<
			std::is_base_of< red::memory::Pool, PoolType >::value,
			memory::internal::CreateSharedPtrFromPool,
			memory::internal::CreateSharedPtrDefault
		>::type Creator;

		return Creator::template Create< T, Storage, PoolType >( std::forward< Args >( args )... );
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Destroy( typename std::enable_if< std::is_same< U, void >::value, U >::type* )
	{
		ParentType::template Destroy< T >();
	}

	template< typename T, typename Storage, typename PoolType >
	template< typename U >
	RED_MEMORY_INLINE void SharedStorage< T, Storage, PoolType >::Destroy( typename std::enable_if< std::is_base_of< red::memory::Pool, U >::value, U >::type* )
	{
		ParentType::template Destroy< T, PoolType >();
	}

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	RED_MEMORY_INLINE bool operator==( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr )
	{
		return leftPtr.Get() == rightPtr.Get();
	}

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	RED_MEMORY_INLINE bool operator!=( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr )
	{
		return leftPtr.Get() != rightPtr.Get();
	}

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	RED_MEMORY_INLINE bool operator<( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr )
	{
		return leftPtr.Get() < rightPtr.Get();
	}

	template< typename LeftType, typename LeftStorage, typename LeftPool >
	RED_MEMORY_INLINE bool operator==( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, std::nullptr_t )
	{
		return leftPtr.Get() == nullptr;
	}

	template< typename LeftType, typename LeftStorage, typename LeftPool >
	RED_MEMORY_INLINE bool operator!=( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, std::nullptr_t )
	{
		return leftPtr.Get() != nullptr;
	}

	template< typename RightType, typename RightStorage, typename RightPool >
	RED_MEMORY_INLINE bool operator==( std::nullptr_t, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr )
	{
		return rightPtr.Get() == nullptr;
	}

	template< typename RightType, typename RightStorage, typename RightPool >
	RED_MEMORY_INLINE bool operator!=( std::nullptr_t, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr )
	{
		return rightPtr.Get() != nullptr;
	}
}

#endif 
