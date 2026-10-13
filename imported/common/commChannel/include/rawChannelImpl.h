/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rawChannel.h"
#include "channelConnection.h"

namespace comm
{
	struct ResponseDest;
	class RawResponseImpl : public IRawResponse, public red::NonCopyable
	{
	public:
		RawResponseImpl( const ResponseDest& responseDest );
		virtual void Send( red::UniqueBuffer buffer ) override;
	private:
		red::Network::IPacketNetworkManager& m_network;
		red::Network::PacketConnectionID m_connectionID;
	};

	class RawChannelImpl : public IRawChannel, public red::NonCopyable
	{
	public:
		RawChannelImpl( red::UniquePtr< IChannelConnection > connection, const String& name );
		virtual ~RawChannelImpl() override;

		virtual bool IsOpen() const override;
		virtual bool IsConnected() const override;

		virtual const String& GetName() const override;

		virtual void Send( red::UniqueBuffer buffer ) override;

		virtual void RegisterListener( IRawMessageListener* listener ) override;
		virtual void UnregisterListener( IRawMessageListener* listener ) override;

	private:
		Bool OnPacket( ChannelData packet, const ResponseDest& responseDest );

		red::UniquePtr< IChannelConnection > m_connection;
		String m_name;

		red::LightMutex m_listenersLock;
		red::DynArray< IRawMessageListener* > m_listeners{ red::PoolBackend() };
	};
}
