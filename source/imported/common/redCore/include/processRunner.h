/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#ifdef RED_PLATFORM_WINPC

#include "../../redContainers/include/ustring/utf16String.h"

#include "redCoreApi.h"

namespace red
{
	class AbsolutePath;
}

class REDCORE_API CProcessRunner
{
private:
	Char _fullCommandLine[4096];

	HANDLE _stdoutRead;
	HANDLE _stdoutWrite;

	PROCESS_INFORMATION _processInformation;

	DWORD				_exitCode;

public:
	enum LogType
	{
		ELT_StdOut	= RED_FLAG( 0 ),
		ELT_File	= RED_FLAG( 1 ),
		ELT_String	= RED_FLAG( 2 ),
	};

public:
	CProcessRunner();
	~CProcessRunner();

	Bool Run( const red::AbsolutePath& appPath, const red::Utf16String& arguments, const red::AbsolutePath& workingDirectory, Bool createNoWindow = true );
	Bool WaitForFinish( Uint32 timeoutMS );

	Bool Terminate( Uint32 exitCode = 0, Bool closeHandles = true );

	void LogOutput( Uint32 typeFlags, const red::String& filePath, red::String* outString = nullptr );

	Uint32 GetExitCode() const;
	Uint32 GetProcessId() const;

	red::Utf16String GetFullCommandLine() const { return red::Utf16String( _fullCommandLine ); }
};

#endif