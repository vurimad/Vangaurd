/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "globalModeInfo.h"
#include "commandline.h"

namespace
{
	// True if we are closing
	static Bool s_isClosing = false;

	// True if we are running game engine
	static Bool s_isGame = false;

	static Bool s_isEditor = false;

	static Bool s_isHeadlessModeInitialized = false;
	static Bool s_isHeadlessMode = false;
}

namespace dd
{
	static red::CrashData< Bool > isClosing{ "GlobalMode", "IsClosing", s_isClosing };
	static red::CrashData< Bool > isGame{ "GlobalMode", "IsGame", s_isGame };
}

//////////////////////////////////////////////////////////////////////////
Bool IsClosingMode()
{
	return s_isClosing;
}

void SetClosingMode()
{
	s_isClosing = true;
	dd::isClosing.Set(true);
}

//////////////////////////////////////////////////////////////////////////
Bool IsGameMode()
{
	return s_isGame;
}

void SetGameMode()
{
	s_isGame = true;
	dd::isGame.Set(true);
}

Bool IsEditorMode()
{
	return s_isEditor;
}

void SetEditorMode()
{
	s_isEditor = true;
}

void InitializeHeadlessState()
{
	RED_ASSERT( !s_isHeadlessModeInitialized );

#ifdef RED_PLATFORM_LINUX
	s_isHeadlessMode = true;
#else
	const red::CommandLine& commandLine = red::CommandLine::Get();

	String engineClass;
	if( commandLine.GetFirstOption( "engine", engineClass ) )
	{
		if ( engineClass == "serverServerGameEngine" ||
			 engineClass == "HeadlessTestServerEngine" ||
			 engineClass == "HeadlessGameEngine" )
		{
			s_isHeadlessMode = true;
		}
	}
#endif

	s_isHeadlessModeInitialized = true;
}

Bool IsHeadlessMode()
{
	if ( !s_isHeadlessModeInitialized )
	{
		InitializeHeadlessState();
	}

	return s_isHeadlessMode;
}