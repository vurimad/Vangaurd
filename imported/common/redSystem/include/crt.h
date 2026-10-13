/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_SYSTEM_CRT_H_
#define _RED_SYSTEM_CRT_H_

namespace red
{
	void Memcpy( void* __restrict dest, const void* __restrict source, size_t size );
	void Memmove( void* dest, const void* source, size_t size );
	void Memset( void* buffer, Int32 value, size_t size );
	void Memzero( void* buffer, size_t size );
	int Memcmp( const void* a, const void* b, size_t size);

	size_t Strlen( const AnsiChar* str );
	size_t Strlen( const UniChar* str );
	size_t Strlen( const AnsiChar* str, size_t maxBufferSize );
	size_t Strlen( const UniChar* str, size_t maxBufferSize );

	Bool Strcpy( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy = -1 );
	Bool Strcpy( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy = -1 );
	Bool Strcat( AnsiChar* dest, const AnsiChar* source, size_t destSize, size_t sourceToCopy = -1);
	Bool Strcat( UniChar* dest, const UniChar* source, size_t destSize, size_t sourceToCopy = -1 );

	Int32 Strcmp( const AnsiChar* a, const AnsiChar* b );
	Int32 Strcmp( const UniChar* a, const UniChar* b );
	REDSYSTEM_API Int32 StrcmpNC( const AnsiChar* a, const AnsiChar* b );
	REDSYSTEM_API Int32 StrcmpNC( const UniChar* a, const UniChar* b );

	// Compare at most 'max' characters
	Int32 Strcmp( const AnsiChar* a, const AnsiChar* b, size_t max );
	Int32 Strcmp( const UniChar* a, const UniChar* b, size_t max );
	Int32 StrcmpNC( const AnsiChar* a, const AnsiChar* b, size_t max );
	REDSYSTEM_API Int32 StrcmpNC( const UniChar* a, const UniChar* b, size_t max );

	// Search for first occurrence of a character
	AnsiChar* Strchr( AnsiChar* str, AnsiChar searchTerm );
	const AnsiChar* Strchr( const AnsiChar* str, AnsiChar searchTerm );
	UniChar* Strchr( UniChar* str, UniChar searchTerm );
	const UniChar* Strchr( const UniChar* str, UniChar searchTerm );

	// Search for last occurrence of a character
	AnsiChar* StrchrR( AnsiChar* str, AnsiChar searchTerm );
	const AnsiChar* StrchrR( const AnsiChar* str, AnsiChar searchTerm );
	UniChar* StrchrR( UniChar* str, UniChar searchTerm );
	const UniChar* StrchrR( const UniChar* str, UniChar searchTerm );

	// Search for the first instance of a string
	AnsiChar* Strstr( AnsiChar* str, const AnsiChar* searchTerm );
	const AnsiChar* Strstr( const AnsiChar* str, const AnsiChar* searchTerm );
	UniChar* Strstr( UniChar* str, const UniChar* searchTerm );
	const UniChar* Strstr( const UniChar* str, const UniChar* searchTerm );

	// Check if given character is whitespace
	Bool IsWhiteSpace( AnsiChar c );
	Bool IsWhiteSpace( UniChar c );

	//////////////////////////////////////////////////////////////////////////
	// string functions

	enum class StringConversionType
	{
		ConvertAll,
		Truncate,
	};

	REDSYSTEM_API size_t WideCharToStdChar( AnsiChar* dest, const UniChar* source, size_t destSize, StringConversionType conversionType = StringConversionType::ConvertAll );
	REDSYSTEM_API size_t StdCharToWideChar( UniChar* dest, const AnsiChar* source, size_t destSize, StringConversionType conversionType = StringConversionType::ConvertAll );
	
	// Faster versions that expect ASCII stored in wide-char; no checks
	REDSYSTEM_API size_t WideCharToStdChar_NoConv( char* dest, const wchar_t* source, size_t destSize );
	REDSYSTEM_API size_t StdCharToWideChar_NoConv( wchar_t* dest, const char* source, size_t destSize );

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	REDSYSTEM_API int FileSystemStringCharCount( const char* src );
	REDSYSTEM_API bool EngineStringToFileSystemString( const char* src, wchar_t* dst, size_t dstSize );
	REDSYSTEM_API int EngineStringCharCount( const wchar_t* src );
	REDSYSTEM_API bool FileSystemStringToEngineString( const wchar_t* src, char* dst, size_t dstSize );
#endif

	REDSYSTEM_API void ReplaceChar( wchar_t* s, size_t size, wchar_t from, wchar_t to );
	REDSYSTEM_API void ReplaceChar( char* s, size_t size, char from, char to );
	REDSYSTEM_API void StrToUpper( wchar_t* ptr, Uint32 size );
	REDSYSTEM_API void StrToUpper( char* ptr, Uint32 size );
	REDSYSTEM_API void StrToLower( wchar_t* ptr, Uint32 size );
	REDSYSTEM_API void StrToLower( char* ptr, Uint32 size );

	enum Base
	{
		BaseAuto = 0,		// Detect based on format of number
		BaseEight = 8,		// Octal
		BaseTen = 10,		// Decimal
		BaseSixteen = 16,	// Hex
	};

	Bool StringToInt( Int32& out, const AnsiChar* in, AnsiChar** end, Base base );
	Bool StringToInt( Int64& out, const AnsiChar* in, AnsiChar** end, Base base );
	Bool StringToInt( Uint64& out, const AnsiChar* in, AnsiChar** end, Base base );
	Bool StringToInt( Int32& out, const UniChar* in, UniChar** end, Base base );
	Bool StringToInt( Uint32& out, const AnsiChar* in, AnsiChar** end, Base base );
	Bool StringToInt( Uint32& out, const UniChar* in, UniChar** end, Base base );

	REDSYSTEM_API Double StringToDouble( const AnsiChar* str );
	REDSYSTEM_API Double StringToDouble( const Char* str );

	//////////////////////////////////////////////////////////////////////////
	// Print Functions
	Int32 VSNPrintF( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, va_list arg );
	Int32 VSNPrintF( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, va_list arg );

	REDSYSTEM_API Int32 SNPrintFUnsafe( AnsiChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... );
	REDSYSTEM_API Int32 SNPrintFUnsafe( UniChar* buffer, size_t count, STATIC_CHECK_PRINTF_MSC const UniChar* format, ... );

	// These functions are for converting to "Char" from UniChar or AnsiChar or any custom typedef
	RED_FORCE_INLINE Bool StringConvert( UniChar* dest, const AnsiChar* source, size_t destSize )		{ return StdCharToWideChar( dest, source, destSize ) > 0; }
	RED_FORCE_INLINE Bool StringConvert( AnsiChar* dest, const UniChar* source, size_t destSize )		{ return WideCharToStdChar( dest, source, destSize ) > 0; }

	// These are "dummy" functions that provide the same appearance of functionality as the above functions
	// Allowing you to use StringConvert when some platform configurations have differing choices on encoding
	RED_FORCE_INLINE Bool StringConvert( UniChar* dest, const UniChar* source, size_t destSize )		{ return red::Strcpy( dest, source, destSize ); }
	RED_FORCE_INLINE Bool StringConvert( AnsiChar* dest, const AnsiChar* source, size_t destSize )		{ return red::Strcpy( dest, source, destSize ); }
}

#include "crt.hpp"

#endif
