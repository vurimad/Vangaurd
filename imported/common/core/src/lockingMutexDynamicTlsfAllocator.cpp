/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "lockingMutexDynamicTlsfAllocator.h"

#include "../../redMemory/src/tlsfBlock.h"
#include "../../redMemory/src/scopedLock.h"
#include "../../redMemory/include/redMemoryPublic.h"

namespace red
{
	LockingMutexDynamicTLSFAllocator::LockingMutexDynamicTLSFAllocator()
		: m_allocatorIsFull( false )
	{}

	LockingMutexDynamicTLSFAllocator::~LockingMutexDynamicTLSFAllocator()
	{}

	void LockingMutexDynamicTLSFAllocator::Initialize( const memory::DynamicTLSFAllocatorParameter & parameter )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Initialize( parameter );
	}
	
	void LockingMutexDynamicTLSFAllocator::Uninitialize()
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Uninitialize();
	}

	memory::Block LockingMutexDynamicTLSFAllocator::Allocate( memory::u32 size )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		return RED_UNLIKELY( m_allocatorIsFull ) ? memory::NullBlock() : m_allocator.Allocate( size );
	}
	
	memory::Block LockingMutexDynamicTLSFAllocator::AllocateAligned( memory::u32 size, memory::u32 alignment )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		return RED_UNLIKELY( m_allocatorIsFull ) ? memory::NullBlock() : m_allocator.AllocateAligned( size, alignment );
	}
	
	memory::Block LockingMutexDynamicTLSFAllocator::Reallocate( memory::Block & block, memory::u32 size )
	{
		// Some basic check could be made before locking and calling Reallocate. Like block size == size, do nothing.
		memory::ScopedLock< LockPrimitive > lock( m_lock ); 
		return RED_UNLIKELY( m_allocatorIsFull ) ? memory::NullBlock() : m_allocator.Reallocate( block, size );
	}

	memory::Block LockingMutexDynamicTLSFAllocator::ReallocateAligned( memory::Block & block, memory::u32 size, memory::u32 alignment )
	{
		// Some basic check could be made before locking and calling Reallocate. Like block size == size, do nothing.
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		return RED_UNLIKELY( m_allocatorIsFull ) ? memory::NullBlock() : m_allocator.ReallocateAligned( block, size, alignment );
	}
	
	void LockingMutexDynamicTLSFAllocator::Free( memory::Block & block )
	{
		memory::ScopedLock< LockPrimitive > lock( m_lock );
		m_allocator.Free( block );
	}

	bool LockingMutexDynamicTLSFAllocator::OwnBlock( memory::u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	memory::u64 LockingMutexDynamicTLSFAllocator::GetBlockSize( memory::u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not own by this allocator." );
		return memory::GetTLSFBlockSize( address );
	}

	void LockingMutexDynamicTLSFAllocator::BuildMetrics( memory::TLSFAllocatorMetrics & metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void LockingMutexDynamicTLSFAllocator::SerializeMetrics( memory::Serializer & serializer )
	{
		memory::SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializerMetricsWithoutAllocatorIdentifiers( serializer );
	}

	void LockingMutexDynamicTLSFAllocator::SerializerMetricsWithoutAllocatorIdentifiers( memory::Serializer & serializer )
	{
		m_allocator.SerializerMetricsWithoutAllocatorIdentifiers( serializer );
	}

	memory::Block LockingMutexDynamicTLSFAllocator::GetAllocatedPages() const
	{
		return m_allocator.GetAllocatedPages();
	}
}
