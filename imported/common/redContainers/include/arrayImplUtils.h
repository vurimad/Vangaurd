/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

class ArrayImplUtils
{
public:

	template < typename TRandomAccessIterator, typename TArrayTo >
	static void Copy( TRandomAccessIterator fromBegin, TRandomAccessIterator fromEnd, TArrayTo& arrTo );

	template < typename TElement >
	RED_INLINE static void MoveBackwards( TElement* buf, Uint32 offset, Uint32 num );

	template < typename TElement >
	RED_INLINE static void MoveForwards( TElement* buf, Uint32 offset, Uint32 num );

	template < typename TArray >
	RED_INLINE static void MoveForwardsAt( TArray& arr, Uint32 index );

	template < typename TArray >
	RED_INLINE static void GrowNoConstruct( TArray& arr, Uint32 amount );

	RED_INLINE static Uint32 CalcResizeCapacity( Uint32 desiredSize, Uint32 currentCapacity );
};

} // red

#include "arrayImplUtils.hpp"
