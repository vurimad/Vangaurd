/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathVector2.h"

Bool ToString( red::String& outTxt, const Vector2& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToString( xAsString, val.X ) &&
		::ToString( yAsString, val.Y ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}

Bool ToStringMaxPrecision( red::String& outTxt, const Vector2& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToStringMaxPrecision( xAsString, val.X ) &&
		::ToStringMaxPrecision( yAsString, val.Y ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}