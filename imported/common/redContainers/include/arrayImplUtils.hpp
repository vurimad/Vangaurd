/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TRandomAccessIterator, typename TArrayTo >
void ArrayImplUtils::Copy( TRandomAccessIterator fromBegin, TRandomAccessIterator fromEnd, TArrayTo& arrTo )
{
	typedef typename TArrayTo::ElementType ElementType;
	typedef typename policies::CopyConstructorExecutorSelector< ElementType >::Type		CopyConstructorExecutor;
	typedef typename policies::CopyAssignmentExecutorSelector< ElementType >::Type		CopyAssignmentExecutor;
	typedef typename policies::DestructorExecutorSelector< ElementType >::Type			DestructorExecutor;

	const Uint32 newSize = red::Distance< Uint32 >( fromBegin, fromEnd );
	if ( newSize == 0 )
	{
		arrTo.Clear();
	}
	else
	{		
		const Uint32 oldSize = arrTo.Size();
		const ElementType* const fromData = &( *fromBegin );

		// There are enough elements, copy what is needed, destroy the rest.
		if ( newSize <= oldSize ) 
		{
			ElementType* const toData = arrTo.TypedData();
			// because of C4996 warning (see arrayPolicies for details)
			// std::copy( arrData, arrData + newSize, data );
			CopyAssignmentExecutor::Execute( toData, fromData, newSize );
			DestructorExecutor::Execute( toData + newSize, oldSize - newSize );
		}
		else if ( newSize <= arrTo.Capacity() ) // Enough space allocated. Copy over existing and construct the rest.
		{
			ElementType* const toData = arrTo.TypedData();
			// because of C4996 warning (see arrayPolicies for details)
			// std::copy( arrData, arrData + oldSize, data );
			CopyAssignmentExecutor::Execute( toData, fromData, oldSize );
			CopyConstructorExecutor::Execute( toData + oldSize, fromData + oldSize, newSize - oldSize );
		}
		else // Worst case, buffer has to be reallocated. Destroy everything, and reconstruct everything. 
		{
			DestructorExecutor::Execute( arrTo.TypedData(), oldSize );
			arrTo.m_size = 0;
			arrTo.ResizeBuffer( newSize );
			CopyConstructorExecutor::Execute( arrTo.TypedData(), fromData, newSize );
		}
		arrTo.m_size = newSize;
	}	
}

//////////////////////////////////////////////////////////////////////////

template< typename TElement >
RED_INLINE void ArrayImplUtils::MoveBackwards( TElement* buf, Uint32 offset, Uint32 num )
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type		DestructorExecutor;
	typedef typename policies::MoveAssignmentExecutorSelector< TElement >::Type	MoveAssignmentExecutor;

	// move "num" elements from "buf + offset" to "buf"
	// because of C4996 warning (see arrayPolicies for details)
	// TElement* buf_offset = buf + offset;
	// std::move( buf_offset, buf_offset + num, buf );
	MoveAssignmentExecutor::Execute( buf, buf + offset, num );

	// destruct "offset" elements starting from "buf + num" ending at "buf + num + offset"
	DestructorExecutor::Execute( buf + num, offset );
}

template< typename TElement >
RED_INLINE void ArrayImplUtils::MoveForwards( TElement* buf, Uint32 offset, Uint32 num )
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type		DestructorExecutor;
	typedef typename policies::MoveAssignmentExecutorSelector< TElement >::Type	MoveAssignmentExecutor;

	// move in reverse direction "num" elements from "buf" to "buf + offset"
	// because of C4996 warning (see arrayPolicies for details)
	// TElement* buf_num = buf + num;
	// std::move_backward( buf, buf_num, buf_num + offset );
	MoveAssignmentExecutor::Execute( buf + offset, buf, num );

	// destruct "offset" elements starting from "buf" ending at "buf + offset"
	DestructorExecutor::Execute( buf, offset );
}

template< typename TArray >
RED_INLINE void ArrayImplUtils::MoveForwardsAt( TArray& arr, Uint32 index )
{
	typedef typename TArray::ElementType ElementType;
	typedef typename policies::MoveConstructorExecutorSelector< ElementType >::Type MoveConstructorExecutor;

	const Uint32 oldSize = arr.Size();

	RED_FATAL_ASSERT( index < oldSize, "MoveForwardsAt argument has to be within array range." );

	// first, let's create a space for a new element
	arr.GrowNoConstruct( 1 );

	ElementType* const data = arr.TypedData();
	const Uint32 size = arr.Size();

	// then let's move last element to a newly created place using move constructor
	MoveConstructorExecutor::Execute( data + size - 1, data + oldSize - 1 );

	// next move rest of the elements using move assignment
	MoveForwards( data + index, 1, oldSize - index - 1 );
}

//////////////////////////////////////////////////////////////////////////

template< typename TArray >
RED_INLINE void ArrayImplUtils::GrowNoConstruct( TArray& arr, Uint32 amount )
{
	arr.GrowNoConstruct( amount );
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE Uint32 ArrayImplUtils::CalcResizeCapacity( Uint32 desiredSize, Uint32 currentCapacity )
{
	while ( desiredSize > currentCapacity )
	{
		// Increase the size of the buffer by 1.5 each time. This is better than increasing by 2x the size, as it allows 
		// the holes (from freeing the old buffer) to eventually get big enough to fit the new buffer (assuming the allocations happen in contiguous,
		// linear address space)
		// The highest bound we should use is phi (1.618...) as anything over phi would give a decreasing Fibonacci sequence of sizes,
		// and we would never be able to re-use the old holes
		// e.g.
		// grow(32) - alloc(32) - no hole
		// grow(48) - alloc(48), free(32) - 32 byte hole
		// grow(72) - alloc(72), free(48) - 80 byte hole
		// grow(120) - alloc(120), free(72) - 152 byte hole
		// grow(180) - alloc(180), free(120) - 272 byte hole
		// grow(270) - alloc(270) -> HOLE FILLED, free(120)
		currentCapacity = ( currentCapacity > 1 ) ? ( currentCapacity + ( currentCapacity / 2 ) ) : ( currentCapacity + 1 );
	}
	return currentCapacity;
}

} // red