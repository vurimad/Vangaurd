/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SYSTEM_ALLOCATOR_ORBIS_HELPER_H_
#define _RED_MEMORY_SYSTEM_ALLOCATOR_ORBIS_HELPER_H_

#include "systemBlock.h"

namespace red { namespace memory { struct VirtualRange; } }

namespace red
{
namespace memory
{
namespace internal
{
	extern const u64 c_orbisSystemPageSize;

	u32 ComputePageProtectionFlags( u32 flags );

	SystemBlock CommitBlockToDirectMemory( const SystemBlock & block, u32 flags );
	SystemBlock CommitAlignedBlockToDirectMemory( const SystemBlock & block, u32 flags, u32 alignment );
	SystemBlock CommitBlockToFlexibleMemory( const SystemBlock & block, u32 flags );
	SystemBlock CommitAlignedBlockToFlexibleMemory( const SystemBlock & block, u32 flags, u32 alignment );

	u64 DecommitRange( const VirtualRange & range );
	void DecommitBlock( const SystemBlock & block );

	void PartialDecommitBlock( const SystemBlock & block, const SystemBlock & partialBlock );
}
}
}

#endif
