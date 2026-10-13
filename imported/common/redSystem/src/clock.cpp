/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "clock.h"

#if defined( RED_PLATFORM_ORBIS )
#	include "rtc.h"
#	pragma comment ( lib, "libSceRtc_stub_weak.a" )
#endif

#include <ctime>

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
namespace
{
	constexpr Int32 c_windowsTick = 10000000;
	constexpr LONGLONG c_secondsToUnixEpoch = 116444736000000000LL;

	Uint64 UnixTimeToFileTimestamp( time_t t )
	{
		return Int32x32To64( t, c_windowsTick ) + c_secondsToUnixEpoch;
	}

	time_t FileTimeToUnixTimestamp( FILETIME ft )
	{
		LARGE_INTEGER li;
		li.LowPart = ft.dwLowDateTime;
		li.HighPart = ft.dwHighDateTime;

		return ( li.QuadPart - c_secondsToUnixEpoch ) / c_windowsTick;
	}
}
#endif

namespace red
{

void DateTime::SetRaw( Uint64 dateTime )
{
	m_date = static_cast< Uint32 >( dateTime >> 32 ); m_time = static_cast< Uint32 >( dateTime );
}

DateTime DateTime::FromFileTime( Uint64 fileTimestamp )
{
	// fileTimestamp is in 100 nanosecond intervals since 00:00:00 1 January 1601 UTC
	// Same as the Windows FILETIME

	DateTime result;
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	FILETIME ft;
	ft.dwHighDateTime = static_cast<Uint32>(fileTimestamp >> 32);
	ft.dwLowDateTime = static_cast<Uint32>(fileTimestamp);

	SYSTEMTIME lt;
	if ( ::FileTimeToSystemTime( &ft, &lt ) != 0 )
	{
		result.SetYear( static_cast< Uint32 >( lt.wYear ) );
		result.SetMonth( static_cast< Uint32 >( lt.wMonth ) - 1 );
		result.SetDay( static_cast< Uint32 >( lt.wDay ) - 1 );
		result.SetHour( static_cast< Uint32 >( lt.wHour ) );
		result.SetMinute( static_cast< Uint32 >( lt.wMinute ) );
		result.SetSecond( static_cast< Uint32 >( lt.wSecond ) );
		result.SetMilliSeconds( static_cast< Uint32 >( lt.wMilliseconds ) );
	}
#elif defined( RED_PLATFORM_ORBIS )
	SceRtcDateTime st;
	if ( sceRtcSetWin32FileTime( &st, fileTimestamp ) == SCE_OK )
	{
		result.SetYear( static_cast< Uint32 >( st.year ) );
		result.SetMonth( static_cast< Uint32 >( st.month ) - 1 );
		result.SetDay( static_cast< Uint32 >( st.day ) - 1 );
		result.SetHour( static_cast< Uint32 >( st.hour ) );
		result.SetMinute( static_cast< Uint32 >( st.minute ) );
		result.SetSecond( static_cast< Uint32 >( st.second ) );
		result.SetMilliSeconds( static_cast< Uint32 >( st.microsecond ) / 1000 );
	}
#endif

	return result;
}

DateTime DateTime::FromFileTimeToLocal(Uint64 fileTimestamp)
{
	DateTime result;
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	FILETIME ft, lft;
	ft.dwHighDateTime = static_cast<Uint32>(fileTimestamp >> 32);
	ft.dwLowDateTime = static_cast<Uint32>(fileTimestamp);

	SYSTEMTIME lt;
	if ( ::FileTimeToLocalFileTime( &ft, &lft ) && ::FileTimeToSystemTime( &lft, &lt ) != 0 )
	{
		result.SetYear( static_cast< Uint32 >( lt.wYear ) );
		result.SetMonth( static_cast< Uint32 >( lt.wMonth ) - 1 );
		result.SetDay( static_cast< Uint32 >( lt.wDay ) - 1 );
		result.SetHour( static_cast< Uint32 >( lt.wHour ) );
		result.SetMinute( static_cast< Uint32 >( lt.wMinute ) );
		result.SetSecond( static_cast< Uint32 >( lt.wSecond ) );
		result.SetMilliSeconds( static_cast< Uint32 >( lt.wMilliseconds ) );
	}
#elif defined( RED_PLATFORM_ORBIS )
	SceRtcDateTime st;
	SceRtcTick utc, lt;
	if ( sceRtcSetWin32FileTime( &st, fileTimestamp ) == SCE_OK && sceRtcConvertDateTimeToTick(&st, &utc) == SCE_OK && sceRtcConvertUtcToLocalTime(&utc, &lt) == SCE_OK && sceRtcConvertTickToDateTime(&lt, &st) == SCE_OK )
	{
		result.SetYear( static_cast< Uint32 >( st.year ) );
		result.SetMonth( static_cast< Uint32 >( st.month ) - 1 );
		result.SetDay( static_cast< Uint32 >( st.day ) - 1 );
		result.SetHour( static_cast< Uint32 >( st.hour ) );
		result.SetMinute( static_cast< Uint32 >( st.minute ) );
		result.SetSecond( static_cast< Uint32 >( st.second ) );
		result.SetMilliSeconds( static_cast< Uint32 >( st.microsecond ) / 1000 );
	}
#endif

	return result;
}

DateTime DateTime::FromUnixTimestamp( time_t timestamp )
{
	DateTime result;

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	result = FromFileTime( UnixTimeToFileTimestamp( timestamp ) );
#elif defined( RED_PLATFORM_ORBIS )
	SceRtcDateTime lt;
	const Int32 errorCode = sceRtcSetTime_t( &lt, timestamp );
	if ( errorCode < SCE_OK )
	{
		RED_LOG_ERROR( "DateTime::FromUnixTimestamp: Failed to convert timestamp(%llu) to date time due to %ld", timestamp, errorCode );
		return result;
	}

	result.SetYear( static_cast< Uint32 >( lt.year ) );
	result.SetMonth( static_cast< Uint32 >( lt.month ) - 1 );
	result.SetDay( static_cast< Uint32 >( lt.day ) - 1 );
	result.SetHour( static_cast< Uint32 >( lt.hour ) );
	result.SetMinute( static_cast< Uint32 >( lt.minute ) );
	result.SetSecond( static_cast< Uint32 >( lt.second ) );
	result.SetMilliSeconds( static_cast< Uint32 >( lt.microsecond ) / 1000 );
#endif

	return result;
}

time_t DateTime::GetUnixTimestamp() const
{
	time_t timestamp = 0;
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	SYSTEMTIME lt;
	lt.wYear = GetYear();
	lt.wMonth = GetMonth() + 1;
	lt.wDay = GetDay() + 1;
	lt.wHour = GetHour();
	lt.wMinute = GetMinute();
	lt.wSecond = GetSecond();
	lt.wMilliseconds = GetMilliSeconds();

	FILETIME ft;
	if ( !::SystemTimeToFileTime( &lt, &ft ) )
	{
		RED_LOG_ERROR( "DateTime::GetUnixTimestamp: Failed to convert date time to UNIX timestamp due to %ld", GetLastError() );
	}

	timestamp = FileTimeToUnixTimestamp( ft );

#elif defined( RED_PLATFORM_ORBIS )

	SceRtcDateTime lt;
	lt.year = GetYear();
	lt.month = GetMonth() + 1;
	lt.day = GetDay() + 1;
	lt.hour = GetHour();
	lt.minute = GetMinute();
	lt.second = GetSecond();
	lt.microsecond = GetMilliSeconds() * 1000;

	const Int32 errorCode = sceRtcGetTime_t( &lt, &timestamp );
	if ( errorCode < SCE_OK )
	{
		RED_LOG_ERROR( "DateTime::GetUnixTimestamp: Failed to convert date time to UNIX timestamp due to %ld", errorCode );
	}

#endif

	return timestamp;
}

Clock::Clock()
{

}

Clock::~Clock()
{

}

void Clock::GetLocalTime( DateTime& dt ) const
{
	// If this variable is being reused or is non-zero then weirdness will ensue!
	dt.Clear();

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	SYSTEMTIME lt;

#	if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 )
	::GetLocalTime( &lt );
#	elif defined( RED_PLATFORM_DURANGO )
	FILETIME sft, lft;
	::GetSystemTimeAsFileTime( &sft );
	::FileTimeToLocalFileTime( &sft, &lft );
	::FileTimeToSystemTime( &lft, &lt );
#	endif

	dt.SetYear( static_cast< Uint32 >( lt.wYear ) );
	dt.SetMonth( static_cast< Uint32 >( lt.wMonth ) - 1 );
	dt.SetDay( static_cast< Uint32 >( lt.wDay ) - 1 );
	dt.SetHour( static_cast< Uint32 >( lt.wHour ) );
	dt.SetMinute( static_cast< Uint32 >( lt.wMinute ) );
	dt.SetSecond( static_cast< Uint32 >( lt.wSecond ) );
	dt.SetMilliSeconds( static_cast< Uint32 >( lt.wMilliseconds ) );
#elif defined( RED_PLATFORM_ORBIS )
	SceRtcDateTime lt;
	sceRtcGetCurrentClockLocalTime( &lt );

	dt.SetYear( static_cast< Uint32 >( lt.year ) );
	dt.SetMonth( static_cast< Uint32 >( lt.month ) - 1 );
	dt.SetDay( static_cast< Uint32 >( lt.day ) - 1 );
	dt.SetHour( static_cast< Uint32 >( lt.hour ) );
	dt.SetMinute( static_cast< Uint32 >( lt.minute ) );
	dt.SetSecond( static_cast< Uint32 >( lt.second ) );
	dt.SetMilliSeconds( static_cast< Uint32 >( lt.microsecond ) / 1000 );
#else
	RED_UNUSED( dt );
#endif
}

void Clock::GetUTCTime( DateTime& dt ) const
{
	// If this variable is being reused or is non-zero then weirdness will ensue!
	dt.Clear();

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	SYSTEMTIME st;
	::GetSystemTime( &st );

	dt.SetYear( static_cast< Uint32 >( st.wYear ) );
	dt.SetMonth( static_cast< Uint32 >( st.wMonth ) - 1 );
	dt.SetDay( static_cast< Uint32 >( st.wDay ) - 1 );
	dt.SetHour( static_cast< Uint32 >( st.wHour ) );
	dt.SetMinute( static_cast< Uint32 >( st.wMinute ) );
	dt.SetSecond( static_cast< Uint32 >( st.wSecond ) );
	dt.SetMilliSeconds( static_cast< Uint32 >( st.wMilliseconds ) );
#elif defined( RED_PLATFORM_ORBIS )
	SceRtcDateTime st;
	sceRtcGetCurrentClock( &st, 0 );

	dt.SetYear( static_cast< Uint32 >( st.year ) );
	dt.SetMonth( static_cast< Uint32 >( st.month ) - 1 );
	dt.SetDay( static_cast< Uint32 >( st.day ) - 1 );
	dt.SetHour( static_cast< Uint32 >( st.hour ) );
	dt.SetMinute( static_cast< Uint32 >( st.minute ) );
	dt.SetSecond( static_cast< Uint32 >( st.second ) );
	dt.SetMilliSeconds( static_cast< Uint32 >( st.microsecond ) / 1000 );
#else
	RED_UNUSED( dt );
#endif
}

} // namespace red
