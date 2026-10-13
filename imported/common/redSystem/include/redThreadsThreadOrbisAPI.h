/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_ORBISAPI_H
#define RED_THREADS_THREAD_ORBISAPI_H
#pragma once

#include "redThreadsPlatform.h"

//////////////////////////////////////////////////////////////////////////
// Forward declarations
//////////////////////////////////////////////////////////////////////////
namespace red {

	class Thread;

} // namespace red {

//////////////////////////////////////////////////////////////////////////
// Synchronization object implementation decls
//////////////////////////////////////////////////////////////////////////
namespace red { namespace OrbisAPI {

	class MutexImpl
	{
		friend class ConditionVariableImpl;

	private:
		ScePthreadMutex				m_mutex;

	protected:
		MutexImpl();
		~MutexImpl();

		void		AcquireImpl();
		Bool		TryAcquireImpl();
		void		ReleaseImpl();
		void		SetSpinCountImpl( TSpinCount count );
	};

	class RWLockImpl
	{
	private:
		ScePthreadRwlock			m_rwlock;

	protected:
		RWLockImpl();
		~RWLockImpl();

	protected:
		void AcquireReadSharedImpl();
		void AcquireWriteExclusiveImpl();

		void ReleaseReadSharedImpl();
		void ReleaseWriteExclusiveImpl();

// 		Bool TryAcquireReadSharedImpl();
// 		Bool TryAcquireWriteExclusiveImpl();
	};

	class SemaphoreImpl
	{
	private:
		SceKernelSema			m_semaphore;

	protected:
		SemaphoreImpl( Int32 initialCount, Int32 maximumCount );
		~SemaphoreImpl();

		void		AcquireImpl();
		Bool		TryAcquireImpl( TTimespec timeoutMs );
		void		ReleaseImpl( Int32 count );
		const void* GetOSHandleImpl() const;
	};

	class ConditionVariableImpl
	{
	private:
		ScePthreadCond 				m_cond;

	protected:
		ConditionVariableImpl();
		~ConditionVariableImpl();

	protected:
		void WaitImpl( MutexImpl& mutexImpl );
		void WaitWithTimeoutImpl( MutexImpl& mutexImpl, TTimespec timeoutMs );

		void WakeAnyImpl();
		void WakeAllImpl();
	};

// #tbd: supposedly was slower, but should be faster now than using condvars and mutexes
#define REDTHR_USE_KERNEL_EVENT_FLAG

	class ManualResetEventImpl
	{
	private:
#ifdef REDTHR_USE_KERNEL_EVENT_FLAG
		static const Uint64 SIGNALLED_BITMASK = 0xFFFFFFFFFFFFFFFFUL;
		SceKernelEventFlag			m_eventFlag;
#else
		//ScePthreadCond				m_cond;
#endif

	protected:
		explicit ManualResetEventImpl( Bool startSignalled );
		~ManualResetEventImpl();

	protected:
		void SetEventImpl();
		void ResetEventImpl();
		void WaitImpl();
		Bool TryWaitImpl( TTimespec timeoutMS );
		const void* GetOSHandleImpl() const;
	};

} } // namespace red { namespace OrbisAPI {

//////////////////////////////////////////////////////////////////////////
// Thread object implementation
//////////////////////////////////////////////////////////////////////////
namespace red { namespace OrbisAPI {

	void YieldCurrentThreadImpl();
	void SleepOnCurrentThreadImpl( TTimespec sleepTimeInMS );
	Uint32 GetMaxHardwareConcurrencyImpl();
	void SetCurrentThreadAffinityImpl( TAffinityMask affinityMask );
	void SetCurrentThreadNameImpl( const char* threadName );
	
	class ThreadImpl
	{
	private:
		ThreadMemParams		m_memParams;

	private:
		ScePthread				m_thread;	

	public:
								ThreadImpl( const ThreadMemParams& memParams );
								~ThreadImpl();

		void					InitThread( Thread* context );
		void					JoinThread();
		void					DetachThread();

	public:

		void					SetAffinityMask( TAffinityMask mask );
		void					SetPriority( EThreadPriority priority );

	public:
		Bool					operator==( const ThreadImpl& rhs ) const;
		RED_INLINE Bool			IsValid() const { return m_thread != ScePthread(); }
	};

} } // namespace red { namespace OrbisAPI {

#include "redThreadsThreadOrbisAPI.inl"

#endif // RED_THREADS_THREAD_ORBISAPI_H