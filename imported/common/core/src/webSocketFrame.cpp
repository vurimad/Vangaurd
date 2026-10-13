/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "webSocketFrame.h"
#include "../../../common/redSystem/include/swapBytes.h"

namespace
{
	// See RFC 6455 section 5.2 "Base Framing Protocol" for WebSockets.
	const Uint8 FIN_BIT = 0x80;
	const Uint8 RSV1_BIT = 0x40;
	const Uint8 RSV2_BIT = 0x20;
	const Uint8 RSV3_BIT = 0x10;
	const Uint8 RSV123_MASK = RSV1_BIT | RSV2_BIT | RSV3_BIT;
	const Uint8 OPCODE_MASK = 0x0F;
	const Uint8 OPCODE_CONTROL_BIT = 0x08;
	const Uint8 MASK_BIT = 0x80;
	const Uint8 SMALL_PAYLOAD_MASK = 0x7F;
}

Uint32 PackWebSocketHeader( const WebSocketHeaderInfo& headerInfo, Uint8* buffer, Uint32 bufferSize )
{
	// Basic output buffer size validation
	if ( headerInfo.m_payloadLength > INT64_MAX )
	{
		RED_LOG_ERROR( "PackWebSocketHeader: payload length %lu exceeds max length INT64_MAX", headerInfo.m_payloadLength );
		return 0;
	}

	if ( headerInfo.m_payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX )
	{
		if ( WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES > bufferSize )
		{
			RED_LOG_ERROR( "PackWebSocketHeader: output buffer too small");
			return 0;
		}
	}
	else if ( headerInfo.m_payloadLength <= WebSocketHeaderInfo::MEDIUM_PAYLOAD_MAX )
	{
		if ( WebSocketHeaderInfo::MEDIUM_MESSAGE_HEADER_BYTES > bufferSize )
		{
			RED_LOG_ERROR( "PackWebSocketHeader: output buffer too small");
			return 0;
		}
	}
	else
	{
		if ( WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES > bufferSize )
		{
			RED_LOG_ERROR( "PackWebSocketHeader: output buffer too small");
			return 0;
		}
	}
	if ( (headerInfo.m_opCode & 0xF0) != 0)
	{
		RED_LOG_ERROR( "PackWebSocketHeader: invalid opcode %u", headerInfo.m_opCode );
		return 0;
	}

	Uint32 i = 0;
	Uint8* b = buffer;
	
	b[i] = 0;
	if ( headerInfo.m_final )
		b[i] |= FIN_BIT;
	if ( headerInfo.m_rsv1 )
		b[i] |= RSV1_BIT;
	if ( headerInfo.m_rsv2 )
		b[i] |= RSV2_BIT;
	if ( headerInfo.m_rsv3 )
		b[i] |= RSV3_BIT;
	b[i] |= headerInfo.m_opCode;

	i += 1;

	b[i] = 0;
	if ( headerInfo.m_mask )
		b[i] |= MASK_BIT;
	if ( headerInfo.m_payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX )
	{
		b[i] |= static_cast< Uint8 >( headerInfo.m_payloadLength );
		i += 1;
	}
	else if ( headerInfo.m_payloadLength <= WebSocketHeaderInfo::MEDIUM_PAYLOAD_MAX )
	{
		b[i] |= (WebSocketHeaderInfo::SMALL_PAYLOAD_MAX + 1);
		i += 1;
		*reinterpret_cast< Uint16* >( &b[i] ) = red::SwapBytes16( static_cast< Uint16 >( headerInfo.m_payloadLength ) );
		i += sizeof( Uint16 );
	}
	else
	{
		b[i] |= (WebSocketHeaderInfo::SMALL_PAYLOAD_MAX + 2);
		i += 1;
		*reinterpret_cast< Uint64* >( &b[i] ) = red::SwapBytes64( headerInfo.m_payloadLength );
		i += sizeof( Uint64 );
	}

	return i;
}