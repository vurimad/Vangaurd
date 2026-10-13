/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "flexibleSystemAllocator.h"
#include "systemAllocatorOrbisHelper.h"
#include "utils.h"

namespace red
{
namespace memory
{
	FlexibleSystemAllocator::FlexibleSystemAllocator()
		: m_totalFlexibleMemoryAvailable( 0 )
	{}

	FlexibleSystemAllocator::~FlexibleSystemAllocator()
	{}
	
	void FlexibleSystemAllocator::OnInitialize()
	{
		u64 size = 0;
		i32 result = sceKernelAvailableFlexibleMemorySize( &size );

		RED_MEMORY_ASSERT( result == SCE_OK, "SYSTEM ERROR cannot fetch Orbis available memory." );
		RED_UNUSED( result );
	
		m_totalFlexibleMemoryAvailable = size;
	}

	u64 FlexibleSystemAllocator::OnReleaseVirtualRange( const VirtualRange & range )
	{
		return internal::DecommitRange( range );
	}
	
	SystemBlock FlexibleSystemAllocator::OnCommit( const SystemBlock & block, u32 flags )
	{
		return internal::CommitBlockToFlexibleMemory( block, flags );
	}

	SystemBlock FlexibleSystemAllocator::OnCommitAligned( const SystemBlock & block, u32 flags, u32 alignment )
	{
		return internal::CommitAlignedBlockToFlexibleMemory( block, flags, alignment );
	}
	
	void FlexibleSystemAllocator::OnDecommit( const SystemBlock & block )
	{
		internal::DecommitBlock( block );
	}

	void FlexibleSystemAllocator::OnPartialDecommit( const SystemBlock & block, const SystemBlock & partialBlock )
	{
		internal::PartialDecommitBlock( block, partialBlock );
	}

	u64 FlexibleSystemAllocator::OnGetTotalPhysicalMemoryAvailable() const
	{
		return m_totalFlexibleMemoryAvailable;
	}

	u64 FlexibleSystemAllocator::OnGetCurrentPageMemoryAvailable() const
	{
		size_t availableFlexMem = 0;
		sceKernelAvailableFlexibleMemorySize( &availableFlexMem );
		return availableFlexMem;
	}

	u64 FlexibleSystemAllocator::OnGetPageSize() const
	{
		return internal::c_orbisSystemPageSize;
	}

	void FlexibleSystemAllocator::OnWriteReportToLog() const
	{
		size_t configuredFlexMem = 0;
		sceKernelConfiguredFlexibleMemorySize( &configuredFlexMem );
		RED_MEMORY_LOG( "Memory: sceKernelConfiguredFlexibleMemorySize() returned %lu", configuredFlexMem );

		size_t availableFlexMem = 0;
		sceKernelAvailableFlexibleMemorySize( &availableFlexMem );
		RED_MEMORY_LOG( "Memory: sceKernelAvailableFlexibleMemorySize() returned %lu", availableFlexMem );
	}

	void FlexibleSystemAllocator::OnWriteReportToJson( FILE* file ) const
	{
		size_t configuredFlexMem = 0;
		sceKernelConfiguredFlexibleMemorySize( &configuredFlexMem );
		std::fprintf( file, ",\"sceKernelConfiguredFlexibleMemorySize\":%lu", configuredFlexMem );

		size_t availableFlexMem = 0;
		sceKernelAvailableFlexibleMemorySize( &availableFlexMem );
		std::fprintf( file, ",\"sceKernelAvailableFlexibleMemorySize\":%lu", availableFlexMem );
	}
}
}
