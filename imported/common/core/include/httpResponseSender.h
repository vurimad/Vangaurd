/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//---------------------------------

#include "basicDataBlob.h"

//---------------------------------

/// HTTP helper used to send data through network, supports partial data sending
class CHTTPResponseSender : public red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	enum EWebSocket
	{
		eWebSocket,
	};

public:
	explicit CHTTPResponseSender( const Uint32 errorCode ); // for raw errors
	CHTTPResponseSender( DataBlobPtr contentData, const String& contentType, const Bool compress = true, const Bool allowCaching = false ); // for data
	CHTTPResponseSender( EWebSocket, const Uint32 responseCode, const String& secWebSocketKey, const String& webSocketProtocol, const String& secWebSocketVersion );
	~CHTTPResponseSender();

	// Do we have actual content ?
	const Bool HasContent() const { return m_data && (m_data->GetDataSize() > 0); }

	// Get data to send, returns 0 if there's no more data to send
	void GetDataToSend( const void*& outDataPtr, Uint32& outDataSize ) const;

	// Advance state by the amount of that that was actually sent, returns true when finished
	const Bool AdvanceState( const Uint32 numBytesActuallySent );

	RED_INLINE Bool IsWebSocketUpgrade() const { return m_isWebSocketUpgrade; }

private:
	// binary version of HTTP response header + payload (may be empty)
	String				m_header;
	DataBlobPtr			m_data;

	// current write pointer
	Uint32				m_pos;
	Bool				m_isWebSocketUpgrade;
};

//---------------------------------
