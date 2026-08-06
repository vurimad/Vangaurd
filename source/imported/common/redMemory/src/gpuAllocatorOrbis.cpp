/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "gpuAllocatorOrbis.h"
#include "systemAllocator.h"
#include "buddyAllocatorUtils.h"
#include "flags.h"

namespace red
{
namespace memory
{
	const u32 c_gpuAllocatorOrbisMaxBuddySize = RED_MEGA_BYTE( 2 );

	GpuAllocatorOrbis::GpuAllocatorOrbis()
	{}

	GpuAllocatorOrbis::~GpuAllocatorOrbis()
	{
	}

	void GpuAllocatorOrbis::Initialize( const GpuAllocatorOrbisParamater& parameter )
	{
		for ( Uint32 i = 0; i < parameter.buddyAllocatorData.Size(); ++i )
		{
			const auto& data = parameter.buddyAllocatorData[i];
			if ( data.maxBudget == 0 )
			{
				continue;
			}

			RED_MEMORY_ASSERT( data.alignment >= c_minBuddyAllocatorAlignment, "Given alignment is too small" );
			RED_MEMORY_ASSERT( data.alignment <= c_maxBuddyAllocatorAlignment, "Given alignment is too big" );
			u32 index = BuddyILog2( data.alignment ) - c_minBuddyAllocatorAlignmentPowerOf2;
			RED_MEMORY_ASSERT( !m_buddyAllocators[index].IsInitialized(), "Buddy allocator with (%u) alignement is alrady registered", data.alignment );

			DynamicBuddyAllocatorParameter param
			{
				parameter.systemAllocator,
				data.maxBudget,
				data.initBudget,
				data.alignment,
				parameter.flags
			};

			m_buddyAllocators[index].Initialize( param );
		}

		BigSizeAllocatorParameter bigSizeAllocatorParam
		{
			parameter.systemAllocator,
			parameter.bigSizeAllocatorBudget,
			parameter.flags
		};

		m_bigSizeAllocator.Initialize( bigSizeAllocatorParam );
	}

	void GpuAllocatorOrbis::Uninitialize()
	{
		for ( auto& buddyAllocator : m_buddyAllocators )
		{
			buddyAllocator.Uninitialize();
		}

		m_bigSizeAllocator.Uninitialize();
	}

	Block GpuAllocatorOrbis::Allocate( u32 size )
	{
		return AllocateAligned( size, DefaultAlignmentType::value );
	}

	Block GpuAllocatorOrbis::AllocateAligned( u32 size, u32 alignment )
	{
		Block block = NullBlock();

		if ( size < c_gpuAllocatorOrbisMaxBuddySize )
		{
			RED_MEMORY_ASSERT( alignment >= c_minBuddyAllocatorAlignment, "Given alignment is too small" );
			RED_MEMORY_ASSERT( alignment <= c_maxBuddyAllocatorAlignment, "Given alignment is too big" );
			const u32 index = BuddyILog2( alignment ) - c_minBuddyAllocatorAlignmentPowerOf2;
			RED_MEMORY_ASSERT( m_buddyAllocators[index].IsInitialized(), "Buddy allocator with alignment (%u) is not registered.", alignment );
			block = m_buddyAllocators[index].Allocate( size );
		}

		if ( block == NullBlock() )
		{
			block = m_bigSizeAllocator.AllocateAligned( size, alignment );
		}

		return block;
	}

	Block GpuAllocatorOrbis::Reallocate( Block& /*block*/, u32 /*size*/ )
	{
		RED_MEMORY_ASSERT( 0, "Reallocate should never be called" );
		return NullBlock();
	}

	Block GpuAllocatorOrbis::ReallocateAligned( Block& /*block*/, u32 /*size*/, u32 /*alignment*/ )
	{
		RED_MEMORY_ASSERT( 0, "ReallocateAligned should never be called" );
		return NullBlock();
	}

	void GpuAllocatorOrbis::Free( Block& block )
	{
		if ( !block.address )
		{
			return;
		}

		for ( auto& allocator : m_buddyAllocators )
		{
			if ( allocator.IsInitialized() && allocator.OwnBlock( block.address ) )
			{
				allocator.Free( block );
				allocator.ShrinkBuffer();
				return;
			}
		}

		if ( m_bigSizeAllocator.OwnBlock( block.address ) )
		{
			m_bigSizeAllocator.Free( block );
			return;
		}

		RED_MEMORY_ASSERT( 0, "Block (%llu) is not owned by this allocator", block.address );
	}

	bool GpuAllocatorOrbis::OwnBlock( u64 block ) const
	{
		for ( auto& allocator : m_buddyAllocators )
		{
			if ( allocator.IsInitialized() && allocator.OwnBlock( block ) )
			{
				return true;
			}
		}

		return m_bigSizeAllocator.OwnBlock( block ) ;
	}

	u64 GpuAllocatorOrbis::GetBlockSize( u64 block ) const
	{
		for ( auto& allocator : m_buddyAllocators )
		{
			if ( allocator.IsInitialized() && allocator.OwnBlock( block ) )
			{
				return allocator.GetBlockSize( block );
			}
		}

		return m_bigSizeAllocator.GetBlockSize( block ) ;
	}

}
}