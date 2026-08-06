/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_DYNAMIC_STACK_ALLOCATOR_H_
#define _RED_MEMORY_DYNAMIC_STACK_ALLOCATOR_H_

#include "../include/utils.h"
#include "../include/virtualRange.h"
#include "../../redSystem/include/utility.h"

#include "allocatorMetrics.h"
#include "allocator.h"
#include "stackAllocator.h"

namespace red
{
	namespace memory
	{
		class SystemAllocator;
		class Serializer;

		struct RED_MEMORY_API DynamicStackAllocatorParameter
		{
			SystemAllocator* systemAllocator;
			u32 chunkSize;
			u32 flags;
		};

		class RED_MEMORY_API DynamicStackAllocator : NonCopyable
		{
		public:

			RED_MEMORY_DECLARE_ALLOCATOR( DynamicStackAllocator, StackAllocatorMetrics, 8 );

			DynamicStackAllocator();
			~DynamicStackAllocator();

			void Initialize(const DynamicStackAllocatorParameter& parameter);
			void Uninitialize();

			Block Allocate(u32);
			Block AllocateAligned(u32 size, u32 alignment);
			void Free(Block& block);
			Block Reallocate(Block& block, u32 size);
			Block ReallocateAligned(Block& block, u32 size, u32 alignment);

			bool OwnBlock(u64 block) const;
			u64 GetBlockSize(u64 block) const;

			void Reset();

			void BuildMetrics( StackAllocatorMetrics & metrics );
			void SerializeMetrics( Serializer & serializer );

		private:

			SystemBlock AllocateBlock(u32 size);
			bool CanCreateMoreBlock() const;

			RED_ALIGN(64) StackAllocator m_allocator;
			u32 m_chunkSize;
			VirtualRange m_virtualRange;
			u64 m_nextVirtualAddress;
			SystemAllocator* m_systemAllocator;
			u32 m_flags;
			u64 m_minimalAllocationSize;
		};
	}
}

#endif