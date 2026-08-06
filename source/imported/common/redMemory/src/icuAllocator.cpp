/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "icuAllocator.h"
#include "assert.h"
#include "tlsfBlock.h"
#include "vault.h"
#include "flags.h"

namespace red
{
namespace memory
{
    const u64 c_icuAllocatorTLSFVirtualRange = RED_GIGA_BYTE( 1 );
	const u32 c_icuAllocatorTLSFInitialAllocSize =  RED_MEGA_BYTE( 1 );
	const u32 c_icuAllocatorTLSFChunkSize = RED_MEGA_BYTE( 1 );
    const u32 c_icuAllocatorVirtualRangeAlignment = 0;
    const u32 c_icuAllocatorMaxTLSFSize = RED_MEGA_BYTE( 2 );

    ICUAllocator::ICUAllocator()
        : m_systemAllocator( NULL )
        , m_defaultAllocator( NULL )
	{}

    ICUAllocator::~ICUAllocator()
    {}

    void ICUAllocator::Initialize( const ICUAllocatorParameter & parameter )
    {
        m_systemAllocator = parameter.systemAllocator;

        RED_MEMORY_ASSERT( m_systemAllocator, "ICUAllocator cannot be initialized. Need access to SystemAllocator" );

        m_defaultAllocator = parameter.defaultAllocator;

        RED_MEMORY_ASSERT( m_defaultAllocator, "ICUAllocator cannot be initialized. Need access to DefaultAllocator" );

        DynamicTLSFAllocatorParameter tlsfParameter =
        {
            m_systemAllocator,
            c_icuAllocatorTLSFVirtualRange,
            c_icuAllocatorTLSFInitialAllocSize,
            c_icuAllocatorTLSFChunkSize,
            Flags_CPU_Read_Write,
            c_icuAllocatorVirtualRangeAlignment
        };

        m_tlsfAllocator.Initialize( tlsfParameter );
    }

    void ICUAllocator::Uninitialize()
    {
        m_tlsfAllocator.Uninitialize();
    }

    Block ICUAllocator::Allocate( u32 size )
    {
        RED_MEMORY_ASSERT( IsInitialized(), "ICUAllocator was not initialized." );

        Block block = NullBlock();

        size = size > 1u ? size : 1u;

        if ( size > c_slabMaxAllocSize  && size <= c_icuAllocatorMaxTLSFSize )
        {
            block = m_tlsfAllocator.Allocate( size );
        }

        if ( !block.address )
        {
            block = m_defaultAllocator->Allocate( size );
        }

        return block;
    }

    Block ICUAllocator::AllocateAligned( u32 size, u32 alignment )
    {
        RED_MEMORY_ASSERT( IsInitialized(), "ICUAllocator was not initialized." );

        Block block = NullBlock();

        size = size > 1u ? size : 1u;

        if ( size > c_slabMaxAllocSize  && size <= c_icuAllocatorMaxTLSFSize )
        {
            block = m_tlsfAllocator.AllocateAligned( size, alignment );
        }

        if ( !block.address )
        {
            block = m_defaultAllocator->AllocateAligned( size, alignment );
        }

        return block;
    }

    Block ICUAllocator::Reallocate( Block & block, u32 size )
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

        return m_defaultAllocator->Reallocate( block, size );
    }

    Block ICUAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
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

        return m_defaultAllocator->ReallocateAligned( block, size, alignment );
    }

    Block ICUAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size )
    {
        Block newBlock = NullBlock();

        if ( size > c_slabMaxAllocSize && size <= c_icuAllocatorMaxTLSFSize )
        {
            newBlock = m_tlsfAllocator.Reallocate( block, size );
        }

        if ( !newBlock.address )
        {
            newBlock = m_defaultAllocator->Allocate( size );
            if ( newBlock.address )
            {
                MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
            }
        }

        return newBlock;
    }

    Block ICUAllocator::ReallocateFromTLSFAllocator( Block & block, u32 size, u32 alignment )
    {
        Block newBlock = NullBlock();

        if ( size > c_slabMaxAllocSize && size <= c_icuAllocatorMaxTLSFSize )
        {
            newBlock = m_tlsfAllocator.ReallocateAligned( block, size, alignment );
        }

        if ( !newBlock.address )
        {
            newBlock = m_defaultAllocator->AllocateAligned( size, alignment );
            if ( newBlock.address )
            {
                MemcpyBlock( newBlock.address, block.address, std::min( size, GetTLSFBlockSize( block.address ) ) );
				m_tlsfAllocator.Free( block );
            }
        }

        return newBlock;
    }

    void ICUAllocator::Free( Block & block )
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

    bool ICUAllocator::OwnBlock( u64 block ) const
    {
        return m_tlsfAllocator.OwnBlock( block );
    }

    u64 ICUAllocator::GetBlockSize( u64 address ) const
    {
        if ( m_tlsfAllocator.OwnBlock( address ) )
        {
            return GetTLSFBlockSize( address );
        }
        
        return m_defaultAllocator->GetBlockSize( address );
    }

    void ICUAllocator::BuildMetrics( ICUAllocatorMetrics & metrics )
    {
        AllocatorMetrics & defaultMetric = metrics.metrics;
        TLSFAllocatorMetrics & tlsfMetric = metrics.tlsfAllocatorMetrics;

        m_tlsfAllocator.BuildMetrics( tlsfMetric );

        defaultMetric.consumedSystemMemoryBytes = tlsfMetric.metrics.consumedSystemMemoryBytes;
        defaultMetric.consumedMemoryBytes = tlsfMetric.metrics.consumedMemoryBytes;
        defaultMetric.bookKeepingBytes = tlsfMetric.metrics.bookKeepingBytes;
    }

    void ICUAllocator::SerializeMetrics( Serializer & serializer )
    {
        ICUAllocatorMetrics metrics;
        Memzero( &metrics, sizeof( metrics ) );
        BuildMetrics( metrics );

        SerializeAllocatorIdentifiers( this, serializer );
		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
    }

    bool ICUAllocator::IsInitialized() const
    {
        return m_systemAllocator != NULL && m_defaultAllocator != NULL;
    }

    ICUAllocator & AcquireICUAllocator()
    {
        return AcquireVault().GetICUAllocator();
    }
}
}