/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packetNetworkPacketHeader.h"
#include "packetNetworkUtils.h"

namespace red
{
	namespace Network
	{
		typedef red::DynArray< Utils::Packet > PacketArray;

		class REDNETWORK_API PacketNetworkPacketParser
		{
		public:
			PacketNetworkPacketParser();
			
			enum class EParsingState : Uint8
			{
				ParsingHeaderState,
				ParsingContentState
			};

			// Append data to parser and return parsed packets (if any)
			RED_MOCKABLE void AppendAndParse( const void* data, Uint32 size, PacketArray& outPackets );

		private:
			RED_MOCKABLE void ProcessParsingState( const Uint8* data, Uint32 size, PacketArray& outPackets );

			// Return true if the stage is finished
			RED_MOCKABLE Bool TryToParseHeader( const Uint8*& data, Uint32& size );
			RED_MOCKABLE Bool TryToParseContent( const Uint8*& data, Uint32& size, PacketArray& outPackets );

			HeaderUtils::Header											m_currentHeader;

			red::UniqueBuffer											m_headerBuffer;
			Uint32														m_currentHeaderSize;
			red::UniqueBuffer											m_contentBuffer;
			Uint32														m_currentContentSize;
			EParsingState												m_parsingState;
		};
	}	// Network
}	// Red
