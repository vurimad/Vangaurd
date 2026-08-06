/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "systemAllocatorOrbisHelper.h"
#include "flags.h"
#include "virtualRange.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#endif

#if !defined( RED_CONFIGURATION_FINAL )
#define RED_ORBIS_CHECKED_MEMORY
#endif

namespace red
{
namespace memory
{

namespace dd
{
	static red::CrashData< Int32 > s_commitFailedErrorCode{ "Engine", "CommitFailedErrorCode", 0 };
}

namespace internal
{
	const u64 c_orbisSystemPageSize = 16 * 1024;
	const off_t c_orbisInvalidOffset = ~0;

	u32 ComputePageProtectionFlags( u32 flags )
	{
		u32 result = 0;
		result |= ( flags & Flags_CPU_Read ) ? SCE_KERNEL_PROT_CPU_READ : 0;
		result |= ( flags & Flags_CPU_Write ) ? SCE_KERNEL_PROT_CPU_WRITE : 0;
		result |= ( flags & Flags_GPU_Read ) ? SCE_KERNEL_PROT_GPU_READ : 0;
		result |= ( flags & Flags_GPU_Write ) ? SCE_KERNEL_PROT_GPU_WRITE : 0;
		return result;
	}

	off_t AllocateDirectMemory( u64 size, SceKernelMemoryType type, u32 alignment )
	{
		off_t foundOffset = 0;
		i32 result = sceKernelAllocateDirectMemory( 
			0, // Search the entire physical memory space
			SCE_KERNEL_MAIN_DMEM_SIZE,
			size,
			alignment, // 0 - default alignment, 16k 
			type,
			&foundOffset
		);

		if( result != SCE_OK )
		{
			if( result == SCE_KERNEL_ERROR_EINVAL )
			{
				RED_MEMORY_HALT( "Invalid parameter passed to sceKernelAllocateDirectMemory" );
			}
			else
			{ /* Failed to allocate Direct memory. OOM */ }

			dd::s_commitFailedErrorCode.Set( result );

			return c_orbisInvalidOffset;
		}

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatAllocPhysicalMemory( foundOffset, size, alignment, type );
#endif

		return foundOffset;
	}

	void FreeDirectMemory( off_t offset, u64 size )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatReleasePhysicalMemory( offset, size );
#endif

#if defined( RED_ORBIS_CHECKED_MEMORY )
		i32 result = sceKernelCheckedReleaseDirectMemory( offset, size );
#else
		i32 result = sceKernelReleaseDirectMemory( offset, size );
#endif
		if( result != SCE_OK )
		{
			RED_MEMORY_HALT( "Invalid argument passed to sceKernelReleaseDirectMemory" );
		}
	}

	void FreeFlexibleMemory( void * address, u64 size )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatReleaseFlexibleMemory( address, size );
#endif

		i32 result = sceKernelReleaseFlexibleMemory( address, size );
		if( result != SCE_OK )
		{
			RED_MEMORY_HALT( "Invalid argument passed to sceKernelReleaseFlexibleMemory" );
		}
	}

	SystemBlock MapDirectMemory( void * address, const u64 sizeRoundedToPageSize, u32 alignment, SceKernelMemoryType type, u32 protectFlag, off_t physicalMemoryOffset, Bool noCoalesceBlocks )
	{
		const int flags = noCoalesceBlocks ? SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_COALESCE | SCE_KERNEL_MAP_NO_OVERWRITE : SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_OVERWRITE;

		i32 result = sceKernelMapDirectMemory( 
			&address, 
			sizeRoundedToPageSize,
			protectFlag,
			flags,
			physicalMemoryOffset, 
			alignment // 0 - default alignment, 16k 
			);

		if( result != SCE_OK )
		{
			if( result == SCE_KERNEL_ERROR_EACCES )
			{
				RED_MEMORY_HALT( "Access is prohibited for the area specified as the map destination" );
			}
			else if( result == SCE_KERNEL_ERROR_EINVAL )
			{
				RED_MEMORY_HALT( "Invalid parameters passed to sceKernelMapDirectMemory" );
			}
			else
			{ /* Failed to find free space to allocate direct memory. System out of memory. */ }

			dd::s_commitFailedErrorCode.Set( result );

			return NullSystemBlock();
		}

		u64 resultAddress = reinterpret_cast< u64 >( address );
		SystemBlock resultBlock = { resultAddress, sizeRoundedToPageSize };

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatMapDirectMemory( address, sizeRoundedToPageSize, protectFlag, flags, physicalMemoryOffset, alignment, type );
#else
		RED_UNUSED( type );
#endif

		return resultBlock;
	}

	SystemBlock CommitBlockToDirectMemory( const SystemBlock & block, u32 flags )
	{
		const u64 sizeRoundedToPageSize = RoundUp( block.size, c_orbisSystemPageSize );
		const u64 alignedAddress = AlignAddress( block.address, c_orbisSystemPageSize );
		void * address = reinterpret_cast< void * >( alignedAddress );

		u32 protectFlag = ComputePageProtectionFlags( flags );
		SceKernelMemoryType type = flags & Flags_Garlic_Bus ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION;

		off_t physicalMemoryOffset = AllocateDirectMemory( sizeRoundedToPageSize, type, c_orbisSystemPageSize );

		if( physicalMemoryOffset != c_orbisInvalidOffset )
		{
			const Bool noCoalesceBlocks = flags & Flags_No_Coalesce_Blocks;
			return MapDirectMemory( address, sizeRoundedToPageSize, c_orbisSystemPageSize, type, protectFlag, physicalMemoryOffset, noCoalesceBlocks );
		}

		return NullSystemBlock(); 	
	}

	SystemBlock CommitAlignedBlockToDirectMemory( const SystemBlock & block, u32 flags, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Alignment has to be power of 2." );
		RED_MEMORY_ASSERT( alignment >= c_orbisSystemPageSize, "Alignment has to be greater or equal to page size." );

		const u64 sizeRoundedToPageSize = RoundUp( block.size, c_orbisSystemPageSize );
		const u64 alignedAddress = AlignAddress( block.address, alignment );
		void * address = reinterpret_cast< void * >( alignedAddress );

		u32 protectFlag = ComputePageProtectionFlags( flags );
		SceKernelMemoryType type = flags & Flags_Garlic_Bus ? SCE_KERNEL_WC_GARLIC : SCE_KERNEL_WB_ONION;

		off_t physicalMemoryOffset = AllocateDirectMemory( sizeRoundedToPageSize, type, alignment );

		if( physicalMemoryOffset != c_orbisInvalidOffset )
		{
			const Bool noCoalesceBlocks = flags & Flags_No_Coalesce_Blocks;
			return MapDirectMemory( address, sizeRoundedToPageSize, alignment, type, protectFlag, physicalMemoryOffset, noCoalesceBlocks );
		}

		return NullSystemBlock(); 	
	}

	SystemBlock CommitBlockToFlexibleMemory( const SystemBlock & block, u32 flags )
	{
		const u64 sizeRoundedToPageSize = RoundUp( block.size, c_orbisSystemPageSize );
		const u64 alignedAddress = AlignAddress( block.address, c_orbisSystemPageSize );
		void * address = reinterpret_cast< void * >( alignedAddress );
		u32 protectFlag = ComputePageProtectionFlags( flags );

		i32 result = sceKernelMapFlexibleMemory( 
			&address, 
			sizeRoundedToPageSize,
			protectFlag,
			SCE_KERNEL_MAP_FIXED 
			);

		if( result != SCE_OK )
		{
			if( result == SCE_KERNEL_ERROR_EACCES )
			{
				RED_MEMORY_HALT( "Access is prohibited for the area specified as the map destination" );
			}
			else if( result == SCE_KERNEL_ERROR_EINVAL )
			{
				RED_MEMORY_HALT( "Invalid parameters passed to sceKernelMapFlexibleMemory" );
			}
			else
			{ /* Failed to find free space to allocate flexible memory. System out of memory. */ }

			return NullSystemBlock();
		}

		u64 resultAddress = reinterpret_cast< u64 >( address );
		SystemBlock resultBlock = { resultAddress, sizeRoundedToPageSize };

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatMapFlexibleMemory( address, sizeRoundedToPageSize, protectFlag, flags );
#endif

		return resultBlock;
	}

	SystemBlock CommitAlignedBlockToFlexibleMemory( const SystemBlock & block, u32 flags, u32 /*alignment*/ )
	{
		return CommitBlockToFlexibleMemory( block, flags );
	}

	u64 DecommitRange( const VirtualRange & range )
	{
		// On Orbis we have to glue/unglue everything ourselves. 
		// When releasing a range, we need to make sure all physical memory bound to that range is given back to OS.

		i32 virtualQueryResult = SCE_OK;
		void * currentAddress = reinterpret_cast< void* >( range.start );
		const u64 endAddressValue = range.end;
		u64 memoryDecommited = 0;

		while( virtualQueryResult == SCE_OK )
		{
			SceKernelVirtualQueryInfo virtualMemoryInfo = {};

			virtualQueryResult = sceKernelVirtualQuery( 
				currentAddress, 
				SCE_KERNEL_VQ_FIND_NEXT, // This flag means it will give next valid virtual address if provided address was not mapped.
				&virtualMemoryInfo, 
				sizeof( virtualMemoryInfo ) 
				);

			// With SCE_KERNEL_VQ_FIND_NEXT flag, sceKernelVirtualQuery return SCE_KERNEL_ERROR_EACCES when there is nothing mapped anymore.
			if( virtualQueryResult != SCE_KERNEL_ERROR_EACCES ) 
			{
				if( virtualQueryResult == SCE_OK )
				{
					RED_MEMORY_ASSERT( virtualMemoryInfo.isDirectMemory, "Range was not allocated in Flexible memory." );

					const u64 startAddress = reinterpret_cast< u64 >( virtualMemoryInfo.start );
					const u64 endAddress = reinterpret_cast< u64 >( virtualMemoryInfo.end );

					if( endAddressValue > startAddress )
					{
						const u64 size = reinterpret_cast< u64 >( endAddress - startAddress );
						virtualMemoryInfo.isDirectMemory ? FreeDirectMemory( virtualMemoryInfo.offset, size ) : FreeFlexibleMemory( virtualMemoryInfo.start, size ) ;
						
						memoryDecommited += size; 
					}
					else
					{
						break; // virtual memory is out of range. We are done.
					}
				}
				else if( virtualQueryResult == SCE_KERNEL_ERROR_EINVAL )
				{
					RED_MEMORY_HALT( "Ptr address is out of virtual memory address space" );
				}
				else
				{
					RED_MEMORY_HALT( "An unknown error occurred while attempting to query a virtual address" );
				}
			}
			else 
			{ /* Everything was released. We are done here.*/ }
		}

		return memoryDecommited;
	}

	void DecommitBlock( const SystemBlock & block )
	{
		void * address = reinterpret_cast< void* >( block.address );

		// On Orbis we have to glue everything ourselves. 
		// Decommiting virtual memory means the we need also to release physical memory bound to it.
		// In order to release direct memory based on its virtual address, it is necessary to
		// call virtualQuery in order to get the mapped physical offset

		SceKernelVirtualQueryInfo virtualMemoryInfo = {};
		i32 result = sceKernelVirtualQuery( address, 0, &virtualMemoryInfo, sizeof( virtualMemoryInfo ) );

		if( result != SCE_OK )
		{
			if( result == SCE_KERNEL_ERROR_EACCES )
			{
				RED_MEMORY_HALT( "Memory specified by ptr is not mapped" );
			}
			else if( result == SCE_KERNEL_ERROR_EINVAL )
			{
				RED_MEMORY_HALT( "Ptr address is out of virtual memory address space" );
			}
			else
			{
				RED_MEMORY_HALT( "An unknown error occurred while attempting to query a virtual address" );
			}
		}
		else
		{
			virtualMemoryInfo.isDirectMemory ? FreeDirectMemory( virtualMemoryInfo.offset, block.size ) : FreeFlexibleMemory( virtualMemoryInfo.start, block.size );
		}
	}

	void PartialDecommitBlock( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		void * blockAddress = reinterpret_cast< void* >( block.address );
		void * partialBlockAddress = reinterpret_cast< void* >( partialBlock.address );

		// On Orbis we have to glue everything ourselves. 
		// Decommiting virtual memory means the we need also to release physical memory bound to it.
		// In order to release direct memory based on its virtual address, it is necessary to
		// call virtualQuery in order to get the mapped physical offset

		SceKernelVirtualQueryInfo blockVirtualMemoryInfo = {};
		const i32 blockResult = sceKernelVirtualQuery( blockAddress, 0, &blockVirtualMemoryInfo, sizeof( blockVirtualMemoryInfo ) );

		SceKernelVirtualQueryInfo partialBlockVirtualMemoryInfo = {};
		const i32 partialBlockResult = sceKernelVirtualQuery( partialBlockAddress, 0, &partialBlockVirtualMemoryInfo, sizeof( partialBlockVirtualMemoryInfo) );

		if ( blockResult != SCE_OK || partialBlockResult != SCE_OK )
		{
			if ( blockResult == SCE_KERNEL_ERROR_EACCES || partialBlockResult == SCE_KERNEL_ERROR_EACCES )
			{
				RED_MEMORY_HALT( "Memory specified by ptr is not mapped" );
			}
			else if ( blockResult == SCE_KERNEL_ERROR_EINVAL || partialBlockResult == SCE_KERNEL_ERROR_EINVAL )
			{
				RED_MEMORY_HALT( "Ptr address is out of virtual memory address space" );
			}
			else
			{
				RED_MEMORY_HALT( "An unknown error occurred while attempting to query a virtual address" );
			}
		}
		else
		{
			if ( blockVirtualMemoryInfo.isDirectMemory )
			{
				RED_MEMORY_ASSERT( blockVirtualMemoryInfo.offset == partialBlockVirtualMemoryInfo.offset, "Partial block needs to alocated on the same physical page as given block." );
				const u64 remainingSize = block.size - partialBlock.size;
				const u64 alignedAddress = RoundUp( blockVirtualMemoryInfo.offset + remainingSize, c_orbisSystemPageSize );
				FreeDirectMemory( alignedAddress, partialBlock.size );

#if defined( RED_ORBIS_CHECKED_MEMORY )
				// NOTE confirm virtual memory range matches reduction requested
				SceKernelVirtualQueryInfo freeResultVirtualMemoryInfo = {};
				const i32 freeBlockResult = sceKernelVirtualQuery( blockAddress, 0, &freeResultVirtualMemoryInfo, sizeof( freeResultVirtualMemoryInfo) );
				RED_MEMORY_ASSERT( freeBlockResult == SCE_OK, "Failed to query partial decommit results" );
				RED_MEMORY_ASSERT( ( (u64)freeResultVirtualMemoryInfo.end - (u64)freeResultVirtualMemoryInfo.start ) == remainingSize, "Remaining allocated memory does not match request decommit" );
#endif
			}
			else
			{
				const u64 startVirtualAddress = reinterpret_cast< u64 >( blockVirtualMemoryInfo.start );
				const u64 endVirtualAddress = reinterpret_cast< u64 >( blockVirtualMemoryInfo.end );
				const u64 alignedAddress = RoundUp( reinterpret_cast< u64 >( blockVirtualMemoryInfo.start ) + partialBlock.size, c_orbisSystemPageSize );
				RED_MEMORY_ASSERT( alignedAddress >= startVirtualAddress && alignedAddress < endVirtualAddress, "Partial block needs to fit in to given block." );
				FreeFlexibleMemory( reinterpret_cast< void* >( blockVirtualMemoryInfo.start ), block.size );
			}
		}
	}
}
}
}
