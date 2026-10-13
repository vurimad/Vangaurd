/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "protoChannel.h"
#include "rawChannel.h"

namespace red {
	namespace Network {
		class IPacketNetworkManager;

		namespace Utils {
			class CommAddress;
		}
	}
}

namespace comm
{
	class COMMCHANNEL_API ChannelFactory
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ChannelFactory( red::Network::IPacketNetworkManager& network );

		red::UniquePtr< IProtoChannel > CreateListenerProtoChannel( const String& name, const red::Network::Utils::CommAddress& address, Bool reuseAddress = false );
		red::UniquePtr< IProtoChannel > OpenProtoChannel( const String& name, const red::Network::Utils::CommAddress& address );

		red::UniquePtr< IRawChannel > CreateListenerRawChannel( const String& name, const red::Network::Utils::CommAddress& address, Bool reuseAddress = false );
		red::UniquePtr< IRawChannel > OpenRawChannel( const String& name, const red::Network::Utils::CommAddress& address );

	private:
		red::Network::IPacketNetworkManager& m_network;
	};
}
