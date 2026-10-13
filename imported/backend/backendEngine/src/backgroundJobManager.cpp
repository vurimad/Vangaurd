/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redSystem/include/utility.h"

#include "backgroundJob.h"
#include "backgroundJobManager.h"
#include "../../common/redCore/include/singleton.h"

#if 0

namespace Config
{
	BACKEND_ENGINE_API TConfigVar< Int32 >	cvNumJobThreads( "Engine/Backend/JobManager", "NumThreads", 2, 0, 64, eConsoleVarFlag_Save );
	BACKEND_ENGINE_API TConfigVar< Int32 >	cvJobQueueSize( "Engine/Backend/JobManager", "QueueSize", 8192, 1, 8192, eConsoleVarFlag_Save );
	BACKEND_ENGINE_API TConfigVar< Int32 >	cvJobThreadStackSizeKB( "Engine/Backend/JobManager", "ThreadStackSizeKB", 1024, 32, 2048, eConsoleVarFlag_Save );
	BACKEND_ENGINE_API TConfigVar< Int32 >	cvJobThreadPriority( "Engine/Backend/JobManager", "ThreadPriority", red::TP_Normal, red::TP_Idle, red::TP_TimeCritical, eConsoleVarFlag_Save );
}

namespace tools
{

namespace prv
{
	RED_TLS Bool GIsJobThread;

	class JobThread : public red::Thread
	{
	public:
		JobThread( BackgroundJobManager& jobManager, Uint32 stackSize );
		virtual void ThreadFunc() override;

	public:
		RED_FORCE_INLINE void RequestExit()
		{
			m_exit.SetValue( true );
		}

	private:
		BackgroundJobManager&			m_jobManager;
		red::Atomic< Bool >	m_exit;
	};

	JobThread::JobThread( BackgroundJobManager& jobManager, Uint32 stackSize)
		: red::Thread( "backendWorker", red::ThreadMemParams(stackSize) )
		, m_jobManager( jobManager )
		, m_exit( false )
	{}

	void JobThread::ThreadFunc()
	{
		GIsJobThread = true;
		while ( !m_exit.GetValue() )
		{
			IBackgroundJob* job = m_jobManager.PopJob();
			RED_FATAL_ASSERT(job, "");
			job->RunJob();
			job->Release();
		}
	}
}

BackgroundJobManager& GetJobManager()
{
	return red::TSingleton< BackgroundJobManager >::GetInstance();
}

BackgroundJobManager::BackgroundJobManager()
	: m_jobQueueTail( 0 )
	, m_jobQueueHead( 0 )
{
}

BackgroundJobManager::~BackgroundJobManager()
{
	Shutdown();
}

void BackgroundJobManager::Init()
{
	RED_FATAL_ASSERT ( ::SIsMainThread(), "" );

	const Uint32 numJobs = Config::cvNumJobThreads.Get();
	if ( numJobs < 1 )
	{
		RED_LOG( "Engine: tools::JobManager threading disabled" );
		return;
	}

	const Int32 queueSizePow2 = Config::cvJobQueueSize.Get();
	m_jobQueue.Resize( red::RoundUpToPowerOf2(queueSizePow2) );
	m_pProducerCount.Reset( RED_NEW(red::Semaphore)(queueSizePow2, queueSizePow2) );
	m_pConsumerCount.Reset( RED_NEW(red::Semaphore)(0, queueSizePow2) );

	const red::EThreadPriority threadPriority = static_cast< red::EThreadPriority >( Config::cvJobThreadPriority.Get() );
	const Uint32 stackSize = Config::cvJobThreadStackSizeKB.Get() * 1024;
	m_jobThreads.Resize( numJobs );
	for ( Uint32 i = 0; i < numJobs; ++i )
	{
		m_jobThreads[i].Reset( RED_NEW(prv::JobThread)(*this, stackSize) );
		m_jobThreads[i]->InitThread();
		m_jobThreads[i]->SetPriority( threadPriority );
	}
}

void BackgroundJobManager::Issue( IBackgroundJob* job )
{
	job->ScheduleJob();
	job->AddRef();
	if ( prv::GIsJobThread || m_jobThreads.Size() < 1 )
	{
		job->RunJob();
		job->Release();
		return;
	}

	m_pProducerCount->Acquire();
	{
		red::ScopedLock<red::Mutex> lock( m_jobQueueMutex );
		const Uint32 index = (m_jobQueueTail++) & (m_jobQueue.Size()-1);
		m_jobQueue[ index ] = job;
	}
	m_pConsumerCount->Release();
}

IBackgroundJob* BackgroundJobManager::PopJob()
{
	IBackgroundJob* job = nullptr;
	m_pConsumerCount->Acquire();
	{
		red::ScopedLock<red::Mutex> lock( m_jobQueueMutex );
		const Uint32 index = (m_jobQueueHead++) & (m_jobQueue.Size()-1);
		job = m_jobQueue[index];
	}
	m_pProducerCount->Release();
	return job;
}

void BackgroundJobManager::Shutdown()
{
	RED_FATAL_ASSERT ( ::SIsMainThread(), "" );

	for ( auto& thread : m_jobThreads )
	{
		thread->RequestExit();
	}

	class ShutdownJob : public IBackgroundJob
	{
		virtual Bool Process() override { return true; }
	};

	for ( Uint32 i = 0; i < m_jobThreads.Size(); ++i )
	{
		IBackgroundJob* job = RED_NEW(ShutdownJob);
		Issue( job );
		job->Release();
	}

	for ( auto& thread : m_jobThreads )
	{
		thread->JoinThread();
	}

	// Put into state where can be reinitialized for unit tests
	m_jobThreads.Clear();
	m_jobQueue.Clear();
	m_jobQueueHead = 0;
	m_jobQueueTail = 0;
}

}

#else

RED_NO_EMPTY_FILE()

#endif

