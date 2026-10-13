/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dynamicBuddyAllocator.h"
#include "utils.h"

namespace red
{
namespace memory
{
	DynamicBuddyAllocator::DynamicBuddyAllocator()
	{}

	DynamicBuddyAllocator::~DynamicBuddyAllocator()
	{}

	void DynamicBuddyAllocator::Initialize( const DynamicBuddyAllocatorParameter& parameter )
	{
		BuddyAllocatorParamater param
		{
			parameter.systemAllocator,
			parameter.maxBufferSize,
			parameter.initBufferSize,
			parameter.alignment,
			parameter.flags
		};

		m_allocator.Initialize( param );
	}

	void DynamicBuddyAllocator::Uninitialize()
	{
		m_allocator.Uninitialize();
	}

	Block DynamicBuddyAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, m_allocator.GetAlignment() );
	}

	Block DynamicBuddyAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		Block block = m_allocator.AllocateAligned( size, alignment );

		if ( block != NullBlock() )
		{
			return block;
		}

		size = RoundUp( size, alignment );
		if ( m_allocator.CanGrowBuffer( size ) && m_allocator.GrowBlock( size ) )
		{
			block = m_allocator.AllocateAligned( size, alignment );
		}

		return block;
	}

	Block DynamicBuddyAllocator::Reallocate( Block& block, u32 size )
	{
		return m_allocator.ReallocateAligned( block, size, m_allocator.GetAlignment() );
	}

	Block DynamicBuddyAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		Block reallocatedBlock = m_allocator.ReallocateAligned( block, size, alignment );

		if ( !reallocatedBlock.address && size != 0 )
		{
			size = RoundUp( size, alignment );
			if ( m_allocator.CanGrowBuffer( size ) && m_allocator.GrowBlock( size ) )
			{
				reallocatedBlock = m_allocator.ReallocateAligned( block, size, alignment );
			}
		}

		return reallocatedBlock;
	}

	void DynamicBuddyAllocator::Free( Block& block )
	{
		 m_allocator.Free( block );
	}

	bool DynamicBuddyAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 DynamicBuddyAllocator::GetBlockSize( u64 block ) const
	{
		return m_allocator.GetBlockSize( block );
	}

	u32 DynamicBuddyAllocator::GetSystemMemoryUsage() const
	{
		return m_allocator.GetSystemMemoryUsage();
	}

	void DynamicBuddyAllocator::ShrinkBuffer()
	{
		m_allocator.ShrinkBuffer();
	}
}
}