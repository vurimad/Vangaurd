/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_GPU_ALLOCATOR_ORBIS_H_
#define RED_MEMORY_GPU_ALLOCATOR_ORBIS_H_

#include "allocator.h"
#include "allocatorMetrics.h"
#include "lockingDynamicBuddyAllocator.h"
#include "bigSizeAllocator.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	class SystemAllocator;
	class Serializer;

	const u32 c_minBuddyAllocatorAlignmentPowerOf2 = 4;
	const u32 c_maxBuddyAllocatorAlignmentPowerOf2 = 16;
	const u32 c_minBuddyAllocatorAlignment = 1 << c_minBuddyAllocatorAlignmentPowerOf2;
	const u32 c_maxBuddyAllocatorAlignment = 1 << c_maxBuddyAllocatorAlignmentPowerOf2;
	const u32 c_maxNumBuddyAllocators = c_maxBuddyAllocatorAlignmentPowerOf2 - c_minBuddyAllocatorAlignmentPowerOf2 + 1; // each buddy allocator has different alignment in range [1 << 4; 1 << 16]

	struct BuddyAllocatorData
	{
		u32 maxBudget;
		u32 initBudget;
		u32 alignment;
	};

	struct GpuAllocatorOrbisMetrics
	{};

	struct GpuAllocatorOrbisParamater
	{
		SystemAllocator* systemAllocator;
		SimpleArray< BuddyAllocatorData, c_maxNumBuddyAllocators > buddyAllocatorData;
		u64 bigSizeAllocatorBudget;
		u32 flags;
	};

	class RED_MEMORY_API GpuAllocatorOrbis
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( GpuAllocatorOrbis, GpuAllocatorOrbisMetrics, 16 );

		GpuAllocatorOrbis();
		~GpuAllocatorOrbis();

		void Initialize( const GpuAllocatorOrbisParamater& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

	private:
		SimpleArray< LockingDynamicBuddyAllocator, c_maxNumBuddyAllocators > m_buddyAllocators;
		BigSizeAllocator m_bigSizeAllocator;
	};
}
}

#endif