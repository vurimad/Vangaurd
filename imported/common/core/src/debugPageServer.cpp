/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugPageServer.h"

#if !defined( NO_DEBUG_PAGES )

#include "../../redFileSystem/include/fileSys.h"
#include "debugPageHandler.h"
#include "debugPageHTMLDoc.h"

#include "httpHeader.h"
#include "httpServer.h"
#include "httpResponseData.h"
#include "basicURL.h"
#include "basicDataBlob.h"
#include "webSocketEndpoint.h"

using red::DynArray;

namespace Config
{
	TConfigVar< Bool >		cvDebugPagesEnabled("DebugPages", "Enabled", true, eConsoleVarFlag_Developer );
	TConfigVar< Int32 >		cvDebugPagesPort( "DebugPages", "MainPort", 37010, 100, 65535, eConsoleVarFlag_Developer | eConsoleVarFlag_ReadOnly );
	TConfigVar< Int32 >		cvDebugPagesNumPorts( "DebugPages", "NumPorts", 100, 1, 65535, eConsoleVarFlag_Developer | eConsoleVarFlag_ReadOnly );
	TConfigVar< String >	cvDebugPagesStaticContent( "DebugPages", "StaticContentDir", "debug\\http\\", eConsoleVarFlag_Developer | eConsoleVarFlag_ReadOnly );
}

CDebugPageServer::CDebugPageServer()
	: m_server( nullptr )
	, m_port( 0 )
{
}

CDebugPageServer::~CDebugPageServer()
{
}

CDebugPageServer& CDebugPageServer::GetInstance()
{
	static CDebugPageServer theInstance;
	return theInstance;
}

Bool CDebugPageServer::Initialize()
{
	// initialize only once
	if ( !m_server && Config::cvDebugPagesEnabled.Get())
	{
		m_server = RED_NEW( CHTTPServer )();

		// register debug page system as HTTP request handler
		m_server->RegisterHandler( this );

		// determine the directory with static content (files that are always there)
		const red::AbsolutePath contentDir = GFileManager->GetEngineRoot().AddDirPath( Config::cvDebugPagesStaticContent.Get() );

		// create HTTP server that will service the debug requests
		// Save the used port for debug info in case the config var is later changed
		m_port = static_cast< Uint16 >( Config::cvDebugPagesPort.Get() );
		const Uint32 numPorts = Config::cvDebugPagesNumPorts.Get();

		if ( !m_server->Initialize( m_port, numPorts, contentDir ) )
		{
			RED_DELETE( m_server );
			m_server = nullptr;

			RED_LOG_ERROR( "Core: Failed to initialize service handler at any port" );
			return false;
		}

		// set a initialized
		RED_LOG( "Core: Initialized service handler at port %d", m_port );
	}

	// initialized
	return true;
}

void CDebugPageServer::Shutdown()
{
	if ( m_server )
	{
		// unregister
		m_server->UnregisterHandler( this );

		// close HTTP server
		m_server->Shutdown();
		RED_DELETE( m_server );
		m_server = nullptr;
	}
}

void CDebugPageServer::ProcessServiceCommands( Float timeDelta )
{
	if ( m_server )
	{
		m_server->Update( timeDelta );
	}
}

void CDebugPageServer::GetDebugInfo( DebugInfo& outInfo ) const
{
	outInfo.m_running = m_server != nullptr;
	outInfo.m_port = m_port;
}

void CDebugPageServer::RegisterDebugPage( const String& basePath, IDebugPageHandler* handler )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// register handler
	if ( !m_handlers.Full() )
	{
		Handler info;
		info.m_basePath = basePath;
		info.m_handler = handler;

		// make sure base path ends with "/"
		if ( !info.m_basePath.EndsWith( "/" ) )
			info.m_basePath += "/";

		m_handlers.PushBack( info );
	}
}

void CDebugPageServer::UnregisterDebugPage( IDebugPageHandler* handler )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	for ( auto it = m_handlers.Begin(); it != m_handlers.End(); ++it )
	{
		if ( it->m_handler == handler )
		{
			it->m_handler = nullptr;
		}
	}
}

Bool CDebugPageServer::OnHTTPRequest( const class CHTTPRequest& request, const OnHTTPResponse& onResponse )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	HTTPResponseDataPtr responseData;

	// we only support POST/GET in here
	if ( request.m_method != "GET" && request.m_method != "POST" )
	{
		responseData = red::CreateUniquePtr< CHTTPResponseData >( CHTTPResponseData::eResultCode_BadRequest );
	}
	else
	{
		// create full URL by parsing the requested address
		CBasicURL fullURL = CBasicURL::Parse( request.m_url, true );
		fullURL.SetProtocol( request.m_websocketUpgrade ? "ws" : "http" );
		fullURL.SetAddress( request.m_host );

		// process handlers
		const Bool isPostRequest = (request.m_method == "POST");
		for ( auto it = m_handlers.Begin(); it != m_handlers.End(); ++it )
		{
			// skip unregistered ones
			if ( !it->m_handler )
			{
				continue;
			}

			// valid request path ?
			if ( !request.m_url.BeginsWith( it->m_basePath ) )
			{
				continue;
			}

			// create relative URL string (with host name) and split it into parts
			const String relativePath = request.m_url.StringAfter( it->m_basePath );
			const CBasicURL relativeURL = CBasicURL::Parse( relativePath, /*relative*/ true );

			if ( request.m_websocketUpgrade )
			{
				auto wsSource = it->m_handler->QueryWebSocketEndpoint( fullURL, relativeURL );
				if ( !wsSource )
				{
					responseData = red::CreateUniquePtr< CHTTPResponseData >( CHTTPResponseData::eResultCode_NotFound );
					break;
				}

				responseData = red::CreateUniquePtr< CHTTPResponseData >( CHTTPResponseData::eResultCode_SwitchingProtocols, *wsSource );
				break;
			}

			// allow the command handler to service the request
			if ( isPostRequest )
			{
				const String contentString( (const AnsiChar*)request.m_contentData.TypedData(), request.m_contentLength );
				responseData.Reset( it->m_handler->OnCommand( fullURL, relativeURL, contentString ) );
				break;
			}
			else
			{
				responseData.Reset( it->m_handler->OnPage( fullURL, relativeURL ) );
				break;
			}
		}

		// HACK: index page
		if ( !responseData && ( request.m_url == "/" || request.m_url == "/index.html" ) )
		{
			responseData.Reset( HandleIndexPage( fullURL ) );
		}
	}

	if ( responseData )
	{
		const auto resultCode = responseData->GetResultCode();
		onResponse( std::move( responseData ), resultCode );
	}
	else
	{
		// no handlers found
		responseData = red::CreateUniquePtr< CHTTPResponseData >( CHTTPResponseData::eResultCode_NotFound );
		onResponse( std::move( responseData ), CHTTPResponseData::eResultCode_NotFound );
	}

	return true;
}

class CHTTPResponseData* CDebugPageServer::HandleIndexPage( const CBasicURL& fullURL )
{
	CDebugPageHTMLResponse response( fullURL );
	{
		CDebugPageHTMLDocument doc( response, "RedEngine Runtime Debug System" );
		doc << "<p>Welcome in the arcane debug, select your weapon:</p>";

		// list of of the registered debug page handlers
		{
			red::ScopedLock< red::Mutex > lock( m_lock );

			// collect categories
			DynArray< String > categories{ red::PoolDebug() };
			for ( auto it = m_handlers.Begin(); it != m_handlers.End(); ++it )
			{
				if ( it->m_handler && it->m_handler->HasIndexPage() )
				{
					red::alg::PushBackUnique( categories, it->m_handler->GetCategory() );
				}
			}

			// sort categories by name
			std::sort( categories.Begin(), categories.End() );

			// show debug groups with links to index pages of each debug page
			for ( const String& debugCategory : categories )
			{
				// category 
				doc << "<span class=\"debugcategory\">";
				doc << debugCategory;
				doc << "</span>";

				// elements
				doc << "<ul class=\"debuglist\">";

				for ( auto it = m_handlers.Begin(); it != m_handlers.End(); ++it )
				{
					if ( it->m_handler && it->m_handler->HasIndexPage() && it->m_handler->GetCategory() == debugCategory )
					{
						doc.Open( "li" ).Doc().Link( "%hs", it->m_basePath.AsChar()+1 ).Write( it->m_handler->GetTitle().AsChar() );
					}
				}

				doc << "</ul>";
			}
		}
	}
	return response.GetData();
}

#else
RED_NO_EMPTY_FILE();
#endif
