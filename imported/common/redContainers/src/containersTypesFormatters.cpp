/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "containersTypesFormatters.h"


//////////////////////////////////////////////////////////////////////////
// using
using red::String;
using red::Utf8String;
using red::Utf16String;
using red::StringView;


//////////////////////////////////////////////////////////////////////////
// string
Bool red::CheckFormatString( const String& val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkh = checkFormat ? std::strstr( checkFormat+1, "hs" ) : nullptr;

		if ( checkFormat && checkh )
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const String& val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		// using provided format string
		if ( formatString && (CheckFormatString( val, formatString ) || prv::CheckNewFormatString( val, formatString )) )
		{
			char outFormatString[ 128 ];
			prv::PrepareFormatString( formatString, red::GetFormatString( val ), outFormatString, sizeof( outFormatString ) );
			written += SNPrintFUnsafe( buffer, bufferLen, outFormatString, val.AsChar() );
			return true;
		}
	}
	written = 0;
	return false;
}


//////////////////////////////////////////////////////////////////////////
// utf16 string
Bool red::CheckFormatString( const Utf16String& val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkl = checkFormat ? std::strstr( checkFormat+1, "ls" ) : nullptr;

		if ( checkFormat && checkl )
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const Utf16String& val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		// using provided format string
		if ( formatString && (CheckFormatString( val, formatString ) || prv::CheckNewFormatString( val, formatString )) )
		{
			char outFormatString[ 128 ];
			prv::PrepareFormatString( formatString, red::GetFormatString( val ), outFormatString, sizeof( outFormatString ) );
			written += SNPrintFUnsafe( buffer, bufferLen, outFormatString, val.AsChar() );
			return true;
		}
	}
	written = 0;
	return false;
}


//////////////////////////////////////////////////////////////////////////
// utf8 string
Bool red::CheckFormatString( const Utf8String& val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );
		const char* checkh = checkFormat ? std::strstr( checkFormat+1, "hs" ) : nullptr;

		if ( checkFormat && checkh )
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const Utf8String& val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		// using provided format string
		if ( formatString && (CheckFormatString( val, formatString ) || prv::CheckNewFormatString( val, formatString )) )
		{
			char outFormatString[ 128 ];
			prv::PrepareFormatString( formatString, red::GetFormatString( val ), outFormatString, sizeof( outFormatString ) );
			written += SNPrintFUnsafe( buffer, bufferLen, outFormatString, val.GetPtr() );
			return true;
		}
	}
	written = 0;
	return false;
}


//////////////////////////////////////////////////////////////////////////
// string view
Bool red::CheckFormatString( const StringView& val, const char* formatToCheck )
{
	if ( formatToCheck )
	{
		const char* checkFormat = std::strchr( formatToCheck, '%' );

		if ( checkFormat )
		{
			return true;
		}
	}
	return false;
}

Bool red::ToBuffer( char* buffer, const Uint32 bufferLen, const StringView& val, Int32& written, const char* formatString )
{
	if ( buffer && bufferLen )
	{
		// using provided format string
		if ( formatString && (CheckFormatString( val, formatString ) || prv::CheckNewFormatString( val, formatString )) )
		{
			char outFormatString[ 128 ];
			prv::PrepareFormatString( formatString, red::GetFormatString( val ), outFormatString, sizeof( outFormatString ) );
			const Uint32 len = val.Length();
			const String& valCopied = String_CreateExternal_OnStack_SetN( val.Data(), len, len+1 );
			written += SNPrintFUnsafe( buffer, bufferLen, outFormatString, valCopied.AsChar() );
			return true;
		}
	}
	written = 0;
	return false;
}
