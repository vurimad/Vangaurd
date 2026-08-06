/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_THREAD_H
#define RED_THREADS_THREAD_H
#pragma once

#include "redThreadsPlatform.h"
#include "redThreadsCommon.h"
#include "redThreadsAtomic.h"
#include "redThreadsRedSystem.h"
#include "readWriteSpinLock.h"

namespace red
{
	//////////////////////////////////////////////////////////////////////////
// Thread initialization parameters
//////////////////////////////////////////////////////////////////////////
struct REDSYSTEM_API ThreadMemParams
{
	TStackSize m_stackSize;

	ThreadMemParams( TStackSize stackSize = OSAPI::g_kDefaultSpawnedThreadStackSize );
};

}  // namespace red

//////////////////////////////////////////////////////////////////////////
// Synchronization object decls
//////////////////////////////////////////////////////////////////////////
#if defined( RED_THREADS_PLATFORM_WINDOWS_API )
#	include "redThreadsThreadWinAPI.h"
#elif defined( RED_THREADS_PLATFORM_ORBIS_API )
#	include "redThreadsThreadOrbisAPI.h"
#elif defined( RED_THREADS_PLATFORM_LINUX_API )
#	include "redThreadsThreadLinuxAPI.h"
#else
#	error No thread implementation for platform
#endif



namespace red {

    struct ThreadId;

	template< typename TAcquireRelease >
	class ScopedLock
	{
		REDTHR_NOCOPY_CLASS( ScopedLock );

	private:
		TAcquireRelease& m_syncObjectRef;

	public:
		ScopedLock( TAcquireRelease& syncObject );
		~ScopedLock();
	};

// Macro for easy scope lock object creation
#define RED_SCOPE_LOCK(lock)	red::ScopedLock< decltype(lock) > RED_CONCATENATE2( _varLock, __LINE__ ) ( lock )

	template< typename TAcquireReleaseShared >
	class ScopedSharedLock
	{
		REDTHR_NOCOPY_CLASS( ScopedSharedLock );

	private:
		TAcquireReleaseShared& m_syncObjectRef;

	public:
		ScopedSharedLock( TAcquireReleaseShared& syncObject );
		~ScopedSharedLock();
	};

	// Macro for easy scope shared lock object creation
#define RED_SCOPE_SHARED_LOCK(lock)	red::ScopedSharedLock< decltype(lock) > RED_CONCATENATE2( _varLock, __LINE__ ) ( lock )

	class Mutex : private OSAPI::MutexImpl
	{
		REDTHR_NOCOPY_CLASS( Mutex );

		friend class ConditionVariable;

	private:
		typedef OSAPI::MutexImpl Base;

	public:
		Mutex();
		~Mutex();

	public:
		void Acquire();
		Bool TryAcquire();
		void Release();
		void SetSpinCount( TSpinCount count );
	};

	// NOTE: not reentrant
	class SpinLock
	{
		REDTHR_NOCOPY_CLASS( SpinLock );

	public:
		SpinLock( const Bool isInitiallyAquired = false );
		~SpinLock();

		Bool TryAcquire();
		void Acquire();
		void Release();

	private:
		atomic::TAtomic8	m_spinLock;

		static void YieldThread( Uint32 iter );
	};

	// Cheap light weight mutex, sizeof() = 16, can yield
	class LightMutex
	{
		REDTHR_NOCOPY_CLASS( LightMutex );

	public:
		LightMutex();

		void Acquire();
		void Release();

	private:
		atomic::TAtomic32	m_threadLock;
		atomic::TAtomic16	m_counter;
		Uint8				m_recursion;
		RWSpinLock			m_spinLock;

		static Uint32 GetCurrentThreadId();
	};

	class NullMutex
	{
		REDTHR_NOCOPY_CLASS( NullMutex );

	public:
		NullMutex();
		~NullMutex();

	public:
		void Acquire();
		Bool TryAcquire();
		void Release();
		void SetSpinCount( TSpinCount count );
	};

	// Note: Don't use recursively or upgrade locks from read to write (or vice versa) - thin API wrapper at the moment.
	class RWLock : private OSAPI::RWLockImpl
	{
		REDTHR_NOCOPY_CLASS( RWLock );
		
	private:
		typedef OSAPI::RWLockImpl Base;

	public:
		RWLock();
		~RWLock();

	public:
		void Acquire() { AcquireWriteExclusive(); }
		void Release() { ReleaseWriteExclusive(); }
		void AcquireShared() { AcquireReadShared(); }
		void ReleaseShared() { ReleaseReadShared(); }

		void AcquireReadShared();
		void AcquireWriteExclusive();

		void ReleaseReadShared();
		void ReleaseWriteExclusive();

//		Not supported on Vista
// 		Bool TryAcquireReadShared();
// 		Bool TryAcquireWriteExclusive();
	};

	class Semaphore : private OSAPI::SemaphoreImpl
	{
		REDTHR_NOCOPY_CLASS( Semaphore );

	private:
		typedef OSAPI::SemaphoreImpl Base;

	public:
		Semaphore( Int32 initialCount, Int32 maximumCount );

	public:
		void		Acquire();
		Bool		TryAcquire( TTimespec timeoutMs = 0 );
		void		Release( Int32 count = 1);
		const void* GetOSHandle() const;
	};

	class ConditionVariable : private OSAPI::ConditionVariableImpl
	{
		REDTHR_NOCOPY_CLASS( ConditionVariable );

	private:
		typedef OSAPI::ConditionVariableImpl Base;

	public:
		ConditionVariable();
		~ConditionVariable();

	public:
		void Wait( Mutex& mutex );
		void Wait( Mutex& mutex, TTimespec timeoutMs );
		void WakeAll();
		void WakeAny();
	};

	class ManualResetEvent : private OSAPI::ManualResetEventImpl
	{
		REDTHR_NOCOPY_CLASS( ManualResetEvent );

	private:
		typedef OSAPI::ManualResetEventImpl Base;

	public:
		explicit ManualResetEvent( Bool startSignalled = false );
		~ManualResetEvent();

	public:
		void SetEvent();
		void ResetEvent();
		void Wait();
		Bool TryWait( TTimespec timeoutMS = 0 );
		const void* GetOSHandle() const;
	};

// #tbd: enable even in final build
#ifdef RED_ASSERTS_ENABLED
# define RED_UPDATE_GUARD_ENABLED
#endif

	// Basically a RWLock that fatal asserts instead of blocks. Some simple bool isUpdating is insufficent because we might start our on OnTick while
	// some system is already in the middle of spawning an effect and modifying the state (if ony it were guaranteed that this system ran first).
	//
	// The gist is to partition the bits so 0-30 is for readers, and 31 for the writer.
	// Atomically increment the lower bits for each reader, and the writer atomically sets the highest bit,
	// assuming there cannot be billions of simultaneous readers.
	class UpdateFlag
	{
	public:
		explicit UpdateFlag( const char* assertMessage )
#ifdef RED_UPDATE_GUARD_ENABLED
			: m_value{ s_sharedUnderflowSentryBorrowBit }
#endif
		{
			// #todo: remove from ctor
			RED_UNUSED(assertMessage);
		}

	public:
		void AcquireShared();
		void ReleaseShared();

		void AcquireExclusive();
		void ReleaseExclusive();

		RED_INLINE void Acquire() { AcquireExclusive(); }
		RED_INLINE void Release() { ReleaseExclusive(); }

	private:
		static Bool WasSharedAcquired(Uint32 oldValue);
		static Bool WasNotExclusiveAcquired(Uint32 oldValue);
		static Bool WasOnceExclusiveAcquired(Uint32 oldValue);
		static Bool IsSharedOverflow(Uint32 newValue);
		static Bool IsSharedUnderflow(Uint32 newValue);

		// The sentryBit is set by default to protect the exclusive bit.
		// So if shared access "underflows", then we'll detect a borrow from the sentry bit
		// instead of borrowing from the exclusiveBit and incorrectly clearing the exclusive access.
		//
		// Under/overflow detection assumes you have enough "runway", e.g., there aren't literally a billion threads that can all underflow ReleaseShared() at once
		// and borrow from the exclusiveBit after exhausting the numerical range below s_sentryBit.
		static const Uint32 s_exclusiveAccessBit = 1U << 24;
		static const Uint32 s_exclusiveAccessMask = ~(s_exclusiveAccessBit - 1U);
		static const Uint32 s_sharedUnderflowSentryBorrowBit = 1U << 23; // Should always be set!
		static const Uint32 s_sharedOverflowCarryBit = 1U << 22;
		static const Uint32 s_sharedAccessMask = s_sharedOverflowCarryBit | (s_sharedOverflowCarryBit - 1U); // all the bits that can be set when has shared access

#ifdef RED_UPDATE_GUARD_ENABLED
		red::Atomic< Uint32 > m_value;
#endif
	};

	// Single "writer", multiple "readers" low contention
	class UpdateFlagGuard
	{
	public:
		enum Mode
		{
			Inclusive,
			Exclusive,
		};

		UpdateFlagGuard( UpdateFlag& flag, Mode mode );
		~UpdateFlagGuard();

		UpdateFlagGuard( const UpdateFlag& ) = delete;
		UpdateFlagGuard( UpdateFlag&& ) = delete;

	private:
		UpdateFlag& m_flag;
		Mode m_mode;
	};

} // namespace red

#define RED_SCOPE_FLAG_GUARD( __updateFlag__, __mode__ ) ::red::UpdateFlagGuard RED_CONCATENATE2( updateFlagGuard, __LINE__ )( __updateFlag__, ::red::UpdateFlagGuard::Mode::__mode__ )

//////////////////////////////////////////////////////////////////////////
// Thread object decl
//////////////////////////////////////////////////////////////////////////
namespace red {

    struct ThreadId;

	const size_t g_kMaxThreadNameLength = 31; //!< Maximum supported debugger thread name length (exluding a null terminator).

	extern REDSYSTEM_API void YieldCurrentThread();
	extern REDSYSTEM_API void SleepOnCurrentThread( TTimespec sleepTimeInMS );
	extern REDSYSTEM_API void SetCurrentThreadAffinity( TAffinityMask affinityMask );
	extern REDSYSTEM_API void SetCurrentThreadName( const char* threadName );
#ifdef RED_THREADS_SUPPORTS_SUSPEND
	extern REDSYSTEM_API void SuspendThreadById( red::ThreadId threadId );
	extern REDSYSTEM_API void ResumeThreadById( red::ThreadId threadId );
#endif // #ifdef RED_THREADS_SUPPORTS_SUSPEND

	// Get the max hardware concurrency level. Doesn't necessarily mean the best in the case of consoles
	// where the core may not be used exclusively, such as 7th CPU mode.
	extern REDSYSTEM_API Uint32 GetMaxHardwareConcurrency();

	class REDSYSTEM_API Thread
	{
		REDTHR_NOCOPY_CLASS( Thread );

	private:
		typedef OSAPI::ThreadImpl ThreadImpl;

	private:
		ThreadImpl			m_threadImpl;
		AnsiChar			m_threadName[ g_kMaxThreadNameLength + 1 ];

	public:
							Thread( const AnsiChar* threadName, const ThreadMemParams& memParams = ThreadMemParams() );
 		virtual				~Thread();
		void				JoinThread();
		void				DetachThread();
		Bool				IsValid() const;

		// Not called from the constructor since ThreadFunc is virtual.
		void				InitThread();

	public:
		virtual void		ThreadFunc() = 0;

	public:
		const AnsiChar*		GetThreadName() const;

	public:
		void				SetAffinityMask( TAffinityMask mask );
		void				SetPriority( EThreadPriority priority );
#if defined(RED_PLATFORM_DURANGO) || defined(RED_THREADS_PLATFORM_WINDOWS_API)
		void				DisablePriorityBoost( Bool threadBoostDisabled );
#endif

	public:
		Bool				operator==( const Thread& rhs ) const;
	};

} // namespace red

#if defined( RED_THREADS_PLATFORM_WINDOWS_API )
#	include "redThreadsThreadWinAPI.inl"
#elif defined( RED_THREADS_PLATFORM_ORBIS_API )
#	include "redThreadsThreadOrbisAPI.inl"
#elif defined( RED_THREADS_PLATFORM_LINUX_API )
#	include "redThreadsThreadLinuxAPI.inl"
#else
#	error No thread implementation for platform
#endif

#include "redThreadsThread.inl"

REDSYSTEM_API const Bool SIsMainThread();

#endif // RED_THREADS_THREAD_H