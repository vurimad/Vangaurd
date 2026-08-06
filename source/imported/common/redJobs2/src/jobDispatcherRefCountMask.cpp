/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobDispatcherRefCountMask.h"

namespace job { namespace prv {

void DispatcherRefCountMask::AddRef()
{
	m_refCount.Increment();
}

DispatcherRefCountMask::ReleaseResult DispatcherRefCountMask::Release()
{
	const Uint32 curVal = m_refCount.Decrement();
	RED_FATAL_ASSERT( curVal != std::numeric_limits<Uint32>::max(), "Refcount underflow" );
	
	const Bool isZero = curVal == 0;
	ReleaseResult result;
	result.isZero = isZero;
	return result;
}

} } // job/prv
