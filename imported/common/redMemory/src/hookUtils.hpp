/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_HOOK_UTILS_HPP_
#define _RED_MEMORY_HOOK_UTILS_HPP_

#include "../include/hookTypes.h"
#include "poolConstant.h"
#include "pool.h"
#include "poolStorage.h"

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
#include "memoryAnalyzerOrbis.h"
#endif

namespace red
{
namespace memory
{
namespace internal
{
	RED_MEMORY_API void ProcessPreHooks( HookPreParameter & param, u32 disabledHooks );
	RED_MEMORY_API void ProcessPostHooks( HookPostParameter & param, u32 disabledHooks);

	template< bool >
	struct ProxyIDResolver
	{
		template< typename T >
		static u32 Resolve(const T &)
		{
			return c_poolNodeInvalid; // ctremblay probably should generate something valid for allocator.
		}
	};

	template< >
	struct ProxyIDResolver< true >
	{
		template< typename T >
		static u32 Resolve(const T & )
		{
			return T::GetHandle();
		}

		static u32 Resolve(const Pool & pool)
		{
			return pool.GetHandle();
		}
	};

	template< typename T >
	constexpr u32 GetProxyId( const T & proxy )
	{
		return ProxyIDResolver< 
			std::is_base_of< Pool, T >::value
		>::Resolve( proxy ); 
	}
}
	template< typename PoolType >
	RED_MEMORY_INLINE HookProxyParameter GenerateProxyParameter()
	{
		typedef typename PoolType::AllocatorType AllocatorType;
		static_assert( ProxyHasTypeId< AllocatorType >::value, "Allocator needs Type Id to be use with hooks." );
		typedef PoolStorageProxy< PoolType > ProxyType;

		HookProxyParameter param = 
		{
			PoolType::GetHandle(),
			AllocatorType::TypeId,
			AddressOf( &ProxyType::GetAllocator() )
		};

		return param;
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE HookProxyParameter GenerateProxyParameter( ProxyType & proxy )
	{
		static_assert( ProxyHasTypeId< ProxyType >::value, "Proxy needs Type Id to be use with hooks." );

		HookProxyParameter param = 
		{
			internal::GetProxyId( proxy ),
			ProxyType::TypeId,
			AddressOf( proxy )
		};

		return param;
	}

#ifdef RED_MEMORY_ENABLE_HOOKS


	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPreAllocateHooks( u32 & size, u32 disabledHooks )
	{
		Block block = NullBlock();
		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter< PoolType >()
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreAllocateHooks( const ProxyType & proxy, u32 & size, u32 disabledHooks )
	{
		Block block = NullBlock();
		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter( proxy )
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPreFreeHooks( Block & block, u32 disabledHooks )
	{
		if( !block.address )
			return;

		typedef PoolStorageProxy< PoolType > ProxyType;
		block.size = ProxyType::GetBlockSize( block.address );
		u32 size = 0;

		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter< PoolType >()
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreFreeHooks( const ProxyType & proxy, Block & block, u32 disabledHooks )
	{
		if( !block.address )
			return;

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		block.size = proxy.GetBlockSize( block.address );
#else
		block.size = internal::AcquireProxy( proxy ).GetBlockSize( block.address );
#endif
		u32 size = 0;

		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter( proxy )
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE  void ProcessPreReallocateHooks( Block & block, u32 & size, u32 disabledHooks )
	{
		if( block.address )
		{
			typedef PoolStorageProxy< PoolType > ProxyType;
			block.size = ProxyType::GetBlockSize( block.address );
		}

		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter< PoolType >()
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreReallocateHooks( const ProxyType & proxy, Block & block, u32 & size, u32 disabledHooks )
	{
		if( block.address )
		{
#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
			block.size = proxy.GetBlockSize( block.address );
#else
			block.size = internal::AcquireProxy( proxy ).GetBlockSize( block.address );
#endif
		}

		HookPreParameter param = 
		{
			&block,
			&size,
			GenerateProxyParameter( proxy )
		};

		internal::ProcessPreHooks( param, disabledHooks );
	}


	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPostAllocateHooks( Block & block, u32 disabledHooks )
	{
		static_assert( !std::is_same< PoolType, Pool >::value, "Provider Pool Type can be bas pool class." );

		Block inputBlock = NullBlock();

		HookPostParameter param = 
		{
			&inputBlock,
			&block,
			GenerateProxyParameter< PoolType >()
		};

		internal::ProcessPostHooks( param, disabledHooks );
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPostAllocateHooks( const ProxyType & proxy, Block & block, u32 disabledHooks )
	{
		Block inputBlock = NullBlock();

		HookPostParameter param = 
		{
			&inputBlock,
			&block,
			GenerateProxyParameter( proxy )
		};

		internal::ProcessPostHooks( param, disabledHooks );
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPostReallocateHooks( Block & inputBlock, Block & outputBlock, u32 disabledHooks )
	{
		static_assert( !std::is_same< PoolType, Pool >::value, "Provider Pool Type can be bas pool class." );
		
		HookPostParameter param = 
		{
			&inputBlock,
			&outputBlock,
			GenerateProxyParameter< PoolType >()
		};

		internal::ProcessPostHooks( param, disabledHooks );
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPostReallocateHooks( const ProxyType & proxy, Block & inputBlock, Block & outputBlock, u32 disabledHooks )
	{
		HookPostParameter param = 
		{
			&inputBlock,
			&outputBlock,
			GenerateProxyParameter( proxy )
		};

		internal::ProcessPostHooks( param, disabledHooks );
	}

#else

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPreAllocateHooks( u32 &, u32 )
	{

	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreAllocateHooks( const ProxyType & , u32 &, u32 )
	{
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPreFreeHooks( Block & block, u32 )
	{
		RED_UNUSED( block );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		MemoryAnalyzerFree( block );
#endif
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreFreeHooks( const ProxyType & , Block & block, u32 )
	{
		RED_UNUSED( block );
#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		MemoryAnalyzerFree( block );
#endif
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPreReallocateHooks( Block & block, u32 &, u32 )
	{
		RED_UNUSED( block );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		if( block.address )
		{
			typedef PoolStorageProxy< PoolType > ProxyType;
			block.size = ProxyType::GetBlockSize( block.address );
			MemoryAnalyzerReallocateBegin( block );
		}
#endif
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPreReallocateHooks( const ProxyType & proxy, Block & block, u32 &, u32 )
	{
		RED_UNUSED( block );
		RED_UNUSED( proxy );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		if( block.address )
		{

			block.size = proxy.GetBlockSize( block.address );
			MemoryAnalyzerReallocateBegin( block );
		}
#endif
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPostAllocateHooks( Block & block, u32 disabledHooks )
	{
		RED_UNUSED( block );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		MemoryAnalyzerAllocate( block, PoolType::GetHandle() );
#endif

#ifdef RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE
		if ( !( disabledHooks & HookType_Memory_Marking ) )
		{
			MemsetBlock( block, 0 );
		}
#endif
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPostAllocateHooks( const ProxyType & proxy, Block & block, u32 disabledHooks )
	{
		RED_UNUSED( block );
		RED_UNUSED( proxy );
#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		MemoryAnalyzerAllocate( block, internal::GetProxyId( proxy ) );
#endif

		
#ifdef RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE
		if ( !( disabledHooks & HookType_Memory_Marking ) )
		{
			MemsetBlock( block, 0 );
		}
#endif
	}

	template< typename PoolType >
	RED_MEMORY_INLINE void ProcessPostReallocateHooks( Block & inputBlock, Block & outputBlock, u32 disabledHooks )
	{
		RED_UNUSED( inputBlock );
		RED_UNUSED( outputBlock );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		if( outputBlock.address )
		{
			if( inputBlock.address )
			{
				MemoryAnalyzerReallocateEnd( outputBlock );
			}
			else
			{
				MemoryAnalyzerAllocate( outputBlock, PoolType::GetHandle() );
			}
		}
		
#endif

#ifdef RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE
		if ( !( disabledHooks & HookType_Memory_Marking ) )
		{
			if ( inputBlock.size < outputBlock.size )
			{
				const u64 size = outputBlock.size - inputBlock.size;
				const u64 address = outputBlock.address + inputBlock.size;
				MemsetBlock( address, 0, size );
			}
		}
#endif
	}

	template< typename ProxyType >
	RED_MEMORY_INLINE void ProcessPostReallocateHooks( const ProxyType & proxy, Block & inputBlock, Block & outputBlock, u32 disabledHooks )
	{
		RED_UNUSED( inputBlock );
		RED_UNUSED( outputBlock );
		RED_UNUSED( proxy );

#ifdef RED_USE_ORBIS_MEMORY_ANALYZER
		if( outputBlock.address )
		{
			if( inputBlock.address )
			{
				MemoryAnalyzerReallocateEnd( outputBlock );
			}
			else
			{
				MemoryAnalyzerAllocate( outputBlock, internal::GetProxyId( proxy ) );
			}
		}

#endif

#ifdef RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE
		if ( !( disabledHooks & HookType_Memory_Marking ) )
		{
			if ( inputBlock.size < outputBlock.size )
			{
				const u64 size = outputBlock.size - inputBlock.size;
				const u64 address = outputBlock.address + inputBlock.size;
				MemsetBlock( address, 0, size );
			}
		}
#endif
	}

#endif
}
}

#endif
