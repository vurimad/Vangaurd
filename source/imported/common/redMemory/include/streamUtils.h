/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_STREAM_UTILS_H_
#define _RED_MEMORY_STREAM_UTILS_H_

#include "stream.h"
#include "../include/uniquePtr.h"

namespace red
{
namespace memory
{
	red::UniquePtr<Stream> OpenStream( const char * filePath );
}
}

#endif