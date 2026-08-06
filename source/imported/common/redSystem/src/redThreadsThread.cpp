/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../include/threads.h"

#ifdef USE_VTUNE_PROFILER
# include "ittnotify.h"
# pragma comment ( lib, "libittnotify.lib" )
#endif

namespace red
{
	template class REDSYSTEM_API Atomic<Int32>;
	template class REDSYSTEM_API Atomic<Uint32>;
	template class REDSYSTEM_API Atomic<Int64>;
	template class REDSYSTEM_API Atomic<Uint64>;
}

namespace red
{
namespace profiler
{

#ifdef USE_VTUNE_PROFILER
	void REDSYSTEM_API SyncCreateMutex( void* addr, const char* objType )
	{
		__itt_sync_createA( addr, objType, nullptr, __itt_attr_mutex );
	}

	void REDSYSTEM_API SyncDestroyMutex( void* addr )
	{
		__itt_sync_destroy( addr );
	}

	void REDSYSTEM_API SyncPrepare( void* addr )
	{
		__itt_sync_prepare( addr );
	}

	void REDSYSTEM_API SyncCancel( void* addr )
	{
		__itt_sync_cancel( addr );
	}

	void REDSYSTEM_API SyncAcquired( void* addr )
	{
		__itt_sync_acquired( addr );
	}

	void REDSYSTEM_API SyncReleasing( void* addr )
	{
		__itt_sync_releasing( addr );
	}
#endif
}


	void YieldCurrentThread()
	{
		OSAPI::YieldCurrentThreadImpl();
	}

	void SleepOnCurrentThread( TTimespec sleepTimeInMS )
	{
		OSAPI::SleepOnCurrentThreadImpl( sleepTimeInMS );
	}

	void SetCurrentThreadAffinity( TAffinityMask affinityMask )
	{
		OSAPI::SetCurrentThreadAffinityImpl( affinityMask );
	}
	
	void SetCurrentThreadName( const char* threadName )
	{
		OSAPI::SetCurrentThreadNameImpl( threadName );
	}

#ifdef RED_THREADS_SUPPORTS_SUSPEND
	void SuspendThreadById( red::ThreadId id )
	{
		OSAPI::SuspendThreadByIdImpl( id );
	}

	void ResumeThreadById( red::ThreadId id )
	{
		OSAPI::ResumeThreadByIdImpl( id );
	}
#endif // #ifdef RED_THREADS_SUPPORTS_SUSPEND

	Uint32 GetMaxHardwareConcurrency()
	{
		return OSAPI::GetMaxHardwareConcurrencyImpl();
	}

	ThreadMemParams::ThreadMemParams( TStackSize stackSize /*= OSAPI::g_kDefaultSpawnedThreadStackSize */ )
		: m_stackSize( stackSize )
	{
	}

	Thread::Thread( const AnsiChar* threadName, 
					  const ThreadMemParams& memParams /*= ThreadMemParams()*/ )
		: m_threadImpl( memParams )
	{
		RED_UNUSED( threadName );
		const size_t nameBufSize = sizeof(m_threadName)/sizeof(m_threadName[0]); // buffer has +1 size for NULL terminator
		RED_SYSTEM_ASSERT( threadName, "Thread name cannot be NULL." );
		RED_SYSTEM_ASSERT( red::Strlen(threadName) <= g_kMaxThreadNameLength, "Thread name is larger than common OS supported size" );
		red::Strcpy( m_threadName, threadName, nameBufSize );
	}

	Thread::~Thread()
	{
	}

	void Thread::JoinThread()
	{
		m_threadImpl.JoinThread();
	}

	void Thread::DetachThread()
	{
		m_threadImpl.DetachThread();
	}

	Bool Thread::IsValid() const
	{
		return m_threadImpl.IsValid();
	}

	void Thread::InitThread()
	{
		m_threadImpl.InitThread( this );
	}

	void Thread::SetAffinityMask( TAffinityMask mask )
	{
		RED_UNUSED( mask );
		m_threadImpl.SetAffinityMask( mask );
	}

	void Thread::SetPriority( EThreadPriority priority )
	{
		m_threadImpl.SetPriority( priority );
	}

#if defined(RED_THREADS_PLATFORM_WINDOWS_API)
	void Thread::DisablePriorityBoost( Bool threadBoostDisabled )
	{
		m_threadImpl.SetPriorityBoost( threadBoostDisabled );
	}
#endif

	Bool Thread::operator==( const Thread& rhs ) const
	{
		return m_threadImpl == rhs.m_threadImpl;
	}

} // namespace red


//--------------------------------------

// Keep as POD to avoid static initialization fiasco during onexit
static Uint32 GMainThreadID = 0;

REDSYSTEM_API void InitMainThreadId()
{
	GMainThreadID = red::ThreadId::CurrentThread().AsNumber();
}

const Bool SIsMainThread()
{
	const Uint32 id = red::ThreadId::CurrentThread().AsNumber();
	return !GMainThreadID || GMainThreadID == id;
}

//--------------------------------------
