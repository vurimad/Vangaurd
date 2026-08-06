/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "channelConnectionImpl.h"
#include "channelFactory.h"
#include "protoChannelImpl.h"
#include "rawChannelImpl.h"

namespace comm
{
	ChannelFactory::ChannelFactory( red::Network::IPacketNetworkManager& network )
		: m_network( network )
	{
	}

	red::UniquePtr< IProtoChannel > ChannelFactory::CreateListenerProtoChannel( const String& name, const red::Network::Utils::CommAddress& address, Bool reuseAddress )
	{
		red::UniquePtr< ChannelConnection > connection = red::CreateUniquePtr< ChannelConnection > ( m_network );
		red::UniquePtr< IProtoChannel > channel;

		if ( connection->StartListening( address, reuseAddress ) )
		{
			channel = red::CreateUniquePtr< ProtoChannelImpl >( std::move( connection ), name );
		}
		else
		{
			RED_LOG_ERROR( "ChannelFactory: Failed to create proto channel with name \"%hs\" on port %hu.", name.AsChar(), address.GetPort() );
		}

		return channel;
	}

	red::UniquePtr< IProtoChannel > ChannelFactory::OpenProtoChannel( const String& name, const red::Network::Utils::CommAddress& address )
	{
		RED_LOG_DEBUG("Creating [%hs] to [%hs : %u]", name.AsChar(), address.GetHost().AsChar(), address.GetPort());


		red::UniquePtr< ChannelConnection > connection = red::CreateUniquePtr< ChannelConnection > ( m_network );
		red::UniquePtr< IProtoChannel > channel;

		if ( connection->OpenConnection( address ) )
		{
			channel = red::CreateUniquePtr< ProtoChannelImpl >( std::move( connection ), name );
		}
		else
		{
			RED_LOG_ERROR( "ChannelFactory: Failed to open proto channel with name \"%hs\" on host %hs on port %hu.", name.AsChar(), address.GetHost().AsChar(), address.GetPort() );
		}

		return channel;
	}

	red::UniquePtr< IRawChannel > ChannelFactory::CreateListenerRawChannel( const String& name, const red::Network::Utils::CommAddress& address, Bool reuseAddress/* = false*/ )
	{
		red::UniquePtr< ChannelConnection > connection = red::CreateUniquePtr< ChannelConnection > ( m_network );
		red::UniquePtr< IRawChannel > channel;

		if ( connection->StartListening( address, reuseAddress ) )
		{
			channel = red::CreateUniquePtr< RawChannelImpl >( std::move( connection ), name );
		}
		else
		{
			RED_LOG_ERROR( "ChannelFactory: Failed to create raw channel with name \"%hs\" on port %hu.", name.AsChar(), address.GetPort() );
		}

		return channel;
	}

	red::UniquePtr< IRawChannel > ChannelFactory::OpenRawChannel( const String& name, const red::Network::Utils::CommAddress& address )
	{
		red::UniquePtr< ChannelConnection > connection = red::CreateUniquePtr< ChannelConnection > ( m_network );
		red::UniquePtr< IRawChannel > channel;

		if ( connection->OpenConnection( address ) )
		{
			channel = red::CreateUniquePtr< RawChannelImpl >( std::move( connection ), name );
		}
		else
		{
			RED_LOG_ERROR( "ChannelFactory: Failed to open raw channel with name \"%hs\" on host %hs on port %hu.", name.AsChar(), address.GetHost().AsChar(), address.GetPort() );
		}

		return channel;
	}
}