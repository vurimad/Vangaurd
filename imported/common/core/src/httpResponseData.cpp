/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "httpResponseData.h"

CHTTPResponseData::CHTTPResponseData( const String& contentType, DataBlobPtr data )
	: m_resultCode( eResultCode_OK )
	, m_data( data )
	, m_contentType( contentType )
	, m_webSocketEndpoint( nullptr )
{
}

CHTTPResponseData::CHTTPResponseData( const EResultCode code )
	: m_resultCode( code )
	, m_webSocketEndpoint( nullptr )
{
}

CHTTPResponseData::CHTTPResponseData( const EResultCode code, class WebSocketEndpoint& endpoint )
	: m_resultCode( code )
	, m_webSocketEndpoint( &endpoint )
{
}

CHTTPResponseData::~CHTTPResponseData()
{
}
