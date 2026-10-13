/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dynamicStackAllocator.h"
#include "systemAllocator.h"
#include "utils.h"
#include "flags.h"

namespace red
{
namespace memory
{
namespace
{
	const u32 c_dynamicStackAllocatorHeaderSize = 8;
	const u64 c_dynamicStackAllocatorRangeSize = RED_GIGA_BYTE( 4 );
}

	DynamicStackAllocator::DynamicStackAllocator()
		:	m_chunkSize(0),
			m_virtualRange( NullVirtualRange() ),
			m_nextVirtualAddress( 0 ),
			m_systemAllocator( nullptr ),
			m_flags( Flags_CPU_Read_Write ),
			m_minimalAllocationSize( 0 )
	{
	}

	DynamicStackAllocator::~DynamicStackAllocator()
	{
		if (m_virtualRange != NullVirtualRange())
			Uninitialize();
	}

	void DynamicStackAllocator::Initialize( const DynamicStackAllocatorParameter& parameter )
	{
		RED_MEMORY_ASSERT( m_virtualRange == NullVirtualRange(), "Allocator was already initialized." );
		RED_MEMORY_ASSERT( parameter.systemAllocator, "Allocator need access to SystemAllocator." );
		RED_MEMORY_ASSERT( parameter.chunkSize >= c_dynamicStackAllocatorHeaderSize, "Chunk size needs to be at least of c_dynamicStackAllocatorHeaderSize." );
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.chunkSize ), "Chunk size needs to be power of 2." );

		m_chunkSize = parameter.chunkSize;
		m_systemAllocator = parameter.systemAllocator;
		m_flags = parameter.flags;
		m_virtualRange = m_systemAllocator->ReserveVirtualRange( c_dynamicStackAllocatorRangeSize, m_flags );
		m_nextVirtualAddress = m_virtualRange.start;

		const SystemBlock block = AllocateBlock( m_chunkSize );
		if ( block != NullSystemBlock() )
		{
			m_minimalAllocationSize = block.size;

			StackAllocatorParameter stackAllocatorParameter =
			{
				{ block.address, block.size },
				DynamicStackAllocator::DefaultAlignmentType::value
			};

			m_allocator.Initialize(stackAllocatorParameter);
		}
	}

	void DynamicStackAllocator::Uninitialize()
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		m_systemAllocator->ReleaseVirtualRange(m_virtualRange);
		m_virtualRange = NullVirtualRange();
		m_nextVirtualAddress = 0;
	}

	Block DynamicStackAllocator::Allocate( u32 size )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		Block block = m_allocator.Allocate( size );
		if ( !block.address && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				block = m_allocator.Allocate(size);
			}
		}

		return block;
	}

	Block DynamicStackAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		Block block = m_allocator.AllocateAligned( size, alignment );
		if ( !block.address && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				block = m_allocator.AllocateAligned(size, alignment);
			}
		}

		return block;
	}

	void DynamicStackAllocator::Free( Block& block )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		m_allocator.Free( block );
	}

	Block DynamicStackAllocator::Reallocate( Block& block, u32 size )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		Block reallocatedBlock = m_allocator.Reallocate( block, size );
		if ( !reallocatedBlock.address && size != 0 && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				reallocatedBlock = m_allocator.Allocate( size );
			}
		}

		return reallocatedBlock;
	}

	Block DynamicStackAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		Block reallocatedBlock = m_allocator.ReallocateAligned( block, size, alignment );
		if ( !reallocatedBlock.address && size != 0 && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				reallocatedBlock = m_allocator.AllocateAligned( size, alignment );
			}
		}

		return reallocatedBlock;
	}

	bool DynamicStackAllocator::OwnBlock( u64 block ) const
	{
		return block - m_virtualRange.start < GetVirtualRangeSize( m_virtualRange );
	}

	u64 DynamicStackAllocator::GetBlockSize( u64 block ) const
	{
		return m_allocator.GetBlockSize( block );
	}

	void DynamicStackAllocator::Reset()
	{
		const u64 preallocatedChunkAddress = m_virtualRange.start + m_minimalAllocationSize;
		RED_MEMORY_ASSERT( preallocatedChunkAddress <= m_nextVirtualAddress, "Next virtual address is pointing to invalid location." );

		m_allocator.Reset();

		if ( preallocatedChunkAddress < m_nextVirtualAddress )
		{
			const SystemBlock decomitBlock = { preallocatedChunkAddress, m_nextVirtualAddress - preallocatedChunkAddress };
			m_systemAllocator->Decommit( decomitBlock );

			m_nextVirtualAddress = preallocatedChunkAddress;
			m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
		}
	}

	void DynamicStackAllocator::BuildMetrics( StackAllocatorMetrics & metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void DynamicStackAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}

	SystemBlock DynamicStackAllocator::AllocateBlock( u32 size )
	{
		SystemBlock block = { m_nextVirtualAddress, RoundUp( size, m_chunkSize ) };
		block = m_systemAllocator->Commit( block, m_flags );

		if (block.address)
		{
			m_nextVirtualAddress = block.address + block.size;
			return block;
		}

		return NullSystemBlock();
	}

	bool DynamicStackAllocator::CanCreateMoreBlock() const
	{
		return m_nextVirtualAddress < m_virtualRange.end;
	}

}
}