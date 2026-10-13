/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "redThreadsPlatform.h"
#include "systemAssert.h"
#if defined( RED_THREADS_PLATFORM_ORBIS_API )

#include "redThreadsThreadOrbisAPI.h"

#include <pthread_np.h>

extern const char sceUserMainThreadName[] = "MainThread";
int sceUserMainThreadPriority = SCE_KERNEL_PRIO_FIFO_DEFAULT;

// Not supported, and doesn't actually work
//size_t sceUserMainThreadStackSize = 2 * 1024 * 1024;

namespace red { namespace OrbisAPI {

	// Priority *numerical values* are inverted. E.g., default-10 has higher priority than default+10.
	const Int32		g_kThreadPriorityIdle = SCE_KERNEL_PRIO_FIFO_LOWEST;
	const Int32		g_kThreadPriorityLowest = SCE_KERNEL_PRIO_FIFO_DEFAULT + 20;
	const Int32		g_kThreadPriorityBelowNormal = SCE_KERNEL_PRIO_FIFO_DEFAULT + 10;
	const Int32		g_kThreadPriorityNormal = SCE_KERNEL_PRIO_FIFO_DEFAULT;
	const Int32		g_kThreadPriorityAboveNormal = SCE_KERNEL_PRIO_FIFO_DEFAULT - 10;
	const Int32		g_kThreadPriorityHighest = SCE_KERNEL_PRIO_FIFO_DEFAULT - 20;
	const Int32		g_kThreadPriorityTimeCritical = SCE_KERNEL_PRIO_FIFO_HIGHEST;

	const Int32		g_kDefaultThreadSchedulingPolicy = SCE_KERNEL_SCHED_RR;
	const Int32		g_kDefaultThreadPriority = g_kThreadPriorityNormal;

	void YieldCurrentThreadImpl()
	{
		::scePthreadYield();
	}

	void SleepOnCurrentThreadImpl( TTimespec sleepTimeInMS )
	{
		const TTimespec microPerMilli = 1000;
		REDTHR_SCE_CHECK( ::sceKernelUsleep( sleepTimeInMS * microPerMilli ) );
	}

	void SetCurrentThreadAffinityImpl( TAffinityMask affinityMask )
	{
		ScePthread thread = ::scePthreadSelf();
		if ( affinityMask != 0 )
		{
			REDTHR_SCE_CHECK( ::scePthreadSetaffinity( thread, affinityMask ) );
		}
	}

	void SetCurrentThreadNameImpl( const char* threadName )
	{
		// No-op
	}

	Uint32 GetMaxHardwareConcurrencyImpl()
	{
#if SCE_ORBIS_SDK_VERSION >= 0x03000000u
		const Int32 cpuMode = ::sceKernelGetCpumode();
		switch ( cpuMode )
		{
		case SCE_KERNEL_CPUMODE_6CPU:
			return 6;
		case SCE_KERNEL_CPUMODE_7CPU_LOW:
		case SCE_KERNEL_CPUMODE_7CPU_NORMAL:
			return 7;
		default:
			RED_SYSTEM_ASSERT( false, "Unknown CPU mode!");
			break;
		}
#endif
		return 6;
	}

	static void* ThreadEntryFunc( void *userData )
	{
		_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
		_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

		Thread* context = static_cast< Thread* >( userData );
		RED_SYSTEM_ASSERT( context, "Missing thread context" );
		if ( context )
		{
			context->ThreadFunc();
		}

		return nullptr;
	}

	namespace // anonymous
	{
		Int32 priorityLUT[] =
		{
			g_kThreadPriorityIdle,
			g_kThreadPriorityLowest,
			g_kThreadPriorityBelowNormal,
			g_kThreadPriorityNormal,
			g_kThreadPriorityAboveNormal,
			g_kThreadPriorityHighest,
			g_kThreadPriorityTimeCritical,
		};
	}

	ThreadImpl::ThreadImpl( const ThreadMemParams& memParams )
		: m_memParams( memParams )
		, m_thread()
	{
	}

	void ThreadImpl::InitThread( Thread* context )
	{
		const TStackSize stackSize = m_memParams.m_stackSize;

		RED_SYSTEM_ASSERT( context, "No thread context specified" );
		RED_SYSTEM_ASSERT( m_memParams.m_stackSize >= PTHREAD_STACK_MIN, "Stack size is too small" );

		ScePthreadAttr attr;
		REDTHR_SCE_CHECK( ::scePthreadAttrInit( &attr ) );

		// Stack params
		//////////////////////////////////////////////////////////////////////////
		REDTHR_SCE_CHECK( ::scePthreadAttrSetstacksize( &attr, stackSize ) );
		//REDTHR_SCE_CHECK( ::scePthreadAttrSetguardsize( &attr, PAGE_SIZE ) );

		// Run settings
		//////////////////////////////////////////////////////////////////////////
		SceKernelSchedParam schedParam;
		schedParam.sched_priority = g_kDefaultThreadPriority;
		REDTHR_SCE_CHECK( ::scePthreadAttrSetinheritsched( &attr, SCE_PTHREAD_EXPLICIT_SCHED ) );
		REDTHR_SCE_CHECK( ::scePthreadAttrSetschedpolicy( &attr, g_kDefaultThreadSchedulingPolicy ) );
		REDTHR_SCE_CHECK( ::scePthreadAttrSetschedparam( &attr, &schedParam ) );
		REDTHR_SCE_CHECK( ::scePthreadAttrSetdetachstate( &attr, SCE_PTHREAD_CREATE_JOINABLE ) );

		// Create the thread
		//////////////////////////////////////////////////////////////////////////
		const AnsiChar* threadName = context->GetThreadName();
		REDTHR_SCE_CHECK( ::scePthreadCreate( &m_thread, &attr, ThreadEntryFunc, context, threadName ) );

		// Cleanup
		//////////////////////////////////////////////////////////////////////////
		REDTHR_SCE_CHECK( ::scePthreadAttrDestroy( &attr ) );
	}

	ThreadImpl::~ThreadImpl()
	{
		// Could detach the thread, but since ThreadFunc belongs to
		// Thread we're more likely in some messed up state.
		RED_SYSTEM_ASSERT( !IsValid(), "Programmer error - manage thread lifetimes properly: thread object reached base destructor without a JoinThread() or DetachThread()" );
	}

	void ThreadImpl::JoinThread()
	{
		RED_SYSTEM_ASSERT( IsValid(), "" );
		if ( IsValid() )
		{
			REDTHR_SCE_CHECK( ::scePthreadJoin( m_thread, nullptr ) );
			m_thread = ScePthread();
		}
	}

	void ThreadImpl::DetachThread()
	{
		RED_SYSTEM_ASSERT( IsValid(), "" );
		if ( IsValid() )
		{
			REDTHR_SCE_CHECK( ::scePthreadDetach( m_thread ) );
			m_thread = ScePthread();
		}
	}

	void ThreadImpl::SetAffinityMask( TAffinityMask mask )
	{
		RED_SYSTEM_ASSERT( IsValid(), "" );
		if ( IsValid() )
		{
			REDTHR_SCE_CHECK( ::scePthreadSetaffinity( m_thread, static_cast< SceKernelCpumask >( mask ) ) );
		}
	}

	void ThreadImpl::SetPriority( EThreadPriority priority )
	{
		RED_SYSTEM_ASSERT( IsValid(), "" );
		if ( IsValid() )
		{
			REDTHR_SCE_CHECK( ::scePthreadSetprio( m_thread, priorityLUT[ priority ] ) );
		}
	}

	Bool ThreadImpl::operator==( const ThreadImpl& rhs ) const
	{
		RED_SYSTEM_ASSERT( IsValid(), "" );
		if ( IsValid() )
		{
			return ::scePthreadEqual( m_thread, rhs.m_thread ) != 0;
		}
		return false;
	}

} } // namespace red { namespace OrbisAPI {

#endif // RED_THREADS_PLATFORM_ORBIS_API
