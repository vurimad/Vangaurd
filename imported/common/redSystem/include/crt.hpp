/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_SYSTEM_CRT_HPP_
#define _RED_SYSTEM_CRT_HPP_

#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cctype>
#include <cwctype>
#include <stdio.h>
#include <errno.h>

namespace red
{
	RED_FORCE_INLINE void checkMemcpyArgs( const void* dest, const void* src, size_t n )
	{
		RED_TOUCH3(dest, src, n );

// #tbd: break this log/assert cycle or use trace instead
// #ifdef RED_ASSERTS_ENABLED
// 		const uintptr_t srcAddr = reinterpret_cast<uintptr_t>(src);
// 		const uintptr_t destAddr = reinterpret_cast<uintptr_t>(dest);
// 		RED_SYSTEM_ASSERT( src || 0 == n, "Src nullptr");
// 		RED_SYSTEM_ASSERT( dest || 0 == n, "Dest nullptr");
// 		RED_SYSTEM_ASSERT( 0 == n ||											/* not copying anything anyway, happens */
// 			( srcAddr != destAddr ) &&
// 			(srcAddr > destAddr && destAddr+n-1 < srcAddr) ||		/* OR copy starts above dest and writing into dest won't clobber src */
// 			(srcAddr < destAddr && srcAddr+n-1 < destAddr),		/* OR copy starts below dest and never moves into dest */
// 			"memcpy with overlapping dest and src is undefined and does cause serious bugs! Use memmove instead: dest=%p, src=%p, size=%llu",
// 			dest,
// 			src,
// 			n );
// #endif
	}

	RED_FORCE_INLINE void Memcpy( void* __restrict dest, const void* __restrict source, size_t size )
	{
		checkMemcpyArgs( dest, source, size ); std::memcpy( dest, source, size );
	}
	
	RED_FORCE_INLINE void Memmove( void* dest, const void* source, size_t size )
	{
		 std::memmove( dest, source, size );
	}
	
	RED_FORCE_INLINE void Memset( void* buffer, Int32 value, size_t size )
	{
		std::memset( buffer, value, size ); 
	}
	
	RED_FORCE_INLINE void Memzero( void* buffer, size_t size )
	{
		std::memset( buffer, 0, size );
	}
	
	RED_FORCE_INLINE int Memcmp( const void* a, const void* b, size_t size)
	{
		return std::memcmp( a, b, size );
	}

	RED_FORCE_INLINE size_t Strlen( const AnsiChar* str )
	{ 
		return std::strlen( str ); 
	}

	RED_FORCE_INLINE size_t Strlen( const UniChar* str )
	{ 
		return std::wcslen( str ); 
	}

#ifdef RED_PLATFORM_LINUX

	RED_FORCE_INLINE size_t Strlen( const AnsiChar* str, size_t maxBufferSize )
	{
		return ::strnlen( str, maxBufferSize );
	}

	RED_FORCE_INLINE size_t Strlen( const UniChar* str, size_t maxBufferSize )
	{
		return ::wcsnlen( str, maxBufferSize );
	}

	RED_FORCE_INLINE Bool Strcpy( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy )
	{
		MemSize sourceLength = Strlen( source, sourceToCopy );
		const MemSize n = destSize < ( sourceLength + 1 ) ? destSize : ( sourceLength + 1 );
		::strncpy( dest, source, n );
		if ( n > 0 )
			dest[n - 1] = '\0';
		return true;
	}

	RED_FORCE_INLINE Bool Strcpy( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy )
	{
		MemSize sourceLength = Strlen( source, sourceToCopy );
		const MemSize n = destSize < ( sourceLength + 1 ) ? destSize : ( sourceLength + 1 );
		::wcsncpy( dest, source, n );
		if ( n > 0 )
			dest[n - 1] = L'\0';
		return true;
	}

	RED_FORCE_INLINE Bool Strcat( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy )
	{
		MemSize sourceLength = Strlen( source, sourceToCopy );
		// strncat writes n chars and then appends null
		const MemSize nbytes = destSize < ( sourceLength + 1 ) ? destSize : ( sourceLength + 1 );
		if ( nbytes < 1 )
		{
			*dest = '\0';
			return true;
		}
		::strncat( dest, source, nbytes - 1 );
		return true;
	}

	RED_FORCE_INLINE Bool Strcat( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy )
	{
		MemSize sourceLength = Strlen( source, sourceToCopy );
		// wcsncat writes n chars and then appends null
		const MemSize nbytes = destSize < ( sourceLength + 1 ) ? destSize : ( sourceLength + 1 );
		if ( nbytes < 1 )
		{
			*dest = TXT( '\0' );
			return true;
		}
		::wcsncat( dest, source, nbytes - 1 );
		return true;
	}

#else

	RED_FORCE_INLINE size_t Strlen( const AnsiChar* str, size_t maxBufferSize )
	{
		return ::strnlen_s( str, maxBufferSize );
	}

	RED_FORCE_INLINE size_t Strlen( const UniChar* str, size_t maxBufferSize )
	{
		return ::wcsnlen_s( str, maxBufferSize );
	}

	RED_FORCE_INLINE Bool Strcpy( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy )		
	{
#ifdef RED_COMPILER_CLANG
		return ::strncpy_s( dest, destSize, source, sourceToCopy < RSIZE_MAX ? sourceToCopy : RSIZE_MAX ) == 0; 
#else
		return ::strncpy_s( dest, destSize, source, sourceToCopy ) == 0; 
#endif
	}

	RED_FORCE_INLINE Bool Strcpy( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy )
	{ 
#ifdef RED_COMPILER_CLANG
		return ::wcsncpy_s( dest, destSize, source, sourceToCopy < RSIZE_MAX ? sourceToCopy : RSIZE_MAX ) == 0;
#else
		return ::wcsncpy_s( dest, destSize, source, sourceToCopy ) == 0;
#endif
	}
	
	RED_FORCE_INLINE Bool Strcat( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy )
	{ 
#ifdef RED_COMPILER_CLANG
		return ::strncat_s( dest, destSize, source, sourceToCopy < RSIZE_MAX ? sourceToCopy : RSIZE_MAX ) == 0; 
#else
		return ::strncat_s( dest, destSize, source, sourceToCopy ) == 0; 
#endif
	}
	
	RED_FORCE_INLINE Bool Strcat( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy  )			
	{ 
#ifdef RED_COMPILER_CLANG
		return ::wcsncat_s( dest, destSize, source, sourceToCopy < RSIZE_MAX ? sourceToCopy : RSIZE_MAX ) == 0; 
#else
		return ::wcsncat_s( dest, destSize, source, sourceToCopy ) == 0; 
#endif
	}

#endif

	RED_FORCE_INLINE Int32 Strcmp( const AnsiChar* a, const AnsiChar* b )						
	{ 
		return std::strcmp( a, b ); 
	}
	
	RED_FORCE_INLINE Int32 Strcmp( const UniChar* a, const UniChar* b )
	{ 
		return ::wcscmp( a, b ); 
	}
	
	RED_FORCE_INLINE Int32 StrcmpNC( const AnsiChar* a, const AnsiChar* b )
	{
#ifdef RED_COMPILER_CLANG	
		return ::strcasecmp( a, b );
#else	
		return ::_stricmp( a, b ); 
#endif	
	}

	RED_FORCE_INLINE Int32 Strcmp( const AnsiChar* a, const AnsiChar* b, size_t max )
	{
		return std::strncmp( a, b, max ); 
	}
	
	RED_FORCE_INLINE Int32 Strcmp( const UniChar* a, const UniChar* b, size_t max )
	{ 
		return ::wcsncmp( a, b, max ); 
	}
	
	RED_FORCE_INLINE Int32 StrcmpNC( const AnsiChar* a, const AnsiChar* b, size_t max )
	{
#ifdef RED_COMPILER_CLANG	
		return ::strncasecmp( a, b, max );
#else
		return ::_strnicmp( a, b, max ); 
#endif	
	}

	RED_FORCE_INLINE AnsiChar* Strchr( AnsiChar* str, AnsiChar searchTerm )				
	{ 
		return std::strchr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const AnsiChar* Strchr( const AnsiChar* str, AnsiChar searchTerm )			
	{ 
		return std::strchr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE UniChar* Strchr( UniChar* str, UniChar searchTerm )					
	{ 
		return ::wcschr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const UniChar* Strchr( const UniChar* str, UniChar searchTerm )			
	{ 
		return ::wcschr( str, searchTerm ); 
	}

	RED_FORCE_INLINE AnsiChar* StrchrR( AnsiChar* str, AnsiChar searchTerm )			
	{ 
		return std::strrchr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const AnsiChar* StrchrR( const AnsiChar* str, AnsiChar searchTerm )		
	{ 
		return std::strrchr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE UniChar* StrchrR( UniChar* str, UniChar searchTerm )				
	{ 
		return ::wcsrchr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const UniChar*	StrchrR( const UniChar* str, UniChar searchTerm )		
	{ return ::wcsrchr( str, searchTerm ); 
	}

	RED_FORCE_INLINE AnsiChar* Strstr( AnsiChar* str, const AnsiChar* searchTerm )			
	{ 
		return std::strstr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const AnsiChar* Strstr( const AnsiChar* str, const AnsiChar* searchTerm )	
	{ 
		return std::strstr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE UniChar* Strstr( UniChar* str, const UniChar* searchTerm )			
	{ 
		return ::wcsstr( str, searchTerm ); 
	}
	
	RED_FORCE_INLINE const UniChar* Strstr( const UniChar* str, const UniChar* searchTerm )		
	{ 
		return ::wcsstr( str, searchTerm ); 
	}

	RED_FORCE_INLINE Bool IsWhiteSpace( AnsiChar c )
	{
		return std::isspace( c ) != 0;
	}

	RED_FORCE_INLINE Bool IsWhiteSpace( UniChar c )
	{
		return std::iswspace( c ) != 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Int64& out, const AnsiChar* in, AnsiChar** end, Base base )
	{
		errno = 0;
#ifdef RED_COMPILER_CLANG	
		out = static_cast< Int64 >( std::strtoll( in, end, base ) );
#else
		out = static_cast< Int64 >( _strtoi64( in, end, base ) );
#endif	
		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Uint64& out, const AnsiChar* in, AnsiChar** end, Base base )
	{
		errno = 0;
#ifdef RED_COMPILER_CLANG	
		out = static_cast<Uint64>( std::strtoul( in, end, base ) );
#else
		out = static_cast<Uint64>( _strtoui64( in, end, base ) );
#endif	
		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Int32& out, const AnsiChar* in, AnsiChar** end, Base base )
	{
		errno = 0;
		long tmp = std::strtol( in, end, base );
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
		// Check enough subtle bugs in the past have been from 64 vs 32-bit longs
		// Could make own own parsing function, although the stdlib ones also take locale support into consideration
		if ( tmp > INT32_MAX )
		{
			tmp = INT32_MAX;
			errno = ERANGE;
		}
		else if ( tmp < INT32_MIN )
		{
			tmp = INT32_MIN;
			errno = ERANGE;
		}
#endif

		out = static_cast< Int32 >( tmp );

		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Int32& out, const UniChar* in, UniChar** end, Base base )
	{
		errno = 0;
		long tmp = ::wcstol( in, end, base );

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
		// Check enough subtle bugs in the past have been from 64 vs 32-bit longs
		// Could make own own parsing function, although the stdlib ones also take locale support into consideration
		if ( tmp > INT32_MAX )
		{
			tmp = INT32_MAX;
			errno = ERANGE;
		}
		else if ( tmp < INT32_MIN )
		{
			tmp = INT32_MIN;
			errno = ERANGE;
		}
#endif

		out = static_cast< Int32 >( tmp );

		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Uint32& out, const AnsiChar* in, AnsiChar** end, Base base )
	{
		errno = 0;
		unsigned long tmp = std::strtoul( in, end, base );

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
		// Check enough subtle bugs in the past have been from 64 vs 32-bit longs
		// Could make own own parsing function, although the stdlib ones also take locale support into consideration
		if ( tmp > UINT32_MAX )
		{
			tmp = UINT32_MAX;
			errno = ERANGE;
		}
#endif

		out = static_cast< Uint32 >( tmp );

		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Bool StringToInt( Uint32& out, const UniChar* in, UniChar** end, Base base )
	{
		errno = 0;
		unsigned long tmp = ::wcstoul( in, end, base );

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
		// Check enough subtle bugs in the past have been from 64 vs 32-bit longs
		// Could make own own parsing function, although the stdlib ones also take locale support into consideration
		if ( tmp > UINT32_MAX )
		{
			tmp = UINT32_MAX;
			errno = ERANGE;
		}
#endif
		out = static_cast< Uint32 >( tmp );

		// If errno == ERANGE, "out" has been set to INT_MIN or INT_MAX

		return errno == 0;
	}

	RED_FORCE_INLINE Double	StringToDouble( const AnsiChar* str )	
	{ 
		return ::atof( str ); 
	}

	RED_FORCE_INLINE Int32 SScanf( const AnsiChar* buffer, const AnsiChar* format, ... )
	{
		if ( buffer == nullptr ) return -1;
		va_list arg; 
		va_start( arg, format ); 
#if defined(RED_PLATFORM_LINUX)
		Int32 result = vsscanf( buffer, format, arg ); 
#else
		Int32 result = vsscanf_s( buffer, format, arg ); 
#endif
		va_end(arg);
		return result;
	}

#ifdef RED_PLATFORM_LINUX

	RED_FORCE_INLINE Int32 VSNPrintF( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, va_list arg )
	{
		if ( buffer == nullptr ) return -1; 
		return ::vsnprintf( buffer, count, format, arg );
	}

	RED_FORCE_INLINE Int32 VSNPrintF( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, va_list arg )
	{
		if ( buffer == nullptr ) return -1; 
		// FIXME theres no wsnwprintf function available on the standard
		return ::vswprintf( buffer, count, format, arg );
	}

	RED_FORCE_INLINE Int32 VSPrintF( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, va_list arg )
	{
		if ( buffer == nullptr ) return -1;
		return ::vsprintf( buffer, format, arg );
	}

	RED_FORCE_INLINE Int32 VSPrintF( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, va_list arg )
	{
		if ( buffer == nullptr ) return -1;
		return ::vswprintf( buffer, count, format, arg );
	}

#else

	RED_FORCE_INLINE Int32 VSNPrintF( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, va_list arg )
	{ 
#ifdef RED_COMPILER_CLANG	
		return ::vsnprintf_s( buffer, count, format, arg ); 
#else		
		return ::vsnprintf_s( buffer, count, _TRUNCATE, format, arg );  
#endif	
	}

	RED_FORCE_INLINE Int32 VSNPrintF( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, va_list arg )
	{ 
#ifdef RED_COMPILER_CLANG	
		return vsnwprintf_s( buffer, count, format, arg ); 
#else		
		return ::_vsnwprintf_s( buffer, count, _TRUNCATE, format, arg ); 
#endif	
	}

	RED_FORCE_INLINE Int32 VSPrintF( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, va_list arg )
	{	
		return ::vsprintf_s( buffer, count, format, arg );
	}

	RED_FORCE_INLINE Int32 VSPrintF( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, va_list arg )
	{
		return ::vswprintf_s( buffer, count, format, arg );
	}

#endif
}

#endif
