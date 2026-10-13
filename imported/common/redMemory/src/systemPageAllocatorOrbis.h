/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_ORBIS_H_
#define _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_ORBIS_H_

#include "systemPageAllocator.h"
#include "mutex.h"
#include "../include/utils.h"

namespace red
{
namespace memory
{
	class SystemPageAllocatorOrbis : public SystemPageAllocator
	{
	public:
		
		typedef SimpleArray< VirtualRange, 512 > FreeRangesContainer;

		SystemPageAllocatorOrbis();
		virtual ~SystemPageAllocatorOrbis();

		FreeRangesContainer::const_iterator InternalFreeRangesBegin() const;
		FreeRangesContainer::const_iterator InternalFreeRangesEnd() const;

	private:

		virtual void OnInitialize() override final;
		virtual VirtualRange OnReserveRange( u64 size, u32 pageSize, u32 flags ) override final;
		virtual VirtualRange OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment ) override final;
		virtual void OnReleaseRange( const VirtualRange & range ) override final;
	
		FreeRangesContainer m_freeRanges;
		u32 m_freeRangeCount;
		Mutex m_lock;
	};
}
}

#endif
