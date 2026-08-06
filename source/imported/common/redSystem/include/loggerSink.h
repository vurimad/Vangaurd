/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_SINK_H_
#define _RED_SYSTEM_LOGGER_SINK_H_

namespace red
{
	struct LoggerLine;
	struct LoggerLineWithDataError;

	class REDSYSTEM_API LoggerSink
	{
	public:

		virtual void SinkLogLine( const char * formattedMessage, const LoggerLine & message ) = 0;
		virtual void SinkLogLine( const char * formattedMessage, const LoggerLineWithDataError & message );

		virtual void Flush() = 0;
	
	protected:
	
		virtual ~LoggerSink();
	};
}

#endif
