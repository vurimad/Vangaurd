/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_WINAPI_INL
#define RED_THREADS_THREAD_WINAPI_INL
#pragma once

namespace red { namespace WinAPI {

	//////////////////////////////////////////////////////////////////////////
	// Mutex implementation
	//////////////////////////////////////////////////////////////////////////
	inline MutexImpl::MutexImpl()
	{
		::InitializeCriticalSection( &m_criticalSection );
	}

	inline MutexImpl::~MutexImpl()
	{
		::DeleteCriticalSection( &m_criticalSection );
	}

	inline void MutexImpl::AcquireImpl()
	{
		::EnterCriticalSection( &m_criticalSection );
	}

	inline Bool MutexImpl::TryAcquireImpl()
	{
		return ::TryEnterCriticalSection( &m_criticalSection ) != FALSE;
	}

	inline void MutexImpl::ReleaseImpl()
	{
		::LeaveCriticalSection( &m_criticalSection );
	}

	inline void MutexImpl::SetSpinCountImpl( TSpinCount count )
	{
		(void)::SetCriticalSectionSpinCount( &m_criticalSection, count );
	}

	//////////////////////////////////////////////////////////////////////////
	// RWLock implementation
	//////////////////////////////////////////////////////////////////////////
	inline RWLockImpl::RWLockImpl()
	{
		::InitializeSRWLock( &m_rwlock );
	}

	inline RWLockImpl::~RWLockImpl()
	{
		// No RWLock cleanup necessary
	}

	inline void RWLockImpl::AcquireReadSharedImpl()
	{
		::AcquireSRWLockShared( &m_rwlock );
	}

	inline void RWLockImpl::AcquireWriteExclusiveImpl()
	{
		::AcquireSRWLockExclusive( &m_rwlock );
	}

	inline void RWLockImpl::ReleaseReadSharedImpl()
	{
		::ReleaseSRWLockShared( &m_rwlock );
	}

	inline void RWLockImpl::ReleaseWriteExclusiveImpl()
	{
		::ReleaseSRWLockExclusive( &m_rwlock );
	}

// 	inline Bool RWLockImpl::TryAcquireReadSharedImpl()
// 	{
// 		return ::TryAcquireSRWLockShared( &m_rwlock ) != FALSE;
// 	}
// 
// 	inline Bool RWLockImpl::TryAcquireWriteExclusiveImpl()
// 	{
// 		return ::TryAcquireSRWLockExclusive( &m_rwlock ) != FALSE;
// 	}
	
	//////////////////////////////////////////////////////////////////////////
	// Semaphore implementation
	//////////////////////////////////////////////////////////////////////////
	inline SemaphoreImpl::SemaphoreImpl( Int32 initialCount, Int32 maximumCount )
	{
		RED_SYSTEM_ASSERT( initialCount >= 0 && maximumCount >= 0, "Invalid count arguments" );
		RED_SYSTEM_ASSERT( initialCount <= maximumCount, "Invalid count arguments" );

#ifdef RED_PLATFORM_DURANGO
		m_semaphore = ::CreateSemaphoreEx( nullptr, initialCount, maximumCount, nullptr, 0, SYNCHRONIZE | SEMAPHORE_MODIFY_STATE );
#else
		m_semaphore = ::CreateSemaphore( nullptr, initialCount, maximumCount, nullptr );
#endif
		REDTHR_WIN_CHECK( m_semaphore );
	}

	inline SemaphoreImpl::~SemaphoreImpl()
	{
		if ( m_semaphore )
		{
			REDTHR_WIN_CHECK( ::CloseHandle( m_semaphore ) );
		}
	}

	inline void SemaphoreImpl::AcquireImpl()
	{
		RED_SYSTEM_ASSERT( m_semaphore, "No semaphore" );
		const DWORD waitResult = ::WaitForSingleObject( m_semaphore, INFINITE );
		REDTHR_WIN_CHECK( waitResult != WAIT_FAILED ); // Get extended error info if applicable
		RED_SYSTEM_VERIFY( waitResult == WAIT_OBJECT_0, "Failed to wait" );
	}

	inline Bool SemaphoreImpl::TryAcquireImpl( TTimespec timeoutMs )
	{
		RED_SYSTEM_ASSERT( timeoutMs != INFINITE, "Infinite timeout specified" );
		const DWORD waitResult = ::WaitForSingleObject( m_semaphore, static_cast< DWORD >( timeoutMs ) );
		REDTHR_WIN_CHECK( waitResult != WAIT_FAILED ); // Get extended error info if applicable
		RED_SYSTEM_VERIFY( waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT, "Failed to wait" );

		return waitResult == WAIT_OBJECT_0;
	}

	inline void SemaphoreImpl::ReleaseImpl( Int32 count )
	{
		RED_SYSTEM_ASSERT( m_semaphore || count > 0, "No semaphore" );

		REDTHR_WIN_CHECK( ::ReleaseSemaphore( m_semaphore, count, nullptr ) );
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
		::InitializeConditionVariable( &m_cond );
	}

	inline ConditionVariableImpl::~ConditionVariableImpl()
	{
		// No cleanup function
	}

	inline void ConditionVariableImpl::WaitImpl( MutexImpl& mutexImpl )
	{
		REDTHR_WIN_CHECK( ::SleepConditionVariableCS( &m_cond, &mutexImpl.m_criticalSection, INFINITE ) );
	}

	inline void ConditionVariableImpl::WaitWithTimeoutImpl( MutexImpl& mutexImpl, TTimespec timeoutMs )
	{
		REDTHR_SCE_CHECK( ::SleepConditionVariableCS( &m_cond, &mutexImpl.m_criticalSection, timeoutMs ) );
	}

	inline void ConditionVariableImpl::WakeAnyImpl()
	{
		::WakeConditionVariable( &m_cond );
	}

	inline void ConditionVariableImpl::WakeAllImpl()
	{
		::WakeAllConditionVariable( &m_cond );
	}

	//////////////////////////////////////////////////////////////////////////
	// Manual reset event implementation
	//////////////////////////////////////////////////////////////////////////
	inline ManualResetEventImpl::ManualResetEventImpl( Bool startSignalled )
	{
		m_handle = ::CreateEvent( nullptr, TRUE, startSignalled ? TRUE : FALSE, nullptr );
		REDTHR_WIN_CHECK( m_handle != nullptr );
	}

	inline ManualResetEventImpl::~ManualResetEventImpl()
	{
		if ( m_handle )
		{
			REDTHR_WIN_CHECK( ::CloseHandle( m_handle ) );
		}
	}

	inline void ManualResetEventImpl::SetEventImpl()
	{
		REDTHR_WIN_CHECK( ::SetEvent( m_handle ) );
	}

	inline void ManualResetEventImpl::ResetEventImpl()
	{
		REDTHR_WIN_CHECK( ::ResetEvent( m_handle ) );
	}

	inline void ManualResetEventImpl::WaitImpl()
	{
		const DWORD waitResult = ::WaitForSingleObject( m_handle, INFINITE );
		REDTHR_WIN_CHECK( waitResult != WAIT_FAILED ); // Get extended error info if applicable
		RED_SYSTEM_VERIFY( waitResult == WAIT_OBJECT_0, "Failed to wait" );
	}

	inline Bool ManualResetEventImpl::TryWaitImpl( TTimespec timeoutMS )
	{
		REDTHR_WIN_CHECK( timeoutMS != INFINITE );
		const DWORD waitResult = ::WaitForSingleObject( m_handle, static_cast< DWORD >( timeoutMS ) );
		REDTHR_WIN_CHECK( waitResult != WAIT_FAILED ); // Get extended error info if applicable
		RED_SYSTEM_VERIFY( waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT, "Failed to wait" );

		return waitResult == WAIT_OBJECT_0;
	}

	inline const void* ManualResetEventImpl::GetOSHandleImpl() const
	{
		return m_handle;
	}

} } // namespace red { namespace WinAPI {

#endif // RED_THREADS_THREAD_WINAPI_INL