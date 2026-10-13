/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FRAME_ALLOCATOR_H_
#define _RED_MEMORY_FRAME_ALLOCATOR_H_

#include "allocator.h"
#include "frameAllocatorUtils.h"

namespace red
{
namespace memory
{
	class RED_ALIGN( 64 ) RED_MEMORY_API FrameAllocator : NonCopyable
	{
	public:
		
		RED_MEMORY_DECLARE_ALLOCATOR( FrameAllocator, FrameAllocatorMetrics, 8 );

		FrameAllocator();
		~FrameAllocator();

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

		u32 Internal_GetNumberOfFrames() const;
		u64 Internal_GetCurrentPositionInFrameBlock() const;
		const VirtualRange& Internal_GetVirtualRange() const;

	private:

		Block GrowBlock( const Block & block, u32 size );
		bool IsInitialized() const;

		u64 m_currentPositionInFrameBlock;
		VirtualRange m_virtualRange;

		u64 m_nextFrameBlockAddress;

		SystemAllocator * m_systemAllocator;

#if defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )
		u64 m_firstCommitedFrameBlockAddress;
		u32 m_numberOfCommitedFrameBlocks;
#endif

		u32 m_frameBlockSize;
		u32 m_numberOfFrames;
		u32 m_flags;

#if !defined( RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK )
		char m_padding[8];
#endif
	};
}
}

#endif