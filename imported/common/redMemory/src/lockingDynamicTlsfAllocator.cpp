/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "lockingDynamicTlsfAllocator.h"
#include "tlsfBlock.h"
#include "scopedLock.h"

namespace red
{
namespace memory
{
	// bit hacky, but Allocate may trigger OOM which will call BuildMetrics to generate memory report...
	static thread_local Bool s_isLocked = false;

	LockingDynamicTLSFAllocator::LockingDynamicTLSFAllocator()
		: m_allocatorIsFull( false )
	{}

	LockingDynamicTLSFAllocator::~LockingDynamicTLSFAllocator()
	{}

	void LockingDynamicTLSFAllocator::Initialize( const DynamicTLSFAllocatorParameter & parameter )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Initialize( parameter );
	}
	
	void LockingDynamicTLSFAllocator::Uninitialize()
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Uninitialize();
	}

	Block LockingDynamicTLSFAllocator::Allocate( u32 size )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		s_isLocked = true;
		Block result = RED_UNLIKELY( m_allocatorIsFull ) ? NullBlock() : m_allocator.Allocate( size );
		s_isLocked = false;
		return result;
	}
	
	Block LockingDynamicTLSFAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		s_isLocked = true;
		Block result = RED_UNLIKELY( m_allocatorIsFull ) ? NullBlock() : m_allocator.AllocateAligned( size, alignment );
		s_isLocked = false;
		return result;
	}
	
	Block LockingDynamicTLSFAllocator::Reallocate( Block & block, u32 size )
	{
		// Some basic check could be made before locking and calling Reallocate. Like block size == size, do nothing.
		memory::ScopedLock< LockPrimitive > lock( m_lock ); 
		s_isLocked = true;
		Block result = RED_UNLIKELY( m_allocatorIsFull ) ? NullBlock() : m_allocator.Reallocate( block, size );
		s_isLocked = false;
		return result;
	}

	Block LockingDynamicTLSFAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		// Some basic check could be made before locking and calling Reallocate. Like block size == size, do nothing.
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		s_isLocked = true;
		Block result = RED_UNLIKELY( m_allocatorIsFull ) ? NullBlock() : m_allocator.ReallocateAligned( block, size, alignment );
		s_isLocked = false;
		return result;
	}
	
	void LockingDynamicTLSFAllocator::Free( Block & block )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Free( block );
	}

	bool LockingDynamicTLSFAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 LockingDynamicTLSFAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not own by this allocator." );
		return GetTLSFBlockSize( address );
	}

	void LockingDynamicTLSFAllocator::BuildMetrics( TLSFAllocatorMetrics & metrics )
	{
		if ( !s_isLocked ) m_lock.AcquireShared();
		m_allocator.BuildMetrics( metrics );
		if ( !s_isLocked ) m_lock.ReleaseShared();
	}

	void LockingDynamicTLSFAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		{
			if ( !s_isLocked ) m_lock.AcquireShared();
			m_allocator.SerializerMetricsWithoutAllocatorIdentifiers( serializer );
			if ( !s_isLocked ) m_lock.ReleaseShared();
		}
	}

	void LockingDynamicTLSFAllocator::SerializerMetricsWithoutAllocatorIdentifiers( Serializer & serializer )
	{
		if ( !s_isLocked ) m_lock.AcquireShared();
		m_allocator.SerializerMetricsWithoutAllocatorIdentifiers( serializer );
		if ( !s_isLocked ) m_lock.ReleaseShared();
	}

	void LockingDynamicTLSFAllocator::InternalMarkLockingDynamicTLSFAllocatorAsFull()
	{
		m_allocatorIsFull = true;
	}

	Block LockingDynamicTLSFAllocator::GetAllocatedPages() const
	{
		return m_allocator.GetAllocatedPages();
	}
}
}
