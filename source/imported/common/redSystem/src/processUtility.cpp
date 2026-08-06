#include "build.h"
#include "processUtility.h"
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
#include <unistd.h>
#elif defined( RED_PLATFORM_WINPC )
#include <Shlwapi.h>
#include <psapi.h>
#include <winternl.h>

#pragma comment( lib, "Shlwapi.lib" )
#pragma comment( lib, "Psapi.lib" ) 
#pragma comment( lib, "Ntdll.lib" )
#endif

Uint32 GetProcessId()
{
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
	return static_cast< Uint32 >( getpid() );
#elif defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	return static_cast< Uint32 >( ::GetProcessId( GetCurrentProcess() ) );
#endif
}

#if defined( RED_PLATFORM_WINPC )
Bool GetProcessName( AnsiChar* processName, Uint32 processNameLength )
{
	AnsiChar processPath[ MAX_PATH ] = { '\0' };
	::GetModuleFileNameA( NULL, processPath, MAX_PATH );
	const AnsiChar* processNameStr = ::PathFindFileNameA( processPath );

	const Uint32 processNameStrLength = static_cast< Uint32 >( red::Strlen( processNameStr ) );
	if ( processNameStrLength > processNameLength )
	{
		return false;
	}

	red::Strcpy( processName, processNameStr, processNameLength );
	return true;
}

REDSYSTEM_API Bool GetProcessName( HANDLE processHandle, AnsiChar* processName, Uint32 processNameLength )
{
	DWORD cbNeeded;
	HMODULE hExeModule;
	if( EnumProcessModules( processHandle, &hExeModule, sizeof( hExeModule ), &cbNeeded ) )
	{
		return GetModuleBaseNameA( processHandle, hExeModule, processName, processNameLength ) != 0;
	}

	return false;
}

REDSYSTEM_API Bool GetProcessCommandLine( HANDLE processHandle, UniChar* commandLineContent, Uint32 commandLineContentLength )
{
	/** Based on:
		- https://stackoverflow.com/questions/6520428/how-to-query-a-running-process-for-its-parameters-list-windows-c
		- https://stackoverflow.com/questions/5454667/how-to-get-the-process-environment-block-peb-from-extern-process
	*/

	PVOID rtlUserProcParamsAddress = nullptr;
	UNICODE_STRING commandLine = {};
	PROCESS_BASIC_INFORMATION processInformation = {};
	ULONG returnedLength = 0;

	// Get PEB (process environment block) pointer.
	NtQueryInformationProcess( processHandle, ProcessBasicInformation, &processInformation, sizeof( processInformation ), &returnedLength );
	if( !returnedLength )
	{
		return false;
	}

	// Get the address of ProcessParameters.
	if( !ReadProcessMemory( processHandle,
		&( processInformation.PebBaseAddress->ProcessParameters ),
		&rtlUserProcParamsAddress,
		sizeof( PVOID ), NULL ) )
	{
		return false;
	}

	// Read the CommandLine UNICODE_STRING structure.
	if( !ReadProcessMemory( processHandle,
		&( ( ( _RTL_USER_PROCESS_PARAMETERS* )rtlUserProcParamsAddress )->CommandLine ),
		&commandLine, sizeof( commandLine ), NULL ) )
	{
		return false;
	}

	// Read the command line.
	if( !ReadProcessMemory( processHandle, commandLine.Buffer,
		commandLineContent, Min( ( Uint32 )commandLine.Length, commandLineContentLength * 2 ), NULL ) )
	{
		return false;
	}

	return true;
}

#endif