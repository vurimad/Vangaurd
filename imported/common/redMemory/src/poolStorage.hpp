/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_POOL_STORAGE_HPP_
#define _RED_MEMORY_POOL_STORAGE_HPP_

#include "defaultAllocator.h"
#include "metricsUtils.h"
#include "../include/oomHandler.h"

#if defined( RED_MEMORY_USE_DEBUG_ALLOCATOR ) || defined( RED_MEMORY_ALLOW_DEBUG_ALLOCATOR )
#	include "debugAllocator.h"
#	define RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
#endif

namespace red
{
namespace memory
{
	class PoolRoot;
	class PoolCPU;
	class PoolGPU;
	class PoolFlexible;
}

	class PoolDefault;
	class PoolLegacyOperator;
	class PoolEngine;
	class PoolRefCount;
	class PoolDebug;
	class PoolBackend;
}

namespace red
{
namespace memory
{
	RED_MEMORY_API u64 MakeAllocatorStorage( void* allocator, const PoolHandle handle );
	RED_MEMORY_API Bool IsDebugAllocatorEnabled( const PoolStorage& storage );

namespace internal
{
	RED_MEMORY_API void PoolDefaultOOMHandler( PoolHandle handle, u32 size, u32 alignment );

	template< typename AllocatorType >
	struct AllocatorInitializer
	{
		RED_MEMORY_INLINE static AllocatorType * Get()
		{
			return nullptr;
		}
	};

	template<>
	struct AllocatorInitializer< DefaultAllocator >
	{
		RED_MEMORY_INLINE static DefaultAllocator * Get()
		{
			return &AcquireDefaultAllocator();
		}
	};

	RED_MEMORY_INLINE void HandleAllocateSucceeded( PoolStorage & storage, const Block & block )
	{
		RED_UNUSED( storage );
		RED_UNUSED( block );

#ifdef RED_MEMORY_ENABLE_REPORT
		i64 value = atomic::ExchangeAdd64( &storage.bytesAllocated, block.size );
		i64 previousValue = storage.maxBytesAllocated;
		while( previousValue < storage.bytesAllocated && atomic::CompareExchange64( &storage.maxBytesAllocated, value, previousValue ) != previousValue )
		{
			previousValue = storage.maxBytesAllocated;
		}
#endif
#ifdef RED_MEMORY_ENABLE_METRICS
		memory::AddAllocateMetric( storage.handle, block );
#endif
	}
	
	RED_MEMORY_INLINE void HandleReallocateSucceeded( PoolStorage & storage, const Block & inputBlock, const Block & outputBlock )
	{
		RED_UNUSED( storage );
		RED_UNUSED( inputBlock );
		RED_UNUSED( outputBlock );

#ifdef RED_MEMORY_ENABLE_REPORT
		const i64 size = outputBlock.size - inputBlock.size;
		i64 value = atomic::ExchangeAdd64( &storage.bytesAllocated, size );
		i64 previousValue = storage.maxBytesAllocated;
		while( previousValue < storage.bytesAllocated && atomic::CompareExchange64( &storage.maxBytesAllocated, value, previousValue ) != previousValue )
		{
			previousValue = storage.maxBytesAllocated;
		}
#endif
#ifdef RED_MEMORY_ENABLE_METRICS
		memory::AddReallocateMetric( storage.handle, inputBlock, outputBlock );
#endif
	}
	
	RED_MEMORY_INLINE void HandleFreeSucceeded( PoolStorage & storage, const Block & block )
	{
		RED_UNUSED( storage );
		RED_UNUSED( block );

#ifdef RED_MEMORY_ENABLE_REPORT
		atomic::ExchangeAdd64( &storage.bytesAllocated, -static_cast< i32 >( block.size ) );
#endif
#ifdef RED_MEMORY_ENABLE_METRICS
		memory::AddFreeMetric( storage.handle, block );
#endif
	}

	static constexpr u64 c_AllocatorPointerValueMask = 0xFFFFffffFFFFfff8ull;
	static constexpr u64 c_AllocatorPointerFlagsMask = ~c_AllocatorPointerValueMask;

	RED_MEMORY_INLINE u64 EncodeAllocatorPointer( void *const pointer )
	{
		const u64 allocatorPointerValue = reinterpret_cast< u64 > ( pointer );
		RED_ASSERT( ( allocatorPointerValue & c_AllocatorPointerFlagsMask ) == 0, "Passed pointer has non zero flags storage." );
		return allocatorPointerValue;
	}

	/** Should this pool use debug allocator? */
	static constexpr u64 c_AllocatorPointerFlag_UsingDebugAllocator = 1 << 0;
	/** Can this pool ever use debug allocator? Some pools are using custom allocator and its usage expects specific behavior of allocator. */
	static constexpr u64 c_AllocatorPointerFlag_NeverUseDebugAllocator = 1 << 1;
	static constexpr u64 c_AllocatorPointerFlag_Unused2 = 1 << 2;
}


	RED_MEMORY_INLINE void *DecodeAllocatorPointer( u64 encodedPointer )
	{
		return reinterpret_cast< void * >( encodedPointer & internal::c_AllocatorPointerValueMask );
	}


	template< typename PoolType >
	struct StaticPoolStorage
	{
		static PoolStorage storage /*RED_STATIC_PRIORITY( 110 )*/;
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolRoot >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 101 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolCPU >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 102 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolGPU >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 102 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolFlexible >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 102 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolDefault >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 102 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolDebug >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 103 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolRefCount >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 103 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolLegacyOperator >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 102 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolEngine >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 105 );
	};

	template<>
	struct RED_MEMORY_API StaticPoolStorage< PoolBackend >
	{
		static PoolStorage storage RED_STATIC_PRIORITY( 106 );
	};

	template< typename PoolType >
	PoolStorage StaticPoolStorage< PoolType >::storage = 
	{
		MakeAllocatorStorage( internal::AllocatorInitializer< typename PoolType::AllocatorType >::Get(), PoolType::GetHandle() ),
		0,
		0,
		nullptr,
		PoolType::GetHandle(),
		PoolType::AllocatorType::TypeId
	};

	template< typename PoolType >
	RED_MEMORY_INLINE Block PoolStorageProxy< PoolType >::Allocate( u32 size )
	{
		using DecayedPoolType = std::decay_t< PoolType >;

		Block block;
#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			block = allocator.Allocate( size );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			block = allocator.Allocate( size );
		}

		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		block.address ? internal::HandleAllocateSucceeded( storage, block ) : HandleAllocateFailure( storage, size, AllocatorType::DefaultAlignmentType::value );
		return block;
	}
	
	template< typename PoolType >
	RED_MEMORY_INLINE Block PoolStorageProxy< PoolType >::AllocateAligned( u32 size, u32 alignment )
	{
		using DecayedPoolType = std::decay_t< PoolType >;

		Block block;
#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			block = allocator.AllocateAligned( size, alignment );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			block = allocator.AllocateAligned( size, alignment );

		}

		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		block.address ? internal::HandleAllocateSucceeded( storage, block ) : HandleAllocateFailure( storage, size, alignment );
		return block;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE Block PoolStorageProxy< PoolType >::Reallocate( Block & inputBlock, u32 size )
	{
		using DecayedPoolType = std::decay_t< PoolType >;

		Block outputBlock;

#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			outputBlock = allocator.Reallocate( inputBlock, size );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			outputBlock = allocator.Reallocate( inputBlock, size );
		}

		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		if( outputBlock.address || !size  )
		{
			internal::HandleReallocateSucceeded( storage, inputBlock, outputBlock );
		}
		else
		{
			HandleAllocateFailure( storage, size, AllocatorType::DefaultAlignmentType::value );
		}

		return outputBlock;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE Block PoolStorageProxy< PoolType >::ReallocateAligned( Block & inputBlock, u32 size, u32 alignment )
	{
		using DecayedPoolType = std::decay_t< PoolType >;

		Block outputBlock;
#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			outputBlock = allocator.ReallocateAligned( inputBlock, size, alignment );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			outputBlock = allocator.ReallocateAligned( inputBlock, size, alignment );
		}

		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		if( outputBlock.address || !size  )
		{
			internal::HandleReallocateSucceeded( storage, inputBlock, outputBlock );
		}
		else
		{
			HandleAllocateFailure( storage, size, alignment );
		}

		return outputBlock;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::Free( Block & block )
	{
		using DecayedPoolType = std::decay_t< PoolType >;

#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			allocator.Free( block );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			allocator.Free( block );
		}

		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		internal::HandleFreeSucceeded( storage, block );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE u64 PoolStorageProxy< PoolType >::GetBlockSize( u64 address )
	{
#ifdef RED_MEMORY_DEBUG_ALLOCATOR_ENABLED
		if( IsUsingDebugAllocator() )
		{
			DebugAllocator & allocator = AcquireDebugAllocator();
			return allocator.GetBlockSize( address );
		}
		else
#endif
		{
			AllocatorType & allocator = GetAllocator();
			return allocator.GetBlockSize( address );
		}
	}

	template< typename PoolType >
	RED_MEMORY_INLINE typename PoolStorageProxy< PoolType >::AllocatorType & PoolStorageProxy< PoolType >::GetAllocator()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		return *static_cast< AllocatorType *>( DecodeAllocatorPointer( storage.allocatorStorage ) );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE Bool PoolStorageProxy< PoolType >::IsUsingDebugAllocator()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		const PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		if( IsDebugAllocatorEnabled( storage ) )
		{
			RED_ASSERT( ( storage.allocatorStorage & internal::c_AllocatorPointerFlag_NeverUseDebugAllocator ) == 0, "The pool is supposed to never use debug allocator, yet it somehow does. Debug." );
			return true;
		}
		return false;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::EnableDebugAllocator()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		if( ( storage.allocatorStorage & internal::c_AllocatorPointerFlag_UsingDebugAllocator ) != 0 )
		{
			return;
		}
		RED_ASSERT( storage.bytesAllocated == 0, "Enabling debug allocator on pool that has already something allocated is unsafe!" );
		storage.allocatorStorage |= internal::c_AllocatorPointerFlag_UsingDebugAllocator;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::ForceNoDebugAllocator()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		RED_ASSERT( storage.bytesAllocated == 0, "Trying to force no debug allocator on pool that has already something allocated is unsafe!" );
		// Make sure debug allocator is not enabled on that pool.
		storage.allocatorStorage &= ~internal::c_AllocatorPointerFlag_UsingDebugAllocator;
		storage.allocatorStorage |= internal::c_AllocatorPointerFlag_NeverUseDebugAllocator;
	}


	template< typename PoolType >
	RED_MEMORY_INLINE u64 PoolStorageProxy< PoolType >::GetFlags()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		const PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		return storage.allocatorStorage & internal::c_AllocatorPointerFlagsMask;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE PoolHandle PoolStorageProxy< PoolType >::GetHandle()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		const PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		return storage.handle;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE u64 PoolStorageProxy< PoolType >::GetTotalBytesAllocated()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		const PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		return storage.bytesAllocated;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::ResetTotalBytesAllocated()
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		atomic::Exchange64( &storage.bytesAllocated, 0 );
		atomic::Exchange64( &storage.maxBytesAllocated, 0 );		
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::SetAllocator( AllocatorType & allocator )
	{
		RED_ASSERT( IsAligned( &allocator, static_cast< u32 >( 8 ) ), "Passed allocator is not 8 byte aligned. Fix it or adjust the code." );
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		// Set allocator and keep flags.
		const u64 flags = GetFlags();
		storage.allocatorStorage = internal::EncodeAllocatorPointer( &allocator ) | flags;
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void PoolStorageProxy< PoolType >::SetOutOfMemoryHandler( PoolOOMHandler * handler )
	{
		using DecayedPoolType = std::decay_t< PoolType >;
		PoolStorage & storage = StaticPoolStorage< DecayedPoolType >::storage;
		storage.oomHandler = handler;
	}
}
}

#endif
