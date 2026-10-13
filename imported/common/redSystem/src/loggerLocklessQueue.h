/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_LOCKLESS_QUEUE_H_
#define _RED_SYSTEM_LOGGER_LOCKLESS_QUEUE_H_

#include "../include/logMessage.h"
#include "../include/redThreadsAtomic.h"

namespace red
{
	const Uint32 c_loggerQueueSize = 128;

	namespace err
	{
		template< typename TLoggerLine >
		class OrbisLoggerCrashDumper;
	}

	template< typename TLoggerLine >
	class LoggerLocklessQueue
	{
		friend class err::OrbisLoggerCrashDumper< TLoggerLine >;

	public:

		LoggerLocklessQueue();
		~LoggerLocklessQueue();

		bool QueueMessage( TLoggerLine && message );
		bool DequeueMessage( TLoggerLine & message );

	private:

		struct Entry
		{
			atomic::TAtomic32 position;
			TLoggerLine message;
		};

		RED_ALIGN( 64 ) atomic::TAtomic32 m_queuePosition;
		RED_ALIGN( 64 ) atomic::TAtomic32 m_dequeuePosition;
		RED_ALIGN( 64 ) Entry m_entries[ c_loggerQueueSize ];
	};
}

#include "loggerLocklessQueue.hpp"

#endif
