/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include "coreApi.h"


//////////////////////////////////////////////////////////////////////////
// forwards
struct Color;
class CName;

namespace red
{
    class String;
}

// to/from string
extern RED_CORE_API const Bool ToString( red::String& outTxt, const CName& val, const char* customFormat = nullptr );
extern RED_CORE_API const Bool FromString( const red::String& txt, CName& outVal );

#include "../../redContainers/include/fundamentalStringConversion.h"
