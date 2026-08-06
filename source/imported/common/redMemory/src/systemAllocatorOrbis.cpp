/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemAllocatorOrbis.h"
#include "systemAllocatorOrbisHelper.h"
#include "systemPageAllocator.h"
#include "utils.h"
#include "assert.h"
#include "flags.h"
#include "../redSystem/include/unitTestMode.h"
#include <gnm/platform.h>

namespace red
{
namespace memory
{
	// Keeping that as low as possible, makes bookkeeping cheaper
	// think about how many entries you get when you divide that size by page size 
	// If we use the full user space (1.2TB) it would cost a 100MB in bookkeeping.
	const u64 c_virtualSpaceSupported = RED_GIGA_BYTE( 128 ); 

	// Physical page size. Keep it in sync with other files where that size is supplied
	// The actual physical page for CPU on PS4 is 16KB, but the cost of going 64KB is minimal (given we don't ask system for small allocations directly)
	// Going with a larger page size reduces bookkeeping cost.
	const u64 c_physicalPageSize = RED_KILO_BYTE( 64 );



	SystemAllocatorOrbis::SystemAllocatorOrbis()
		: m_totalDirectMemoryAvailableInTheSystemAtStartup( 0 )
		, m_virtualRangeSupported( NullVirtualRange() )
		, m_bookkeepingListVirtualRange( NullVirtualRange() )
		, m_physicalPoolAddress( 0 )
		, m_physicalBudget( 0 )
		, m_numPhysicalPages( 0 )
		, m_pageSize( 0 )
		, m_numVirtualPages( 0 )
		, m_flags( 0 )
		, m_virtualPageToPhysicalPage( nullptr )
		, m_freePhysicalPageIDs( nullptr )
		, m_numFreePhysicalPages( 0 )
	{}

	SystemAllocatorOrbis::~SystemAllocatorOrbis()
	{
		if( m_pageAllocator )
		{
			m_pageAllocator->ReleaseRange( m_bookkeepingListVirtualRange );
		}

		if( m_bookkeepingListVirtualRange != NullVirtualRange() )
		{
			internal::DecommitRange( m_bookkeepingListVirtualRange );	
		}
		
		if( m_physicalPoolAddress )
		{
			sceKernelReleaseDirectMemory( m_physicalPoolAddress, m_physicalBudget );
		}
		
	}

	void SystemAllocatorOrbis::OnInitialize()
	{
		// For starters, find out how much memory we have available in the whole PS4 system
		{
			off_t offset = 0;
			i32 result = sceKernelAvailableDirectMemorySize( 0, SCE_KERNEL_MAIN_DMEM_SIZE, 0, &offset, &m_totalDirectMemoryAvailableInTheSystemAtStartup );
			RED_MEMORY_ASSERT( result == SCE_OK, "SYSTEM ERROR cannot fetch Orbis available memory." );
			RED_UNUSED( result );
		}

		// Establish some basic numbers for this allocator operations
		m_pageSize = c_physicalPageSize;
		m_numVirtualPages = c_virtualSpaceSupported / c_physicalPageSize;

		const u32 ContigiuousMemoryBudgetToLeaveOut = ( sce::Gnm::getGpuMode() == sce::Gnm::kGpuModeNeo ) ? RED_MEGA_BYTE( 40 ) : RED_MEGA_BYTE( 26 );
		m_physicalBudget = UnitTestMode() ? RED_MEGA_BYTE( 512 ) : (u64)Max( (i64)m_totalDirectMemoryAvailableInTheSystemAtStartup - ContigiuousMemoryBudgetToLeaveOut, (i64)0 );
		m_numPhysicalPages = m_physicalBudget / m_pageSize;
		m_virtualRangeSupported = { SCE_KERNEL_APP_MAP_AREA_START_ADDR, SCE_KERNEL_APP_MAP_AREA_START_ADDR + c_virtualSpaceSupported };

		RED_MEMORY_ASSERT( m_physicalBudget > 0, "No available memory" );

		// Allocate and initialize bookkeeping structures
		{
			// Allocate
			const u32 VirtualToPhysicalPage_Size = m_numVirtualPages * sizeof( PhysicalPageID );
			const u32 FreePhysicalPageIDs_Size = m_numPhysicalPages * sizeof( PhysicalPageID );
			const u32 OverallBookkeepingMemorySize = VirtualToPhysicalPage_Size + FreePhysicalPageIDs_Size;
			m_bookkeepingListVirtualRange = m_pageAllocator->ReserveRange( OverallBookkeepingMemorySize, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );
			const SystemBlock block = internal::CommitBlockToDirectMemory( { m_bookkeepingListVirtualRange.start, OverallBookkeepingMemorySize }, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );

			m_virtualPageToPhysicalPage = reinterpret_cast<PhysicalPageID*>( block.address );
			m_freePhysicalPageIDs = reinterpret_cast<PhysicalPageID*>( block.address + VirtualToPhysicalPage_Size );

			// --- Initialize
			{
				// Reset all ids to Invalid. All memory is free.
				red::Memset( m_virtualPageToPhysicalPage, 0xFF, VirtualToPhysicalPage_Size );

				// Throw all physical pages to the free queue
				for ( u32 i = 0; i < m_numPhysicalPages; ++i )
				{
					m_freePhysicalPageIDs[i] = m_numPhysicalPages - i - 1;
				}
				m_numFreePhysicalPages = m_numPhysicalPages;
			}
		}

		// Commit all the available direct memory right away.
		{
			i32 result = sceKernelAllocateDirectMemory(
				0,							// Search from the beginning ...
				SCE_KERNEL_MAIN_DMEM_SIZE,	// till the end of entire physical memory space
				m_physicalBudget,			// Allocate the whole pool budget at once
				m_pageSize,					// Align to our page size
				m_flags & Flags_Garlic_Bus ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION,
				&m_physicalPoolAddress		// Our output address, beginning of the physical space we can use
			);
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to allocate the physical space for VirtualGPU allocator." );
		}
	}

	u64 SystemAllocatorOrbis::OnReleaseVirtualRange( const VirtualRange & range )
	{
		SystemBlock blockToDecommit = { range.start, range.end - range.start };
		const i32 numPhysicalPagesRequired = blockToDecommit.size / m_pageSize;

		RED_SCOPE_LOCK( m_lock );

		// Perform actual kernelBatchMap (to unmap :P)
		// If allocation requires HUGE amount of pages to supply, then we don't want to have arbitrarily large batcMapArrays - hence let's do a bunch of map batches
		const u32 numMapBatchesRequired = ( numPhysicalPagesRequired + cMaxNumPagesInOneBatchMap - 1 ) / cMaxNumPagesInOneBatchMap;
		u32 numPagesLeft = numPhysicalPagesRequired;
		for ( u32 b = 0; b < numMapBatchesRequired; ++b )
		{
			const u64 batchAddress = blockToDecommit.address + b * cMaxNumPagesInOneBatchMap * m_pageSize;
			const u32 numPagesInBatch = Min( numPagesLeft, cMaxNumPagesInOneBatchMap );
			const u32 firstVirtualPageId = VirtualAddressToVirtualPageIdx( batchAddress );

			// Return physical pages for free bookkeeping and unmap
			Uint32 numMapEntriesProduced = 0;

			for ( u32 i = 0; i < numPagesInBatch; ++i )
			{
				const u32 virtualPageIdx = firstVirtualPageId + i;
				const PhysicalPageID physicalPageId = m_virtualPageToPhysicalPage[virtualPageIdx];

				if ( physicalPageId != PhysicalPageIDInvalid )
				{
					// Page in use, free it and add unmap entry
					m_virtualPageToPhysicalPage[virtualPageIdx] = PhysicalPageIDInvalid;
					m_freePhysicalPageIDs[m_numFreePhysicalPages++] = physicalPageId;

					SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
					mapEntry.start = (void*)( batchAddress + (u64)i * m_pageSize );
					mapEntry.length = m_pageSize;
					mapEntry.operation = SCE_KERNEL_MAP_OP_UNMAP;
				}
			}

			i32 entriesSuccessfullyUnmapped = 0;
			i32 result = sceKernelBatchMap2( m_tempMapEntries, numMapEntriesProduced, &entriesSuccessfullyUnmapped, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE );
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to perform kernelBatchMap." );
			RED_MEMORY_ASSERT( entriesSuccessfullyUnmapped == numMapEntriesProduced, "Not all pages were succesfully mapped." );

			numPagesLeft -= numPagesInBatch;
		}

		return blockToDecommit.size;
	}
	
	SystemBlock SystemAllocatorOrbis::OnCommit( const SystemBlock & block, u32 flags )
	{
		return OnCommitAligned( block, flags, m_pageSize );
	}

	SystemBlock SystemAllocatorOrbis::OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Alignment has to be power of 2." );
		RED_MEMORY_ASSERT( alignment >= m_pageSize, "Alignment has to be greater or equal to page size." );

		const u64 sizeRoundedToPageSize = RoundUp( block.size, m_pageSize );
		const u64 alignedAddress = AlignAddress( block.address, alignment );
		void * address = reinterpret_cast<void *>( alignedAddress );
		const Bool isGarlic = flags & Flags_Garlic_Bus;
		const u32 protectFlag = internal::ComputePageProtectionFlags( flags );
		const i32 numPhysicalPagesRequired = sizeRoundedToPageSize / m_pageSize;

		if ( block.address < m_virtualRangeSupported.start || block.address + block.size > m_virtualRangeSupported.end )
		{
			// address outside of supported range!
			return NullSystemBlock();
		}

		RED_SCOPE_LOCK( m_lock );

		if ( numPhysicalPagesRequired > m_numFreePhysicalPages )
		{
			// OOM
			return NullSystemBlock();
		}

		// If allocation requires HUGE amount of pages to supply, then we don't want to have arbitrarily large batcMapArrays - hence let's do a bunch of map batches
		const u32 numMapBatchesRequired = ( numPhysicalPagesRequired + cMaxNumPagesInOneBatchMap - 1 ) / cMaxNumPagesInOneBatchMap;
		u32 numPagesLeft = numPhysicalPagesRequired;
		for ( u32 b = 0; b < numMapBatchesRequired; ++b )
		{
			const u64 batchAddress = alignedAddress + b * cMaxNumPagesInOneBatchMap * m_pageSize;
			const u32 numPagesInBatch = Min( numPagesLeft, cMaxNumPagesInOneBatchMap );
			const u32 firstVirtualPageId = VirtualAddressToVirtualPageIdx( batchAddress );
			
			// Prepare sufficient amount of physical pages, assign them to virtual pages too.
			GrabFreePhysicalPages( firstVirtualPageId, numPagesInBatch, m_tempPhysicalPageIDs );

			// Perform actual kernelBatcHMap
			Uint32 numMapEntriesProduced = 0;
			{
				for ( u32 i = 0; i < numPagesInBatch; ++i )
				{
					const PhysicalPageID currPhysicalPageID = m_tempPhysicalPageIDs[i];

					// 1/2: Perform actual mapping between virtual and physical page.
					// NOTE: only a subset of MapEntry fields are used, based on what .operation is selected. Hence I don't fill all of them.
					{
						SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
						mapEntry.start = (void*)( batchAddress + (u64)( i * m_pageSize ) );
						mapEntry.offset = m_physicalPoolAddress + (u64)currPhysicalPageID * m_pageSize;
						mapEntry.length = m_pageSize;
						mapEntry.protection = protectFlag;
						mapEntry.type = isGarlic ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION;
						mapEntry.operation = SCE_KERNEL_MAP_OP_MAP_DIRECT;
					}

#define USE_BATCHED_KERNEL_MAP	// Option of NOT batching is here just so we can debug specific failed pages

#ifdef USE_BATCHED_KERNEL_MAP
					// 2/2: Since we don't perform an actual sceKernelAllocate, we need to make sure the memory will be mapped to proper bus
					// Would be way nicer, to do that within the MAP_DIRECT op above, BUT ...
					// ... sadly, when MAP_DIRECT op is used, the "type" flag is being ignored, and we have to make additional map call with TYPE_PROTECT op explicitly :(
					{
						SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
						mapEntry.start = (void*)( batchAddress + (u64)( i * m_pageSize ) );
						mapEntry.length = m_pageSize;
						mapEntry.protection = protectFlag;
						mapEntry.type = isGarlic ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION;
						mapEntry.operation = SCE_KERNEL_MAP_OP_TYPE_PROTECT;
					}
#endif
				}

				i32 entriesSuccessfullyMapped = 0;

				i32 mapResult = -1;

#ifdef USE_BATCHED_KERNEL_MAP
				// Do that in a batch
				mapResult = sceKernelBatchMap2( m_tempMapEntries, numMapEntriesProduced, &entriesSuccessfullyMapped, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE );
#else		
				// One by one, so we can actually debug
				for ( u32 i = 0; i < numMapEntriesProduced; ++i )
				{
					auto& entry = m_tempMapEntries[i];
					mapResult = sceKernelMapNamedDirectMemory( (void**)&entry.start, entry.length, protectFlag, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE | SCE_KERNEL_MAP_NO_OVERWRITE, entry.offset, 64 * 1024, "DUPA\0" );
					if ( mapResult != SCE_OK )
					{
						SceKernelVirtualQueryInfo queryInfo;
						const i32 queryResult = sceKernelVirtualQuery( entry.start, SCE_KERNEL_MAP_NO_COALESCE | SCE_KERNEL_VQ_FIND_NEXT, &queryInfo, sizeof( SceKernelVirtualQueryInfo ) );
						RED_TOUCH( queryResult );
						break;
					}
				}
#endif
				
				if ( mapResult != SCE_OK )
				{
					// Ok, so we failed to allocate after all. Store additional info to make the memory dump as useful as possible.
					// NOTE: The state of the allocator will not be safe for further usage afterwards. We can't be sure which pages were mapped successfully and which weren't.
					// NOTE2: It is possible to make it so, but not really worth it. Current norm is that we crash on any OOM.
					sceKernelGetPageTableStats( &m_lastPageTableStats.cpuTotal, &m_lastPageTableStats.cpuAvailable, &m_lastPageTableStats.gpuTotal, &m_lastPageTableStats.gpuAvailable );
					RED_MEMORY_ASSERT( mapResult == SCE_OK, "Failed to perform kernelBatchMap." );
					return NullSystemBlock();
				}
			}

			numPagesLeft -= numPagesInBatch;
		}
		RED_MEMORY_ASSERT( numPagesLeft == 0, "Something wasn't finished" );

		SystemBlock retBlock = { alignedAddress, sizeRoundedToPageSize };
		return retBlock;
	}
	
	void SystemAllocatorOrbis::OnDecommit( const SystemBlock & block )
	{
		const i32 numPhysicalPagesRequired = block.size / m_pageSize;

		RED_SCOPE_LOCK( m_lock );

		// Perform actual kernelBatchMap (to unmap :P)
		// If allocation requires HUGE amount of pages to supply, then we don't want to have arbitrarily large batcMapArrays - hence let's do a bunch of map batches
		const u32 numMapBatchesRequired = ( numPhysicalPagesRequired + cMaxNumPagesInOneBatchMap - 1 ) / cMaxNumPagesInOneBatchMap;
		u32 numPagesLeft = numPhysicalPagesRequired;
		for ( u32 b = 0; b < numMapBatchesRequired; ++b )
		{
			const u64 batchAddress = block.address + b * cMaxNumPagesInOneBatchMap * m_pageSize;
			const u32 numPagesInBatch = Min( numPagesLeft, cMaxNumPagesInOneBatchMap );
			const u32 firstVirtualPageId = VirtualAddressToVirtualPageIdx( batchAddress );

			// Return physical pages for free bookkeeping and unmap
			const u32 numPhysicalPagesReturned = ReturnPhysicalPages( firstVirtualPageId, numPagesInBatch, m_tempPhysicalPageIDs );
			Uint32 numMapEntriesProduced = 0;

			for ( u32 i = 0; i < numPhysicalPagesReturned; ++i )
			{
				SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
				mapEntry.start = (void*)( batchAddress + (u64)i * m_pageSize );
				mapEntry.length = m_pageSize;
				mapEntry.operation = SCE_KERNEL_MAP_OP_UNMAP;
			}

			i32 entriesSuccessfullyUnmapped = 0;
			i32 result = sceKernelBatchMap2( m_tempMapEntries, numMapEntriesProduced, &entriesSuccessfullyUnmapped, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE );
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to perform kernelBatchMap." );
			RED_MEMORY_ASSERT( entriesSuccessfullyUnmapped == numPhysicalPagesReturned, "Not all pages were succesfully mapped." );

			numPagesLeft -= numPagesInBatch;
		}
		RED_MEMORY_ASSERT( numPagesLeft == 0, "Something wasn't finished" );
	}

	void SystemAllocatorOrbis::OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		RED_MEMORY_ASSERT( partialBlock.address >= block.address && partialBlock.address + partialBlock.size <= block.address + block.size, "Partial block needs to fit in to given block." );

		RED_SCOPE_LOCK( m_lock );

		const u64 alignedAddressStart = RoundUp( partialBlock.address, m_pageSize );
		const u64 alignedAddressEnd = RoundUp( partialBlock.address + partialBlock.size, m_pageSize );

		OnReleaseVirtualRange( { alignedAddressStart, alignedAddressEnd } );
	}

	u32 SystemAllocatorOrbis::VirtualAddressToVirtualPageIdx( u64 addr ) const
	{
		RED_MEMORY_ASSERT( addr >= m_virtualRangeSupported.start && addr <= m_virtualRangeSupported.end, "address outside of supported range!" );

		const u32 virtualPageId = ( addr - m_virtualRangeSupported.start ) / m_pageSize;
		RED_MEMORY_ASSERT( virtualPageId < m_numVirtualPages, "!!" );

;		return virtualPageId;
	}

	void SystemAllocatorOrbis::GrabFreePhysicalPages( u32 firstVirtualPageIdx, const u32 count, PhysicalPageID* outIDs )
	{
		RED_MEMORY_ASSERT( m_numFreePhysicalPages >= count, "This helper should only be called after availability of physical pages is verified." );
		RED_MEMORY_ASSERT( count <= cMaxNumPagesInOneBatchMap, "Too many pages to allocate for one virtual slot" );
		RED_MEMORY_ASSERT( firstVirtualPageIdx < m_numVirtualPages, "Bad virtual page Idx!" );
		RED_MEMORY_ASSERT( firstVirtualPageIdx + count < m_numVirtualPages, "Bad virtual page Idx!" );

		// Fill output array of physical pages, taken from free list
		for ( u32 i = 0; i < count; ++i )
		{
			const u32 virtualPageIdx = firstVirtualPageIdx + i;
			const PhysicalPageID physicalPageId = m_freePhysicalPageIDs[--m_numFreePhysicalPages];
			outIDs[i] = physicalPageId;

			RED_MEMORY_ASSERT( m_virtualPageToPhysicalPage[virtualPageIdx] == PhysicalPageIDInvalid, "Virtual page already has a physical page assigned!!!" );
			m_virtualPageToPhysicalPage[virtualPageIdx] = physicalPageId;

			m_freePhysicalPageIDs[m_numFreePhysicalPages] = PhysicalPageIDInvalid;	// Invalidate that page just to make it easier for debugging
		}
	}

	u32 SystemAllocatorOrbis::ReturnPhysicalPages( u32 firstVirtualPageIdx, const u32 count, PhysicalPageID* outReturnedIDs )
	{
		RED_MEMORY_ASSERT( count <= cMaxNumPagesInOneBatchMap, "Too many pages to allocate for one virtual slot" );
		RED_MEMORY_ASSERT( firstVirtualPageIdx < m_numVirtualPages, "Bad virtual page Idx!" );
		RED_MEMORY_ASSERT( firstVirtualPageIdx + count < m_numVirtualPages, "Bad virtual page Idx!" );

		u32 numPagesReturned = 0;

		// Fill output array of physical pages, taken from virtual pages
		for ( u32 i = 0; i < count; ++i )
		{
			const u32 virtualPageIdx = firstVirtualPageIdx + i;

			// Take physical page from virtual one, and mark virtual page as on that has no physical page assigned.
			const PhysicalPageID physicalPageId = m_virtualPageToPhysicalPage[virtualPageIdx];

			RED_MEMORY_ASSERT( physicalPageId != PhysicalPageIDInvalid, "Expected to be taken." );
		
			// If virtual page indeed had a physical page allocated for it, then return
			m_virtualPageToPhysicalPage[virtualPageIdx] = PhysicalPageIDInvalid;

			// Return physical page to the free list, and put it in out array
			m_freePhysicalPageIDs[m_numFreePhysicalPages++] = physicalPageId;
			outReturnedIDs[numPagesReturned++] = physicalPageId;
		}

		return numPagesReturned;
	}

	u64 SystemAllocatorOrbis::OnGetTotalPhysicalMemoryAvailable() const
	{
		return m_totalDirectMemoryAvailableInTheSystemAtStartup;
	}

	u64 SystemAllocatorOrbis::OnGetCurrentPageMemoryAvailable() const
	{
		off_t physAddrOut = 0;
		size_t sizeOut = 0;
		sceKernelAvailableDirectMemorySize( 0, SCE_KERNEL_MAIN_DMEM_SIZE, 0, &physAddrOut, &sizeOut );
		return sizeOut;
	}

	u64 SystemAllocatorOrbis::OnGetPageSize() const
	{
		return m_pageSize;
	}

	void SystemAllocatorOrbis::OnWriteReportToLog() const
	{
		// Assemble some numbers first
		const u64 freePhysicalMemory = m_numFreePhysicalPages * m_pageSize;			//<- This is memory that is being available in system allocator. Large number? Good - we just have spares.
		const u64 unaccountedFreePhysicalMemory = GetCurrentPageMemoryAvailable();	//<- This is memory that we don't do anything with. If it's NOT 0 or close to 0 - fix that.
		sceKernelGetPageTableStats( &m_lastPageTableStats.cpuTotal, &m_lastPageTableStats.cpuAvailable, &m_lastPageTableStats.gpuTotal, &m_lastPageTableStats.gpuAvailable );

		RED_MEMORY_LOG( "Memory: Basic Informations" );
		RED_MEMORY_LOG( "Memory: \tDirect Memory Used: %" PRIu64, m_physicalBudget - freePhysicalMemory );
		RED_MEMORY_LOG( "Memory: \tDirect Memory Available: %" PRIu64, freePhysicalMemory );
		RED_MEMORY_LOG( "Memory: \tDirect Memory That We Never Access (should be insignificant): %" PRIu64, unaccountedFreePhysicalMemory );
		RED_MEMORY_LOG( "Memory: \tDirect Memory Available In The System Overall (as returned by sceKernelGetDirectMemorySize()): %lu", sceKernelGetDirectMemorySize() );
		RED_MEMORY_LOG( "Memory: \tPage Table Stats: %i %i %i %i", m_lastPageTableStats.cpuTotal, m_lastPageTableStats.cpuAvailable, m_lastPageTableStats.gpuTotal, m_lastPageTableStats.gpuAvailable );

		off_t physAddrOut = 0;
		size_t sizeOut = 0;
		const Int32 ret = sceKernelAvailableDirectMemorySize( 0, SCE_KERNEL_MAIN_DMEM_SIZE, 0, &physAddrOut, &sizeOut );
		if(ret == SCE_KERNEL_ERROR_ENOMEM)
		{
			RED_MEMORY_LOG( "Memory: sceKernelAvailableDirectMemorySize returned SCE_KERNEL_ERROR_ENOMEM searching entire range" );
		}
		else if(ret == SCE_KERNEL_ERROR_EINVAL)
		{
			RED_MEMORY_LOG( "Memory: sceKernelAvailableDirectMemorySize returned SCE_KERNEL_ERROR_EINVAL searching entire range" );
		}
		else if(ret == SCE_OK)
		{
			RED_MEMORY_LOG( "Memory: sceKernelAvailableDirectMemorySize largest dmem available at: physAddrOut=%lu, size=%lu", static_cast<u64>(physAddrOut), sizeOut );
		}
		else
		{
			RED_MEMORY_LOG( "Memory: sceKernelAvailableDirectMemorySize returned unexpected error 0x%08X searching entire range", ret );
		}
	}

	void SystemAllocatorOrbis::OnWriteReportToJson( FILE* file ) const
	{
		off_t physAddrOut = 0;
		size_t sizeOut = 0;
		const Int32 ret = sceKernelAvailableDirectMemorySize( 0, SCE_KERNEL_MAIN_DMEM_SIZE, 0, &physAddrOut, &sizeOut );
		if( ret == SCE_OK )
		{
			std::fprintf( file, ",\"sceKernelGetDirectMemorySize\":%lu,\"sceKernelAvailableDirectMemorySize\":%lu", sceKernelGetDirectMemorySize(), sizeOut );
		}
	}
}
}
