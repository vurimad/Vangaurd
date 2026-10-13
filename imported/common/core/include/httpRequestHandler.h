/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "httpResponseData.h"

//---------------------------------

using OnHTTPResponse = red::Function< void( HTTPResponseDataPtr response, CHTTPResponseData::EResultCode returnCode ) >;

/// HTTP request handler
class RED_CORE_API IHTTPRequestHandler
{
public:
	virtual ~IHTTPRequestHandler() {};

	// Handle request, called always from main thread in safe place. Returning true means that the request was handled by current handler.
	virtual Bool OnHTTPRequest( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) = 0;
};

//---------------------------------
