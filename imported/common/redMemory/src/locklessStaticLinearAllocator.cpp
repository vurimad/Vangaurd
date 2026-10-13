/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "locklessStaticLinearAllocator.h"

namespace red
{
namespace memory
{
	LocklessStaticLinearAllocator::LocklessStaticLinearAllocator()
#if !defined( RED_CONFIGURATION_FINAL )
		: m_initialized( false )
#endif
	{
	}

	void LocklessStaticLinearAllocator::Initialize( const StaticLinearAllocatorParameter& parameter )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( !m_initialized, "LocklessStaticLinearAllocator is already initialized.");
#endif

		RED_MEMORY_ASSERT( parameter.buffer != nullptr, "No buffer provided." );
		RED_MEMORY_ASSERT( parameter.bufferSize, "Buffer size can't be 0." );

		const u64 bufferAddress = reinterpret_cast< u64 >( parameter.buffer );

		LinearAllocatorParameter linearAllocatorParameter =
		{
			{ bufferAddress, parameter.bufferSize }
		};

		m_allocator.Initialize( linearAllocatorParameter );

#if !defined( RED_CONFIGURATION_FINAL )
		m_initialized = true;
#endif
	}

	Block LocklessStaticLinearAllocator::Allocate( u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticLinearAllocator is not initialized." );
#endif
		return m_allocator.Allocate( size );
	}

	Block LocklessStaticLinearAllocator::AllocateAligned( u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticLinearAllocator is not initialized." );
#endif
		return m_allocator.AllocateAligned( size, alignment );
	}

	void LocklessStaticLinearAllocator::Free( Block& block )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticLinearAllocator is not initialized." );
#endif
		m_allocator.Free( block );
	}

	Block LocklessStaticLinearAllocator::Reallocate( Block& block, u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticLinearAllocator is not initialized." );
#endif
		return m_allocator.Reallocate( block, size );
	}

	Block LocklessStaticLinearAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticLinearAllocator is not initialized." );
#endif
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	Bool LocklessStaticLinearAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 LocklessStaticLinearAllocator::GetBlockSize( u64 address ) const
	{
		return m_allocator.GetBlockSize( address );
	}

	void LocklessStaticLinearAllocator::Reset()
	{
		m_allocator.Reset();
	}

	void LocklessStaticLinearAllocator::BuildMetrics( LinearAllocatorMetrics& metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void LocklessStaticLinearAllocator::SerializeMetrics( Serializer& serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}
}
}