/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "stringParser.h"

#include "math.h"
#include "mathVector2.h"
#include "mathVector3.h"
#include "mathVector4.h"
#include "mathEulerAngles.h"
#include "mathPoint.h"
#include "mathColor.h"

Bool GParseVector2(const AnsiChar*& stream, Vector2& value)
{
	Vector2 temp(0.0f, 0.0f);
	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseFloat(stream, temp.X))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Y))
		return false;
	if (!GParseKeyword(stream, "]"))
		return false;

	value = temp;
	return true;
}

Bool GParseVector3(const AnsiChar*& stream, Vector3& value)
{
	Vector3 temp(0.0f, 0.0f, 0.0f);
	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseFloat(stream, temp.X))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Y))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Z))
		return false;
	if (!GParseKeyword(stream, "]"))
		return false;

	value = temp;
	return true;
}

Bool GParseVector4(const AnsiChar*& stream, Vector4& value)
{
	Vector4 temp(0.0f, 0.0f, 0.0f, 1.0f);
	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseFloat(stream, temp.X))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Y))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Z))
		return false;

	// W component is optional in vector4
	if (GParseKeyword(stream, ","))
		if (!GParseFloat(stream, temp.W))
			return false;

	if (!GParseKeyword(stream, "]"))
		return false;

	value = temp;
	return true;
}

Bool GParseEulerAngles(const AnsiChar*& stream, EulerAngles& value)
{
	EulerAngles temp(0.0f, 0.0f, 0.0f);
	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseFloat(stream, temp.Roll))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Pitch))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseFloat(stream, temp.Yaw))
		return false;
	if (!GParseKeyword(stream, "]"))
		return false;

	value = temp;
	return true;
}

Bool GParsePoint( const AnsiChar*& stream, struct Point& value )
{
	Point temp(0, 0);
	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseInteger(stream, temp.x))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseInteger(stream, temp.y))
		return false;
	if (!GParseKeyword(stream, "]"))
		return false;

	value = temp;
	return true;
}

Bool GParseColor(const AnsiChar*& stream, Color& value)
{
	Int32 r = 0, g = 0, b = 0, a = 255;

	if (!GParseKeyword(stream, "["))
		return false;
	if (!GParseInteger(stream, r))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseInteger(stream, g))
		return false;
	if (!GParseKeyword(stream, ","))
		return false;
	if (!GParseInteger(stream, b))
		return false;

	// alpha is optional
	if (GParseKeyword(stream, ","))
		if (!GParseInteger(stream, a))
			return false;

	if (!GParseKeyword(stream, "]"))
		return false;

	value = Color(
		(Uint8)Clamp<Int32>(r, 0, 255),
		(Uint8)Clamp<Int32>(g, 0, 255),
		(Uint8)Clamp<Int32>(b, 0, 255),
		(Uint8)Clamp<Int32>(a, 0, 255));

	return true;
}

Bool GParseColorName(const AnsiChar*& stream, struct Color& value)
{
	if (red::StrcmpNC("BLACK", stream) == 0)
	{
		value = Color::BLACK();
		return true;
	}
	else if (red::StrcmpNC("WHITE", stream) == 0)
	{
		value = Color::WHITE();
		return true;
	}
	else if (red::StrcmpNC("RED", stream) == 0)
	{
		value = Color::RED();
		return true;
	}
	else if (red::StrcmpNC("GREEN", stream) == 0)
	{
		value = Color::GREEN();
		return true;
	}
	else if (red::StrcmpNC("BLUE", stream) == 0)
	{
		value = Color::BLUE();
		return true;
	}
	else if (red::StrcmpNC("YELLOW", stream) == 0)
	{
		value = Color::YELLOW();
		return true;
	}
	else if (red::StrcmpNC("CYAN", stream) == 0)
	{
		value = Color::CYAN();
		return true;
	}
	else if (red::StrcmpNC("MAGENTA", stream) == 0)
	{
		value = Color::MAGENTA();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_RED", stream) == 0)
	{
		value = Color::LIGHT_RED();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_GREEN", stream) == 0)
	{
		value = Color::LIGHT_GREEN();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_BLUE", stream) == 0)
	{
		value = Color::LIGHT_BLUE();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_YELLOW", stream) == 0)
	{
		value = Color::LIGHT_YELLOW();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_CYAN", stream) == 0)
	{
		value = Color::LIGHT_CYAN();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_MAGENTA", stream) == 0)
	{
		value = Color::LIGHT_MAGENTA();
		return true;
	}
	else if (red::StrcmpNC("LIGHT_GRAY", stream) == 0)
	{
		value = Color::LIGHT_GRAY();
		return true;
	}
	else if (red::StrcmpNC("DARK_RED", stream) == 0)
	{
		value = Color::DARK_RED();
		return true;
	}
	else if (red::StrcmpNC("DARK_GREEN", stream) == 0)
	{
		value = Color::DARK_GREEN();
		return true;
	}
	else if (red::StrcmpNC("DARK_BLUE", stream) == 0)
	{
		value = Color::DARK_BLUE();
		return true;
	}
	else if (red::StrcmpNC("DARK_YELLOW", stream) == 0)
	{
		value = Color::DARK_YELLOW();
		return true;
	}
	else if (red::StrcmpNC("DARK_CYAN", stream) == 0)
	{
		value = Color::DARK_CYAN();
		return true;
	}
	else if (red::StrcmpNC("DARK_MAGENTA", stream) == 0)
	{
		value = Color::DARK_MAGENTA();
		return true;
	}
	else if (red::StrcmpNC("DARK_GRAY", stream) == 0)
	{
		value = Color::DARK_GRAY();
		return true;
	}
	else if (red::StrcmpNC("BROWN", stream) == 0)
	{
		value = Color::BROWN();
		return true;
	}
	else if (red::StrcmpNC("GRAY", stream) == 0)
	{
		value = Color::GRAY();
		return true;
	}
	else if (red::StrcmpNC("NORMAL", stream) == 0)
	{
		value = Color::NORMAL();
		return true;
	}
	else if (red::StrcmpNC("CLEAR", stream) == 0)
	{
		value = Color::CLEAR();
		return true;
	}

	return false;
}

Bool GParseGuid(const AnsiChar*& stream, CGUID& ret)
{
	// ObjectID is a guid in []
	if (GParseKeyword(stream, "["))
	{
		// parse the GUID
		CGUID val;
		if (GParseHex(stream, val.parts.A) && GParseKeyword(stream, "-")
			&& GParseHex(stream, val.parts.B) && GParseKeyword(stream, "-")
			&& GParseHex(stream, val.parts.C) && GParseKeyword(stream, "-")
			&& GParseHex(stream, val.parts.D))
		{
			// parse ending bracket
			if (GParseKeyword(stream, "]"))
			{
				ret = val;
				return true;
			}
		}
	}

	// not parsed/not valid
	return false;
}