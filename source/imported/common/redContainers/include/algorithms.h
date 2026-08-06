/**
* Copyright (c) 2013-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include <iterator>
#include <utility>
#include <algorithm>
#include <numeric>

#include "../../redSystem/include/utility.h"
#include "../../redMath/include/numericalUtils.h"
#include "../../redSystem/include/assert.h"
#include "containersCommon.h"
#include "arrayIterator.h"
#include "checkedIterator.h"

//------------------------------------------------------------------------------

// Common algorithms used by the various container classes

namespace red { namespace alg {

// This functions returns iterator to greatest value lower or equal to given value (LowerBound function is not doing that).
template<class _Iter, class _Val>
RED_INLINE _Iter LowerBoundIndex( _Iter begin, _Iter end, const _Val &val )
{
	_Iter endCopy = end;
	Uint32 count = red::Distance< Uint32 >( begin, end );

	while( count > 0 )
	{
		Uint32 halfCount = count / 2;

		_Iter middle = begin + halfCount;
		if ( *middle < val )
		{
			if ( ( middle + 1 < end && *( middle + 1 ) > val ) || middle + 1 == endCopy ) return middle;
			begin = middle+1;
			count = count - halfCount - 1;
		}
		else
		{
			count = halfCount;
		}	
	}

	return begin;
}

template<class _Iter, class _Val>
RED_INLINE _Iter BinarySearch( _Iter begin, _Iter end, const _Val &val )
{
	_Iter lowerBound = std::lower_bound( begin, end, val );
	return ( lowerBound != end ) && ( !( val < *lowerBound ) ) ? lowerBound : end;
}

template<class _Iter, class _Val, class _Pred>
RED_INLINE _Iter BinarySearch( _Iter begin, _Iter end, const _Val &val, const _Pred &pred )
{
	_Iter lowerBound = std::lower_bound( begin, end, val, pred );
	return ( lowerBound != end ) && ( !pred( val, *lowerBound ) ) ? lowerBound : end;
}

template < class Val, class Functor >
RED_INLINE Val FunctionalBinarySearch( const Val& minVal, const Val& maxVal, Functor functor )
{
	//ASSERT( functor.Accept(minVal) );
	Val vMin = minVal;			// Inclusive
	Val vMax = maxVal;			// Exclusive
	while ( !functor.Stop( vMax, vMin ) )
	{
		Val vRet = functor.Step( vMax, vMin );
		if (functor.Accept(vRet))
		{
			vMin = vRet;
		}
		else
		{
			vMax = vRet;
		}
	}
	return vMin;
};

//------------------------------------------------------------------------------

// PushBack
//	Push back element if not present in the array

template < typename TArray, typename TElement >
Bool PushBackUnique( TArray& arr, const TElement& element )
{
	if ( std::find( arr.Begin(), arr.End(), element ) == arr.End() )
	{
		arr.PushBack( element );
		return true;
	}
	return false;
}

template < typename TArray, typename TElement >
Bool PushBackUnique( TArray& arr, TElement&& element )
{
	if ( std::find( arr.Begin(), arr.End(), element ) == arr.End() )
	{
		arr.PushBack( std::forward< TElement >( element ) );
		return true;
	}
	return false;
}

template < typename TArray, typename Iterator >
void PushBackUnique( TArray& arr, Iterator from, Iterator to )
{
	for ( Iterator it = from; it != to; ++it )
	{
		PushBackUnique( arr, *it );
	}
}

//------------------------------------------------------------------------------

// Removes elements from the first iterator range if they are in the second iterator range
// Both input ranges must be sorted, otherwise the results are unpredictable
// This is a set difference operation removing elements from the first set if they are found in the second set
// Note: Maintains ordering of the ranges
template < typename Iterator1, typename Iterator2 >
Iterator1 RemoveIfIn( Iterator1 sourceIter, Iterator1 sourceEnd, Iterator2 excludeIter, Iterator2 excludeEnd )
{
	Iterator1 resultIter = sourceIter;
	while ( sourceIter != sourceEnd && excludeIter != excludeEnd )
	{
		if ( *sourceIter < *excludeIter )
		{
			if ( resultIter != sourceIter )
			{
				*resultIter = std::move( *sourceIter );
			}
			++resultIter;
			++sourceIter;
		}
		else if ( *excludeIter < *sourceIter )
		{
			++excludeIter;
		}
		else // *sourceIter == *excludeIter
		{
			++excludeIter;
			++sourceIter;
		}
	}

	while ( sourceIter != sourceEnd )
	{
		if ( resultIter != sourceIter )
		{
			*resultIter = std::move( *sourceIter );
		}
		++resultIter;
		++sourceIter;
	}

	return resultIter;
}

// Keeps elements from the first iterator range if they are in the second iterator range
// Both input ranges must be sorted, otherwise the results are unpredictable
// This is a set difference operation removing elements from the first set if they are not found in the second set
// Note: Maintains ordering of the ranges
template < typename Iterator1, typename Iterator2 >
Iterator1 KeepIfIn( Iterator1 sourceIter, Iterator1 sourceEnd, Iterator2 includeIter, Iterator2 includeEnd )
{
	Iterator1 resultIter = sourceIter;
	while ( sourceIter != sourceEnd && includeIter != includeEnd )
	{
		if ( *sourceIter == *includeIter )
		{
			if ( resultIter != sourceIter )
			{
				*resultIter = std::move( *sourceIter );
			}
			++resultIter;
			++sourceIter;
			++includeIter;
		}
		else
		{
			if ( *includeIter < *sourceIter )
			{
				++includeIter;
			}
			else // *sourceIter > *includeIter
			{
				++sourceIter;
			}
		}
	}

	return resultIter;
}

//------------------------------------------------------------------------------

// Helper to Remove duplicated entries
// NOTE: this works on arrays most efficiently
template < typename TArray >
RED_INLINE void RemoveDuplicates( TArray& arr )
{
	const auto begin = arr.Begin(), end = arr.End();
	if ( begin != end )
	{
		std::sort( begin, end );
		auto it = std::unique( begin, end );
		arr.Remove( it, end );
	}
}

template < typename TArray, typename Pred >
RED_INLINE decltype(auto) FindIf( TArray& arr, Pred&& p )
{
	return std::find_if(begin(arr), end(arr), std::forward<Pred>(p));
}

template < typename TArray, typename TElement >
RED_INLINE void EraseAll( TArray& arr, const TElement& element )
{
	arr.Remove( std::remove( begin( arr ), end( arr ), element ), end( arr ) );
}

template < typename TArray, typename Pred >
RED_INLINE void EraseIf( TArray& arr, Pred&& p )
{
	arr.Remove(std::remove_if(begin(arr), end(arr), std::forward<Pred>(p)), end(arr));
}

template < typename TArray, typename Pred >
RED_INLINE Bool AllOf( TArray& arr, Pred&& p )
{
	return std::all_of(begin(arr), end(arr), std::forward<Pred>(p));
}

template < typename TArray, typename Pred >
RED_INLINE Bool NoneOf( TArray& arr, Pred&& p )
{
	return std::none_of(begin(arr), end(arr), std::forward<Pred>(p));
}

template < typename TArray, typename Pred >
RED_INLINE Bool AnyOf( TArray& arr, Pred&& p )
{
	return std::any_of(begin(arr), end(arr), std::forward<Pred>(p));
}

template <typename TArray, typename Iterator, typename Pred >
RED_INLINE Iterator Transform( TArray& arr, Iterator& itr, Pred&& p )
{
	return std::transform(begin(arr), end(arr), itr, std::forward<Pred>(p));
}

template <typename TArray, typename Value, typename ReduceOp >
RED_INLINE Value Accumulate( TArray& arr, Value start, ReduceOp&& p )
{
	return std::accumulate(begin(arr), end(arr), start, std::forward<ReduceOp>(p));
}

//------------------------------------------------------------------------------

namespace ClearPtrImpl {

	template < typename TArray >
	void ClearPtr( TArray& arr, ArrayIteratorTag tag )
	{
		for ( typename TArray::iterator it = arr.Begin(), itEnd = arr.End(); it != itEnd; ++it )
		{
			RED_DELETE( *it );
		}
		// We need the following call to resize buffer to 0 - Resize( 0 ) will do the same.
		// It doesn't introduce additional overhead: Clear doesn't shrink the buffer and pointer's destructor is trivial.
		arr.Clear();
	}

	template < typename TMap >
	void ClearPtr( TMap& map, MapIteratorTag tag )
	{
		for ( typename TMap::iterator it = map.Begin(), itEnd = map.End(); it != itEnd; ++it )
		{
			RED_DELETE( it.Value() );
		}
		// We need the following call to resize buffer to 0 - Resize( 0 ) will do the same.
		// It doesn't introduce additional overhead: Clear doesn't shrink the buffer and pointer's destructor is trivial.
		map.Clear();
	}

} // ClearPtrImpl

template < typename TContainer >
void ClearPtr( TContainer& cont )
{
	ClearPtrImpl::ClearPtr( cont, typename TContainer::iterator::Tag() );
}

//------------------------------------------------------------------------------

template < typename K, typename V, typename TSortPredicate = std::less< K > >
struct ArrayMapSortPredicate
{
	RED_INLINE Bool operator()( const std::pair< K, V >& a, const std::pair< K, V >& b ) const
	{ 
		return predicate( a.first, b.first );
	}	
private:
	TSortPredicate predicate;
};

template < typename T >
struct DefaultEqualFunc
{
	template < typename U >
	static RED_INLINE Bool Equal( const T& a, const U& b )
	{
		return a == b;
	}
};

template< class _Res, class _Collection, class _Fn >
RED_INLINE _Res ForEachSumResult( const _Collection& coll, _Fn func )
{
	_Res result = 0;
	for ( auto it = coll.Begin(), end = coll.End(); it != end; ++it )
	{
		result += func(*it);
	}
	return result;
}

//------------------------------------------------------------------------------

// be aware that it works correctly only for values being exact power of 2
// for other values it returns floor( log2( x ) )
template < Uint32 N >
struct Log2
{
	static const Uint32 Value = Log2< N / 2 >::Value + 1;
};

template <>
struct Log2< 1 >
{
	static const Uint32 Value = 0;
};

template <>
struct Log2< 0 >
{
	// well, that's not super correct but it's here for safety reasons
	static const Uint32 Value = 0;
};

//------------------------------------------------------------------------------

// call the visit function for all of the items that have the bit set
// breaks iteration if visitor returns true
template < typename TBitSet, typename TVisitor >
RED_INLINE void VisitBitSet( const TBitSet& bitset, TVisitor visitor )
{
	Uint32 index = bitset.FindNextSet( 0 );
	const Uint32 size = bitset.Size();
	while ( index < size )
	{
		if ( visitor( index ) )
		{
			return;
		}
		index = bitset.FindNextSet( index + 1 );
	} 
}

} } // red::alg
