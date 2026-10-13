/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "crt.h"
#include "ruid.h"

namespace red
{

RUID RUID::CreateTransient()
{
	return RUID( Transient );
}

void RUID::DisableVirtualRUIDs()
{
	s_isVirtualRUIDsEnabled = false;
}

RUID RUID::Zero()
{
	return RUID();
}

RUID::RUID( ETransient )
{
	// 62-bit counter
	const Uint64 counter = s_dynamicCounter.Increment();
	constexpr Uint64 maxCounter = ( 1ULL << 62 ) - 1;
	RED_FATAL_ASSERT( counter <= maxCounter, "Counter exceeded max allowed!" );
	m_value = ( counter << 2 ) | c_transientMask;
}

Bool RUID::FromString( const char* str )
{
	Uint64 parsedValue = 0;
	char* end = nullptr;
	if ( !str || !*str || !StringToInt( parsedValue, str, &end, BaseSixteen ) || *end != '\0' /*parsed to end of string*/ )
	{
		m_value = 0;
		return false;
	}

	*this = RUID( parsedValue );
	return true;
}

Bool RUID::Unsafe_FromString( const char* str )
{
	Uint64 parsedValue = 0;
	if ( !str || !*str || !StringToInt( parsedValue, str, nullptr, BaseSixteen ) )
	{
		m_value = 0;
		return false;
	}

	*this = RUID( parsedValue );
	return true;
}

void RUID::ToString( char* buffer, Uint32 bufferSize, const char* customFormat ) const
{
	SNPrintFUnsafe( buffer, bufferSize, customFormat ? customFormat : RED_RUID_STRING_FORMAT, m_value );
}

RUIDRef RUIDRef::Zero()
{
	return RUIDRef();
}

Atomic<Uint64> RUID::s_dynamicCounter;
Bool RUID::s_isVirtualRUIDsEnabled = true;

} // red

