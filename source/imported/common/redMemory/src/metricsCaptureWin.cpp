/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "metricsCaptureWin.h"

RED_DISABLE_WARNING_MSC(4091);
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

namespace red
{
namespace memory
{
namespace
{
	BOOL CALLBACK EnumerateLoadedModulesProc64( _In_ PCSTR moduleName, _In_ DWORD64 moduleBase, _In_ ULONG moduleSize, _In_opt_ PVOID userContext )
	{
		const char c_modulePrefix[] = "MOD_"; // Used to identify modules
		red::Uint32 moduleNameLength = static_cast< u32 >( Strlen( moduleName ) );
		red::Uint64 moduleSize64 = static_cast< u64 >( moduleSize );
		Serializer* serializer = static_cast< Serializer* >( userContext );

		serializer->Serialize( c_modulePrefix, sizeof( c_modulePrefix ) - 1 ); // Don't write the terminator
		serializer->Serialize( moduleSize64 );
		serializer->Serialize( moduleNameLength );
		serializer->Serialize( moduleName, moduleNameLength );
		serializer->Serialize( moduleBase );

		return TRUE;
	}
}

	void MetricsCaptureWin::WritePlatfromIdentifier( Serializer& serializer )
	{
		const char platformIdentifier[] = "WINDOWS";
		const u16 platformIdentifierSize = static_cast< u16 >( sizeof( platformIdentifier ) - 1 );
		serializer.Serialize( platformIdentifierSize );
		serializer.Serialize( platformIdentifier, platformIdentifierSize );
	}

	void MetricsCaptureWin::WriteLoadedModules( Serializer& serializer )
	{
		HANDLE thisProcess = GetCurrentProcess();
		EnumerateLoadedModules64( thisProcess, EnumerateLoadedModulesProc64, &serializer );
	}
}
}