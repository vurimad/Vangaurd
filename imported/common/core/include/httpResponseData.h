
/**
* Copyright (c) 2007-19 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "basicDataBlob.h"

//---------------------------------

/// HTTP response data - raw data, no header
class RED_CORE_API CHTTPResponseData : public red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	enum EResultCode
	{
		eResultCode_SwitchingProtocols = 101,
		eResultCode_OK = 200,
		eResultCode_BadRequest = 400,
		eResultCode_Forbidden = 403,
		eResultCode_NotFound = 404,
		eResultCode_UpgradeRequired = 426,
		eResultCode_ServerError = 500,
		eResultCode_NotImplemented = 501,
	};

	CHTTPResponseData( const String& contentType, DataBlobPtr data ); // assumes eResultCode_OK
	CHTTPResponseData( const EResultCode code );
	CHTTPResponseData( const EResultCode code, class WebSocketEndpoint& endpoint );
	~CHTTPResponseData();

	// Get result code
	RED_INLINE const EResultCode GetResultCode() const { return m_resultCode; }

	// Get content type
	RED_INLINE const String& GetContentType() const { return m_contentType; }

	// Get response data
	RED_INLINE DataBlobPtr GetData() const { return m_data; }

	RED_INLINE class WebSocketEndpoint* GetWebSocketEndpoint() const { return m_webSocketEndpoint; }

private:
	EResultCode				m_resultCode;
	String					m_contentType;
	DataBlobPtr				m_data;
	class WebSocketEndpoint* m_webSocketEndpoint;
};

using HTTPResponseDataPtr = red::UniquePtr< CHTTPResponseData >;
//---------------------------------
