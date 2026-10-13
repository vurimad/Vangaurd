/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_WORKER_H_
#define _RED_SYSTEM_LOGGER_WORKER_H_

namespace red
{
	class Logger;

	class REDSYSTEM_API LoggerWorker : public red::Thread
	{
	public:

		LoggerWorker();
		virtual ~LoggerWorker();

		RED_MOCKABLE void Start( Logger * logger );
		RED_MOCKABLE void Stop();

		virtual void ThreadFunc();

	private:

		bool m_isRunning;
		Logger * m_logger;
	};
}

#endif
