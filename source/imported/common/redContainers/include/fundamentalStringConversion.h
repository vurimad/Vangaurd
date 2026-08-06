/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "string/string.h"
#include "ustring/utf16String.h"

namespace red
{
    class String;
    class Utf16String;
    class RUIDRef;
    class GUID;
    class RUID;
}


//////////////////////////////////////////////////////////////////////////
// to/from string

template< typename T >
const Bool ToString( red::String& outTxt, const T& val, const char* customFormat = nullptr );

template< typename T >
const Bool FromString( const red::String& txt, T& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::String& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::String& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Utf16String& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Utf16String& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Bool val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Bool& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Int8 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Int8& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Int16 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Int16& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Int32 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Int32& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Int64 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Int64& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Uint8 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Uint8& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Uint16 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Uint16& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Uint32 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Uint32& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Uint64 val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Uint64& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Float val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool ToStringMaxPrecision( red::String& outTxt, const red::Float val );
extern RED_CONTAINERS_API const Bool ToStringMantisaExponentSign( red::String& outTxt, const red::Float val );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Float& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::Double val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::Double& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::GUID& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::GUID& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::RUID& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::RUID& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::RUIDRef& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::RUIDRef& outVal );

extern RED_CONTAINERS_API const Bool ToString( red::String& outTxt, const red::DynArray<Uint8>& val, const char* customFormat = nullptr );
extern RED_CONTAINERS_API const Bool FromString( const red::String& txt, red::DynArray<Uint8>& outVal );

//////////////////////////////////////////////////////////////////////////

template< typename T, typename P = RED_CONTAINER_STRING_DEFAULT_POOL_NAME >
RED_FORCE_INLINE const red::String ToStringDirect( const T& val, const P& pool = P() )
{
	red::String ret  { pool };
	(void)ToString( ret, val );
	return ret;
}

template< typename T, typename P = RED_CONTAINER_STRING_DEFAULT_POOL_NAME >
RED_FORCE_INLINE const red::Utf16String ToStringDirectUni( const T& val, const P& pool = P())
{
	String ret { pool };
	(void)ToString( ret, val );
	return red::Utf16String( ANSI_TO_UNICODE( ret.AsChar() ) );
}

template< typename T, typename P = RED_CONTAINER_STRING_DEFAULT_POOL_NAME >
RED_FORCE_INLINE const red::String ToStringDirectMaxPrecision( const T& val, const P& pool = P())
{
	red::String ret{ pool };
	(void)ToStringMaxPrecision( ret, val );
	return ret;
}

template< typename T >
RED_FORCE_INLINE const T FromStringDirect( const red::String& txt )
{
	T ret{};
	(void)FromString( txt, ret );
	return ret;
}

template< typename T >
RED_FORCE_INLINE const T FromStringDirectUni( const red::Utf16String& txt )
{
	T ret{};
	(void)FromString( UNICODE_TO_ANSI( txt.AsChar() ), ret );
	return ret;
}

template< typename T >
RED_FORCE_INLINE const Bool ToStringT( red::String& outTxt, const T& val )
{
	return ToString( outTxt, val );
}

template< typename T >
RED_FORCE_INLINE const Bool FromStringT( const red::String& txt, T& outVal )
{
	return FromString( txt, outVal );
}
