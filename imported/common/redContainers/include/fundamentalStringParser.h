/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../include/string/string.h"

// ANSI string parsing
extern RED_CONTAINERS_API Bool GParseString( const red::AnsiChar*& stream, red::String& value );
extern RED_CONTAINERS_API Bool GParseEscapedString(const red::AnsiChar*& stream, red::String& value);
extern RED_CONTAINERS_API Bool GParseWhitespaces( const AnsiChar*& stream );
extern RED_CONTAINERS_API Bool GParseIdentifier( const AnsiChar*& stream, red::String& string );
extern RED_CONTAINERS_API Bool GParseKeyword( const AnsiChar*& stream, const AnsiChar* keyword );
extern RED_CONTAINERS_API Bool GParseFloat( const AnsiChar*& stream, Float& value );
extern RED_CONTAINERS_API Bool GParseDouble( const AnsiChar*& stream, Double& value );
extern RED_CONTAINERS_API Bool GParseBool( const AnsiChar*& stream, Bool& value );
extern RED_CONTAINERS_API Bool GParseInteger( const AnsiChar*& stream, Int32& value );
extern RED_CONTAINERS_API Bool GParseInteger( const AnsiChar*& stream, Int64& value );
extern RED_CONTAINERS_API Bool GParseInteger( const AnsiChar*& stream, Uint32& value );
extern RED_CONTAINERS_API Bool GParseInteger( const AnsiChar*& stream, Uint64& value );
extern RED_CONTAINERS_API Bool GParseHex( const AnsiChar*& stream, Uint64& value );
extern RED_CONTAINERS_API Bool GParseHex( const AnsiChar*& stream, Uint32& value );
extern RED_CONTAINERS_API Bool GParseToken( const AnsiChar*& stream, red::String& token );

/// Ansi parser - typed - will fail if the value is out of range
extern RED_CONTAINERS_API Bool GParseInt8( const AnsiChar*& stream, Int8& value );
extern RED_CONTAINERS_API Bool GParseInt16( const AnsiChar*& stream, Int16& value );
extern RED_CONTAINERS_API Bool GParseInt32( const AnsiChar*& stream, Int32& value );
extern RED_CONTAINERS_API Bool GParseInt64( const AnsiChar*& stream, Int64& value );
extern RED_CONTAINERS_API Bool GParseUint8( const AnsiChar*& stream, Uint8& value );
extern RED_CONTAINERS_API Bool GParseUint16( const AnsiChar*& stream, Uint16& value );
extern RED_CONTAINERS_API Bool GParseUint32( const AnsiChar*& stream, Uint32& value );
extern RED_CONTAINERS_API Bool GParseUint64( const AnsiChar*& stream, Uint64& value );
