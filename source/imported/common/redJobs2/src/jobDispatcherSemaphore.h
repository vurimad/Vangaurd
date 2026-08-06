/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace job
{

namespace prv
{

// A semaphore that's faster to Acquire() when resources are available,
// since it avoids a call into the kernel.
// Spins for a specified count if can't immediately Acquire().
class DispatcherSemaphore : red::NonCopyable
{
public:
	DispatcherSemaphore();
	~DispatcherSemaphore();

	void SetSpinCount( Int32 spinCount );
	void Acquire();
	Bool TryAcquire();
	void Release( Int32 releaseCount = 1 );

private:
	red::Atomic<Int32 > m_count;
	red::Semaphore m_numThreadsWaiting;
	RED_ALIGNED_VAR( Int32, 64 ) m_spinCount;
};

}

}
