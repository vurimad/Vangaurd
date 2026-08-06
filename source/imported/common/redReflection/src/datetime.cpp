/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "datetime.h"
#include "rttiSimpleType.h"

#if defined( RED_PLATFORM_ORBIS )
#	include <rtc.h>
#elif defined( RED_PLATFORM_LINUX )
#	include <time.h>
#endif
#include "../../redFileSystem/include/file.h"

RTTI_DEFINE_SIMPLE_TYPE( CDateTime );

CDateTime CDateTime::INVALID;

void CDateTime::ImportFromOldFileTimeFormat( Uint64 winStyleTimestamp )
{
	// FILETIME on Windows:
	// Contains a 64-bit value representing the number of 100-nanosecond intervals since January 1, 1601 (UTC).
	
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
	FILETIME windowsFileFormat;
	windowsFileFormat.dwHighDateTime	= static_cast< DWORD >( winStyleTimestamp >> 32 );
	windowsFileFormat.dwLowDateTime		= static_cast< DWORD >( winStyleTimestamp );

	SYSTEMTIME windowsDateTimeFormat;
	red::Memzero( &windowsDateTimeFormat, sizeof( SYSTEMTIME ) );

	FileTimeToSystemTime( &windowsFileFormat, &windowsDateTimeFormat );

	SetYear			( static_cast< Uint32 >( windowsDateTimeFormat.wYear ) );
	SetMonth		( static_cast< Uint32 >( windowsDateTimeFormat.wMonth ) - 1 );
	SetDay			( static_cast< Uint32 >( windowsDateTimeFormat.wDay ) - 1 );
	SetHour			( static_cast< Uint32 >( windowsDateTimeFormat.wHour ) );
	SetMinute		( static_cast< Uint32 >( windowsDateTimeFormat.wMinute ) );
	SetSecond		( static_cast< Uint32 >( windowsDateTimeFormat.wSecond ) );
	SetMilliSeconds	( static_cast< Uint32 >( windowsDateTimeFormat.wMilliseconds ) );

#elif defined( RED_PLATFORM_ORBIS )

	SceRtcDateTime orbisDateTime;
	if( sceRtcSetWin32FileTime( &orbisDateTime, winStyleTimestamp ) == SCE_OK )
	{
		SetYear			( static_cast< Uint32 >( orbisDateTime.year ) );
		SetMonth		( static_cast< Uint32 >( orbisDateTime.month ) - 1 );
		SetDay			( static_cast< Uint32 >( orbisDateTime.day ) - 1 );
		SetHour			( static_cast< Uint32 >( orbisDateTime.hour ) );
		SetMinute		( static_cast< Uint32 >( orbisDateTime.minute ) );
		SetSecond		( static_cast< Uint32 >( orbisDateTime.second ) );
		SetMilliSeconds	( static_cast< Uint32 >( orbisDateTime.microsecond ) / 100 );
	}

#elif defined( RED_PLATFORM_LINUX )

	// convert windows time to unix first
	time_t unixTime = static_cast< time_t >( winStyleTimestamp / 10000000 - 11644473600LL );

	struct tm linuxFileTime = { 0 };
	if ( gmtime_r( &unixTime, &linuxFileTime ) != NULL )
	{
		SetYear			( static_cast< Uint32 >( linuxFileTime.tm_year ) + 1900 ); // 1900+n
		SetMonth		( static_cast< Uint32 >( linuxFileTime.tm_mon ) ); // 0-11
		SetDay			( static_cast< Uint32 >( linuxFileTime.tm_mday ) - 1 ); // 1-31
		SetHour			( static_cast< Uint32 >( linuxFileTime.tm_hour ) );
		SetMinute		( static_cast< Uint32 >( linuxFileTime.tm_min ) );
		SetSecond		( static_cast< Uint32 >( linuxFileTime.tm_sec ) );
		
		Uint64 ms = 10000; // amount of 100-nanoseconds in a ms
		Uint64 second = ms * 1000; // 1 second
		Uint64 milliseconds = ( winStyleTimestamp - ( ( winStyleTimestamp / second ) * second ) ) * ms;
		SetMilliSeconds	( static_cast< Uint32 >( milliseconds ) );
	}
#else
#	error Unsupported platform in timestamp conversion
#endif
}

void CDateTime::Serialize( IFile& file )
{
	file << m_date;
	file << m_time;
}

//////////////////////////////////////////////////////////////////////////
// Type formatters
namespace red
{
	Bool CheckFormatString(const CDateTime& val, const char* formatToCheck)
	{
		if (formatToCheck)
		{
			// old c-style format
			const char* checkFormat = std::strchr(formatToCheck, '%');
			if (checkFormat)
			{
				return true;
			}
		}
		return false;
	}

	// custom DateTime ToBuffer()
	// supported:
	// day/month/year hour:minute:second (default)
	// todo:
	// only one format is supported right now
	Bool ToBuffer(char* buffer, const Uint32 bufferLen, const CDateTime& val, Int32& written, const char* formatString)
	{
		if (buffer && bufferLen)
		{
			if (formatString && (red::CheckFormatString(val, formatString) || red::prv::CheckNewFormatString(val, formatString)))
			{
				const Uint32 dateFront = val.GetDay() + 1;
				const Uint32 dateMiddle = val.GetMonth() + 1;
				const Uint32 dateRear = val.GetYear();

				// day/month/year hour:minute:second
				written += red::SNPrintFUnsafe(buffer, bufferLen, formatString, dateFront, dateMiddle, dateRear, val.GetHour(), val.GetMinute(), val.GetSecond());
				return true;
			}
		}
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////
// Conversion from/to string
const Bool ToString(red::String& outTxt, const CDateTime& val, const char* customFormat)
{
	// init
	AnsiChar formattedBuffer[512];
	Int32 written = 0;

	if (red::ToBuffer(formattedBuffer, sizeof(formattedBuffer), val, written, customFormat ? customFormat : red::GetFormatString(val)))
	{
		outTxt.Set(formattedBuffer, written);
		return true;
	}
	return false;
}

const Bool FromString(const String& txt, CDateTime& outVal)
{
	return false;
}