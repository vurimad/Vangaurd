/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if !defined( NO_DEBUG_PAGES )

#include "../../../common/redContainers/include/staticArray.h"
#include "basicURL.h"
#include "basicDataBlob.h"
#include "debugPageHandler.h"
#include "httpRequestHandler.h"

/// Service command system
class RED_CORE_API CDebugPageServer : public red::NonCopyable, public IHTTPRequestHandler
{
public:
	// Get instance of the command server
	static CDebugPageServer& GetInstance();

	// Initialize server - creates the network listener and the processing thread
	Bool Initialize();

	// Shutdown system - closes the network
	void Shutdown();

	// Register debug page
	void RegisterDebugPage( const String& basePath, IDebugPageHandler* handler );

	// Unregister debug page
	void UnregisterDebugPage( IDebugPageHandler* handler );

	// Process commands, called in Tick from main thread
	void ProcessServiceCommands( Float timeDelta );

public:
	struct DebugInfo
	{
		Uint16	m_port;
		Bool	m_running;
	};

	void GetDebugInfo( DebugInfo& outInfo ) const;

protected:
	CDebugPageServer();
	~CDebugPageServer();

	static const Uint32				MAX_HANDLERS = 128;

	struct Handler
	{
		String					m_basePath;
		IDebugPageHandler*		m_handler;
	};

	class CHTTPServer*							m_server;

	red::Mutex									m_lock;
	red::StaticArray< Handler, MAX_HANDLERS >	m_handlers;

	// IHTTPRequestHandler interface
	virtual Bool OnHTTPRequest( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) override;

	// Handle special pages
	class CHTTPResponseData* HandleIndexPage( const CBasicURL& fullURL );

private:
	Uint16 m_port;
};

#endif
