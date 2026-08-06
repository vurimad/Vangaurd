/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "redIOMemory.h"
#include "redIOAsyncIO.h"

namespace io
{

Bool Initialize( const InitSetup& initSetup /*= InitSetup{}*/ )
{
	InitializeMemoryPools();

	if ( ! GAsyncIO.Init( initSetup ) )
	{
		RED_LOG_ERROR( "RedIO: Failed to intialize asyncIO" );
		return false;
	}

	return true;
}

void Shutdown()
{
	GAsyncIO.Shutdown();
}

}