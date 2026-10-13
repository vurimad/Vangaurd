/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "processRunner.h"
#include "absolutePath.h"
#include "../../redIO/include/redIOPublic.h"

#ifdef RED_PLATFORM_WINPC

using red::Utf16String;

CProcessRunner::CProcessRunner()
	: _stdoutRead( nullptr )
	, _stdoutWrite( nullptr )
{
	// Setup security attributes needed by the pipe
	SECURITY_ATTRIBUTES securityAttribs; 

	// Set the bInheritHandle flag so pipe handles are inherited. 
	securityAttribs.nLength = sizeof(SECURITY_ATTRIBUTES); 
	securityAttribs.bInheritHandle = TRUE; 
	securityAttribs.lpSecurityDescriptor = nullptr; 

	// Create a pipe for the orbis-psslc.exe process's STDOUT. 
	if ( !CreatePipe( &_stdoutRead, &_stdoutWrite, &securityAttribs, RED_MEGA_BYTE( 16 ) ) )  // TEMP HACK : Increase internal pipe buffer size so functional tests do not stall while running cooking commandlets
	{
		RED_LOG_WARNING( "Core: Unable to create process STDOUT pipe" );
	}

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if ( _stdoutRead && !SetHandleInformation( _stdoutRead, HANDLE_FLAG_INHERIT, 0 ) )
	{
		RED_LOG_WARNING( "Core: Read handle process's STDOUT is inherited!" );
	}

	red::Memzero( _fullCommandLine, sizeof( _fullCommandLine ) );
}


CProcessRunner::~CProcessRunner()
{
	if ( !CloseHandle( _stdoutWrite ) )
	{
		RED_LOG_WARNING( "Core: Couldn't close process's STDOUT write handle" );
	}
	if ( !CloseHandle( _stdoutRead ) )
	{
		RED_LOG_WARNING( "Core: Couldn't close process's STDOUT read handle" );
	}
}

Bool CProcessRunner::Run( const red::AbsolutePath& appPath, const Utf16String& arguments, const red::AbsolutePath& workingDirectory, Bool createNoWindow )
{
	// Initialize process startup info
	STARTUPINFO startupInfo;
	ZeroMemory( &startupInfo, sizeof( STARTUPINFO ) );
	startupInfo.cb = sizeof( STARTUPINFO );

	// Specify redirection handles
	startupInfo.hStdError = _stdoutWrite;
	startupInfo.hStdOutput = _stdoutWrite;
	startupInfo.dwFlags |= STARTF_USESTDHANDLES;

	// Process information
	ZeroMemory( &_processInformation, sizeof( PROCESS_INFORMATION ) );

	// Combine appPath and arguments into one command line.
	if ( !arguments.Empty() )
		red::SNPrintFUnsafe( _fullCommandLine, RED_ARRAY_COUNT( _fullCommandLine ), TXT( "\"%hs\" %ls" ), appPath.AsChar(), arguments.AsChar() );
	else
		red::SNPrintFUnsafe( _fullCommandLine, RED_ARRAY_COUNT( _fullCommandLine ), TXT( "\"%hs\"" ), appPath.AsChar() );

	DWORD creationFlags = createNoWindow? CREATE_NO_WINDOW : 0;

	// Spawn shader compiler
	// Give null for app path, so it'll use the commandLine, and we get proper argv
	if ( !CreateProcessW( nullptr, _fullCommandLine, nullptr, nullptr, true, creationFlags, nullptr, workingDirectory.ToUtf16String().AsChar(), &startupInfo, &_processInformation ) )
	{
		RED_LOG_CATEGORY( red::LoggerCategory_Core ,"Core: %ls", _fullCommandLine );

		LPVOID lpMsgBuf;
		DWORD dw = GetLastError();
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, nullptr );
		RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Core, "Core: %ls", lpMsgBuf );
		LocalFree(lpMsgBuf);

		return false;
	}


	return true;
}

Bool CProcessRunner::WaitForFinish( Uint32 timeoutMS )
{
	DWORD ret = WaitForSingleObject( _processInformation.hProcess, timeoutMS );
	if ( ret )
	{
		RED_LOG( "Core: %s", _fullCommandLine );
		if ( ret == WAIT_TIMEOUT )
		{
			RED_LOG_WARNING( "Core: Operation timed out." );
		}
		else
		{
			LPVOID lpMsgBuf;
			DWORD dw = GetLastError();
			FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, nullptr );
			RED_LOG_WARNING( "Core: Error '%ls'", lpMsgBuf );
			LocalFree(lpMsgBuf);
		}
		return false;
	}

	return true;
}

Bool CProcessRunner::Terminate( Uint32 exitCode /*=0*/, Bool closeHandles /*=true*/ )
{
	if ( !TerminateProcess( _processInformation.hProcess, exitCode ) )
	{
		return false;
	}

	if ( closeHandles )
	{
		if ( !CloseHandle( _processInformation.hThread ) )
		{
			RED_LOG_WARNING( "Core: Couldn't close thread: %d", _processInformation.dwThreadId );
			return false;
		}
		if ( !CloseHandle( _processInformation.hProcess ) )
		{
			RED_LOG_WARNING( "Core: Couldn't close process: %d", _processInformation.dwProcessId );
			return false;
		}
	}	

	return true;
}

Uint32 CProcessRunner::GetExitCode() const
{
	LPDWORD exitCode = (LPDWORD)&_exitCode;
	GetExitCodeProcess( _processInformation.hProcess, exitCode );
	return _exitCode;
}

Uint32 CProcessRunner::GetProcessId() const
{
	return (Uint32)_processInformation.dwProcessId;
}

void CProcessRunner::LogOutput( Uint32 typeFlags, const String& filePath, String* outString /*=nullptr*/ )
{
	// check flags and parameters
	if ( typeFlags & LogType::ELT_File && filePath.Empty() )
	{
		// don't log to file if no fileName provided
		typeFlags &= ~LogType::ELT_File;
	}
	if ( typeFlags & LogType::ELT_String && outString == nullptr )
	{
		// don't log to string if no String object provided
		typeFlags &= ~LogType::ELT_String;
	}

	if ( ( typeFlags & LogType::ELT_File ) == 0 &&
		( typeFlags & LogType::ELT_String ) == 0 &&
		( typeFlags & LogType::ELT_StdOut ) == 0
		)
	{
		// no log at all, early exit
		return;
	}

	// Read the error report
	DWORD numBytesRead;
	Bool success;
	const Uint32 s_BufferSize = 8192;
	AnsiChar logBuf[ s_BufferSize + 1 ];

	Bool logToFile = !filePath.Empty();
	io::NativeFileHandle errorFileHandle;
	Uint32 dummyOut;

	if ( logToFile )
	{
		if ( !errorFileHandle.Open( filePath.AsChar(), io::eOpenFlag_Write | io::eOpenFlag_Truncate | io::eOpenFlag_Create ) )
		{
			RED_LOG_WARNING( "Core: Unable to create error file '%hs', logging to stdout", filePath.AsChar() );
			logToFile = false;
		}
	}

	while ( true )
	{
		DWORD bytesAvailable = 0;
		// We don't have a named pipe, but this function is usable for read end of anonymous pipe.
		if ( !PeekNamedPipe( _stdoutRead, nullptr, 0, nullptr, &bytesAvailable, nullptr ) )
		{
			RED_LOG_WARNING( "Core: PeekNamedPipe failed. Can't dump process output." );
			break;
		}
		// If there's nothing to read, we're done here.
		if ( bytesAvailable == 0 )
		{
			break;
		}

		DWORD bytesToRead = Min( s_BufferSize, bytesAvailable );

		success = ( 0 != ReadFile( _stdoutRead, logBuf, bytesToRead, &numBytesRead, NULL ) );
		if ( !success || numBytesRead == 0 )
		{
			break;
		}
		logBuf[bytesToRead] = '\0';

		if ( typeFlags & LogType::ELT_File )
		{
			RED_VERIFY( errorFileHandle.Write( logBuf, bytesToRead, dummyOut ) );
		}		
		if ( typeFlags & LogType::ELT_String )
		{
			*outString += logBuf;
		}
		if ( typeFlags & LogType::ELT_StdOut )
		{
			RED_LOG( "Process: %s", logBuf );
		}
	}

	if ( logToFile )
	{
		RED_VERIFY( errorFileHandle.Close() );
	}
}
#else
RED_NO_EMPTY_FILE();
#endif