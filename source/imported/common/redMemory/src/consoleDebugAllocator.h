/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ORBIS_DEBUG_ALLOCATOR_H_
#define _RED_MEMORY_ORBIS_DEBUG_ALLOCATOR_H_

#include "allocator.h"
#include "../include/block.h"
#include "bigSizeAllocator.h"

namespace red
{
namespace memory
{
	class DefaultAllocator;
	class SystemAllocator;

	struct ConsoleDebugAllocatorMetrics
	{};

	class ConsoleDebugAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( ConsoleDebugAllocator, ConsoleDebugAllocatorMetrics, 8 );

		ConsoleDebugAllocator();
		~ConsoleDebugAllocator();

		void Initialize( DefaultAllocator * defaultAllocator, SystemAllocator* systemAllocator );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

	private:

		RED_ALIGN( 64 ) DefaultAllocator * m_defaultAllocator;
		BigSizeAllocator m_bigSizeAllocator;
	};

}
}

#endif
