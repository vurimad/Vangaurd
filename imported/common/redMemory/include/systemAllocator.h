/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SYSTEM_ALLOCATOR_H_
#define _RED_MEMORY_SYSTEM_ALLOCATOR_H_

#include "systemBlock.h"
#include "virtualRange.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace red
{
namespace memory
{
	class SystemPageAllocator;
	class SystemOOMHandler;

	class RED_MEMORY_API SystemAllocator
	{
	public:

		SystemAllocator();
		virtual ~SystemAllocator();

		void Initialize( SystemPageAllocator * pageAllocator, SystemOOMHandler* oomHandler );

		RED_MOCKABLE VirtualRange ReserveVirtualRange( u64 size, u32 flags );
		RED_MOCKABLE VirtualRange ReserveAlignedVirtualRange( u64 size, u32 flags, u32 alignment );
		RED_MOCKABLE void ReleaseVirtualRange( const VirtualRange & range );
		
		RED_MOCKABLE SystemBlock Commit( const SystemBlock & block, u32 flags );
		RED_MOCKABLE SystemBlock CommitAligned( const SystemBlock & block, u32 flags, u32 alignment );

		RED_MOCKABLE void Decommit( const SystemBlock & block );
		RED_MOCKABLE void PartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock );

		u64 GetTotalPhysicalMemoryAvailable() const;
		u64 GetCurrentPhysicalMemoryAvailable() const;
		u64 GetCurrentPageMemoryAvailable() const;

		u64 GetPageSize() const;

		void WriteReportToLog() const;
		void WriteReportToJson( FILE* file ) const;

	private:
		SystemAllocator( const SystemAllocator& );
		SystemAllocator& operator=( const SystemAllocator& );

		virtual void OnInitialize() = 0;
		virtual u64 OnReleaseVirtualRange( const VirtualRange & block ) = 0;
		virtual SystemBlock OnCommit( const SystemBlock & block, u32 flags ) = 0;
		virtual SystemBlock OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment ) = 0;
		virtual void OnDecommit( const SystemBlock & block ) = 0;
		virtual void OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock ) = 0;
		virtual u64 OnGetTotalPhysicalMemoryAvailable() const = 0;
		virtual u64 OnGetCurrentPageMemoryAvailable() const = 0;
		virtual u64 OnGetPageSize() const = 0;
		virtual void OnWriteReportToLog() const = 0;
		virtual void OnWriteReportToJson( FILE* file ) const = 0;

	protected:
		atomic::TAtomic64 m_commitedMemoryInBytes;
		SystemPageAllocator * m_pageAllocator;
		SystemOOMHandler* m_oomHandler;
	};
}
}

#endif
