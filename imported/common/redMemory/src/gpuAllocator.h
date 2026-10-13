/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_GPU_ALLOCATOR_H_
#define RED_MEMORY_GPU_ALLOCATOR_H_

#include "allocator.h"
#include "lockingDynamicTlsfAllocator.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
#ifdef RED_PLATFORM_ORBIS
	class SystemAllocator;

	struct VirtualGPUAllocatorMetrics
	{
		AllocatorMetrics metrics;
		u32 physicalBudgetPreallocated;	// How many bytes are preallocated by this allocator
		u64 virtualSpacePrereserved;	// How large virtual space was reserved
		u32 physicalMemoryFree;			// How much  of the preallocated physical budget is free
		u32 physicalPagesFree;			// How many physical pages are unused (corresponds to the previous one)
		u32 maxPhysicalPagesPerSlot;	// What's the max number of physical pages consumed for a single virtual slot
		u32 virtualSlotsFree;			// How many virtual slots are left?
		u32 lowestPhysicalFreeEver;		// What was the lowest point of free memory?
		float wastePercent;				// Waste in percentage
		i32 pageTableStats_cpuTotal;	// Page table stats. Will only contain non -1 values if actual kernelMap failure took place
		i32 pageTableStats_cpuAvailable;// Page table stats. Will only contain non -1 values if actual kernelMap failure took place
		i32 pageTableStats_gpuTotal;	// Page table stats. Will only contain non -1 values if actual kernelMap failure took place
		i32 pageTableStats_gpuAvailable;// Page table stats. Will only contain non -1 values if actual kernelMap failure took place
	};

	struct RED_MEMORY_API VirtualAllocatorOrbisParameter
	{
		SystemAllocator * systemAllocator;
		u64 virtualRangeSize;
		u32 physicalBudget;
		u32 pageSize;
		u32 virtualSlotSize;
		u32 flags;
	};

	class RED_MEMORY_API VirtualAllocatorOrbis : NonCopyable
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( VirtualAllocatorOrbis, VirtualGPUAllocatorMetrics, 16 );

		VirtualAllocatorOrbis();
		~VirtualAllocatorOrbis();

		void Initialize( const VirtualAllocatorOrbisParameter & parameter );
		void Uninitialize();

		// Only one allocate function, no "aligned" version, since alignment is always the same (64KB)
		Block Allocate( u32 size, Bool isGarlic = false );
		Block AllocateAligned( u32 size, u32 alignment, Bool isGarlic = false );
		void Free( Block & block, Bool isGarlic = false );

		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 block ) const;
		u32 GetPageSize() const;

		void BuildMetrics( VirtualGPUAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );
		u32 GetAvailablePhysicalSpace() const;

		// UNIT TEST ONLY
		void InternalMarkBigSizeAllocatorAsFull();

	private:
		typedef u32 PhysicalPageID;
		typedef i16 VirtualSlotID;
		struct PhysicalPageIDListNode
		{
			PhysicalPageID id;
			u32 next;
		};

		// Helpers
		VirtualRange VirtualSlotToVirtualRange( VirtualSlotID virtualSlotIdx ) const;
		VirtualSlotID VirtualRangeToVirtualSlot( const VirtualRange range ) const;
		VirtualSlotID VirtualAddressToVirtualSlot( u64 address ) const;

		void GrabFreePhysicalPages( const u32 count, VirtualSlotID virtualSlotID, PhysicalPageID* outIDs );
		u32 ReturnPhysicalPages( const VirtualSlotID virtualSlotID, PhysicalPageID* outReturnedIDs );

		//SystemBlock AllocateBlock( u32 size, u32 alignment );
		//void DeallocateBlock( Block & block );
		//VirtualRange ReserveRange( u32 size, u32 alignment );
		//void ReleaseRange( const VirtualRange & range );

		VirtualRange m_virtualRangeReservedBySystem;	// The range reserved by system, the one to release when we're done
		VirtualRange m_virtualRange;					// The range to use, with alignment
		VirtualRange m_bookkeepingListVirtualRange;

		SystemAllocator * m_systemAllocator;

		off_t m_physicalPoolAddress;	// Address of the whole physical memory allocation, made on initialization.
		u64 m_physicalBudget;			// Overall physical budget
		u64 m_numPhysicalPages;			// Number of physical pages
		u64 m_physicalPageSize;					// Size of the physical page
		u64 m_virtualSlotSize;			// Each allocation is using this uniform virtual size.
		u64 m_numVirtualSlots;			// Overall number of virtual slots.
		u32 m_flags;					// Flags...

		// Virtual slots bookkeeping
		i16* m_freeVirtualSlotIDs;							// Virtual slots that are free for the taking
		PhysicalPageID* m_virtualSlotToHeadPhysicalPage;	// A map, from  virtual slot to head physical page. If it's PhysicalPageIDInvalid then it's unused. Otherwise it is an index into m_physicalPageIDsInUseList.

		i64	 m_numFreeVirtualSlots;

		// Physical page bookkeeping	
		const PhysicalPageID PhysicalPageIDInvalid = 0xFFFFFFFF;			// Denotes unused page
		const PhysicalPageID MaxPhysicalpageID = PhysicalPageIDInvalid - 1;	// Denotes max page index
		PhysicalPageIDListNode* m_physicalPageIDsInUseList;					// Heap for physical page IDs. Each entry can optionally point to the next one, until the whole virtual size of the resource is covered.
		PhysicalPageID* m_freePhysicalPageIDs;	// Physical pages that are free for the taking
		i64  m_numFreePhysicalPages;			// Num physical pages that are free for the taking

		// These can help us figure out from memory dump, that we run out of page table
		// If we never failed a kernelMap so far, those will remain unused (-1)
		struct PageTableStats
		{
			i32 cpuTotal = -1;
			i32 cpuAvailable = -1;
			i32 gpuTotal = -1;
			i32 gpuAvailable = -1;
		} m_lastPageTableStats;
		
		u64	m_lowestFreePhysicalMemoryEver;

		Bool m_allocatorIsFull;

		constexpr static i32 cMaxNumPagesPerAllocEver = 1200;
		SceKernelBatchMapEntry	m_tempMapEntries[cMaxNumPagesPerAllocEver*2];
		PhysicalPageID			m_tempPhysicalPageIDs[cMaxNumPagesPerAllocEver];
		mutable SpinLock m_lock;

		u32* m_realSizes;			// What are true sizes of allocation in each virtual slot		
		//u8 m_padding[8 ];
	};

	//static_assert( sizeof( VirtualGPUAllocator ) == 128, "Virtual GPU allocator has to fit in two cache lines" );

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////

	struct GpuAllocatorParameter
	{
		SystemAllocator * systemAllocator;
	};

	struct GpuAllocatorMetrics
	{
		AllocatorMetrics metrics;
		TLSFAllocatorMetrics tlsfAllocatorMetrics;
		BigSizeAllocatorMetrics bigSizeAllocatorMetrics;
	};

	class RED_MEMORY_API GpuAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( GpuAllocator, GpuAllocatorMetrics, 16 );

		GpuAllocator();
		~GpuAllocator();

		void Initialize( const GpuAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void BuildMetrics( GpuAllocatorMetrics & metrics, Bool virtualAllocatorOnly = false );
		void SerializeMetrics( Serializer & serializer );

	private:
		Uint32 m_tlsfMaxSize;

		RED_ALIGN( 64 ) LockingDynamicTLSFAllocator m_tlsfAllocator;
		RED_ALIGN( 64 ) BigSizeAllocator m_bigSizeAllocator;
	};

	//////////////////////////////////////////////////////////////////////////

	// An allocator dedicated to allocating physical memory that is actually physically contiguous
	// !!!!!!!! ONLY REQUIRED FOR SWAP CHAIN !!!!!!!!!!!!!
	// !!!!!!!! DON'T USE IT FOR ANYTHING ELSE !!!!!!!!!!!
	struct GpuContiguousAllocatorParameter
	{
		SystemAllocator * systemAllocator;
	};
	struct GpuContiguousAllocatorMetrics
	{
		u32 usedMemory;
	};

	class RED_MEMORY_API GpuContiguousAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( GpuContiguousAllocator, GpuContiguousAllocatorMetrics, RED_KILO_BYTE( 64 ) );

		GpuContiguousAllocator();
		~GpuContiguousAllocator();

		void Initialize( const GpuContiguousAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void BuildMetrics( GpuContiguousAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:
		constexpr static u32 MaxAllocCount = 4;
		// Space for just a few allocations is enough, this is only for SwapChain
		struct Alloc
		{
			VirtualRange range;
			SystemBlock  sysBlock;
			Bool IsValid() const { return range.start != 0 && range.end != 0 && sysBlock != NullSystemBlock(); }
		}					m_allocs[MaxAllocCount];
		u32					m_numAllocs;
		SystemAllocator *	m_systemAllocator;
		u32					m_pageSize;
	};
#endif
}
}

#endif

