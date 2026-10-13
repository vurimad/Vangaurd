/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOPublic.h"
#include "redIOProfilerInterface.h"
#include "redIONullProfiler.h"

RED_NO_EMPTY_FILE()

#ifdef RED_PROFILE_FILE_SYSTEM

NullIOProfiler GNullIOProfiler;
IIOProfiler* IIOProfiler::GIOProfiler = &GNullIOProfiler;

void IIOProfiler::Set( IIOProfiler* newProfiler )
{
	if ( newProfiler == nullptr )
		GIOProfiler = &GNullIOProfiler;
	else
		GIOProfiler = newProfiler;
}

#endif // RED_PROFILE_FILE_SYSTEM