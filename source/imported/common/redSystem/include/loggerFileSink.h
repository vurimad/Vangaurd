/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_FILE_SINK_H_
#define _RED_SYSTEM_LOGGER_FILE_SINK_H_

#include "loggerSink.h"
#include <stdio.h>

namespace red
{
	class REDSYSTEM_API LoggerFileSink : public LoggerSink
	{
	public:

		LoggerFileSink();
		~LoggerFileSink();

		red::Bool OpenFile( const char * filename, const char* mode = "w" );
		red::Bool CloseFile();

		virtual void SinkLogLine( const char * formattedMessage, const LoggerLine & message ) override final;
		virtual void Flush() override final;

		static void SendEmergencyCrashModeSignal();

	private:
		FILE* m_file;
		Uint64 m_numLinesSunk;
	};
}

#endif
