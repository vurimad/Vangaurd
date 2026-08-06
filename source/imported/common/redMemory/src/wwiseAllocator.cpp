/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "wwiseAllocator.h"
#include "assert.h"
#include "tlsfBlock.h"
#include "vault.h"
#include "flags.h"

namespace red
{
namespace memory
{
    const u64 c_wwiseAllocatorTLSFVirtualRange = RED_GIGA_BYTE( 1 );
	const u32 c_wwiseAllocatorTLSFInitialAllocSize =  RED_MEGA_BYTE( 16 );
	const u32 c_wwiseAllocatorTLSFChunkSize = RED_MEGA_BYTE( 2 );
    const u32 c_wwiseAllocatorVirtualRangeAlignment = 0;
    const u32 c_wwiseAllocatorMaxTLSFSize = RED_MEGA_BYTE( 2 );

    WwiseAllocator::WwiseAllocator()
        : m_systemAllocator( nullptr )
		, m_defaultAllocator( nullptr )
	{}

    WwiseAllocator::~WwiseAllocator()
    {}

    void WwiseAllocator::Initialize( const WwiseAllocatorParameter & parameter )
    {
        m_systemAllocator = parameter.systemAllocator;

        RED_MEMORY_ASSERT( m_systemAllocator, "WwiseAllocator cannot be initialized. Need access to SystemAllocator" );

		m_defaultAllocator = parameter.defaultAllocator;

		RED_MEMORY_ASSERT( m_defaultAllocator, "WwiseAllocator cannot be initialized. Need access to DefaultAllocator" );

		red::memory::BigSizeAllocatorParameter bigSizeParam =
		{
			m_systemAllocator,
			RED_MEGA_BYTE( 500 ),
			red::memory::Flags_CPU_Read_Write
		};
		m_bigSizeAllocator.Initialize( bigSizeParam );

        DynamicTLSFAllocatorParameter tlsfParameter =
        {
            m_systemAllocator,
            c_wwiseAllocatorTLSFVirtualRange,
            c_wwiseAllocatorTLSFInitialAllocSize,
            c_wwiseAllocatorTLSFChunkSize,
            Flags_CPU_Read_Write | Flags_Skip_System_OOM,
            c_wwiseAllocatorVirtualRangeAlignment
        };

        m_tlsfAllocator.Initialize( tlsfParameter );
    }

    void WwiseAllocator::Uninitialize()
    {
        m_tlsfAllocator.Uninitialize();
		m_bigSizeAllocator.Uninitialize();
    }

	thread_local bool s_isThreadRegistered = false;

    Block WwiseAllocator::Allocate( u32 size )
    {
        RED_MEMORY_ASSERT( IsInitialized(), "WwiseAllocator was not initialized." );

        Block block = NullBlock();

        size = size > 1u ? size : 1u;

        if ( size > c_slabMaxAllocSize  && size <= c_wwiseAllocatorMaxTLSFSize )
        {
            block = m_tlsfAllocator.Allocate( size );
        }

		if ( !block.address )
		{
			const Bool canUseDefaultAllocator = size <= red::memory::DefaultAllocator::GetMaxAllocationSize();
			if ( canUseDefaultAllocator )
			{
				if( !s_isThreadRegistered )
				{
					s_isThreadRegistered = true;
					red::memory::RegisterCurrentThread( "WWise" );
				}

				block = m_defaultAllocator->Allocate( size );
			}
			else
			{
				block = m_bigSizeAllocator.Allocate( size );
			}
		}

        return block;
    }

    Block WwiseAllocator::AllocateAligned( u32 size, u32 alignment )
    {
        RED_MEMORY_ASSERT( IsInitialized(), "WwiseAllocator was not initialized." );

        Block block = NullBlock();

        size = size > 1u ? size : 1u;

        if ( size > c_slabMaxAllocSize  && size <= c_wwiseAllocatorMaxTLSFSize )
        {
            block = m_tlsfAllocator.AllocateAligned( size, alignment );
        }

		if ( !block.address )
		{
			const Bool canUseDefaultAllocator = size <= red::memory::DefaultAllocator::GetMaxAllocationSize();
			if ( canUseDefaultAllocator )
			{
				if( !s_isThreadRegistered )
				{
					s_isThreadRegistered = true;
					red::memory::RegisterCurrentThread( "WWise" );
				}

				block = m_defaultAllocator->AllocateAligned( size, alignment );
			}
			else
			{
				block = m_bigSizeAllocator.AllocateAligned( size, alignment );
			}
		}

        return block;
    }

    Block WwiseAllocator::Reallocate( Block & block, u32 size )
    {
        if ( size == 0 )
        {
            Free( block );
            return NullBlock();
        }

        if ( block.address == 0 )
        {
            return Allocate( size );
        }

        if ( m_tlsfAllocator.OwnBlock( block.address ) )
        {
            return ReallocateFromTLSFAllocator( block, size );
        }

		if ( m_bigSizeAllocator.OwnBlock( block.address ) )
		{
			ALWAYSENABLED_RED_FATAL( "Those allocations are not expected to ever need reallocation." );
		}

        return m_defaultAllocator->Reallocate( block, size );
    }

    Block WwiseAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
    {
        RED_MEMORY_ASSERT( IsAligned( block.address, alignment ), "Cannot Reallocate a block on different alignment." );

        if ( size == 0 )
        {
            Free( block );
            return NullBlock();
        }

        if ( block.address == 0 )
        {
            return AllocateAligned( size, alignment );
        }

        if ( m_tlsfAllocator.OwnBlock( block.address ) )
        {
            return ReallocateFromTLSFAllocator( block, size, alignment );
        }

		if ( m_bigSizeAllocator.OwnBlock( block.address ) )
		{
			ALWAYSENABLED_RED_FATAL( "Those allocations are not expected to ever need reallocation." );
		}

        return m_defaultAllocator->ReallocateAligned( block, size, alignment );
    }

    Block WwiseAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size )
    {
        Block newBlock = NullBlock();

        if ( size > c_slabMaxAllocSize && size <= c_wwiseAllocatorMaxTLSFSize )
        {
            newBlock = m_tlsfAllocator.Reallocate( block, size );
        }

        if ( !newBlock.address )
        {
			const Bool canUseDefaultAllocator = size <= red::memory::DefaultAllocator::GetMaxAllocationSize();
			if ( canUseDefaultAllocator )
			{
				newBlock = m_defaultAllocator->Allocate( size );
			}
			else
			{
				newBlock = m_bigSizeAllocator.Allocate( size );
			}

            if ( newBlock.address )
            {
                MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
            }
        }

        return newBlock;
    }

    Block WwiseAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size, u32 alignment )
    {
        Block newBlock = NullBlock();

        if ( size > c_slabMaxAllocSize && size <= c_wwiseAllocatorMaxTLSFSize )
        {
            newBlock = m_tlsfAllocator.ReallocateAligned( block, size, alignment );
        }

        if ( !newBlock.address )
        {
			const Bool canUseDefaultAllocator = size <= red::memory::DefaultAllocator::GetMaxAllocationSize();
			if ( canUseDefaultAllocator )
			{
				newBlock = m_defaultAllocator->AllocateAligned( size, alignment );
			}
			else
			{
				newBlock = m_bigSizeAllocator.AllocateAligned( size, alignment );
			}

            if ( newBlock.address )
            {
                MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
            }
        }

        return newBlock;
    }

    void WwiseAllocator::Free( Block & block )
    {
        if ( m_tlsfAllocator.OwnBlock( block.address ) )
        {
            m_tlsfAllocator.Free( block );
        }
        else if ( m_bigSizeAllocator.OwnBlock( block.address ) )
        {
			m_bigSizeAllocator.Free( block );
        }
		else
		{
			m_defaultAllocator->Free( block );
		}
    }

    bool WwiseAllocator::OwnBlock( u64 block ) const
    {
        return m_tlsfAllocator.OwnBlock( block ) || m_bigSizeAllocator.OwnBlock( block );
    }

    u64 WwiseAllocator::GetBlockSize( u64 address ) const
    {
        if ( m_tlsfAllocator.OwnBlock( address ) )
        {
            return GetTLSFBlockSize( address );
        }
		else if ( m_bigSizeAllocator.OwnBlock( address ) )
		{
			return m_bigSizeAllocator.GetBlockSize( address );
		}
        
        return m_defaultAllocator->GetBlockSize( address );
    }

    void WwiseAllocator::BuildMetrics( WwiseAllocatorMetrics & metrics )
    {
        AllocatorMetrics & defaultMetric = metrics.metrics;
        TLSFAllocatorMetrics & tlsfMetric = metrics.tlsfAllocatorMetrics;
		BigSizeAllocatorMetrics & bigSizeMetric = metrics.bigSizeAllocatorMetrics;

        m_tlsfAllocator.BuildMetrics( tlsfMetric );
		m_bigSizeAllocator.BuildMetrics( bigSizeMetric );

        defaultMetric.consumedSystemMemoryBytes = tlsfMetric.metrics.consumedSystemMemoryBytes + bigSizeMetric.metrics.consumedSystemMemoryBytes;
        defaultMetric.consumedMemoryBytes = tlsfMetric.metrics.consumedMemoryBytes + bigSizeMetric.metrics.consumedMemoryBytes;
        defaultMetric.bookKeepingBytes = tlsfMetric.metrics.bookKeepingBytes + bigSizeMetric.metrics.bookKeepingBytes;
    }

    void WwiseAllocator::SerializeMetrics( Serializer & serializer )
    {
        WwiseAllocatorMetrics metrics;
        Memzero( &metrics, sizeof( metrics ) );
        BuildMetrics( metrics );

        SerializeAllocatorIdentifiers( this, serializer );
		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
    }

    bool WwiseAllocator::IsInitialized() const
    {
        return m_systemAllocator != nullptr && m_defaultAllocator != nullptr;
    }

    WwiseAllocator & AcquireWwiseAllocator()
    {
        return AcquireVault().GetWwiseAllocator();
    }
}
}