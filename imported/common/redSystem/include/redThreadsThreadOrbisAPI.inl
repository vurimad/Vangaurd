/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_ORBISAPI_INL
#define RED_THREADS_THREAD_ORBISAPI_INL
#pragma once

#include <pthread_np.h>
#include "redThreadsPlatform.h"

namespace red { namespace OrbisAPI {

	//////////////////////////////////////////////////////////////////////////
	// Mutex implementation
	//////////////////////////////////////////////////////////////////////////
	inline MutexImpl::MutexImpl()
	{
		ScePthreadMutexattr attr;
		REDTHR_SCE_CHECK( ::scePthreadMutexattrInit( &attr ) );
		REDTHR_SCE_CHECK( ::scePthreadMutexattrSettype( &attr, SCE_PTHREAD_MUTEX_RECURSIVE ) );
		REDTHR_SCE_CHECK( ::scePthreadMutexattrSetprotocol( &attr, SCE_PTHREAD_PRIO_INHERIT ) );
		REDTHR_SCE_CHECK( ::scePthreadMutexInit( &m_mutex, &attr, nullptr ) );
		REDTHR_SCE_CHECK( ::scePthreadMutexattrDestroy( &attr ) );
	}

	inline MutexImpl::~MutexImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadMutexDestroy( &m_mutex ) );
	}

	inline void MutexImpl::AcquireImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadMutexLock( &m_mutex ) );
	}

	inline Bool MutexImpl::TryAcquireImpl()
	{
		Int32 retval = ::scePthreadMutexTrylock( &m_mutex );

#ifdef RED_ASSERTS_ENABLED
		RED_SYSTEM_ASSERT( retval == SCE_OK || retval == SCE_KERNEL_ERROR_EBUSY, "" );
#else
		RED_VERIFY( retval == SCE_OK || retval == SCE_KERNEL_ERROR_EBUSY );
#endif

		return retval == SCE_OK;
	}

	inline void MutexImpl::ReleaseImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadMutexUnlock( &m_mutex ) );
	}

	inline void MutexImpl::SetSpinCountImpl( TSpinCount count )
	{
		RED_UNUSED( count );
		// No op for now, as can only use SCE wrappers as of the latest PS4 SDK.
		// Possibility to use an adaptive mutex instead, but then can't rely on it
		// being recursive...
		//REDTHR_SCE_CHECK( ::pthread_mutex_setspinloops_np( &m_mutex, count ) );
	}

	//////////////////////////////////////////////////////////////////////////
	// RWLock implementation
	//////////////////////////////////////////////////////////////////////////
	inline RWLockImpl::RWLockImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadRwlockInit( &m_rwlock, nullptr, nullptr ) );
	}

	inline RWLockImpl::~RWLockImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadRwlockDestroy( &m_rwlock ) );
	}

	inline void RWLockImpl::AcquireReadSharedImpl()
	{
		// In the case of a reading lock, if SCE_KERNEL_ERROR_EBUSY then we've reached the max number of reading threads
		// TBD: Practical limit and when we'll start busy waiting.
		
#if 0
		int lockResult = SCE_KERNEL_ERROR_EAGAIN;
		do
		{
			lockResult = ::scePthreadRwlockRdlock( &m_rwlock );
			RED_ASSERT( lockResult == SCE_OK || lockResult == SCE_KERNEL_ERROR_EAGAIN, "Acquiring read lock failed: 0x%08X", lockResult );
		}
		while ( lockResult == SCE_KERNEL_ERROR_EAGAIN );
#endif

		// Can't even hit SCE_KERNEL_ERROR_EAGAIN - run out of spawnable threads first
		REDTHR_SCE_CHECK( ::scePthreadRwlockRdlock( &m_rwlock ) );
	}

	inline void RWLockImpl::AcquireWriteExclusiveImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadRwlockWrlock( &m_rwlock ) );
	}

	inline void RWLockImpl::ReleaseReadSharedImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadRwlockUnlock( &m_rwlock ) );
	}

	inline void RWLockImpl::ReleaseWriteExclusiveImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadRwlockUnlock( &m_rwlock ) );
	}

// 	inline Bool RWLockImpl::TryAcquireReadSharedImpl()
// 	{
// 		// In the case of a reading lock, if SCE_KERNEL_ERROR_EBUSY then we've reached the max number of reading threads
// 		const int lockResult = ::scePthreadRwlockTryrdlock( &m_rwlock );
// 		RED_ASSERT( lockResult == SCE_OK || lockResult == SCE_KERNEL_ERROR_EBUSY || lockResult == SCE_KERNEL_ERROR_EAGAIN, "Trying to acquire read lock failed: 0x%08X", lockResult );
// 
// 		return lockResult == SCE_OK;
// 	}
// 
// 	inline Bool RWLockImpl::TryAcquireWriteExclusiveImpl()
// 	{
// 		// If in the case of a writing lock, SCE_KERNEL_ERROR_EBUSY means it's already exclusively locked (and recursive write locks are not supported)
// 		const int lockResult = ::scePthreadRwlockTrywrlock( &m_rwlock );
// 		RED_ASSERT( lockResult == SCE_OK || lockResult == SCE_KERNEL_ERROR_EBUSY, "Trying to acquire write lock failed: 0x%08X", lockResult );
// 
// 		return lockResult == SCE_OK;
// 	}

	//////////////////////////////////////////////////////////////////////////
	// Semaphore implementation
	//////////////////////////////////////////////////////////////////////////
	inline SemaphoreImpl::SemaphoreImpl( Int32 initialCount, Int32 maximumCount )
	{
		RED_SYSTEM_ASSERT( initialCount >= 0 && maximumCount >= 0, "Invalid semaphore count" );
		RED_SYSTEM_ASSERT( initialCount <= maximumCount, "Invalid semaphore count" );
		REDTHR_SCE_CHECK( ::sceKernelCreateSema( &m_semaphore, "sema", SCE_KERNEL_SEMA_ATTR_TH_FIFO, initialCount, maximumCount, nullptr ) );
	}

	inline SemaphoreImpl::~SemaphoreImpl()
	{
		REDTHR_SCE_CHECK( ::sceKernelDeleteSema( m_semaphore ) );
	}

	inline void SemaphoreImpl::AcquireImpl()
	{
		REDTHR_SCE_CHECK( ::sceKernelWaitSema( m_semaphore, 1, nullptr ) );
	}

	inline Bool SemaphoreImpl::TryAcquireImpl( TTimespec timeoutMs )
	{
		if ( timeoutMs == 0 )
		{
			const int pollResult = ::sceKernelPollSema( m_semaphore, 1 );
			RED_SYSTEM_ASSERT( pollResult == SCE_OK || pollResult == SCE_KERNEL_ERROR_EBUSY, "Polling semaphore failed" );
			return pollResult == SCE_OK;	
		}

		const TTimespec microPerMilli = 1000;
		TTimespec waitTime = timeoutMs * microPerMilli;
		const int waitResult = ::sceKernelWaitSema( m_semaphore, 1, &waitTime );
		RED_SYSTEM_ASSERT( waitResult == SCE_OK || waitResult == SCE_KERNEL_ERROR_ETIMEDOUT, "Waiting on semaphore failed" );
		return waitResult == SCE_OK;
	}

	inline void SemaphoreImpl::ReleaseImpl( Int32 count )
	{
		RED_SYSTEM_ASSERT( count > 0, "" );
		REDTHR_SCE_CHECK( ::sceKernelSignalSema( m_semaphore, count ) );
	}

	inline const void* SemaphoreImpl::GetOSHandleImpl() const
	{
		return m_semaphore;
	}

	//////////////////////////////////////////////////////////////////////////
	// Condition variable implementation
	//////////////////////////////////////////////////////////////////////////
	inline ConditionVariableImpl::ConditionVariableImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadCondInit( &m_cond, nullptr, nullptr ) );
	}

	inline ConditionVariableImpl::~ConditionVariableImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadCondDestroy( &m_cond ) );
	}

	inline void ConditionVariableImpl::WaitImpl( MutexImpl& mutexImpl )
	{
		REDTHR_SCE_CHECK( ::scePthreadCondWait( &m_cond, &mutexImpl.m_mutex ) );
	}

	inline void ConditionVariableImpl::WaitWithTimeoutImpl( MutexImpl& mutexImpl, TTimespec timeoutMs )
	{
		REDTHR_SCE_CHECK( ::scePthreadCondTimedwait( &m_cond, &mutexImpl.m_mutex, timeoutMs * 1000 ) );
	}

	inline void ConditionVariableImpl::WakeAnyImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadCondSignal( &m_cond ) );
	}

	inline void ConditionVariableImpl::WakeAllImpl()
	{
		REDTHR_SCE_CHECK( ::scePthreadCondBroadcast( &m_cond ) );
	}

	//////////////////////////////////////////////////////////////////////////
	// Manual reset event implementation
	//////////////////////////////////////////////////////////////////////////
	inline ManualResetEventImpl::ManualResetEventImpl( Bool startSignalled )
	{
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		// #tbd: according to the docs name cannot be null, but then probably limits the number of events possible!
		const Uint32 attr = SCE_KERNEL_EVF_ATTR_TH_FIFO | SCE_KERNEL_EVF_ATTR_MULTI;
		const Uint64 initPattern = startSignalled ? SIGNALLED_BITMASK : 0;
		REDTHR_SCE_CHECK( ::sceKernelCreateEventFlag( &m_eventFlag, "redKernelEventFlag", attr, initPattern, nullptr ) );
#else
		ScePthreadCond				m_cond;
#endif
	}

	inline ManualResetEventImpl::~ManualResetEventImpl()
	{
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		REDTHR_SCE_CHECK( ::sceKernelDeleteEventFlag( m_eventFlag ) );
#else
#error todo
		ScePthreadCond				m_cond;
#endif
	}

	inline void ManualResetEventImpl::SetEventImpl()
	{
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		REDTHR_SCE_CHECK( ::sceKernelSetEventFlag( m_eventFlag, SIGNALLED_BITMASK ) );
#else
#error todo
		//ScePthreadCond				m_cond;
#endif
	}
	
	inline void ManualResetEventImpl::ResetEventImpl()
	{
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		REDTHR_SCE_CHECK( ::sceKernelClearEventFlag( m_eventFlag, ~SIGNALLED_BITMASK ) );
#else
#error todo
		//ScePthreadCond				m_cond;
#endif
	}

	inline void ManualResetEventImpl::WaitImpl()
	{
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		REDTHR_SCE_CHECK( ::sceKernelWaitEventFlag( m_eventFlag, SIGNALLED_BITMASK, 
			SCE_KERNEL_EVF_WAITMODE_AND /* tbd: see if OR is faster here*/, nullptr, nullptr ) );
#else
#error todo
		//ScePthreadCond				m_cond;
#endif
	}

	inline Bool ManualResetEventImpl::TryWaitImpl( TTimespec timeoutMS )
	{
 #ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		if ( timeoutMS == 0 )
		{
			const int pollResult = ::sceKernelPollEventFlag( m_eventFlag, SIGNALLED_BITMASK, SCE_KERNEL_EVF_WAITMODE_AND, nullptr );
			RED_SYSTEM_ASSERT( pollResult == SCE_OK || pollResult == SCE_KERNEL_ERROR_EBUSY, "Polling event failed" );
			return pollResult == SCE_OK;
		}
		else
		{
			SceKernelUseconds timeout = timeoutMS * 1000;
			const int waitResult = ::sceKernelWaitEventFlag( m_eventFlag, SIGNALLED_BITMASK, 
				SCE_KERNEL_EVF_WAITMODE_AND /* tbd: see if OR is faster here*/, nullptr, &timeout );
			RED_SYSTEM_ASSERT( waitResult == SCE_OK || waitResult == SCE_KERNEL_ERROR_ETIMEDOUT, "Waiting on event failed" );
			return waitResult == SCE_OK;
		}
#else
#error todo
		//ScePthreadCond				m_cond;
#endif
	}

	inline const void* ManualResetEventImpl::GetOSHandleImpl() const
	{
		return m_eventFlag;
	}

} } // namespace red { namespace OrbisAPI {

#endif // RED_THREADS_THREAD_ORBISAPI_INL
