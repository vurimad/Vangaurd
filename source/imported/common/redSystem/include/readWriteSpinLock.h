/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_THREADS_READ_WRITE_SPIN_LOCK_H_
#define _RED_THREADS_READ_WRITE_SPIN_LOCK_H_

#include "redThreadsAtomic.h"

#if defined(RED_PLATFORM_DURANGO)
	// Switch to allow Xbox to use native kernel objects
	//#define USE_NATIVE_RWLOCK
#endif

namespace red
{
class RWSpinLock
{
public:

	RWSpinLock();
	~RWSpinLock();

	REDSYSTEM_API void AcquireShared();
	REDSYSTEM_API void Acquire();
	REDSYSTEM_API bool TryAcquire();
	REDSYSTEM_API bool TryAcquireShared();

	void ReleaseShared();
	void Release();

private:

	void YieldThread( Uint32& spinCount );

#if defined(USE_NATIVE_RWLOCK)
	SRWLOCK m_SRWLock;
#else
	static constexpr Int8 UnlockValue = 0;
	static constexpr Int8 WriteLockValue = -1;

	atomic::TAtomic8 m_lock;
#endif

};
} // namespace red

#include "readWriteSpinLock.inl"

#endif
