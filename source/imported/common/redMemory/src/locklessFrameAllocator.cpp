/**
 * Copyright (c) 2017-18 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "locklessFrameAllocator.h"
#include "assert.h"
#include "../include/systemAllocator.h"
#include "../../redSystem/include/unitTestMode.h"

namespace red
{
namespace memory
{

namespace
{
	void MarkAllocatedBlock( const Block & block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if( UnitTestMode() )
		{
			MemsetBlock( block, c_frameAllocatorUnitTestAllocFiller );
		}
#endif

		RED_UNUSED( block );
	}

	void MarkFreeBlock( const Block & block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if( UnitTestMode() )
		{
			MemsetBlock( block, c_frameAllocatorUnitTestFreeFiller );
		}
#endif

		RED_UNUSED( block );
	}
}

	static_assert( __alignof( LocklessFrameAllocator ) == 64, "LocklessFrameAllocator is incorrectly aligned." );
	static_assert( sizeof( LocklessFrameAllocator ) <= 64, "LocklessFrameAllocator should fit in one cache line." );

	static_assert( c_frameAllocatorHeaderAlignment == LocklessFrameAllocator::DefaultAlignmentType::value, "LocklessFrameAllocator Header alignment must be equal than allocator default alignment" );

	LocklessFrameAllocator::LocklessFrameAllocator()
		: m_virtualRange( NullVirtualRange() )
		, m_nextFrameBlockAddress( 0 )
		, m_systemAllocator( nullptr )
#if defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )
		, m_firstCommitedFrameBlockAddress( 0 )
		, m_numberOfCommitedFrameBlocks( 0 )
#endif
		, m_frameBlockSize( 0 )
		, m_numberOfFrames( 0 )
		, m_flags( 0 )
	{
#if !defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )
		RED_TOUCH( m_padding );
#endif
	}
	
	LocklessFrameAllocator::~LocklessFrameAllocator()
	{}

	void LocklessFrameAllocator::Initialize( const FrameAllocatorParameter & parameter )
	{
		ValidateInitializationParameter( parameter );

		m_systemAllocator = parameter.systemAllocator;
		m_frameBlockSize = parameter.frameBlockSize;
		m_flags = parameter.flags;
		m_numberOfFrames = parameter.numberOfFrames;

#if defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )

		// reserving one page more for a memory trap
		const u32 bufferSize = RoundUp( ( m_numberOfFrames + 1 ) * m_frameBlockSize, static_cast< u32 >( m_systemAllocator->GetPageSize() ) );
		m_virtualRange = m_systemAllocator->ReserveVirtualRange( bufferSize, m_flags );

		// commit memory for one frame
		const SystemBlock commitBlock = { m_virtualRange.start, m_frameBlockSize };
		const SystemBlock commitedBlock = m_systemAllocator->Commit( commitBlock, m_flags );
		RED_MEMORY_ASSERT( commitBlock != NullSystemBlock(), "Failed to commit memory" );
		RED_MEMORY_ASSERT( commitBlock == commitedBlock, "Commited block does not match with requested one" );

		m_firstCommitedFrameBlockAddress = commitedBlock.address;
		m_numberOfCommitedFrameBlocks = 1;
#else
		// commit memory for all frames
		const u32 bufferSize = RoundUp( m_numberOfFrames * m_frameBlockSize, static_cast< u32 >( m_systemAllocator->GetPageSize() ) );
		m_virtualRange = m_systemAllocator->ReserveVirtualRange( bufferSize, m_flags );
		const SystemBlock commitBlock = { m_virtualRange.start, bufferSize };
		const SystemBlock commitedBlock = m_systemAllocator->Commit( commitBlock, m_flags );
		RED_MEMORY_ASSERT( commitBlock != NullSystemBlock(), "Failed to commit memory" );
		RED_MEMORY_ASSERT( commitBlock == commitedBlock, "Commited block does not match with requested one" );
		RED_MEMORY_ASSERT( commitedBlock.address + commitedBlock.size == m_virtualRange.end, "Commited block does not match with requested one" );
#endif

		m_nextFrameBlockAddress = commitedBlock.address + m_frameBlockSize;
		m_currentPositionInFrameBlock.SetValue( commitedBlock.address );
	}

	void LocklessFrameAllocator::Uninitialize()
	{
		if ( m_virtualRange != NullVirtualRange() )
		{
			m_systemAllocator->ReleaseVirtualRange( m_virtualRange );
			m_virtualRange = NullVirtualRange();
		}
	}

	Block LocklessFrameAllocator::Allocate( u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "Allocator is not initialized." );

		size = RoundUp( std::max( 1u, size ), LocklessFrameAllocator::DefaultAlignmentType::value );
		const u64 sizeWithHeader = size + c_frameAllocatorHeaderSize;
		
		u64 address = m_currentPositionInFrameBlock.ExchangeAdd( sizeWithHeader );
		if( address + sizeWithHeader < m_nextFrameBlockAddress )
		{
			FrameAllocatorHeader * header = reinterpret_cast< FrameAllocatorHeader* >( address );
			header->size = size;
			header->marker = c_frameAllocatorHeaderAllocateMarker;

			const Block block = { address + c_frameAllocatorHeaderSize, size };
			MarkAllocatedBlock( block );
			return block;
		}
		else
		{ /* address reserves is out of bound. We are OOM. */ }
	
		return NullBlock();
	}
	
	Block LocklessFrameAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		if( alignment > LocklessFrameAllocator::DefaultAlignmentType::value )
		{
			RED_MEMORY_ASSERT( IsInitialized(), "Allocator is not initialized." );
			RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Alignment must be power of two." );
			RED_MEMORY_ASSERT( alignment >  LocklessFrameAllocator::DefaultAlignmentType::value, "Alignement request is smaller than default one, 8. Call Allocate instead." );

			size = RoundUp( std::max( 1u, size ), alignment );
			// ctremblay: if this function is called, alignment requirement is guarantied to be bigger than default alignment
			// Allocate enough to handle worst case scenario: rounding up address to next alignment boundaries. 
			// Header size is same than min alignment, so it should always fit in.
			u32 sizeIncludingAlignmentPad = size + alignment;
			u64 address = m_currentPositionInFrameBlock.ExchangeAdd( sizeIncludingAlignmentPad );
			if( address + sizeIncludingAlignmentPad < m_nextFrameBlockAddress )
			{
				address = AlignAddress( address + c_frameAllocatorHeaderSize,  alignment );
				FrameAllocatorHeader * header = reinterpret_cast< FrameAllocatorHeader* >( address - c_frameAllocatorHeaderSize );
				header->size = size;
				header->marker = c_frameAllocatorHeaderAllocateMarker;

				const Block block = { address, size };
				MarkAllocatedBlock( block );
				return block;
			}
			else
			{ /* address reserves is out of bound. We are OOM. */ }

			return NullBlock();
		}

		return Allocate( size );
	}
	
	Block LocklessFrameAllocator::Reallocate( Block & block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}
	
	Block LocklessFrameAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return alignment <= DefaultAlignmentType::value ? Allocate( size ) : AllocateAligned( size, alignment );
		}

		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, alignment );
		block.size = GetBlockSize( block.address );

		RED_MEMORY_ASSERT( IsAligned( block.address, alignment ), "ReallocateAligned do not support changing original block alignment." );

		if( size == block.size )
		{
			return block;
		}
		else if( size < block.size )
		{
			return block;
		}
		else
		{
			Block grownBlock = GrowBlock( block, size ); 

			if( grownBlock != NullBlock() )
			{
				return grownBlock;	
			}
			else
			{
				// GrowBlock failed, go through regular allocate
				Block allocBlock = alignment <= DefaultAlignmentType::value ? Allocate( size ) : AllocateAligned( size, alignment );
				if( allocBlock != NullBlock() )
				{
					MemcpyBlock( allocBlock.address, block.address, block.size );
					Free( block );
					return allocBlock;
				}
			}
		}

		// Everything failed. Most likely OOM.
		return NullBlock();
	}

	Block LocklessFrameAllocator::GrowBlock( const Block & block, u32 size )
	{
		// ctremblay: Why not going through same path than Allocate for Reallocate and simply just not support contiguous block ?
		// Simply put, this allocator can be use with containers like DynArray, HashMap etc... So you can get: PushBack, PushBack, PushBack with no reserve. 
		// I will try at least to return same block, and bypass overhead of moving memory.
		while( 1 )
		{
			u64 position = m_currentPositionInFrameBlock.GetValue();
			
			if( position == block.address + block.size )
			{
				// current position is contiguous to block address. Try to grow block
				const u64 newPosition = ( block.address + size );

				if ( newPosition > m_virtualRange.end )
				{
					// OOM, cannot grow! 
					return NullBlock();
				}

				if( m_currentPositionInFrameBlock.CompareExchange( newPosition, position ) == position )
				{
					// Grow succeeded !
					FrameAllocatorHeader * header = reinterpret_cast< FrameAllocatorHeader* >( block.address - c_frameAllocatorHeaderSize );
					header->size = size;
					return { block.address, size };
				}
				else
				{ /* Grow failed. Try again. */ }
			}
			else
			{
				// Cannot Grow block. exit loop
				break;
			}
		}

		return NullBlock();
	}
	
	void LocklessFrameAllocator::Free( Block & block )
	{
		if( block.address )
		{
			RED_MEMORY_ASSERT( OwnBlock( block.address ), "Allocator do not own this block." );
			FrameAllocatorHeader * header =  reinterpret_cast< FrameAllocatorHeader * >( block.address - c_frameAllocatorHeaderSize );
			RED_MEMORY_ASSERT( header->marker == c_frameAllocatorHeaderAllocateMarker, "Block was already deleted, or underrun occured." );
			block.size = header->size;
			header->marker = c_frameAllocatorHeaderFreeMarker;
			MarkFreeBlock( block );

			const u64 position = m_currentPositionInFrameBlock.GetValue();
			if ( position == block.address + block.size )
			{
				// freeing in LIFO order, try to move back the current position
				m_currentPositionInFrameBlock.CompareExchange( block.address - c_frameAllocatorHeaderSize, position );
			}
		}
	}

	bool LocklessFrameAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( IsInitialized(), "LocklessFrameAllocator was not initialized." );
		return block - m_virtualRange.start < GetVirtualRangeSize( m_virtualRange ); 
	}
	
	u64 LocklessFrameAllocator::GetBlockSize( u64 address ) const
	{
		FrameAllocatorHeader * header =  reinterpret_cast< FrameAllocatorHeader * >( address - c_frameAllocatorHeaderSize );
		RED_MEMORY_ASSERT( header->marker == c_frameAllocatorHeaderAllocateMarker, "Block was already deleted, or underrun occured." );	
		return header->size;
	}

	void LocklessFrameAllocator::Reset()
	{
#if defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )

		// ctremblay: Reset will decommit range when it is called >= times than m_numberOfFrames.
		// Windows seems to always return same address range after freeing virtual memory (except when gflags is on). 
		// That's why I'm Committing memory to a different range every Reset manually instead of a full release of virtual memory.
		// Else we won't get memory access exception if something illegal happen.
		// See unit tests for proof. I'm testing it crashing if any memory access are made after reset. Tests will fail if this code is remove. 

		if ( m_numberOfFrames == m_numberOfCommitedFrameBlocks )
		{
			const SystemBlock block = { m_firstCommitedFrameBlockAddress, m_frameBlockSize };
			m_systemAllocator->Decommit( block );
			m_firstCommitedFrameBlockAddress = m_firstCommitedFrameBlockAddress + m_frameBlockSize >= m_virtualRange.end ? m_virtualRange.start : m_firstCommitedFrameBlockAddress + m_frameBlockSize;
			RED_MEMORY_ASSERT( m_firstCommitedFrameBlockAddress >= m_virtualRange.start && m_firstCommitedFrameBlockAddress <= m_virtualRange.end, "First commited frame block address is out of virtual range" );
		}
		else
		{
			++m_numberOfCommitedFrameBlocks;
		}

		const u64 commitAddress = m_nextFrameBlockAddress + m_frameBlockSize > m_virtualRange.end ? m_virtualRange.start : m_nextFrameBlockAddress;
		const SystemBlock commitBlock = { commitAddress, m_frameBlockSize };
		const SystemBlock commitedBlock = m_systemAllocator->Commit( commitBlock, m_flags );
		RED_MEMORY_ASSERT( commitBlock != NullSystemBlock(), "Failed to commit memory" );
		RED_MEMORY_ASSERT( commitBlock == commitedBlock, "Commited block does not match with requested one" );

		m_currentPositionInFrameBlock.SetValue( commitedBlock.address );
		m_nextFrameBlockAddress = commitBlock.address + m_frameBlockSize;
#else
		const u64 newAddress = m_nextFrameBlockAddress >= m_virtualRange.end ? m_virtualRange.start : m_nextFrameBlockAddress;
		m_currentPositionInFrameBlock.SetValue( newAddress );
		m_nextFrameBlockAddress = newAddress + m_frameBlockSize;
#endif
	}

	void LocklessFrameAllocator::BuildMetrics( FrameAllocatorMetrics& metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		metrics.metrics.bookKeepingBytes = c_frameAllocatorHeaderSize;

		if ( m_systemAllocator )
		{
#if defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )
			const u64 frameBlockSize = RoundUp( static_cast< u64 >( m_frameBlockSize ), m_systemAllocator->GetPageSize() );
			metrics.metrics.consumedSystemMemoryBytes = m_numberOfCommitedFrameBlocks * frameBlockSize;
#else
			metrics.metrics.consumedSystemMemoryBytes = GetVirtualRangeSize( m_virtualRange );
#endif
			const u64 nextFrameBlockAddress = m_nextFrameBlockAddress;
			const u64 currentPositionInFrameBlock = m_currentPositionInFrameBlock.GetValue();

			if ( nextFrameBlockAddress >= m_virtualRange.end )
			{
				metrics.freeBlockSize = m_virtualRange.end > currentPositionInFrameBlock ? m_virtualRange.end - currentPositionInFrameBlock : 0;
			}
			else
			{
				metrics.freeBlockSize = nextFrameBlockAddress > currentPositionInFrameBlock ? nextFrameBlockAddress - currentPositionInFrameBlock : 0;
			}

			metrics.metrics.consumedMemoryBytes = metrics.metrics.consumedSystemMemoryBytes - metrics.freeBlockSize;
		}
	}

	void LocklessFrameAllocator::SerializeMetrics( Serializer& serializer )
	{
		FrameAllocatorMetrics metrics;
		BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	bool LocklessFrameAllocator::IsInitialized() const
	{
		return m_systemAllocator != nullptr;
	}

	u32 LocklessFrameAllocator::Internal_GetNumberOfFrames() const
	{
		return m_numberOfFrames;
	}

	u64 LocklessFrameAllocator::Internal_GetCurrentPositionInFrameBlock() const
	{
		return m_currentPositionInFrameBlock.GetValue();
	}

	const VirtualRange& LocklessFrameAllocator::Internal_GetVirtualRange() const
	{
		return m_virtualRange;
	}

}
}
