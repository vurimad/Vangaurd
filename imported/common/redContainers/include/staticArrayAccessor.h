/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "staticArray.h"

namespace red {

//////////////////////////////////////////////////////////////////////////
// This class allows to 'wrap' any StaticArray object and access/modify it
// by knowing only size and alignment of its element type.
// It assumes that StaticArray's memory layout is as following:
//     Type array[ MaxSize ]
//     Uint32 size;
// 
// Thus, the address of "size" is this + sizeof( Type ) * MaxSize adjusted to proper alignment.
// Helpful when we have a pointer to a DynArray not knowing its element type
// (RTTI/serialization).
// Does not call constructors/destructor for inserted/remove elements;
//////////////////////////////////////////////////////////////////////////

class RED_CONTAINERS_API StaticArrayAccessor
{
public:

	// Get pointer to the buffer
	void* Data() const;
	// Get number of elements in the array
	Uint32 GetSize( Uint32 typeSize, Uint32 maxSize ) const;

	// Get pointer to the element at specified 'index'
	void* GetElement( Uint32 typeSize, Uint32 index );
	// Get const pointer to the element at specified 'index'
	const void* GetElement( Uint32 typeSize, Uint32 index ) const;

	// Remove all elements from the array
	void Clear( Uint32 typeSize, Uint32 maxSize );
	// Insert 'count' number of elements at the end of the array; new elements are uninitialized
	Uint32 Grow( Uint32 typeSize, Uint32 maxSize, Uint32 count );
	// Remove 'count' number of elements from the end of the array
	void Remove( Uint32 typeSize, Uint32 maxSize, Uint32 count );

	// Get StaticArrayAccessor reference pointing to the specified place in the memory
	static StaticArrayAccessor& GetRef( const void* ptr );
	// Get StaticArrayAccessor reference pointing to the specified array
	template< typename TElement, Uint32 MaxSize >
	RED_INLINE static StaticArrayAccessor& GetRef( StaticArray< TElement, MaxSize >& arr )
	{
		return reinterpret_cast< StaticArrayAccessor& >( arr );
	}

	// Calculate total size of the array
	static Uint32 CalcTypeSize( Uint32 typeSize, Uint32 maxSize, Uint32 typeAlignment );
	// Calculate alignment of the array
	static Uint32 CalcTypeAlignment( Uint32 typeAlignment );

private:

	RED_INLINE StaticArrayAccessor() { }

	Uint32& SizeRef( Uint32 typeSize, Uint32 maxSize ) const;
};

} // red
