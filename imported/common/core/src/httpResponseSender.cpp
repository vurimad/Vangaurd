/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "httpHeader.h"
#include "httpResponseData.h"
#include "httpResponseSender.h"
#include "../../redCompression/include/compression.h"
#include "../../redCompression/include/resultBuffer.h"

//-----

CHTTPResponseSender::CHTTPResponseSender( const Uint32 code )
	: m_pos( 0 )
	, m_isWebSocketUpgrade( false )
{
	// setup HTTP response header
	CHTTPResponse response;
	response.SetResponseCode( code );

	// build response string
	m_header = response.ToString();
	m_header += "\r\n"; // extra empty line
}

CHTTPResponseSender::CHTTPResponseSender( DataBlobPtr contentData, const String& contentType, const Bool compress /*= true*/, const Bool allowCaching /*= false*/ )
	: m_pos( 0 )
	, m_isWebSocketUpgrade( false )
{
	CHTTPResponse response;

	// setup data
	if ( compress )
	{
		// HTTP thing: compress data using the RAW zlib, memory will be allocated from the compression pool
		const auto compressedData = compression::CompressData( compression::CT_ZlibRaw, contentData->GetData(), contentData->GetDataSize(), compression::GetDefaultCompressionAllocator() );
		if ( compressedData != nullptr )
		{
			// build new data for sending
			m_data.Reset( RED_NEW( CDataBlob )( compressedData->GetData(), (const Uint32) compressedData->GetDataSize() ) ); // TODO: we can save one memory copy here
			red::Memcpy( m_data->GetData(), compressedData->GetData(), compressedData->GetDataSize() );

			// setup request data
			response.SetContent( contentType, "deflate", m_data->GetDataSize() );
			response.SetResponseCode( 200 );
		}
		else
		{
			// compression was not valid
			response.SetResponseCode( 500 );
		}
	}
	else
	{
		// setup request data using raw input data
		response.SetContent( contentType, "identity", contentData->GetDataSize() );
		response.SetResponseCode( 200 );

		// use the data directly
		m_data = contentData;
	}

	// setup HTTP response header
	m_header = response.ToString();
	m_header += "\r\n"; // extra empty line
}

CHTTPResponseSender::CHTTPResponseSender( EWebSocket, const Uint32 responseCode, const String& secWebSocketKey, const String& webSocketProtocol, const String& secWebSocketVersion )
	: m_pos( 0 )
	, m_isWebSocketUpgrade( false )
{
	if ( responseCode == 101 ) // switching protocols, otherwise failed handshake
	{
		m_isWebSocketUpgrade = true;
	}

	// setup HTTP response header
	CHTTPResponse response;
	response.SetResponseCode( responseCode );
	response.SetWebsocketInfo( secWebSocketKey, webSocketProtocol, secWebSocketVersion );

	// build response string
	m_header = response.ToString();
	m_header += "\r\n"; // extra empty line
}

CHTTPResponseSender::~CHTTPResponseSender()
{
}

void CHTTPResponseSender::GetDataToSend( const void*& outDataPtr, Uint32& outDataSize ) const
{
	const Uint32 headerLen = m_header.Length();
	const Uint32 dataSize = m_data ? m_data->GetDataSize() : 0;

	// header data ?
	if ( m_pos < headerLen )
	{
		const Uint32 maxToSend = (headerLen - m_pos);
		outDataPtr = m_header.AsChar() + m_pos;
		outDataSize = maxToSend;
	}
	// payload data ?
	else if ( dataSize > 0 )
	{
		const Uint32 offsetInData = m_pos - headerLen;
		const Uint32 maxToSend = (dataSize - offsetInData);
		outDataPtr = (const Uint8*) m_data->GetData() + offsetInData;
		outDataSize = maxToSend;
	}
	// error
	else
	{
		outDataPtr = nullptr;
		outDataSize = 0;
	}
}

const Bool CHTTPResponseSender::AdvanceState( const Uint32 numBytesActuallySent )
{
	// advance position
	m_pos += numBytesActuallySent;

	// all data sent ?
	const Uint32 headerLen = m_header.Length();
	const Uint32 dataSize = m_data ? m_data->GetDataSize() : 0;
	return ( m_pos >= ( dataSize + headerLen ) );
}

//-----
