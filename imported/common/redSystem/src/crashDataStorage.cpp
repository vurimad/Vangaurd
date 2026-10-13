/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "crashData.h"

namespace red { namespace err {

static atomic::TAtomic64 gCrashValueSequenceAllocator = 0;

Uint64 NextCrashValueSequence()
{
	return atomic::Increment64(&gCrashValueSequenceAllocator);
}

} }