/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "loggerSink.h"
#include "logMessage.h"

namespace red
{
	LoggerSink::~LoggerSink()
	{}

	void LoggerSink::SinkLogLine( const char * formattedMessage, const LoggerLineWithDataError & message )
	{
		//SinkLogLine( formattedMessage, message.line );
	}

}
