/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redContainers/include/map.h"

//--------------------------------------

/// HTTP Date/Time format
class CHTTPDate
{
public:
	Uint16				m_year;
	Uint8				m_month;
	Uint8				m_day;
	Uint8				m_hour;
	Uint8				m_min;
	Uint8				m_sec;

public:
	CHTTPDate();
	CHTTPDate( const Uint32 year, const Uint32 month, const Uint32 day, const Uint32 hour, const Uint32 min, const Uint32 sec );
	CHTTPDate( const red::DateTime& time );

	// Is this a valid date ?
	const Bool IsValid() const;

	// Parse from string
	static CHTTPDate Parse( const String& txt );

	// Convert to system DateTime
	const red::DateTime ToSystem() const;

	// Convert to string (HTTP protocol standard)
	const String ToString() const;
};

//---------------------------------

/// HTTP Request (http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html)
class CHTTPRequest
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	typedef red::Map< String, String >		TCookies;
	typedef red::Map< String, String >		TParams;
	typedef red::DynArray< String >			TAcceptedData;

	// method + version
	String m_method;
	String m_version;
	String m_url;
	String m_host;

	// cookies
	TCookies m_cookies{ red::PoolDebug() };

	// accepted content types
	TAcceptedData m_accept{ red::PoolDebug() };

	// client information
	String m_userAgent;
	Bool m_keepAlive;

	// custom content (POST mostly)
	String m_contentType;
	Uint32 m_contentLength;
	red::DynArray< Uint8 > m_contentData{ red::PoolDebug() }; // may not be allocated

	// websocket upgrade
	Bool m_websocketUpgrade;
	String m_connection;
	String m_origin;
	String m_secWebSocketKey;
	String m_secWebSocketVersion;
	red::DynArray< String > m_secWebSocketProtocols{ red::PoolDebug() };
	red::DynArray< String >	m_secWebSocketExtensions{ red::PoolDebug() }; // #todo: permessage-deflate

	// all header parameters
	TParams	m_params{ red::PoolDebug() };

public:
	CHTTPRequest();

	// parse request from text buffer, returns true if successful
	const Bool Parse( const AnsiChar* txt );

private:
	// message delimiters
	static const AnsiChar SYMBOL_ARGS;
	static const AnsiChar SYMBOL_KEY;
	static const AnsiChar SYMBOL_CR;
	static const AnsiChar SYMBOL_LF;

	// parsing helpers
	static Bool SkipWhitespace( const AnsiChar*& txt );
	static Bool ParseText( const AnsiChar*& txt, String& outText, const AnsiChar aditionalDelimiter = 0 );
	static Bool ParseLineEnd( const AnsiChar*& txt );

	// process header parameter
	void ProcessHeaderParam( const String& name, const String& value );
};

//---------------------------------

/// HTTP Response (http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html)
class CHTTPResponse
{
public:
	CHTTPResponse();

	// setup response code
	void SetResponseCode( const Uint32 code );

	// setup content type and size
	void SetContent( const String& type, const String& encoding, const Uint32 size );

	void SetWebsocketInfo( const String& acceptNonce, const String& protocol, const String& requiredVersion );

	// format text data
	const String ToString() const;

private:
	String m_version;
	String m_server;
	CHTTPDate m_date;
	Uint32 m_code;

	// cookies
	String m_cookies;

	// params
	String m_cacheControl;	
	Bool m_keepAlive;

	// basic flags
	Uint32 m_contentSize;
	String m_contentEncoding;
	String m_contentType;

	// websockets
	String m_websocketsAcceptNonce;
	String m_websocketsProtocol;
	String m_websocketsExtensions;
	String m_websocketsRequiredVersion;
	Bool m_isWebSocket;

	// get name for response code
	static const AnsiChar* GetResponseName( const Uint32 code );
};