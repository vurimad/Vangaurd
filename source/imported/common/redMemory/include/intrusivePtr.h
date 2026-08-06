/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_INTRUSIVE_PTR_H_
#define _RED_MEMORY_INTRUSIVE_PTR_H_

#include "sharedStorage.h"
#include "intrusiveSharedStorage.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using IntrusivePtr = SharedStorage< T, internal::IntrusiveSharedStorage, PoolType >;
}

#endif
