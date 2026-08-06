/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/
#include "build.h"
#include "guid.h"
#include "os.h"
#include "redThreadsAtomic.h"
#include "systemAssert.h"

namespace red
{
	const GUID GUID::ZERO;
}

#if ( defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) )
#	include <objbase.h>
#endif

// CRuntimeGUIDGenerator

red::Atomic< red::Uint64 > g_runtimeGUIDCounter( 0 );

red::Uint64 red::CRuntimeGUIDGenerator::GetRuntimeCounter()
{
	return g_runtimeGUIDCounter.GetValue();
}

void red::CRuntimeGUIDGenerator::SetRuntimeCounter( Uint64 counter )
{
	g_runtimeGUIDCounter.SetValue( counter );
}

red::Bool red::CRuntimeGUIDGenerator::CanCollideWithRuntimeGUID( const red::GUID& guid )
{
	return guid.parts.A == 0xFFFFFFFF && guid.parts.B == 0xFFFFFFFF;
}

red::GUID red::CRuntimeGUIDGenerator::CreateGUID()
{
	// Note:
	// It seems CoCreateGuid() generates GUIDs that have 0 set on the following 4 bits: 60,61,63,70
	// So, to avoid collision with these we're setting bits 0..63 to 1 (ignore the 70th bit for convenience reasons)
	// Also, to assure GUID uniqueness, we put the next value of persistent (stored in game savegame) 64-bit counter into remaining 64 bits

	GUID guid;
	guid.parts.A = 0xFFFFFFFF;
	guid.parts.B = 0xFFFFFFFF;
	*( Uint64* ) &guid.parts.C = g_runtimeGUIDCounter.Increment();

	return guid;
}

// GUID

red::GUID::GUID( const char* str )
{
	red::Memzero( this, sizeof( GUID ) );
	FromString( str );
}

red::Bool red::GUID::FromString( const char* str )
{
	const char* lastPtr = str + red::Strlen( str );
	char* endPtr = nullptr;
	for( Uint32 i = 0; i < RED_GUID_NUM_PARTS; ++i )
	{
		RED_SYSTEM_VERIFY( StringToInt( guid[ i ], str, &endPtr, BaseSixteen ), "Error converting string to guid: %s", str );
		RED_SYSTEM_ASSERT( str != endPtr, "Error converting string to guid: %s", str );

		// Quit if conversion failed
		if ( str == endPtr )
		{
			return false;
		}

		// Quit if we get to the end of the string
		if ( endPtr >= lastPtr )
		{
		    return ( i == RED_GUID_NUM_PARTS - 1 );
		}

		// Make sure we skip over the "-" char
		str = endPtr + 1;
	}

	return true;
}

void red::GUID::ToString( char* buffer, Uint32 bufferSize, const char* customFormat ) const
{
	SNPrintFUnsafe( buffer, bufferSize, customFormat ? customFormat : RED_GUID_STRING_FORMAT, parts.A, parts.B, parts.C, parts.D );
}

REDSYSTEM_API const red::AnsiChar* GCurrentCookedResourcePath = nullptr;
REDSYSTEM_API red::Uint32 GCurrentCookedResourceGUIDIndex = 0;
REDSYSTEM_API red::Bool GDeterministicGUIDGeneration = false;

red::GUID red::GUID::Create()
{
	GUID newGuid;

	if( GDeterministicGUIDGeneration )
	{
		if( GCurrentCookedResourcePath == nullptr )
			return red::GUID::ZERO;

		Uint64 resourceHash = red::CalculateHash64( GCurrentCookedResourcePath );
		newGuid.guid[ 0 ] = resourceHash >> 32;
		newGuid.guid[ 1 ] = resourceHash & 0xffffffff;
		newGuid.guid[ 2 ] = 0xBAADF00D;
		newGuid.guid[ 3 ] = GCurrentCookedResourceGUIDIndex++;
		return newGuid;
	}

#if ( defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) ) && !defined( RED_CONFIGURATION_FINAL )

	Bool canCollideWithRuntimeGUID;
	do 
	{
		::GUID guid;
		RED_SYSTEM_VERIFY( CoCreateGuid( &guid ) == S_OK, "Failed to create GUID" );
		static_assert( sizeof( ::GUID ) == sizeof( GUID ), "GUID size is no equal to Windows GUID" );
		red::Memcpy( &newGuid, &guid, sizeof( GUID ) );

		canCollideWithRuntimeGUID = CRuntimeGUIDGenerator::CanCollideWithRuntimeGUID( newGuid );
		RED_SYSTEM_ASSERT( !canCollideWithRuntimeGUID, "" );

	} while ( canCollideWithRuntimeGUID );

#else

	newGuid = CRuntimeGUIDGenerator::CreateGUID();

#endif

	return newGuid;
}

red::GUID red::GUID::Create( const char* str )
{
	GUID newGuid;
	newGuid.FromString( str );
	return newGuid;
}
