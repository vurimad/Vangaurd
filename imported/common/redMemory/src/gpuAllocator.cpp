/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "gpuAllocator.h"
#include "flags.h"
#include "tlsfBlock.h"
#include "systemAllocator.h"
#include "systemAllocatorOrbisHelper.h"
#ifdef RED_PLATFORM_ORBIS
#include <gnm/platform.h>
#endif
#include "vault.h"

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_ORBIS
	const u32 c_gpuAllocatorVirtualRangeAlignment = RED_KILO_BYTE(64);

	//////////////////////////////////////////////////////////////////////////

	VirtualAllocatorOrbis::VirtualAllocatorOrbis()
		: m_systemAllocator( nullptr )
		, m_physicalPoolAddress( 0 )
		, m_physicalBudget( 0 )
		, m_numPhysicalPages( 0 )
		, m_virtualSlotSize( 0 )
		, m_numVirtualSlots( 0 )
		, m_flags( 0 )
		, m_freeVirtualSlotIDs( nullptr )
		, m_virtualSlotToHeadPhysicalPage( nullptr )
		, m_numFreeVirtualSlots( 0 )
		, m_physicalPageIDsInUseList( nullptr )
		, m_freePhysicalPageIDs( nullptr )
		, m_numFreePhysicalPages( 0 )
		, m_lowestFreePhysicalMemoryEver( RED_GIGA_BYTE(999) )
		, m_allocatorIsFull( false )
		, m_realSizes( nullptr )
	{

	}

	VirtualAllocatorOrbis::~VirtualAllocatorOrbis()
	{

	}

	// Give huge alignment to our virtual ranges. This allows us not to worry about whatever insane render target alignment is needed, 
	// and we can pretty much ignore the "alignment" argument passed to the allocator.
	constexpr static u32 VirtualRangeAlignment = RED_MEGA_BYTE( 2 );

	void VirtualAllocatorOrbis::Initialize( const VirtualAllocatorOrbisParameter & parameter )
	{
		RED_MEMORY_ASSERT( parameter.systemAllocator, "Given system allocator does not exist." );
		RED_MEMORY_ASSERT( parameter.virtualRangeSize, "Virtual range size needs to be greater then 0." );

		m_systemAllocator = parameter.systemAllocator;
		m_physicalPageSize = parameter.pageSize;					// Page size for physical allocations.
		m_virtualSlotSize = parameter.virtualSlotSize;				// Every single resource alloc will get this virtual size (this way we don't need to deal with virtual fragmentation)
		m_physicalBudget = parameter.physicalBudget;				// Our physical budget
		m_numPhysicalPages = parameter.physicalBudget / m_physicalPageSize;	// That many physical pages to manage
		m_lowestFreePhysicalMemoryEver = m_physicalBudget;

		RED_MEMORY_ASSERT( m_numPhysicalPages < PhysicalPageIDInvalid, "Physical budget is too large for this allocator to manage. If it is really necessary, you'd need to switch to 32bit indexing" );

		// Allocate virtual space (a lot of it)
		{
			m_flags = parameter.flags | Flags_No_Coalesce_Blocks;
			m_virtualRangeReservedBySystem = m_systemAllocator->ReserveVirtualRange( parameter.virtualRangeSize, m_flags );
			m_virtualRange = m_virtualRangeReservedBySystem;
			m_virtualRange.start = AlignAddress( m_virtualRange.start, VirtualRangeAlignment );
			m_virtualRange.end = AlignAddress( m_virtualRange.end, VirtualRangeAlignment ) - VirtualRangeAlignment;
			m_numVirtualSlots = ( m_virtualRange.end - m_virtualRange.start ) / parameter.virtualSlotSize;
			RED_MEMORY_ASSERT( m_virtualRange.end - m_virtualRange.start >= parameter.virtualRangeSize - VirtualRangeAlignment, "System allocator didn't reserve the requested amount of virtual space." );
		}

		// Allocate the whole physical pool right away. This is the only time this pool asks the kernel for memory.
		{
			i32 result = sceKernelAllocateDirectMemory(
				0,							// Search from the beginning ...
				SCE_KERNEL_MAIN_DMEM_SIZE,	// till the end of entire physical memory space
				parameter.physicalBudget,	// Allocate the whole pool budget at once
				m_physicalPageSize,			// Align to our page size
				m_flags & Flags_Garlic_Bus ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION,
				&m_physicalPoolAddress		// Our output address, beginning of the physical space we can use
			);
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to allocate the physical space for VirtualGPU allocator." );
		}

		// Allocate and initialize bookkeeping structures
		{
			// Allocate
			const u32 FreeVirtualSlotIDsListSize = m_numVirtualSlots * sizeof( VirtualSlotID );
			const u32 VirtualSlotStateIndicesSize = m_numVirtualSlots * sizeof( PhysicalPageID );
			const u32 PhysicalPageIDsSize = m_numPhysicalPages * sizeof( PhysicalPageIDListNode );
			const u32 FreePhysicalPageIDsSize = m_numPhysicalPages * sizeof( PhysicalPageID );
			const u32 RealSizesTableSize = m_numVirtualSlots * sizeof( u32 );

			const u32 OverallBookkeepingMemorySize = FreeVirtualSlotIDsListSize + VirtualSlotStateIndicesSize + PhysicalPageIDsSize + FreePhysicalPageIDsSize + RealSizesTableSize;

			m_bookkeepingListVirtualRange = m_systemAllocator->ReserveVirtualRange( OverallBookkeepingMemorySize, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );
			const SystemBlock block = m_systemAllocator->Commit( { m_bookkeepingListVirtualRange.start, OverallBookkeepingMemorySize }, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );

			m_freeVirtualSlotIDs = reinterpret_cast<i16*>( block.address );
			m_virtualSlotToHeadPhysicalPage = reinterpret_cast<PhysicalPageID*>( block.address + FreeVirtualSlotIDsListSize );
			m_physicalPageIDsInUseList = reinterpret_cast<PhysicalPageIDListNode*>( block.address + FreeVirtualSlotIDsListSize + VirtualSlotStateIndicesSize );
			m_freePhysicalPageIDs = reinterpret_cast<PhysicalPageID*>( block.address + FreeVirtualSlotIDsListSize + VirtualSlotStateIndicesSize + PhysicalPageIDsSize );
			m_realSizes = reinterpret_cast<u32*>( block.address + FreeVirtualSlotIDsListSize + VirtualSlotStateIndicesSize + PhysicalPageIDsSize + FreePhysicalPageIDsSize );

			// --- Initialize
			{
				// Reset all ids to Invalid. All memory is free.
				red::Memset( m_physicalPageIDsInUseList, 0xFF, PhysicalPageIDsSize );

				// Throw all physical pages to the free queue
				for ( u32 i = 0; i < m_numPhysicalPages; ++i )
				{
					m_freePhysicalPageIDs[i] = m_numPhysicalPages - i - 1;
				}
				m_numFreePhysicalPages = m_numPhysicalPages;

				// Throw all virtual slots to free queue
				for ( u16 i = 0; i < m_numVirtualSlots; ++i )
				{
					m_freeVirtualSlotIDs[i] = m_numVirtualSlots - i - 1;
					m_virtualSlotToHeadPhysicalPage[i] = PhysicalPageIDInvalid;
					m_realSizes[i] = 0;
				}
				m_numFreeVirtualSlots = m_numVirtualSlots;
			}
		}
	}

	void VirtualAllocatorOrbis::Uninitialize()
	{
		RED_WARNING( m_systemAllocator, "Allocator was not initialized." );
		if( m_systemAllocator )
		{
			m_systemAllocator->ReleaseVirtualRange( m_bookkeepingListVirtualRange );
			m_systemAllocator->ReleaseVirtualRange( m_virtualRangeReservedBySystem );

			i32 result = sceKernelReleaseDirectMemory( m_physicalPoolAddress, m_physicalBudget );
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to allocate the physical space for VirtualGPU allocator." );
		}
	}

	Block VirtualAllocatorOrbis::Allocate( u32 size, Bool isGarlic /*= false*/ )
	{
		return AllocateAligned( size, m_physicalPageSize, isGarlic );
	}

	red::memory::Block VirtualAllocatorOrbis::AllocateAligned( u32 size, u32 alignment, Bool isGarlic /*= false*/ )
	{
		if ( RED_UNLIKELY( m_allocatorIsFull ) )
		{
			return NullBlock();
		}

		// NOTE: No rounding up whatsoever is required. All allocations wired through this allocator are guaranteed to be 2MB aligned.
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Given alignment is not power of 2." );
		RED_MEMORY_ASSERT( alignment <= VirtualRangeAlignment, "Just make sure, we expect the physical page to never be lower than 64KB" );

		if ( size > m_virtualSlotSize )
		{
			// Requested memory too large for this allocator.
			return NullBlock();
		}

		const i32 numPhysicalPagesRequired = ( (u64)size + m_physicalPageSize - 1 ) / m_physicalPageSize;

		RED_SCOPE_LOCK( m_lock );

		if ( m_numFreeVirtualSlots == 0 || numPhysicalPagesRequired > m_numFreePhysicalPages )
		{
			// OOM
			return NullBlock();
		}

		// Find a free virtual slot
		const VirtualSlotID virtualSlotID = m_freeVirtualSlotIDs[m_numFreeVirtualSlots - 1];
		RED_MEMORY_ASSERT( virtualSlotID != -1, "Bug in bookkeeping, that's not supposed to happen. If VirtualSlotID is in the freeVirtualSlotIDs list, then it should be valid." );

		const VirtualRange virtualRange = VirtualSlotToVirtualRange( virtualSlotID );
		RED_MEMORY_ASSERT( virtualRange.start >= m_virtualRange.start && virtualRange.end <= m_virtualRange.end, "Somehow the virtual range for this allocation became outside of the prereserved virtual range for the whole allocator." );

		// Reserve physical pages for this allocation
		{
			RED_MEMORY_ASSERT( numPhysicalPagesRequired <= cMaxNumPagesPerAllocEver, "Unexpectedly large allocation!" );

			// Prepare sufficient amount of physical pages
			GrabFreePhysicalPages( numPhysicalPagesRequired, virtualSlotID, m_tempPhysicalPageIDs );

			// Perform actual kernelBatcHMap
			Uint32 numMapEntriesProduced = 0;
			{
				for ( u32 i = 0; i < numPhysicalPagesRequired; ++i )
				{
					const PhysicalPageID currPhysicalPageID = m_tempPhysicalPageIDs[i];

					SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
					mapEntry.start = (void*)( virtualRange.start + (u64)i * m_physicalPageSize );
					mapEntry.offset = m_physicalPoolAddress + (u64)currPhysicalPageID * m_physicalPageSize;
					mapEntry.length = m_physicalPageSize;
					mapEntry.protection = SCE_KERNEL_PROT_CPU_RW;
					mapEntry.type = -1;	// Will be ignored anyway
					mapEntry.operation = SCE_KERNEL_MAP_OP_MAP_DIRECT;
				}
				if ( isGarlic )
				{
					// Additionally, produce memory type remappings, enabling garlic bus
					for ( u32 i = 0; i < numPhysicalPagesRequired; ++i )
					{
						SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
						mapEntry.start = (void*)( virtualRange.start + (u64)i * m_physicalPageSize );
						//mapEntry.offset - this one is ignored when issuing SCE_KERNEL_MAP_OP_TYPE_PROTECT 
						mapEntry.length = m_physicalPageSize;
						mapEntry.protection = SCE_KERNEL_PROT_CPU_RW | SCE_KERNEL_PROT_GPU_RW;
						mapEntry.type = SCE_KERNEL_WC_GARLIC;
						mapEntry.operation = SCE_KERNEL_MAP_OP_TYPE_PROTECT;
					}
				}

				i32 entriesSuccessfullyMapped = 0;

				i32 mapResult = -1;
#define USE_BATCHED_KERNEL_MAP	// Option of NOT batching is here just so we can debug specific failed pages
#ifdef USE_BATCHED_KERNEL_MAP
				// Do that in a batch
				mapResult = sceKernelBatchMap2( m_tempMapEntries, numMapEntriesProduced, &entriesSuccessfullyMapped, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE );
#else		
				// One by one, so we can actually debug
				for ( u32 i = 0; i < numPhysicalPagesRequired; ++i )
				{
					auto& entry = m_tempMapEntries[i];
					mapResult = sceKernelMapDirectMemory2( (void**)&entry.start, entry.length, entry.type, SCE_KERNEL_PROT_CPU_RW | SCE_KERNEL_PROT_GPU_RW, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE, entry.offset, 64 * 1024 );
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
					return NullBlock();
				}
			}
		}

		// Track stuff
		m_realSizes[virtualSlotID] = size;
		if ( m_numFreePhysicalPages * m_physicalPageSize < m_lowestFreePhysicalMemoryEver )
		{
			// Track that for memory reports
			m_lowestFreePhysicalMemoryEver = m_numFreePhysicalPages * m_physicalPageSize;
		}

		// Remove the slot from freelist
		m_freeVirtualSlotIDs[m_numFreeVirtualSlots-- - 1] = -1;
		RED_MEMORY_ASSERT( m_numFreePhysicalPages >= 0, "Bookkeeping is buggy." );
		RED_MEMORY_ASSERT( m_numFreeVirtualSlots >= 0, "Bookkeeping is buggy." );

		return { virtualRange.start, size };
	}

	void VirtualAllocatorOrbis::Free( Block & block, Bool isGarlic /*= false*/ )
	{
		if ( !block.address ) return;

		// Locate Virtual Slot
		const VirtualSlotID virtualSlotID = VirtualRangeToVirtualSlot( { block.address, block.address + block.size } );
		const VirtualRange virtualRange = VirtualSlotToVirtualRange( virtualSlotID );

		RED_MEMORY_ASSERT( virtualRange.start == block.address, "A different address was returned?" );

		RED_SCOPE_LOCK( m_lock );

		// Return physical pages for free bookkeeping and unmap
		const u32 numPhysicalPagesReturned = ReturnPhysicalPages( virtualSlotID, m_tempPhysicalPageIDs );

		// Perform actual kernelBatchMap (to unmap)
		{
			Uint32 numMapEntriesProduced = 0;
			if ( isGarlic )
			{
				// First, produce memory type remappings, switching the memory back into Onion bus
				for ( u32 i = 0; i < numPhysicalPagesReturned; ++i )
				{
					SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
					mapEntry.start = (void*)( virtualRange.start + (u64)i * m_physicalPageSize );
					//mapEntry.offset - this one is ignored when issuing SCE_KERNEL_MAP_OP_TYPE_PROTECT 
					mapEntry.length = m_physicalPageSize;
					mapEntry.protection = SCE_KERNEL_PROT_CPU_RW;
					mapEntry.type = SCE_KERNEL_WB_ONION;
					mapEntry.operation = SCE_KERNEL_MAP_OP_TYPE_PROTECT;
				}
			}

			for ( u32 i = 0; i < numPhysicalPagesReturned; ++i )
			{
				SceKernelBatchMapEntry& mapEntry = m_tempMapEntries[numMapEntriesProduced++];
				mapEntry.start = (void*)( virtualRange.start + (u64)i * m_physicalPageSize );
				mapEntry.length = m_physicalPageSize;
				mapEntry.operation = SCE_KERNEL_MAP_OP_UNMAP;
			}

			i32 entriesSuccessfullyUnmapped = 0;
			i32 result = sceKernelBatchMap2( m_tempMapEntries, numMapEntriesProduced, &entriesSuccessfullyUnmapped, SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE );
			RED_MEMORY_ASSERT( result == SCE_OK, "Failed to perform kernelBatchMap." );
			RED_MEMORY_ASSERT( entriesSuccessfullyUnmapped == numPhysicalPagesReturned || entriesSuccessfullyUnmapped == 2 * numPhysicalPagesReturned, "Not all pages were succesfully mapped." );
		}

		// Return virtual slot to the list of free virtual slots
		m_freeVirtualSlotIDs[m_numFreeVirtualSlots++] = virtualSlotID;
		RED_MEMORY_ASSERT( m_numFreeVirtualSlots <= m_numVirtualSlots, "Looks like we returned more virtual slots than we ever took." );

		block.size = m_realSizes[virtualSlotID];
		m_realSizes[virtualSlotID] = 0;
	}

	Block VirtualAllocatorOrbis::Reallocate( Block & block, u32 size )
	{
		return ReallocateAligned( block, size, 16 );
	}

	Block VirtualAllocatorOrbis::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		if ( block == NullBlock() )
		{
			return AllocateAligned( size, alignment );
		}

		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this big size allocator." );

		if ( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		block.size = GetBlockSize( block.address );

		Block newBlock = AllocateAligned( size, alignment );
		if ( newBlock != NullBlock() )
		{
			MemcpyBlock( newBlock.address, block.address, std::min( newBlock.size, block.size ) );
			Free( block );
		}
		return newBlock;
	}

	bool VirtualAllocatorOrbis::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		return block >= m_virtualRange.start && block <= m_virtualRange.end;
	}

	u64 VirtualAllocatorOrbis::GetBlockSize( u64 block ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( block ), "Block is not owned by this big size allocator." );

		RED_SCOPE_SHARED_LOCK( m_lock );

		u32 retVal = 0;
		VirtualSlotID slotID = VirtualAddressToVirtualSlot( block );
		retVal = m_realSizes[slotID];

		return retVal;
	}

	u32 VirtualAllocatorOrbis::GetPageSize() const
	{
		return m_physicalPageSize;
	}

	void VirtualAllocatorOrbis::BuildMetrics( VirtualGPUAllocatorMetrics & metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		RED_SCOPE_SHARED_LOCK( m_lock );

		metrics.metrics.consumedSystemMemoryBytes = m_physicalBudget;
		metrics.metrics.bookKeepingBytes = m_bookkeepingListVirtualRange.end - m_bookkeepingListVirtualRange.start;

		metrics.physicalBudgetPreallocated = m_physicalBudget;
		metrics.virtualSpacePrereserved = m_virtualRange.end - m_virtualRange.start;
		metrics.physicalMemoryFree = (u64)m_numFreePhysicalPages * m_physicalPageSize;
		metrics.physicalPagesFree = m_numFreePhysicalPages;
		metrics.virtualSlotsFree = m_numFreeVirtualSlots;
		metrics.lowestPhysicalFreeEver = m_lowestFreePhysicalMemoryEver;

		// Find max allocation AND amount of waste generated in bytes
		u32 maxRealSize = 0;
		u32 wasteInBytes = 0;
		for ( u32 i = 0; i < m_numVirtualSlots; ++i )
		{
			const u32 realSize = m_realSizes[i];

			if ( realSize > maxRealSize )
				maxRealSize = realSize;

			if ( realSize > 0 )
			{
				RED_MEMORY_ASSERT( m_virtualSlotToHeadPhysicalPage[i] != PhysicalPageIDInvalid, "Some mismatch. This needs debugging!" );
				const u32 residualSize = realSize % m_physicalPageSize;
				if ( residualSize > 0 )
				{
					wasteInBytes += m_physicalPageSize - residualSize;
				}
			}
		}

		// Calculate the amount of waste in percentage
		const u32 memoryConsumed = m_physicalBudget - metrics.physicalMemoryFree;
		metrics.wastePercent = ( (Float)wasteInBytes / (Float)memoryConsumed ) * 100.0f;

		metrics.maxPhysicalPagesPerSlot = ( (u64)maxRealSize + (u64)m_physicalPageSize - 1 ) / m_physicalPageSize;

		metrics.pageTableStats_cpuTotal		= m_lastPageTableStats.cpuTotal;
		metrics.pageTableStats_cpuAvailable	= m_lastPageTableStats.cpuAvailable;
		metrics.pageTableStats_gpuTotal		= m_lastPageTableStats.gpuTotal;
		metrics.pageTableStats_gpuAvailable = m_lastPageTableStats.gpuAvailable;
	}

	void VirtualAllocatorOrbis::SerializeMetrics( Serializer & serializer )
	{
		VirtualGPUAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast<u32>( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	u32 VirtualAllocatorOrbis::GetAvailablePhysicalSpace() const
	{
		RED_SCOPE_SHARED_LOCK( m_lock );
		return m_physicalPageSize * m_numFreePhysicalPages;
	}

	red::memory::VirtualRange VirtualAllocatorOrbis::VirtualSlotToVirtualRange( VirtualSlotID virtualSlotIdx ) const
	{
		VirtualRange retRange;
		retRange.start = m_virtualRange.start + m_virtualSlotSize * virtualSlotIdx;
		retRange.end = retRange.start + m_virtualSlotSize;
		return retRange;
	}

	red::memory::VirtualAllocatorOrbis::VirtualSlotID VirtualAllocatorOrbis::VirtualRangeToVirtualSlot( const VirtualRange range ) const
	{
		const VirtualSlotID slotID = ( range.start - m_virtualRange.start ) / m_virtualSlotSize;
		RED_MEMORY_ASSERT( range.end <= ( m_virtualRange.start + (u64)m_virtualSlotSize * ( slotID + 1 ) ),
			"The range passed here is spanning over multiple slots. That doesn't clearly indicate a bug in the allocator, but it is not expected." );
		return slotID;
	}

	red::memory::VirtualAllocatorOrbis::VirtualSlotID VirtualAllocatorOrbis::VirtualAddressToVirtualSlot( u64 address ) const
	{
		const VirtualRange testRange = { address, address + 1 };
		return VirtualRangeToVirtualSlot( testRange );
	}

	void VirtualAllocatorOrbis::GrabFreePhysicalPages( const u32 count, VirtualSlotID virtualSlotID, PhysicalPageID* outIDs )
	{
		RED_MEMORY_ASSERT( m_numFreePhysicalPages >= count, "This helper should only be called after availability of physical pages is verified." );
		RED_MEMORY_ASSERT( count <= cMaxNumPagesPerAllocEver, "Too many pages to allocate for one virtual slot" );

		// NOTE: Grabbing pages is only allowed if a specific virtual slot takes ownership of them.
		// Fill output array of physical pages, taken from free list
		for ( u32 i = 0; i < count; ++i )
		{
			outIDs[i] = m_freePhysicalPageIDs[--m_numFreePhysicalPages];
			m_freePhysicalPageIDs[m_numFreePhysicalPages] = PhysicalPageIDInvalid;	// Invalidate that page just to make it easier for debugging
		}

		// Do the bookkeeping duty so that we can deallocate those later
		// That involves building a list of taken pages
		// TODO: merge those two loops
		{
			const PhysicalPageID headPageID = outIDs[0];

			// Put head of physical page IDs under respective virtual slot id.
			m_virtualSlotToHeadPhysicalPage[virtualSlotID] = headPageID;

			// Fill the head with ID and invalidate "next" index
			PhysicalPageIDListNode* headPhysicalPage = &m_physicalPageIDsInUseList[headPageID];
			headPhysicalPage->id = headPageID;
			headPhysicalPage->next = PhysicalPageIDInvalid;

			// If more than one page, turn that head into an actual list
			for ( i32 i = 1; i < count; ++i )
			{
				headPhysicalPage->next = outIDs[i];
				// Reuse the head pointer for subsequent entries
				headPhysicalPage = &m_physicalPageIDsInUseList[outIDs[i]];
				headPhysicalPage->id = outIDs[i];
				headPhysicalPage->next = PhysicalPageIDInvalid;	// Again, invalidate "next", so we don't need another iteration in case this is the last page.
			}
		}

	}

	u32 VirtualAllocatorOrbis::ReturnPhysicalPages( const VirtualSlotID virtualSlotID, PhysicalPageID* outReturnedIDs )
	{
		u32 numPagesReturned = 0;

		// Grab head page ID
		PhysicalPageID headPageID = m_virtualSlotToHeadPhysicalPage[virtualSlotID];
		// Unassign from virtual slot
		m_virtualSlotToHeadPhysicalPage[virtualSlotID] = PhysicalPageIDInvalid;
		outReturnedIDs[numPagesReturned++] = headPageID;

		// Clear out head page list node and all followers, and return them to the free list
		PhysicalPageIDListNode* headPhysicalPagesNode = &m_physicalPageIDsInUseList[headPageID];
		m_freePhysicalPageIDs[m_numFreePhysicalPages++] = headPhysicalPagesNode->id;
		headPhysicalPagesNode->id = PhysicalPageIDInvalid;

		// Go through the list and return all pages to the pool of free
		while ( headPhysicalPagesNode != nullptr && headPhysicalPagesNode->next != PhysicalPageIDInvalid )
		{
			RED_MEMORY_ASSERT( numPagesReturned < cMaxNumPagesPerAllocEver, "Unexpectedly large amount of pages to return from one virtual slot. This is a bug in the allocator." );

			// Grab ID of the "next" one and clear it out
			const PhysicalPageID nextPageID = headPhysicalPagesNode->next;
			headPhysicalPagesNode->next = PhysicalPageIDInvalid;

			// Go to the next one and clear it. If it has a valid "next" then subsequent iteration will take care of it,
			headPhysicalPagesNode = &m_physicalPageIDsInUseList[nextPageID];
			m_freePhysicalPageIDs[m_numFreePhysicalPages++] = headPhysicalPagesNode->id;
			headPhysicalPagesNode->id = PhysicalPageIDInvalid;
			++numPagesReturned;
		}

		RED_MEMORY_ASSERT( m_numFreePhysicalPages <= m_numPhysicalPages, "A bug in bookkeeping. Looks like we have more free pages than overall pages" );
		return numPagesReturned;
	}

	void VirtualAllocatorOrbis::InternalMarkBigSizeAllocatorAsFull()
	{
		m_allocatorIsFull = true;
	}

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////

	GpuAllocator::GpuAllocator()
	{}

	GpuAllocator::~GpuAllocator()
	{}

	void GpuAllocator::Initialize( const GpuAllocatorParameter & parameter )
	{
		// Initialize TLSF part
		{
			m_tlsfMaxSize = RED_KILO_BYTE( 256 );
			m_tlsfAllocator.Initialize(
			{
				parameter.systemAllocator,
				RED_GIGA_BYTE( 8 ),
				RED_MEGA_BYTE( 300 ),
				RED_MEGA_BYTE( 2 ),
				red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write | red::memory::Flags_Garlic_Bus,
				c_gpuAllocatorVirtualRangeAlignment
			} );
		}
		
		BigSizeAllocatorParameter bsParameter =
		{
			parameter.systemAllocator,
			RED_GIGA_BYTE( 32 ),
			red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write | red::memory::Flags_Garlic_Bus
		};

		m_bigSizeAllocator.Initialize( bsParameter );
	}
	
	void GpuAllocator::Uninitialize()
	{
		m_bigSizeAllocator.Uninitialize();
		m_tlsfAllocator.Uninitialize();
	}

	Block GpuAllocator::Allocate( u32 size )
	{
		Block block = NullBlock();

		size = std::max( size, 1u );

		if ( size <= m_tlsfMaxSize )
		{
			block = m_tlsfAllocator.Allocate( size );
		}

		if ( !block.address )
		{
			block = m_bigSizeAllocator.Allocate( size );
		}

		return block;
	}

	Block GpuAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		Block block = NullBlock();

		size = std::max( size, 1u );

		if( size < m_tlsfMaxSize && alignment != RED_KILO_BYTE( 64 ) )
		{
			block = m_tlsfAllocator.AllocateAligned( size, alignment );
		}

		if ( !block.address )
		{
			block = m_bigSizeAllocator.AllocateAligned( size, alignment );
		}

		return block;
	}
	
	Block GpuAllocator::Reallocate( Block & /*block*/, u32 /*size*/ )
	{
		RED_HALT( "Reallocate not supported" );
		return {};
	}
	
	Block GpuAllocator::ReallocateAligned( Block & /*block*/, u32 /*size*/, u32 /*alignment*/ )
	{
		RED_HALT( "Reallocate not supported" );
		return {};
	}
	
	void GpuAllocator::Free( Block & block )
	{
		RED_MEMORY_ASSERT( !block.address || OwnBlock( block.address ), "Memory Block is not own by GpuAllocator." );
		if ( m_tlsfAllocator.OwnBlock( block.address ) )
		{
			m_tlsfAllocator.Free( block );
		}
		else
		{
			m_bigSizeAllocator.Free( block );
		}
	}

	bool GpuAllocator::OwnBlock( u64 block ) const
	{
		return m_tlsfAllocator.OwnBlock( block ) || m_bigSizeAllocator.OwnBlock( block );
	}
	
	u64 GpuAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not owned by this allocator." );

		if ( m_tlsfAllocator.OwnBlock( address ) )
		{
			return GetTLSFBlockSize( address );
		}
		else
		{
			return m_bigSizeAllocator.GetBlockSize( address );
		}
	}

	void GpuAllocator::BuildMetrics( GpuAllocatorMetrics & metrics, Bool virtualAllocatorOnly )
	{
		AllocatorMetrics & defaultMetric = metrics.metrics;
		TLSFAllocatorMetrics & tlsfMetric = metrics.tlsfAllocatorMetrics;
		BigSizeAllocatorMetrics & bigSizeAllocatorMetrics = metrics.bigSizeAllocatorMetrics;

		m_tlsfAllocator.BuildMetrics( metrics.tlsfAllocatorMetrics );
		m_bigSizeAllocator.BuildMetrics( bigSizeAllocatorMetrics );

		defaultMetric.consumedSystemMemoryBytes = tlsfMetric.metrics.consumedSystemMemoryBytes + bigSizeAllocatorMetrics.metrics.consumedSystemMemoryBytes;
		defaultMetric.consumedMemoryBytes = tlsfMetric.metrics.consumedMemoryBytes + bigSizeAllocatorMetrics.metrics.consumedMemoryBytes;
		defaultMetric.bookKeepingBytes = tlsfMetric.metrics.bookKeepingBytes + bigSizeAllocatorMetrics.metrics.bookKeepingBytes;
	}

	void GpuAllocator::SerializeMetrics( Serializer & serializer )
	{
		GpuAllocatorMetrics metrics;
		Memzero( &metrics, sizeof( metrics ) );
		BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );
		serializer.Serialize( static_cast<u32>(sizeof( metrics )) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	//////////////////////////////////////////////////////////////////////////

	GpuContiguousAllocator::GpuContiguousAllocator()
		: m_numAllocs( 0 )
		, m_systemAllocator( nullptr )
		, m_pageSize( 0 )
	{
		red::Memzero( m_allocs, sizeof( m_allocs ) );
	}

	GpuContiguousAllocator::~GpuContiguousAllocator()
	{
	}

	void GpuContiguousAllocator::Initialize( const GpuContiguousAllocatorParameter & parameter )
	{
		m_systemAllocator = parameter.systemAllocator;
		m_pageSize = m_systemAllocator->GetPageSize();
	}

	void GpuContiguousAllocator::Uninitialize()
	{
	}

	Block GpuContiguousAllocator::Allocate( u32 size )
	{
		RED_FATAL("This function should never be called");
		return NullBlock();
	}

	Block GpuContiguousAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Alignment has to be power of 2." );
		RED_MEMORY_ASSERT( alignment <= m_pageSize, "Make sure that our assumed alignment of page size is enough." );
		const u64 sizeRoundedToPageSize = RoundUp( size, m_pageSize );

		// Select allocation slot
		RED_MEMORY_ASSERT( m_numAllocs + 1 < MaxAllocCount, "Too many allocations!" );
		const u32 allocId = m_numAllocs++;
		Alloc& newAllocation = m_allocs[allocId];
		RED_MEMORY_ASSERT( !newAllocation.IsValid(), "Allocation slot should not be used!" );

		// Perform allocation
		newAllocation.range = m_systemAllocator->ReserveVirtualRange( sizeRoundedToPageSize, Flags_GPU_Read_Write | Flags_Garlic_Bus | Flags_No_Coalesce_Blocks );
		newAllocation.sysBlock = internal::CommitBlockToDirectMemory( { newAllocation.range.start, sizeRoundedToPageSize }, Flags_GPU_Read_Write | Flags_Garlic_Bus | Flags_No_Coalesce_Blocks );

		RED_MEMORY_ASSERT( newAllocation.sysBlock != NullSystemBlock(), "Allocation failed. The app will not be able to render." );
		return { newAllocation.sysBlock.address, newAllocation.sysBlock.size };
	}

	Block GpuContiguousAllocator::Reallocate( Block & block, u32 size )
	{
		RED_FATAL( "This function should never be called" );
		return NullBlock();
	}

	Block GpuContiguousAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_FATAL( "This function should never be called" );
		return NullBlock();
	}

	void GpuContiguousAllocator::Free( Block & block )
	{
		for ( u32 i = 0; i < MaxAllocCount; ++i )
		{
			auto& alloc = m_allocs[i];
			if ( alloc.IsValid() && 
				( block.address >= alloc.range.start && 
				( block.address + block.size <= alloc.range.end ) ) )
			{
				m_systemAllocator->ReleaseVirtualRange( alloc.range );
				alloc.range = { 0,0 };
				alloc.sysBlock = { 0,0 };
				return;
			}
		}
	}

	bool GpuContiguousAllocator::OwnBlock( u64 block ) const
	{
		for ( u32 i = 0; i < MaxAllocCount; ++i )
		{
			const auto& alloc = m_allocs[i];
			if ( alloc.IsValid() && ( block >= alloc.range.start && block <= alloc.range.end ) )
			{
				return true;
			}
		}
		return false;
	}

	u64 GpuContiguousAllocator::GetBlockSize( u64 address ) const
	{
		for ( u32 i = 0; i < MaxAllocCount; ++i )
		{
			const auto& alloc = m_allocs[i];
			if ( alloc.IsValid() && ( address >= alloc.range.start && address <= alloc.range.end ) )
			{
				return alloc.sysBlock.size;
			}
		}
		return 0;
	}

	void GpuContiguousAllocator::SerializeMetrics( Serializer & serializer )
	{
	}

#endif
}
}
