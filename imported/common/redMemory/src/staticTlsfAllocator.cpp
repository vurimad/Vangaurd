/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "staticTlsfAllocator.h"
#include "assert.h"
#include "tlsfConstant.h"
#include "utils.h"
#include "tlsfBlock.h"

namespace red
{
namespace memory
{
	StaticTLSFAllocator:: StaticTLSFAllocator()
		: m_parameter( { nullptr, 0 } )
	{}

	StaticTLSFAllocator::~StaticTLSFAllocator()
	{}

	void StaticTLSFAllocator::Initialize( const StaticTLSFAllocatorParameter & parameter )
	{
		m_parameter = parameter;
		ValidateParameter();
	
		const u64 bufferAddress = reinterpret_cast< u64 >( parameter.buffer );
		VirtualRange range = { bufferAddress, bufferAddress + parameter.bufferSize };
		SystemBlock block = { bufferAddress, parameter.bufferSize };

		TLSFAllocatorParameter implementationParam = 
		{
			range,
			block
		};

		m_allocator.Initialize( implementationParam );
	}

	void StaticTLSFAllocator::Uninitialize()
	{}

	Block StaticTLSFAllocator::Allocate( u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StaticTLSFAllocator is not initialized." );
		return m_allocator.Allocate( size );
	}

	Block StaticTLSFAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StaticTLSFAllocator is not initialized." );
		return m_allocator.AllocateAligned( size, alignment );
	}

	void StaticTLSFAllocator::Free( Block & block )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StaticTLSFAllocator is not initialized." );
		m_allocator.Free( block );
	}

	Block StaticTLSFAllocator::Reallocate( Block & block, u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StaticTLSFAllocator is not initialized." );
		return m_allocator.Reallocate( block, size );
	}

	Block StaticTLSFAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StaticTLSFAllocator is not initialized." );
		return m_allocator.ReallocateAligned( block, size, alignment );
	}

	bool StaticTLSFAllocator::OwnBlock( u64 block ) const
	{
		return m_allocator.OwnBlock( block );
	}

	u64 StaticTLSFAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not own by this allocator." );
		return GetTLSFBlockSize( address );
	}

	u64 StaticTLSFAllocator::GetBiggestBlockSize() const
	{
		return m_allocator.GetBiggestBlockSize();
	}

	void StaticTLSFAllocator::BuildMetrics( TLSFAllocatorMetrics & metrics )
	{
		metrics.metrics.consumedSystemMemoryBytes = m_parameter.bufferSize;
		m_allocator.BuildMetrics( metrics );
	}

	void StaticTLSFAllocator::SerializeMetrics( Serializer & serializer )
	{
		SerializeAllocatorIdentifiers( this, serializer );
		m_allocator.SerializeMetrics( serializer );
	}

	bool StaticTLSFAllocator::IsInitialized() const
	{
		return m_parameter.buffer != nullptr;
	}

	void StaticTLSFAllocator::ValidateParameter( ) const
	{
		RED_MEMORY_ASSERT( m_parameter.buffer, "Cannot initialize StaticTLSFAllocator with null buffer." );
		RED_MEMORY_ASSERT( IsAligned( m_parameter.buffer, 16 ), "Cannot initialize StaticTLSFAllocator. Provided buffer must be aligned on 16 byte.");
		RED_MEMORY_ASSERT( m_parameter.bufferSize, "Cannot initialize StaticTLSFAllocator with 0 size buffer." );
		RED_MEMORY_ASSERT( m_parameter.bufferSize > ComputeTLSFBookKeepingSize( m_parameter.bufferSize ), 
			"Provided buffer of size %d is not big enough. Need at least %d byte.", m_parameter.bufferSize, ComputeTLSFBookKeepingSize( m_parameter.bufferSize ) );
	}
}
}
