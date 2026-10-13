/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if !defined( NO_DEBUG_PAGES )

#include "basicURL.h"
#include "basicDataBlob.h"
#include "webSocketEndpoint.h"
#include "../../resource/include/jsonFileHelper.h"

/// Service command handler
class RED_CORE_API IDebugPageHandler
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	IDebugPageHandler( const String& basicURL, const Bool autoRegister = true );
	virtual ~IDebugPageHandler();

	//! Get debug page title
	virtual String GetTitle() const = 0;

	//! Get debug page category
	virtual String GetCategory() const = 0;

	//! Do we have an index page ? (if so we are directly available in the debug menu)
	virtual Bool HasIndexPage() const { return false; }

	//! Process POST request - change the state of engine, get value of variable, etc, NO PAGE REFRESH
	virtual class CHTTPResponseData* OnCommand( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl, const String& content );

	//! Process GET command - display a page or sth, commands are always passed via URLs, return NULL if not handled
	virtual class CHTTPResponseData* OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) = 0;

	//! Get a WebSocket endpoint. Note that the URL protocol for WebSockets is 'ws', not 'http'. So ws://some/url vs http://some/url
	//! Return NULL if not handled.
	virtual WebSocketEndpoint* QueryWebSocketEndpoint( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) const;
};

/// Service command handler that always outputs HTML window
class RED_CORE_API IDebugPageHandlerHTML : public IDebugPageHandler
{
public:
	IDebugPageHandlerHTML( const String& basicURL, const Bool autoRegister = true );
	virtual ~IDebugPageHandlerHTML() {};

	// Fill the HTML result page
	virtual Bool OnFillPage( const class CBasicURL& fullURL, const class CBasicURL& relativeURL, class CDebugPageHTMLDocument& doc ) = 0;

private:
	//! Process command, commands are always passed via URLs, return NULL if not handled
	virtual class CHTTPResponseData* OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) override;
};

class RED_CORE_API IDebugPageHandlerJSON : public IDebugPageHandler
{
public:
	IDebugPageHandlerJSON( const String& basicURL, const Bool autoRegister = true );
	virtual ~IDebugPageHandlerJSON() {};

	// Fill the JSON data
	virtual Bool OnFillData( const class CBasicURL& fullURL, const class CBasicURL& relativeURL, JSONFileHelper::JSONCompactWriter& json ) = 0;

private:
	//! Process command, commands are always passed via URLs, return NULL if not handled
	virtual class CHTTPResponseData* OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) override;
};

class RED_CORE_API IDebugPageHandlerJSONWebSocket : public IDebugPageHandler, public WebSocketEndpoint
{
public:
	IDebugPageHandlerJSONWebSocket( const String& basicURL, const Bool autoRegister = true );
	virtual ~IDebugPageHandlerJSONWebSocket() {};
public:
	virtual String GetTitle() const override final { return ""; }
	virtual String GetCategory() const override final { return ""; }
	virtual Bool HasIndexPage() const override final { return false; }
	virtual class CHTTPResponseData* OnCommand( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl, const String& content ) override final { return nullptr; }
	virtual class CHTTPResponseData* OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) override final { return nullptr; }
	virtual WebSocketEndpoint* QueryWebSocketEndpoint( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) const override final;
	virtual void OnReadPayload( EWebSocketPayloadType type, IFile& reader )  override final;
	virtual EWebSocketPayloadType OnWritePayload( IFile& writer ) override final;

protected:
	// Fill the JSON data
	virtual Bool OnFillData( JSONFileHelper::JSONCompactWriter& json ) = 0;
	virtual Bool OnReadData( IFile& reader ) = 0;
};

#endif
