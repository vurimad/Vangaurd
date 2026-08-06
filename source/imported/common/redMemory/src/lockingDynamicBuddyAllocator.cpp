/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "lockingDynamicBuddyAllocator.h"

namespace red
{
namespace memory
{
	LockingDynamicBuddyAllocator::LockingDynamicBuddyAllocator()
	{}

	LockingDynamicBuddyAllocator::~LockingDynamicBuddyAllocator()
	{}

	void LockingDynamicBuddyAllocator::Initialize( const DynamicBuddyAllocatorParameter& parameter )
	{
		m_allocator.Initialize( parameter );
	}

	void LockingDynamicBuddyAllocator::Uninitialize()
	{
		RED_SCOPE_LOCK( m_lock );
		m_allocator.Uninitialize();
	}

	Block LockingDynamicBuddyAllocator::Allocate( u32 size )
	{
		RED_SCOPE_LOCK( m_lock );
		return m_allocator.Allocate( size );
	}

	Block LockingDynamicBuddyAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_SCOPE_LOCK( m_lock );
		return m_allocator.AllocateAligned( size, alignment );
	}

	Block LockingDynamicBuddyAllocator::Reallocate( Block& block, u32 size )
	{
		RED_SCOPE_LOCK( m_lock );
		return m_allocator.Reallocate( block, size );
	}

	Block LockingDynamicBuddyAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		RED_SCOPE_LOCK( m_lock );
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	void LockingDynamicBuddyAllocator::Free( Block& block )
	{
		RED_SCOPE_LOCK( m_lock );
		m_allocator.Free( block );
	}

	bool LockingDynamicBuddyAllocator::OwnBlock( u64 block ) const
	{
		RED_SCOPE_SHARED_LOCK( m_lock );
		return m_allocator.OwnBlock( block );
	}

	u64 LockingDynamicBuddyAllocator::GetBlockSize( u64 block ) const
	{
		RED_SCOPE_SHARED_LOCK( m_lock );
		return m_allocator.GetBlockSize( block );
	}

	u32 LockingDynamicBuddyAllocator::GetSystemMemoryUsage() const
	{
		RED_SCOPE_SHARED_LOCK( m_lock );
		return m_allocator.GetSystemMemoryUsage();
	}

	void LockingDynamicBuddyAllocator::ShrinkBuffer()
	{
		RED_SCOPE_LOCK( m_lock );
		m_allocator.ShrinkBuffer();
	}
}
}