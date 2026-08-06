/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "loggerWorker.h"
#include "logger.h"
#include "errorHandler.h"

namespace red
{
	const char * c_loggerWorkerName = "redLoggerWorker";
	Uint32 c_loggerWorkerStackSize = 128 * 1024;

	LoggerWorker::LoggerWorker()
		:	red::Thread( c_loggerWorkerName, { c_loggerWorkerStackSize } ),
			m_isRunning( false ),
			m_logger( nullptr )
	{}

	LoggerWorker::~LoggerWorker()
	{}

	void LoggerWorker::Start( Logger * logger )
	{
		m_logger = logger;
		m_isRunning = true;
		InitThread();
		SetPriority( TP_Normal );
#ifdef RED_PLATFORM_CONSOLE
		SetAffinityMask( RED_FLAG(6) );
#endif

//#tbd: Xbox
#ifdef RED_PLATFORM_ORBIS
		// This thread shouldn't be blocked, because then if the logger queue is full
		// we're backlogging everything else, possibly deadlocking if this thread doesn't run
		SetPriority( red::TP_Highest );
#endif
	}

	void LoggerWorker::Stop()
	{
		m_isRunning = false;
		JoinThread();
	}

	void LoggerWorker::ThreadFunc()
	{
		red::SetLoggerThreadId( red::ThreadId::CurrentThread() );
		while( m_isRunning )
		{
			while ( m_logger->ConsumeNextMessage() )
			{
				continue;
			}

#ifdef RED_PLATFORM_ORBIS
			red::SleepOnCurrentThread( 1 );
#endif
		}
	}
}
