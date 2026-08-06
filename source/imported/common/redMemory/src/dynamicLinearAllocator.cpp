/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dynamicLinearAllocator.h"
#include "systemAllocator.h"
#include "utils.h"
#include "flags.h"

namespace red
{
namespace memory
{
namespace
{
	const u64 c_dynamicLinearAllocatorRangeSize = 4_GB;
}

	DynamicLinearAllocator::DynamicLinearAllocator()
		: m_virtualRange( NullVirtualRange() )
		, m_nextVirtualAddress( 0 )
		, m_minimalAllocationSize( 0 )
		, m_systemAllocator( nullptr )
		, m_chunkSize( 0 )
		, m_flags( Flags_CPU_Read_Write )
	{}

	DynamicLinearAllocator::~DynamicLinearAllocator()
	{
		if ( m_virtualRange != NullVirtualRange() )
			Uninitialize();
	}

	void DynamicLinearAllocator::Initialize( const DynamicLinearAllocatorParameter& parameter )
	{
		RED_MEMORY_ASSERT( m_virtualRange == NullVirtualRange(), "Allocator was already initialized." );
		RED_MEMORY_ASSERT( parameter.systemAllocator, "Allocator need access to SystemAllocator." );
		RED_MEMORY_ASSERT( parameter.chunkSize, "Chunk size needs to be at least 1." );
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.chunkSize ), "Chunk size needs to be power of 2." );

		m_chunkSize = parameter.chunkSize;
		m_systemAllocator = parameter.systemAllocator;
		m_flags = parameter.flags;
		m_virtualRange = m_systemAllocator->ReserveVirtualRange( c_dynamicLinearAllocatorRangeSize, m_flags );
		m_nextVirtualAddress = m_virtualRange.start;

		const SystemBlock block = AllocateBlock( m_chunkSize );
		if ( block != NullSystemBlock() )
		{
			LinearAllocatorParameter linearAllocatorParameter =
			{
				{ block.address, block.size }
			};

			m_allocator.Initialize( linearAllocatorParameter );
		}
	}

	void DynamicLinearAllocator::Uninitialize()
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		m_systemAllocator->ReleaseVirtualRange( m_virtualRange );
		m_virtualRange = NullVirtualRange();
		m_nextVirtualAddress = 0;
	}

	Block DynamicLinearAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, DefaultAlignmentType::value );
	}

	Block DynamicLinearAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "DynamicLinearAllocator is not initialized" );

		Block block = m_allocator.AllocateAligned( size, alignment );
		if ( !block.address && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				block = m_allocator.AllocateAligned( size, alignment );
			}
		}

		return block;
	}

	void DynamicLinearAllocator::Free( Block& block )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "DynamicLinearAllocator is not initialized" );
		m_allocator.Free( block );
	}

	Block DynamicLinearAllocator::Reallocate( Block& block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}

	Block DynamicLinearAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "DynamicLinearAllocator is not initialized" );
		Block reallocatedBlock = m_allocator.ReallocateAligned( block, size, alignment );
		if ( !reallocatedBlock.address && size != 0 && CanCreateMoreBlock() )
		{
			if ( AllocateBlock( size ) != NullSystemBlock() )
			{
				m_allocator.UpdateBuffer( { m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start } );
				reallocatedBlock = m_allocator.ReallocateAligned( block, size, alignment );
			}
		}

		return reallocatedBlock;
	}

	Bool DynamicLinearAllocator::OwnBlock( u64 block ) const
	{
		return block - m_virtualRange.start < GetVirtualRangeSize( m_virtualRange );
	}

	u64 DynamicLinearAllocator::GetBlockSize( u64 address ) const
	{
		return m_allocator.GetBlockSize( address );
	}

	u32 DynamicLinearAllocator::GetChunkSize() const
	{
		return m_chunkSize;
	}

	void DynamicLinearAllocator::Reset()
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

	void DynamicLinearAllocator::BuildMetrics( LinearAllocatorMetrics& metrics )
	{
		m_allocator.BuildMetrics( metrics );

		const auto freeBlockSize = metrics.metrics.largestBlockSize;
		const auto freeVirtualMemory = m_virtualRange.end - m_nextVirtualAddress;
		metrics.metrics.largestBlockSize = freeBlockSize + freeVirtualMemory;
	}

	void DynamicLinearAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}

	bool DynamicLinearAllocator::IsInitialized() const
	{
		return m_virtualRange != NullVirtualRange();
	}

	SystemBlock DynamicLinearAllocator::AllocateBlock( u32 size )
	{
		SystemBlock block = { m_nextVirtualAddress, RoundUp( size, m_chunkSize ) };
		block = m_systemAllocator->Commit( block, m_flags );

		if ( block.address )
		{
			m_nextVirtualAddress = block.address + block.size;
			return block;
		}

		return NullSystemBlock();
	}

	bool DynamicLinearAllocator::CanCreateMoreBlock() const
	{
		return m_nextVirtualAddress < m_virtualRange.end;
	}

	void DynamicLinearAllocator::Reserve( u32 size )
	{
		if( CanCreateMoreBlock() && AllocateBlock( size ) != NullSystemBlock() )
		{
			m_allocator.UpdateBuffer( {m_virtualRange.start, m_nextVirtualAddress - m_virtualRange.start} );
		}
	}
}
}