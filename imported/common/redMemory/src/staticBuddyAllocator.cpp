/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "staticBuddyAllocator.h"
#include "utils.h"

namespace red
{
namespace memory
{
	StaticBuddyAllocator::StaticBuddyAllocator()
	{}

	StaticBuddyAllocator::~StaticBuddyAllocator()
	{}

	void StaticBuddyAllocator::Initialize( const StaticBuddyAllocatorParameter& parameter )
	{
		BuddyAllocatorParamater param
		{
			parameter.systemAllocator,
			parameter.bufferSize,
			parameter.bufferSize,
			parameter.alignment,
			parameter.flags
		};

		m_allocator.Initialize( param );
	}

	void StaticBuddyAllocator::Uninitialize()
	{
		m_allocator.Uninitialize();
	}

	Block StaticBuddyAllocator::Allocate( u32 size )
	{
		return m_allocator.Allocate( size );
	}

	Block StaticBuddyAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		return m_allocator.AllocateAligned( size, alignment );
	}

	Block StaticBuddyAllocator::Reallocate(Block& block, u32 size)
	{
		return m_allocator.Reallocate( block, size );
	}

	Block StaticBuddyAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	void StaticBuddyAllocator::Free( Block& block )
	{
		return m_allocator.Free( block );
	}

	bool StaticBuddyAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 StaticBuddyAllocator::GetBlockSize( u64 block ) const
	{
		return m_allocator.GetBlockSize( block );
	}
}
}