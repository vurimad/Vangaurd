/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugPageHandler.h"

#if !defined( NO_DEBUG_PAGES )

#include "debugPageServer.h"
#include "debugPageHTMLDoc.h"
#include "httpResponseData.h"
#include "basicURL.h"
#include "basicDataBlob.h"
#include "../../redFileSystem/include/memoryFileWriter.h"

//-------

IDebugPageHandler::IDebugPageHandler( const String& basicURL, const Bool autoRegister /*= true*/ )
{
	if ( autoRegister )
	{
		CDebugPageServer::GetInstance().RegisterDebugPage( basicURL, this );
	}
}

IDebugPageHandler::~IDebugPageHandler()
{
	CDebugPageServer::GetInstance().UnregisterDebugPage( this );
}

class CHTTPResponseData* IDebugPageHandler::OnCommand( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl, const String& content )
{
	// command not implemented
	return RED_NEW( CHTTPResponseData )( CHTTPResponseData::eResultCode_NotImplemented );
}

class WebSocketEndpoint* IDebugPageHandler::QueryWebSocketEndpoint( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) const
{
	return nullptr;
}
//-------

IDebugPageHandlerHTML::IDebugPageHandlerHTML( const String& basicURL, const Bool autoRegister /*= true*/ )
	: IDebugPageHandler( basicURL, autoRegister )
{
}

class CHTTPResponseData* IDebugPageHandlerHTML::OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl )
{
	CDebugPageHTMLResponse response( fullURL );
	{
		CDebugPageHTMLDocument doc( response, GetTitle() );
		if (!OnFillPage( fullURL, relativeUrl, doc ))
			return RED_NEW( CHTTPResponseData )( CHTTPResponseData::eResultCode_NotImplemented );
	}

	return response.GetData();
}

//-------

IDebugPageHandlerJSON::IDebugPageHandlerJSON( const String& basicURL, const Bool autoRegister /*= true*/ )
	: IDebugPageHandler( basicURL, autoRegister )
{
}

class CHTTPResponseData* IDebugPageHandlerJSON::OnPage( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl )
{
	// #tbd: debug memory allocator. For now better than just churning through memory the dynamic allocator, although the 
	// returned blob still could
	char payload[ 2*65536 ];
	char writerPool[ 1024 ];
	CMemoryFileWriterExternalBuffer file( payload, sizeof( payload ) );
	JSONFileHelper::JSONFileStream stream( &file );
	rapidjson::MemoryPoolAllocator< JSONFileHelper::FixedAllocator > pool( writerPool, sizeof( writerPool ), sizeof( writerPool ) );
	JSONFileHelper::JSONCompactWriter json( stream, &pool );
	if (!OnFillData( fullURL, relativeUrl, json ))
	{
		return RED_NEW( CHTTPResponseData )( CHTTPResponseData::eResultCode_NotImplemented );
	}
	stream.Flush();
	const Uint32 size = file.GetRealSize();
	return RED_NEW( CHTTPResponseData )( "application/json", red::CreateSharedPtr< CDataBlob >( payload, file.GetRealSize() ) );
}

IDebugPageHandlerJSONWebSocket::IDebugPageHandlerJSONWebSocket( const String& basicURL, const Bool autoRegister /*= true */ )
	: IDebugPageHandler( basicURL, autoRegister )
{
}

WebSocketEndpoint* IDebugPageHandlerJSONWebSocket::QueryWebSocketEndpoint( const class CBasicURL& fullURL, const class CBasicURL& relativeUrl ) const
{
	return const_cast< WebSocketEndpoint* >( static_cast< const WebSocketEndpoint* >( this ) );
}

void IDebugPageHandlerJSONWebSocket::OnReadPayload( EWebSocketPayloadType tyoe, IFile& reader )
{
	OnReadData( reader );
}

WebSocketEndpoint::EWebSocketPayloadType IDebugPageHandlerJSONWebSocket::OnWritePayload( IFile& writer )
{
	// #tbd: debug memory allocator. For now better than just churning through memory the dynamic allocator, although the 
	// returned blob still could
	char writerPool[ 1024 ];
	JSONFileHelper::JSONFileStream stream( &writer );
	rapidjson::MemoryPoolAllocator< JSONFileHelper::FixedAllocator > pool( writerPool, sizeof( writerPool ), sizeof( writerPool ) );
	JSONFileHelper::JSONCompactWriter json( stream, &pool );
	OnFillData( json );
	stream.Flush();
	return EWebSocketPayloadType::TextUtf8;
};

#else
RED_NO_EMPTY_FILE();
#endif
