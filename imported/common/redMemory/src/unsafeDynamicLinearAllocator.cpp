/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "unsafeDynamicLinearAllocator.h"
#include "systemAllocator.h"
#include "utils.h"
#include "flags.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#endif

namespace red
{
namespace memory
{

namespace
{
const u64 c_dynamicLinearAllocatorRangeSize = 4_GB;
}

UnsafeDynamicLinearAllocator::UnsafeDynamicLinearAllocator()
	: m_startAddress( 0 )
	, m_endAddress( 0 )
	, m_position( 0 )
	, m_virtualRange( NullVirtualRange() )
	, m_nextVirtualAddress( 0 )
	, m_systemAllocator( nullptr )
	, m_chunkSize( 0 )
	, m_flags( Flags_CPU_Read_Write )
{}

UnsafeDynamicLinearAllocator::~UnsafeDynamicLinearAllocator()
{
	if ( m_virtualRange != NullVirtualRange() )
	{
		Uninitialize();
	}
}

void UnsafeDynamicLinearAllocator::Initialize( const UnsafeDynamicLinearAllocatorParameter& parameter )
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
		m_startAddress = block.address;
		m_endAddress = m_startAddress + block.size;
		m_position = m_startAddress;
	}
}

void UnsafeDynamicLinearAllocator::Uninitialize()
{
	RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
	m_systemAllocator->ReleaseVirtualRange( m_virtualRange );
	m_virtualRange = NullVirtualRange();
	m_nextVirtualAddress = 0;
}

Block UnsafeDynamicLinearAllocator::Internal_Allocate( u32 size, u32 alignment )
{
	alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
	size = RoundUp( size, alignment );

	u64 address = RoundUp( m_position, static_cast< u64 >( alignment ) );

	const u64 newPosition = ( address + size );
	if ( newPosition > m_endAddress )
	{
		return NullBlock();
	}

	m_position = newPosition;

	return { address, size };
}

Block UnsafeDynamicLinearAllocator::Allocate( u32 size )
{
	return AllocateAligned( size, DefaultAlignmentType::value );
}

Block UnsafeDynamicLinearAllocator::AllocateAligned( u32 size, u32 alignment )
{
	RED_MEMORY_ASSERT( IsInitialized(), "DynamicLinearAllocator is not initialized" );

	Block block = Internal_Allocate( size, alignment );
	if ( !block.address && CanCreateMoreBlock() )
	{
		if ( AllocateBlock( size ) != NullSystemBlock() )
		{
			m_startAddress = m_virtualRange.start;
			m_endAddress = m_nextVirtualAddress;
			block = Internal_Allocate( size, alignment );
		}
	}

	return block;
}

void UnsafeDynamicLinearAllocator::Free( Block& block )
{
	RED_FATAL( "UnsafeDynamicLinearAllocator: Unsupported Operation" );
	RED_UNUSED( block );
}

Block UnsafeDynamicLinearAllocator::Reallocate( Block& block, u32 size )
{
	RED_FATAL( "UnsafeDynamicLinearAllocator: Unsupported Operation" );
	RED_UNUSED( block );
	RED_UNUSED( size );
	return NullBlock();
}

Block UnsafeDynamicLinearAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
{
	RED_FATAL( "UnsafeDynamicLinearAllocator: Unsupported Operation" );
	RED_UNUSED( block );
	RED_UNUSED( size );
	RED_UNUSED( alignment );
	return NullBlock();
}

Bool UnsafeDynamicLinearAllocator::OwnBlock( u64 block ) const
{
	return block - m_virtualRange.start < GetVirtualRangeSize( m_virtualRange );
}

u64 UnsafeDynamicLinearAllocator::GetBlockSize( u64 address ) const
{
	RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not owned by this LinearAllocator." );
	RED_UNUSED( address );
	return 0;
}

void UnsafeDynamicLinearAllocator::Reset()
{
	const u64 preallocatedChunkAddress = m_virtualRange.start;
	RED_MEMORY_ASSERT( preallocatedChunkAddress <= m_nextVirtualAddress, "Next virtual address is pointing to invalid location." );

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
	if ( m_position != m_startAddress )
	{
		sceMatFreeRange( reinterpret_cast< void* >( m_startAddress ), m_position - m_startAddress );
	}
#endif
	m_position = m_startAddress;

	if ( preallocatedChunkAddress < m_nextVirtualAddress )
	{
		const SystemBlock decomitBlock = { preallocatedChunkAddress, m_nextVirtualAddress - preallocatedChunkAddress };
		m_systemAllocator->Decommit( decomitBlock );

		m_nextVirtualAddress = preallocatedChunkAddress;
		m_startAddress = m_virtualRange.start;
		m_endAddress = m_nextVirtualAddress;
	}
}

void UnsafeDynamicLinearAllocator::BuildMetrics( LinearAllocatorMetrics& metrics )
{
	Memzero( &metrics, sizeof( metrics ) );

	metrics.metrics.bookKeepingBytes = 0;
	metrics.metrics.consumedSystemMemoryBytes = m_endAddress - m_startAddress;
	metrics.metrics.consumedMemoryBytes = m_position - m_startAddress;

	const auto freeBlockSize = m_endAddress - m_position;
	const auto freeVirtualMemory = m_virtualRange.end - m_nextVirtualAddress;
	metrics.metrics.smallestBlockSize = freeBlockSize >= DefaultAlignmentType::value ? DefaultAlignmentType::value : 0;
	metrics.metrics.largestBlockSize = freeBlockSize + freeVirtualMemory;
}

void UnsafeDynamicLinearAllocator::SerializeMetrics( Serializer & serializer )
{
	SerializeAllocatorIdentifiers( this, serializer );

	LinearAllocatorMetrics metrics;
	BuildMetrics( metrics );

	serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
	serializer.Serialize( &metrics, sizeof( metrics ) );
}

Block UnsafeDynamicLinearAllocator::GetUsedMemoryRange() const
{
	return { m_startAddress, m_position - m_startAddress };
}

bool UnsafeDynamicLinearAllocator::IsInitialized() const
{
	return m_virtualRange != NullVirtualRange();
}

SystemBlock UnsafeDynamicLinearAllocator::AllocateBlock( u32 size )
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

bool UnsafeDynamicLinearAllocator::CanCreateMoreBlock() const
{
	return m_nextVirtualAddress < m_virtualRange.end;
}


} // memory
} // red
