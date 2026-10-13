/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "telemetryAllocator.h"
#include "allocators.h"
#include "assert.h"
#include "../../redMemory/include/flags.h"
#include "../../redMemory/include/systemAllocator.h"
#include "../../redMemory/src/tlsfBlock.h"

namespace red
{
	const memory::u64 c_telemetryAllocatorTLSFVirtualRange = RED_GIGA_BYTE( 1 );
	const memory::u32 c_telemetryAllocatorTLSFInitialAllocSize =  RED_MEGA_BYTE( 1 );
	const memory::u32 c_telemetryAllocatorTLSFChunkSize = RED_MEGA_BYTE( 1 );
	const memory::u32 c_telemetryAllocatorVirtualRangeAlignment = 0;
	const memory::u32 c_telemetryAllocatorMaxTLSFSize = RED_MEGA_BYTE( 2 );


	TelemetryAllocator::TelemetryAllocator()
		: m_systemAllocator( nullptr )
		, m_threadIdProvider( nullptr )
	{
		m_threadIdProvider = &m_threadIdProviderStorage;
	}

	TelemetryAllocator::~TelemetryAllocator()
	{}

	void TelemetryAllocator::Initialize( const TelemetryAllocatorParameter & parameter )
	{
		m_telemetryThreadIdList.Fill( 0 );

		m_systemAllocator = parameter.systemAllocator;

		RED_MEMORY_ASSERT( m_systemAllocator, "TelemtryAllocator cannot be initialized. Need access to SystemAllocator" );

		m_defaultAllocator = parameter.defaultAllocator;

		RED_MEMORY_ASSERT( m_defaultAllocator, "TelemtryAllocator cannot be initialized. Need access to DefaultAllocator" );

		memory::DynamicTLSFAllocatorParameter tlsfParameter =
		{
			m_systemAllocator,
			c_telemetryAllocatorTLSFVirtualRange,
			c_telemetryAllocatorTLSFInitialAllocSize,
			c_telemetryAllocatorTLSFChunkSize,
			memory::Flags_CPU_Read_Write | memory::Flags::Flags_Skip_System_OOM,
			c_telemetryAllocatorVirtualRangeAlignment
		};

		m_tlsfAllocator.Initialize( tlsfParameter );
		m_threadMonitor = red::memory::GetThreadMonitor();
		m_threadMonitor->RegisterOnThreadDiedSignal( &TelemetryAllocator::NotifyOnThreadDied, this );
	}

	void TelemetryAllocator::Uninitialize()
	{
		m_threadMonitor->UnregisterFromThreadDiedSignal( &TelemetryAllocator::NotifyOnThreadDied, this );
		m_tlsfAllocator.Uninitialize();
	}

	memory::Block TelemetryAllocator::Allocate( memory::u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "TelemetryAllocator was not initialized." );

		memory::Block block = memory::NullBlock();

		size = size > 1u ? size : 1u;

		//Use mutex version of TLSF allocator only in telemetry thread
		if ( IsInTelemetryThread() && size <= c_telemetryAllocatorMaxTLSFSize )
		{
			block = m_tlsfAllocator.Allocate( size );
		}

		if ( !block.address )
		{
			block = m_defaultAllocator->Allocate( size );
		}

		return block;
	}

	memory::Block TelemetryAllocator::AllocateAligned( memory::u32 size, memory::u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "TelemetryAllocator was not initialized." );

		memory::Block block = memory::NullBlock();

		size = size > 1u ? size : 1u;

		//Use mutex version of TLSF allocator only in telemetry thread
		if ( IsInTelemetryThread() && size <= c_telemetryAllocatorMaxTLSFSize )
		{
			block = m_tlsfAllocator.AllocateAligned( size, alignment );
		}

		if ( !block.address )
		{
			block = m_defaultAllocator->AllocateAligned( size, alignment );
		}

		return block;
	}

	memory::Block TelemetryAllocator::Reallocate( memory::Block & block, memory::u32 size )
	{
		if ( size == 0 )
		{
			Free( block );
			return memory::NullBlock();
		}

		if ( block.address == 0 )
		{
			return Allocate( size );
		}

		if ( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromTLSFAllocator( block, size );
		}

		return m_defaultAllocator->Reallocate( block, size );
	}

	memory::Block TelemetryAllocator::ReallocateAligned( memory::Block & block, memory::u32 size, memory::u32 alignment )
	{
		RED_MEMORY_ASSERT( memory::IsAligned( block.address, alignment ), "Cannot Reallocate a block on different alignment." );

		if ( size == 0 )
		{
			Free( block );
			return memory::NullBlock();
		}

		if ( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		if ( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromTLSFAllocator( block, size, alignment );
		}

		return m_defaultAllocator->ReallocateAligned( block, size, alignment );
	}

	memory::Block TelemetryAllocator::ReallocateFromTLSFAllocator( memory::Block & block, memory::u32 size )
	{
		memory::Block newBlock = memory::NullBlock();

		if ( size <= c_telemetryAllocatorMaxTLSFSize )
		{
			newBlock = m_tlsfAllocator.Reallocate( block, size );
		}

		if ( !newBlock.address )
		{
			newBlock = m_defaultAllocator->Allocate( size );
			if ( newBlock.address )
			{
				memory::MemcpyBlock( newBlock.address, block.address, std::min( size, memory::GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}

		return newBlock;
	}

	memory::Block TelemetryAllocator::ReallocateFromTLSFAllocator( memory::Block & block, memory::u32 size, memory::u32 alignment )
	{
		memory::Block newBlock = memory::NullBlock();

		if ( size <= c_telemetryAllocatorMaxTLSFSize )
		{
			newBlock = m_tlsfAllocator.ReallocateAligned( block, size, alignment );
		}

		if ( !newBlock.address )
		{
			newBlock = m_defaultAllocator->AllocateAligned( size, alignment );
			if ( newBlock.address )
			{
				memory::MemcpyBlock( newBlock.address, block.address, std::min( size, memory::GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
			}
		}

		return newBlock;
	}

	void TelemetryAllocator::Free( memory::Block & block )
	{
		if ( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			m_tlsfAllocator.Free( block );
		}
		else
		{
			m_defaultAllocator->Free( block );
		}
	}

	bool TelemetryAllocator::OwnBlock( memory::u64 block ) const
	{
		return m_tlsfAllocator.OwnBlock( block );
	}

	memory::u64 TelemetryAllocator::GetBlockSize( memory::u64 address ) const
	{
		if ( m_tlsfAllocator.OwnBlock( address ) )
		{
			return memory::GetTLSFBlockSize( address );
		}
		
		return m_defaultAllocator->GetBlockSize( address );
	}

	void TelemetryAllocator::RegisterCurrentThreadAsTelemetry()
	{
		RED_MEMORY_ASSERT( IsInitialized(), "TelemetryAllocator was not initialized." );
		static int count = 0;

		count += 1;

		const memory::u32 id = m_threadIdProvider->GetCurrentId();

		if( std::find( m_telemetryThreadIdList.Begin(), m_telemetryThreadIdList.End(), id ) == m_telemetryThreadIdList.End() )
		{
			for( auto iter = m_telemetryThreadIdList.Begin(), end = m_telemetryThreadIdList.End(); iter != end; ++iter )
			{
				if( atomic::CompareExchange32( reinterpret_cast< atomic::TAtomic32* >( &( *iter ) ), id, 0 ) == 0 )
				{
					m_threadMonitor->MonitorCurrentThread();
					return;
				}
			}
			RED_MEMORY_ASSERT( false, "No more space for Thread in TelemetryAllocator." );
		}
	}

	void TelemetryAllocator::OnThreadDied( red::memory::ThreadId id )
	{
		auto it = std::find( m_telemetryThreadIdList.Begin(), m_telemetryThreadIdList.End(), id );
		if ( it != m_telemetryThreadIdList.End() )
		{
			*it = 0;
		}
	}

	void TelemetryAllocator::NotifyOnThreadDied( red::memory::ThreadId id, void * userData )
	{
		TelemetryAllocator* allocator = static_cast< TelemetryAllocator* >( userData );
		allocator->OnThreadDied( id );
	}

	Bool TelemetryAllocator::IsInTelemetryThread() const
	{
		const memory::u32 id = m_threadIdProvider->GetCurrentId();
		return std::find( m_telemetryThreadIdList.Begin(), m_telemetryThreadIdList.End(), id ) != m_telemetryThreadIdList.End();
	}

	bool TelemetryAllocator::IsInitialized() const
	{
		return m_systemAllocator != NULL && m_defaultAllocator != NULL;
	}

	RED_CORE_API TelemetryAllocator & AcquireTelemetryAllocator()
	{
		return GetCoreAllocatorStorage().m_telemetryAllocator;
	}
}
