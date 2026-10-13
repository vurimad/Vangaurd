/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_UNSAFE_DYNAMIC_LINEAR_ALLOCATOR_H_
#define _RED_MEMORY_UNSAFE_DYNAMIC_LINEAR_ALLOCATOR_H_

#include "allocator.h"
#include "linearAllocator.h"
#include "../include/block.h"
#include "../include/virtualRange.h"
#include "../include/systemBlock.h"

namespace red
{
namespace memory
{
class Serializer;
class SystemAllocator;

struct UnsafeDynamicLinearAllocatorParameter
{
	SystemAllocator* systemAllocator;
	u32 chunkSize;
	u32 flags;
};

class RED_MEMORY_API UnsafeDynamicLinearAllocator
{
public:
	RED_MEMORY_DECLARE_ALLOCATOR( UnsafeDynamicLinearAllocator, LinearAllocatorMetrics, LinearAllocator::DefaultAlignmentType::value );

	UnsafeDynamicLinearAllocator();
	~UnsafeDynamicLinearAllocator();

	void Initialize( const UnsafeDynamicLinearAllocatorParameter& parameter );
	void Uninitialize();

	Block Allocate( u32 size );
	Block AllocateAligned( u32 size, u32 alignment );
	void Free( Block& block );
	Block Reallocate( Block& block, u32 size );
	Block ReallocateAligned( Block& block, u32 size, u32 alignment );

	Bool OwnBlock( u64 block ) const;
	u64 GetBlockSize( u64 address ) const;

	void Reset();

	void BuildMetrics( LinearAllocatorMetrics& metrics );
	void SerializeMetrics( Serializer& serializer );

	Block GetUsedMemoryRange() const;

private:
	bool IsInitialized() const;

	SystemBlock AllocateBlock( u32 size );
	bool CanCreateMoreBlock() const;

	Block Internal_Allocate( u32 size, u32 alignment );

	// start address of the buffer
	u64 m_startAddress;
	u64 m_endAddress;
	u64 m_position;

	VirtualRange m_virtualRange;
	u64 m_nextVirtualAddress;
	SystemAllocator* m_systemAllocator;
	u32 m_chunkSize;
	u32 m_flags;
};


} // memory
} // red

#endif // _RED_MEMORY_UNSAFE_DYNAMIC_LINEAR_ALLOCATOR_H_
