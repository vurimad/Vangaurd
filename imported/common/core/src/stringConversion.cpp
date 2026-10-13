/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../redContainers/include/containersTypesFormatters.h"
#include "stringConversion.h"
#include "../../redReflection/include/stringParser.h"

#include "../../redSystem/include/guid.h"


//////////////////////////////////////////////////////////////////////////
// CName
const Bool ToString( red::String& outTxt, const CName& val, const char* customFormat )
{
	// init
	AnsiChar formattedBuffer[ 512 ];
	Int32 written = 0;

	if ( red::ToBuffer( formattedBuffer, sizeof( formattedBuffer ), val, written, customFormat ? customFormat : red::GetFormatString( val ) ) )
	{
		outTxt.Set( formattedBuffer, written );
		return true;
	}
	return false;
}

const Bool FromString( const String& txt, CName& outVal )
{
	outVal = RED_NAME( txt );
	return true;
}
