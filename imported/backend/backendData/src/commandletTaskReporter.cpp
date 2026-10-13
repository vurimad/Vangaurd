/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "commandletTaskReporter.h"

namespace commandlet
{

CommandletTaskReporter::CommandletTaskReporter( red::IProcessStatusReporter& commandletReporter )
	: m_commandletReporter( commandletReporter )
	, m_outputType( OutputType::NONE )
	, m_handle( INVALID_HANDLE_VALUE )
	, m_lastLineCount( 0 )
	, m_opened( false )
{
}

CommandletTaskReporter::~CommandletTaskReporter()
{
	if ( m_opened )
	{
		CloseOutputDevice();
	}
}

void CommandletTaskReporter::SetTasksCount( Uint32 count )
{
	RED_SCOPE_LOCK( m_guard );
	m_tasksCount = count;
	m_finishedTasksCount = 0;

	if ( m_outputType == OutputType::INTERACTIVE )
	{
		m_cachedStageId = m_commandletReporter.GetCurrentStageId();
		m_taskId = m_commandletReporter.GetTask( "Processed tasks" );
		m_messageId = m_commandletReporter.GetMessage( m_taskId );

		m_timer.Reset();
	}
}

void CommandletTaskReporter::UpdateSystemStatus( const red::String& str )
{
	if ( m_outputType == OutputType::CONSOLE )
	{
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		GetConsoleScreenBufferInfo( m_handle, &consoleInfo );
		const Uint32 lineWidth = consoleInfo.dwSize.X;

		DWORD written = 0;
		COORD coord = m_statusPosition;
		WriteConsoleOutputCharacterA( m_handle, str.AsChar(), str.Length(), coord, &written );
		if ( written < lineWidth )
		{
			coord.X += static_cast< SHORT >( written );
			FillConsoleOutputCharacterA( m_handle, ' ', lineWidth - written, coord, &written );
		}
		::SetConsoleCursorPosition( m_handle, coord );
	}
	else if ( m_outputType == OutputType::DIRECT )
	{
		fprintf( stdout, "%hs\n", str.AsChar() );
	}
}

void CommandletTaskReporter::UpdateTaskProgress( Uint32 task, red::String&& message )
{
	if ( m_outputType == OutputType::INTERACTIVE || m_outputType == OutputType::CONSOLE )
	{
		red::ScopedLock< red::SpinLock > lock( m_guard );
		m_taskStatusMessages.Set( task, std::move( message ) );
		if ( ShouldFlushProgressMessage_NoLock() )
		{
			FlushProgressMessage_NoLock();
		}
	}
	else if ( m_outputType == OutputType::DIRECT )
	{
		red::ScopedLock< red::SpinLock > lock( m_guard );
		fprintf( stdout, "%hs\n", message.AsChar() );
	}
}

void CommandletTaskReporter::ClearTaskProgress( Uint32 task )
{
	if ( m_outputType == OutputType::INTERACTIVE || m_outputType == OutputType::CONSOLE )
	{
		red::ScopedLock< red::SpinLock > lock( m_guard );
		m_taskStatusMessages.Remove( task );
		m_finishedTasksCount += 1;

		if ( ShouldFlushProgressMessage_NoLock() )
		{
			FlushProgressMessage_NoLock();
		}
	}
}

bool CommandletTaskReporter::ShouldFlushProgressMessage_NoLock()
{
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	if ( now >= m_nextFlushTimestamp )
	{
		m_nextFlushTimestamp = now + std::chrono::milliseconds( 50 );
		return true;
	}
	return false;
}

void CommandletTaskReporter::FlushProgressMessage_NoLock()
{
	if ( m_outputType == OutputType::INTERACTIVE )
	{
		// Display fewer active jobs in the editor as the commandlet runner dialog doesn't show more than that
		constexpr const Uint32 c_maxTasksToDisplay = 8;

		String message;

		const auto allTaskStrings = m_taskStatusMessages.Values();
		if ( allTaskStrings.Size() > c_maxTasksToDisplay )
		{
			const auto tasksToDisplay = allTaskStrings.SubSpan( 0, c_maxTasksToDisplay );
			message = String::Join( tasksToDisplay, "\n" );
			message += String::Printf( "\n ... and %u more tasks.", allTaskStrings.Size() - c_maxTasksToDisplay );
		}
		else
		{
			message = String::Join( allTaskStrings, "\n" );
		}

		if ( m_cachedStageId != m_commandletReporter.GetCurrentStageId() )
		{
			m_taskId = -1;
			m_messageId = -1;
		}

		if ( m_taskId != -1 )
		{
			const Float processingTime = (Float)m_timer.GetDelta();
			m_commandletReporter.SetTaskProgress( m_taskId, m_finishedTasksCount, m_tasksCount, processingTime );
			m_commandletReporter.SetTaskMessage( m_taskId, m_messageId, message );
		}
	}
	else if ( m_outputType == OutputType::CONSOLE )
	{
		// The interactive console window can show more jobs so have a reasonable limit based on current-ish CPU thread count
		constexpr const Uint32 c_maxTasksToDisplay = 20;

		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		GetConsoleScreenBufferInfo( m_handle, &consoleInfo );
		const Uint32 lineWidth = consoleInfo.dwSize.X;

		Uint32 writtenLineCount = 0;
		COORD coord = m_progressPosition;
		auto WriteLine = [this, &coord, &writtenLineCount, lineWidth]( const String& str )
		{
			DWORD written = 0;
			// Write text data, it may wrap to the next line
			WriteConsoleOutputCharacterA( m_handle, str.AsChar(), str.Length(), coord, &written );
			coord.X = static_cast< SHORT >( written % lineWidth );
			coord.Y += static_cast< SHORT >( written / lineWidth );
			writtenLineCount += (written / lineWidth);
			// Write blank characters for the remainder of the last line, clears existing characters from screen
			FillConsoleOutputCharacterA( m_handle, ' ', lineWidth - coord.X, coord, &written );
			coord.X = 0;
			coord.Y += 1;
			writtenLineCount += 1;
		};

		const auto allTaskStrings = m_taskStatusMessages.Values();
		const Uint32 stringsToDisplay = Min( c_maxTasksToDisplay, allTaskStrings.Size() );
		for ( Uint32 i = 0; i < stringsToDisplay; ++i )
		{
			const auto& line = allTaskStrings[ i ];
			WriteLine( line );
		}
		if ( stringsToDisplay < allTaskStrings.Size() )
		{
			const auto line = String::Printf( " ... and %u more tasks.", allTaskStrings.Size() - c_maxTasksToDisplay );
			WriteLine( line );
		}
		COORD lastCoord = coord;
		for ( Uint32 i = writtenLineCount; i <= m_lastLineCount; ++i )
		{
			DWORD written = 0;
			FillConsoleOutputCharacterA( m_handle, ' ', lineWidth, coord, &written );
			coord.X = 0;
			coord.Y += 1;
		}
		// Set the cursor to the last line after the output so that it will scroll into view
		::SetConsoleCursorPosition( m_handle, lastCoord );

		m_lastLineCount = writtenLineCount;
	}
}

void CommandletTaskReporter::OpenOutputDevice( bool isRunningCommunications )
{
	m_nextFlushTimestamp = std::chrono::system_clock::now() + std::chrono::milliseconds( 50 );

	if ( isRunningCommunications )
	{
		m_outputType = OutputType::INTERACTIVE;
	}
	else
	{
		m_handle = ::GetStdHandle( STD_OUTPUT_HANDLE );
		if ( m_handle == nullptr )
		{
			// If Handle is nullptr then it means this is being run as a windowed app and does not
			// have a standard output
			m_handle = INVALID_HANDLE_VALUE;
			m_outputType = OutputType::NONE;
		}
		else
		{
			// Check that the output is actually going to console, otherwise treat it as 
			// a file redirect and use a simple direct stdout writing using the file interface
			// From remarks in https://docs.microsoft.com/en-us/windows/console/writeconsole
			DWORD mode = 0;
			if ( ::GetConsoleMode( m_handle, &mode ) )
			{
				m_outputType = OutputType::CONSOLE;

				CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
				GetConsoleScreenBufferInfo( m_handle, &consoleInfo );

				m_statusPosition = consoleInfo.dwCursorPosition;
				m_progressPosition = m_statusPosition;
				m_progressPosition.Y += 1;
			}
			else
			{
				m_outputType = OutputType::DIRECT;
				m_handle = INVALID_HANDLE_VALUE;
			}
		}
	}

	m_opened = true;
}

void CommandletTaskReporter::CloseOutputDevice()
{
	m_opened = false;

	if ( m_outputType == OutputType::CONSOLE )
	{
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		::GetConsoleScreenBufferInfo( m_handle, &consoleInfo );
		const Uint32 lineWidth = consoleInfo.dwSize.X;

		DWORD written = 0;
		COORD coord = m_progressPosition;

		for ( Uint32 i = 0; i <= m_lastLineCount; ++i )
		{
			::FillConsoleOutputCharacterA( m_handle, ' ', lineWidth, coord, &written );
			coord.Y += 1;
		}
	}
	else if ( m_outputType == OutputType::DIRECT )
	{
		fflush( stdout );
	}
}

} // commandlet
