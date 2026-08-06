/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_POOL_UTILS_HPP_
#define _RED_MEMORY_INCLUDE_POOL_UTILS_HPP_

#include "../src/poolStorage.h"

namespace red
{
namespace memory
{
	template< typename PoolType >
	RED_MEMORY_INLINE void InitializePool( const PoolParameter & param, typename PoolType::AllocatorType & allocator )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		RegisterPool( PoolType::GetHandle(), param );
#ifdef RED_MEMORY_ALLOW_DEBUG_ALLOCATOR
		if( ShouldPoolUseDebugAllocator( PoolType::GetHandle() ) )
		{
			ProxyType::EnableDebugAllocator();
		}
#endif
		ProxyType::SetAllocator( allocator );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void SetPoolOOMHandler( PoolOOMHandler * oomHandler )
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		ProxyType::SetOutOfMemoryHandler( oomHandler );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ForceNoDebugAllocator()
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		ProxyType::ForceNoDebugAllocator();
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void DisableContributeToParentMetrics()
	{
		const PoolHandle handle = PoolType::GetHandle();
		DisableContributeToParentMetrics( handle );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void SetMirroredPool()
	{
		const PoolHandle handle = PoolType::GetHandle();
		SetMirroredPool( handle );
	}


	template< typename PoolType >
	RED_MEMORY_INLINE u64 GetPoolBudget()
	{
		const PoolHandle handle = PoolType::GetHandle();
		return GetPoolBudget( handle );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE u64 GetPoolTotalBytesAllocated()
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		return ProxyType::GetTotalBytesAllocated();
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ResetPoolTotalBytesAllocated()
	{
		typedef PoolStorageProxy< PoolType > ProxyType;
		ProxyType::ResetTotalBytesAllocated();
	}

	template< typename PoolType >
	RED_MEMORY_INLINE const char * GetPoolName()
	{
		const PoolHandle handle = PoolType::GetHandle();
		return GetPoolName( handle );
	}

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )

	template <typename T, typename = void>
	struct InternalHasPoolContext 
	{
		template< typename U >
		static typename U::_INTERNAL_RED_POOL_CONTEXT Test( int );
		
		template< typename >
		static void Test( ... );
	
		enum { Value = !std::is_void< decltype( Test< T >( 0 ) )>::value };
	};

	template <typename T, typename = void>
	struct InternalHasPolymorphicPoolContext
	{
		template< typename U >
		static typename U::_INTERNAL_RED_POLYMORPHIC_POOL_CONTEXT Test( int );

		template< typename >
		static void Test( ... );

		enum { Value = !std::is_void< decltype( Test< T >( 0 ) )>::value };
	};

#else 

	template< typename T >
	struct InternalPoolContextMissing
	{
		typedef void Type;
	};

	template< typename T, typename = void >
	struct InternalHasPoolContext
	{
		enum { Value = false };
	};

	template< typename T >
	struct InternalHasPoolContext< T, typename InternalPoolContextMissing< typename T::_INTERNAL_RED_POOL_CONTEXT >::Type >
	{
		enum { Value = true };
	};

	template< typename T, typename = void >
	struct InternalHasPolymorphicPoolContext
	{
		enum { Value = false };
	};

	template< typename T >
	struct InternalHasPolymorphicPoolContext< T, typename InternalPoolContextMissing< typename T::_INTERNAL_RED_POLYMORPHIC_POOL_CONTEXT >::Type >
	{
		enum { Value = true };
	};

#endif

	template< typename Type, typename DefaultPoolType, class Enable >
	struct PoolResolver
	{
		typedef DefaultPoolType PoolType;
	};

	template< typename Type, typename DefaultPoolType >
	struct PoolResolver< Type, DefaultPoolType, typename std::enable_if< InternalHasPoolContext< Type >::Value >::type >
	{
		typedef typename Type::_INTERNAL_RED_POOL_CONTEXT PoolType;
	};

namespace internal
{
	template< typename PoolType >
	void InitializePool(
		const char* poolName,
		red::memory::PoolStorage* storage,
		Uint64 budget,
		red::memory::PoolHandle handle,
		ProxyTypeId proxyId,
		typename PoolType::AllocatorType& allocator,
		void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
		void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) )
	{
		red::memory::PoolParameter param
		{
			poolName,
			storage,
			budget,
			handle
		};

		red::memory::InitializePool< PoolType >( param, allocator );

		red::memory::RegisterAllocatorMetricsProcessor(
			PoolType::GetHandle(),
			proxyId,
			metricsSerializer, 
			metricsDeserializer );
	}
}

}
}


#endif
