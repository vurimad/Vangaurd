/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

namespace rapidxml
{
	thread_local static Bool tl_hasError = false;

	RED_RESOURCE_API Bool HasError()
	{
		return tl_hasError;
	}

	RED_RESOURCE_API void ResetErrors()
	{
		tl_hasError = false;
	}

	RED_RESOURCE_API void parse_error_handler( const char* what, void* where )
	{
		tl_hasError = true;

		if ( where )
		{
			String text = static_cast< char* >( where );
			RED_LOG_ERROR( "XML Parsing error: '%hs' near '%hs'", what, text.LeftString(100).AsChar() );
		}
		else
		{
			RED_LOG_ERROR( "XML Parsing error: '%hs'", what );
		}
	}
}
