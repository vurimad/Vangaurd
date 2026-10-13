/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "file.h"
#include <stdio.h>

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
# include <share.h>
#endif

namespace red
{
	Bool FileOpen( FILE** handle, const AnsiChar* filename, const AnsiChar* mode )	
	{ 
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		// Allow read access so error reporter can copy the log file while it's still open
		*handle = ::_fsopen( filename, mode, _SH_DENYNO );
		return *handle != nullptr;
#elif defined( RED_PLATFORM_ORBIS )
		auto result = ::fopen_s( handle, filename, mode ); 
		return result == 0;
#elif defined( RED_PLATFORM_LINUX )
		// FIXME proper way to do this?
		*handle = fopen(filename, mode);
		auto result = !( *handle );
		return result == 0;
#else
# error Unsupported platform!
		return false;
#endif
	}
	
	Bool FileClose( FILE* handle )													
	{ 
		return ::fclose( handle ) == 0; 
	}
	
	Bool FileFlush( FILE* handle )													
	{ 
		return ::fflush( handle ) == 0; 
	}
	
	void FilePrint( FILE* handle, const AnsiChar* buffer )							
	{ 
		::fputs( buffer, handle ); 
	}
	
	void FilePrint( FILE* handle, const UniChar* buffer )							
	{
		::fputws( buffer, handle ); 
	}

	/*Int32 Internal::FilePrintF( FILE* handle, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
	{
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = vfprintf_s( handle, format, arglist );
		va_end( arglist );
		return retval;
	}

	Int32 Internal::FilePrintF( FILE* handle, STATIC_CHECK_PRINTF_MSC const UniChar* format, ... )
	{
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = vfwprintf_s( handle, format, arglist );
		va_end( arglist );
		return retval;
	}*/

}
