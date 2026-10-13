/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_INL
#define RED_THREADS_THREAD_INL
#pragma once

namespace red {

	//////////////////////////////////////////////////////////////////////////
	// Mutex implementation
	//////////////////////////////////////////////////////////////////////////
	inline Mutex::Mutex()
	{
	}

	inline Mutex::~Mutex()
	{
	}

	inline void Mutex::Acquire()
	{
		Base::AcquireImpl();
	}
	
	inline Bool Mutex::TryAcquire()
	{
		return Base::TryAcquireImpl();
	}

	inline void Mutex::Release()
	{
		Base::ReleaseImpl();
	}

	inline void Mutex::SetSpinCount( TSpinCount count )
	{
		Base::SetSpinCountImpl( count );
	}

	inline NullMutex::NullMutex()
	{
	}

	inline NullMutex::~NullMutex()
	{
	}

	inline void NullMutex::Acquire()
	{

	}

	inline Bool NullMutex::TryAcquire()
	{
		return true;
	}

	inline void NullMutex::Release()
	{
	}

	inline void NullMutex::SetSpinCount( TSpinCount count )
	{
		RED_UNUSED( count );
	}

	//////////////////////////////////////////////////////////////////////////
	// Scoped lock implementation
	//////////////////////////////////////////////////////////////////////////
	template < typename TAcquireRelease > inline ScopedLock< TAcquireRelease >::ScopedLock( TAcquireRelease& syncObject )
		: m_syncObjectRef( syncObject )
	{
		m_syncObjectRef.Acquire();
	}

	template < typename TAcquireRelease > inline ScopedLock< TAcquireRelease >::~ScopedLock()
	{
		m_syncObjectRef.Release();
	}

	//////////////////////////////////////////////////////////////////////////
	// Shared scoped lock implementation
	//////////////////////////////////////////////////////////////////////////
	template < typename TAcquireReleaseShared > inline ScopedSharedLock< TAcquireReleaseShared >::ScopedSharedLock( TAcquireReleaseShared& syncObject )
		: m_syncObjectRef( syncObject )
	{
		m_syncObjectRef.AcquireShared();
	}

	template < typename TAcquireReleaseShared > inline ScopedSharedLock< TAcquireReleaseShared >::~ScopedSharedLock()
	{
		m_syncObjectRef.ReleaseShared();
	}

	//////////////////////////////////////////////////////////////////////////
	// RWLock implementation
	//////////////////////////////////////////////////////////////////////////
	inline RWLock::RWLock()
	{
	}

	inline RWLock::~RWLock()
	{
	}

	inline void RWLock::AcquireReadShared()
	{
		Base::AcquireReadSharedImpl();
	}

	inline void RWLock::AcquireWriteExclusive()
	{
		Base::AcquireWriteExclusiveImpl();
	}

	inline void RWLock::ReleaseReadShared()
	{
		Base::ReleaseReadSharedImpl();
	}

	inline void RWLock::ReleaseWriteExclusive()
	{
		Base::ReleaseWriteExclusiveImpl();
	}

// 	inline Bool RWLock::TryAcquireReadShared()
// 	{
// 		return Base::TryAcquireReadSharedImpl();
// 	}
// 
// 	inline Bool RWLock::TryAcquireWriteExclusive()
// 	{
// 		return Base::TryAcquireWriteExclusiveImpl();
// 	}

	//////////////////////////////////////////////////////////////////////////
	// Semaphore implementation
	//////////////////////////////////////////////////////////////////////////
	inline Semaphore::Semaphore( Int32 initialCount, Int32 maximumCount )
		: Base( initialCount, maximumCount )
	{
	}

	inline void Semaphore::Acquire()
	{
		Base::AcquireImpl();
	}

	inline Bool Semaphore::TryAcquire( TTimespec timeoutMS /*=0*/ )
	{
		return Base::TryAcquireImpl( timeoutMS );
	}

	inline void Semaphore::Release( Int32 count /*=1*/ )
	{
		Base::ReleaseImpl( count );
	}

	inline const void* Semaphore::GetOSHandle() const
	{
		return Base::GetOSHandleImpl();
	}

	//////////////////////////////////////////////////////////////////////////
	// Condition variable implementation
	//////////////////////////////////////////////////////////////////////////
	inline ConditionVariable::ConditionVariable()
	{
	}

	inline ConditionVariable::~ConditionVariable()
	{
	}

	inline void ConditionVariable::Wait( Mutex& mutex )
	{
		return Base::WaitImpl( mutex );
	}

	inline void ConditionVariable::Wait( Mutex& mutex, TTimespec timeoutMs )
	{
		return Base::WaitWithTimeoutImpl( mutex, timeoutMs );
	}

	inline void ConditionVariable::WakeAll()
	{
		Base::WakeAllImpl();
	}

	inline void ConditionVariable::WakeAny()
	{
		Base::WakeAnyImpl();
	}

	//////////////////////////////////////////////////////////////////////////
	// Manual reset event implementation
	//////////////////////////////////////////////////////////////////////////
	inline ManualResetEvent::ManualResetEvent( Bool startSignalled /*= false*/ )
		: Base( startSignalled )
	{
	}

	inline ManualResetEvent::~ManualResetEvent()
	{
	}

	inline void ManualResetEvent::SetEvent()
	{
		Base::SetEventImpl();
	}

	inline void ManualResetEvent::ResetEvent()
	{
		Base::ResetEventImpl();
	}

	inline void ManualResetEvent::Wait()
	{
		Base::WaitImpl();
	}

	inline Bool ManualResetEvent::TryWait( TTimespec timeoutMS /*=0*/ )
	{
		return Base::TryWaitImpl( timeoutMS );
	}

	inline const void* ManualResetEvent::GetOSHandle() const
	{
		return Base::GetOSHandleImpl();
	}

	// TODO get rid of this, we should use RWSpinLock instead
	//////////////////////////////////////////////////////////////////////////
	// Basic spin lock implementation
	//////////////////////////////////////////////////////////////////////////
	inline SpinLock::SpinLock( const Bool isInitiallyAquired /*= false*/ )
		: m_spinLock( isInitiallyAquired ? 1 : 0 )
	{
		profiler::SyncCreateMutex( this, "SpinLock" );
	}

	inline SpinLock::~SpinLock()
	{
		profiler::SyncDestroyMutex( this );
	}

	inline Bool SpinLock::TryAcquire()
	{
		profiler::SyncPrepare( this );
		Uint32 ret = atomic::Exchange8( &m_spinLock, 1 );
		if ( ret == 0 )
		{
			profiler::SyncAcquired( this );
			return true;
		}

		profiler::SyncCancel( this );
		return false;
	}

	inline void SpinLock::Acquire()
	{
		profiler::SyncPrepare( this );
		for ( Uint32 iter = 0; !TryAcquire(); ++iter )
		{
			YieldThread( iter );
		}
		profiler::SyncAcquired( this );
	}

	inline void SpinLock::Release()
	{
		profiler::SyncReleasing( this );
		atomic::Exchange8( &m_spinLock, 0 );
	}

	inline void SpinLock::YieldThread( Uint32 iter )
	{
		if ( iter < 16 )
		{
			/* do nothing - spin lock */
		}
		else
		{
			YieldCurrentThread();
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// Basic light weight mutex implementation
	//////////////////////////////////////////////////////////////////////////

	inline LightMutex::LightMutex()
		: m_threadLock( 0 )
		, m_counter( 0 )
		, m_recursion( 0 )
	{
		m_spinLock.Acquire();
	}

	inline void LightMutex::Acquire()
	{
		const Uint32 currentThreadID = GetCurrentThreadId();
		if ( atomic::Increment16( &m_counter ) > 1 ) // recursive entry and/or second entry
		{
			// we are calling this from other thread
			if ( m_threadLock != static_cast< atomic::TAtomic32 >( currentThreadID ) )
			{
				m_spinLock.Acquire();
			}
		}

		// acquire lock
		m_threadLock = currentThreadID;
		++m_recursion;
		RED_FATAL_ASSERT( m_recursion, "Synchronization error: Recursion overflow" );
	}

	inline void LightMutex::Release()
	{
		// NOTE: we assume we own the lock when calling this function
		RED_FATAL_ASSERT( m_recursion, "Usage error: Missing Acquired() to balance Release()" );
		const Uint8 recursion = --m_recursion;
		if ( 0 == recursion )
			m_threadLock = 0;

		// release the lock only if there was a distinct possibility we were waiting for it
		if ( atomic::Decrement16( &m_counter ) > 0 )
		{
			// if recusrion==0 than we are still holding some locks but non on this thread
			if ( recursion == 0 )
				m_spinLock.Release();
		}
	}

	inline Uint32 LightMutex::GetCurrentThreadId()
	{
		thread_local Bool currentThreadIdInitialized = false;
		thread_local Uint32 currentThreadIdCache = 0;
		if( currentThreadIdInitialized )
		{
			return currentThreadIdCache;
		}

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
		const Uint32 currentThreadId = static_cast< Uint32 >( ::GetCurrentThreadId() );
#elif defined( RED_PLATFORM_ORBIS )
		const Uint32 currentThreadId = static_cast< Uint32 >( ::scePthreadGetthreadid() );
#elif defined( RED_PLATFORM_LINUX )
		// Hope to avoid a syscall just to get an ID. otherwise syscall(SYS_gettid). Should be unqiue enough on Linux.
		const Uint32 currentThreadId = static_cast< Uint32 >( pthread_self() );
#else
# error Unsupported platform!
#endif

		currentThreadIdInitialized = true;
		currentThreadIdCache = currentThreadId;
		return currentThreadId;
	}

	//////////////////////////////////////////////////////////////////////////
	// UpdateFlag{Guard} implementation
	//////////////////////////////////////////////////////////////////////////

	RED_INLINE Bool UpdateFlag::WasSharedAcquired(Uint32 oldValue)
	{
		return (oldValue & UpdateFlag::s_sharedAccessMask) != 0;
	}

	RED_INLINE Bool UpdateFlag::WasNotExclusiveAcquired(Uint32 oldValue)
	{
		return (oldValue & UpdateFlag::s_exclusiveAccessMask) == 0;
	}

	RED_INLINE Bool UpdateFlag::WasOnceExclusiveAcquired(Uint32 oldValue)
	{
		return ( oldValue & UpdateFlag::s_exclusiveAccessMask ) == UpdateFlag::s_exclusiveAccessBit;
	}

	RED_INLINE Bool UpdateFlag::IsSharedOverflow(Uint32 newValue)
	{
		// Check if overflowed by setting the carry bit
		return (newValue & UpdateFlag::s_sharedOverflowCarryBit) != 0;
	}

	RED_INLINE Bool UpdateFlag::IsSharedUnderflow(Uint32 newValue)
	{
		// Check if the sentry bit was CLEARED, i.e., borrowed from
		// Practically speaking, we should never underflow by going below zero
		return (newValue & UpdateFlag::s_sharedUnderflowSentryBorrowBit) == 0;
	}

	RED_INLINE void UpdateFlag::AcquireShared()
	{
#ifdef RED_UPDATE_GUARD_ENABLED
		const Uint32 oldValue = m_value.ExchangeAdd( 1 );
		const Uint32 newValue = oldValue + 1U;
		ALWAYSENABLED_RED_FATAL_ASSERT( WasNotExclusiveAcquired(oldValue), "User synchronization error: simultaneous exclusive and shared access" );
		ALWAYSENABLED_RED_FATAL_ASSERT( !IsSharedOverflow(newValue), "Usage error: AcquireShared() overflow. Missing ReleaseShared()?" );
#endif
	}

	RED_INLINE void UpdateFlag::AcquireExclusive()
	{
#ifdef RED_UPDATE_GUARD_ENABLED
		const Uint32 oldValue = m_value.ExchangeAdd( UpdateFlag::s_exclusiveAccessBit ); // preserve any inclusive increment

		ALWAYSENABLED_RED_FATAL_ASSERT( WasNotExclusiveAcquired(oldValue), "Synchronization error: multiple simultaneous exclusive accesses" );

		// Assume always has the right to exclusive access, so any rogue shared access must be at fault instead.
		// Therefore sleep a moment and wait for the shared accessor to fatal assert when releasing, instead of asserting here.
		// This should make it easier to find the actual source of the problem.
		// Only remaining consideration is having to guess at an appropriate timeout and avoid any watchdog timeout. Could possibly set a flag
		// that the watchdog looks for, since we're going to fatal assert anyway, it's trivial to set a one-way global variable.
		// Console PLM (process lifetime management, e.g., suspending) timeouts could be another unavoidable issue if we wait too long.
		if (WasSharedAcquired(oldValue))
		{
			red::SleepOnCurrentThread(1000);
		}

		// The rogue reader failed to release in time, so check the parallel stacks to see what's going on.
		// Maybe it got deadlocked somewhere, in which case deadlock detection in our locks could also help. Assuming it's not just busy waiting itself.
		// E.g, a timeout in the lock itself or remember multiple lock acquisition orders, and check for any variation, which could then lead to deadlock.
		// Of course, there could then be livelock if it tries some back off strategy to locking multiple locks instead.
		ALWAYSENABLED_RED_FATAL_ASSERT( !WasSharedAcquired(oldValue), "Synchronization error: simultaneous exclusive and shared access" );
#endif
	}

	RED_INLINE void UpdateFlag::ReleaseExclusive()
	{
#ifdef RED_UPDATE_GUARD_ENABLED
		// Asserts also on releasing exclusive access. We want catch catch both threads in dump.
		const Uint32 oldValue = m_value.ExchangeAdd( static_cast< Uint32 >( -static_cast< Int32 >( UpdateFlag::s_exclusiveAccessBit ) ) );
		ALWAYSENABLED_RED_FATAL_ASSERT( WasOnceExclusiveAcquired(oldValue), "Synchronization error: multiple simultaneous exclusive accesses" );

		// Shared access will assert if locked, and don't want to beat it and assert here first,
		// because it's better to assume rogue shared access is at fault.
		if (WasSharedAcquired(oldValue))
		{
			red::SleepOnCurrentThread(1000);
		}

		ALWAYSENABLED_RED_FATAL_ASSERT( !WasSharedAcquired(oldValue), "Synchronization error: simultaneous exclusive and shared access" );
#endif
	}

	RED_INLINE void UpdateFlag::ReleaseShared()
	{
#ifdef RED_UPDATE_GUARD_ENABLED
		// Can't assert at the violator, since can't know didn't add to the value, but will eventually assert on ReleaseShared.
		const Uint32 oldValue = m_value.ExchangeAdd( static_cast< Uint32 >( -1 ) );
		const Uint32 newValue = oldValue - 1U;
		ALWAYSENABLED_RED_FATAL_ASSERT( !IsSharedUnderflow(newValue), "Usage error: Missing AcquiredShared() to balance ReleaseShared()" );

		// AcquireExclusive() is sleeping, waiting for this to hit. Assumes rogue shared access always at fault, and not exclusive access.
		ALWAYSENABLED_RED_FATAL_ASSERT( WasNotExclusiveAcquired(oldValue), "Synchronization error: simultaneous exclusive and shared access" );
#endif
	}

	RED_INLINE UpdateFlagGuard::UpdateFlagGuard( UpdateFlag& flag, Mode mode )
		: m_flag( flag )
		, m_mode( mode )
	{
		if ( mode == Exclusive )
		{
			m_flag.AcquireExclusive();
		}
		else
		{
			m_flag.AcquireShared();
		}
	}

	RED_INLINE UpdateFlagGuard::~UpdateFlagGuard()
	{
		if ( m_mode == Exclusive )
		{
			m_flag.ReleaseExclusive();
		}
		else
		{
			m_flag.ReleaseShared();
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Thread object implementation
	//////////////////////////////////////////////////////////////////////////

	inline const AnsiChar* Thread::GetThreadName() const
	{
		return m_threadName;
	}

} // namespace red

#endif // RED_THREADS_THREAD_INL