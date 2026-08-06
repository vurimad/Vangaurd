/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "processTime.h"

namespace red
{

namespace prv
{

#ifdef RED_PLATFORM_WINPC

// 
// Parts of this taken and adapted from https://github.com/HowardHinnant/date/wiki/Examples-and-Recipes#fILETIME
// A time point is just a duration from the start of the epoch, so we can kind of treat the
// durations as time points for our purposes here
//

// FILETIME has 100ns intervals
using FileTimeDuration = std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>;

constexpr FileTimeDuration DurationFromFileTime( const FILETIME& fileTime )
{
	return FileTimeDuration{ (static_cast< int64_t >( fileTime.dwHighDateTime ) << 32) | fileTime.dwLowDateTime };
}

FileTimeDuration GetProcessStartTime()
{
	FILETIME creationTime;
	FILETIME nullTime;
	if ( ::GetProcessTimes( ::GetCurrentProcess(), &creationTime, &nullTime, &nullTime, &nullTime ) != 0 )
	{
		return DurationFromFileTime( creationTime );
	}
	return FileTimeDuration(0);
}

FileTimeDuration GetProcessCurrentTime()
{
	FILETIME currentTime;
	::GetSystemTimeAsFileTime( &currentTime );
	return DurationFromFileTime( currentTime );
}

std::chrono::milliseconds GetTimeSinceProcessStart()
{
	static FileTimeDuration startTime = GetProcessStartTime();
	FileTimeDuration currentTime = GetProcessCurrentTime();
	return std::chrono::duration_cast<std::chrono::milliseconds>( currentTime - startTime );
}

#endif

} // prv

std::chrono::milliseconds GetTimeSinceProcessStart()
{
#ifdef RED_PLATFORM_WINPC
	return prv::GetTimeSinceProcessStart();
#else
	RED_FATAL( "Not implemented for this platform!" );
	return std::chrono::milliseconds(0);
#endif
}

} // red
