/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
 
#pragma once

#include "packetNetworkUtils.h"

namespace red
{
	namespace Network
	{
		namespace HeaderUtils
		{
			namespace Consts
			{
				const Uint8 PacketMagicNumberOneByte = 0x40; // Magic number is 4x this number
				const Uint32 PacketMagicNumber = 0x40404040;
			}

			struct Header
			{
				Uint32 m_magic;
				Uint32 m_contentSize;
				Uint32 m_contentCrc;

				typedef Uint32 HeaderCrc;
				HeaderCrc m_headerCrc; // This must be the last property of header
			};

			RED_INLINE Uint32 CalculateContentCrc( const red::UniqueBuffer& contentBuffer )
			{
				return CalculateHash32( contentBuffer.Get(), contentBuffer.GetSize() );
			}

			RED_INLINE Header::HeaderCrc CalculateHeaderCrc( Header& header )
			{
				return CalculateHash32( static_cast<void*>(&header), sizeof(Header) - sizeof(Header::HeaderCrc) );
			}

			RED_INLINE HeaderUtils::Header CreatePacketHeaderFor( const Utils::Packet& packet )
			{
				Header header;
				header.m_magic = Consts::PacketMagicNumber;
				header.m_contentSize = packet.Size();
				header.m_contentCrc = CalculateContentCrc( packet.GetBuffer() );
				header.m_headerCrc = CalculateHeaderCrc( header );
				return header;
			}
		}
	}
}
