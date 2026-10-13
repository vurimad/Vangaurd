/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SYSTEM_ALLOCATOR_ORBIS_H_
#define _RED_MEMORY_SYSTEM_ALLOCATOR_ORBIS_H_

#include "../include/systemAllocator.h"

namespace red
{
namespace memory
{
	class SystemAllocatorOrbis : public SystemAllocator
	{
	public:

		SystemAllocatorOrbis();
		virtual ~SystemAllocatorOrbis();

	private:
		virtual void OnInitialize() override final;
		virtual u64 OnReleaseVirtualRange( const VirtualRange & block ) override final;
		virtual SystemBlock OnCommit( const SystemBlock & block, u32 flags ) override final;
		virtual SystemBlock OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment ) override final;
		virtual void OnDecommit( const SystemBlock & block ) override final;
		virtual void OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock ) override final;
		virtual u64 OnGetTotalPhysicalMemoryAvailable() const override final;
		virtual u64 OnGetCurrentPageMemoryAvailable() const override final;
		virtual u64 OnGetPageSize() const override final;
		virtual void OnWriteReportToLog() const override final;
		virtual void OnWriteReportToJson( FILE* file ) const override final;

		// Internals
		typedef u32 PhysicalPageID;
		struct PhysicalPageIDListNode
		{
			PhysicalPageID id;
			u32 next;
		};

		u32 VirtualAddressToVirtualPageIdx( u64 addr ) const;

		void GrabFreePhysicalPages( u32 firstVirtualPageIdx, const u32 count, PhysicalPageID* outIDs );
		u32 ReturnPhysicalPages( u32 firstVirtualPageIdx, const u32 count, PhysicalPageID* outReturnedIDs );

		u64 m_totalDirectMemoryAvailableInTheSystemAtStartup;		// Memory available in the system during initialization

		// ---- Virtual allocation props start here

		VirtualRange m_virtualRangeSupported;	// The range reserved by system, the one to release when we're done
		VirtualRange m_bookkeepingListVirtualRange;

		const PhysicalPageID PhysicalPageIDInvalid = 0xFFFFFFFF;	// Denotes unused page
		off_t m_physicalPoolAddress;	// Address of the whole physical memory allocation, made on initialization.
		u64 m_physicalBudget;			// Overall physical budget, committed right away during init
		u64 m_numPhysicalPages;			// Number of physical pages
		u64 m_pageSize;					// Size of the page (both physical and virtual)
		u64 m_numVirtualPages;			// Overall number of virtual pages available.
		u32 m_flags;					// Flags...

		PhysicalPageID* m_virtualPageToPhysicalPage;	// Under an index of a virtual page, there is an ID of a physical page assigned to it
		PhysicalPageID* m_freePhysicalPageIDs;			// Physical pages that are free for the taking
		i64  m_numFreePhysicalPages;			// Num physical pages that are free for the taking

		// These can help us figure out from memory dump, that we run out of page table
		// If we never failed a kernelMap so far, those will remain unused (-1)
		struct PageTableStats
		{
			i32 cpuTotal = -1;
			i32 cpuAvailable = -1;
			i32 gpuTotal = -1;
			i32 gpuAvailable = -1;
		} mutable m_lastPageTableStats;

		constexpr static u32 cMaxNumPagesInOneBatchMap = 128;
		SceKernelBatchMapEntry	m_tempMapEntries[cMaxNumPagesInOneBatchMap * 2];
		PhysicalPageID			m_tempPhysicalPageIDs[cMaxNumPagesInOneBatchMap];

		mutable Mutex m_lock;
	};
}
}

#endif
