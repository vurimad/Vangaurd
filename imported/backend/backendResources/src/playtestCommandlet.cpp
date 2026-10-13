/**
* Copyright c 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redCore/include/absolutePath.h"

#include <chrono>
#include <thread>

#if _WIN32
#include <tlhelp32.h>
#include <comdef.h>
#include <tchar.h>
#endif

class PlaytestCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( PlaytestCommandlet );

public:
	PlaytestCommandlet();
	~PlaytestCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override { return "Runs Playtesting tool"; }
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:
	bool StartProcess( const red::AbsolutePath& command );
	DWORD FindProcessId( const String& processName );
};

RTTI_BEGIN_TYPE( PlaytestCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

PlaytestCommandlet::PlaytestCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "playtest" ) )
{
}

PlaytestCommandlet::~PlaytestCommandlet()
{
}

bool PlaytestCommandlet::Execute( const red::CommandLine& options )
{
	String toolName = "MultiplayerPlaytestingTool.exe";
	red::AbsolutePath path = GFileManager->GetEngineRoot();
	path.AppendDirPath( "tools" ).AppendDirPath( "multiplayer" ).AppendFilePath( toolName );

	RED_LOG( "Tool path %s", path.AsChar() );

	if( FindProcessId( toolName ) )
	{
		return true;
	}
	else
	{
		return StartProcess( path );
	}
}

bool PlaytestCommandlet::StartProcess( const red::AbsolutePath& command )
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof( si ) );
	si.cb = sizeof( si );
	ZeroMemory( &pi, sizeof( pi ) );

	const char* path = command.AsChar();

	// Start the child process.
	if( !CreateProcessA( path,  // Module name (use command line)
						 NULL,  // Command line
						 NULL,  // Process handle not inheritable
						 NULL,  // Thread handle not inheritable
						 FALSE, // Set handle inheritance to FALSE
						 0,		// No creation flags
						 NULL,  // Use parent's environment block
						 NULL,  // Use parent's starting directory
						 &si,   // Pointer to STARTUPINFO structure
						 &pi )  // Pointer to PROCESS_INFORMATION structure
	)
	{
		const auto err = GetLastError();
		RED_LOG_ERROR( "BackgroundProcess: Failed to start process %s, last error = 0x%08X", path, err );
		return false;
	}

	// Close process and thread handles.
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	// Wait till process is ready

	RED_LOG_INFO( "BackgroundProcess: Waiting for Playtesting Tool to be ready..." );
	constexpr Uint32 maxWaitSecs = 20;
	Uint32 elapsedWaitSecs = 0;
	while ( elapsedWaitSecs < maxWaitSecs )
	{
		// Attempt to open shared named mutex created by playtesting tool

		HANDLE playtestingToolMutex = OpenMutex( 
			MUTEX_ALL_ACCESS,            // request full access
			FALSE,                       // handle not inheritable
			TEXT( "RED Multiplayer Playtesting Tool" ) ); // IMPORTANT: this mutex name must be in sync with Playtesting Tool
		if ( playtestingToolMutex )
		{
			RED_LOG_INFO( "BackgroundProcess: Playtesting Tool is ready." );
			CloseHandle( playtestingToolMutex );
			return true;
		}

		// Wait a sec

		Sleep( 1000 );
		++elapsedWaitSecs;
	}

	RED_LOG_ERROR( "BackgroundProcess: Waiting for Playtesting Tool to become ready took too much time (%u secs).", maxWaitSecs );
	return false;
}

DWORD PlaytestCommandlet::FindProcessId( const String& processName )
{
#ifdef _WIN32
	PROCESSENTRY32 processInfo;
	processInfo.dwSize = sizeof( processInfo );

	HANDLE processesSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, NULL );
	if( processesSnapshot == INVALID_HANDLE_VALUE )
		return 0;

	Process32First( processesSnapshot, &processInfo );
	if( !strcmp( processName.AsChar(), _bstr_t( processInfo.szExeFile ) ) )
	{
		CloseHandle( processesSnapshot );
		return processInfo.th32ProcessID;
	}

	while( Process32Next( processesSnapshot, &processInfo ) )
	{
		if( !strcmp( processName.AsChar(), _bstr_t( processInfo.szExeFile ) ) )
		{
			CloseHandle( processesSnapshot );
			return processInfo.th32ProcessID;
		}
	}

	CloseHandle( processesSnapshot );
#endif
	return 0;
}

void PlaytestCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  no arguments.. yet" );
}
