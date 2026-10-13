/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

struct WebSocketMaskingKey
{
	Uint8 m_data[ 4 ];
};

struct WebSocketHeaderInfo
{
	// See RFC 6455
	static const Uint32 SMALL_MESSAGE_HEADER_BYTES = 2;
	static const Uint32 MEDIUM_MESSAGE_HEADER_BYTES = 4;
	static const Uint32 LARGE_MESSAGE_HEADER_BYTES = 10;
	static const Uint32 MASKING_KEY_BYTES = 4;

	static const Uint32 SMALL_PAYLOAD_MAX = 125;
	static const Uint32 MEDIUM_PAYLOAD_MAX = 65535;
	static const Uint64 LARGE_PAYLOAD_MAX = INT64_MAX;

	enum EOpcode : Uint8
	{
		eOpcode_ContinuationFrame = 0x0,
		eOpcode_TextFrame = 0x1,
		eOpcode_BinaryFrame = 0x2,
		eOpcode_Close = 0x8,
		eOpcode_Ping = 0x9,
		eOpcode_Pong = 0xA,
	};

	enum ECloseCode
	{
		eCloseCode_NormalClose = 1000,
		eCloseCode_GoingAway = 1001,
		eCloseCode_ProtocolError = 1002,
		eCloseCode_UnacceptableData = 1003,
		eCloseCode_BadData = 1007,
		eCloseCode_PolicyViolation = 1008,
		eCloseCode_MessageTooBig = 1009,
		eCloseCode_ClientExtensionRequired = 1010,
		eCloseCode_ServerUnexpectedError = 1011,
	};

	Bool	m_final;
	Bool	m_rsv1;
	Bool	m_rsv2;
	Bool	m_rsv3;
	Uint8	m_opCode;
	Bool	m_mask;
	Uint64	m_payloadLength;
};

// Returns the packed header size, or zero on error
Uint32 PackWebSocketHeader( const WebSocketHeaderInfo& headerInfo, Uint8* buffer, Uint32 bufferSize );
