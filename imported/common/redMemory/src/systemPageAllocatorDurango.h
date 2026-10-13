/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_DURANGO_H_
#define _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_DURANGO_H_

#include "systemPageAllocator.h"

namespace red
{
namespace memory
{
	class SystemPageAllocatorDurango : public SystemPageAllocator
	{
	public:
		SystemPageAllocatorDurango();
		virtual ~SystemPageAllocatorDurango();

	private:

		virtual void OnInitialize() override final;
		virtual VirtualRange OnReserveRange( u64 size, u32 pageSize, u32 flags ) override final;
		virtual VirtualRange OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment ) override final;
		virtual void OnReleaseRange( const VirtualRange & range ) override final;
	};

}
}

#endif
