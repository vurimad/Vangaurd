/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "staticStackAllocator.h"

namespace red
{
namespace memory
{
	StaticStackAllocator::StaticStackAllocator()
#if !defined( RED_CONFIGURATION_FINAL )
		: m_initialized( false )
#endif
	{}

	void StaticStackAllocator::Initialize( const StaticStackAllocatorParameter& parameter )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( !m_initialized, "StaticStackAllocator is already initialized.");
#endif

		RED_MEMORY_ASSERT( parameter.buffer != nullptr, "No buffer provided." );
		RED_MEMORY_ASSERT( parameter.bufferSize, "Buffer size can't be 0." );

		const u64 bufferAddress = reinterpret_cast< u64 >( parameter.buffer );

		StackAllocatorParameter stackAllocatorParameter =
		{
			{ bufferAddress, parameter.bufferSize },
			StaticStackAllocator::DefaultAlignmentType::value
		};

		m_allocator.Initialize(stackAllocatorParameter);

#if !defined( RED_CONFIGURATION_FINAL )
		m_initialized = true;
#endif
	}

	Block StaticStackAllocator::Allocate( u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "StaticStackAllocator is not initialized." );
#endif
		return m_allocator.Allocate( size );
	}

	Block StaticStackAllocator::AllocateAligned( u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "StaticStackAllocator is not initialized." );
#endif
		return m_allocator.AllocateAligned( size, alignment );
	}

	void StaticStackAllocator::Free( Block& block )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "StaticStackAllocator is not initialized." );
#endif
		m_allocator.Free( block );
	}

	Block StaticStackAllocator::Reallocate( Block& block, u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "StaticStackAllocator is not initialized." );
#endif
		return m_allocator.Reallocate( block, size );
	}

	Block StaticStackAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "StaticStackAllocator is not initialized." );
#endif
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	bool StaticStackAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 StaticStackAllocator::GetBlockSize( u64 block ) const
	{
		return m_allocator.GetBlockSize( block );
	}

	void StaticStackAllocator::Reset()
	{
		m_allocator.Reset();
	}

	void StaticStackAllocator::BuildMetrics( StackAllocatorMetrics & metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void StaticStackAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}
}
}