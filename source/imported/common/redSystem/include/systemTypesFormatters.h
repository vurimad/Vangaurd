/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "redSystemPublic.h"
#include <float.h>
#include "types.h"
#include "ruid.h"
#include "guid.h"

//////////////////////////////////////////////////////////////////////////
// type formatters
namespace red
{
	// char* (AnsiChar*)
	REDSYSTEM_API constexpr const char* GetFormatString( const char* val ) { RED_UNUSED( val ); return "%hs"; }
	extern REDSYSTEM_API Bool CheckFormatString( const char* val, const char* formatToCheck );

	// wchar_t* (UniChar*)
	REDSYSTEM_API constexpr const char* GetFormatString( const UniChar* val ) { RED_UNUSED( val ); return "%ls"; }
	extern REDSYSTEM_API Bool CheckFormatString( const UniChar* val, const char* formatToCheck );

	// Bool
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Bool val ) { RED_UNUSED( val ); return "%hs"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Bool val, const char* formatToCheck );
	REDSYSTEM_API extern Bool ToBuffer( char* buffer, const Uint32 bufferLen, const red::Bool val, Int32& written, const char* formatString = nullptr );

	// Int8
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Int8 val ) { RED_UNUSED( val ); return "%hhi"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Int8 val, const char* formatToCheck );

	// Int16
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Int16 val ) { RED_UNUSED( val ); return "%hi"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Int16 val, const char* formatToCheck );

	// Int32
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Int32 val ) { RED_UNUSED( val ); return "%i"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Int32 val, const char* formatToCheck );

	// Int64
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Int64 val ) { RED_UNUSED( val ); return "%lli"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Int64 val, const char* formatToCheck );

	// Uint8
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Uint8 val ) { RED_UNUSED( val ); return "%hhu"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Uint8 val, const char* formatToCheck );

	// Uint16
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Uint16 val ) { RED_UNUSED( val ); return "%hu"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Uint16 val, const char* formatToCheck );

	// Uint32
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Uint32 val ) { RED_UNUSED( val ); return "%u"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Uint32 val, const char* formatToCheck );

	// Uint64
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Uint64 val ) { RED_UNUSED( val ); return "%llu"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Uint64 val, const char* formatToCheck );

	// Float
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Float val ) { RED_UNUSED( val ); return "%f"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Float val, const char* formatToCheck );

	// Double
	REDSYSTEM_API constexpr const char* GetFormatString( const red::Double val ) { RED_UNUSED( val ); return "%f"; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::Double val, const char* formatToCheck );

	// void* - pointer
	REDSYSTEM_API constexpr const char* GetFormatString( const void* val ) { RED_UNUSED( val ); return "%016llX"; }
	extern REDSYSTEM_API Bool CheckFormatString( const void* val, const char* formatToCheck );

	// GUID
	REDSYSTEM_API constexpr const char* GetFormatString(const red::GUID& val) { RED_UNUSED( val ); return RED_GUID_STRING_FORMAT; }
	extern REDSYSTEM_API Bool CheckFormatString(const red::GUID& val, const char* formatToCheck);
	extern REDSYSTEM_API Bool ToBuffer(char* buffer, const red::Uint32 bufferLen, const red::GUID& val, red::Int32& written, const char* formatString = nullptr);

	// RUID
	REDSYSTEM_API constexpr const char* GetFormatString( const red::RUID& val ) { RED_UNUSED( val ); return RED_RUID_STRING_FORMAT; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::RUID val, const char* formatToCheck );
	extern REDSYSTEM_API Bool ToBuffer( char* buffer, const red::Uint32 bufferLen, const red::RUID val, red::Int32& written, const char* formatString = nullptr );

	REDSYSTEM_API constexpr const char* GetFormatString( const red::RUIDRef& val ) { RED_UNUSED( val ); return RED_RUID_STRING_FORMAT; }
	extern REDSYSTEM_API Bool CheckFormatString( const red::RUIDRef val, const char* formatToCheck );
	extern REDSYSTEM_API Bool ToBuffer( char* buffer, const red::Uint32 bufferLen, const red::RUIDRef val, red::Int32& written, const char* formatString = nullptr );
}