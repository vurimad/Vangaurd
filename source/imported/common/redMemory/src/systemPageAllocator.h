/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_H_
#define _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_H_

#include "../include/virtualRange.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace red
{
namespace memory
{
	class SystemOOMHandler;

	class RED_MEMORY_API SystemPageAllocator
	{
	public:
		
		void Initialize( SystemOOMHandler* oomHandler );

		RED_MOCKABLE VirtualRange ReserveRange( u64 size, u32 flags );
		RED_MOCKABLE VirtualRange ReserveAlignedRange( u64 size, u32 flags, u32 alignment );
		RED_MOCKABLE void ReleaseRange( const VirtualRange & range );

		RED_MOCKABLE u32 GetPageSize() const;
		u32 GetReservedPageCount() const;

	protected:

		SystemPageAllocator();
		virtual ~SystemPageAllocator();

		void SetPageSize( u32 pageSize );

	private:
		SystemPageAllocator( const SystemPageAllocator & );
		const SystemPageAllocator & operator=( const SystemPageAllocator & );

		virtual void OnInitialize() = 0;
		virtual VirtualRange OnReserveRange( u64 size, u32 pageSize, u32 flags ) = 0;
		virtual VirtualRange OnReserveAlignedRange( u64 size, u32 pageSize, u32 flags, u32 alignment ) = 0; 
		virtual void OnReleaseRange( const VirtualRange & range ) = 0;
	
		u32 m_platformPageSize;
		atomic::TAtomic32 m_pageReservedCount;
		SystemOOMHandler* m_oomHandler;
	};
}
}

#endif
