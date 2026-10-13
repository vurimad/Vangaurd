/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "string/string.h"
#include "string/stringLocale.h"
#include "ustring/utf8String.h"
#include "ustring/utf16String.h"
#include "string/stringView.h"


//////////////////////////////////////////////////////////////////////////
// type formatters
namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// String
	RED_CONTAINERS_API constexpr const char* GetFormatString( const red::String& val ) { RED_UNUSED( val ); return "%hs"; }
	extern RED_CONTAINERS_API Bool CheckFormatString( const red::String& val, const char* formatToCheck );
	extern RED_CONTAINERS_API Bool ToBuffer( char* buffer, const Uint32 bufferLen, const red::String& val, Int32& written, const char* customFormat = nullptr );

	// Utf8String
	RED_CONTAINERS_API constexpr const char* GetFormatString( const red::Utf8String& val ) { RED_UNUSED( val ); return "%hs"; }
	extern RED_CONTAINERS_API Bool CheckFormatString( const red::Utf8String& val, const char* formatToCheck );
	extern RED_CONTAINERS_API Bool ToBuffer( char* buffer, const Uint32 bufferLen, const red::Utf8String& val, Int32& written, const char* customFormat = nullptr );

	// Utf16String
	RED_CONTAINERS_API constexpr const char* GetFormatString( const red::Utf16String& val ) { RED_UNUSED( val ); return "%ls"; }
	extern RED_CONTAINERS_API Bool CheckFormatString( const red::Utf16String& val, const char* formatToCheck );
	extern RED_CONTAINERS_API Bool ToBuffer( char* buffer, const Uint32 bufferLen, const red::Utf16String& val, Int32& written, const char* customFormat = nullptr );

	// StringView
	RED_CONTAINERS_API constexpr const char* GetFormatString( const red::StringView& val ) { RED_UNUSED( val ); return "%hs"; }
	extern RED_CONTAINERS_API Bool CheckFormatString( const red::StringView& val, const char* formatToCheck );
	extern RED_CONTAINERS_API Bool ToBuffer( char* buffer, const Uint32 bufferLen, const red::StringView& val, Int32& written, const char* customFormat = nullptr );
}
