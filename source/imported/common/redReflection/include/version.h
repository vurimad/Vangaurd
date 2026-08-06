/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

//! application major version - 0 - prior to release, 1 - after release
#define APP_VERSION_MAJOR	"3"

//! application minor version - basically last milestone number
#define APP_VERSION_MINOR	"0"

// build number, incremented with every build
#define APP_VERSION_BUILD	"BUILD_VERSION_FULL"

//! last P4 change used to compile new build
#define APP_LAST_P4_CHANGE 	"CL_INTERNAL"

//! the p4 stream name
#define APP_P4_STREAM 	"STREAM_NAME"

#define APP_P4_SHELF "P4_SHELF"

#if defined(BUILD_MACHINE)
#define APP_FILE_VERSION_STR "3.0.BUILD_VERSION_FULL"
#define APP_FILE_VERSION 3,0,BUILD_VERSION_MAJOR,BUILD_VERSION_MINOR
#else
#define APP_FILE_VERSION_STR "3.0.0.0(Local Build)"
#define APP_FILE_VERSION 3,0,0,0
#endif

#define APP_VERSION_NUMBER APP_VERSION_MAJOR "." APP_VERSION_MINOR "." APP_VERSION_BUILD "  P4CL: " APP_LAST_P4_CHANGE "  Stream: " APP_P4_STREAM " " APP_P4_SHELF

// Exclude following code if resource compiler is being invoked.
// Resource compiler is used to pull above defines into Resource.rc on WinPC.
#ifndef RC_INVOKED
RED_INLINE Bool redIsInternalVersionOrig()
{
	return red::Strcmp( APP_LAST_P4_CHANGE, "CL_INTERNAL" ) == 0;
}

// You'd think this'd work, but something on the BM is very weird...
// #define redIsInternalVersion()\
// 	(red::Strcmp( APP_LAST_P4_CHANGE, "CL_INTERNAL" ) == 0)
#endif

#define APP_DATE __DATE__

// TIMESTAMP: 2010-05-12 16:06:44
