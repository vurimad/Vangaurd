/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynArray.h"

namespace red
{
	// Utility class for allocating Uint32 indices, maintaining a set of allocated indices, making sure we allocate a unique (free) one etc.
	// It was designed to help out name suffixes handling ("name1", "name2", "name345" etc), but can be used as a generic Uint32 set.
	// The implementation may vary, but currently it is implemented as an array of index ranges.
	class RED_CONTAINERS_API IndexAllocator
	{
	public:
		IndexAllocator();
		IndexAllocator( const IndexAllocator& );
		IndexAllocator( IndexAllocator&& );

		IndexAllocator& operator=( const IndexAllocator& );
		IndexAllocator& operator=( IndexAllocator&& );

		~IndexAllocator();

		// Test if given index is used (allocated).
		Bool IsUsed( Uint32 index ) const;
		// If initialIndex is not already used by the set it will be returned.
		// If it is used, then next free index will be returned.
		Uint32 FindNextFree( Uint32 initialIndex ) const;
		// Insert (allocate) new index into the set.
		// If initialIndex is not already used by the set it will be allocated and returned.
		// If it is used, then next free index will be allocated and returned.
		Uint32 AllocateNextFree( Uint32 initialIndex );
		// Release allocated index (deallocate)
		void Free( Uint32 index );
		// Remove all indices, the set will become empty.
		void Clear();

		// Validate correctness of the index set.
		void Validate() const;
		// Get number of all allocated indices.
		Uint32 Size() const;
		// Test if the set is empty.
		Bool Empty() const;

		// Print the set of indices in some readable and compact form (i.e. in the form of index ranges).
		String ToString() const;

		// Convert index set to an array (sorted) of indices.
		// This is mostly for the purpose of unit tests.
		red::DynArray< Uint32 > ToArray() const;

	private:
		struct Range
		{
			Uint32 m_beginIndex;     //< Inclusive, this index is "inside" the range.
			Uint32 m_endIndex;       //< Exclusive, this index is the first one "outside" the range.
		};

		static Bool compareIndexWithRange( Uint32 index, const Range& range );


		red::DynArray< Range > m_ranges{ red::PoolEngine() };
	};

} // red
