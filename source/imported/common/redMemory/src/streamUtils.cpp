/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "streamUtils.h"

namespace red
{
namespace memory
{

#ifndef RED_PLATFORM_WINPC
	red::UniquePtr<Stream> OpenStream( const char * )
	{
		return nullptr;
	}
#endif

}
}