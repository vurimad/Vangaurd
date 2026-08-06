/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "mutex.h"

namespace red
{
namespace memory
{
	Mutex::Mutex()
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		::InitializeCriticalSection( &m_criticalSection );

#elif defined( RED_PLATFORM_ORBIS )
		ScePthreadMutexattr attr;
		::scePthreadMutexattrInit( &attr );
		::scePthreadMutexattrSettype( &attr, SCE_PTHREAD_MUTEX_RECURSIVE );
		::scePthreadMutexattrSetprotocol( &attr, SCE_PTHREAD_PRIO_INHERIT );
		::scePthreadMutexInit( &m_mutex, &attr, nullptr );
		::scePthreadMutexattrDestroy( &attr );

#elif defined( RED_PLATFORM_LINUX )
		pthread_mutexattr_t attr;
		::pthread_mutexattr_init( &attr );
		::pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
		::pthread_mutexattr_setprotocol( &attr, PTHREAD_PRIO_INHERIT );
		::pthread_mutex_init( &m_mutex, &attr );
		::pthread_mutexattr_destroy( &attr );

#else
#	error Unsupported platform!
#endif
	}

	Mutex::~Mutex()
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		::DeleteCriticalSection( &m_criticalSection );

#elif defined( RED_PLATFORM_ORBIS )
		::scePthreadMutexDestroy( &m_mutex );

#elif defined( RED_PLATFORM_LINUX )
		::pthread_mutex_destroy( &m_mutex );

#endif
	}

	void Mutex::Acquire()
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		::EnterCriticalSection( &m_criticalSection );

#elif defined( RED_PLATFORM_ORBIS )
		::scePthreadMutexLock( &m_mutex );

#elif defined( RED_PLATFORM_LINUX )
		::pthread_mutex_lock( &m_mutex );

#endif
	}
	
	void Mutex::Release()
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		::LeaveCriticalSection( &m_criticalSection );

#elif defined( RED_PLATFORM_ORBIS )
		::scePthreadMutexUnlock( &m_mutex );

#elif defined( RED_PLATFORM_LINUX )
		::pthread_mutex_unlock( &m_mutex );

#endif
	}
}
}
