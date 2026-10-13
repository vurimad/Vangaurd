/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redContainers/include/fundamentalStringParser.h"

// ANSI parsing of more complex types: all data is assumed to be in [], e.g.: [0,0,0,1]
extern RED_REFLECTION_API Bool GParseVector2( const AnsiChar*& stream, struct Vector2& value );
extern RED_REFLECTION_API Bool GParseVector3( const AnsiChar*& stream, struct Vector3& value );
extern RED_REFLECTION_API Bool GParseVector4( const AnsiChar*& stream, struct Vector4& value );
extern RED_REFLECTION_API Bool GParseEulerAngles( const AnsiChar*& stream, struct EulerAngles& value ); // Roll(X) Pitch(Y) Yaw(Z) order
extern RED_REFLECTION_API Bool GParsePoint( const AnsiChar*& stream, struct Point& value );
extern RED_REFLECTION_API Bool GParseColor( const AnsiChar*& stream, struct Color& value ); // RGBA order
extern RED_REFLECTION_API Bool GParseColorName( const AnsiChar*& stream, struct Color& value ); // Translate color name to color
extern RED_REFLECTION_API Bool GParseGuid( const AnsiChar*& stream, CGUID& ret ); // Standard GUID
