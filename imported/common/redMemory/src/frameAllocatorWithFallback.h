/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FRAME_ALLOCATOR_WITH_FALLBACK_H_
#define _RED_MEMORY_FRAME_ALLOCATOR_WITH_FALLBACK_H_

#include "locklessFrameAllocator.h"

namespace red
{
namespace memory
{
	struct FrameAllocatorWithFallbackMetrics
	{
		FrameAllocatorMetrics metrics;
		u64 outOfBudgetUsedBytes;
	};

	class RED_ALIGN( 64 ) RED_MEMORY_API FrameAllocatorWithFallback : NonCopyable
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( FrameAllocatorWithFallback, FrameAllocatorWithFallbackMetrics, 8 );

		FrameAllocatorWithFallback();
		~FrameAllocatorWithFallback();

		void Initialize( const FrameAllocatorParameter& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void Reset();

		void BuildMetrics( FrameAllocatorWithFallbackMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

		u64 Debug_GetOutOfBudgetBytes() const;

	private:
		Block ReallocateFromFrameAllocator( Block& block, u32 size );
		Block ReallocateFromDefaultAllocator( DefaultAllocator& defaultAllocator, Block& block, u32 size );

		Block ReallocateFromFrameAllocator( Block& block, u32 size, u32 alignment );
		Block ReallocateFromDefaultAllocator( DefaultAllocator& defaultAllocator, Block& block, u32 size, u32 alignment );

		LocklessFrameAllocator m_frameAllocator;
#ifndef RED_CONFIGURATION_FINAL
		mutable red::UpdateFlag m_debugBanFrameAllocator;
		mutable atomic::TAtomic64 m_outOfBudgetUsedBytes;
#endif
	};
}
}

#endif