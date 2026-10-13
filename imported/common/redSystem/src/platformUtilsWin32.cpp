/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

RED_NO_EMPTY_FILE()

#ifdef RED_PLATFORM_WINPC

#include "platformUtilsWin32.h"
#include "utility.h"

//////////////////////////////////////////////////////////////////////////
// Definition, not in any official header file but stable since Win95 apparently.
// Can be used in place of DIALOGTEMPLATE in functions.
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms645398(v=vs.85).aspx

#pragma pack(push, 1)
typedef struct
{
	WORD dlgVer;
	WORD signature;
	DWORD helpID;
	DWORD exStyle;
	DWORD style;
	WORD cDlgItems;
	short x;
	short y;
	short cx;
	short cy;
} DLGTEMPLATEEX;
#pragma pack(pop)

//////////////////////////////////////////////////////////////////////////

static Bool g_isShowingEmbeddedWin32Dialog = false;

void ShowEmbeddedWin32Dialog( const void* embrcData, HWND parent, DLGPROC lpDialogFunc, void* dlgData )
{
	// Resource file format
	// https://msdn.microsoft.com/en-us/library/windows/desktop/ms648007(v=vs.85).aspx
	// Basic header is 32 bytes, and first record is empty
	// #tbd: parsing/basic validation
	const Uint64 dlgResourceOffset = 64;
	const DLGTEMPLATEEX* dlgTemplate =  reinterpret_cast< const DLGTEMPLATEEX* >( reinterpret_cast< const Uint8* >( embrcData ) + dlgResourceOffset );

	red::ScopedFlag<Bool> scopedFlag( g_isShowingEmbeddedWin32Dialog = true, false );
	(void)DialogBoxIndirectParamA( GetModuleHandleA( nullptr ), reinterpret_cast<const DLGTEMPLATE*>( dlgTemplate ), parent, lpDialogFunc, reinterpret_cast< LPARAM >( dlgData ) );

}

Bool IsShowingEmbeddedWin32Dialog()
{
	return g_isShowingEmbeddedWin32Dialog;
}

Bool IsWindows7()
{
	OSVERSIONINFOEX osVersion;
	DWORDLONG dwlConditionMask = 0;

	ZeroMemory( &osVersion, sizeof( OSVERSIONINFOEX ) );
	osVersion.dwOSVersionInfoSize = sizeof( OSVERSIONINFOEX );
	osVersion.dwMajorVersion = 6;
	osVersion.dwMinorVersion = 1;

	int op = VER_EQUAL;
	VER_SET_CONDITION( dwlConditionMask, VER_MAJORVERSION, op );
	VER_SET_CONDITION( dwlConditionMask, VER_MINORVERSION, op );

	return VerifyVersionInfo( &osVersion, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask );
}

#endif
