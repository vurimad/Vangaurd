/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKLESS_DEBUG_BAN_FRAME_ALLOCATOR_H_
#define _RED_MEMORY_LOCKLESS_DEBUG_BAN_FRAME_ALLOCATOR_H_

#ifndef RED_CONFIGURATION_FINAL

#include "locklessFrameAllocator.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace red
{
namespace memory
{
	const Bool c_checkDebugBanFrameAllocator = true;

	class RED_ALIGN( 64 ) RED_MEMORY_API LocklessDebugBanFrameAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( LocklessDebugBanFrameAllocator, FrameAllocatorMetrics, 8 );

		LocklessDebugBanFrameAllocator();
		~LocklessDebugBanFrameAllocator();

		void Initialize( const FrameAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void Reset();

		void BuildMetrics( FrameAllocatorMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

	private:
		LocklessFrameAllocator m_allocator;
		mutable red::UpdateFlag m_debugBanFrameAllocator;
	};
}
}

#endif

#endif 

