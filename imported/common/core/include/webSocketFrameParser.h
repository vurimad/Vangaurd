/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "webSocketFrame.h"

namespace websocketsHelper
{
	class DataStreamGuard;
}

class CWebSocketFrameParser
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	enum class EResult
	{
		Continue,
		Invalid,
		TooLarge,
		Cancel,
	};

public:
	struct CallbackToken
	{
		typedef EResult( *OnWebsocketDataFrame )( CallbackToken& token, const WebSocketHeaderInfo& headerInfo );
		typedef EResult( *OnWebsocketControlFrame )( CallbackToken& token, const WebSocketHeaderInfo& headerInfo, const void* unmaskedPayload );

		CallbackToken()
			: m_onDataFrame( nullptr )
			, m_onControlFrame( nullptr )
			, m_userArg( nullptr )
			, m_dataPayloadBuffer( nullptr )
			, m_dataPayloadBufferSize( 0 )
		{
		}

		OnWebsocketDataFrame m_onDataFrame;
		OnWebsocketControlFrame m_onControlFrame;
		void* m_userArg;
		Uint8* m_dataPayloadBuffer;
		Uint64 m_dataPayloadBufferSize;
	};

	typedef EResult( *OnWebsocketDataFrame )( CallbackToken& token, const WebSocketHeaderInfo& headerInfo );
	typedef EResult( *OnWebsocketControlFrame )( CallbackToken& token, const WebSocketHeaderInfo& headerInfo, const void* unmaskedPayload );

	explicit CWebSocketFrameParser( const CallbackToken& token );

	EResult Decode( const void* data, Uint32 size );

private:
	struct PayloadState
	{
		PayloadState()
			: m_opcode( 0 )
			, m_rsv1( false )
			, m_rsv2( false )
			, m_rsv3( false )
			, m_decodeBufferPos( 0 )
			, m_bufferDefragPos( 0 )
			, m_isFragmented( false )
		{}

		Uint8	m_opcode;
		Bool	m_rsv1;
		Bool	m_rsv2;
		Bool	m_rsv3;
		Uint32	m_decodeBufferPos;
		Uint32  m_bufferDefragPos;
		Bool	m_isFragmented;
	};

	struct BufferState
	{
		BufferState()
			: m_decodeBufferPos( 0 )
		{}

		Uint32 m_decodeBufferPos;
	};
	
	// Note: control frames may be interjected between fragmented data frames.
	// So maintain separate data and control frame payloads.
	CallbackToken   m_callbackToken;
	BufferState		m_headerState;
	PayloadState	m_payloadState;
	BufferState		m_controlFramePayloadState;
	Uint8 m_headerDecodeBuffer[ WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES + WebSocketHeaderInfo::MASKING_KEY_BYTES ];
	Uint8 m_controlFramePayloadDecodeBuffer[ WebSocketHeaderInfo::SMALL_PAYLOAD_MAX ];
	Bool m_decodeErrorFlag;

private:
	Bool DecodeHeader( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& outHeaderInfo, WebSocketMaskingKey& outMaskingKey );
	Bool DecodePayload( websocketsHelper::DataStreamGuard& stream, const WebSocketMaskingKey& key, Uint32 payloadLength );
	Bool DecodeControlPayload( websocketsHelper::DataStreamGuard& stream, const WebSocketMaskingKey& key, Uint32 payloadLength );

private:
	EResult ProcessControlFrame( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& headerInfo, const WebSocketMaskingKey& maskingKey );
	EResult ProcessDataFrame( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& headerInfo, const WebSocketMaskingKey& maskingKey );
};
