/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "crt.h"
#include <locale>

namespace red
{

#ifdef RED_COMPILER_CLANG 

	////////////////////////////////////////////////////////////////////////////////////////
	// !!!! NOTE !!!!
	// This function will only work as intended for strings containing characters compatible with the default locale
	Int32 StrcmpNC( const UniChar* a, const UniChar* b )
	{
		RED_SYSTEM_ASSERT( a != nullptr, "Cannot compare null strings. This will probably crash" );
		RED_SYSTEM_ASSERT( b != nullptr, "Cannot compare null strings. This will probably crash" );

		std::locale thisLocale;		// This is copied from the global locale when default constructor used
		MemSize aLength = red::Strlen( a );
		MemSize bLength = red::Strlen( b );

		// Note! This assumes the strings are null terminated. If they are not, this will overflow
		MemSize searchLength = aLength > bLength ? bLength : aLength;
		MemSize strCounter = 0;
		while( strCounter <= searchLength )		// We actually test the first null character from either string
		{
			UniChar aLowercase = std::tolower( a[strCounter], thisLocale );
			UniChar bLowercase = std::tolower( b[strCounter], thisLocale );

			if( aLowercase > bLowercase )
				return 1;
			else if( aLowercase < bLowercase )
				return -1;

			++strCounter;
		}

		return 0;
	}

	////////////////////////////////////////////////////////////////////////////////////////
	// !!!! NOTE !!!!
	// This function will only work as intended for strings containing characters compatible with the default locale
	Int32 StrcmpNC( const UniChar* a, const UniChar* b, size_t max )
	{
		RED_SYSTEM_ASSERT( a != nullptr, "Cannot compare null strings. This will probably crash" );
		RED_SYSTEM_ASSERT( b != nullptr, "Cannot compare null strings. This will probably crash" );

		std::locale thisLocale;		// This is copied from the global locale when default constructor used
		MemSize aLength = red::Strlen( a ) + 1;
		MemSize bLength = red::Strlen( b ) + 1;

		MemSize searchLength = aLength > bLength ? bLength : aLength;
		searchLength = searchLength > max ? max : searchLength;

		MemSize strCounter = 0;
		while( strCounter < searchLength )		// We actually test the first null character from either string
		{
			UniChar aLowercase = std::tolower( a[strCounter], thisLocale );
			UniChar bLowercase = std::tolower( b[strCounter], thisLocale );

			if( aLowercase > bLowercase )
				return 1;
			else if( aLowercase < bLowercase )
				return -1;

			++strCounter;
		}

		return 0;
	}

#else

	Int32 StrcmpNC( const UniChar* a, const UniChar* b )						
	{ 
		return ::_wcsicmp( a, b ); 
	}

	Int32 StrcmpNC( const UniChar* a, const UniChar* b, size_t max )
	{
		return ::_wcsnicmp( a, b, max );
	}

#endif

	size_t WideCharToStdChar( AnsiChar* dest, const UniChar* source, size_t destSize, StringConversionType conversionType /* = ConvertAll */ )
	{
		size_t charsWritten = 0;
		const MemSize sourceLength = red::Strlen( source );
		// Truncate: the number of converted symbols needs to be less than destSize (the last symbol is reserved for delimiter)
		const size_t toConvert = conversionType == StringConversionType::Truncate ? Min( sourceLength, destSize - 1 ) : sourceLength;
#ifdef RED_PLATFORM_LINUX
		size_t errval = ::wcstombs( dest, source, destSize );
#else
		const errno_t errval = wcstombs_s( &charsWritten, dest, destSize, source, toConvert );
#endif
		RED_UNUSED( errval );

		// The null terminator is not included in the length
		return charsWritten;
	}

	size_t StdCharToWideChar( UniChar* dest, const AnsiChar* source, size_t destSize, StringConversionType conversionType /* = ConvertAll */ )
	{
		size_t charsWritten = 0;
		const MemSize sourceLength = red::Strlen( source );
		// Truncate: the number of converted symbols needs to be less than destSize (the last symbol is reserved for delimiter)
		const size_t toConvert = conversionType == StringConversionType::Truncate ? Min( sourceLength, destSize - 1 ) : sourceLength;
#ifdef RED_PLATFORM_LINUX
		size_t errval = ::mbstowcs( dest, source, destSize );
#else
		const errno_t errval = mbstowcs_s( &charsWritten, dest, destSize, source, toConvert );
#endif
		RED_UNUSED( errval );

		// The null terminator is not included in the length
		return charsWritten;
	}

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	// On Windows the filesystem uses UTF-16 but internally we use UTF-8 for encoded paths
	// MultiByte = UTF-8
	// Wide = UTF-16

	int FileSystemStringCharCount( const char* src )
	{
		return ::MultiByteToWideChar( CP_UTF8, 0, src,  -1, nullptr, 0 );
	}
	
	bool EngineStringToFileSystemString( const char* src, wchar_t* dst, size_t dstSize )
	{
		RED_ASSERT( dstSize >= FileSystemStringCharCount( src ), "Insufficient buffer size allocated for string conversion" );

		bool success = 0 != ::MultiByteToWideChar( CP_UTF8, 0, src, -1, dst,  static_cast<int>( dstSize ) );
		if ( !success )
		{
			DWORD error = ::GetLastError();
			RED_LOG_ERROR( "Failed to convert Engine string '%hs' (UTF-8) to FileSystem string (UTF-16) reason %u", src, error );
		}
		return success;
	}
	
	int EngineStringCharCount( const wchar_t* src )
	{
		return ::WideCharToMultiByte( CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr );
	}

	bool FileSystemStringToEngineString( const wchar_t* src, char* dst, size_t dstSize )
	{
		RED_ASSERT( dstSize >= EngineStringCharCount( src ), "Insufficient buffer size allocated for string conversion" );

		bool success = 0 != ::WideCharToMultiByte( CP_UTF8, 0, src, -1, dst, static_cast<int>( dstSize ), nullptr, nullptr );
		if ( !success )
		{
			DWORD error = ::GetLastError();
			RED_LOG_ERROR( "Failed to convert FileSystem string '%ls' (UTF-16) to Engine string (UTF-8) reason %u", src, error );
		}
		return success;
	}
#endif
	
	namespace impl
	{
		template< typename TChar > RED_FORCE_INLINE
		static void ReplaceCharT( TChar* s, size_t size, TChar from, TChar to )
		{
			if ( !s )
			{
				return;
			}
			for ( Uint32 i = 0; i < size; ++i )
			{
				if ( !s[i] )
				{
					break;
				}
				else if ( s[i] == from )
				{
					s[i] = to;
				}
			}
		}

		template< typename TChar > RED_FORCE_INLINE
		static void StrToUpperT( TChar* ptr, Uint32 size )
		{
			for ( Uint32 i = 0; i < size; ++i, ++ptr )
			{
				if ( *ptr >= 'a' && *ptr <= 'z' )
				{
					*ptr += static_cast< TChar >('A' - 'a');
				}
			}
		}

		template< typename TChar > RED_FORCE_INLINE
		static void StrToLowerT( TChar* ptr, Uint32 size )
		{
			for ( Uint32 i = 0; i < size; ++i, ++ptr )
			{
				if ( *ptr >= 'A' && *ptr <= 'Z' )
				{
					*ptr += static_cast< TChar >('a' - 'A');
				}
			}
		}

		template< typename TFromChar, typename TToChar > RED_FORCE_INLINE
		static size_t FomXCharToYCharT_NoConv( TToChar* dst, const TFromChar* src, size_t dstSize )
		{
			if ( dstSize == 0 )
			{
				return 0;
			}
			size_t len = 0;
			for ( ; len < dstSize-1; ++len )
			{
				dst[len] = static_cast<char>( src[len] );
				if ( !src[len] )
				{
					break;
				}
			}
			dst[ len + 1 ] = '\0';

			return len;
		}
	}

	void ReplaceChar( wchar_t* s, size_t size, wchar_t from, wchar_t to )
	{
		impl::ReplaceCharT( s, size, from , to );
	}

	void ReplaceChar( char* s, size_t size, char from, char to )
	{
		impl::ReplaceCharT( s, size, from, to );
	}

	void StrToUpper( wchar_t* ptr, Uint32 size )
	{
		impl::StrToUpperT( ptr, size );
	}

	void StrToUpper( char* ptr, Uint32 size )
	{
		impl::StrToUpperT( ptr, size );
	}
	
	void StrToLower( wchar_t* ptr, Uint32 size )
	{
		impl::StrToLowerT( ptr, size );
	}
	
	void StrToLower( char* ptr, Uint32 size )
	{
		impl::StrToLowerT( ptr, size );
	}

	size_t WideCharToStdChar_NoConv( char* dst, const wchar_t* src, size_t dstSize )
	{
		return impl::FomXCharToYCharT_NoConv( dst, src, dstSize );
	}

	size_t StdCharToWideChar_NoConv( wchar_t* dst, const char* src, size_t dstSize )
	{
		return impl::FomXCharToYCharT_NoConv( dst, src, dstSize );
	}

	STATIC_CHECK_USE_DECL
	Int32 SNPrintFUnsafe( AnsiChar* buffer, size_t count, const AnsiChar* format, ... )
	{
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = VSNPrintF( buffer, count, format, arglist );
		va_end( arglist );
		return retval;
	}

	STATIC_CHECK_USE_DECL
	Int32 SNPrintFUnsafe( UniChar* buffer, size_t count, const UniChar* format, ... )
	{
		va_list arglist;
		va_start( arglist, format );
		Int32 retval = VSNPrintF( buffer, count, format, arglist );
		va_end( arglist );
		return retval;
	}
}
