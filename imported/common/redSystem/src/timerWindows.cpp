/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/
#include "build.h"
#include "timerWindows.h"

// This wants to be removed as soon as we have a real solution in place
#ifdef CONTINUOUS_SCREENSHOT_HACK

namespace red
{

void Timer::EnableGameTimeHack()
{
	if ( !IsEnabledTimeHack )
	{
		TimeHackBaseTime =  GetSeconds();
		IsEnabledTimeHack = true;
	}
}

void Timer::DisableGameTimeHack()
{
	if ( IsEnabledTimeHack )
	{
		IsEnabledTimeHack = false;
		TimeHackCorrection = GetSeconds() - TimeHackBaseTime;
	}
}

void Timer::NextFrameGameTimeHack()
{
	if ( IsEnabledTimeHack )
	{
		TimeHackBaseTime += 1.0 / ScreenshotFramerate;
	}
}

void Timer::SetScreenshotFramerate( Double framerate )
{
	ScreenshotFramerate = framerate;
}

} // namespace red

#endif
