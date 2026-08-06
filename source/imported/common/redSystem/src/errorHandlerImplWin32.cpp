/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_PLATFORM_WINPC

#include "errorReporterWin32.h"
#include "errorReporterIPCWin32.h"

namespace errorReporter
{
	Int32 MainLoop( const char* connectionString )
	{
		return dbgutils::win32::ErrorReporterMainLoop( connectionString );
	}

	Bool IsAttachedToProcess( Uint32 processID )
	{
		return dbgutils::win32::IsAttachedToProcess( processID );
	}
}

#endif