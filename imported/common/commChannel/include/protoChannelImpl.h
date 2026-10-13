/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "protoChannel.h"
#include "channelConnection.h"

class ProtoReaderBinary;
class ProtoWriterBinary;

namespace comm
{
	struct MessageFlags
	{
		enum type : Uint8
		{
			FireAndForget = 0x1,
			NeedsAck = 0x2,
			Ack = 0x4,
		};
	};

	struct ResponseDest;
	class Message;
	class ProtoResponseImpl : public IProtoResponse
	{
	public:
		ProtoResponseImpl( const ProtoResponseImpl& other );
		ProtoResponseImpl( const ResponseDest& responseDest, Uint16 handle, Uint8 flags );
		ProtoResponseImpl( const ResponseDest& responseDest );

		virtual void Send( const Message& response ) override;
		virtual red::UniquePtr< IProtoResponse > Clone() const override;

	private:
		Uint16 m_handle;
		Uint8 m_flags;

		red::Network::IPacketNetworkManager& m_network;
		red::Network::PacketConnectionID m_connectionID;
	};

	// IChannel interface implementation with TCP protocol
	class ProtoChannelImpl : public IProtoChannel, public red::NonCopyable
	{
	public:
		ProtoChannelImpl( red::UniquePtr< IChannelConnection > connection, const String& name );
		virtual ~ProtoChannelImpl() override;

		virtual bool IsOpen() const override;
		virtual bool IsConnected() const override;

		virtual const String& GetName() const override;

		virtual void Send( const comm::Message& message ) override;
		virtual void SendAck( const comm::Message& message, MessageCallback callback ) override;
		virtual const MessageSharedPtr SendAndWait( const comm::Message& message, Uint32 timeoutMs ) override;

		virtual void RegisterListener( IProtoMessageListener* listener ) override;
		virtual void UnregisterListener( IProtoMessageListener* listener ) override;

	private:
		Bool OnPacket( ChannelData packet, const ResponseDest& responseDest );

		Bool OnFireAndForget( ProtoReaderBinary& reader, const ResponseDest& responseDest );
		Bool OnNeedsAck( ProtoReaderBinary& reader, const ResponseDest& responseDest );
		Bool OnAck( ProtoReaderBinary& reader);

		Bool DispatchMessageToListeners( MessageSharedPtr& message, IProtoResponse& response );

		void DoSend( red::UniqueBuffer buffer );

		red::UniquePtr< IChannelConnection > m_connection;
		String m_name;
		MessageHandle m_nextMessageHandle;

		red::Mutex m_listenersLock;
		red::DynArray< IProtoMessageListener* > m_listeners;

		red::Mutex m_callbacksLock;
		red::HashMap< MessageHandle, MessageCallback > m_callbacks;
	};
}
