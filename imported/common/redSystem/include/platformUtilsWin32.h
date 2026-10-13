/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef RED_PLATFORM_WINPC

REDSYSTEM_API void ShowEmbeddedWin32Dialog( const void* embrcData, HWND parent, DLGPROC lpDialogFunc, void* dlgData );

REDSYSTEM_API Bool IsShowingEmbeddedWin32Dialog();

REDSYSTEM_API Bool IsWindows7();

#endif
