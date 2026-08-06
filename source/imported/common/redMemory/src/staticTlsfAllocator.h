/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_STATIC_TLSF_ALLOCATOR_H_
#define _RED_MEMORY_STATIC_TLSF_ALLOCATOR_H_

#include "../include/utils.h"
#include "../include/block.h"
#include "allocator.h"
#include "tlsfAllocator.h"

namespace red
{
namespace memory
{
	struct StaticTLSFAllocatorParameter
	{
		void * buffer;
		u32 bufferSize;
	};

	class RED_MEMORY_API StaticTLSFAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( StaticTLSFAllocator, TLSFAllocatorMetrics, 16 );

		StaticTLSFAllocator();
		~StaticTLSFAllocator();

		void Initialize( const StaticTLSFAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block & block );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const; 
		u64 GetBlockSize( u64 address ) const;
		u64 GetBiggestBlockSize() const;

		void BuildMetrics( TLSFAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:

		StaticTLSFAllocator( const StaticTLSFAllocator& );
		StaticTLSFAllocator & operator=( const StaticTLSFAllocator& ); 

		bool IsInitialized() const;

		void ValidateParameter() const;

		RED_ALIGN( 64 ) TLSFAllocator m_allocator;
		StaticTLSFAllocatorParameter m_parameter;
	};

}
}

#endif
