/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if 0

namespace tools
{

namespace prv
{
	class JobThread;
}

class IBackgroundJob;

class BACKEND_ENGINE_API BackgroundJobManager : private red::NonCopyable
{
public:
	BackgroundJobManager();
	~BackgroundJobManager();
	void Init( );
	void Shutdown();

public:
	//! Issue single job
	void Issue( IBackgroundJob* job );

private:
	friend class prv::JobThread;
	IBackgroundJob* PopJob();

private:
	red::DynArray< red::UniquePtr< prv::JobThread > > m_jobThreads;
	
private:
	red::UniquePtr< red::Semaphore >	m_pProducerCount;
	red::UniquePtr< red::Semaphore >	m_pConsumerCount;
	red::DynArray< IBackgroundJob* >	m_jobQueue;
	Uint32 								m_jobQueueTail;
	Uint32 								m_jobQueueHead;

	// #tbd: if really need lockless, need a MPMC ringbuffer, e.g., 
	// something like http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
	// Can't just interlocked increment the head/tail constrained with semaphores, even looping on a null job check
	// because a consumer thread could loop around and "steal" the job that another consumer got the index for but hasn't read out and nulled yet.
	// Otherwise would need some barrier to detect delayed consumers.
	red::Mutex							m_jobQueueMutex;
};

// Singleton as requested (vs e.g., part of backend engine)
BACKEND_ENGINE_API BackgroundJobManager& GetJobManager();
}

#endif