/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathVector3.h"

Bool ToString( red::String& outTxt, const Vector3& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToString( xAsString, val.X ) &&
		::ToString( yAsString, val.Y ) &&
		::ToString( zAsString, val.Z ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += " ";
		vecAsString += zAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}

Bool ToStringMaxPrecision( red::String& outTxt, const Vector3& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToStringMaxPrecision( xAsString, val.X ) &&
		::ToStringMaxPrecision( yAsString, val.Y ) &&
		::ToStringMaxPrecision( zAsString, val.Z ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += " ";
		vecAsString += zAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}