/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_CONSTS_H
#define RED_THREADS_CONSTS_H
#pragma once

#include "redThreadsPlatform.h"

namespace red {

//////////////////////////////////////////////////////////////////////////
// Enumerations
//////////////////////////////////////////////////////////////////////////
enum EThreadPriority : Int32
{
	TP_Idle,			//!< Indicates that the thread is idle. 
	TP_Lowest,			//!< Indicates the lowest scheduling priority for an active thread. 
	TP_BelowNormal,		//!< Indicates the second-lowest scheduling priority for an active thread. 
	TP_Normal,			//!< Indicates the default scheduling priority for an active thread.	
	TP_AboveNormal,		//!< Indicates the second-highest priority for an active thread that can be rescheduled. 
	TP_Highest,			//!< Indicates the highest priority for an active thread that can be rescheduled. 
	TP_TimeCritical,	//!< Indicates the true highest priority level for an active thread, one which cannot be rescheduled.
};

//////////////////////////////////////////////////////////////////////////
// EAsyncResult
//////////////////////////////////////////////////////////////////////////
enum EAsyncResult : Int32
{			
	eAsyncResult_Unknown				= -3, //!< Unknown state
	eAsyncResult_Canceled				= -2, //!< Operation was canceled
	eAsyncResult_Error					= -1, //!< Error performing async operation
	eAsyncResult_Success				=  0, //!< Completed async operation successfully
	eAsyncResult_Pending				=  1, //!< Pending result for async operation
	eAsyncResult_Undeferred				=  2, //!< Trying again after a deferral
};

namespace profiler
{
#if defined( USE_VTUNE_PROFILER )
	extern void REDSYSTEM_API SyncCreateMutex( void* addr, const char* objType );
	extern void REDSYSTEM_API SyncDestroyMutex( void* addr );
	extern void REDSYSTEM_API SyncPrepare( void* addr );
	extern void REDSYSTEM_API SyncCancel( void* addr );
	extern void REDSYSTEM_API SyncAcquired( void* addr );
	extern void REDSYSTEM_API SyncReleasing( void* addr );
#else
	RED_INLINE void SyncCreateMutex( void*, const char* )
	{}
	RED_INLINE void SyncDestroyMutex( void* ) {}
	RED_INLINE void SyncPrepare( void* ) {}
	RED_INLINE void SyncCancel( void* ) {}
	RED_INLINE void SyncAcquired( void* ) {}
	RED_INLINE void SyncReleasing( void* ) {}
#endif
}

} // namespace red


#if defined( RED_THREADS_PLATFORM_WINDOWS_API )

namespace red { namespace WinAPI {

#ifdef RED_ARCH_X64
	const TStackSize			g_kDefaultSpawnedThreadStackSize = 2 * 1024 * 1024;
#else
	const TStackSize			g_kDefaultSpawnedThreadStackSize = 1024 * 1024;
#endif

} } // namespace red { namespace WinAPI {

#elif defined( RED_THREADS_PLATFORM_ORBIS_API )

namespace red { namespace OrbisAPI {

const TStackSize				g_kDefaultSpawnedThreadStackSize = 2 * 1024 * 1024;

} } // namespace red { namespace OrbisAPI {

#elif defined( RED_THREADS_PLATFORM_LINUX_API )

namespace red { namespace LinuxAPI {
	const TStackSize			g_kDefaultSpawnedThreadStackSize = 2 * 1024 * 1024;

} } // namespace red { namespace LinuxAPI {

#else
#error Unsupported platform!

#endif

#endif // RED_THREADS_CONSTS_H