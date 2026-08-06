/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "locklessStaticFixedSizeAllocator.h"

namespace red
{
namespace memory
{
	LocklessStaticFixedSizeAllocator::LocklessStaticFixedSizeAllocator()
#if !defined( RED_CONFIGURATION_FINAL )
		: m_initialized( false )
#endif
	{}

	LocklessStaticFixedSizeAllocator::~LocklessStaticFixedSizeAllocator()
	{}

	void LocklessStaticFixedSizeAllocator::Initialize( const LocklessStaticFixedSizeAllocatorParameter & param )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( !m_initialized, "LocklessStaticFixedSizeAllocator is already initialized." );
#endif

		const u64 bufferAddress = reinterpret_cast< u64 >( param.buffer );
		VirtualRange range = { bufferAddress, bufferAddress + param.bufferSize };
		SystemBlock block = { bufferAddress, param.bufferSize };

		LocklessFixedSizeAllocatorParameter implementationParam = 
		{
			param.blockSize,
			param.blockAlignment,
			range,
			block
		};

		m_allocator.Initialize( implementationParam );

#if !defined( RED_CONFIGURATION_FINAL )
		m_initialized = true;
#endif
	}

	void LocklessStaticFixedSizeAllocator::Uninitialize()
	{}

	Block LocklessStaticFixedSizeAllocator::Allocate( u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticFixedSizeAllocator is not initialized." );
#endif
		return m_allocator.Allocate( size );
	}
	
	Block LocklessStaticFixedSizeAllocator::AllocateAligned( u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticFixedSizeAllocator is not initialized." );
#endif
		return m_allocator.AllocateAligned( size, alignment );
	}
	
	void LocklessStaticFixedSizeAllocator::Free( Block & block )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticFixedSizeAllocator is not initialized." );
#endif
		m_allocator.Free( block );
	}
	
	Block LocklessStaticFixedSizeAllocator::Reallocate( Block & block, u32 size )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticFixedSizeAllocator is not initialized." );
#endif
		return m_allocator.Reallocate( block, size );
	}

	Block LocklessStaticFixedSizeAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
#if !defined( RED_CONFIGURATION_FINAL )
		RED_MEMORY_ASSERT( m_initialized, "LocklessStaticFixedSizeAllocator is not initialized." );
#endif
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	bool LocklessStaticFixedSizeAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 LocklessStaticFixedSizeAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not own by this allocator." );
		RED_UNUSED( address );
		return m_allocator.GetBlockSize();
	}

	u32 LocklessStaticFixedSizeAllocator::GetTotalBlockCount() const
	{
		return m_allocator.GetTotalBlockCount();
	}

	void LocklessStaticFixedSizeAllocator::BuildMetrics( FixedSizeAllocatorMetrics & metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void LocklessStaticFixedSizeAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}
}
}
