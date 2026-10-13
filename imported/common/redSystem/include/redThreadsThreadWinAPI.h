/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_WINAPI_H
#define RED_THREADS_THREAD_WINAPI_H
#pragma once

#include "redThreadsPlatform.h"

#define RED_THREADS_SUPPORTS_SUSPEND

//////////////////////////////////////////////////////////////////////////
// Forward declarations
//////////////////////////////////////////////////////////////////////////
namespace red {

	class Thread;

} // namespace red

//////////////////////////////////////////////////////////////////////////
// Synchronization object implementation decls
//////////////////////////////////////////////////////////////////////////
namespace red { namespace WinAPI {

	class MutexImpl
	{
		friend class ConditionVariableImpl;

	private:
		CRITICAL_SECTION		m_criticalSection;

	protected:
		MutexImpl();
		~MutexImpl();

	protected:
		void		AcquireImpl();
		Bool		TryAcquireImpl();
		void		ReleaseImpl();
		void		SetSpinCountImpl( TSpinCount count );
	};

	class RWLockImpl
	{
	private:
		SRWLOCK		m_rwlock;

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
		HANDLE		m_semaphore;

	protected:
		SemaphoreImpl( Int32 initialCount, Int32 maximumCount );
		~SemaphoreImpl();

	protected:
		void		AcquireImpl();
		Bool		TryAcquireImpl( TTimespec timeoutMs );
		void		ReleaseImpl( Int32 count );
		const void* GetOSHandleImpl() const;
	};

	class ConditionVariableImpl
	{
	private:
		CONDITION_VARIABLE			m_cond;

	protected:
		ConditionVariableImpl();
		~ConditionVariableImpl();

	protected:
		void WaitImpl( MutexImpl& mutexImpl );
		void WaitWithTimeoutImpl( MutexImpl& mutexImpl, TTimespec timeoutMs );

		void WakeAnyImpl();
		void WakeAllImpl();
	};

	class ManualResetEventImpl
	{
	private:
		HANDLE m_handle;

	protected:
		explicit ManualResetEventImpl( Bool startSignalled );
		~ManualResetEventImpl();

	protected:
		void SetEventImpl();
		void ResetEventImpl();
		void WaitImpl();
		Bool TryWaitImpl( TTimespec timeoutMs );
		const void* GetOSHandleImpl() const;
	};

} } // namespace red { namespace WinAPI {

//////////////////////////////////////////////////////////////////////////
// Thread object implementation
//////////////////////////////////////////////////////////////////////////
namespace red { namespace WinAPI {

	void YieldCurrentThreadImpl();
	void SleepOnCurrentThreadImpl( TTimespec sleepTimeInMS );
	void SetCurrentThreadAffinityImpl( TAffinityMask affinityMask );
	void SetCurrentThreadNameImpl( const char* threadName );
	void SuspendThreadByIdImpl( red::ThreadId id );
	void ResumeThreadByIdImpl( red::ThreadId id );
	Uint32 GetMaxHardwareConcurrencyImpl();

	class ThreadImpl
	{
	private:
		Thread*				m_debug;

	private:
		ThreadMemParams		m_memParams;

	private:
		HANDLE					m_thread;

	public:
								ThreadImpl( const ThreadMemParams& memParams );
								~ThreadImpl();

		void					InitThread( Thread* context );
		void					JoinThread();
		void					DetachThread();

	public:
		void					SetAffinityMask( Uint64 mask );
		void					SetPriority( EThreadPriority priority );
		void					SetPriorityBoost( Bool threadBoostDisabled );

	public:
		Bool					operator==( const ThreadImpl& rhs ) const;
		RED_INLINE Bool			IsValid() const { return m_thread != HANDLE(); }
	};

} } // namespace red { namespace WinAPI {

#include "redThreadsThreadWinAPI.inl"

#endif // RED_THREADS_THREAD_WINAPI_H