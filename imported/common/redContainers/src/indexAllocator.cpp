/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "indexAllocator.h"

namespace red
{
	IndexAllocator::IndexAllocator()
	{
	}

	IndexAllocator::IndexAllocator( const IndexAllocator& ) = default;
	IndexAllocator::IndexAllocator( IndexAllocator&& ) = default;
	IndexAllocator::~IndexAllocator() = default;

	IndexAllocator& IndexAllocator::operator=( const IndexAllocator& ) = default;
	IndexAllocator& IndexAllocator::operator=( IndexAllocator&& ) = default;


	RED_INLINE Bool IndexAllocator::compareIndexWithRange( Uint32 index, const Range& range )
	{
		return index < range.m_beginIndex;
	}

	Bool IndexAllocator::IsUsed( Uint32 index ) const
	{
		if ( !m_ranges.Empty() )
		{
			red::DynArray< Range >::const_iterator iter = std::upper_bound( m_ranges.Begin(), m_ranges.End(), index, compareIndexWithRange );   // Find first range with (m_beginIndex > index).
			if ( iter != m_ranges.Begin() )
			{
				--iter;
				if ( (index >= iter->m_beginIndex) && (index < iter->m_endIndex) )
				{
					return true;
				}
			}
		}

		return false;
	}

	Uint32 IndexAllocator::FindNextFree( Uint32 index ) const
	{
		if ( m_ranges.Empty() )
			return index;

		red::DynArray< Range >::const_iterator iter = std::upper_bound( m_ranges.Begin(), m_ranges.End(), index, compareIndexWithRange );    // Find first range with (m_beginIndex > index).
		if ( iter == m_ranges.Begin() )
		{
			return index;            // All ranges contain only larger indices. The one we are looking for is not present.
		}

		const Range& prevRange = *(--iter);                           // We can safely call operator--() also on an end iterator.
		RED_FATAL_ASSERT( prevRange.m_beginIndex <= index );          // Previous range must start at smaller index.
		if ( prevRange.m_endIndex <= index )
			return index;
		else
			return prevRange.m_endIndex;
	}

	Uint32 IndexAllocator::AllocateNextFree( Uint32 index )
	{
		if ( m_ranges.Empty() )
		{
			m_ranges.PushBack( Range{ index, index + 1u } );
			return index;
		}

		red::DynArray< Range >::iterator iter = std::upper_bound( m_ranges.Begin(), m_ranges.End(), index, compareIndexWithRange );   // Find first range with (m_beginIndex > index).
		if ( iter == m_ranges.Begin() )
		{                                                                    // All ranges contain only larger indices. The one we are looking for is not present.
			if ( index == (iter->m_beginIndex - 1u) )
				iter->m_beginIndex = index;                                  // This is the first range, we do not need to care about merging with previous range (there is no previous one).
			else
			{
				m_ranges.Insert( m_ranges.Begin(), Range{ index, index + 1u } );
			}
			return index;
		}
		else if ( iter == m_ranges.End() )
		{                                                                    // All ranges have their firstIndex <= index. We will just check the last one.
			Range& lastRange = m_ranges.Back();
			RED_FATAL_ASSERT( lastRange.m_beginIndex <= index );             // Last range must start at smaller index.

			if ( index <= lastRange.m_endIndex )
				return lastRange.m_endIndex++;                               // We have collision, the index falls into the last range (or the index was equal to the end of the last range).
			else
			{
				m_ranges.PushBack( Range{ index, index + 1u } );
				return index;
			}
		}

		// Most complex case, we are inserting "in the middle".
		red::DynArray< Range >::iterator prevIter = iter - 1;
		RED_FATAL_ASSERT( index < iter->m_beginIndex );
		RED_FATAL_ASSERT( index >= prevIter->m_beginIndex );
		RED_FATAL_ASSERT( prevIter->m_endIndex < iter->m_beginIndex );       // There must be at least one index gap between ranges, otherwise they would be merged.

		if ( index <= prevIter->m_endIndex )
			index = prevIter->m_endIndex++;
		else if ( index == (iter->m_beginIndex - 1u) )
			iter->m_beginIndex = index;
		else
		{
			m_ranges.Insert( iter, Range{ index, index + 1u } );                  // Does not fall into previous or next range, ranges do not merge.
			return index;
		}

		if ( prevIter->m_endIndex == iter->m_beginIndex )                    // Merging two ranges.
		{
			prevIter->m_endIndex = iter->m_endIndex;
			m_ranges.Remove( iter );
		}
		return index;
	}

	void IndexAllocator::Free( Uint32 index )
	{
		if ( !m_ranges.Empty() )
		{
			red::DynArray< Range >::iterator iter = std::upper_bound( m_ranges.Begin(), m_ranges.End(), index, compareIndexWithRange );   // Find first range with (m_beginIndex > index).
			if ( iter != m_ranges.Begin() )
			{
				--iter;
				if ( (index >= iter->m_beginIndex) && (index < iter->m_endIndex) )
				{
					Uint32 rangeSize = iter->m_endIndex - iter->m_beginIndex;
					if ( rangeSize == 1 )
					{
						m_ranges.Remove( iter );
					}
					else if ( index == iter->m_beginIndex )
						++iter->m_beginIndex;
					else if ( index == (iter->m_endIndex - 1u) )
						--iter->m_endIndex;
					else
					{
						Range newRange = { index + 1, iter->m_endIndex };
						iter->m_endIndex = index;

						m_ranges.Insert( iter + 1, newRange );
					}
					return;
				}
			}
		}

		RED_FATAL( "Unable to remove index %u from indices list, index was not found", index );
	}

	void IndexAllocator::Clear()
	{
		m_ranges.Clear();
	}

	void IndexAllocator::Validate() const
	{
		for ( Uint32 i = 0; i < m_ranges.Size(); ++i )
		{
			const Range& range = m_ranges[i];
			RED_FATAL_ASSERT( range.m_beginIndex < range.m_endIndex );
			if ( i > 0 )
			{
				const Range& prevRange = m_ranges[i - 1];
				RED_FATAL_ASSERT( prevRange.m_endIndex < range.m_beginIndex );
			}
		}
	}

	Uint32 IndexAllocator::Size() const
	{
		Uint32 numIndices = 0;
		for ( const Range& range : m_ranges )
		{
			Uint32 rangeSize = range.m_endIndex - range.m_beginIndex;
			numIndices += rangeSize;
		}
		return numIndices;
	}

	Bool IndexAllocator::Empty() const
	{
		return m_ranges.Empty();
	}

	String IndexAllocator::ToString() const
	{
		String str;
		for ( const Range& range : m_ranges )
		{
			if ( !str.Empty() )
				str += ", ";
			if ( range.m_beginIndex == (range.m_endIndex - 1u) )
				str += String::Printf( "%u", range.m_beginIndex );
			else
				str += String::Printf( "<%u,%u>", range.m_beginIndex, range.m_endIndex - 1u );
		}
		return str;
	}

	red::DynArray< Uint32 > IndexAllocator::ToArray() const
	{
		red::DynArray< Uint32 > indices{ red::PoolEngine() };
		for ( const Range& range : m_ranges )
		{
			Uint32 index = range.m_beginIndex;
			Uint32 rangeSize = range.m_endIndex - range.m_beginIndex;
			while ( rangeSize > 0 )
			{
				indices.PushBack( index++ );
				--rangeSize;
			}
		}
		return indices;
	}

} // red
