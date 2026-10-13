/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "defaultAllocator.h"
#include "assert.h"
#include "slabHeader.h"
#include "systemAllocator.h"
#include "threadMonitor.h"
#include "tlsfBlock.h"
#include "vault.h"
#include "flags.h"
#include "gpuAllocator.h"
#include "../redSystem/include/unitTestMode.h"

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_CONSOLE
	// On console, we don't want and don't need super huge virtual ranges, since we don't have to support crazy editing scenarios
	// This is espacially good for Orbis, where our own implementation of virtual System Allocator can benefit from smaller bookkeeping memory requirement.
	const u64 c_defaultAllocatorSlabVirtualRange = RED_GIGA_BYTE( 1 );
	const u64 c_defaultAllocatorTLSFVirtualRange = RED_GIGA_BYTE( 2 );
	const u64 c_defaultAllocatorBigSizeVirtualRange = RED_GIGA_BYTE( 8 );
	const u64 c_maxDefaultAllocation = RED_MEGA_BYTE( 45 );
#else
	const u64 c_defaultAllocatorSlabVirtualRange = RED_GIGA_BYTE( 16 );
	const u64 c_defaultAllocatorTLSFVirtualRange = RED_GIGA_BYTE( 64 );
	const u64 c_defaultAllocatorBigSizeVirtualRange = RED_GIGA_BYTE( 32 );
	const u64 c_maxDefaultAllocation = RED_GIGA_BYTE( 8 );
#endif

#ifdef RED_PLATFORM_ORBIS
	// On Orbis, we need the full set of slab chunks, although ...
	const u32 c_defaultAllocatorSlabInitialChunkCount = 1200;
#elif defined( RED_PLATFORM_DURANGO )
	// On Durango we are utilizing magic memory of new SDK - ask Charles for details.
	const u32 c_defaultAllocatorSlabInitialChunkCount = 1200 - 512; // ctremblay: we have chunk coming from XMemGetAuxiliaryTitleMemory only on X1 and X1S
#else
	const u32 c_defaultAllocatorSlabInitialChunkCount = 300;
#endif

	const u32 c_defaultAllocatorTLSFInitialAllocSize =  RED_MEGA_BYTE( 704 );
	const u32 c_defaultAllocatorTLSFChunkSize = RED_MEGA_BYTE( 2 );

#ifdef RED_PLATFORM_CONSOLE
	const u32 c_defaultAllocatorMaxTLSFSize = RED_KILO_BYTE( 512 );
#else
	const u32 c_defaultAllocatorMaxTLSFSize = RED_MEGA_BYTE( 1 );
#endif

	const u32 c_defaultAllocatorVirtualRangeAlignment = 0;

	DefaultAllocator::DefaultAllocator()
		: m_systemAllocator( nullptr )
	{}

	DefaultAllocator::~DefaultAllocator()
	{}

	void DefaultAllocator::Initialize( const DefaultAllocatorParameter & parameter )
	{
		m_systemAllocator = parameter.systemAllocator;

		RED_MEMORY_ASSERT( m_systemAllocator, "DefaultAllocator cannot be initialize. Need access to SytemAllocator." );

		LocklessSlabAllocatorParameter slabParameter =
		{
			c_defaultAllocatorSlabVirtualRange,
			!UnitTestMode() ? c_defaultAllocatorSlabInitialChunkCount : 10,
			Flags_CPU_Read_Write | Flags_Skip_System_OOM,
			m_systemAllocator,
			parameter.threadMonitor
		};

		m_slabAllocator.Initialize( slabParameter );

		DynamicTLSFAllocatorParameter tlsfParameter = 
		{
			m_systemAllocator,
			c_defaultAllocatorTLSFVirtualRange,
			!UnitTestMode() ? c_defaultAllocatorTLSFInitialAllocSize : RED_MEGA_BYTE( 100 ),
			c_defaultAllocatorTLSFChunkSize,
			Flags_CPU_Read_Write | Flags_Skip_System_OOM,
			c_defaultAllocatorVirtualRangeAlignment
		};

		m_tlsfAllocator.Initialize( tlsfParameter );
	
		BigSizeAllocatorParameter bsParameter =
		{
			m_systemAllocator,
			c_defaultAllocatorBigSizeVirtualRange,
			Flags_CPU_Read_Write | Flags_Skip_System_OOM
		};
		m_bigSizeAllocator.Initialize( bsParameter );
	}

	void DefaultAllocator::Uninitialize()
	{
		m_bigSizeAllocator.Uninitialize();
		m_tlsfAllocator.Uninitialize();
		m_slabAllocator.Uninitialize();
	}

	Block DefaultAllocator::Allocate( u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "DefaultAllocator was not initialized." );
		RED_MEMORY_ASSERT( static_cast< u64 >( size ) <= c_maxDefaultAllocation, "Allocation too big for Default Allocator." );
		Block block = NullBlock();
		
		if ( static_cast< u64 >( size ) > c_maxDefaultAllocation )
		{
			return block;
		}

		size = std::max( size, 1u );

		if( size <= c_slabMaxAllocSize && m_slabAllocator.IsCurrentThreadRegistered() )
		{
			block = m_slabAllocator.Allocate( size );
		}

		if( !block.address && size <= c_defaultAllocatorMaxTLSFSize )
		{
			block = m_tlsfAllocator.Allocate( size );
		}

		if( !block.address )
		{
			block = m_bigSizeAllocator.Allocate( size );
		}
		
		// We failed to allocate even from Big Size. We are out of memory.
		// However ... There is a slight chance block of enough size exists in TLSF.
		if( !block.address )
		{
			block = m_tlsfAllocator.Allocate( size );
		}

		return block;
	}

	Block DefaultAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "DefaultAllocator was not initialized." );
		RED_MEMORY_ASSERT( static_cast< u64 >( size ) <= c_maxDefaultAllocation, "Allocation too big for Default Allocator." );
		Block block = NullBlock(); 

		if ( static_cast< u64 >( size ) > c_maxDefaultAllocation )
		{
			return block;
		}

		size = std::max( size, 1u );

		if( size <= c_slabMaxAllocSize && ( alignment <= 16 || ( size == 64 && alignment == 64 ) ) && m_slabAllocator.IsCurrentThreadRegistered() )
		{
			block = m_slabAllocator.AllocateAligned( size, alignment );
		}

		if( !block.address && size <= c_defaultAllocatorMaxTLSFSize )
		{
			block = m_tlsfAllocator.AllocateAligned( size, alignment );
		}

		if( !block.address )
		{
			block = m_bigSizeAllocator.AllocateAligned( size, alignment );
		}

		// We failed to allocate even from Big Size. We are out of memory.
		// However ... There is a slight chance block of enough size exists in TLSF.
		if( !block.address )
		{
			block = m_tlsfAllocator.AllocateAligned( size, alignment );
		}

		return block;
	}

	Block DefaultAllocator::Reallocate( Block & block, u32 size )
	{
		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return Allocate( size );
		}

		RED_MEMORY_ASSERT( OwnBlock( block.address ), "DefaultAllocator do not own memory block." );

		if( m_slabAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromSlabAllocator( block, size );
		}
		else if( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromTLSFAllocator( block, size );
		}
		else
		{
			return ReallocateFromBigSizeAllocator( block, size );
		}
	}

	Block DefaultAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsAligned( block.address, alignment ), "Cannot Reallocate a block on different alignment." );

		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		RED_MEMORY_ASSERT( OwnBlock( block.address ), "DefaultAllocator do not own memory block." );

		if( m_slabAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromSlabAllocator( block, size, alignment );
		}
		else if( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromTLSFAllocator( block, size, alignment );
		}
		else
		{
			return ReallocateFromBigSizeAllocator( block, size, alignment );
		}
	}

	Block DefaultAllocator::ReallocateFromSlabAllocator( Block & block, u32 size )
	{
		Block newBlock = NullBlock();

		if( size <= c_slabMaxAllocSize && m_slabAllocator.IsCurrentThreadRegistered() )
		{
			newBlock = m_slabAllocator.Reallocate( block, size );
		}

		if( !newBlock.address )
		{	
			if( size <= c_defaultAllocatorMaxTLSFSize )
			{
				newBlock = m_tlsfAllocator.Allocate( size );
			}

			if( !newBlock.address )
			{
				newBlock = m_bigSizeAllocator.Allocate( size );
			}

			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, GetSlabBlockSize( block.address ) );
				m_slabAllocator.Free( block );
			}
		}

		return newBlock;
	}

	Block DefaultAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size )
	{
		Block newBlock = NullBlock();

		if( size <= c_slabMaxAllocSize || size > c_defaultAllocatorMaxTLSFSize )
		{
			newBlock = Allocate( size );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}
		else
		{
			newBlock = m_tlsfAllocator.Reallocate( block, size );
		}

		if( !newBlock.address )
		{
			newBlock = Allocate( size );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}

		return newBlock;
	}

	Block DefaultAllocator::ReallocateFromBigSizeAllocator( Block & block, u32 size )
	{
		Block newBlock = NullBlock();

		if( size > c_defaultAllocatorMaxTLSFSize )
		{
			newBlock = m_bigSizeAllocator.Reallocate( block, size );
		}

		if( !newBlock.address )
		{
			newBlock = Allocate( size );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( newBlock.size, block.size ) );
				m_bigSizeAllocator.Free( block );
			}
		}

		return newBlock;
	}

	Block DefaultAllocator::ReallocateFromSlabAllocator( Block & block, u32 size, u32 alignment )
	{
		Block newBlock = NullBlock();

		if( size <= c_slabMaxAllocSize && m_slabAllocator.IsCurrentThreadRegistered() )
		{
			newBlock = m_slabAllocator.ReallocateAligned( block, size, alignment );
		}

		if( !newBlock.address )
		{
			if( size <= c_defaultAllocatorMaxTLSFSize )
			{
				newBlock = m_tlsfAllocator.AllocateAligned( size, alignment );
			}

			if( !newBlock.address )
			{
				newBlock = m_bigSizeAllocator.AllocateAligned( size, alignment );
			}

			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, GetSlabBlockSize( block.address ) );
				m_slabAllocator.Free( block );
			}
		}

		return newBlock;
	}

	Block DefaultAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size, u32 alignment )
	{
		Block newBlock = NullBlock();

		if( size <= c_slabMaxAllocSize )
		{
			newBlock = AllocateAligned( size, alignment );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}
		else
		{
			newBlock = m_tlsfAllocator.ReallocateAligned( block, size, alignment );
		}

		if( !newBlock.address )
		{
			newBlock = AllocateAligned( size, alignment );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}

		return newBlock;
	}

	Block DefaultAllocator::ReallocateFromBigSizeAllocator( Block & block, u32 size, u32 alignment )
	{
		Block newBlock = NullBlock();

		if( size > c_defaultAllocatorMaxTLSFSize )
		{
			newBlock = m_bigSizeAllocator.ReallocateAligned( block, size, alignment );
		}

		if( !newBlock.address )
		{
			newBlock = AllocateAligned( size, alignment );
			if( newBlock.address )
			{
				MemcpyBlock( newBlock.address, block.address, std::min( newBlock.size, block.size ) );
				m_bigSizeAllocator.Free( block );
			}
		}

		return newBlock;
	}

	void DefaultAllocator::Free( Block & block )
	{
		RED_MEMORY_ASSERT( !block.address || OwnBlock( block.address ), "Memory Block is not own by DefaultAllocator." );
		if( m_slabAllocator.OwnBlock( block.address ) )
		{
			m_slabAllocator.Free( block );
		}
		else if( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			m_tlsfAllocator.Free( block );
		}
		else
		{
			m_bigSizeAllocator.Free( block );
		}
	}

	void DefaultAllocator::RegisterCurrentThread( const AnsiChar* threadName )
	{
		m_slabAllocator.RegisterCurrentThread( threadName );
	}

	bool DefaultAllocator::OwnBlock( u64 block ) const
	{
		return m_slabAllocator.OwnBlock( block ) || m_tlsfAllocator.OwnBlock( block ) || m_bigSizeAllocator.OwnBlock( block );
	}

	u64 DefaultAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not owned by this allocator." );

		if( m_slabAllocator.OwnBlock( address ) )
		{
			return GetSlabBlockSize( address );
		}
		else if( m_tlsfAllocator.OwnBlock( address ) )
		{
			return GetTLSFBlockSize( address );
		}
		else
		{
			return m_bigSizeAllocator.GetBlockSize( address );
		}
	}

	u64 DefaultAllocator::GetMaxAllocationSize()
	{
		return c_maxDefaultAllocation;
	}

	void DefaultAllocator::BuildMetrics( DefaultAllocatorMetrics & metrics, Bool virtualAllocatorOnly )
	{
		RED_TOUCH( virtualAllocatorOnly );
		BigSizeAllocatorMetrics & bigSizeAllocatorMetrics = metrics.bigSizeAllocatorMetrics;
		AllocatorMetrics & defaultMetric = metrics.metrics;
		LocklessSlabAllocatorMetrics & slabMetric = metrics.locklessSlabAllocatorMetrics;
		TLSFAllocatorMetrics & tlsfMetric = metrics.tlsfAllocatorMetrics;

		m_slabAllocator.BuildMetrics( slabMetric );
		m_tlsfAllocator.BuildMetrics( metrics.tlsfAllocatorMetrics );
		m_bigSizeAllocator.BuildMetrics( bigSizeAllocatorMetrics );

		defaultMetric.consumedSystemMemoryBytes = slabMetric.metrics.consumedSystemMemoryBytes + tlsfMetric.metrics.consumedSystemMemoryBytes + bigSizeAllocatorMetrics.metrics.consumedSystemMemoryBytes;
		defaultMetric.consumedMemoryBytes = slabMetric.metrics.consumedMemoryBytes + tlsfMetric.metrics.consumedMemoryBytes + bigSizeAllocatorMetrics.metrics.consumedMemoryBytes;
		defaultMetric.bookKeepingBytes = slabMetric.metrics.bookKeepingBytes + tlsfMetric.metrics.bookKeepingBytes + bigSizeAllocatorMetrics.metrics.bookKeepingBytes;
	}

	void DefaultAllocator::SerializeMetrics( Serializer & serializer )
	{
		DefaultAllocatorMetrics metrics;
		Memzero( &metrics, sizeof( metrics ) );
		BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );
		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	bool DefaultAllocator::IsInitialized() const
	{
		return m_systemAllocator != nullptr;
	}

	bool DefaultAllocator::InternalSlabAllocatorOwnBlock( const Block & block ) const
	{
		return m_slabAllocator.OwnBlock( block.address );
	}

	bool DefaultAllocator::InternalTLSFAllocatorOwnBlock( const Block & block ) const
	{
		return m_tlsfAllocator.OwnBlock( block.address );
	}
	
	bool DefaultAllocator::InternalBigSizeAllocatorOwnBlock( const Block & block ) const
	{
		return m_bigSizeAllocator.OwnBlock( block.address );
	}

	void DefaultAllocator::InternalMarkSlabAllocatorAsFull()
	{
		// No local slab allocator available is same behavior. I.E. it will return nullptr
		m_slabAllocator.InternalMarkAllLocalSlabAllocatorAsTaken();
	}
	
	void DefaultAllocator::InternalMarkTLSFAllocatorAsFull()
	{
		m_tlsfAllocator.InternalMarkLockingDynamicTLSFAllocatorAsFull();
	}

	void DefaultAllocator::InternalMarkBigSizeAllocatorAsFull()
	{
		m_bigSizeAllocator.InternalMarkBigSizeAllocatorAsFull();
	}

	u32 DefaultAllocator::InternalGetBigSizeAllocatorPageSize()
	{
		return m_bigSizeAllocator.GetPageSize();
	}

	LocklessSlabAllocator& DefaultAllocator::InternalAcquireLocklessSlabAllocator()
	{
		return m_slabAllocator;
	}

	BigSizeAllocator& DefaultAllocator::InternalAcquireBigSizeAllocator()
	{
		return m_bigSizeAllocator;
	}

	DefaultAllocator& AcquireDefaultAllocator()
	{
		return AcquireVault().GetDefaultAllocator();
	}
}
}
