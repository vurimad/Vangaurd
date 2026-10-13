/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

REDSYSTEM_API Uint32 GetProcessId();

#if defined( RED_PLATFORM_WINPC )
	REDSYSTEM_API Bool GetProcessName( AnsiChar* processName, Uint32 processNameLength );
	template < size_t N >
	RED_INLINE Bool GetProcessName( AnsiChar( &processName )[ N ] ) { return GetProcessName( processName, N ); }

	REDSYSTEM_API Bool GetProcessName( HANDLE processHandle, AnsiChar* processName, Uint32 processNameLength );
	template < size_t N >
	RED_INLINE Bool GetProcessName( HANDLE processHandle, AnsiChar( &processName )[ N ] ) { return GetProcessName( processHandle, processName, N ); }

	REDSYSTEM_API Bool GetProcessCommandLine( HANDLE processHandle, UniChar* commandLineContent, Uint32 commandLineContentLength );
	template < size_t N >
	RED_INLINE Bool GetProcessCommandLine( HANDLE processHandle, UniChar( &commandLineContent )[ N ] ) { return GetProcessCommandLine( processHandle, commandLineContent, N ); }

#endif