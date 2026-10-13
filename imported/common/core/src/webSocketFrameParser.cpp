/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "webSocketFrameParser.h"
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

CWebSocketFrameParser::CWebSocketFrameParser( const CallbackToken& token )
	: m_callbackToken( token )
	, m_decodeErrorFlag( false )
{
	RED_FATAL_ASSERT( token.m_onControlFrame );
	RED_FATAL_ASSERT( token.m_onDataFrame );

	if (m_callbackToken.m_dataPayloadBuffer && m_callbackToken.m_dataPayloadBufferSize > 0)
	{
		// Clear in case overread in the future
		red::Memzero( m_callbackToken.m_dataPayloadBuffer, m_callbackToken.m_dataPayloadBufferSize );
	}
	red::Memzero( &m_controlFramePayloadDecodeBuffer, sizeof( m_controlFramePayloadDecodeBuffer ) );
}

namespace websocketsHelper
{
	static constexpr Bool IsControlFrame( Uint8 opcode )
	{
		return (opcode & OPCODE_CONTROL_BIT) != 0;
	}

	static constexpr Bool IsKnownOpcode( Uint8 opcode )
	{
		return	opcode == WebSocketHeaderInfo::eOpcode_ContinuationFrame ||
				opcode == WebSocketHeaderInfo::eOpcode_TextFrame ||
				opcode == WebSocketHeaderInfo::eOpcode_BinaryFrame ||
				opcode == WebSocketHeaderInfo::eOpcode_Close ||
				opcode == WebSocketHeaderInfo::eOpcode_Ping ||
				opcode == WebSocketHeaderInfo::eOpcode_Pong;
	}

	class DataStreamGuard
	{
	public:
		DataStreamGuard( const void* data, Uint32 size )
			: m_data( static_cast<const Uint8*>( data ) )
			, m_size( size )
		{
		}

		~DataStreamGuard()
		{
			RED_FATAL_ASSERT( m_size == 0 );
		}

		DataStreamGuard( const DataStreamGuard& ) = delete;
		DataStreamGuard& operator=( const DataStreamGuard& ) = delete;
		Uint32 GetBytesAvailable() const { return m_size; }
		const Uint8* Consume( Uint32 size )
		{
			RED_FATAL_ASSERT( size > 0 && size <= m_size );
			const Uint8* ret = m_data;
			m_data += size;
			m_size -= size;
			return ret;
		}

		void Clear() { m_size = 0; }

	private:
		const Uint8* m_data;
		Uint32 m_size;
	};

	static RED_INLINE Bool EnsureDataSize( DataStreamGuard& stream, Uint32 requiredSize, Uint8* output, Uint32& outputSize )
	{
		if (outputSize < requiredSize)
		{
			const Uint32 bytesNeeded = requiredSize - outputSize;
			const Uint32 n = std::min( bytesNeeded, stream.GetBytesAvailable() );
			if (bytesNeeded > 0)
			{
				red::Memcpy( output + outputSize, stream.Consume( n ), n );
				outputSize += n;
			}
			return outputSize >= requiredSize;
		}
		return true;
	}
}

CWebSocketFrameParser::EResult CWebSocketFrameParser::ProcessControlFrame( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& headerInfo, const WebSocketMaskingKey& maskingKey )
{
	if (!headerInfo.m_final)
	{
		RED_LOG_ERROR( "Invalid WebSocket frame: control frames must not be fragmented" );
		return EResult::Invalid;
	}
	if (headerInfo.m_payloadLength > WebSocketHeaderInfo::SMALL_PAYLOAD_MAX)
	{
		RED_LOG_ERROR( "Invalid WebSocket frame: control frames must not exceed 125 bytes, payload %lu bytes", headerInfo.m_payloadLength );
		return EResult::Invalid;
	}

	if (!DecodeControlPayload( stream, maskingKey, static_cast< Uint32 >( headerInfo.m_payloadLength )))
	{
		return EResult::Continue;
	}

	m_headerState = BufferState();
	m_controlFramePayloadState = BufferState();

	RED_FATAL_ASSERT( headerInfo.m_payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX, "Should have already validated payloadLength!" );
	const EResult cbResult = m_callbackToken.m_onControlFrame( m_callbackToken, headerInfo, m_controlFramePayloadDecodeBuffer );
	if ( cbResult != EResult::Continue )
	{
		return cbResult;
	}

	// Clear in case overread in the future
	red::Memzero( &m_controlFramePayloadDecodeBuffer, sizeof( m_controlFramePayloadDecodeBuffer ) );

	return EResult::Continue;
}

CWebSocketFrameParser::EResult CWebSocketFrameParser::ProcessDataFrame( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& headerInfo, const WebSocketMaskingKey& maskingKey )
{
	if (m_payloadState.m_isFragmented)
	{
		if (headerInfo.m_opCode != WebSocketHeaderInfo::eOpcode_ContinuationFrame)
		{
			RED_LOG_ERROR( "Invalid WebSocket frame: expected opcode zero for continuation frame, received opcode: %u", headerInfo.m_opCode );
			return EResult::Invalid;
		}

		// #tbd: at least, it doesn't make sense for any known extension
		if ( headerInfo.m_rsv1 || headerInfo.m_rsv2 || headerInfo.m_rsv3 )
		{
			RED_LOG_ERROR( "Invalid WebSocket frame: reserved bits set in continuation frame: RSV1=%d, RSV2=%d, RSV3=%d", 
						   headerInfo.m_rsv1, headerInfo.m_rsv2, headerInfo.m_rsv3 );
			return EResult::Invalid;
		}
	}
	else
	{
		if (headerInfo.m_opCode == WebSocketHeaderInfo::eOpcode_ContinuationFrame)
		{
			RED_LOG_ERROR( "Invalid WebSocket frame: unexpected opcode zero in non-fragmented frame" );
			return EResult::Invalid;
		}

		if (!headerInfo.m_final)
		{
			if (headerInfo.m_opCode != WebSocketHeaderInfo::eOpcode_TextFrame &&
				 headerInfo.m_opCode != WebSocketHeaderInfo::eOpcode_BinaryFrame)
			{
				RED_LOG_ERROR( "Invalid WebSocket frame: only text or binary frames can be fragmented" );
				return EResult::Invalid;
			}
			m_payloadState.m_isFragmented = true;
		}
		m_payloadState.m_opcode = headerInfo.m_opCode;
	}

	if (headerInfo.m_payloadLength > m_callbackToken.m_dataPayloadBufferSize)
	{
		return EResult::TooLarge;
	}

	if (!DecodePayload( stream, maskingKey, static_cast< Uint32 >( headerInfo.m_payloadLength )))
	{
		return EResult::Continue;
	}
	m_payloadState.m_bufferDefragPos += static_cast<Uint32>( headerInfo.m_payloadLength );
	if (!headerInfo.m_final)
	{
		return EResult::Continue;
	}

	WebSocketHeaderInfo defragHeaderInfo;
	red::Memzero( &defragHeaderInfo, sizeof( defragHeaderInfo ) );
	{
		defragHeaderInfo.m_final = true;
		defragHeaderInfo.m_rsv1 = m_payloadState.m_rsv1;
		defragHeaderInfo.m_rsv2 = m_payloadState.m_rsv2;
		defragHeaderInfo.m_rsv3 = m_payloadState.m_rsv3;
		defragHeaderInfo.m_opCode = m_payloadState.m_opcode;
		defragHeaderInfo.m_mask = true;
		defragHeaderInfo.m_payloadLength = m_payloadState.m_bufferDefragPos;
	}

	// Reset
	m_headerState = BufferState();
	m_payloadState = PayloadState();

	const EResult cbResult = m_callbackToken.m_onDataFrame( m_callbackToken, defragHeaderInfo );
	if ( cbResult != EResult::Continue )
	{
		return cbResult;
	}

	if (m_callbackToken.m_dataPayloadBuffer && m_callbackToken.m_dataPayloadBufferSize > 0)
	{
		// Clear in case overread in the future
		red::Memzero( m_callbackToken.m_dataPayloadBuffer, m_callbackToken.m_dataPayloadBufferSize );
	}

	return EResult::Continue;
}

CWebSocketFrameParser::EResult CWebSocketFrameParser::Decode( const void* data, Uint32 size )
{
	if ( m_decodeErrorFlag )
	{
		return EResult::Invalid;
	}

	websocketsHelper::DataStreamGuard stream( data, size );
	while ( stream.GetBytesAvailable() != 0 )
	{
		WebSocketHeaderInfo headerInfo;
		WebSocketMaskingKey maskingKey;
		if ( !DecodeHeader( stream, headerInfo, maskingKey ) )
		{
			return EResult::Continue;
		}
		if ( m_decodeErrorFlag )
		{
			stream.Clear();
			return EResult::Invalid;
		}

		if ( !websocketsHelper::IsKnownOpcode( headerInfo.m_opCode ) )
		{
			RED_LOG_ERROR( "Invalid WebSocket frame: unknown opcode %u", headerInfo.m_opCode );
			m_decodeErrorFlag = true;
			stream.Clear();
			return EResult::Invalid;
		}

		const EResult ret = websocketsHelper::IsControlFrame( headerInfo.m_opCode ) ? 
			ProcessControlFrame( stream, headerInfo, maskingKey ) : ProcessDataFrame( stream, headerInfo, maskingKey );

		if ( ret != EResult::Continue )
		{
			m_decodeErrorFlag = true;
			stream.Clear();
			return ret;
		}
	}

	return EResult::Continue;
}

Bool CWebSocketFrameParser::DecodePayload( websocketsHelper::DataStreamGuard& stream, const WebSocketMaskingKey& key, Uint32 payloadLength )
{
	Uint32& bufferPosRef = m_payloadState.m_decodeBufferPos;
	Uint8* decodePayloadBuffer = reinterpret_cast< Uint8* >( m_callbackToken.m_dataPayloadBuffer );
	const Uint32 bytesNeeded = payloadLength - bufferPosRef;
	if ( bytesNeeded > 0 )
	{
		Uint8* defragBuffer = &decodePayloadBuffer[ bufferPosRef + m_payloadState.m_bufferDefragPos ];
		const Uint32 n = std::min( bytesNeeded, stream.GetBytesAvailable() );
		const Uint8* const maskedBytes = stream.Consume( n );
		const Uint32 maskOffset = ( bufferPosRef & 3 ); // keymask index global to entire frame data, not relative to new data
		for ( Uint32 i = 0; i < n; ++i )
		{
			defragBuffer[ bufferPosRef++ ] = maskedBytes[i] ^ key.m_data[ ( i + maskOffset ) & 3 ];
		}
	}
	return bufferPosRef == payloadLength; // finished?
}

Bool CWebSocketFrameParser::DecodeControlPayload( websocketsHelper::DataStreamGuard& stream, const WebSocketMaskingKey& key, Uint32 payloadLength )
{
	Uint32& bufferPosRef = m_controlFramePayloadState.m_decodeBufferPos;
	const Uint32 bytesNeeded = payloadLength - bufferPosRef;
	if (bytesNeeded > 0)
	{
		const Uint32 n = std::min( bytesNeeded, stream.GetBytesAvailable() );
		const Uint8* const maskedBytes = stream.Consume( n );
		const Uint32 maskOffset = ( bufferPosRef & 3 ); // keymask index global to entire frame data, not relative to new data
		for (Uint32 i = 0; i < n; ++i)
		{
			m_controlFramePayloadDecodeBuffer[ bufferPosRef++ ] = maskedBytes[i] ^ key.m_data[ ( i + maskOffset ) & 3 ];
		}
	}
	return bufferPosRef == payloadLength; // finished?
}

Bool CWebSocketFrameParser::DecodeHeader( websocketsHelper::DataStreamGuard& stream, WebSocketHeaderInfo& outHeaderInfo, WebSocketMaskingKey& outMaskingKey )
{
	Uint32& bufferPosRef = m_headerState.m_decodeBufferPos;
	if (!websocketsHelper::EnsureDataSize( stream, WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES, m_headerDecodeBuffer, bufferPosRef ))
	{
		return false;
	}

	Uint64 payloadLength = 0;
	Uint32 maskingKeyOffset = 0;
	const Uint8 smallPayloadLen = m_headerDecodeBuffer[ 1 ] & SMALL_PAYLOAD_MASK;
	if (smallPayloadLen <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX)
	{
		payloadLength = smallPayloadLen;
		maskingKeyOffset = WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES;
	}
	else if (smallPayloadLen == WebSocketHeaderInfo::SMALL_PAYLOAD_MAX + 1) // magic size which indicates to look at the next two bytes
	{
		if (!websocketsHelper::EnsureDataSize( stream, WebSocketHeaderInfo::MEDIUM_MESSAGE_HEADER_BYTES, m_headerDecodeBuffer, bufferPosRef ))
		{
			return false;
		}
		payloadLength = red::SwapBytes16( *reinterpret_cast<Uint16*>( &m_headerDecodeBuffer[ 2 ] ) );
		if (payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX)
		{
			RED_LOG_ERROR( "Invalid WebSocket header decoded: payloadLength %lu, but using 2-byte extended size", payloadLength );
			red::Memzero( &outHeaderInfo, sizeof( outHeaderInfo ) );
			m_decodeErrorFlag = true;
			return true;
		}

		maskingKeyOffset = WebSocketHeaderInfo::MEDIUM_MESSAGE_HEADER_BYTES;
	}
	else // magic size (127) indiciates to look at the next eight bytes
	{
		if (!websocketsHelper::EnsureDataSize( stream, WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES, m_headerDecodeBuffer, bufferPosRef ))
		{
			return false;
		}
		payloadLength = red::SwapBytes64( *reinterpret_cast<Uint64*>( &m_headerDecodeBuffer[ 2 ] ) );
		if (payloadLength <= WebSocketHeaderInfo::MEDIUM_PAYLOAD_MAX)
		{
			RED_LOG_ERROR( "Invalid WebSocket header decoded: payloadLength %lu, but using 8-byte extended size", payloadLength );
			red::Memzero( &outHeaderInfo, sizeof( outHeaderInfo ) );
			m_decodeErrorFlag = true;
			return true;
		}

		// The RFC specifies that the high bit must be clear, thus limiting the maximum size
		if (payloadLength > INT64_MAX)
		{
			RED_LOG_ERROR( "Invalid WebSocket header decoded: payloadLength %lu exceeds INT64_MAX", payloadLength );
			red::Memzero( &outHeaderInfo, sizeof( outHeaderInfo ) );
			m_decodeErrorFlag = true;
			return true;
		}

		maskingKeyOffset = WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES;
	}

	Bool isMasked = ( m_headerDecodeBuffer[ 1 ] & MASK_BIT ) != 0;
	if (isMasked)
	{
		const Uint32 numHeaderBytesWithMaskingKey = bufferPosRef + WebSocketHeaderInfo::MASKING_KEY_BYTES;
		if (!websocketsHelper::EnsureDataSize( stream, numHeaderBytesWithMaskingKey, m_headerDecodeBuffer, bufferPosRef ))
		{
			return false;
		}
		red::Memcpy( outMaskingKey.m_data, &m_headerDecodeBuffer[ maskingKeyOffset ], sizeof( outMaskingKey ) );
		if ( *reinterpret_cast< const Uint32* >( outMaskingKey.m_data ) == 0 )
		{
			RED_LOG_WARNING( "WebSocket masking key all zero. Not very entropic! Likely error or poorly implemented WebSocket client");
		}
	}
	else
	{
		RED_LOG_ERROR( "Invalid WebSocket header decoded: client frames must be masked" );
		red::Memzero( &outHeaderInfo, sizeof( outHeaderInfo ) );
		m_decodeErrorFlag = true;
		return true;
	}

	const Uint8* hdr = m_headerDecodeBuffer;
	outHeaderInfo.m_final = ( hdr[ 0 ] & FIN_BIT ) != 0;
	outHeaderInfo.m_rsv1 = ( hdr[ 0 ] & RSV1_BIT ) != 0;
	outHeaderInfo.m_rsv2 = ( hdr[ 0 ] & RSV2_BIT ) != 0;
	outHeaderInfo.m_rsv3 = ( hdr[ 0 ] & RSV3_BIT ) != 0;
	outHeaderInfo.m_opCode = ( hdr[ 0 ] & OPCODE_MASK ) != 0;
	outHeaderInfo.m_mask = isMasked;
	outHeaderInfo.m_payloadLength = payloadLength;

	return true;
}
