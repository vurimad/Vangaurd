/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "network.h"
#include "packetNetworkPacketParser.h"
#include "packetNetworkPacketHeader.h"

#include "../../redSystem/include/log.h"

namespace red
{
	namespace Network
	{
		PacketNetworkPacketParser::PacketNetworkPacketParser()
			: m_headerBuffer( CreatePacketBuffer( sizeof( m_currentHeader) ) )
			, m_currentHeaderSize( 0 )
			, m_currentContentSize( 0 )
			, m_parsingState( EParsingState::ParsingHeaderState )
		{
		}

		void PacketNetworkPacketParser::AppendAndParse( const void* data, Uint32 size, PacketArray& outPackets )
		{
			const Uint8* bytes = ( const Uint8* ) data;
			ProcessParsingState( bytes, size, outPackets );
		}

		void PacketNetworkPacketParser::ProcessParsingState( const Uint8* bytes, Uint32 size, PacketArray& outPackets )
		{
			while ( size > 0 )
			{
				switch ( m_parsingState )
				{
					case EParsingState::ParsingHeaderState:
					{
						if ( TryToParseHeader( bytes, size ) )
						{
							m_parsingState = EParsingState::ParsingContentState;
						}
						break;
					}
					case EParsingState::ParsingContentState:
					{
						if ( TryToParseContent( bytes, size, outPackets ) )
						{
							m_parsingState = EParsingState::ParsingHeaderState;
						}
						break;
					}
					default:
					{
						RED_FATAL( "Invalid parsing state" );
					}
				}
			}
		}

		Bool PacketNetworkPacketParser::TryToParseHeader( const Uint8*& data, Uint32& size )
		{
			Uint32 copySize = std::min( ( (Uint32)sizeof( m_currentHeader) - m_currentHeaderSize ), size );
			red::Memcpy( ( ( Uint8* )m_headerBuffer.Get() ) + m_currentHeaderSize, data, copySize );

			data += copySize;
			size -= copySize; 
			m_currentHeaderSize += copySize;

			// Parse header and it's crc
			if ( m_currentHeaderSize == sizeof( m_currentHeader ) )
			{
				red::Memcpy( &m_currentHeader, m_headerBuffer.Get(), sizeof( m_currentHeader ) );
				m_currentHeaderSize = 0;

				// Validate header crc
				if ( m_currentHeader.m_headerCrc == CalculateHeaderCrc( m_currentHeader ) )
				{
					m_currentContentSize = 0;
					m_contentBuffer = CreatePacketBuffer( m_currentHeader.m_contentSize );
					return true;
				}
				else
				{
					RED_LOG_WARNING( "Network: [PacketNetwork for channels] Wrong header CRC." );
				}
			}

			return false;
		}

		Bool PacketNetworkPacketParser::TryToParseContent( const Uint8*& data, Uint32& size, PacketArray& outPackets )
		{
			Uint32 copySize = std::min( ( m_currentHeader.m_contentSize - m_currentContentSize ), size );
			red::Memcpy( ( ( Uint8* )m_contentBuffer.Get() ) + m_currentContentSize, data, copySize );

			data += copySize;
			size -= copySize; 
			m_currentContentSize += copySize;

			if ( m_currentContentSize == m_currentHeader.m_contentSize )
			{
				if( m_currentHeader.m_contentCrc == HeaderUtils::CalculateContentCrc( m_contentBuffer ) )
				{
					outPackets.EmplaceBack( Utils::Packet( std::move( m_contentBuffer ) ) );
				}
				else
				{
					RED_LOG_WARNING( "Network: [PacketNetwork for channels] Wrong content CRC." );
					m_contentBuffer.Reset();
				}
				return true;
			}

			return false;
		}

	}	// Network
}	// Red
