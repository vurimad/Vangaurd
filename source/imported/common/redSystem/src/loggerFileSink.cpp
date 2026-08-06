/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "loggerLocklessQueue.h"
#include "loggerFileSink.h"
#include "file.h"

namespace dd
{
	struct LoggerFileSinkCrashData
	{
		red::CrashData<const char*, 64> logFileName{ "Logger/Sinks/File", "FileName" };
		// Not necessarily conclusive on consoles, at least Xbox if you try to write to the G: drive
		red::CrashData<Bool> logWasOpened{ "Logger/Sinks/File", "WasOpened" };
	};

	static LoggerFileSinkCrashData& GetCrashData()
	{
		static LoggerFileSinkCrashData crashData;
		return crashData;
	}
}

namespace red
{
	// NOTE: it's too late on PS4 to make use of this; so instead the crash handler dumps the logger queue to a text file
	atomic::TAtomic32 s_crashModeSignal = 0;

	LoggerFileSink::LoggerFileSink()
		: m_file( nullptr )
		, m_numLinesSunk( 0 )
	{
	}

	LoggerFileSink::~LoggerFileSink()
	{
		CloseFile();
	}

	red::Bool LoggerFileSink::OpenFile( const char * filename, const char * mode )
	{
		const Bool ret = FileOpen( &m_file, filename, mode );
		
		auto& crashData = dd::GetCrashData();
		crashData.logFileName.Set(filename);
		crashData.logWasOpened.Set(ret);
		return ret;
	}

	red::Bool LoggerFileSink::CloseFile()
	{
		red::Bool result = true;
		if ( m_file )
		{
			result = FileClose( m_file );
		}

		m_file = nullptr;
		return result;
	}

	void LoggerFileSink::SinkLogLine( const char * formattedMessage, const LoggerLine & message )
	{
		if ( m_file )
		{
			FilePrint( m_file, formattedMessage );
		}
	}
	
	void LoggerFileSink::Flush()
	{
		if( m_file )
		{
			const Bool isCrashMode = const_cast<volatile atomic::TAtomic32 &>(s_crashModeSignal) != 0;
			
			//#tbd: can remove this, useful for verification in the meantime
			if (isCrashMode)
			{
				FilePrint(m_file, "!!! FLUSHING CRASH LOG !!!");
			}

			FileFlush( m_file );

			// On Xbox, we need to close the file to be able to have WER attach it
			// On PS4, we probably won't even get here since threads seem to get suspended
			// On PC, may as well also close it and ensure actually flushed but shouldn't have to
#if defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WINPC )
			if (isCrashMode)
			{
				CloseFile();
			}
#endif
		}
	}

	void LoggerFileSink::SendEmergencyCrashModeSignal()
	{
		atomic::Exchange32(&s_crashModeSignal, 1);
	}

}
