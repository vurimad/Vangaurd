/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_SYSTEM_FILE_H_
#define _RED_SYSTEM_FILE_H_

#include <stdio.h>

namespace red
{
	REDSYSTEM_API Bool FileOpen( FILE** handle, const AnsiChar* filename, const AnsiChar* mode );
	REDSYSTEM_API Bool FileClose( FILE* handle );
	REDSYSTEM_API Bool FileFlush( FILE* handle );
	REDSYSTEM_API void FilePrint( FILE* handle, const AnsiChar* buffer );
	REDSYSTEM_API void FilePrint( FILE* handle, const UniChar* buffer );
}

#endif
