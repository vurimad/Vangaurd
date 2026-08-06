/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynArray.h"

//////////////////////////////////////////////////////////////////////////
// This class allows to 'wrap' any DynArray object and access/modify it
// by knowing only size and alignment of its element type.
// It does not inherit form DynArray but needs to have the same memory layout:
//     void* buffer;
//     Uint32 capacity;
//     Uint32 size;
//
// Helpful when we have a pointer to a DynArray not knowing its element type
// (RTTI/serialization).
// Does not call constructors/destructor for inserted/remove elements;
//////////////////////////////////////////////////////////////////////////

namespace red {

class DynArrayAccessor : public DynamicBuffer
{
public:

	typedef DynamicBuffer						BufferType;
	typedef DynamicBuffer						BaseClass;

	// Destructor
	RED_INLINE ~DynArrayAccessor();

	// Get pointer to the allocated buffer 
	using BufferType::Data;
	// Get maximum number of elements that can be stored in the array without need of reallocation
	using BufferType::Capacity;
	// Get number of elements in the array
	RED_INLINE Uint32 Size() const { return m_size; }
	// Returns true if array contains 0 elements
	RED_INLINE Bool Empty() const { return m_size == 0; }

	// Remove all elements from the array
	RED_INLINE void Clear();
	// Change number of elements stored in the array; if new size is bigger, new elements are uninitialized; if new size is bigger than capacity, capacity grows by 1.5 factor
	RED_INLINE void Resize( Uint32 size, Uint32 elementSize, Uint32 alignment );
	// Change number of elements stored in the array; if new size is bigger, new elements are uninitialized; if new size is bigger than capacity, new capacity is equal to size
	RED_INLINE void ResizeExact( Uint32 size, Uint32 elementSize, Uint32 alignment );
	// Insert 'amount' number of elements at the end of the array; new elements are uninitialized; if new size is bigger than capacity, capacity grows by 1.5 factor
	RED_INLINE void Grow( Uint32 amount, Uint32 elementSize, Uint32 alignment );
	// Insert 'amount' number of elements at the end of the array; new elements are uninitialized; if new size is bigger than capacity, new capacity is equal to size
	RED_INLINE void GrowExact( Uint32 amount, Uint32 elementSize, Uint32 alignment );
	// Change array capacity (up)
	RED_INLINE void Reserve( Uint32 capacity, Uint32 elementSize, Uint32 alignment );
	// Change array capacity so that it is equal to array size
	RED_INLINE void Shrink( Uint32 elementSize, Uint32 alignment );

	// Swap array with other array
	RED_INLINE void Swap( DynArrayAccessor& other );

	// Construct DynArray at the specified place in the memory
	RED_INLINE static void Create( void *ptr );

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool( Uint32 elementSize ) const;
	
	// Get DynArrayAccessor reference pointing to the specified place in the memory
	RED_INLINE static DynArrayAccessor& GetRef( const void* ptr );
	// Get DynArrayAccessor reference pointing to the specified array
	template< typename TElement >
	RED_INLINE static DynArrayAccessor& GetRef( DynArray< TElement >& arr );

protected:

	Uint32	m_size;

	RED_INLINE DynArrayAccessor();
	RED_INLINE DynArrayAccessor( const DynArrayAccessor& arr );
	RED_INLINE DynArrayAccessor( DynArrayAccessor&& arr );

	RED_INLINE DynArrayAccessor& operator=( const DynArrayAccessor& arr );
	RED_INLINE DynArrayAccessor& operator=( DynArrayAccessor&& arr );
};

} // red

#include "dynArrayAccessor.hpp"
